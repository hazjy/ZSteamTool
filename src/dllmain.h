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

// ── 出站 appid 改写总开关（实验分支用）──────────────────────────────────────
// 默认 **关闭**：只保留观测（出站 IPC 指令画像 / 响应记录 / MMS 字段转储 / 服务器查询观测），
// 不修改任何报文。理由见 doc/TODO/待办/04：那一批改写（MMS 建厅 appid、服务器查询 appid、
// RemoteStorage 首参数）已被实测证明无效，且会把 Steam 侧填好的会话 appid 改坏 ——
// 测"环境变量拉会话身份"这条路线（OnlineHost）时必须保持零改写。
inline constexpr bool kEnableAppIdRewrites = false;

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
