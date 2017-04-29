/*

Copyright [2010] [Josko Nikolic]

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

CODE PORTED FROM THE ORIGINAL GHOST PROJECT: http://ghost.pwner.org/

*/

#ifndef AURA_GAME_H_
#define AURA_GAME_H_

#include "gameslot.h"

//
// CGame
//

class CAura;
class CTCPServer;
class CGameProtocol;
class CPotentialPlayer;
class CGamePlayer;
class CMap;
class CIncomingJoinPlayer;
class CIncomingAction;
class CIncomingChatPlayer;
class CIncomingMapSize;

class CGame
{
public:
	CAura *m_Aura;

protected:
	CTCPServer *m_Socket;                         // listening socket
	CGameProtocol *m_Protocol;                    // game protocol
	std::vector<CGameSlot> m_Slots;               // std::vector of slots
	std::vector<CPotentialPlayer *> m_Potentials; // std::vector of potential players (connections that haven't sent a W3GS_REQJOIN packet yet)
	std::vector<CGamePlayer *> m_Players;         // std::vector of players
	std::queue<CIncomingAction *> m_Actions;      // queue of actions to be sent
	const CMap *m_Map;                            // map data
	std::string m_GameName;                       // game name
	std::string m_VirtualHostName;                // host's name
	uint32_t m_RandomSeed;                        // the random seed sent to the Warcraft III clients
	uint32_t m_HostCounter;                       // a unique game number
	uint32_t m_EntryKey;                          // random entry key for LAN, used to prove that a player is actually joining from LAN
	uint32_t m_Latency;                           // the number of ms to wait between sending action packets (we queue any received during this time)
	uint32_t m_SyncLimit;                         // the maximum number of packets a player can fall out of sync before starting the lag screen
	uint32_t m_SyncCounter;                       // the number of actions sent so far (for determining if anyone is lagging)
	uint32_t m_LastPingTime;                      // GetTime when the last ping was sent
	uint32_t m_LastDownloadTicks;                 // GetTicks when the last map download cycle was performed
	uint32_t m_LastDownloadCounterResetTicks;     // GetTicks when the download counter was last reset
	uint32_t m_LastCountDownTicks;                // GetTicks when the last countdown message was sent
	uint32_t m_CountDownCounter;                  // the countdown is finished when this reaches zero
	uint32_t m_LastLagScreenResetTime;            // GetTime when the "lag" screen was last reset
	uint32_t m_LastActionSentTicks;               // GetTicks when the last action packet was sent
	uint32_t m_LastActionLateBy;                  // the number of ticks we were late sending the last action packet by
	uint32_t m_StartedLaggingTime;                // GetTime when the last lag screen started
	uint32_t m_LastLagScreenTime;                 // GetTime when the last lag screen was active (continuously updated)
	uint16_t m_HostPort;                          // the port to host games on
	uint8_t m_VirtualHostPID;                     // host's PID
	bool m_Exiting;                               // set to true and this class will be deleted next update
	bool m_SlotInfoChanged;                       // if the slot info has changed and hasn't been sent to the players yet (optimization)
	bool m_CountDownStarted;                      // if the game start countdown has started or not
	bool m_GameLoading;                           // if the game is currently loading or not
	bool m_GameLoaded;                            // if the game has loaded or not
	bool m_Lagging;                               // if the lag screen is active or not
	bool m_Desynced;                              // if the game has desynced or not

public:
	CGame(CAura *nAura, const CMap *nMap, std::string &nGameName);
	~CGame();
	CGame(CGame &) = delete;

	inline const CMap *GetMap() const                 { return m_Map; }
	inline CGameProtocol *GetProtocol() const         { return m_Protocol; }
	inline uint32_t GetEntryKey() const               { return m_EntryKey; }
	inline uint16_t GetHostPort() const               { return m_HostPort; }
	inline std::string GetGameName() const            { return m_GameName; }
	inline std::string GetVirtualHostName() const     { return m_VirtualHostName; }
	inline uint32_t GetHostCounter() const            { return m_HostCounter; }

	uint32_t GetNextTimedActionTicks() const;
	uint32_t GetNumPlayers() const;

	inline void SetExiting(bool nExiting)                      { m_Exiting = nExiting; }

	// processing functions

	uint32_t SetFD(void *fd, void *send_fd, int32_t *nfds);
	bool Update(void *fd, void *send_fd);
	void UpdatePost(void *send_fd);

	// generic functions to send packets to players

	void Send(CGamePlayer *player, const BYTEARRAY &data);
	void SendAll(const BYTEARRAY &data);

	// functions to send packets to players

	void SendAllChat(const std::string &message);
	void SendAllSlotInfo();
	void SendVirtualHostPlayerInfo(CGamePlayer *player);
	void SendAllActions();

	// events
	// note: these are only called while iterating through the m_Potentials or m_Players std::vectors
	// therefore you can't modify those std::vectors and must use the player's m_DeleteMe member to flag for deletion

	void EventPlayerDeleted(CGamePlayer *player);
	void EventPlayerDisconnectTimedOut(CGamePlayer *player);
	void EventPlayerDisconnectSocketError(CGamePlayer *player);
	void EventPlayerDisconnectConnectionClosed(CGamePlayer *player);
	void EventPlayerJoined(CPotentialPlayer *potential, CIncomingJoinPlayer *joinPlayer);
	void EventPlayerLeft(CGamePlayer *player, uint32_t reason);
	void EventPlayerLoaded(CGamePlayer *player);
	void EventPlayerAction(CGamePlayer *player, CIncomingAction *action);
	void EventPlayerKeepAlive(CGamePlayer *player);
	void EventPlayerChatToHost(CGamePlayer *player, CIncomingChatPlayer *chatPlayer);
	void EventPlayerChangeTeam(CGamePlayer *player, uint8_t team);
	void EventPlayerChangeColour(CGamePlayer *player, uint8_t colour);
	void EventPlayerChangeRace(CGamePlayer *player, uint8_t race);
	void EventPlayerChangeHandicap(CGamePlayer *player, uint8_t handicap);
	void EventPlayerDropRequest(CGamePlayer *player);
	void EventPlayerMapSize(CGamePlayer *player, CIncomingMapSize *mapSize);

	// these events are called outside of any iterations

	void EventGameStarted();

	// other functions

	uint8_t GetSIDFromPID(uint8_t PID) const;
	CGamePlayer *GetPlayerFromSID(uint8_t SID);
	uint8_t GetNewPID();
	uint8_t GetNewColour();
	BYTEARRAY GetPIDs();
	uint8_t GetHostPID();
	uint8_t GetEmptySlot();
	uint8_t GetEmptySlot(uint8_t team, uint8_t PID);
	void SwapSlots(uint8_t SID1, uint8_t SID2);
	void OpenSlot(uint8_t SID, bool kick);
	void CloseSlot(uint8_t SID, bool kick);
	void ComputerSlot(uint8_t SID, uint8_t skill, bool kick);
	void ColourSlot(uint8_t SID, uint8_t colour);
	void StartCountDown();
	void StopLaggers(const std::string &reason);
	void CreateVirtualHost();
	void DeleteVirtualHost();
};

#endif  // AURA_GAME_H_
