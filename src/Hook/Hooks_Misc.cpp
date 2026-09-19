#include "Hooks_Misc.h"
#include "HookMacros.h"
#include "Utils/HookSupport/VehCommon.h"
#include "dllmain.h"
#include "OSTPlatform/include/Process.h"

#include <thread>
#include <windows.h>

namespace {
    // ── Resolve-only functions ─────────────────────────────────────
    RESOLVE_FUNC(CUtlBufferEnsureCapacity, void*, CUtlBuffer* pCUtlBuffer, uint32 newCapacity);

    // ── VEH-captured functions (one-shot int3) ───────────────────────────────
    // On int3 hit, ctx->Rcx is stored to the named output variable.
    CAPTURE_THIS_FUNC(GetAppIDForCurrentPipe, AppId_t,      g_steamEngine,    void*);
    CAPTURE_THIS_FUNC(GetAppDataFromAppInfo,  int64,        g_pCAppInfoCache, void*, AppId_t, const char*, uint8*, int32);

    // Assumes one game at a time.  Set by SpawnProcess VEH when -onlinefix
    // is detected; cleared when a non-onlinefix game launches, or when the
    // session's game process exits (see TrackOnlineFixGameProcess /
    // StartOnlineFixGameWatcher).
    AppId_t   g_OnlineFixRealAppId;
    // Session identity for the -onlinefix launch (Spacewar by default).
    // Overridable with "-onlinefix=<appid>" / "-onlinefix <appid>" on the
    // command line; every 480-flavoured rewrite uses this value instead.
    AppId_t   g_SessionAppId = kOnlineFixAppId;
    // True once the game starts SteamNetworkingSockets P2P (see GetAppID handler).
    bool      g_NetworkingSocketsActive;
    // Set by -realappid on the same command line. Suppresses the P2P appid flip
    // for this launch only — see ShouldReportOnlineFixAppId.
    bool      g_SuppressAppIdFlip;
    // The -onlinefix session's game process (bound on its pipe handshake).
    // The state above only ever describes THAT process, so it must be dropped
    // once that process is gone — otherwise a later launch that bypasses
    // Steam's spawn path (e.g. the GUI's OnlineHost launcher) inherits a stale
    // "onlinefix active" state: friend persona entries, LobbyInvite gameIDs and
    // the P2P flip gate all keep getting rewritten for a game that is not in an
    // onlinefix session at all.
    PID_t     g_OnlineFixGamePid;
    std::atomic<bool> g_OnlineFixWatcherStop{false};
    std::unordered_map<AppId_t, std::string> g_GameNameCache;

    // Drop everything that describes the -onlinefix session.
    void ResetOnlineFixSession(const char* reason) {
        if (!g_OnlineFixRealAppId) return;
        LOG_MISC_INFO("OnlineFix session cleared ({}): game pid={}, real appid {} -> 0",
                      reason, g_OnlineFixGamePid, g_OnlineFixRealAppId);
        g_OnlineFixRealAppId = 0;
        g_NetworkingSocketsActive = false;
        g_SuppressAppIdFlip = false;
        g_SessionAppId = kOnlineFixAppId;
        g_OnlineFixGamePid = 0;
    }

    // Wait on a handle taken WHILE the game was alive. A "does this pid exist"
    // probe cannot be used here: a terminated-but-not-yet-reaped process still
    // answers OpenProcess/GetProcessTimes (Steam itself keeps a handle on its
    // tracked game processes), so such a probe never sees the death. A handle
    // signals on exit and is immune to pid reuse.
    void StartOnlineFixGameWatcher(PID_t pid) {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!process) {
            LOG_MISC_WARN("OnlineFix: OpenProcess(SYNCHRONIZE) failed for game pid={} (error={}); "
                          "session state will only be dropped on the next non-onlinefix launch",
                          pid, GetLastError());
            return;
        }

        std::thread([pid, process] {
            for (;;) {
                const DWORD wait = WaitForSingleObject(process, 1000);
                if (wait == WAIT_OBJECT_0) {
                    if (g_OnlineFixGamePid == pid) ResetOnlineFixSession("game process exited");
                    break;
                }
                // Anything but a timeout (WAIT_FAILED/WAIT_ABANDONED) means we can
                // no longer trust the handle; bail out and leave the state alone.
                if (wait != WAIT_TIMEOUT) break;
                // Superseded by a newer session, or the DLL is going away.
                if (g_OnlineFixWatcherStop || g_OnlineFixGamePid != pid) break;
            }
            CloseHandle(process);
        }).detach();
    }


    // ── SpawnProcess interception ────────────────────────────────────────────
    // CUser_SpawnProcess(pCUser, pExePath, pCommandLine, pWorkingDir,
    //                    pGameID, ...)
    // arg1=pCUser, arg2=pExePath, arg3=pCommandLine, arg4=pWorkingDir
    // arg5=pGameID (CGameID*; low 24 bits = AppId)
    //
    // Session AppId selection from the command line:
    //   "-onlinefix"             -> default kOnlineFixAppId (Spacewar 480)
    //   "-onlinefix=<appid>"     -> explicit (recommended, unambiguous)
    //   "-onlinefix <appid>"     -> following plain number also accepted
    // Anything else (e.g. "-onlinefixappid" style flags) falls back to default.
    static AppId_t ParseSessionAppId(const char* cmdLine) {
        const char* p = strstr(cmdLine, "-onlinefix");
        if (!p) return kOnlineFixAppId;
        p += 10; // strlen("-onlinefix")
        if (*p == '=') {
            ++p;
        } else if (*p == ' ' || *p == '\t') {
            while (*p == ' ' || *p == '\t') ++p;
        } else {
            return kOnlineFixAppId; // "-onlinefix..." (other flags, end, etc.)
        }
        if (!(*p >= '0' && *p <= '9')) return kOnlineFixAppId;
        unsigned long long v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + static_cast<unsigned long long>(*p - '0');
            if (v > UINT32_MAX) return kOnlineFixAppId;
            ++p;
        }
        return v == 0 ? kOnlineFixAppId : static_cast<AppId_t>(v);
    }

    static void OnSpawnProcessHit(OSTPlatform::Trap::Context& ctx, const VehCommon::Int3Site& /*site*/) {
        CGameID* pGameID = VehCommon::GetArg<CGameID*>(ctx, 5);
        AppId_t appId = static_cast<AppId_t>(pGameID->AppID(true));
        const char* cmdLine = VehCommon::GetArg<const char*>(ctx, 3);

        if (cmdLine && strstr(cmdLine, "-onlinefix"))
        {
            g_OnlineFixRealAppId = appId;
            g_NetworkingSocketsActive = false;
            // Opt out of the P2P appid flip for this game. Launch options are
            // already per-game in Steam, so this needs no appid list of its own.
            g_SuppressAppIdFlip = strstr(cmdLine, "-realappid") != nullptr;
            g_SessionAppId = ParseSessionAppId(cmdLine);
            // New session: the game process is bound on its first pipe handshake.
            g_OnlineFixGamePid = 0;
            pGameID->SetAppID(g_SessionAppId);
            LOG_MISC_INFO("SpawnProcess: appid {} -> {} (session), realappid={}, cmd=\"{}\"",
                          appId, g_SessionAppId, g_SuppressAppIdFlip, cmdLine);
        } else {
            g_OnlineFixRealAppId = 0;
            g_SuppressAppIdFlip = false;
            g_SessionAppId = kOnlineFixAppId;
            g_OnlineFixGamePid = 0;
        }
    }

    // ── SteamController_OptedInMask ──────────────────────────────────────────
    // Called by CUser_BuildSpawnEnvBlock with pGameID's appid to
    // compute EnableConfiguratorSupport and the SDL_* env vars.
    // With 480 the spawned game inherits Spacewar's Steam Input
    // opt-in and gameoverlayrenderer hijacks the XInput stream.
    HOOK_FUNC(OptedInMask, int64,void* pThis, AppId_t appId)
    {
        if (appId == Hooks_Misc::SessionAppId() && g_OnlineFixRealAppId) {
            LOG_MISC_INFO("OptedInMask: appid {} -> {}",appId, g_OnlineFixRealAppId);
            appId = g_OnlineFixRealAppId;
        }
        return oOptedInMask(pThis, appId);
    }

    // ── CUser_BuildSpawnEnvBlock ─────────────────────────────────────────────
    // pOverlayCGameID drives SteamOverlayGameId, which the in-game overlay
    // reads. Upstream/BST restores it to the real appid for screenshot tags /
    // community URLs -- but that BREAKS ActivateGameOverlayInviteDialog: the
    // overlay binds its invite dialog to (overlay identity == lobby appid),
    // and with identities mismatched against the 480-space lobby it degrades
    // to a plain friends list, so lobby invites never happen (PEAK 实测).
    // Keep overlay identity at 480; controller identity (OptedInMask above)
    // is still restored to the real appid.  [本地合并：取本地实测方向]
    HOOK_FUNC(BuildSpawnEnvBlock, int64,
              void* pThis, CGameID* pCGameID, void* a3, void* env,
              CGameID* pOverlayCGameID, void* a6, int a7,
              void* a8, void* a9, unsigned int a10, char a11)
    {
        if (g_OnlineFixRealAppId) {
            LOG_MISC_INFO("BuildSpawnEnvBlock: keeping OverlayCGameID at {} for -onlinefix invite dialog",
                          Hooks_Misc::SessionAppId());
        }
        return oBuildSpawnEnvBlock(pThis, pCGameID, a3, env,
                                    pOverlayCGameID, a6, a7,
                                    a8, a9, a10, a11);
    }

    // CAppInfoCache::GetOrAddAppData
    // The injected package keeps Lua-provided ids in PackageInfo::AppIdVec.
    // Some of those ids can actually be depot ids, but we cannot trust the
    // Lua config to classify app ids and depot ids for us. In offline mode,
    // depot ids usually have only placeholder appinfo data. That blocks
    // CClientAppManager_ProcessPendingLicenseUpdates, because it waits for
    // every AppIdVec entry to have resolved appinfo unless the entry has been
    // marked as a known-unknown id by the PICS path. For injected ids that
    // still have placeholder appinfo, set skip_flag so Steam treats them like
    // PICS unknown_appids instead of keeping the license update pending.
    HOOK_FUNC(GetOrAddAppData,CAppData*,void* pCache, AppId_t appId,bool bCreate)
    {
        CAppData* pData = oGetOrAddAppData(pCache, appId, bCreate);
        // LOG_MISC_TRACE("GetOrAddAppData: appId={} bCreate={} -> pData={}", appId, bCreate, pData ? pData->DebugString() : "null");
        // TODO: find a more robust way
        if (LuaConfig::HasDepot(appId, false) && pData && !bCreate && pData->IsUnresolvedAppInfo()) {
            LOG_MISC_DEBUG("GetOrAddAppData: Marking appId {} as skip_flag=true to bypass license update blocking", appId);
            pData->bSkipFlag = true;
        }
        return pData;
    }
}

namespace Hooks_Misc {
    void Install() {
        RESOLVE_C(CUtlBufferEnsureCapacity);

        ARM_CAPTURE_C(GetAppIDForCurrentPipe);
        ARM_CAPTURE_C(GetAppDataFromAppInfo);

        ARM_INT3_C(SpawnProcess, true, &OnSpawnProcessHit, nullptr);

        HOOK_BEGIN();
        INSTALL_HOOK_C(BuildSpawnEnvBlock);
        INSTALL_HOOK_C(OptedInMask);
        // INSTALL_HOOK_C(GetOrAddAppData);
        HOOK_END();
    }

    void Uninstall() {
        g_OnlineFixWatcherStop = true;
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(BuildSpawnEnvBlock);
        UNINSTALL_HOOK(OptedInMask);
        // UNINSTALL_HOOK(GetOrAddAppData);
        UNHOOK_END();
    }

    AppId_t GetAppIDForCurrentPipeWrap() {
        if (!CAPTURE_READY(GetAppIDForCurrentPipe)) {
            LOG_MISC_WARN("GetAppIDForCurrentPipeWrap called before capture — returning 0");
            return 0;
        }
        auto appid = oGetAppIDForCurrentPipe(g_steamEngine);
        if (!appid) {
            LOG_MISC_TRACE("GetAppIDForCurrentPipeWrap: AppId=0(Not GamePipe)");
        } else {
            LOG_MISC_TRACE("GetAppIDForCurrentPipeWrap: AppId={}", appid);
        }
        return appid;
    }

    
    AppId_t ResolveAppId() {
        if (g_OnlineFixRealAppId) return g_OnlineFixRealAppId;
        return GetAppIDForCurrentPipeWrap();
    }

    bool IsOnlineFixActive() {
        return g_OnlineFixRealAppId != 0;
    }

    void TrackOnlineFixGameProcess(PID_t pid) {
        // Only the first game process after a -onlinefix launch owns the
        // session; later handshakes belong to children/helpers of that game.
        if (!g_OnlineFixRealAppId || pid == 0 || g_OnlineFixGamePid != 0) return;

        g_OnlineFixGamePid = pid;
        LOG_MISC_INFO("OnlineFix session bound to game pid={} (real appid {})",
                      pid, g_OnlineFixRealAppId);
        StartOnlineFixGameWatcher(pid);
    }

    AppId_t SessionAppId() {
        return g_SessionAppId;
    }

    void NotifyNetworkingSocketsUsed() {
        if (g_OnlineFixRealAppId && !g_NetworkingSocketsActive) {
            g_NetworkingSocketsActive = true;
            LOG_MISC_INFO("NetworkingSockets active: GetAppID now reports {} for cert match",
                          g_SessionAppId);
        }
    }

    bool ShouldReportOnlineFixAppId() {
        // The flip exists so a P2P socket's appid matches the 480 session cert,
        // which some titles need (#146). It is blunt though: from the moment it
        // trips, every GetAppID answer is the fake appid for the rest of the
        // process's life. Games that ask Steam for their own appid during later
        // startup then get 480 and misbehave — Bodycam (2406770) black-screens
        // straight after login this way.
        //
        // Both behaviours are needed by different games, and the call itself
        // gives no way to tell them apart, so -realappid opts out per launch.
        if (g_SuppressAppIdFlip) return false;
        return g_OnlineFixRealAppId != 0 && g_NetworkingSocketsActive;
    }

    bool EnsureBufferCapacity(CUtlBuffer* pWrite, uint32 newCapacity,bool updatePut)
    {
        if (oCUtlBufferEnsureCapacity) {
            LOG_MISC_DEBUG("Before ensuring CUtlBuffer capacity: {}", pWrite->DebugString());
            oCUtlBufferEnsureCapacity(pWrite, newCapacity);
            LOG_MISC_DEBUG("After ensuring CUtlBuffer capacity: {}", pWrite->DebugString());
            if(updatePut) pWrite->m_Put = newCapacity;
            return true;
        }
        LOG_MISC_WARN("EnsureBufferCapacity: oCUtlBufferEnsureCapacity not resolved");
        return false;
    }

    // ── Game name ────────────────────────────────────────────────
    std::string GetGameNameByAppID(AppId_t appId)
    {
        auto it = g_GameNameCache.find(appId);
        if (it != g_GameNameCache.end()) return it->second;

        std::string name;

        if (CAPTURE_READY(GetAppDataFromAppInfo)) {
            char buf[256] = {};
            // "common/name" triggers auto-localization: the function detects
            // prefix "common" (keyType=2) + key "name", then tries
            // "name_localized/<current_lang>" before falling back to "name".
            // Returns strlen+1 on success, -1 on failure.
            int64 len = oGetAppDataFromAppInfo(g_pCAppInfoCache, appId, "common/name",
                reinterpret_cast<uint8*>(buf), sizeof(buf));
            if (len > 1)
                name.assign(buf, static_cast<size_t>(len - 1));
        }

        LOG_MISC_DEBUG("GetGameNameByAppID({}): {}", appId, name);
        g_GameNameCache[appId] = name;
        return name;
    }

}
