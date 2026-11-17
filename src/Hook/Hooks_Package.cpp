#include "Hooks_Package.h"
#include "HookMacros.h"
#include "Hooks_SteamUI.h"
#include "dllmain.h"
#include "Utils/HookSupport/VehCommon.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace {
    RESOLVE_FUNC(CUtlMemoryGrow,               void*, CUtlVector<AppId_t>*, int);
    RESOLVE_FUNC(MarkLicenseAsChanged,         int64, void*, uint32, bool);
    RESOLVE_FUNC(ProcessPendingLicenseUpdates, bool,  void*);

    CAPTURE_THIS_FUNC(GetPackageInfo, PackageInfo*,g_pCPackageInfo,void* pThis, uint32 packageId, uint64 accessToken);
    
    void* g_pCUser = nullptr;
    PackageInfo* g_pInjectedPackageInfo = nullptr;
    bool  g_licenseInitialized = false;
    bool  g_licenseRefreshPending = false;

    // 家庭共享许可集合：CheckAppOwnership 见过 bFamilyShared/bBorrowed 的 appid。
    // 真拥有走 LuaConfig::OwnedAppIdSet，两个集合互斥，合起来构成 HasValidLicense。
    // CheckAppOwnership 跑在 Steam 线程上，热重载线程也会读，故加锁。
    std::mutex g_licenseMutex;
    std::unordered_set<AppId_t> g_sharedLicenseApps;

    // 家庭共享的出借方 accountid，按 AppId 记录。
    // Steam 在 bFamilyShared 置位期间把 AppOwnership::SteamId32 填成出借人，
    // 假入库分支把标记清掉之后，只有这张表还能反查归属：
    //   1. 纯共享游戏上报时把 owner 还原成出借方，客户端和服务器账本才对得上
    //   2. 判断某个 AppId 是不是家庭库来的（lua 游戏据此改写上报 AppId）
    // 只认借用标记：清掉之后 SteamId32 会被 Steam 改写，拿它做判断不可信。
    // 记录写下后不再改动 —— 漏记会让整套共享修复失效，宁可留着多余的。
    std::mutex g_familySharedOwnersMutex;
    std::unordered_map<AppId_t, AccountID_t> g_familySharedOwners;

    bool HasFamilySharedOwner(AppId_t appId) {
        std::lock_guard<std::mutex> lock(g_familySharedOwnersMutex);
        return g_familySharedOwners.find(appId) != g_familySharedOwners.end();
    }

    constexpr PackageId_t kInjectedPackageId = 0;
    constexpr uint64_t kInjectedPkgAccessToken = 10660652434190618804ull;

    bool MarkLicenseAsChangedAndProcessUpdates() {
        if (!g_pCUser || !oMarkLicenseAsChanged || !oProcessPendingLicenseUpdates) {
            LOG_PACKAGE_WARN("MarkLicenseAsChangedAndProcessUpdates: dependencies not ready, skipping");
            return false;
        }
        oMarkLicenseAsChanged(g_pCUser, kInjectedPackageId, true);
        oProcessPendingLicenseUpdates(g_pCUser);
        LOG_PACKAGE_DEBUG("MarkLicenseAsChangedAndProcessUpdates: marked package {} as changed and processed updates", kInjectedPackageId);
        return true;
    }

    void TryProcessPendingLicenseRefresh() {
        if (!g_licenseRefreshPending)
            return;
        if (MarkLicenseAsChangedAndProcessUpdates())
            g_licenseRefreshPending = false;
    }

    bool CUtlMemoryGrowWrap(CUtlVector<AppId_t>* pVec, int grow_size) {
        if (!oCUtlMemoryGrow) {
            LOG_PACKAGE_WARN("CUtlMemoryGrow: oCUtlMemoryGrow not ready, cannot grow");
            return false;
        }
        return oCUtlMemoryGrow(pVec, grow_size);
    }

    // 包 0 是按 m_Size 遍历的外部向量。同一个 appid 若已在其中就不再写第二份：
    // 热重载的"先加后删"很容易造出重复项，而移除只会摘走一份、留下残影。
    bool AppIdVecContains(const CUtlVector<AppId_t>& vec, AppId_t id) {
        for (uint32 i = 0; i < vec.m_Size; ++i)
            if (vec.m_Memory.m_pMemory[i] == id) return true;
        return false;
    }

    bool InitFakeLicenseOnce(PackageInfo* pPkg) {
        // check package status before injecting
        if (pPkg->Status != EPackageStatus::Available) {
            LOG_PACKAGE_WARN("InitFakeLicenseOnce: package status is not Available ({}), skipping injection", static_cast<int>(pPkg->Status));
            return false;
        }

        // Inject all depots from config into the fake license. 
        std::vector<AppId_t> appIds = LuaConfig::GetAllDepotIds();
        if (!appIds.empty()) {
            uint32 oldSize = pPkg->AppIdVec.m_Size;
            uint32 numToAdd = static_cast<uint32>(appIds.size());
            LOG_PACKAGE_INFO("InitFakeLicense(PackageId={}): adding {} apps, oldSize={}", kInjectedPackageId, numToAdd, oldSize);
            if (!CUtlMemoryGrowWrap(&pPkg->AppIdVec, numToAdd)) {
                LOG_PACKAGE_WARN("InitFakeLicense(PackageId={}): failed to grow AppId vector", kInjectedPackageId);
                return false;
            }
            uint32 inserted = 0;
            for (uint32 i = 0; i < numToAdd; i++) {
                if (AppIdVecContains(pPkg->AppIdVec, appIds[i])) continue;  // 已在包里，不重复写
                pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + inserted] = appIds[i];
                ++inserted;
            }
            // Grow 只扩容量不改长度，回写长度保证注入的 AppId 对按长度遍历的逻辑可见
            pPkg->AppIdVec.m_Size = oldSize + inserted;
            if (inserted != numToAdd)
                LOG_PACKAGE_DEBUG("InitFakeLicense(PackageId={}): skipped {} duplicate appid(s)",
                                  kInjectedPackageId, numToAdd - inserted);
        }

        g_licenseInitialized = true;
        g_licenseRefreshPending = true;
        TryProcessPendingLicenseRefresh();
        // 注入只让 Steam"认为拥有"，并不会把未安装的 app 放进库：库的可见性由
        // steamui 侧 CSteamApp 的 OwnershipFlags 决定，得由我们自己推一次（否则
        // 未安装的游戏全部缺席，已安装的因为 acf 锚点仍能显示）。
        Hooks_SteamUI::QueueAdditions(appIds);
        return true;
    }

    bool TryInitFakeLicenseOnce() {
        if (g_licenseInitialized) return true;
        if(CAPTURE_READY(GetPackageInfo)){
            PackageInfo* pPkg = oGetPackageInfo(g_pCPackageInfo, kInjectedPackageId, kInjectedPkgAccessToken);
            if(!pPkg) {
                LOG_PACKAGE_WARN("TryInitFakeLicenseOnce: GetPackageInfo returned null for injected package");
                return false;
            }
            if(!g_pInjectedPackageInfo) g_pInjectedPackageInfo = pPkg;
            return InitFakeLicenseOnce(pPkg);
        }
        return false;
    }


    HOOK_FUNC(CheckAppOwnership, bool, void* pObj, AppId_t appId, AppOwnership* pOwn) {
        if (!g_pCUser) {
            g_pCUser = pObj;
            LOG_PACKAGE_DEBUG("CheckAppOwnership: captured CUser {}", g_pCUser);
        }

        bool result = oCheckAppOwnership(pObj, appId, pOwn);
        TryInitFakeLicenseOnce();

        // 先记原始共享状态再动 pOwn：下面的注入分支会改写这些标记，不记就丢了。
        // 每次调用都同步写入/移除 —— 出借者收回后 Steam 会重查，此时标记消失，
        // 若不跟着移除就会被永久误判为共享。
        {
            std::lock_guard<std::mutex> lock(g_licenseMutex);
            if (pOwn && (pOwn->bFamilyShared || pOwn->bBorrowed))
                g_sharedLicenseApps.insert(appId);
            else
                g_sharedLicenseApps.erase(appId);
        }

        // 记下出借方归属。必须排在下面清标记之前：清完就再也认不出来了。
        if (pOwn && (pOwn->bFamilyShared || pOwn->bBorrowed) && pOwn->SteamId32 != 0) {
            bool recorded = false;
            {
                std::lock_guard<std::mutex> lock(g_familySharedOwnersMutex);
                AccountID_t& slot = g_familySharedOwners[appId];
                if (slot != pOwn->SteamId32) {
                    slot = pOwn->SteamId32;
                    recorded = true;
                }
            }
            if (recorded) {
                LOG_PACKAGE_INFO("FamilySharing: recorded lender {} for AppId={} (familyShared={} borrowed={})",
                                 pOwn->SteamId32, appId, pOwn->bFamilyShared, pOwn->bBorrowed);
            }
        }

        // 第二个参数传 true：已经标记为真拥有的 AppId 直接跳过，不再重复判定。
        if (pOwn && LuaConfig::HasDepot(appId, true)) {
            // 真拥有的游戏写进 lua 通常是为了补 DLC，主游戏本身不该改走假入库：
            // 那样会把 Steam 的许可判定覆盖掉，购买状态、成就、时长都会挂到假许可上。
            //
            // 判据要同时满足三条，缺一不可：
            //   1. result：Steam 认这条许可（假入库的也认，所以单看它不够）
            //   2. ExistInPackageNums > 1：真实包和假许可包同时覆盖
            //   3. 没有出借方记录：共享游戏也会因为注入假包让这个值涨到 2，
            //      而且它的 bFamilyShared 在首轮就被下面清掉了，光看标记认不出来。
            //      g_familySharedOwners 里的记录在清标记之前就写下了，不会漏。
            const bool borrowed = pOwn->bFamilyShared || pOwn->bBorrowed;
            const bool knownShared = HasFamilySharedOwner(appId);
            if (result && pOwn->ExistInPackageNums > 1 && !borrowed && !knownShared) {
                LuaConfig::MarkOwned(appId);
                pOwn->ReleaseState = EAppReleaseState::Released;
                LOG_PACKAGE_INFO("FakeLicense: AppId={} 自己拥有，按正版处理", appId);
                return result;
            }

            // 剩下的走假入库：家庭库里也有的游戏因此不会占用出借方名额。
            // 借用相关的标记必须一并抹掉，否则 Steam 后续仍会按共享许可
            // 追踪这条 AppId，占用和到期收回照旧发生 —— 服务端会连着下发
            // 停玩通知，倒计时走完直接断开会话（返回码 6 = 别处登录）。
            pOwn->PackageId          = kInjectedPackageId;
            pOwn->ReleaseState       = EAppReleaseState::Released;
            pOwn->bOwnsLicense       = true;
            pOwn->bFreeLicense       = false;
            pOwn->bFamilyShared      = false;
            pOwn->bBorrowed          = false;
            pOwn->bLicenseLocked     = false;
            pOwn->bLicensePending    = false;
            pOwn->bLicenseExpired    = false;
            pOwn->bGuestPass         = false;
            pOwn->bLicensePermanent  = true;
            return true;
        }

        return result;
    }
}

namespace Hooks_Package {
    bool IsSharedLicense(AppId_t appId) {
        std::lock_guard<std::mutex> lock(g_licenseMutex);
        return g_sharedLicenseApps.count(appId) != 0;
    }

    bool HasValidLicense(AppId_t appId) {
        if (LuaConfig::IsOwned(appId)) return true;
        return IsSharedLicense(appId);
    }

    AccountID_t GetFamilySharedOwner(AppId_t appId) {
        std::lock_guard<std::mutex> lock(g_familySharedOwnersMutex);
        auto it = g_familySharedOwners.find(appId);
        return it == g_familySharedOwners.end() ? 0 : it->second;
    }

    void Install() {
        RESOLVE_C(CUtlMemoryGrow);
        RESOLVE_C(MarkLicenseAsChanged);
        RESOLVE_C(ProcessPendingLicenseUpdates);

        ARM_CAPTURE_C(GetPackageInfo);

        HOOK_BEGIN();
        INSTALL_HOOK_C(CheckAppOwnership);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK_C(CheckAppOwnership);
        UNHOOK_END();
    }

    void NotifyLicenseChanged() {
        PackageInfo* pPkg = g_pInjectedPackageInfo;
        if (!pPkg) {
            LOG_PACKAGE_WARN("NotifyLicenseChanged: injected PackageInfo not ready, cannot notify");
            return;
        }

        // ── Remove depots that were unloaded ──
        std::vector<AppId_t> removals = LuaConfig::TakePendingRemovals();
        LOG_PACKAGE_DEBUG("NotifyLicenseChanged: processing {} removals", removals.size());
        uint32_t removedCount = 0;
        for (AppId_t id : removals) {
            // 同一 appid 可能因重复注入而存在多份，一次摘净，避免删了 lua 还残留条目。
            uint32_t removedForId = 0;
            while (pPkg->AppIdVec.FindAndFastRemove(id)) ++removedForId;
            if (removedForId > 0) {
                removedCount += removedForId;
                LOG_PACKAGE_DEBUG("NotifyLicenseChanged: removed AppId {} ({} entr{})",
                                  id, removedForId, removedForId == 1 ? "y" : "ies");
            } else {
                LOG_PACKAGE_WARN("NotifyLicenseChanged: AppId {} not found in package AppIdVec during removal", id);
            }
        }

        // ── Add depots that are newly loaded ──
        std::vector<AppId_t> additions = LuaConfig::TakePendingAdditions();
        std::unordered_set<AppId_t> addedIds;
        LOG_PACKAGE_DEBUG("NotifyLicenseChanged: processing {} additions", additions.size());
        if (!additions.empty()) {
            uint32_t oldSize = pPkg->AppIdVec.m_Size;
            if (CUtlMemoryGrowWrap(&pPkg->AppIdVec, additions.size())) {
                // An applied addition invalidates any UI removal that has not
                // reached the UI thread yet.
                for (AppId_t id : additions)
                    Hooks_SteamUI::CancelRemoval(id);

                uint32_t inserted = 0;
                for (size_t i = 0; i < additions.size(); ++i) {
                    addedIds.insert(additions[i]);
                    if (AppIdVecContains(pPkg->AppIdVec, additions[i])) continue;  // 已在包里
                    pPkg->AppIdVec.m_Memory.m_pMemory[oldSize + inserted] = additions[i];
                    LOG_PACKAGE_DEBUG("NotifyLicenseChanged: inserted AppId {} at [{}]", additions[i], oldSize + inserted);
                    ++inserted;
                }
                // Grow 只扩容量不改长度，回写长度保证注入的 AppId 对按长度遍历的逻辑可见
                pPkg->AppIdVec.m_Size = oldSize + inserted;
            }else {
                LOG_PACKAGE_WARN("NotifyLicenseChanged: failed to grow AppId vector for additions");
            }
        }

        if (addedIds.empty() && removedCount == 0) {
            LOG_PACKAGE_DEBUG("NotifyLicenseChanged: no changes");
            return;
        }

        // Mark package 0 as changed and trigger library refresh.
        if (!MarkLicenseAsChangedAndProcessUpdates()) {
            LOG_PACKAGE_WARN("NotifyLicenseChanged: failed to mark license as changed");
            return;
        }
        LOG_PACKAGE_INFO("NotifyLicenseChanged: {} added, {} removed", addedIds.size(), removedCount);

        // 新加载的 app 同样要推进库（见 InitFakeLicenseOnce 里的说明）。
        if (!addedIds.empty()) {
            std::vector<AppId_t> pushedAdditions(addedIds.begin(), addedIds.end());
            Hooks_SteamUI::QueueAdditions(pushedAdditions);
        }

        // Queue UI removals for the main-thread RunFrame hook to drain.
        // Never touch MarkAppChange from this (FileWatcher) thread.
        size_t queuedRemovalCount = 0;
        for (AppId_t id : removals) {
            // ParseFile unloads the old file before parsing the replacement.
            // Do not queue that transient removal when the id was added again.
            // 也不排队"用户本来就持有许可"的 App：家庭共享/真拥有的游戏不该因为
            // lua 被删改就从库里消失（它的许可来自账号，而不是 lua）。
            if (!addedIds.contains(id) && !HasValidLicense(id)) {
                Hooks_SteamUI::QueueRemoval(id);
                ++queuedRemovalCount;
            }
        }
        LOG_PACKAGE_DEBUG("NotifyLicenseChanged: queued {} UI removals, skipped {} transient removals",
                          queuedRemovalCount, removals.size() - queuedRemovalCount);
    }
}
