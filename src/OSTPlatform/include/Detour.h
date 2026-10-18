#pragma once

namespace OSTPlatform::Detour {

    bool BeginTransaction();
    bool CommitTransaction();
    // 放弃当前事务（Attach/Detach 中途失败时用）。不放弃会留下悬空事务，
    // 后续模块的 Begin 直接返回错却无人检查 —— 钩子静默没装上。
    bool AbortTransaction();
    bool Attach(void** target, void* detour);
    bool Detach(void** target, void* detour);

} // namespace OSTPlatform::Detour
