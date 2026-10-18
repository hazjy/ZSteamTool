#include "RemoteToml.h"
#include "OSTPlatform/include/Http.h"
#include "Utils/Config/Config.h"
#include "Utils/Logging/Log.h"
#include "Utils/SteamMetadata/SteamDiagnostics.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace RemoteToml {

namespace {
    // Built-in mirror chain, tried in order. Used when [remote] url_template is unset.
    // Mirrors are independent repos/hosts (not just CDN copies of one repo).
    // Order matters: the first entry is the one that actually resolves today, and every
    // component pays the timeout of each dead mirror ahead of it. Keep a source that is
    // confirmed reachable first; the rest stay as fallbacks.
    constexpr const char* kDefaultTemplates[] = {
        "https://git.lua.tools/luatools/steam-monitor/raw/branch/{channel}/{component}/{sha256}.toml",
        // Second live source: luatools is the auto-tracking mirror; mmxlyo is the active
        // fork's own table repo (verified still updating, 2026-10-14).
        "https://raw.githubusercontent.com/mmxlyo/steam-monitor/{channel}/{component}/{sha256}.toml",
        "https://raw.githubusercontent.com/OpenSteam001/steam-monitor/{channel}/{component}/{sha256}.toml",
        "https://cdn.jsdelivr.net/gh/OpenSteam001/steam-monitor@{channel}/{component}/{sha256}.toml",
        "https://raw.githubusercontent.com/madoiscool/steam-monitor/{channel}/{component}/{sha256}.toml",
        "https://cdn.jsdelivr.net/gh/madoiscool/steam-monitor@{channel}/{component}/{sha256}.toml",
    };

    static bool HasPlaceholder(std::string_view text, std::string_view placeholder)
    {
        return text.find(placeholder) != std::string_view::npos;
    }

    static bool IsValidTemplate(std::string_view urlTemplate)
    {
        return HasPlaceholder(urlTemplate, "{channel}") &&
               HasPlaceholder(urlTemplate, "{component}") &&
               HasPlaceholder(urlTemplate, "{sha256}");
    }

    static void ReplaceAll(std::string& text,
                           std::string_view from,
                           std::string_view to)
    {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    static std::string ExpandTemplate(std::string urlTemplate,
                                      const Request& request,
                                      std::string_view sha256)
    {
        ReplaceAll(urlTemplate, "{channel}", request.channel);
        ReplaceAll(urlTemplate, "{component}", request.component);
        ReplaceAll(urlTemplate, "{sha256}", sha256);
        return urlTemplate;
    }

    static std::vector<std::string> BuildUrlTemplates()
    {
        const std::vector<std::string> configured = Config::GetRemoteUrlTemplates();
        if (configured.empty())
            return { std::begin(kDefaultTemplates), std::end(kDefaultTemplates) };

        // A configured list REPLACES the built-ins; keep only valid entries.
        std::vector<std::string> out;
        for (const std::string& tmpl : configured) {
            if (IsValidTemplate(tmpl))
                out.push_back(tmpl);
            else
                LOG_WARN("RemoteToml: remote.url_template entry missing "
                         "{{channel}}/{{component}}/{{sha256}}, skipping: {}", tmpl);
        }
        if (out.empty())
            LOG_WARN("RemoteToml: no valid remote.url_template entries; remote fetch disabled");
        return out;
    }
} // namespace

Result Fetch(const Request& request)
{
    namespace fs = std::filesystem;
    Result out;

    // 1. SHA-256 of the DLL.
    const auto hashStart = std::chrono::steady_clock::now();
    out.sha256 = SteamDiagnostics::Sha256Of(request.dllPath);
    const auto hashMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - hashStart).count();

    if (out.sha256.empty()) {
        LOG_WARN("RemoteToml({}/{}): Sha256OfFile failed for {} ({} ms)",
                 request.channel, request.component, request.dllPath, hashMs);
        return out;
    }
    LOG_INFO("RemoteToml({}/{}): sha256 = {} ({} ms)",
             request.channel, request.component, out.sha256, hashMs);

    // 2. Cache path & dir.
    fs::path steamRoot = fs::path(request.dllPath).parent_path();
    fs::path cacheDir  = steamRoot / "opensteamtool" / request.channel / request.component;
    fs::path cachePath = cacheDir / (out.sha256 + ".toml");
    const std::string cachePathText = cachePath.string();

    std::error_code mkdirEc;
    fs::create_directories(cacheDir, mkdirEc);
    if (mkdirEc) {
        LOG_WARN("RemoteToml({}/{}): could not create cache dir {} ({})",
                 request.channel, request.component, cacheDir.string(), mkdirEc.message());
    }

    // 3. Cache first. The URL carries the DLL's SHA-256, so the cache is content-addressed
    //    and a cached table for this exact hash is equivalent to the remote one. Hitting it
    //    skips the whole mirror chain — which used to cost 49-76 s per Steam start because
    //    two dead mirrors each burn a full HTTP timeout, per component, every time.
    //    Network is only needed when the hash changes (client update), on first install,
    //    or when [remote] force_refresh is set.
    const bool forceRefresh = Config::GetRemoteForceRefresh() || request.forceNetwork;
    if (!forceRefresh && fs::exists(cachePath)) {
        std::ifstream ifs(cachePath, std::ios::binary);
        if (ifs) {
            std::string cached((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
            if (!cached.empty()) {
                LOG_INFO("RemoteToml({}/{}): cache hit {} ({} bytes), skipping network",
                         request.channel, request.component, cachePathText, cached.size());
                out.body = std::move(cached);
                out.ok = true;
                out.fromCache = true;
                return out;
            }
            LOG_WARN("RemoteToml({}/{}): cache file is empty, refetching: {}",
                     request.channel, request.component, cachePathText);
        } else {
            LOG_WARN("RemoteToml({}/{}): cache file unreadable, refetching: {}",
                     request.channel, request.component, cachePathText);
        }
    }

    // 4. Try remote (mirror chain; a non-200 on one mirror falls through to the next).
    const std::vector<std::string> urlTemplates = BuildUrlTemplates();
    OSTPlatform::Http::Result http;
    std::string lastUrl;

    size_t notFoundCount = 0;
    for (size_t i = 0; i < urlTemplates.size(); ++i) {
        lastUrl = ExpandTemplate(urlTemplates[i], request, out.sha256);
        LOG_INFO("RemoteToml({}/{}): downloading {}",
                 request.channel, request.component, lastUrl);

        http = OSTPlatform::Http::Execute(L"GET", lastUrl.c_str(),
                                          nullptr, 0, nullptr);

        if (http.ok && http.status == 200) break;

        if (http.ok && http.status == 404) {
            ++notFoundCount;
        } else {
            // Transport failure (timeout / reset): a cross-border link hiccup is common,
            // so retry this mirror once. 404 is not retried (refetching gets the same
            // answer). One retry per mirror, never a loop, so a broken network cannot
            // burn the startup path.
            LOG_DEBUG("RemoteToml({}/{}): transport failed ({} ok={} HTTP={}), retrying once",
                      request.channel, request.component, lastUrl, http.ok, http.status);
            http = OSTPlatform::Http::Execute(L"GET", lastUrl.c_str(),
                                              nullptr, 0, nullptr);
            if (http.ok && http.status == 404) ++notFoundCount;
            if (http.ok && http.status == 200) break;
        }

        // Mirrors are independent repos with possibly different coverage, so a 404
        // (or any non-200) on one does NOT imply the others lack the file — always
        // fall through to the next mirror. A miss on one mirror is normal operation
        // (the built-in chain has sources that are known stale), so it is DEBUG here
        // and only the whole-chain failure below is a WARN.
        LOG_DEBUG("RemoteToml({}/{}): mirror #{} failed ({} ok={} HTTP={}), trying next",
                  request.channel, request.component, i + 1, lastUrl, http.ok, http.status);
    }

    // Distinguish "all mirrors 404" (upstream really has not published this table — worth
    // telling the user) from "network trouble" (stay quiet and retry next start). A
    // misconfigured template that produced zero attempts is neither.
    out.allNotFound = !urlTemplates.empty() && notFoundCount == urlTemplates.size();

    const bool remoteOk = http.ok && http.status == 200 && !http.body.empty();
    if (!remoteOk) {
        if (out.allNotFound) {
            LOG_WARN("RemoteToml({}/{}): all {} mirror(s) returned 404 "
                     "(upstream has not published this table)",
                     request.channel, request.component, urlTemplates.size());
        } else {
            LOG_WARN("RemoteToml({}/{}): all {} mirror(s) failed (last {} ok={} HTTP={})",
                     request.channel, request.component, urlTemplates.size(),
                     lastUrl.empty() ? "<none>" : lastUrl, http.ok, http.status);
        }
    }

    // 5. Remote OK → write cache atomically (tmp + rename), return body.
    if (remoteOk) {
        const std::string tmpPathText = cachePathText + ".tmp";
        std::ofstream ofs(tmpPathText, std::ios::binary | std::ios::trunc);
        if (ofs) {
            ofs.write(http.body.data(),
                      static_cast<std::streamsize>(http.body.size()));
            ofs.close();
            // Windows 上 rename 不覆盖已存在的文件，先删再改；失败时保留旧缓存。
            std::error_code ec;
            fs::remove(cachePath, ec);
            ec.clear();
            fs::rename(tmpPathText, cachePath, ec);
            if (ec) {
                LOG_WARN("RemoteToml({}/{}): could not move {} into place ({})",
                         request.channel, request.component, tmpPathText, ec.message());
            } else {
                LOG_INFO("RemoteToml({}/{}): cached to {} ({} bytes)",
                         request.channel, request.component, cachePathText, http.body.size());
            }
        } else {
            LOG_WARN("RemoteToml({}/{}): could not open {} for writing",
                     request.channel, request.component, tmpPathText);
        }
        out.body = std::move(http.body);
        out.ok = true;
        return out;
    }

    // 6. Remote failed → fall back to whatever is cached for this exact SHA.
    if (fs::exists(cachePath)) {
        LOG_WARN("RemoteToml({}/{}): remote failed (last URL {} HTTP {}); "
                 "falling back to local cache {}",
                 request.channel, request.component,
                 lastUrl.empty() ? "<none>" : lastUrl, http.status, cachePathText);

        std::ifstream ifs(cachePath, std::ios::binary);
        if (ifs) {
            std::string buf((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
            if (!buf.empty()) {
                out.body = std::move(buf);
                out.ok = true;
                out.fromCache = true;
                return out;
            }
            LOG_WARN("RemoteToml({}/{}): cache file empty: {}",
                     request.channel, request.component, cachePathText);
        } else {
            LOG_WARN("RemoteToml({}/{}): could not open cache file: {}",
                     request.channel, request.component, cachePathText);
        }
    }

    // 6. Total failure — caller handles popup / degraded mode.
    LOG_WARN("RemoteToml({}/{}): no source available (last URL: {} HTTP {})",
             request.channel, request.component,
             lastUrl.empty() ? "<none>" : lastUrl, http.status);
    return out;
}

bool RemoveCacheEntry(const Request& request)
{
    namespace fs = std::filesystem;
    const std::string sha = SteamDiagnostics::Sha256Of(request.dllPath);
    if (sha.empty())
        return false;

    const fs::path steamRoot = fs::path(request.dllPath).parent_path();
    const fs::path cachePath = steamRoot / "opensteamtool" / request.channel
                               / request.component / (sha + ".toml");

    std::error_code ec;
    const bool removed = fs::remove(cachePath, ec);
    if (removed) {
        LOG_WARN("RemoteToml({}/{}): discarded bad cache entry {}, will refetch once",
                 request.channel, request.component, cachePath.string());
    } else if (ec) {
        LOG_WARN("RemoteToml({}/{}): could not remove cache entry {} ({})",
                 request.channel, request.component, cachePath.string(), ec.message());
    }
    return removed;
}

} // namespace RemoteToml
