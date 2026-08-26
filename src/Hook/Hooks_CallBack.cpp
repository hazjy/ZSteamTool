#include "Hooks_CallBack.h"
#include "HookMacros.h"
#include "Hooks_Misc.h"
#include "Utils/Logging/Log.h"
#include "dllmain.h"
namespace {

    HOOK_FUNC(SendCallbackToPipe, bool, void* pSteamEngine, HSteamPipe hSteamPipe,
            HSteamUser iClientUser, int iCallback, void* pCallbackData, int cubCallbackData) {
        // ── Callback modifier dispatch ─────────────────────────────────────────
        // Intercept callbacks before they reach the pipe and modify data in-place.
        // To add a new callback: add an else-if branch here.

        // [probe] log every callback id delivered to a game pipe so we can see
        // which of them (if any) carry invite/lobby data during OnlineFix tests.
        LOG_ONLINEFIX_DEBUG("SendCallbackToPipe: user={} cb={} size={}", iClientUser, iCallback, cubCallbackData);

        // -onlinefix: SpawnProcess rewrote the session to Spacewar(480), so lobby
        // invitations arrive with m_ulGameID == 480 while the game believes its own
        // AppId is the real one (GetAppID is deliberately restored). Games that run
        // an invite sanity check (invite.gameID == GetAppID()) silently drop such
        // invites -- rewrite the field back to the real AppId so validation passes.
        // The join itself is unaffected: it goes through m_ulSteamIDLobby on the
        // 480 pipe context, which the backend accepts (everyone owns 480).
        if (iCallback == LobbyInvite_t::k_iCallback
            && cubCallbackData >= static_cast<int>(sizeof(LobbyInvite_t))
            && pCallbackData
            && Hooks_Misc::IsOnlineFixActive())
        {
            auto* invite = static_cast<LobbyInvite_t*>(pCallbackData);
            if ((AppId_t)(invite->m_ulGameID & 0xFFFFFFFFu) == kOnlineFixAppId)
            {
                const AppId_t real = Hooks_Misc::OnlineFixRealAppId();
                LOG_ONLINEFIX_INFO("LobbyInvite: gameID {} -> {}, lobby={:016X}",
                                   kOnlineFixAppId, real, invite->m_ulSteamIDLobby);
                invite->m_ulGameID = real;
            }
        }

        return oSendCallbackToPipe(pSteamEngine, hSteamPipe, iClientUser,
                                    iCallback, pCallbackData, cubCallbackData);
    }
}

namespace Hooks_CallBack {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_C(SendCallbackToPipe);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK_C(SendCallbackToPipe);
        UNHOOK_END();
    }
}
