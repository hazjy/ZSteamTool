#include "dllmain.h"
#include "Hooks_IPC.h"
#include "Hooks_IPC_ISteamUser.h"
#include "Hooks_IPC_ISteamUtils.h"
#include "HookMacros.h"
#include "Hooks_Misc.h"
#include "Pipe/PipeManager.h"
#include "Utils/Support/FnvHash.h"
#include "Utils/SteamMetadata/IPCLoader.h"

#include <cstring>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

    RESOLVE_FUNC(GetPipeClient, CPipeClient*, void* pEngine, HSteamPipe hSteamPipe);

    static CPipeClient* GetPipe(void* pServer, HSteamPipe hSteamPipe) {
        return oGetPipeClient ? oGetPipeClient(pServer, hSteamPipe) : nullptr;
    }

    //  Handler dispatch table
    struct ResolvedHandler {
        EIPCInterface         interfaceID;
        uint32                funcHash;
        std::string           name;       // "IClientUser::GetSteamID" — for logs
        uint32                fencepost;
        uint32                argc;
        IPCHandlerFn          pre;
        IPCHandlerFn          post;

        ResolvedHandler(const IPCHandlerEntry& entry, const IPCLoader::Method& method)
            : interfaceID(method.interfaceID),
              funcHash(method.funcHash),
              name(std::string(entry.interfaceName) + "::" + entry.methodName),
              fencepost(method.fencepost),
              argc(method.argc),
              pre(entry.pre),
              post(entry.post) {}

        std::string DebugString() const {
            return std::format("{} -> hash=0x{:08X} fencepost=0x{:08X} argc={}",
                name, funcHash, fencepost, argc);
        }
    };
    std::vector<ResolvedHandler> g_Handlers;

    static ResolvedHandler* FindHandler(EIPCInterface iface, uint32 funcHash) {
        for (auto& e : g_Handlers) {
            if (e.interfaceID == iface && e.funcHash == funcHash) return &e;
        }
        return nullptr;
    }

    struct IPCDispatch {
        CPipeClient*     pipe = nullptr;
        ResolvedHandler* handler = nullptr;

        bool enabled() const {
            return pipe && handler;
        }

        std::string DebugString() const {
            return std::format("{} {}",pipe ? pipe->DebugString() : "null",
                                handler ? handler->DebugString() : "null");
        }
    };

    static IPCDispatch ResolveDispatch(void* pServer,HSteamPipe hSteamPipe,CUtlBuffer* pRead)
    {
        IPCDispatch dispatch{};
        dispatch.pipe = GetPipe(pServer, hSteamPipe);
        if (!dispatch.pipe) return dispatch;

        // We only care about InterfaceCall messages
        IPCMessages::IPCRequest request{pRead};
        if (!request.ok()) return dispatch;
        if (request.command() != EIPCCommand::InterfaceCall) return dispatch;

        // Ignore calls when appId is not resolved or not in Lua config
        if (!LuaConfig::HasDepot(Hooks_Misc::ResolveAppId())) return dispatch;

        // Parse out the interface call header to find the handler
        IPCMessages::IPCInterfaceCall call{request.body()};
        if (!call.ok()) return dispatch;

        // Lookup handler by interface ID + method hash
        dispatch.handler = FindHandler(call.interfaceID(), call.funcHash());
        if (!dispatch.handler) return dispatch;

        LOG_IPC_TRACE("Resolved IPC handler: {}", dispatch.DebugString());
        return dispatch;
    }

    static void HandleHandshake(void* pServer, HSteamPipe hSteamPipe,CUtlBuffer* pRead)
    {
        IPCMessages::IPCRequest request{pRead};
        if (!request.ok()|| request.command() != EIPCCommand::Handshake) return;
        IPCMessages::IPCHandshakeReq handshake{request.body()};
        if(!handshake.ok()) return;

        CPipeClient* pipe = GetPipe(pServer, hSteamPipe);
        if (!pipe) return;
        // set client PID 
        pipe->m_clientPID = handshake.pid();

        LOG_IPC_DEBUG("Received handshake from {},{}", pipe->DebugString(), handshake.DebugString());
        PipeManager::OnHandshake(pipe);
    }

    // Detect the first SteamNetworkingSockets call (interface 46) so GetAppID can
    // flip to the session AppId for P2P games. Skipped once already seen or when
    // not in onlinefix.
    static void DetectNetworkingSockets(CUtlBuffer* pRead) {
        if (!Hooks_Misc::IsOnlineFixActive() || Hooks_Misc::ShouldReportOnlineFixAppId()) return;
        IPCMessages::IPCRequest request{pRead};
        if (!request.ok() || request.command() != EIPCCommand::InterfaceCall) return;
        IPCMessages::IPCInterfaceCall call{request.body()};
        if (!call.ok()) return;
        if (call.interfaceID() == EIPCInterface::IClientNetworkingSocketsSerialized)
            Hooks_Misc::NotifyNetworkingSocketsUsed();
    }

    // ── 出站 IPC 调用画像 ────────────────────────────────────────────────────
    //
    // 联机路径目前只有 6 个 handler 有覆盖（GetSteamID / 票据 / GetAppID /
    // GetAPICallResult），其余接口在日志里是黑箱 —— 2026-10-16 机械狂欢"邀请按钮
    // 点了没反应"就没有任何线索可查。这里给每个 (pid, interfaceID, funcHash) 的
    // **首次**出现记一行（pid + 接口名 + 方法名 + 参数个数 + body 头），之后只计数，
    // 避免刷爆日志。方法名取自 IPCLoader 的元数据（签名表），所以能直接看到
    // `IClientFriends::InviteUserToGame` 这种可读结果。
    //
    // 刻意**不按 appid 过滤**：早先按 GetAppIDForCurrentPipeWrap() == 会话 appid
    // 过滤，实测在这个路径下拿不到游戏 pipe 的 appid（调用被全部筛掉、日志空）。
    // 改为把 pid 打进每条记录，由 pid 自己说明是哪个进程。
    std::unordered_map<std::string, uint32> g_IpcProfile;
    std::mutex g_IpcProfileMutex;

    void ProfileIpcCall(void* pServer, HSteamPipe hSteamPipe, CUtlBuffer* pRead) {
        // 观测：老路线 -onlinefix，以及新的"环境变量会话"（宿主会话）都要看得到调用列表。
        if (!Hooks_Misc::IsOnlineFixActive() && !Hooks_Misc::EnvSessionActive()) return;

        IPCMessages::IPCRequest request{pRead};
        if (!request.ok() || request.command() != EIPCCommand::InterfaceCall) return;
        IPCMessages::IPCInterfaceCall call{request.body()};
        if (!call.ok()) return;

        CPipeClient* pipe = GetPipe(pServer, hSteamPipe);
        const uint32 pid = pipe ? pipe->m_clientPID : 0;

        const uint64 key = (static_cast<uint64>(pid) << 32) ^ call.funcHash();
        {
            std::lock_guard<std::mutex> lock(g_IpcProfileMutex);
            uint32& seen = g_IpcProfile[std::format("{}|{}|{}", pid,
                                static_cast<uint32>(call.interfaceID()), call.funcHash())];
            if (seen++ > 0) return;       // 记录过 → 只计数
        }

        const IPCLoader::Method* m = IPCLoader::Find(call.interfaceID(), call.funcHash());
        const auto body = call.body();
        const size_t shown = body.size() < 24 ? body.size() : 24;
        std::string hex;
        for (size_t i = 0; i < shown; ++i)
            hex += std::format("{:02X} ", body[i]);

        LOG_IPC_INFO("IPC profile: pid={} {}({})::{} hash=0x{:08X} argc={} bodyBytes={} head={}",
                     pid,
                     EIPCInterfaceName(call.interfaceID()),
                     static_cast<uint32>(call.interfaceID()),
                     m ? m->name.c_str() : "<元数据未收录>",
                     call.funcHash(), m ? m->argc : 0, body.size(), hex);
    }

    // ── 响应侧画像：好友/大厅两类调用的**结果** ──────────────────────────────
    //
    // "点了按钮没反应"通常就是这条调用返回 Failed、或返回体里给了 false/0。请求侧
    // 只说明"发出去过"，所以这里把响应也记下来：EIPCResult（无响应 / OK / Failed）
    // + 返回体头 24 字节。只记接口 3（IClientFriends）与 6（IClientMatchmaking）——
    // 邀请与大厅这条线；每会话预算 400 行，避免刷爆日志。
    uint32 g_IpcRespBudget = 400;

    void ProfileResponse(void* pServer, HSteamPipe hSteamPipe,
                         CUtlBuffer* pRead, CUtlBuffer* pWrite, bool callReturned) {
        if (g_IpcRespBudget == 0 || (!Hooks_Misc::IsOnlineFixActive() && !Hooks_Misc::EnvSessionActive())) return;

        IPCMessages::IPCRequest request{pRead};
        if (!request.ok() || request.command() != EIPCCommand::InterfaceCall) return;
        IPCMessages::IPCInterfaceCall call{request.body()};
        if (!call.ok()) return;

        const uint32 iface = static_cast<uint32>(call.interfaceID());
        if (iface != 3 && iface != 6) return;   // IClientFriends / IClientMatchmaking

        CPipeClient* pipe = GetPipe(pServer, hSteamPipe);
        const uint32 pid = pipe ? pipe->m_clientPID : 0;

        IPCMessages::IPCResponse resp{pWrite};
        const auto body = resp.body();
        const size_t shown = body.size() < 24 ? body.size() : 24;
        std::string hex;
        for (size_t i = 0; i < shown; ++i)
            hex += std::format("{:02X} ", body[i]);

        LOG_IPC_INFO("IPC resp: pid={} {} hash=0x{:08X} result={} returned={} bodyBytes={} head={}",
                     pid, EIPCInterfaceName(call.interfaceID()), call.funcHash(),
                     resp.ok() ? EIPCResultName(resp.result()) : "<无响应>",
                     callReturned, body.size(), hex);
        --g_IpcRespBudget;
    }

    // Precise outbound appid rewrite for -onlinefix games.
    //
    // A few calls pass an AppID as their FIRST parameter and must run against the
    // REAL appid rather than the session appid. IClientRemoteStorage (interface
    // 13) is the known case: a game launched as 480 would otherwise write its
    // cloud saves under Spacewar instead of under its own appid.
    //
    // Only the known interface plus a verified first argument are touched. The
    // payload is deliberately NOT scanned for the value: a FileWrite body may
    // legitimately contain 480 (0x000001E0) as file content, and rewriting that
    // would corrupt the write.
    static void RewriteOutboundAppId(CUtlBuffer* pRead) {
        if (!Hooks_Misc::IsOnlineFixActive()) return;

        IPCMessages::IPCRequest request{pRead};
        if (!request.ok() || request.command() != EIPCCommand::InterfaceCall) return;

        IPCMessages::IPCInterfaceCall call{request.body()};
        if (!call.ok()) return;
        if (call.interfaceID() != EIPCInterface::IClientRemoteStorage) return;

        // Only the session pipe of the -onlinefix-launched game.
        const AppId_t session = Hooks_Misc::SessionAppId();
        if (Hooks_Misc::GetAppIDForCurrentPipeWrap() != session) return;

        const AppId_t real = Hooks_Misc::ResolveAppId();
        if (real == 0 || real == session) return;
        if (call.body().size() < sizeof(uint32)) return;

        uint32 firstArg = 0;
        std::memcpy(&firstArg, call.body().data(), sizeof(uint32));
        if (firstArg != session) return;

        std::memcpy(call.body().data(), &real, sizeof(uint32));
        LOG_IPC_INFO("OnlineFix: IClientRemoteStorage first arg {} -> {} (real appid)",
                     firstArg, real);
    }

    HOOK_FUNC(IPCProcessMessage, bool,void* pServer, HSteamPipe hSteamPipe,
              CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        // handle handshake messages
        HandleHandshake(pServer, hSteamPipe, pRead);
        DetectNetworkingSockets(pRead);
        if (kEnableAppIdRewrites) RewriteOutboundAppId(pRead);
        ProfileIpcCall(pServer, hSteamPipe, pRead);

        IPCDispatch dispatch = ResolveDispatch(pServer, hSteamPipe, pRead);
        // If we didn't find a handler for this message, just pass through to the original function.
        if(!dispatch.enabled()) {
            const bool passthrough = oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite);
            ProfileResponse(pServer, hSteamPipe, pRead, pWrite, passthrough);
            return passthrough;
        }

        // If we did find a handler, run the pre-handler
        if (dispatch.handler->pre)
            dispatch.handler->pre(dispatch.pipe, pRead, pWrite);

        // Then call the original function to let steamclient process the message as normal.
        bool result = oIPCProcessMessage(pServer, hSteamPipe, pRead, pWrite);
        ProfileResponse(pServer, hSteamPipe, pRead, pWrite, result);

        // Ultimately the post-handler can choose to modify the response.
        if (result && dispatch.handler->post)
            dispatch.handler->post(dispatch.pipe, pRead, pWrite);

        return result;
    }

} // namespace


namespace Hooks_IPC {

    void Install() {
        RESOLVE_C(GetPipeClient);

        // Each module registers a static array. Hash lookup against the
        // IPCLoader metadata happens inside RegisterHandlers.
        Hooks_IPC_ISteamUser::Register();
        Hooks_IPC_ISteamUtils::Register();

        LOG_IPC_INFO("Hooks_IPC: {} handlers registered", g_Handlers.size());

        HOOK_BEGIN();
        INSTALL_HOOK_C(IPCProcessMessage);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK_C(IPCProcessMessage);
        UNHOOK_END();
    }

    void RegisterHandlers(std::span<const IPCHandlerEntry> entries) {
        for (const auto& e : entries) {
            const auto* m = IPCLoader::Find(e.interfaceName, e.methodName);
            if (!m) {
                LOG_IPC_WARN("[Handler Disabled] no IPC spec for {}",e.DebugString());
                continue;
            }
            auto& handler = g_Handlers.emplace_back(e,*m);
            LOG_IPC_DEBUG("Hooks_IPC: resolved {}", handler.DebugString());
        }
    }

}
