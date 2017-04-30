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

#include "game.h"
#include "util.h"
#include "config.h"
#include "socket.h"
#include "map.h"
#include "gameplayer.h"
#include "gameprotocol.h"

#include <ctime>
#include <cmath>

uint32_t GetTicks();
void Print(const std::string &message);

//
// CGame
//

CGame::CGame(const CMap* Map, const CGameConfig* Config, CUDPSocket* UDPSocket, uint32_t HostCounter)
	: m_UDPSocket(UDPSocket),
	m_Socket(new CTCPServer()),
	m_Protocol(new CGameProtocol()),
	m_Slots(Map->GetSlots()),
	m_Map(Map),
	m_Config(Config),
	m_RandomSeed(GetTicks()),
	m_HostCounter(HostCounter),
	m_EntryKey(rand()),
	m_SyncLimit(50),
	m_SyncCounter(0),
	m_PingTimer(),
	m_DownloadTimer(),
	m_SyncSlotInfoTimer(),
	m_CountDownTimer(),
	m_CountDownCounter(0),
	m_LagScreenResetTimer(),
	m_ActionSentTimer(),
	m_StartedLaggingTicks(0),
	m_LastLagScreenTicks(0),
	m_HostPort(0),
	m_VirtualHostPID(255),
	m_Exiting(false),
	m_SlotInfoChanged(false),
	m_Lagging(false),
	m_Desynced(false),
	m_State(State::Waiting)
{
	if (m_Socket->Listen(std::string(), m_HostPort))
		Print("[GAME: " + GetGameName() + "] listening on port " + std::to_string(m_HostPort));
	else
	{
		Print("[GAME: " + GetGameName() + "] error listening on port " + std::to_string(m_HostPort));
		m_Exiting = true;
	}
}

CGame::~CGame()
{
	delete m_Socket;
	delete m_Protocol;

	for (auto & potential : m_Potentials)
		delete potential;

	for (auto & player : m_Players)
		delete player;

	for (auto& act : m_Actions)
		delete act;
}

uint32_t CGame::GetNumPlayers() const
{
	uint32_t NumPlayers = 0;

	for (const auto & player : m_Players)
	{
		if (!player->GetLeftMessageSent())
			++NumPlayers;
	}

	return NumPlayers;
}

uint32_t CGame::SetFD(void *fd, void *send_fd, int32_t *nfds)
{
	uint32_t NumFDs = 0;

	if (m_Socket)
	{
		m_Socket->SetFD((fd_set *)fd, (fd_set *)send_fd, nfds);
		++NumFDs;
	}

	for (auto & player : m_Players)
	{
		player->GetSocket()->SetFD((fd_set *)fd, (fd_set *)send_fd, nfds);
		++NumFDs;
	}

	for (auto & potential : m_Potentials)
	{
		if (potential->GetSocket())
		{
			potential->GetSocket()->SetFD((fd_set *)fd, (fd_set *)send_fd, nfds);
			++NumFDs;
		}
	}

	return NumFDs;
}

bool CGame::Update(void *fd, void *send_fd)
{
	const uint32_t Ticks = GetTicks();

	// ping every 5 seconds
	// changed this to ping during game loading as well to hopefully fix some problems with people disconnecting during loading
	// changed this to ping during the game as well
	if (m_PingTimer.update(Ticks, 5000))
	{
		// note: we must send pings to players who are downloading the map because Warcraft III disconnects from the lobby if it doesn't receive a ping every ~90 seconds
		// so if the player takes longer than 90 seconds to download the map they would be disconnected unless we keep sending pings

		SendAll(m_Protocol->SEND_W3GS_PING_FROM_HOST());

		// we also broadcast the game to the local network every 5 seconds so we hijack this timer for our nefarious purposes
		// however we only want to broadcast if the countdown hasn't started
		// see the !sendlan code later in this file for some more information about how this works

		if (m_State == State::Waiting)
		{
			// construct a fixed host counter which will be used to identify players from this "realm" (i.e. LAN)
			// the fixed host counter's 4 most significant bits will contain a 4 bit ID (0-15)
			// the rest of the fixed host counter will contain the 28 least significant bits of the actual host counter
			// since we're destroying 4 bits of information here the actual host counter should not be greater than 2^28 which is a reasonable assumption
			// when a player joins a game we can obtain the ID from the received host counter
			// note: LAN broadcasts use an ID of 0, battle.net refreshes use an ID of 1-10, the rest are unused

			// we send 12 for SlotsTotal because this determines how many PID's Warcraft 3 allocates
			// we need to make sure Warcraft 3 allocates at least SlotsTotal + 1 but at most 12 PID's
			// this is because we need an extra PID for the virtual host player (but we always delete the virtual host player when the 12th person joins)
			// however, we can't send 13 for SlotsTotal because this causes Warcraft 3 to crash when sharing control of units
			// nor can we send SlotsTotal because then Warcraft 3 crashes when playing maps with less than 12 PID's (because of the virtual host player taking an extra PID)
			// we also send 12 for SlotsOpen because Warcraft 3 assumes there's always at least one player in the game (the host)
			// so if we try to send accurate numbers it'll always be off by one and results in Warcraft 3 assuming the game is full when it still needs one more player
			// the easiest solution is to simply send 12 for both so the game will always show up as (1/12) players

			// note: the PrivateGame flag is not set when broadcasting to LAN (as you might expect)
			// note: we do not use m_Map->GetMapGameType because none of the filters are set when broadcasting to LAN (also as you might expect)

			m_UDPSocket->Broadcast(6112, m_Protocol->SEND_W3GS_GAMEINFO(m_Config->War3Version, CreateByteArray((uint32_t)MAPGAMETYPE_UNKNOWN0, false), m_Map->GetMapGameFlags(), m_Map->GetMapWidth(), m_Map->GetMapHeight(), GetGameName(), "Clan 007", 0, m_Map->GetMapPath(), m_Map->GetMapCRC(), 12, 12, m_HostPort, m_HostCounter & 0x0FFFFFFF, m_EntryKey));
		}
	}

	// update players
	for (auto i = begin(m_Players); i != end(m_Players);)
	{
		if ((*i)->Update(fd))
		{
			EventPlayerDeleted(*i);
			delete *i;
			i = m_Players.erase(i);
		}
		else
			++i;
	}

	for (auto i = begin(m_Potentials); i != end(m_Potentials);)
	{
		if ((*i)->Update(fd))
		{
			// flush the socket (e.g. in case a rejection message is queued)
			if ((*i)->GetSocket())
				(*i)->GetSocket()->DoSend((fd_set *)send_fd);
			delete *i;
			i = m_Potentials.erase(i);
		}
		else
			++i;
	}

	// keep track of the largest sync counter (the number of keepalive packets received by each player)
	// if anyone falls behind by more than m_SyncLimit keepalives we start the lag screen
	if (m_State == State::Loaded)
	{
		// check if anyone has started lagging
		// we consider a player to have started lagging if they're more than m_SyncLimit keepalives behind
		if (!m_Lagging)
		{
			std::string LaggingString;

			for (auto & player : m_Players)
			{
				if (m_SyncCounter - player->GetSyncCounter() > m_SyncLimit)
				{
					player->SetLagging(true);
					player->SetStartedLaggingTicks(Ticks);
					m_Lagging = true;
					m_StartedLaggingTicks = Ticks;

					if (LaggingString.empty())
						LaggingString = player->GetName();
					else
						LaggingString += ", " + player->GetName();
				}
			}

			if (m_Lagging)
			{
				// start the lag screen
				Print("[GAME: " + GetGameName() + "] started lagging on [" + LaggingString + "]");
				SendAll(m_Protocol->SEND_W3GS_START_LAG(m_Players));

				// reset everyone's drop vote
				for (auto & player : m_Players)
					player->SetDropVote(false);
				m_LagScreenResetTimer.reset(Ticks);
			}
		}

		if (m_Lagging)
		{
			if (Ticks - m_StartedLaggingTicks >= 600000)
				StopLaggers("was automatically dropped after 60 seconds");

			// we cannot allow the lag screen to stay up for more than ~65 seconds because Warcraft III disconnects if it doesn't receive an action packet at least this often
			// one (easy) solution is to simply drop all the laggers if they lag for more than 60 seconds
			// another solution is to reset the lag screen the same way we reset it when using load-in-game
			if (m_LagScreenResetTimer.update(Ticks, 60000))
			{
				for (auto & _i : m_Players)
				{
					// stop the lag screen
					for (auto & player : m_Players)
					{
						if (player->GetLagging())
							Send(_i, m_Protocol->SEND_W3GS_STOP_LAG(player));
					}

					Send(_i, m_Protocol->SEND_W3GS_INCOMING_ACTION(std::vector<CIncomingAction *>(), 0));

					// start the lag screen
					Send(_i, m_Protocol->SEND_W3GS_START_LAG(m_Players));
				}

				// Warcraft III doesn't seem to respond to empty actions
			}

			// check if anyone has stopped lagging normally
			// we consider a player to have stopped lagging if they're less than half m_SyncLimit keepalives behind

			for (auto & player : m_Players)
			{
				if (player->GetLagging() && m_SyncCounter - player->GetSyncCounter() < m_SyncLimit / 2)
				{
					// stop the lag screen for this player

					Print("[GAME: " + GetGameName() + "] stopped lagging on [" + player->GetName() + "]");
					SendAll(m_Protocol->SEND_W3GS_STOP_LAG(player));
					player->SetLagging(false);
					player->SetStartedLaggingTicks(0);
				}
			}

			// check if everyone has stopped lagging

			bool Lagging = false;

			for (auto & player : m_Players)
			{
				if (player->GetLagging())
				{
					Lagging = true;
					break;
				}
			}

			m_Lagging = Lagging;

			// reset m_ActionSentTimer because we want the game to stop running while the lag screen is up
			m_ActionSentTimer.reset(Ticks);

			// keep track of the last lag screen time so we can avoid timing out players
			m_LastLagScreenTicks = Ticks;
		}
	}

	// send actions every GetLatency() milliseconds
	// actions are at the heart of every Warcraft 3 game but luckily we don't need to know their contents to relay them
	// we queue player actions in EventPlayerAction then just resend them in batches to all players here
	if (m_State == State::Loaded && !m_Lagging && m_ActionSentTimer.update(Ticks, GetLatency())) {
		SendAllActions();
	}

	// end the game if there aren't any players left
	if (m_Players.empty() && (m_State == State::Loading || m_State == State::Loaded))
	{
		Print("[GAME: " + GetGameName() + "] is over (no players left)");
		return true;
	}

	// check if the game is loaded
	if (m_State == State::Loading)
	{
		bool FinishedLoading = true;

		for (auto & player : m_Players)
		{
			FinishedLoading = player->GetFinishedLoading();
			if (!FinishedLoading)
				break;
		}

		if (FinishedLoading)
		{
			m_ActionSentTimer.reset(Ticks);
			m_State = State::Loaded;
		}
	}

	if (m_State == State::Loaded || m_State == State::Loading)
		return m_Exiting;

	if (m_SyncSlotInfoTimer.update(Ticks, 1000))
	{
		if (m_SlotInfoChanged)
			SendAllSlotInfo();
	}

	if (m_DownloadTimer.update(Ticks, 100))
	{
		for (auto & player : m_Players)
		{
			if (player->GetDownloadStarted() && !player->GetDownloadFinished())
			{
				// send up to 100 pieces of the map at once so that the download goes faster
				// if we wait for each MAPPART packet to be acknowledged by the client it'll take a long time to download
				// this is because we would have to wait the round trip time (the ping time) between sending every 1442 bytes of map data
				// doing it this way allows us to send at least 140 KB in each round trip int32_terval which is much more reasonable
				// the theoretical throughput is [140 KB * 1000 / ping] in KB/sec so someone with 100 ping (round trip ping, not LC ping) could download at 1400 KB/sec
				// note: this creates a queue of map data which clogs up the connection when the client is on a slower connection (e.g. dialup)
				// in this case any changes to the lobby are delayed by the amount of time it takes to send the queued data (i.e. 140 KB, which could be 30 seconds or more)
				// for example, players joining and leaving, slot changes, chat messages would all appear to happen much later for the low bandwidth player
				// note: the throughput is also limited by the number of times this code is executed each second
				// e.g. if we send the maximum amount (140 KB) 10 times per second the theoretical throughput is 1400 KB/sec
				// therefore the maximum throughput is 1400 KB/sec regardless of ping and this value slowly diminishes as the player's ping increases
				// in addition to this, the throughput is limited by the configuration value bot_maxdownloadspeed
				// in summary: the actual throughput is MIN( 140 * 1000 / ping, 1400, bot_maxdownloadspeed ) in KB/sec assuming only one player is downloading the map

				const uint32_t MapSize = ByteArrayToUInt32(m_Map->GetMapSize(), false);

				while (player->GetLastMapPartSent() < player->GetLastMapPartAcked() + 1442 * 100 && player->GetLastMapPartSent() < MapSize)
				{
					Send(player, m_Protocol->SEND_W3GS_MAPPART(GetHostPID(), player->GetPID(), player->GetLastMapPartSent(), m_Map->GetMapData()));
					player->SetLastMapPartSent(player->GetLastMapPartSent() + 1442);
				}
			}
		}
	}

	if (m_State == State::CountDown && m_CountDownTimer.update(Ticks, 500))
	{
		if (m_CountDownCounter > 0)
		{
			// we use a countdown counter rather than a "finish countdown time" here because it might alternately round up or down the count
			// this sometimes resulted in a countdown of e.g. "6 5 3 2 1" during my testing which looks pretty dumb
			// doing it this way ensures it's always "5 4 3 2 1" but each int32_terval might not be *exactly* the same length
			SendAllChat(std::to_string(m_CountDownCounter--) + ". . .");
		}
		else
		{
			EventGameStarted(Ticks);
			return m_Exiting;
		}
	}

	// create the virtual host player
	if (GetNumPlayers() < 12)
		CreateVirtualHost();

	// accept new connections
	if (m_Socket)
	{
		CTCPSocket *NewSocket = m_Socket->Accept((fd_set *)fd);

		if (NewSocket)
			m_Potentials.push_back(new CPotentialPlayer(m_Protocol, this, NewSocket));

		if (m_Socket->HasError())
			return true;
	}

	return m_Exiting;
}

void CGame::UpdatePost(void *send_fd)
{
	// we need to manually call DoSend on each player now because CGamePlayer :: Update doesn't do it
	// this is in case player 2 generates a packet for player 1 during the update but it doesn't get sent because player 1 already finished updating
	// in reality since we're queueing actions it might not make a big difference but oh well

	for (auto & player : m_Players)
		player->GetSocket()->DoSend((fd_set *)send_fd);

	for (auto & potential : m_Potentials)
	{
		if (potential->GetSocket())
			potential->GetSocket()->DoSend((fd_set *)send_fd);
	}
}

void CGame::Send(CGamePlayer *player, const BYTEARRAY &data)
{
	if (player)
		player->Send(data);
}

void CGame::SendAll(const BYTEARRAY &data)
{
	for (auto & player : m_Players)
		player->Send(data);
}

void CGame::SendAllChat(const std::string &message)
{
	uint8_t fromPID = GetHostPID();
	// send a public message to all players - it'll be marked [All] in Warcraft 3

	if (GetNumPlayers() > 0)
	{
		Print("[GAME: " + GetGameName() + "] [Local] " + message);

		if (m_State == State::Waiting || m_State == State::CountDown)
		{
			if (message.size() > 254)
				SendAll(m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, GetPIDs(), 16, BYTEARRAY(), message.substr(0, 254)));
			else
				SendAll(m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, GetPIDs(), 16, BYTEARRAY(), message));
		}
		else
		{
			if (message.size() > 127)
				SendAll(m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, GetPIDs(), 32, CreateByteArray((uint32_t)0, false), message.substr(0, 127)));
			else
				SendAll(m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, GetPIDs(), 32, CreateByteArray((uint32_t)0, false), message));
		}
	}
}

void CGame::SendAllSlotInfo()
{
	if (m_State == State::Waiting || m_State == State::CountDown)
	{
		SendAll(m_Protocol->SEND_W3GS_SLOTINFO(m_Slots, m_RandomSeed, m_Map->GetMapLayoutStyle(), m_Map->GetMapNumPlayers()));
		m_SlotInfoChanged = false;
	}
}

void CGame::SendVirtualHostPlayerInfo(CGamePlayer *player)
{
	if (m_VirtualHostPID == 255)
		return;

	const BYTEARRAY IP = { 0, 0, 0, 0 };

	Send(player, m_Protocol->SEND_W3GS_PLAYERINFO(m_VirtualHostPID, GetVirtualHostName(), IP, IP));
}

void CGame::SendAllActions()
{
	++m_SyncCounter;

	// we aren't allowed to send more than 1460 bytes in a single packet but it's possible we might have more than that many bytes waiting in the queue

	uint32_t SubActionsLength = 0;
	std::vector<CIncomingAction *> SubActions;
	for (auto& act : m_Actions)
	{
		if (SubActionsLength + act->GetLength() > 1452)
		{
			SendAll(m_Protocol->SEND_W3GS_INCOMING_ACTION2(SubActions));
			SubActions.clear();
			SubActionsLength = 0;
		}
		SubActions.push_back(act);
		SubActionsLength += act->GetLength();
	}

	SendAll(m_Protocol->SEND_W3GS_INCOMING_ACTION(SubActions, GetLatency()));

	for (auto& act : m_Actions)
	{
		delete act;
	}
	m_Actions.clear();
}

void CGame::EventPlayerDeleted(CGamePlayer *player)
{
	Print("[GAME: " + GetGameName() + "] deleting player [" + player->GetName() + "]: " + player->GetLeftReason());

	// in some cases we're forced to send the left message early so don't send it again

	if (player->GetLeftMessageSent())
		return;

	if (m_State == State::Loaded)
		SendAllChat(player->GetName() + " " + player->GetLeftReason() + ".");

	if (player->GetLagging())
		SendAll(m_Protocol->SEND_W3GS_STOP_LAG(player));

	// tell everyone about the player leaving

	SendAll(m_Protocol->SEND_W3GS_PLAYERLEAVE_OTHERS(player->GetPID(), player->GetLeftCode()));

	// abort the countdown if there was one in progress

	if (m_State == State::CountDown)
	{
		SendAllChat("Countdown aborted!");
		m_State = State::Waiting;
	}
}

void CGame::EventPlayerDisconnectTimedOut(CGamePlayer *player)
{
	// not only do we not do any timeouts if the game is lagging, we allow for an additional grace period of 10 seconds
	// this is because Warcraft 3 stops sending packets during the lag screen
	// so when the lag screen finishes we would immediately disconnect everyone if we didn't give them some extra time

	if (GetTicks() - m_LastLagScreenTicks >= 10000)
	{
		DeletePlayer(player, PLAYERLEAVE_DISCONNECT, "has lost the connection (timed out)");
	}
}

void CGame::EventPlayerDisconnectSocketError(CGamePlayer *player)
{
	DeletePlayer(player, PLAYERLEAVE_DISCONNECT, "has lost the connection (connection error - " + player->GetSocket()->GetErrorString() + ")");
}

void CGame::EventPlayerDisconnectConnectionClosed(CGamePlayer *player)
{
	DeletePlayer(player, PLAYERLEAVE_DISCONNECT, "has lost the connection (connection closed by remote host)");
}

void CGame::EventPlayerJoined(CPotentialPlayer *potential, CIncomingJoinPlayer *joinPlayer)
{
	// check the new player's name

	if (joinPlayer->GetName().empty() || joinPlayer->GetName().size() > 15 || joinPlayer->GetName() == GetVirtualHostName() || joinPlayer->GetName().find(" ") != std::string::npos || joinPlayer->GetName().find("|") != std::string::npos)
	{
		Print("[GAME: " + GetGameName() + "] player [" + joinPlayer->GetName() + "|" + potential->GetExternalIPString() + "] invalid name (taken, invalid char, spoofer, too long)");
		potential->Send(m_Protocol->SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
		potential->SetDeleteMe(true);
		return;
	}

	const uint32_t HostCounterID = joinPlayer->GetHostCounter() >> 28;

	// we use an ID value of 0 to denote joining via LAN, we don't have to set their joined realm.

	if (HostCounterID == 0)
	{
		// check if the player joining via LAN knows the entry key

		if (joinPlayer->GetEntryKey() != m_EntryKey)
		{
			Print("[GAME: " + GetGameName() + "] player [" + joinPlayer->GetName() + "|" + potential->GetExternalIPString() + "] is trying to join the game over LAN but used an incorrect entry key");
			potential->Send(m_Protocol->SEND_W3GS_REJECTJOIN(REJECTJOIN_WRONGPASSWORD));
			potential->SetDeleteMe(true);
			return;
		}
	}

	// try to find an empty slot
	uint8_t SID = GetEmptySlot();
	if (SID >= m_Slots.size())
	{
		potential->Send(m_Protocol->SEND_W3GS_REJECTJOIN(REJECTJOIN_FULL));
		potential->SetDeleteMe(true);
		return;
	}

	// we have a slot for the new player
	// make room for them by deleting the virtual host player if we have to

	if (GetNumPlayers() >= 11)
		DeleteVirtualHost();

	// turning the CPotentialPlayer into a CGamePlayer is a bit of a pain because we have to be careful not to close the socket
	// this problem is solved by setting the socket to nullptr before deletion and handling the nullptr case in the destructor
	// we also have to be careful to not modify the m_Potentials vector since we're currently looping through it

	Print("[GAME: " + GetGameName() + "] player [" + joinPlayer->GetName() + "|" + potential->GetExternalIPString() + "] joined the game");
	CGamePlayer *Player = new CGamePlayer(potential, GetNewPID(), joinPlayer->GetName(), joinPlayer->GetInternalIP());

	m_Players.push_back(Player);
	potential->SetSocket(nullptr);
	potential->SetDeleteMe(true);

	if (m_Map->GetMapOptions() & MAPOPT_CUSTOMFORCES)
		m_Slots[SID] = CGameSlot(Player->GetPID(), 255, SLOTSTATUS_OCCUPIED, 0, m_Slots[SID].GetTeam(), m_Slots[SID].GetColour(), m_Slots[SID].GetRace());
	else
	{
		if (m_Map->GetMapFlags() & MAPFLAG_RANDOMRACES)
			m_Slots[SID] = CGameSlot(Player->GetPID(), 255, SLOTSTATUS_OCCUPIED, 0, 12, 12, SLOTRACE_RANDOM);
		else
			m_Slots[SID] = CGameSlot(Player->GetPID(), 255, SLOTSTATUS_OCCUPIED, 0, 12, 12, SLOTRACE_RANDOM | SLOTRACE_SELECTABLE);

		// try to pick a team and colour
		// make sure there aren't too many other players already

		uint8_t NumOtherPlayers = 0;

		for (auto & slot : m_Slots)
		{
			if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetTeam() != 12)
				++NumOtherPlayers;
		}

		if (NumOtherPlayers < m_Map->GetMapNumPlayers())
		{
			if (SID < m_Map->GetMapNumPlayers())
				m_Slots[SID].SetTeam(SID);
			else
				m_Slots[SID].SetTeam(0);

			m_Slots[SID].SetColour(GetNewColour());
		}
	}

	// send slot info to the new player
	// the SLOTINFOJOIN packet also tells the client their assigned PID and that the join was successful

	Player->Send(m_Protocol->SEND_W3GS_SLOTINFOJOIN(Player->GetPID(), Player->GetSocket()->GetPort(), Player->GetExternalIP(), m_Slots, m_RandomSeed, m_Map->GetMapLayoutStyle(), m_Map->GetMapNumPlayers()));

	// send virtual host info and fake player info (if present) to the new player

	SendVirtualHostPlayerInfo(Player);

	for (auto & ply : m_Players)
	{
		if (!ply->GetLeftMessageSent() && ply != Player)
		{
			// send info about the new player to every other player

			ply->Send(m_Protocol->SEND_W3GS_PLAYERINFO(Player->GetPID(), Player->GetName(), Player->GetExternalIP(), Player->GetInternalIP()));

			// send info about every other player to the new player
			Player->Send(m_Protocol->SEND_W3GS_PLAYERINFO(ply->GetPID(), ply->GetName(), ply->GetExternalIP(), ply->GetInternalIP()));
		}
	}

	// send a map check packet to the new player

	Player->Send(m_Protocol->SEND_W3GS_MAPCHECK(m_Map->GetMapPath(), m_Map->GetMapSize(), m_Map->GetMapInfo(), m_Map->GetMapCRC(), m_Map->GetMapSHA1()));

	// send slot info to everyone, so the new player gets this info twice but everyone else still needs to know the new slot layout

	SendAllSlotInfo();

	// abort the countdown if there was one in progress

	if (m_State == State::CountDown)
	{
		SendAllChat("Countdown aborted!");
		m_State = State::Waiting;
	}

	if (m_State == State::Waiting)
	{
		switch (m_Config->AutoStart)
		{
		case 1:
			StartCountDown();
			break;
		case 2:
			if (GetEmptySlot() == 255)
			{
				StartCountDown();
			}
			break;
		}
	}
}

void CGame::EventPlayerLeft(CGamePlayer *player, uint32_t reason)
{
	// this function is only called when a player leave packet is received, not when there's a socket error, kick, etc...
	DeletePlayer(player, PLAYERLEAVE_LOST, "has left the game voluntarily");
}

void CGame::EventPlayerLoaded(CGamePlayer *player)
{
	SendAll(m_Protocol->SEND_W3GS_GAMELOADED_OTHERS(player->GetPID()));
}

void CGame::EventPlayerAction(CGamePlayer *player, CIncomingAction *action)
{
	m_Actions.push_back(action);
}

void CGame::EventPlayerKeepAlive(CGamePlayer *player)
{
	// check for desyncs

	const uint32_t FirstCheckSum = player->GetCheckSums()->front();

	for (auto & player : m_Players)
	{
		if (player->GetCheckSums()->empty())
			return;

		if (!m_Desynced && player->GetCheckSums()->front() != FirstCheckSum)
		{
			m_Desynced = true;
			Print("[GAME: " + GetGameName() + "] desync detected");
			SendAllChat("Warning! Desync detected!");
			SendAllChat("Warning! Desync detected!");
			SendAllChat("Warning! Desync detected!");
		}
	}

	for (auto & player : m_Players)
		player->GetCheckSums()->pop();
}

void CGame::EventPlayerChatToHost(CGamePlayer *player, CIncomingChatPlayer *chatPlayer)
{
	if (chatPlayer->GetFromPID() == player->GetPID())
	{
		if (m_State == State::Waiting)
		{
			if (chatPlayer->GetType() == CIncomingChatPlayer::CTH_TEAMCHANGE)
				EventPlayerChangeTeam(player, chatPlayer->GetByte());
			else if (chatPlayer->GetType() == CIncomingChatPlayer::CTH_COLOURCHANGE)
				EventPlayerChangeColour(player, chatPlayer->GetByte());
			else if (chatPlayer->GetType() == CIncomingChatPlayer::CTH_RACECHANGE)
				EventPlayerChangeRace(player, chatPlayer->GetByte());
			else if (chatPlayer->GetType() == CIncomingChatPlayer::CTH_HANDICAPCHANGE)
				EventPlayerChangeHandicap(player, chatPlayer->GetByte());
		}
	}
}

void CGame::EventPlayerChangeTeam(CGamePlayer *player, uint8_t team)
{
	// player is requesting a team change

	if (m_Map->GetMapOptions() & MAPOPT_CUSTOMFORCES)
	{
		uint8_t oldSID = GetSIDFromPID(player->GetPID());
		uint8_t newSID = GetEmptySlot(team, player->GetPID());
		SwapSlots(oldSID, newSID);
	}
	else
	{
		if (team > 12)
			return;

		if (team == 12)
		{
			if (m_Map->GetMapObservers() != MAPOBS_ALLOWED && m_Map->GetMapObservers() != MAPOBS_REFEREES)
				return;
		}
		else
		{
			if (team >= m_Map->GetMapNumPlayers())
				return;

			// make sure there aren't too many other players already

			uint8_t NumOtherPlayers = 0;

			for (auto & slot : m_Slots)
			{
				if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetTeam() != 12 && slot.GetPID() != player->GetPID())
					++NumOtherPlayers;
			}

			if (NumOtherPlayers >= m_Map->GetMapNumPlayers())
				return;
		}

		uint8_t SID = GetSIDFromPID(player->GetPID());

		if (SID < m_Slots.size())
		{
			m_Slots[SID].SetTeam(team);

			if (team == 12)
			{
				// if they're joining the observer team give them the observer colour

				m_Slots[SID].SetColour(12);
			}
			else if (m_Slots[SID].GetColour() == 12)
			{
				// if they're joining a regular team give them an unused colour

				m_Slots[SID].SetColour(GetNewColour());
			}

			SendAllSlotInfo();
		}
	}
}

void CGame::EventPlayerChangeColour(CGamePlayer *player, uint8_t colour)
{
	// player is requesting a colour change

	if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS)
		return;

	if (colour > 11)
		return;

	uint8_t SID = GetSIDFromPID(player->GetPID());

	if (SID < m_Slots.size())
	{
		// make sure the player isn't an observer

		if (m_Slots[SID].GetTeam() == 12)
			return;

		ColourSlot(SID, colour);
	}
}

void CGame::EventPlayerChangeRace(CGamePlayer *player, uint8_t race)
{
	// player is requesting a race change

	if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS)
		return;

	if (m_Map->GetMapFlags() & MAPFLAG_RANDOMRACES)
		return;

	if (race != SLOTRACE_HUMAN && race != SLOTRACE_ORC && race != SLOTRACE_NIGHTELF && race != SLOTRACE_UNDEAD && race != SLOTRACE_RANDOM)
		return;

	uint8_t SID = GetSIDFromPID(player->GetPID());

	if (SID < m_Slots.size())
	{
		m_Slots[SID].SetRace(race | SLOTRACE_SELECTABLE);
		SendAllSlotInfo();
	}
}

void CGame::EventPlayerChangeHandicap(CGamePlayer *player, uint8_t handicap)
{
	// player is requesting a handicap change

	if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS)
		return;

	if (handicap != 50 && handicap != 60 && handicap != 70 && handicap != 80 && handicap != 90 && handicap != 100)
		return;

	uint8_t SID = GetSIDFromPID(player->GetPID());

	if (SID < m_Slots.size())
	{
		m_Slots[SID].SetHandicap(handicap);
		SendAllSlotInfo();
	}
}

void CGame::EventPlayerDropRequest(CGamePlayer *player)
{
	// TODO: check that we've waited the full 45 seconds

	if (m_Lagging)
	{
		Print("[GAME: " + GetGameName() + "] player [" + player->GetName() + "] voted to drop laggers");
		SendAllChat("Player [" + player->GetName() + "] voted to drop laggers");

		// check if at least half the players voted to drop

		int32_t Votes = 0;

		for (auto & player : m_Players)
		{
			if (player->GetDropVote())
				++Votes;
		}

		if ((float)Votes / m_Players.size() > 0.50f)
			StopLaggers("lagged out (dropped by vote)");
	}
}

void CGame::EventPlayerMapSize(CGamePlayer *player, CIncomingMapSize *mapSize)
{
	if (m_State != State::Waiting &&  m_State != State::CountDown)
		return;

	uint32_t MapSize = ByteArrayToUInt32(m_Map->GetMapSize(), false);

	if (mapSize->GetSizeFlag() != 1 || mapSize->GetMapSize() != MapSize)
	{
		// the player doesn't have the map

		const std::string *MapData = m_Map->GetMapData();

		if (!MapData->empty())
		{
			if (!player->GetDownloadStarted() && mapSize->GetSizeFlag() == 1)
			{
				// inform the client that we are willing to send the map

				Print("[GAME: " + GetGameName() + "] map download started for player [" + player->GetName() + "]");
				Send(player, m_Protocol->SEND_W3GS_STARTDOWNLOAD(GetHostPID()));
				player->SetDownloadStarted(true);
			}
			else
				player->SetLastMapPartAcked(mapSize->GetMapSize());
		}
		else
		{
			DeletePlayer(player, PLAYERLEAVE_LOBBY, "doesn't have the map and there is no local copy of the map to send");
		}
	}
	else if (player->GetDownloadStarted())
	{
		player->SetDownloadFinished(true);
	}

	uint8_t NewDownloadStatus = (uint8_t)((float)mapSize->GetMapSize() / MapSize * 100.f);
	const uint8_t SID = GetSIDFromPID(player->GetPID());

	if (NewDownloadStatus > 100)
		NewDownloadStatus = 100;

	if (SID < m_Slots.size())
	{
		// only send the slot info if the download status changed

		if (m_Slots[SID].GetDownloadStatus() != NewDownloadStatus)
		{
			m_Slots[SID].SetDownloadStatus(NewDownloadStatus);

			// we don't actually send the new slot info here
			// this is an optimization because it's possible for a player to download a map very quickly
			// if we send a new slot update for every percentage change in their download status it adds up to a lot of data
			// instead, we mark the slot info as "out of date" and update it only once in awhile (once per second when this comment was made)

			m_SlotInfoChanged = true;
		}
	}
}

void CGame::EventGameStarted(uint32_t Ticks)
{
	Print("[GAME: " + GetGameName() + "] started loading with " + std::to_string(GetNumPlayers()) + " players");

	// send a final slot info update if necessary
	// this typically won't happen because we prevent the !start command from completing while someone is downloading the map
	// however, if someone uses !start force while a player is downloading the map this could trigger
	// this is because we only permit slot info updates to be flagged when it's just a change in download status, all others are sent immediately
	// it might not be necessary but let's clean up the mess anyway

	if (m_SlotInfoChanged)
		SendAllSlotInfo();

	m_LagScreenResetTimer.reset(Ticks);
	m_State = State::Loading;

	// since we use a fake countdown to deal with leavers during countdown the COUNTDOWN_START and COUNTDOWN_END packets are sent in quick succession
	// send a start countdown packet

	SendAll(m_Protocol->SEND_W3GS_COUNTDOWN_START());

	// remove the virtual host player

	DeleteVirtualHost();

	// send an end countdown packet

	SendAll(m_Protocol->SEND_W3GS_COUNTDOWN_END());

	// close the listening socket

	delete m_Socket;
	m_Socket = nullptr;

	// delete any potential players that are still hanging around

	for (auto & potential : m_Potentials)
		delete potential;

	m_Potentials.clear();
}

uint8_t CGame::GetSIDFromPID(uint8_t PID) const
{
	if (m_Slots.size() > 255)
		return 255;

	for (uint8_t i = 0; i < m_Slots.size(); ++i)
	{
		if (m_Slots[i].GetPID() == PID)
			return i;
	}

	return 255;
}

CGamePlayer *CGame::GetPlayerFromSID(uint8_t SID)
{
	if (SID >= m_Slots.size())
		return nullptr;

	const uint8_t PID = m_Slots[SID].GetPID();

	for (auto & player : m_Players)
	{
		if (!player->GetLeftMessageSent() && player->GetPID() == PID)
			return player;
	}

	return nullptr;
}

uint8_t CGame::GetNewPID()
{
	// find an unused PID for a new player to use

	for (uint8_t TestPID = 1; TestPID < 255; ++TestPID)
	{
		if (TestPID == m_VirtualHostPID)
			continue;

		bool InUse = false;
		for (auto & player : m_Players)
		{
			if (!player->GetLeftMessageSent() && player->GetPID() == TestPID)
			{
				InUse = true;
				break;
			}
		}

		if (!InUse)
			return TestPID;
	}

	// this should never happen

	return 255;
}

uint8_t CGame::GetNewColour()
{
	// find an unused colour for a player to use

	for (uint8_t TestColour = 0; TestColour < 12; ++TestColour)
	{
		bool InUse = false;

		for (auto & slot : m_Slots)
		{
			if (slot.GetColour() == TestColour)
			{
				InUse = true;
				break;
			}
		}

		if (!InUse)
			return TestColour;
	}

	// this should never happen

	return 12;
}

BYTEARRAY CGame::GetPIDs()
{
	BYTEARRAY result;

	for (auto & player : m_Players)
	{
		if (!player->GetLeftMessageSent())
			result.push_back(player->GetPID());
	}

	return result;
}

uint8_t CGame::GetHostPID()
{
	// return the player to be considered the host (it can be any player) - mainly used for sending text messages from the bot
	// try to find the virtual host player first

	if (m_VirtualHostPID != 255)
		return m_VirtualHostPID;

	// okay then, just use the first available player

	for (auto & player : m_Players)
	{
		if (!player->GetLeftMessageSent())
			return player->GetPID();
	}

	return 255;
}

uint8_t CGame::GetEmptySlot()
{
	if (m_Slots.size() > 255)
		return 255;
	for (uint8_t i = 0; i < m_Slots.size(); ++i)
	{
		if (m_Slots[i].GetSlotStatus() == SLOTSTATUS_OPEN)
			return i;
	}
	return 255;
}

uint8_t CGame::GetEmptySlot(uint8_t team, uint8_t PID)
{
	if (m_Slots.size() > 255)
		return 255;

	// find an empty slot based on player's current slot

	uint8_t StartSlot = GetSIDFromPID(PID);

	if (StartSlot < m_Slots.size())
	{
		if (m_Slots[StartSlot].GetTeam() != team)
		{
			// player is trying to move to another team so start looking from the first slot on that team
			// we actually just start looking from the very first slot since the next few loops will check the team for us

			StartSlot = 0;
		}

		// find an empty slot on the correct team starting from StartSlot

		for (uint8_t i = StartSlot; i < m_Slots.size(); ++i)
		{
			if (m_Slots[i].GetSlotStatus() == SLOTSTATUS_OPEN && m_Slots[i].GetTeam() == team)
				return i;
		}

		// didn't find an empty slot, but we could have missed one with SID < StartSlot
		// e.g. in the DotA case where I am in slot 4 (yellow), slot 5 (orange) is occupied, and slot 1 (blue) is open and I am trying to move to another slot

		for (uint8_t i = 0; i < StartSlot; ++i)
		{
			if (m_Slots[i].GetSlotStatus() == SLOTSTATUS_OPEN && m_Slots[i].GetTeam() == team)
				return i;
		}
	}

	return 255;
}

void CGame::SwapSlots(uint8_t SID1, uint8_t SID2)
{
	if (SID1 < m_Slots.size() && SID2 < m_Slots.size() && SID1 != SID2)
	{
		CGameSlot Slot1 = m_Slots[SID1];
		CGameSlot Slot2 = m_Slots[SID2];

		if (m_Map->GetMapOptions() & MAPOPT_FIXEDPLAYERSETTINGS)
		{
			// don't swap the team, colour, race, or handicap
			m_Slots[SID1] = CGameSlot(Slot2.GetPID(), Slot2.GetDownloadStatus(), Slot2.GetSlotStatus(), Slot2.GetComputer(), Slot1.GetTeam(), Slot1.GetColour(), Slot1.GetRace(), Slot2.GetComputerType(), Slot1.GetHandicap());
			m_Slots[SID2] = CGameSlot(Slot1.GetPID(), Slot1.GetDownloadStatus(), Slot1.GetSlotStatus(), Slot1.GetComputer(), Slot2.GetTeam(), Slot2.GetColour(), Slot2.GetRace(), Slot1.GetComputerType(), Slot2.GetHandicap());
		}
		else
		{
			// swap everything

			if (m_Map->GetMapOptions() & MAPOPT_CUSTOMFORCES)
			{
				// except if custom forces is set, then we don't swap teams...
				Slot1.SetTeam(m_Slots[SID2].GetTeam());
				Slot2.SetTeam(m_Slots[SID1].GetTeam());
			}

			m_Slots[SID1] = Slot2;
			m_Slots[SID2] = Slot1;
		}

		SendAllSlotInfo();
	}
}

void CGame::DeletePlayer(CGamePlayer* player, uint32_t nLeftCode, const std::string &nLeftReason)
{
	player->SetDeleteMe(true);
	player->SetLeftReason(nLeftReason);
	player->SetLeftCode(nLeftCode);

	if (m_State == State::CountDown)
	{
		SendAllChat("Countdown aborted!");
		m_State = State::Waiting;
	}

	if (m_State == State::Waiting)
	{
		uint8_t SID = GetSIDFromPID(player->GetPID());
		if (SID < m_Slots.size())
		{
			CGameSlot Slot = m_Slots[SID];
			m_Slots[SID] = CGameSlot(0, 255, SLOTSTATUS_OPEN, 0, Slot.GetTeam(), Slot.GetColour(), Slot.GetRace());
			SendAllSlotInfo();
		}
	}
}

void CGame::ColourSlot(uint8_t SID, uint8_t colour)
{
	if (SID < m_Slots.size() && colour < 12)
	{
		// make sure the requested colour isn't already taken

		bool Taken = false;
		uint8_t TakenSID = 0;

		for (uint8_t i = 0; i < m_Slots.size(); ++i)
		{
			if (m_Slots[i].GetColour() == colour)
			{
				TakenSID = i;
				Taken = true;
			}
		}

		if (Taken && m_Slots[TakenSID].GetSlotStatus() != SLOTSTATUS_OCCUPIED)
		{
			// the requested colour is currently "taken" by an unused (open or closed) slot
			// but we allow the colour to persist within a slot so if we only update the existing player's colour the unused slot will have the same colour
			// this isn't really a problem except that if someone then joins the game they'll receive the unused slot's colour resulting in a duplicate
			// one way to solve this (which we do here) is to swap the player's current colour into the unused slot

			m_Slots[TakenSID].SetColour(m_Slots[SID].GetColour());
			m_Slots[SID].SetColour(colour);
			SendAllSlotInfo();
		}
		else if (!Taken)
		{
			// the requested colour isn't used by ANY slot

			m_Slots[SID].SetColour(colour);
			SendAllSlotInfo();
		}
	}
}

void CGame::StartCountDown()
{
	if (m_State == State::Waiting)
	{
		m_State = State::CountDown;
		m_CountDownCounter = 5;
	}
}

void CGame::StopLaggers(const std::string &reason)
{
	for (auto & player : m_Players)
	{
		if (player->GetLagging())
		{
			DeletePlayer(player, PLAYERLEAVE_DISCONNECT, reason);
		}
	}
}

void CGame::CreateVirtualHost()
{
	if (m_VirtualHostPID != 255)
		return;

	m_VirtualHostPID = GetNewPID();

	const BYTEARRAY IP = { 0, 0, 0, 0 };

	SendAll(m_Protocol->SEND_W3GS_PLAYERINFO(m_VirtualHostPID, GetVirtualHostName(), IP, IP));
}

void CGame::DeleteVirtualHost()
{
	if (m_VirtualHostPID == 255)
		return;

	SendAll(m_Protocol->SEND_W3GS_PLAYERLEAVE_OTHERS(m_VirtualHostPID, PLAYERLEAVE_LOBBY));
	m_VirtualHostPID = 255;
}
