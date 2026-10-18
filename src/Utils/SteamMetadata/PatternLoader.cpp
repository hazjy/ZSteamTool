#include "PatternLoader.h"
#include "OSTPlatform/include/Memory.h"
#include "OSTPlatform/include/Numbers.h"
#include "OSTPlatform/include/Thread.h"
#include "Utils/Logging/Log.h"
#include "Utils/SteamMetadata/RemoteToml.h"
#include "Utils/SteamMetadata/SteamDiagnostics.h"
#include "Utils/Support/FnvHash.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <toml++/toml.hpp>

// ---- compile-time sanity checks for FNV-1a table keys ----
// If the steam-monitor bot uses the same algorithm these must hold.
static_assert(Fnv1aHash("BBuildAndAsyncSendFrame") == 0x82428E37u,
              "FNV-1a mismatch for BBuildAndAsyncSendFrame");
static_assert(Fnv1aHash("BuildDepotDependency") == 0xC37F2D8Eu,
              "FNV-1a mismatch for BuildDepotDependency");

namespace {

// ---- per-function pattern record ----
struct PatternEntry {
    std::string name;
    uintptr_t   rva = 0;   // 0 = not present in file
    std::string sig;        // empty = not present in file
};

// key = Fnv1aHash(funcName)
using PatternMap = std::unordered_map<uint32_t, PatternEntry>;

// module → its pattern map
static std::unordered_map<OSTPlatform::DynamicLibrary::ModuleHandle, PatternMap> g_moduleMaps;

// Modules whose Load() call failed (popup already shown). FindPattern
// silently returns nullptr for these — without re-logging or adding the
// function to g_missingFunctions — so we don't follow one "TOML missing"
// popup with a second popup listing every dependent hook.
static std::unordered_set<OSTPlatform::DynamicLibrary::ModuleHandle> g_failedModules;

// functions whose names were not found during FindPattern
static std::vector<std::string> g_missingFunctions;

// ---- stale-cache lazy refresh ----
//
// 缓存按客户端 DLL 的 SHA-256 内容寻址，正常启动直接命中、完全不联网（见
// RemoteToml 的 cache-first）。代价是镜像方事后给同一个哈希补过表时我们永远
// 看不到。这里记住"本次的表是从缓存来的"；一旦 FindPattern 有条目查不到，
// 就登记一次懒刷新 —— 每会话每模块最多一次，并且放到分离线程里跑（表不热载，
// 收益在下次启动，绝不能为它拖慢启动）。
struct ModuleSource {
    RemoteToml::Request request;
    bool                fromCache = false;   // 本次的表来自本地缓存
    size_t              entries   = 0;       // 旧表条目数，仅用于日志对比
    std::atomic<uint32_t> misses{0};           // 本会话查不到的条目数
    std::atomic<bool>     refreshNeeded{false};
    std::atomic<bool>     refreshAttempted{false};
};

static std::mutex g_sourceMutex;
static std::unordered_map<OSTPlatform::DynamicLibrary::ModuleHandle,
                          std::unique_ptr<ModuleSource>> g_moduleSources;

// 返回值在进程生命周期内稳定：map 里存的是 unique_ptr，rehash 只搬指针不搬对象。
static ModuleSource* FindSource(OSTPlatform::DynamicLibrary::ModuleHandle module)
{
    std::lock_guard<std::mutex> lock(g_sourceMutex);
    auto it = g_moduleSources.find(module);
    return it == g_moduleSources.end() ? nullptr : it->second.get();
}

// 只有"本次是缓存命中"才值得重取；远程取到的表就是最新的。
static void MarkRefreshNeeded(OSTPlatform::DynamicLibrary::ModuleHandle module)
{
    ModuleSource* src = FindSource(module);
    if (!src || !src->fromCache)
        return;
    src->misses.fetch_add(1, std::memory_order_relaxed);
    src->refreshNeeded.store(true, std::memory_order_relaxed);
}

// ---- byte-pattern scanner ----

static bool ParseSig(const std::string& str,
                     std::vector<uint8_t>& bytes,
                     std::vector<uint8_t>& mask)
{
    bytes.clear();
    mask.clear();
    for (const char* p = str.c_str(); *p; ) {
        if (*p == ' ' || *p == '\t' || *p == ',') { ++p; continue; }
        if (p[0] == '?' && p[1] == '?') {
            bytes.push_back(0); mask.push_back(0); p += 2; continue;
        }
        char hi = p[0], lo = p[1];
        if (!hi || !lo) return false;
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = nib(hi), l = nib(lo);
        if (h < 0 || l < 0) return false;
        bytes.push_back(static_cast<uint8_t>((h << 4) | l));
        mask.push_back(1);
        p += 2;
    }
    return !bytes.empty();
}

static void* ScanModule(OSTPlatform::DynamicLibrary::ModuleHandle module,
                        const std::vector<uint8_t>& bytes,
                        const std::vector<uint8_t>& mask)
{
    const auto image = OSTPlatform::Memory::GetModuleImage(module);
    if (!image) return nullptr;

    auto* base = image->base;
    size_t size = image->size;
    size_t patLen = bytes.size();
    if (size < patLen) return nullptr;

    for (size_t i = 0; i <= size - patLen; ++i) {
        bool found = true;
        for (size_t j = 0; j < patLen; ++j) {
            if (mask[j] && base[i + j] != bytes[j]) { found = false; break; }
        }
        if (found) return base + i;
    }
    return nullptr;
}

// ---- TOML pattern parser ----

// Section keys are hex literals like "0x82428E37"; each section is a table
// with optional `name`, `rva` (hex string), and `sig` (IDA-style bytes).
static PatternMap TableToPatternMap(const toml::table& tbl)
{
    PatternMap map;
    map.reserve(tbl.size());
    for (auto& [rawKey, val] : tbl) {
        if (!val.is_table()) continue;
        auto& sub = *val.as_table();

        const auto parsedKey = OSTPlatform::Numbers::ParseHexUInt32(std::string(rawKey));
        if (!parsedKey) continue;
        const uint32_t hashKey = *parsedKey;

        PatternEntry entry;
        if (auto v = sub["name"].value<std::string>()) entry.name = *v;
        if (auto v = sub["rva"].value<std::string>()) {
            if (const auto rva = OSTPlatform::Numbers::ParseHexUInt64(*v)) {
                entry.rva = static_cast<uintptr_t>(*rva);
            }
        }
        if (auto v = sub["sig"].value<std::string>()) entry.sig = *v;

        map[hashKey] = std::move(entry);
    }
    return map;
}

static PatternMap ParsePatternString(std::string_view body,
                                     std::string* outError = nullptr)
{
    try {
        return TableToPatternMap(toml::parse(body));
    } catch (const toml::parse_error& e) {
        if (outError) *outError = e.description();
        return {};
    }
}

// ---- popup helpers (detached threads so we never block Steam) ----

// Surface a missing pattern file to the user, with enough detail to either
// (a) drop a file in manually, (b) check the upstream repo, or (c) file
// an actionable bug report.  We deliberately only disable hooks for the
// failing module — the rest of OpenSteamTool keeps working.
static void ShowDownloadFailedPopup(const std::string& dllName,
                                    const std::string& sha256,
                                    const std::string& component)
{
    SteamDiagnostics::ShowWarning(
        "内核：未找到该 Steam 版本的签名表",
        "签名文件缺失：" + dllName + "\n\n"
        "依赖 " + dllName + " 的钩子已在本次会话内停用，其它模块不受影响。\n\n"
        "处理方式：\n"
        "  1. 请先重启一次 Steam。内置镜像链会自动重新获取（首选 git.lua.tools，其次 mmxlyo，其余为兜底）。\n"
        "  2. 若 Steam 刚完成更新，请等待镜像方发布对应版本的表（通常需要数小时至一天）。\n"
        "  3. 如需强制重新获取，请在 opensteamtool.toml 的 [remote] 段设置 force_refresh = true，然后重启 Steam。\n"
        "  4. 也可手工放置匹配的表：\n"
        "       <Steam>\\opensteamtool\\pattern\\" + component + "\\" + sha256 + ".toml\n"
        "  5. 反馈与排查：https://github.com/hazjy/ZSteamTool");
}

} // namespace

// ---- public API ----

namespace PatternLoader {

    constexpr const char* kPatternChannel = "pattern";

bool Load(OSTPlatform::DynamicLibrary::ModuleHandle module, const std::string& dllPath, const std::string& component)
{
    namespace fs = std::filesystem;

    // Delegate fetch + cache + mirror fallback to RemoteToml.
    RemoteToml::Request request{ kPatternChannel, component, dllPath };
    RemoteToml::Result r = RemoteToml::Fetch(request);

    std::string parseErr;
    PatternMap map;
    if (r.ok) {
        map = ParsePatternString(r.body, &parseErr);
        if (map.empty() && r.fromCache) {
            // A cached table that parses to nothing is a poisoned cache entry: drop it and
            // refetch exactly once. A *remote* table that parses to nothing is left alone
            // (refetching returns the same bytes) — the popup below reports that case.
            LOG_WARN("PatternLoader: cached TOML for {} parsed empty ({}), discarding and refetching once",
                     component, parseErr.empty() ? "no entries" : parseErr);
            RemoteToml::RemoveCacheEntry(request);
            request.forceNetwork = true;
            r = RemoteToml::Fetch(request);
            if (r.ok)
                map = ParsePatternString(r.body, &parseErr);
        }
        if (!map.empty()) {
            LOG_INFO("PatternLoader: loaded {} patterns for {} ({})",
                     map.size(), component, r.fromCache ? "cache fallback" : "remote");
            g_moduleMaps[module] = std::move(map);

            // 记住来源：只有"本次是缓存命中"时，FindPattern 查不到条目才值得重取。
            // 只登记成功加载的模块 —— 整表失败时 RemoteToml 已经试过全网。
            {
                auto src = std::make_unique<ModuleSource>();
                src->request   = request;
                src->fromCache = r.fromCache;
                src->entries   = g_moduleMaps[module].size();
                std::lock_guard<std::mutex> lock(g_sourceMutex);
                g_moduleSources[module] = std::move(src);
            }
            return true;
        }
        LOG_WARN("PatternLoader: TOML for {} parsed empty ({})",
                 component, parseErr.empty() ? "no entries" : parseErr);
    }

    // Total failure — popup + disable module's hooks.
    std::string dllName = fs::path(dllPath).filename().string();
    std::string sha     = r.sha256.empty() ? "(hash failed)" : r.sha256;
    ShowDownloadFailedPopup(dllName, sha, component);
    g_failedModules.insert(module);
    return false;
}

void* FindPattern(OSTPlatform::DynamicLibrary::ModuleHandle module, const char* funcName)
{
    // If the whole module's pattern file failed to load, stay quiet — the
    // user already saw one popup and the main.log already has the warning.
    // No point amplifying that into one log line per hook plus a second
    // "missing functions" popup later.
    if (g_failedModules.count(module)) {
        return nullptr;
    }

    uint32_t key = Fnv1aHash(funcName);

    auto mapIt = g_moduleMaps.find(module);
    if (mapIt == g_moduleMaps.end()) {
        // Load() was never called for this module.
        LOG_WARN("PatternLoader: FindPattern called for module that was never loaded "
                 "('{}')", funcName);
        g_missingFunctions.emplace_back(funcName);
        return nullptr;
    }

    auto& map = mapIt->second;
    auto entryIt = map.find(key);
    if (entryIt == map.end()) {
        LOG_WARN("PatternLoader: no entry for '{}' (key=0x{:08X})", funcName, key);
        MarkRefreshNeeded(module);
        g_missingFunctions.emplace_back(funcName);
        return nullptr;
    }

    const PatternEntry& entry = entryIt->second;

    // Priority 1: RVA direct offset
    if (entry.rva != 0) {
        void* addr = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(module) + entry.rva);
        LOG_DEBUG("PatternLoader: {} resolved via RVA 0x{:X}", funcName, entry.rva);
        return addr;
    }

    // Priority 2: byte-signature scan
    if (!entry.sig.empty()) {
        std::vector<uint8_t> bytes, mask;
        if (ParseSig(entry.sig, bytes, mask)) {
            void* addr = ScanModule(module, bytes, mask);
            if (addr) {
                uintptr_t rva = reinterpret_cast<uintptr_t>(addr) -
                                reinterpret_cast<uintptr_t>(module);
                LOG_DEBUG("PatternLoader: {} resolved via sig @ RVA 0x{:X}",
                          funcName, rva);
                return addr;
            }
            LOG_WARN("PatternLoader: sig scan miss for '{}' (pattern parsed OK, "
                     "no match in module image)", funcName);
        } else {
            LOG_WARN("PatternLoader: malformed sig for '{}': '{}'",
                     funcName, entry.sig);
        }
    } else {
        LOG_WARN("PatternLoader: entry for '{}' has neither rva nor sig", funcName);
    }

    g_missingFunctions.emplace_back(funcName);
    MarkRefreshNeeded(module);
    return nullptr;
}

void ReportMissingFunctions(bool refreshQueued)
{
    if (g_missingFunctions.empty()) return;

    // Build the list
    std::string list;
    for (const auto& name : g_missingFunctions)
        list += "  - " + name + "\n";
    g_missingFunctions.clear();

    SteamDiagnostics::ShowWarning(
        "内核：部分签名未命中",
        "下列函数未能从签名表中定位：\n" +
        list +
        "\n依赖上述函数的钩子已在本次会话内停用，其它功能不受影响。\n\n" +
        (refreshQueued
            ? "签名表已在后台重新获取（磁盘缓存已更新）。重启一次 Steam 后生效，无需手动处理。\n\n"
            : "") +
        "常见原因：Steam 客户端更新幅度较大（镜像方尚未发布对应版本的表），"
        "或该版本中部分函数的签名已发生变化。\n"
        "反馈与排查：https://github.com/hazjy/ZSteamTool");
}

bool RefreshMissingMetadata()
{
    // 快照 + 占坑都在锁内完成（每会话每模块最多一次），HTTP 在锁外、分离线程里跑。
    struct Target {
        RemoteToml::Request request;
        size_t              entries = 0;
        uint32_t            misses  = 0;
    };
    std::vector<Target> targets;
    {
        std::lock_guard<std::mutex> lock(g_sourceMutex);
        for (auto& [module, src] : g_moduleSources) {
            (void)module;
            if (!src->fromCache) continue;
            if (!src->refreshNeeded.load(std::memory_order_relaxed)) continue;

            bool expected = false;
            if (!src->refreshAttempted.compare_exchange_strong(
                    expected, true, std::memory_order_relaxed)) {
                continue;   // 本会话已经试过，不再重复联网
            }
            targets.push_back({ src->request, src->entries,
                                src->misses.load(std::memory_order_relaxed) });
        }
    }
    if (targets.empty()) return false;

    for (const auto& t : targets) {
        LOG_WARN("PatternLoader: stale cached table for {} is missing {} function(s), "
                 "refreshing once this session", t.request.component, t.misses);
    }

    // 表不热载：这次刷新只写缓存，下次启动才生效。因此必须异步 —— 我们内置
    // 6 条镜像且每条一次 transport 重试，同步跑会给每次启动加一轮全链扫描，
    // 而本会话一点收益都没有。两个模块串行在同一线程里，避免两条并发 HTTP。
    OSTPlatform::Thread::StartDetached(
        [targets = std::move(targets)]() -> uint32_t {
            for (const auto& t : targets) {
                RemoteToml::Request request = t.request;
                request.forceNetwork = true;   // 复用现成的"跳过缓存短路"开关

                LOG_INFO("PatternLoader: refreshing {} table from mirror chain "
                         "(cached table had {} entries)", request.component, t.entries);
                RemoteToml::Result r = RemoteToml::Fetch(request);

                if (!r.ok) {
                    LOG_WARN("PatternLoader: refresh of {} table failed, "
                             "will retry on next launch", request.component);
                    continue;
                }
                if (r.fromCache) {
                    // 镜像也没给出新东西（远程失败回落本地缓存）：会话内不再重试。
                    LOG_WARN("PatternLoader: refresh of {} table fell back to cache, "
                             "upstream has not published a fixed table yet", request.component);
                    continue;
                }

                std::string parseErr;
                const PatternMap fresh = ParsePatternString(r.body, &parseErr);
                if (fresh.empty()) {
                    LOG_WARN("PatternLoader: refreshed {} table parsed empty ({}), "
                             "keeping the cached one", request.component,
                             parseErr.empty() ? "no entries" : parseErr);
                    continue;
                }
                if (fresh.size() > t.entries) {
                    LOG_INFO("PatternLoader: refreshed {} table: {} -> {} entries "
                             "(next launch picks it up)",
                             request.component, t.entries, fresh.size());
                } else {
                    LOG_INFO("PatternLoader: refreshed {} table: {} entries, unchanged "
                             "(upstream has not fixed it yet)",
                             request.component, fresh.size());
                }
            }
            return 0;
        });

    return true;
}

} // namespace PatternLoader
