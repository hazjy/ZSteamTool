#pragma once

#include "dllmain.h"

namespace Hooks_Package {
    // LoadPackage + CheckAppOwnership — patches the package store so that
    // user-supplied depots appear owned and accessible.
    void Install();
    void Uninstall();

    // Mark package 0 as changed and trigger CClientAppManager_ProcessPendingLicenseUpdates.
    void NotifyLicenseChanged();

    // 当前账号对该 App 是否持有家庭共享许可（CheckAppOwnership 见过 bFamilyShared/bBorrowed）。
    bool IsSharedLicense(AppId_t appId);

    // 有效许可 = 真拥有（LuaConfig::IsOwned）或家庭共享许可。
    // 需要"这个 App 确实归用户"的判定（移除队列、Denuvo 授权收尾）用它，
    // 不要用 IsOwned —— 否则家庭共享来的游戏会被当成"未拥有"而遭移除或跳过身份对齐。
    bool HasValidLicense(AppId_t appId);

}
