#pragma once

#include "OSTPlatform/include/DynamicLibrary.h"

#include <string>

namespace PatternLoader {

    // Load metadata before installing hooks for a module.
    bool Load(OSTPlatform::DynamicLibrary::ModuleHandle module, const std::string& dllPath, const std::string& component);

    // Resolve by RVA first, then fall back to signature scanning.
    void* FindPattern(OSTPlatform::DynamicLibrary::ModuleHandle module, const char* funcName);

    // Report unresolved functions after all hooks have been installed.
    // refreshQueued = 配套的签名表已在后台重取（见下）—— 文案要说清"重启后生效"，
    // 否则用户会以为缺函数这件事没法自愈。
    void ReportMissingFunctions(bool refreshQueued = false);

    // 缓存命中但表里缺函数时，异步重取一次签名表（每会话每模块最多一次）。
    // 表不热载，写进缓存供下次启动使用；调用方在本会话启动路径上直接继续，
    // 不会被它拖慢。返回是否真的排上了重取（供弹窗文案用）。
    bool RefreshMissingMetadata();

} // namespace PatternLoader
