// p2p_probe —— Steam 点对点连通性探针（不牵扯任何游戏）
//
// 目的：回答"这两台机器之间，SteamNetworkingSockets 的 P2P 到底能不能建立连接"。
// 用法（两边都要装内核、Steam 已登录；Windows 防火墙放行）：
//   A 机（监听）：p2p_probe.exe listen <虚拟端口> [--seconds 120]
//   B 机（连接）：p2p_probe.exe connect <A的SteamID> <虚拟端口> [--seconds 60]
// 可选：--appid <id>（默认 480）、--api <steam_api64.dll 完整路径>（默认当前目录）
//
// 输出：监听套接字句柄、连接句柄、以及每秒一次的详细连接状态
//   （成功会打印 "Connected"，失败通常打印 5003 Timed out attempting to connect 之类）
//
// 依赖：steam_api64.dll（放在本程序同目录，或用 --api 指定路径）、Steam 客户端在运行。
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

typedef bool (__cdecl *InitFn)();
typedef void* (__cdecl *AccFn)();
typedef unsigned int (__cdecl *ClP2PFn)(void*, int, int, const void*);
typedef int (__cdecl *InitRelayFn)(void*);
typedef unsigned int (__cdecl *ConnFn)(void*, const void*, int, int, const void*);
typedef int (__cdecl *StatFn)(void*, unsigned int, char*, int);
typedef unsigned long long (__cdecl *GetSteamIDFn)(void*);
typedef void (__cdecl *ConnCbFn)(void*);
typedef void (__cdecl *SetConnCbFn)(void*, ConnCbFn);
typedef void (__cdecl *RunCbFn)(void*);
typedef bool (__cdecl *AcceptFn)(void*, unsigned int);

static void* g_pS = nullptr;
static AcceptFn g_accept = nullptr;
static StatFn g_stat = nullptr;

// 各 SDK 版本里"连接状态变化回调"的扁平导出名不一样，运行时按名字里含
// "ConnectionStatusChanged" 找一个，省得写死。
static FARPROC FindExportByNamePart(HMODULE mod, const char* part)
{
    if (!mod) return nullptr;
    unsigned char* base = (unsigned char*)mod;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return nullptr;
    IMAGE_EXPORT_DIRECTORY* exp = (IMAGE_EXPORT_DIRECTORY*)(base + dir.VirtualAddress);
    DWORD* names = (DWORD*)(base + exp->AddressOfNames);
    DWORD* funcs = (DWORD*)(base + exp->AddressOfFunctions);
    WORD* ords = (WORD*)(base + exp->AddressOfNameOrdinals);
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* n = (const char*)(base + names[i]);
        if (n && strstr(n, part)) return (FARPROC)(base + funcs[ords[i]]);
    }
    return nullptr;
}

// 连接状态变化回调：发现新连接就接受，并打印一次
static void __cdecl OnConnStatus(void* pInfo)
{
    if (!pInfo) return;
    const unsigned int hConn = *(const unsigned int*)pInfo;
    printf("[cb] connection event, hConn=%u\n", hConn);
    if (g_accept && g_pS) {
        const bool ok = g_accept(g_pS, hConn);
        printf("[cb] AcceptConnection(%u) -> %s\n", hConn, ok ? "ok" : "fail");
        if (g_stat) {
            char buf[2048]; ZeroMemory(buf, sizeof(buf));
            g_stat(g_pS, hConn, buf, sizeof(buf));
            printf("[cb] status: %s\n", buf[0] ? buf : "(empty)");
        }
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    const char* mode = (argc > 1) ? argv[1] : nullptr;
    if (!mode || (strcmp(mode, "listen") != 0 && strcmp(mode, "connect") != 0)) {
        puts("usage: p2p_probe.exe listen <virtualPort> [--seconds N] [--appid ID] [--api PATH]");
        puts("       p2p_probe.exe connect <peerSteamID> <virtualPort> [--seconds N] [--appid ID] [--api PATH]");
        return 1;
    }

    int vport = 0;
    unsigned long long peer = 0;
    int seconds = 120;
    unsigned long long appid = 480;
    const char* apiPath = "steam_api64.dll";

    int i = 2;
    if (strcmp(mode, "listen") == 0) { if (argc > i) vport = atoi(argv[i++]); }
    else { if (argc > i) peer = _strtoui64(argv[i++], nullptr, 10); if (argc > i) vport = atoi(argv[i++]); }
    for (; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--seconds") == 0) seconds = atoi(argv[++i]);
        else if (strcmp(argv[i], "--appid") == 0) appid = _strtoui64(argv[++i], nullptr, 10);
        else if (strcmp(argv[i], "--api") == 0) apiPath = argv[++i];
    }

    printf("[1] mode=%s virtualPort=%d appid=%llu seconds=%d api=%s\n", mode, vport, appid, seconds, apiPath);

    HMODULE api = LoadLibraryA(apiPath);
    if (!api) { printf("[2] LoadLibrary(%s) failed, err=%lu\n", apiPath, GetLastError()); return 2; }

    InitFn init = (InitFn)GetProcAddress(api, "SteamAPI_InitSafe");
    if (!init || !init()) { puts("[2] SteamAPI_InitSafe failed (Steam 在跑吗？appid 与账号匹配吗？)"); return 3; }
    puts("[2] SteamAPI_InitSafe ok");

    AccFn aS = (AccFn)GetProcAddress(api, "SteamAPI_SteamNetworkingSockets_SteamAPI_v012");
    AccFn aU = (AccFn)GetProcAddress(api, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    if (!aS) { puts("[3] 拿不到 NetworkingSockets 接口"); return 4; }
    g_pS = aS();
    void* pU = aU ? aU() : nullptr;
    printf("[3] sockets=%p utils=%p\n", g_pS, pU);

    InitRelayFn relay = (InitRelayFn)GetProcAddress(api, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    if (relay && pU) printf("[4] InitRelayNetworkAccess -> %d\n", relay(pU));

    ClP2PFn create = (ClP2PFn)GetProcAddress(api, "SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P");
    if (!create) { puts("[5] 找不到 CreateListenSocketP2P"); return 5; }
    const unsigned int hs = create(g_pS, vport, 0, nullptr);
    printf("[5] CreateListenSocketP2P(%d) -> handle=0x%X %s\n", vport, hs, hs ? "VALID" : "INVALID");

    g_stat = (StatFn)GetProcAddress(api, "SteamAPI_ISteamNetworkingSockets_GetDetailedConnectionStatus");
    RunCbFn runcb = (RunCbFn)GetProcAddress(api, "SteamAPI_ISteamNetworkingSockets_RunCallbacks");

    if (strcmp(mode, "listen") == 0) {
        // 有人连进来时要接受它：用连接状态回调自动 Accept
        SetConnCbFn setcb = (SetConnCbFn)FindExportByNamePart(api, "ConnectionStatusChanged");
        g_accept = (AcceptFn)GetProcAddress(api, "SteamAPI_ISteamNetworkingSockets_AcceptConnection");
        if (setcb) { setcb(g_pS, OnConnStatus); puts("[6] 已挂连接状态回调（自动接受新连接）"); }
        else puts("[6] 没有回调接口，只能被动等待（连接可能停在 connecting）");

        for (int t = 0; t < seconds; ++t) {
            if (runcb) runcb(g_pS);
            Sleep(1000);
            if (t % 5 == 4) printf("[7] listening... %ds\n", t + 1);
        }
        puts("[8] listen 结束");
        return 0;
    }

    // connect 模式
    GetSteamIDFn gid = (GetSteamIDFn)GetProcAddress(api, "SteamAPI_ISteamUser_GetSteamID");
    AccFn aUser = (AccFn)GetProcAddress(api, "SteamAPI_SteamUser_v023");
    void* pUser = aUser ? aUser() : nullptr;
    const unsigned long long me = (gid && pUser) ? gid(pUser) : 0;
    printf("[6] 本机 SteamID=%llu  目标 SteamID=%llu\n", me, peer);
    if (!peer) { puts("[7] 没给对端 SteamID"); return 6; }

    unsigned char ident[64]; ZeroMemory(ident, sizeof(ident));
    *(int*)(ident + 0) = 3;                       // k_ESteamNetworkingIdentityType_SteamID
    *(int*)(ident + 4) = 8;
    *(unsigned long long*)(ident + 8) = peer;

    ConnFn connect = (ConnFn)GetProcAddress(api, "SteamAPI_ISteamNetworkingSockets_ConnectP2P");
    if (!connect) { puts("[7] 找不到 ConnectP2P"); return 7; }
    const unsigned int hc = connect(g_pS, ident, vport, 0, nullptr);
    printf("[7] ConnectP2P(peer, virtualPort=%d) -> hConn=0x%X %s\n", vport, hc, hc ? "ACCEPTED" : "INVALID");
    if (!hc) return 8;

    for (int t = 0; t < seconds; ++t) {
        if (runcb) runcb(g_pS);
        Sleep(1000);
        char buf[2048]; ZeroMemory(buf, sizeof(buf));
        if (g_stat) g_stat(g_pS, hc, buf, sizeof(buf));
        printf("[8] t=%3ds  %s\n", t + 1, buf[0] ? buf : "(empty)");
        if (strstr(buf, "Connected")) { puts("[9] 结果: CONNECTED（Steam P2P 可用）"); return 0; }
        if (strstr(buf, "closed")) { puts("[9] 结果: CLOSED（连接没建立）"); return 0; }
    }
    puts("[9] 结果: 到时间仍未连上");
    return 0;
}
