#pragma once

#include "dllmain.h"

// Catch-all for lightweight info-capture int3 traps and launch-time rewrites
// that don't fit a dedicated category — currently:
//   * GetAppIDForCurrentPipe  -> captures the SteamEngine pointer
//   * SpawnProcess            -> OnlineFix detection + 480 rewrite
//   * GetAppDataFromAppInfo   -> captures the CAppInfoCache pointer
namespace Hooks_Misc {
    void Install();
    void Uninstall();

    // Returns the AppId for the current Steam pipe via the captured engine
    // pointer, or 0 if we haven't yet observed the host calling
    // GetAppIDForCurrentPipe.
    AppId_t GetAppIDForCurrentPipeWrap();

    // True while a -onlinefix game is the active spawn.
    bool IsOnlineFixActive();

    // Bind the -onlinefix session to its game process (called from the pipe
    // handshake) and start the exit watcher. The session state only describes
    // that process, so it is dropped as soon as the process exits.
    void TrackOnlineFixGameProcess(PID_t pid);

    // Route A 实验（2026-10-16）：登记"环境变量会话"—— 游戏进程的 SteamAppId 环境变量
    // 给出会话身份（GUI 的 OnlineHost 启动器会设），供客户端内部 appid 洗白使用。
    // clientAppId = 客户端视角里这条 pipe 的真 appid（握手那一刻量，传 0 = 没量到），
    // 用来把洗白**限定在会话那条 pipe** 上：否则同时开着另一个也配过库的游戏时，
    // 它的 pipe 会被一起洗坏。量不到就退化为"只看 HasDepot"的宽口径并记日志。
    void EngageEnvSession(PID_t pid, uint64 creationTime, AppId_t envAppId, AppId_t clientAppId);

    // Call when the game uses SteamNetworkingSockets (IPC interface 46).
    void NotifyNetworkingSocketsUsed();

    // True once P2P started — GetAppID reports 480; before, the real appid.
    bool ShouldReportOnlineFixAppId();

    // True while an "环境变量会话"（GUI 的宿主会话路线）is registered.
    // 该会话刻意让 **客户端与游戏侧都看 480**（一致性优先），所以凡是"把真 appid
    // 塞回游戏"的老逻辑（原为 -onlinefix 的 spawn 改写服务）都要在此时让路。
    bool EnvSessionActive();

    // True when this launch asked to keep the real appid for the game's own view
    // (-realappid): the GetAppID flip is off, so appid-carrying outbound requests
    // should use the real appid as well.
    bool IsAppIdFlipSuppressed();

    // Grow a CUtlBuffer to at least 'newCapacity' bytes and set m_Put = newCapacity.
    // Uses CUtlBuffer::EnsureCapacity from steamclient, resolved on first call.
    bool EnsureBufferCapacity(CUtlBuffer* pWrite, uint32 newCapacity,bool updatePut = false);

    // Resolve the real appid: if OnlineFix is active return real appid,
    // otherwise fall back to GetAppIDForCurrentPipe().
    AppId_t ResolveAppId();

    // The session AppId used by -onlinefix (default kOnlineFixAppId=480).
    // Overridable per launch: "-onlinefix=<appid>" or "-onlinefix <appid>"
    // on the command line (parsed in SpawnProcess; the same value is used
    // for P2P cert / lobby / invite checks / overlay identity).
    AppId_t SessionAppId();

    // Get localized game name via GetAppDataFromAppInfo (cached).
    std::string GetGameNameByAppID(AppId_t appId);

}
