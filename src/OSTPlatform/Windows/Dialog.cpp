#include "include/Dialog.h"
#include "include/Encoding.h"

#include <windows.h>

namespace {

    // 我们所有源码按 /utf-8 编译，字符串字面量是 UTF-8；MessageBoxA 会按进程 ANSI
    // 代码页（简中是 CP936）解释它 —— 中文必乱码。统一转成 UTF-16 走 MessageBoxW。
    std::wstring ToWide(const std::string& utf8) {
        std::wstring wide = OSTPlatform::Encoding::Utf8ToWide(utf8);
        return wide;
    }

    // 消息框按调用线程的 DPI 上下文绘制。steam.exe 自身是 PerMonitorV2，但注入线程
    // 拿到的上下文未必一致；不一致时 Windows 会按系统 DPI 画好再位图拉伸 —— 表现为
    // "边缘发虚"。这里只在当前线程临时提升，弹完立刻恢复（线程级，不影响 Steam 本体）。
    // 动态取函数指针：Win10 1607 之前没有这个 API，取不到就按原样弹。
    using SetThreadDpiAwarenessContextFn = HANDLE (WINAPI*)(HANDLE);

    SetThreadDpiAwarenessContextFn DpiSetter() {
        static const auto fn = []() -> SetThreadDpiAwarenessContextFn {
            auto* user32 = GetModuleHandleW(L"user32.dll");
            return user32 ? reinterpret_cast<SetThreadDpiAwarenessContextFn>(
                                GetProcAddress(user32, "SetThreadDpiAwarenessContext"))
                          : nullptr;
        }();
        return fn;
    }

    int ShowBox(const std::string& title, const std::string& message, UINT flags) {
        const std::wstring titleW   = ToWide(title);
        const std::wstring messageW = ToWide(message);

        // 转换失败（理论上只有超长/非法 UTF-8）时退回 ANSI，至少别弹个空框。
        if (messageW.empty() && !message.empty()) {
            return MessageBoxA(nullptr, message.c_str(), title.c_str(), flags);
        }

        const auto setter = DpiSetter();
        HANDLE previous = nullptr;
        if (setter)
            previous = setter(static_cast<HANDLE>(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

        const int result = MessageBoxW(nullptr, messageW.c_str(), titleW.c_str(), flags);

        if (setter && previous)
            setter(previous);
        return result;
    }

} // namespace

namespace OSTPlatform::Dialog {

void ShowWarning(std::string title, std::string message) {
    ShowBox(title, message, MB_OK | MB_ICONWARNING | MB_TOPMOST);
}

void ShowInfo(std::string title, std::string message) {
    ShowBox(title, message, MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
}

bool ShowConfirm(std::string title, std::string message) {
    return ShowBox(title, message, MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST) == IDYES;
}

} // namespace OSTPlatform::Dialog
