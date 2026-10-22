#include "Hooks_SteamUI.h"
#include "Hooks_Package.h"
#include "HookManager.h"
#include "HookMacros.h"
#include "dllmain.h"
#include "steam_messages.pb.h"
#include "Utils/HookSupport/VehCommon.h"
#include <mutex>
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace
{
    RESOLVE_FUNC(RepeatedFieldUint32_Add, void, void* field, const uint32* value);

    CAPTURE_THIS_FUNC(GetAppByID, CSteamApp*, g_pController,void* pThis, AppId_t appId, bool bCreate);
    CAPTURE_THIS_FUNC(MarkAppChange,void*,g_pAppChangeSource,void* pThis,AppId_t appId, EAppChangeFlags changeFlags);

    // 让一个 CSteamApp 看起来"已拥有"。库里的可见性由 OwnershipFlags 决定：
    // 移除路径就是靠清掉它把 app 从库中摘除的，所以新增路径必须反过来把它补上，
    // 否则未安装的 app（没有 acf 当锚点）永远不会出现在库里。
    constexpr uint32 kActiveLicenseMask =
        k_EAppOwnershipFlags_OwnsLicense |
        k_EAppOwnershipFlags_FreeLicense |
        k_EAppOwnershipFlags_SharedLicense;

    inline void ApplyFakeOwnership(CSteamApp* pApp, AppId_t appId)
    {
        if (pApp->OwnershipFlags & k_EAppOwnershipFlags_LicenseLocked)
        {
            pApp->OwnershipFlags = static_cast<EAppOwnershipFlags>(
                pApp->OwnershipFlags & ~k_EAppOwnershipFlags_LicenseLocked);
        }
        if ((pApp->OwnershipFlags & kActiveLicenseMask) == 0)
        {
            pApp->OwnershipFlags = static_cast<EAppOwnershipFlags>(
                pApp->OwnershipFlags |
                k_EAppOwnershipFlags_OwnsLicense |
                k_EAppOwnershipFlags_LicensePermanent);
        }
        const uint32_t t = LuaConfig::GetPurchaseTime(appId);
        if (t) pApp->PurchasedTime = t;
    }

    HOOK_FUNC(FillInAppOverview, void *, void *pThis, void *pAppOverview, CSteamApp *pApp)
    {
        if (pApp && LuaConfig::HasDepot(pApp->nAppID, false))
        {
            ApplyFakeOwnership(pApp, pApp->nAppID);
            LOG_STEAMUI_TRACE("FillInAppOverview: forced ownership for appId={} flags=0x{:X} purchased={}",
                              pApp->nAppID, static_cast<uint32>(pApp->OwnershipFlags), pApp->PurchasedTime);
        }
        return oFillInAppOverview(pThis, pAppOverview, pApp);
    }

    // Library membership is queued off-thread (injection runs on a Steam thread) and
    // applied on the UI thread: removals in CSteamUIAppControllerRunFrame, additions
    // in FillInAppOverview / the same RunFrame drain.
    std::mutex g_removalMutex;
    std::vector<AppId_t> g_pendingRemovals;
    std::unordered_set<AppId_t> g_removedAppIds;
    std::vector<AppId_t> g_pendingAdditions;
    // 每帧最多推这么多，避免一次注入几百个 app 把 UI 卡住。
    constexpr size_t kMaxAdditionsPerFrame = 64;

    void DrainAdditions()
    {
        std::vector<AppId_t> batch;
        size_t remaining = 0;
        {
            std::lock_guard<std::mutex> lock(g_removalMutex);
            if (g_pendingAdditions.empty()) return;
            const size_t n = (std::min)(g_pendingAdditions.size(), kMaxAdditionsPerFrame);
            batch.assign(g_pendingAdditions.begin(), g_pendingAdditions.begin() + n);
            g_pendingAdditions.erase(g_pendingAdditions.begin(), g_pendingAdditions.begin() + n);
            remaining = g_pendingAdditions.size();
        }

        size_t pushed = 0;
        size_t unknown = 0;
        for (AppId_t appId : batch)
        {
            // 只处理 steamui 已经认识的 app（CSteamApp 表由 appinfo 建立）。
            // 绝不 bCreate：那会造出没有 appinfo 的空壳条目，正是 1.2.0 修过的崩溃来源。
            CSteamApp* pApp = oGetAppByID(g_pController, appId, false);
            if (!pApp)
            {
                ++unknown;
                continue;
            }
            ApplyFakeOwnership(pApp, appId);
            oMarkAppChange(g_pAppChangeSource, appId, EAppChangeFlags::AppInfoOrConfig);
            ++pushed;
        }
        LOG_STEAMUI_DEBUG("RunFrame: pushed {} app(s) into the library overview ({} not in appinfo, {} pending)",
                          pushed, unknown, remaining);
    }

    // Apps to drop from the library: queued off-thread, marked on the UI thread.
    // (state declared above — shared by the addition path)

    // A full rebuild never lists removed_appid for apps still in the map
    // so re-assert our set after the snapshot is built.
    HOOK_FUNC(BuildCompleteAppOverviewChange, void, void *pController,
              CAppOverview_Change *pChange, void *optionalCallbackSlot)
    {
        oBuildCompleteAppOverviewChange(pController, pChange, optionalCallbackSlot);
        std::lock_guard<std::mutex> lock(g_removalMutex);
        if (pChange && !g_removedAppIds.empty() && oRepeatedFieldUint32_Add)
        {
            auto* field = pChange->mutable_removed_appid();
            for (AppId_t appId : g_removedAppIds){
                oRepeatedFieldUint32_Add(field, &appId);
            }
            LOG_STEAMUI_DEBUG("BuildCompleteAppOverviewChange: appended {} removed_appid entries",
                              g_removedAppIds.size());
        }
    }


    // Clearing ownership makes ShouldShowAppInLibrary() false (delta drops it,
    // the full snapshot skips it); MarkAppChange triggers the flush.
    HOOK_FUNC(CSteamUIAppControllerRunFrame, void *, void *pController)
    {
        if (CAPTURE_READY(GetAppByID) && CAPTURE_READY(MarkAppChange))
        {
            DrainAdditions();

            std::vector<AppId_t> draining;
            {
                std::lock_guard<std::mutex> lock(g_removalMutex);
                draining.swap(g_pendingRemovals);
            }
            for (AppId_t appId : draining)
            {
                // 有有效许可（真拥有或家庭共享）的 App 不从库里移除：
                // 它的许可来自账号，lua 只是补充 depot，删 lua 不该让它消失。
                if (Hooks_Package::HasValidLicense(appId))
                {
                    LOG_STEAMUI_DEBUG("RunFrame: appId {} still has a valid license, skipping removal", appId);
                    continue;
                }
                if (CSteamApp *pApp = oGetAppByID(g_pController, appId, false))
                {
                    // Only remove from the library if it's not already uninstalled
                    pApp->OwnershipFlags = k_EAppOwnershipFlags_None;
                    if(pApp->AppStateFlags == k_EAppStateUninstalled){
                        std::lock_guard<std::mutex> lock(g_removalMutex);
                        g_removedAppIds.insert(appId);
                    }

                    LOG_STEAMUI_INFO("RunFrame: removing appId {}", appId);

                    // MarkAppChange 必须在 pApp 非空之后调：app 还没进 steamui 的
                    // CSteamApp 表时（GetAppByID(bCreate=false) 返回 null）通知变更
                    // 会撞上 steamui 的未初始化断言，留下 assert_steam.*.dmp。
                    oMarkAppChange(g_pAppChangeSource, appId, EAppChangeFlags::AppInfoOrConfig);
                }
            }
        }
        return oCSteamUIAppControllerRunFrame(pController);
    }
}

namespace Hooks_SteamUI
{
    void Install()
    {
        ARM_CAPTURE_U(GetAppByID);
        ARM_CAPTURE_U(MarkAppChange);

        RESOLVE_U(RepeatedFieldUint32_Add);

        HOOK_BEGIN();
        INSTALL_HOOK_U(FillInAppOverview);
        INSTALL_HOOK_U(BuildCompleteAppOverviewChange);
        INSTALL_HOOK_U(CSteamUIAppControllerRunFrame);
        HOOK_END();
    }

    void Uninstall()
    {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK(FillInAppOverview);
        UNINSTALL_HOOK(BuildCompleteAppOverviewChange);
        UNINSTALL_HOOK(CSteamUIAppControllerRunFrame);
        UNHOOK_END();
    }

    void QueueRemoval(AppId_t appId)
    {
        std::lock_guard<std::mutex> lock(g_removalMutex);
        g_pendingRemovals.push_back(appId);
    }

    void CancelRemoval(AppId_t appId)
    {
        std::lock_guard<std::mutex> lock(g_removalMutex);
        std::erase(g_pendingRemovals, appId);
        g_removedAppIds.erase(appId);
    }

    void QueueAdditions(const std::vector<AppId_t>& appIds)
    {
        if (appIds.empty()) return;
        std::lock_guard<std::mutex> lock(g_removalMutex);
        for (AppId_t id : appIds)
        {
            if (std::ranges::find(g_pendingAdditions, id) == g_pendingAdditions.end())
                g_pendingAdditions.push_back(id);
            // 同一个 app 刚被排队移除又被加回来：移除作废。
            std::erase(g_pendingRemovals, id);
            g_removedAppIds.erase(id);
        }
        LOG_STEAMUI_DEBUG("QueueAdditions: {} id(s) queued, {} pending",
                          appIds.size(), g_pendingAdditions.size());
    }
}
