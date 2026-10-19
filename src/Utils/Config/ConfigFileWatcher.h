#pragma once

#include <string>

namespace ConfigFileWatcher {
    void Start(const std::string& configPath, const std::string& defaultLuaDir);
    void Stop();
    // 进程退出时用：只置停止标志并 detach，不 join（线程已被系统终止）。
    void Detach();
}
