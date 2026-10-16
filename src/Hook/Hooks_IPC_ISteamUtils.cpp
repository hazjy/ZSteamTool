#include "Hooks_IPC.h"
#include "Hooks_IPC_ISteamUtils.h"
#include "Hooks_Misc.h"
#include "PendingAPICalls.h"
#include "Steam/Callback.h"
#include "Utils/Logging/Log.h"

#include <type_traits>

namespace {
    using namespace IPCMessages::IClientUtils;

    template <class CallbackT>
    bool WriteAPICallResult(CUtlBuffer* pWrite,uint32 callbackCapacity,const CallbackT& callback)
    {
        static_assert(std::is_trivially_copyable_v<CallbackT>);
        if (callbackCapacity < sizeof(CallbackT)) return false;

        GetAPICallResultResp resp{pWrite, callbackCapacity};
        if (!resp.ok()) return false;
        if (!resp.set_pCallback(IPCMessages::asBytes(callback))) return false;
        resp.set_returnValue(true);
        resp.set_pbFailed(false);
        return true;
    }

    // [Post-Handler]: IClientUtils::GetAppID
    //  SpawnProcess rewrites pGameID to the session AppId (480 by default) for
    //  OnlineFix games, so steamclient returns that.  Restore the real app_id.
    //  GetAppID reads and updates the response steamclient pre-filled.
    void HandlerPost_IClientUtils_GetAppID(CPipeClient* pipe, CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        // 环境变量会话（GUI 的宿主会话路线）：这条路线的全部意义就是"会话身份一致"——
        // 客户端、大厅、P2P 都在 480，**游戏侧也必须看 480**。下面这段"还原真 appid"是给
        // 老 -onlinefix 路线（spawn 改写 pGameID）用的，在宿主会话里必须让路：否则游戏按
        // 真 appid 建网络身份，而大厅在 480，连不上。
        // 实测证据：机械狂欢"房间码加入"报网络连接错误；好友 ipc.log 里正是这条
        //   `GetAppID: spoof response 480 -> 4108000`
        if (Hooks_Misc::EnvSessionActive()) return;

        // Once P2P is up, leave the session AppId so the socket matches the
        // session cert.
        if (Hooks_Misc::ShouldReportOnlineFixAppId()) return;

        AppId_t realAppId = Hooks_Misc::ResolveAppId();
        if (!realAppId) return;

        GetAppIDResp resp{pWrite};
        if (!resp.ok()) return;

        // Read what steamclient just wrote, decide whether to spoof.
        const AppId_t current = resp.returnValue();
        if (current == realAppId) return;
        resp.set_returnValue(realAppId);
        LOG_IPC_INFO("GetAppID: spoof response {} -> {}", current, realAppId);
    }

    // ════════════════════════════════════════════════════════════════
    //  GetAPICallResult per-callback handlers
    // ════════════════════════════════════════════════════════════════

    bool HandleCallback_EncryptedAppTicketResponse(CUtlBuffer* pWrite, uint64 hAsyncCall, uint32 cubCallback)
    {
        const auto appId = PendingAPICalls::TakeEncryptedTicket(hAsyncCall);
        if (!appId) return false;

        EncryptedAppTicketResponse_t callback{};
        callback.m_eResult = k_EResultOK;
        if (!WriteAPICallResult(pWrite, cubCallback, callback)) {
            PendingAPICalls::RecordEncryptedTicket(hAsyncCall, *appId);
            LOG_IPC_WARN("Failed to write EncryptedAppTicketResponse for AppId={} hAsyncCall=0x{:X}", 
                            *appId, hAsyncCall);
            return false;
        }
        LOG_IPC_DEBUG("Set K_EResultOK for EncryptedAppTicketResponse callback, AppId={} hAsyncCall=0x{:X}",
                        *appId, hAsyncCall);
        return true;
    }

    struct APICallResultHandlerEntry {
        uint32 callbackId;
        bool (*handler)(CUtlBuffer* pWrite, uint64 hAsyncCall, uint32 cubCallback);
    };

    constexpr APICallResultHandlerEntry kAPICallResultHandlers[] = {
        { EncryptedAppTicketResponse_t::k_iCallback, HandleCallback_EncryptedAppTicketResponse },
    };

    // [Post-Handler]: IClientUtils::GetAPICallResult
    void HandlerPost_IClientUtils_GetAPICallResult(CPipeClient*, CUtlBuffer* pRead, CUtlBuffer* pWrite)
    {
        GetAPICallResultReq req{pRead};
        if (!req.ok()) return;

        AppId_t appId = Hooks_Misc::ResolveAppId();
        LOG_IPC_DEBUG("{}, AppId={}", req.DebugString(),appId);
        for (const auto& entry : kAPICallResultHandlers) {
            if (entry.callbackId == req.iCallbackExpected()) {
                entry.handler(pWrite, req.hSteamAPICall(), req.cubCallback());
                return;
            }
        }
    }

} // namespace

namespace Hooks_IPC_ISteamUtils {
    void Register() {
        IPCHandlerEntry UtilsEntries[] = {
            ADD_IPC_POST_HANDLER(IClientUtils, GetAppID),
            ADD_IPC_POST_HANDLER(IClientUtils, GetAPICallResult),
        };
        Hooks_IPC::RegisterHandlers(UtilsEntries);
    }
}
