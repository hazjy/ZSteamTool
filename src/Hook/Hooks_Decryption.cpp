#include "Hooks_Decryption.h"
#include "HookMacros.h"
#include "dllmain.h"
#include <atomic>
#include <charconv>
#include <string>

namespace {

    // 这个指针被两条线程读（钩子线程与 ReadConfigStoreLocalBinary 的调用方），
    // 原先是无同步裸写 —— 用 atomic 即可，不需要锁。
    std::atomic<void*> g_pConfigStoreLocal{nullptr};

    HOOK_FUNC(ConfigStoreGetBinary, int32, void* pObject, EConfigStore eConfigStore, const char* KeyName, char* Key, uint32 KeySize) {
        if (eConfigStore == k_EConfigStoreUserLocal && pObject &&
            !g_pConfigStoreLocal.load(std::memory_order_relaxed)) {
            g_pConfigStoreLocal.store(pObject, std::memory_order_release);
            LOG_DECRYPTIONKEY_DEBUG("ConfigStoreGetBinary: captured local ConfigStore at {}", pObject);

        }
        if (!KeyName) return oConfigStoreGetBinary(pObject, eConfigStore, KeyName, Key, KeySize);
        std::string name(KeyName);
        LOG_DECRYPTIONKEY_DEBUG("ConfigStore::GetBinary called for pObject={}, eConfigStore={}, KeyName='{}'", 
                                    pObject, static_cast<uint32>(eConfigStore), name);
        // Expected shape: "...\<DepotId>\DecryptionKey"（'/' 分隔同样接受）
        const size_t keyPos = name.rfind("DecryptionKey");
        if (keyPos != std::string::npos && keyPos > 0 &&
            (name[keyPos - 1] == '\\' || name[keyPos - 1] == '/')) {
            const size_t sep = keyPos - 1;
            if (size_t start = name.find_last_of("\\/", sep > 0 ? sep - 1 : 0); start != std::string::npos) {
                // 键名来自 steamclient，可能形如 "...\<非数字>\DecryptionKey"：
                // 用 from_chars 而不是 std::stoul —— 后者会抛异常，异常穿过钩子
                // 等于把 Steam 带走。解析不出来就当不是密钥键，原样放行。
                const std::string depotPart = name.substr(start + 1, sep - start - 1);
                AppId_t depotId = 0;
                const auto* first = depotPart.data();
                const auto* lastCh = depotPart.data() + depotPart.size();
                const auto parsed = std::from_chars(first, lastCh, depotId);
                const bool ok = !depotPart.empty() && parsed.ec == std::errc{} && parsed.ptr == lastCh;
                if (ok) {
                    if (const auto& key = LuaConfig::GetDecryptionKey(depotId); !key.empty()) {
                        if (!Key) {
                            LOG_DECRYPTIONKEY_WARN("Decryption key for depot {} requested with null buffer", depotId);
                        } else if (KeySize >= key.size()) {
                            LOG_DECRYPTIONKEY_INFO("Providing decryption key for depot {}: {}", depotId,
                                                   spdlog::to_hex(key.data(), key.data() + key.size()));
                            memcpy(Key, key.data(), key.size());
                            return static_cast<int32>(key.size());
                        } else {
                            LOG_DECRYPTIONKEY_WARN("Decryption key for depot {} is too large ({} bytes) for buffer ({} bytes)",
                                                    depotId, key.size(), KeySize);
                        }
                    }
                }
            }
        }
        return oConfigStoreGetBinary(pObject, eConfigStore, KeyName, Key, KeySize);
    }

    std::vector<uint8_t> ReadConfigStoreLocalBinary(const std::string& keyName) {
        void* const configStore = g_pConfigStoreLocal.load(std::memory_order_acquire);
        if (!configStore || !oConfigStoreGetBinary) {
            LOG_DECRYPTIONKEY_WARN("GetConfigStoreLocalBinary: ConfigStoreGetBinary not ready, cannot get binary value");
            return {};
        }

        std::vector<uint8_t> value(1024);
        int32 result = oConfigStoreGetBinary(configStore, k_EConfigStoreUserLocal,
                                             keyName.c_str(),
                                             reinterpret_cast<char*>(value.data()),
                                             static_cast<uint32>(value.size()));
        if (result <= 0) {
            LOG_DECRYPTIONKEY_DEBUG("GetConfigStoreLocalBinary: failed to read KeyName='{}'", keyName);
            return {};
        }

        value.resize(result);
        LOG_DECRYPTIONKEY_DEBUG("GetConfigStoreLocalBinary: got value for KeyName='{}' ({} bytes)",
                                keyName, value.size());
        return value;
    }
}

namespace Hooks_Decryption {
    void Install() {
        HOOK_BEGIN();
        INSTALL_HOOK_C(ConfigStoreGetBinary);
        HOOK_END();
    }

    void Uninstall() {
        UNHOOK_BEGIN();
        UNINSTALL_HOOK_C(ConfigStoreGetBinary);
        UNHOOK_END();
    }

    std::vector<uint8_t> GetCacheAppOwnershipTicket(AppId_t appId) {
        std::vector<uint8_t> ticket = ReadConfigStoreLocalBinary(std::format("apptickets\\{}", appId));
        if (ticket.empty()) {
            LOG_DECRYPTIONKEY_DEBUG("no cached ticket for AppId {}", appId);
            return ticket;
        }
        LOG_DECRYPTIONKEY_DEBUG("got cached ticket for AppId {} ({} bytes)", appId, ticket.size());
        return ticket;
    }
}
