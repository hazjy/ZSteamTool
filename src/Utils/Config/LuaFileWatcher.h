#pragma once

#include <string>
#include <vector>

namespace LuaFileWatcher {
    void Start(const std::vector<std::string>& directories);
    void Stop();
    // 进程退出时用：只置停止标志并 detach，不 join（线程已被系统终止）。
    void Detach();
}
