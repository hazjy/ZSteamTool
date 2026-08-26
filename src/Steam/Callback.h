#pragma once

// ── ISteamUser callbacks (base = 100) ───────────────────────────────

constexpr int k_iSteamUserCallbacks = 100;

//-----------------------------------------------------------------------------
// Purpose: Result from RequestEncryptedAppTicket (async)
//-----------------------------------------------------------------------------
struct EncryptedAppTicketResponse_t
{
	enum { k_iCallback = k_iSteamUserCallbacks + 54 };

	EResult m_eResult;
};

//-----------------------------------------------------------------------------
// Purpose: Broadcast when app licenses change (additions / removals / reload).
//          Sent by CClientAppManager after ProcessPendingLicenseUpdates.
//-----------------------------------------------------------------------------
struct AppLicensesChanged_t
{
	enum { k_iCallback = 1020094 };

	bool      m_bReloadAll;                // 0x00  — true = full library refresh
	bool      m_bIsFirstLoad;              // 0x01
	uint32    m_unRemainingPackets;         // 0x04
	uint32    m_unCount;                    // 0x08  — number of entries in m_rgAppsUpdated
	AppId_t   m_rgAppsUpdated[64];         // 0x0C  — batch of updated AppIds
	uint64    m_unAppsAdded;               // 0x110 — bitmask: bit N = m_rgAppsUpdated[N] was added
};

// ── ISteamMatchmaking callbacks (base = 300) ────────────────────────

constexpr int k_iSteamMatchmakingCallbacks = 300;

//-----------------------------------------------------------------------------
// Purpose: Posted when the local user receives a lobby invitation.
//          Layout mirrors isteammatchmaking.h from the public Steamworks SDK.
// OnlineFix note: for games launched with -onlinefix the lobby lives in
// Spacewar(480) space while the game believes its own AppId is the real one;
// SendCallbackToPipe rewrites m_ulGameID back to the real AppId so in-game
// invite validation (invite.gameID == GetAppID()) passes.
//-----------------------------------------------------------------------------
struct LobbyInvite_t
{
	enum { k_iCallback = k_iSteamMatchmakingCallbacks + 3 };

	uint64 m_ulGameID;        // game id of the lobby (480 under -onlinefix)
	uint64 m_ulSteamIDLobby;  // lobby steam id
	uint64 m_ulSteamIDUser;   // inviter steam id
};
