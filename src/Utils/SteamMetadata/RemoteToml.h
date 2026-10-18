#pragma once
#include <string>

namespace RemoteToml {

    struct Request {
        std::string channel;    // "pattern" or "ipc"
        std::string component;  // "steamclient" or "steamui"
        std::string dllPath;
        // 跳过缓存短路、强制走镜像链：给"缓存里的表被判定为坏表"时的重取用。
        bool forceNetwork = false;
    };

    struct Result {
        bool        ok        = false;
        bool        fromCache = false;
        std::string body;
        std::string sha256;
        // 仅失败时有意义：所有镜像都回 404（= 上游确实没发布这张表，值得提示用户）；
        // false = 网络故障/超时（= 静默，下次再试）。成功时恒 false。
        bool        allNotFound = false;
    };

    // Fetch remote TOML first, then fall back to the exact local cache entry.
    Result Fetch(const Request& request);

    // 删除该请求对应的缓存条目（毒缓存自愈用）。返回是否真的删掉了文件。
    bool RemoveCacheEntry(const Request& request);

} // namespace RemoteToml
