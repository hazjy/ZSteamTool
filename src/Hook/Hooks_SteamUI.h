#pragma once

#include "dllmain.h"

#include <vector>

// Hooks targeting steamui.dll:

namespace Hooks_SteamUI {
    void Install();
    void Uninstall();

    // Queues an appId for removal from the library UI
    void QueueRemoval(AppId_t appId);
    // Cancels a queued removal when the app is added again before the UI drains it.
    void CancelRemoval(AppId_t appId);
    // Queues appIds to be pushed into the library overview (drained on the UI thread).
    // 注入的许可不会让 Steam 自己把未安装的 app 放进库，必须由这里主动推一次。
    void QueueAdditions(const std::vector<AppId_t>& appIds);
}
