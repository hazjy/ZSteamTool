#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "Steam/Types.h"

namespace Config {

    enum class LogLevel { Trace, Debug, Info, Warn, Error };

    struct ManifestTimeouts {
        uint32_t resolve = 5000;
        uint32_t connect = 5000;
        uint32_t send    = 10000;
        uint32_t recv    = 10000;
    };

    // [[inject]] entry: a DLL loaded into a matching game process at the IPC handshake.
    struct InjectDll {
        std::string                 path;        // resolved absolute path
        std::string                 whenCmdline; // substring required in the game command line
        std::unordered_set<AppId_t> whenAppids;  // appids this entry applies to
        bool                        allGames = false;  // false: only Lua-unlocked games
    };

    struct CloudSettings {
        bool enabled = false;
        std::string library;
    };

    // [denuvo] — what SteamID identity a game process is allowed to observe.
    //   Normal (default) = upstream OpenSteamTool semantics: GetSteamID is spoofed
    //     only inside the Denuvo authorization window, so the game keeps the
    //     logged-in account's identity the rest of the session and every
    //     per-user thing derived from it (save folders, cloud, settings) stays
    //     bound to that account.
    //   Compat = BetterSteamTools behaviour: spoof for the whole session. Titles
    //     that re-check the SteamID outside the window need this, at the cost of
    //     binding the game's per-user data to the ticket's account.
    enum class DenuvoMode { Normal, Compat };

    struct LoadResult {
        bool applied = false;
        bool luaPathsChanged = false;
    };

    LoadResult Load(const std::string& configPath);

    ManifestTimeouts GetManifestTimeouts();
    LogLevel GetLogLevel();
    std::string GetLogDir();
    std::vector<std::string> GetLuaPaths();
    std::vector<std::string> GetRemoteUrlTemplates();
    CloudSettings GetCloudSettings();
    DenuvoMode GetDenuvoMode();
    bool GetOnlinefixEnvSession();
    bool GetStatsEnableApi();
    bool GetUpdateEnabled();

    // [manifest] — provider selection lives in ManifestClient (table-driven).
    inline uint32_t manifestTimeoutResolve = 5000;
    inline uint32_t manifestTimeoutConnect = 5000;
    inline uint32_t manifestTimeoutSend    = 10000;
    inline uint32_t manifestTimeoutRecv    = 10000;

    // [log]
    inline LogLevel logLevel = LogLevel::Debug;

    // derived from configPath: <steam>/opensteamtool/
    inline std::string logDir;

    // [lua]
    inline std::vector<std::string> luaPaths;

    // [remote] — one or more mirror templates, tried in order. Empty = built-in defaults.
    inline std::vector<std::string> remoteUrlTemplates;

    // [stats]
    inline bool statsEnableApi = true;

    // [update] - self-update check on startup (staged for next Steam launch).
    inline bool updateEnabled = true;

    // [[inject]] - optional DLL injection into matching game processes.
    inline std::vector<InjectDll> injectDlls;

    // [cloud] - optional Steam Cloud save redirection via CloudRedirect.
    inline bool cloudEnabled = false;
    inline std::string cloudLibrary;

    // [denuvo] - SteamID identity mode (see DenuvoMode above).
    inline DenuvoMode denuvoMode = DenuvoMode::Normal;

    // [onlinefix] — 环境变量会话（GUI 的 OnlineHost 宿主启动路线）。
    //   true（默认）= 允许内核按"游戏进程环境变量里的会话身份"工作：`SteamAppId/
    //   SteamGameId/SteamOverlayGameId` 给出会话身份（如 480）时，内核把客户端内部
    //   对该进程的 appid 判断洗成会话身份，使 MMS 建厅等由客户端填 appid 的报文与会话
    //   一致（2026-10-16 实测：建厅由 `#3=15 AccessDenied` 变为 `#3=1（OK）`，邀请弹窗
    //   正常）。只在"该 appid 在 lua 里配过 + 会话身份与真 appid 不同"时生效，普通启动
    //   （Steam 库点开始）不会触发。
    //   false = 关闭该行为；**仅供排查**（想排除内核因素时用它），关掉后只保留原有的
    //   `-onlinefix` 命令行路线（那条路拿不到 Valve 大厅）。
    inline bool onlinefixEnvSession = true;

}
