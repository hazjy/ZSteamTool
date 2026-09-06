#include "Pipe/PipeManager.h"

#include "Pipe/PipeTypes.h"
#include "Pipe/ProcessInspector.h"
#include "Pipe/Features/DenuvoAuth/DenuvoAuth.h"
#include "Pipe/Features/Injection/Injection.h"
#include "Hook/Hooks_Misc.h"
#include "Utils/Logging/Log.h"
#include "Utils/Config/LuaConfig.h"

#include <chrono>
#include <optional>
#include <thread>
#include <unordered_map>

namespace PipeManager {
namespace {

    // OnHandshake runs single-threaded, so this cache needs no lock.
    std::unordered_map<ProcessKey, ProcessInspector::ProcessSnapshot, ProcessKeyHash> g_processes;

    ProcessKey MakeProcessKey(const ProcessInspector::ProcessSnapshot& snapshot) {
        return ProcessKey{snapshot.pid, snapshot.creationTime};
    }

    std::optional<ProcessInspector::ProcessSnapshot> TryReuseCachedProcess(PID_t pid) {
        const auto currentCreationTime = ProcessInspector::GetProcessCreationTime(pid);
        if (!currentCreationTime) return std::nullopt;

        const ProcessKey currentKey{pid, *currentCreationTime};
        const auto it = g_processes.find(currentKey);
        if (it == g_processes.end()) return std::nullopt;

        LOG_PIPE_DEBUG("PipeManager: reusing cached process snapshot pid={} process={}",
                       pid, it->second.DebugString());
        return it->second;
    }

    ProcessInspector::ProcessSnapshot ResolveProcess(PID_t pid) {
        if (auto cached = TryReuseCachedProcess(pid)) return *cached;

        // First sighting: inspect once and cache so sibling pipes reuse it. A dead
        // process yields creationTime=0, so skip caching that junk key.
        const ProcessInspector::ProcessSnapshot snapshot = ProcessInspector::InspectProcess(pid);
        const ProcessKey processKey = MakeProcessKey(snapshot);
        if (processKey.IsValid()) g_processes[processKey] = snapshot;
        return snapshot;
    }

    // Env-based appid first. Falls back to the steamclient pipe's own appid
    // (GetAppIDForCurrentPipe), which can lag the handshake by a few ms for
    // launcher-spawned env-less games; then to the explicit addprocess()
    // process-name mapping. Mirrors upstream PR #148's ordering.
    constexpr int kAppIdResolveRetries = 10;
    constexpr std::chrono::milliseconds kAppIdResolveRetryDelay{20};
    AppId_t ResolveAppIdWithRetry(const ProcessInspector::ProcessSnapshot& snapshot) {
        if (const AppId_t envAppId = snapshot.ResolveAppId(); envAppId != k_uAppIdInvalid)
            return envAppId;
        for (int attempt = 0; attempt < kAppIdResolveRetries; ++attempt) {
            const AppId_t pipeAppId = Hooks_Misc::ResolveAppId();
            if (pipeAppId != k_uAppIdInvalid) {
                LOG_PIPE_DEBUG("PipeManager: appid resolved from pipe on attempt={} appid={}", attempt, pipeAppId);
                return pipeAppId;
            }
            std::this_thread::sleep_for(kAppIdResolveRetryDelay);
        }
        if (!snapshot.imageName.empty()) {
            const AppId_t configAppId = LuaConfig::GetAppIdForProcess(snapshot.imageName);
            if (configAppId != k_uAppIdInvalid) {
                LOG_PIPE_DEBUG("PipeManager: appid from addprocess image={} appid={}", snapshot.imageName, configAppId);
                return configAppId;
            }
        }
        return k_uAppIdInvalid;
    }

} // namespace

void OnHandshake(CPipeClient* pipe) {
    if (!pipe) {
        LOG_PIPE_INFO("PipeManager: ignore null pipe handshake");
        return;
    }

    const PipeKey pipeKey = MakePipeKey(pipe);
    if (pipeKey.pid == 0) {
        LOG_PIPE_DEBUG("PipeManager: ignore handshake with invalid pipe key {}", pipeKey.DebugString());
        return;
    }

    const ProcessInspector::ProcessSnapshot snapshot = ResolveProcess(pipeKey.pid);
    const ProcessKey processKey = MakeProcessKey(snapshot);

    if (!processKey.IsValid()) {
        LOG_PIPE_DEBUG("PipeManager: ignore handshake with invalid process key {} snapshot={}",
                       processKey.DebugString(), snapshot.DebugString());
        return;
    }

    const AppId_t appId = ResolveAppIdWithRetry(snapshot);
    const bool trackedApp = appId != k_uAppIdInvalid && LuaConfig::HasDepot(appId, false);

    PipeContext ctx{};
    ctx.pipe = pipe;
    ctx.process = processKey;
    ctx.appId = appId;
    ctx.gameProcess = snapshot.likelyGameProcess;
    ctx.trackedApp = trackedApp;
    ctx.owned = trackedApp && LuaConfig::IsOwned(appId);

    LOG_PIPE_INFO("PipeManager: handshake {} process={} appid={} trackedApp={} snapshot={}",
                  pipeKey.DebugString(), processKey.DebugString(), appId,
                  trackedApp, snapshot.DebugString());

    // Feature side effects run without holding the registry lock.
    DenuvoAuth::Apply(ctx);
    Injection::Apply(ctx);
}

} // namespace PipeManager
