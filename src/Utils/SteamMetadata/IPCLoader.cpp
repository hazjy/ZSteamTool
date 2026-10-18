#include "IPCLoader.h"
#include "IPCMessages.gen.h"
#include "OSTPlatform/include/Numbers.h"
#include "Utils/Logging/Log.h"
#include "Utils/SteamMetadata/RemoteToml.h"
#include "Utils/SteamMetadata/SteamDiagnostics.h"

#include <unordered_map>
#include <utility>

#include <toml++/toml.hpp>

namespace IPCLoader {

namespace {

    struct Registry {
        std::vector<Interface> interfaces;
        std::unordered_map<EIPCInterface, size_t> byID;
        std::unordered_map<std::string, size_t> byName;

        void Clear()
        {
            interfaces.clear();
            byID.clear();
            byName.clear();
        }

        void Add(Interface iface)
        {
            const size_t index = interfaces.size();
            byID[iface.id] = index;
            byName[iface.name] = index;
            interfaces.push_back(std::move(iface));
        }

        const Method* Find(EIPCInterface interfaceID, uint32_t funcHash) const
        {
            const auto it = byID.find(interfaceID);
            if (it == byID.end()) return nullptr;

            for (const auto& method : interfaces[it->second].methods) {
                if (method.funcHash == funcHash) return &method;
            }
            return nullptr;
        }

        const Method* Find(std::string_view interfaceName,
                           std::string_view methodName) const
        {
            const auto it = byName.find(std::string(interfaceName));
            if (it == byName.end()) return nullptr;

            for (const auto& method : interfaces[it->second].methods) {
                if (method.name == methodName) return &method;
            }
            return nullptr;
        }

        size_t MethodCount() const
        {
            size_t count = 0;
            for (const auto& iface : interfaces)
                count += iface.methods.size();
            return count;
        }
    };

    Registry g_registry;

    // ---- TOML helpers ----

    static bool ParseHexU32(std::string_view s, uint32_t& out)
    {
        const auto parsed = OSTPlatform::Numbers::ParseHexUInt32(s);
        if (!parsed) return false;
        out = *parsed;
        return true;
    }

    static bool ParseInterfaceTable(std::string_view name,
                                    const toml::table& tbl,
                                    Interface& out)
    {
        out.name = std::string(name);

        const auto expected = EIPCInterfaceFromName(name);
        if (!expected) {
            LOG_WARN("IPCLoader: [{}] is not declared in EIPCInterface", name);
            return false;
        }
        out.id = *expected;

        // Backward-compatible metadata: EIPCInterface is authoritative.
        if (auto v = tbl["interface_id"].value<int64_t>()) {
            if (*v < 0 || *v > 0xFF) {
                LOG_WARN("IPCLoader: [{}] interface_id out of range ({})", name, *v);
                return false;
            }
            if (static_cast<EIPCInterface>(*v) != out.id) {
                LOG_WARN("IPCLoader: [{}] interface_id {} disagrees with generated EIPCInterface value {}",
                         name, *v, static_cast<uint8_t>(out.id));
                return false;
            }
        }

        if (auto v = tbl["vtable_rva"].value<std::string>())
            ParseHexU32(*v, out.vtableRva);

        // Walk dotted sub-tables — each is a method.
        for (auto& [methodKey, methodVal] : tbl) {
            if (!methodVal.is_table()) continue;
            const auto& mtbl = *methodVal.as_table();

            Method m;
            m.interfaceID = out.id;
            m.name = std::string(methodKey.str());

            if (auto v = mtbl["funcHash"].value<std::string>()) {
                if (!ParseHexU32(*v, m.funcHash)) {
                    LOG_WARN("IPCLoader: [{}.{}] bad funcHash '{}'",
                             name, m.name, *v);
                    continue;
                }
            } else {
                LOG_WARN("IPCLoader: [{}.{}] missing funcHash", name, m.name);
                continue;
            }

            if (auto v = mtbl["fencepost"].value<std::string>())
                ParseHexU32(*v, m.fencepost);
            if (auto v = mtbl["argc"].value<int64_t>())
                m.argc = static_cast<uint32_t>(*v);
            out.methods.push_back(std::move(m));
        }
        return true;
    }

    static void ShowMissingPopup(const std::string& sha256)
    {
        SteamDiagnostics::ShowWarning(
            "内核：缺少 IPC 表",
            "未找到本版本 Steam 的 IPC 表文件。\n\n"
            "IPC 拦截已在本次会话内停用（票据相关功能不可用）；基于签名的钩子不受影响。\n\n"
            "处理方式：\n"
            "  1. 请先重启一次 Steam，内置镜像链会自动重新获取。\n"
            "  2. 若 Steam 刚完成更新，请等待镜像方发布对应版本的表。\n"
            "  3. 也可手工放置匹配的表：\n"
            "       <Steam>\\opensteamtool\\ipc\\steamclient\\" + sha256 + ".toml\n"
            "  4. 反馈与排查：https://github.com/hazjy/ZSteamTool");
    }

} // namespace

constexpr const char* kIPCChannel = "ipc";

bool Load(const std::string& steamclientPath)
{
    g_registry.Clear();

    RemoteToml::Request request{ kIPCChannel, "steamclient", steamclientPath };
    RemoteToml::Result r = RemoteToml::Fetch(request);

    if (!r.ok) {
        ShowMissingPopup(r.sha256.empty() ? "(hash failed)" : r.sha256);
        return false;
    }

    // Our handlers all live under these two interfaces; if either is missing (or the table
    // parses to zero methods) IPC ticketing is effectively dead and must NOT be reported as
    // success. A cached table in that state is poisoned -> drop it and refetch once; a remote
    // table in that state is simply broken (refetching returns the same bytes).
    auto parseInto = [](const std::string& body, std::string& err) -> bool {
        g_registry.Clear();
        toml::table root;
        try {
            root = toml::parse(body);
        } catch (const toml::parse_error& e) {
            err = e.description();
            return false;
        }
        for (auto& [key, val] : root) {
            if (!val.is_table()) continue;
            Interface iface;
            if (!ParseInterfaceTable(key.str(), *val.as_table(), iface)) continue;
            g_registry.Add(std::move(iface));
        }
        if (MethodCount() == 0) { err = "no methods"; return false; }
        if (Find("IClientUser", "GetSteamID") == nullptr ||
            Find("IClientUtils", "GetAppID") == nullptr) {
            err = "required interfaces missing";
            return false;
        }
        return true;
    };

    std::string parseErr;
    bool parsed = parseInto(r.body, parseErr);
    if (!parsed && r.fromCache) {
        LOG_WARN("IPCLoader: cached TOML unusable ({}), discarding and refetching once", parseErr);
        RemoteToml::RemoveCacheEntry(request);
        request.forceNetwork = true;
        r = RemoteToml::Fetch(request);
        if (!r.ok) {
            ShowMissingPopup(r.sha256.empty() ? "(hash failed)" : r.sha256);
            return false;
        }
        parsed = parseInto(r.body, parseErr);
    }
    if (!parsed) {
        LOG_WARN("IPCLoader: TOML unusable: {}", parseErr);
        ShowMissingPopup(r.sha256);
        return false;
    }

    LOG_INFO("IPCLoader: loaded {} methods across {} interfaces ({})",
             MethodCount(), InterfaceCount(),
             r.fromCache ? "cache fallback" : "remote");
    return true;
}

const Method* Find(EIPCInterface interfaceID, uint32_t funcHash)
{
    return g_registry.Find(interfaceID, funcHash);
}

const Method* Find(std::string_view ifaceName, std::string_view methodName)
{
    return g_registry.Find(ifaceName, methodName);
}

size_t InterfaceCount() 
{ 
    return g_registry.interfaces.size();
}

size_t MethodCount() {
    return g_registry.MethodCount();
}

} // namespace IPCLoader
