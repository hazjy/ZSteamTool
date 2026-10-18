#include "dllmain.h"
#include "Hook/HookManager.h"
#include "Utils/Config/Config.h"
#include "Utils/Config/ConfigFileWatcher.h"
#include "Utils/Config/LuaFileWatcher.h"
#include "Utils/CloudRedirect/CloudRedirectHost.h"
#include "Utils/SteamMetadata/IPCLoader.h"
#include "Utils/SteamMetadata/PatternLoader.h"
#include "Utils/SteamMetadata/SteamDiagnostics.h"
// [本地剔除] Tokeer（bst:// 兑换码）/ AppUpdater（自更新）不接入：本地手动部署模式 + 无外部后端
#include "OSTPlatform/include/Dialog.h"
#include "OSTPlatform/include/DynamicLibrary.h"
#include "OSTPlatform/include/Thread.h"

#include <string>
#include <filesystem>
#include <windows.h>

// prepare key runtime paths.
bool InitializeSteamComponents()
{
    // Steam 根目录以宿主 exe 为准，不用 CWD：经外部工具拉起 steam.exe 时 CWD 是
    // 继承来的工具目录，用 CWD 会导致 steamclient64.dll 定位失败、整个 init 静默全灭
    // （零 hook、解锁全灭）。steamclient64.dll 必定与 steam.exe 同目录，exe 路径才是
    // 唯一可信的锚点。
    std::string steamInstallPath;
    const std::filesystem::path hostExe = OSTPlatform::DynamicLibrary::GetMainExecutablePath();
    if (!hostExe.empty()) {
        const std::string hostName = hostExe.filename().string();
        if (_stricmp(hostName.c_str(), "steam.exe") != 0) {
            LOG_INFO("InitializeSteamComponents: host is not steam.exe ({}), skipping",
                     hostName.empty() ? "(unknown)" : hostName);
            return false;
        }
        steamInstallPath = hostExe.parent_path().string();
        LOG_INFO("Steam install path: {} (host {})", steamInstallPath, hostExe.string());
    }
    if (steamInstallPath.empty()) {
        // 取不到宿主路径时退回旧行为并留告警（不硬失败）。
        steamInstallPath = OSTPlatform::DynamicLibrary::GetCurrentDirectoryPath();
        LOG_WARN("InitializeSteamComponents: GetMainExecutablePath unavailable, falling back to CWD: {}",
                 steamInstallPath);
    }
    if (steamInstallPath.empty()) {
        return false;
    }
    sprintf_s(SteamInstallPath, kRuntimePathCapacity, "%s", steamInstallPath.c_str());
    sprintf_s(SteamclientPath, kRuntimePathCapacity, "%s\\steamclient64.dll",  SteamInstallPath);
    sprintf_s(SteamUIPath,     kRuntimePathCapacity, "%s\\steamui.dll",        SteamInstallPath);
    sprintf_s(DiversionPath,   kRuntimePathCapacity, "%s\\bin\\diversion.dll", SteamInstallPath);
    // Fallback Lua directory: <Steam>\config\lua (what the GUI writes to). Used only
    // while [lua] paths in opensteamtool.toml is empty — the GUI writes that entry
    // whenever the user picks a different directory, so exactly one directory is
    // scanned and lua files can never be loaded twice from two locations.
    // Do NOT change this back to config\stplug-in: that kernel/GUI default mismatch
    // made a fresh install (no toml yet) load none of the user's lua files, so nothing
    // showed up in game ("installed but no effect").
    sprintf_s(LuaDir,          kRuntimePathCapacity, "%s\\config\\lua",        SteamInstallPath);
    sprintf_s(ConfigPath,      kRuntimePathCapacity, "%s\\opensteamtool.toml", SteamInstallPath);
    
    client_hModule = OSTPlatform::DynamicLibrary::Load(SteamclientPath);
    if (!client_hModule) {
        LOG_ERROR("Load steamclient64.dll failed: {} (err={})",
                  SteamclientPath, OSTPlatform::DynamicLibrary::GetLastErrorCode());
        return false;
    }
    LOG_INFO("Loaded steamclient64.dll from {}", SteamclientPath);
    
    ui_hModule = OSTPlatform::DynamicLibrary::Load(SteamUIPath);
    if(!ui_hModule) {
        LOG_ERROR("Load failed for steamui.dll: err={}", OSTPlatform::DynamicLibrary::GetLastErrorCode());
        return false;
    }
    return true;
}

// All initialisation that touches the filesystem, loads modules, scans
// memory, or installs detours runs here on a worker thread — we MUST NOT do
// any of that from inside DllMain (loader lock).
static uint32_t InitThread(OSTPlatform::DynamicLibrary::ModuleHandle selfModule) {
    Log::Init(selfModule);
    LOG_INFO("OpenSteamTool init thread started");

    if (!InitializeSteamComponents()) {
        LOG_ERROR("InitializeSteamComponents failed");
        return 1;
    }

    Config::Load(ConfigPath);
    Log::InitModules();
    Log::InstallPlatformLogSink();
    SteamDiagnostics::Initialize(SteamclientPath, SteamUIPath);

    // Load pattern files for steamclient64.dll and steamui.dll.
    // Each call computes the SHA-256 of the DLL on disk, checks the local
    // cache, and downloads from GitHub if needed.  Both calls are synchronous
    // but run on this worker thread, never under the loader lock.
    PatternLoader::Load(ui_hModule, SteamUIPath, "steamui");
    PatternLoader::Load(client_hModule, SteamclientPath, "steamclient");

    // IPC method metadata (funcHash, fencepost, argc, ...)
    IPCLoader::Load(SteamclientPath);

    std::vector<std::string> watchDirs =
        LuaConfig::ResolveWatchDirs(Config::GetLuaPaths(), std::string(LuaDir), std::string(SteamInstallPath));
    for (const auto& dir : watchDirs)
        LuaConfig::ParseDirectory(dir);

    LuaFileWatcher::Start(watchDirs);
    ConfigFileWatcher::Start(ConfigPath, LuaDir);

    SteamUI::CoreHook();
    SteamClient::CoreHook();

    // Surface any functions that FindPattern() could not locate.
    PatternLoader::ReportMissingFunctions();

    // Optional Steam Cloud save redirection (CloudRedirect). No-op unless
    // [cloud].enabled is set and cloud_redirect.dll is present.
    CloudRedirectHost::Initialize(SteamInstallPath);

    // [本地剔除] 原 BST 此处：TokeerBridge::RegisterUriScheme（bst:// 兑换码，外部服务）
    // 与 AppUpdater 自更新检查（Config::GetUpdateEnabled 门控）——本地手动三 DLL 部署，不接入。

    LOG_INFO("OpenSteamTool init complete");
    return 0;
}

// True only when the host process is steam.exe. The proxy DLLs already gate injection to
// Steam, but rundll32 loads this DLL directly to service a bst:// link — there we must NOT
// run the Steam-injection machinery (steamclient load, hooks, watchers); the TokeerUri
// export does its work standalone.
static bool IsSteamHost()
{
    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH)) return false;
    const char* name = strrchr(exePath, '\\');
    name = name ? name + 1 : exePath;
    return _stricmp(name, "steam.exe") == 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, PVOID pvReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        if (!IsSteamHost())
            return TRUE;   // e.g. rundll32 bst:// handler — no injection here
        // Hand off all real work to a worker thread to avoid running file I/O,
        // module loading and detour transactions under the loader lock.
        OSTPlatform::Thread::StartDetached([module = reinterpret_cast<OSTPlatform::DynamicLibrary::ModuleHandle>(hModule)] {
            return InitThread(module);
        });
    }
    else if (dwReason == DLL_PROCESS_DETACH && IsSteamHost())
    {
        ConfigFileWatcher::Stop();
        LuaFileWatcher::Stop();
        SteamUI::CoreUnhook();
        SteamClient::CoreUnhook();
        CloudRedirectHost::Shutdown();
    }

    return TRUE;
}
