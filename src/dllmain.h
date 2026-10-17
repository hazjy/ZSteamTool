#ifndef DLLMAIN_H
#define DLLMAIN_H

#include "OSTPlatform/include/DynamicLibrary.h"

#include <string>
#include <fstream>
#include <filesystem>
#include <array>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <regex>
#include <memory>
#include <atomic>
#include <format>

#include "Steam/Types.h"
#include "Steam/Enums.h"
#include "Steam/Structs.h"
#include "Steam/Callback.h"
#include "Utils/Config/LuaConfig.h"
#include "Utils/Logging/Log.h"
#include "Utils/Config/Config.h"


inline OSTPlatform::DynamicLibrary::ModuleHandle client_hModule = nullptr;
inline OSTPlatform::DynamicLibrary::ModuleHandle ui_hModule = nullptr;

// ── 出站 appid 改写：只剩 IClientRemoteStorage 首参数这一处 ───────────────────
// 用途：480 启动的游戏，其 `*ForApp` 调用的首参数会被当成 Spacewar → 云存档落到 480
// 名下；打开本开关后把首参数 480 换成真 appid，让云存档回到游戏自己名下。
// **默认关闭**：这一处**尚未做过行为验证**（2026-10-16 仅移植 + 编译通过）。
// 注意：MMS 建厅与服务器查询的报文改写已于 2026-10-17 删除 —— 那两处 2026-10-16 实测无效
// （4108000→#3=15、改写 480/1046930→#3=2；查询改真 appid 后列表变空），结论见
// doc/报告-机械狂欢联机可行性-2026-10-16.md §二。
inline constexpr bool kEnableRemoteStorageAppIdRewrite = false;

inline constexpr size_t kRuntimePathCapacity = 260;

inline char SteamInstallPath[kRuntimePathCapacity] = {};
inline char SteamclientPath[kRuntimePathCapacity]  = {};
inline char SteamUIPath[kRuntimePathCapacity]      = {};
inline char DiversionPath[kRuntimePathCapacity]    = {};
inline char LuaDir[kRuntimePathCapacity]           = {};   // fallback lua dir: <Steam>\config\lua
inline char ConfigPath[kRuntimePathCapacity]       = {};

// The default session AppId used by -onlinefix (SpaceWar). Overridable per
// launch via "-onlinefix=<appid>" / "-onlinefix <appid>" (see Hooks_Misc.cpp
// ParseSessionAppId); runtime value via Hooks_Misc::SessionAppId().
constexpr AppId_t kOnlineFixAppId = 480;

#endif // DLLMAIN_H
