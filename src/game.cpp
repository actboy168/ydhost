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
#include "aura.h"
#include "util.h"
#include "config.h"
#include "socket.h"
#include "map.h"
#include "gameplayer.h"
#include "gameprotocol.h"

#include <ctime>
#include <cmath>

using namespace std;

//
// CGame
//

CGame::CGame(CAura *nAura, CMap *nMap, uint16_t nHostPort, uint8_t nGameState, string &nGameName, string &nOwnerName, string &nCreatorName, string &nCreatorServer)
  : m_Aura(nAura),
    m_Socket(new CTCPServer()),
    m_Protocol(new CGameProtocol(nAura)),
    m_Slots(nMap->GetSlots()),
    m_Map(new CMap(*nMap)),
    m_GameName(nGameName),
    m_LastGameName(nGameName),
    m_VirtualHostName(nAura->m_VirtualHostName),
    m_OwnerName(nOwnerName),
    m_CreatorName(nCreatorName),
    m_MapPath(nMap->GetMapPath()),
    m_RandomSeed(GetTicks()),
    m_HostCounter(nAura->m_HostCounter++),
    m_EntryKey(rand()),
    m_Latency(nAura->m_Latency),
    m_SyncLimit(nAura->m_SyncLimit),
    m_SyncCounter(0), m_GameTicks(0),
    m_CreationTime(GetTime()),
    m_LastPingTime(GetTime()),
    m_LastRefreshTime(GetTime()),
    m_LastDownloadTicks(GetTime()),
    m_DownloadCounter(0),
    m_LastDownloadCounterResetTicks(GetTicks()),
    m_LastCountDownTicks(0),
    m_CountDownCounter(0),
    m_StartedLoadingTicks(0),
    m_StartPlayers(0),
    m_LastLagScreenResetTime(0),
    m_LastActionSentTicks(0),
    m_LastActionLateBy(0),
    m_StartedLaggingTime(0),
    m_LastLagScreenTime(0),
    m_LastReservedSeen(GetTime()),
    m_GameOverTime(0),
    m_LastPlayerLeaveTicks(0),
    m_HostPort(nHostPort),
    m_GameState(nGameState),
    m_VirtualHostPID(255),
    m_Exiting(false),
    m_Saving(false),
    m_SlotInfoChanged(false),
    m_RefreshError(false),
    m_CountDownStarted(false),
    m_GameLoading(false),
    m_GameLoaded(false),
    m_Lagging(false),
    m_Desynced(false)
{
  // start listening for connections

  if (!m_Aura->m_BindAddress.empty())
    Print("[GAME: " + m_GameName + "] attempting to bind to address [" + m_Aura->m_BindAddress + "]");

  if (m_Socket->Listen(m_Aura->m_BindAddress, m_HostPort))
    Print("[GAME: " + m_GameName + "] listening on port " + to_string(m_HostPort));
  else
  {
    Print("[GAME: " + m_GameName + "] error listening on port " + to_string(m_HostPort));
    m_Exiting = true;
  }
}

CGame::~CGame()
{
  delete m_Socket;
  delete m_Protocol;
  delete m_Map;

  for (auto & potential : m_Potentials)
    delete potential;

  for (auto & player : m_Players)
    delete player;

  while (!m_Actions.empty())
  {
    delete m_Actions.front();
    m_Actions.pop();
  }
}

uint32_t CGame::GetNextTimedActionTicks() const
{
  // return the number of ticks (ms) until the next "timed action", which for our purposes is the next game update
  // the main Aura++ loop will make sure the next loop update happens at or before this value
  // note: there's no reason this function couldn't take into account the game's other timers too but they're far less critical
  // warning: this function must take into account when actions are not being sent (e.g. during loading or lagging)

  if (!m_GameLoaded || m_Lagging)
    return 50;

  const uint32_t TicksSinceLastUpdate = GetTicks() - m_LastActionSentTicks;

  if (TicksSinceLastUpdate > m_Latency - m_LastActionLateBy)
    return 0;
  else
    return m_Latency - m_LastActionLateBy - TicksSinceLastUpdate;
}

uint32_t CGame::GetSlotsOccupied() const
{
  uint32_t NumSlotsOccupied = 0;

  for (const auto & slot : m_Slots)
  {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED)
      ++NumSlotsOccupied;
  }

  return NumSlotsOccupied;
}

uint32_t CGame::GetSlotsOpen() const
{
  uint32_t NumSlotsOpen = 0;

  for (const auto & slot : m_Slots)
  {
    if (slot.GetSlotStatus() == SLOTSTATUS_OPEN)
      ++NumSlotsOpen;
  }

  return NumSlotsOpen;
}

uint32_t CGame::GetNumPlayers() const
{
  return GetNumHumanPlayers() + m_FakePlayers.size();
}

uint32_t CGame::GetNumHumanPlayers() const
{
  uint32_t NumHumanPlayers = 0;

  for (const auto & player : m_Players)
  {
    if (!player->GetLeftMessageSent())
      ++NumHumanPlayers;
  }

  return NumHumanPlayers;
}

string CGame::GetDescription() const
{
  string Description = m_GameName + " : " + m_OwnerName + " : " + to_string(GetNumHumanPlayers()) + "/" + to_string(m_GameLoading || m_GameLoaded ? m_StartPlayers : m_Slots.size());

  if (m_GameLoading || m_GameLoaded)
    Description += " : " + to_string((m_GameTicks / 1000) / 60) + "m";
  else
    Description += " : " + to_string((GetTime() - m_CreationTime) / 60) + "m";

  return Description;
}

string CGame::GetPlayers() const
{
  string Players;

  for (const auto & player : m_Players)
  {
    const uint8_t SID = GetSIDFromPID(player->GetPID());

    if (player->GetLeftMessageSent() == false && m_Slots[SID].GetTeam() != 12)
      Players += player->GetName() + ", ";
  }

  const size_t size = Players.size();

  if (size > 2)
    Players = Players.substr(0, size - 2);

  return Players;
}

string CGame::GetObservers() const
{
  string Observers;

  for (const auto & player : m_Players)
  {
    const uint8_t SID = GetSIDFromPID(player->GetPID());

    if (player->GetLeftMessageSent() == false && m_Slots[SID].GetTeam() == 12)
      Observers += player->GetName() + ", ";
  }

  const size_t size = Observers.size();

  if (size > 2)
    Observers = Observers.substr(0, size - 2);

  return Observers;
}

uint32_t CGame::SetFD(void *fd, void *send_fd, int32_t *nfds)
{
  uint32_t NumFDs = 0;

  if (m_Socket)
  {
    m_Socket->SetFD((fd_set *) fd, (fd_set *) send_fd, nfds);
    ++NumFDs;
  }

  for (auto & player : m_Players)
  {
    player->GetSocket()->SetFD((fd_set *) fd, (fd_set *) send_fd, nfds);
    ++NumFDs;
  }

  for (auto & potential : m_Potentials)
  {
    if (potential->GetSocket())
    {
      potential->GetSocket()->SetFD((fd_set *) fd, (fd_set *) send_fd, nfds);
      ++NumFDs;
    }
  }

  return NumFDs;
}

bool CGame::Update(void *fd, void *send_fd)
{
  const uint32_t Time = GetTime(), Ticks = GetTicks();

  // ping every 5 seconds
  // changed this to ping during game loading as well to hopefully fix some problems with people disconnecting during loading
  // changed this to ping during the game as well

  if (Time - m_LastPingTime >= 5)
  {
    // note: we must send pings to players who are downloading the map because Warcraft III disconnects from the lobby if it doesn't receive a ping every ~90 seconds
    // so if the player takes longer than 90 seconds to download the map they would be disconnected unless we keep sending pings

    SendAll(m_Protocol->SEND_W3GS_PING_FROM_HOST());

    // we also broadcast the game to the local network every 5 seconds so we hijack this timer for our nefarious purposes
    // however we only want to broadcast if the countdown hasn't started
    // see the !sendlan code later in this file for some more information about how this works

    if (!m_CountDownStarted)
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

      m_Aura->m_UDPSocket->Broadcast(6112, m_Protocol->SEND_W3GS_GAMEINFO(m_Aura->m_LANWar3Version, CreateByteArray((uint32_t) MAPGAMETYPE_UNKNOWN0, false), m_Map->GetMapGameFlags(), m_Map->GetMapWidth(), m_Map->GetMapHeight(), m_GameName, "Clan 007", 0, m_Map->GetMapPath(), m_Map->GetMapCRC(), 12, 12, m_HostPort, m_HostCounter & 0x0FFFFFFF, m_EntryKey));
    }

    m_LastPingTime = Time;
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
        (*i)->GetSocket()->DoSend((fd_set *) send_fd);

      delete *i;
      i = m_Potentials.erase(i);
    }
    else
      ++i;
  }

  // keep track of the largest sync counter (the number of keepalive packets received by each player)
  // if anyone falls behind by more than m_SyncLimit keepalives we start the lag screen

  if (m_GameLoaded)
  {
    // check if anyone has started lagging
    // we consider a player to have started lagging if they're more than m_SyncLimit keepalives behind

    if (!m_Lagging)
    {
      string LaggingString;

      for (auto & player : m_Players)
      {
        if (m_SyncCounter - player->GetSyncCounter() > m_SyncLimit)
        {
          player->SetLagging(true);
          player->SetStartedLaggingTicks(Ticks);
          m_Lagging = true;
          m_StartedLaggingTime = Time;

          if (LaggingString.empty())
            LaggingString = player->GetName();
          else
            LaggingString += ", " + player->GetName();
        }
      }

      if (m_Lagging)
      {
        // start the lag screen

        Print("[GAME: " + m_GameName + "] started lagging on [" + LaggingString + "]");
        SendAll(m_Protocol->SEND_W3GS_START_LAG(m_Players));

        // reset everyone's drop vote

        for (auto & player : m_Players)
          player->SetDropVote(false);

        m_LastLagScreenResetTime = Time;
      }
    }

    if (m_Lagging)
    {
      uint32_t WaitTime = 60;

      if (Time - m_StartedLaggingTime >= WaitTime)
        StopLaggers("was automatically dropped after " + to_string(WaitTime) + " seconds");

      // we cannot allow the lag screen to stay up for more than ~65 seconds because Warcraft III disconnects if it doesn't receive an action packet at least this often
      // one (easy) solution is to simply drop all the laggers if they lag for more than 60 seconds
      // another solution is to reset the lag screen the same way we reset it when using load-in-game

      if (Time - m_LastLagScreenResetTime >= 60)
      {
        for (auto & _i : m_Players)
        {
          // stop the lag screen

          for (auto & player : m_Players)
          {
            if (player->GetLagging())
              Send(_i, m_Protocol->SEND_W3GS_STOP_LAG(player));
          }

          Send(_i, m_Protocol->SEND_W3GS_INCOMING_ACTION(queue<CIncomingAction *>(), 0));

          // start the lag screen

          Send(_i, m_Protocol->SEND_W3GS_START_LAG(m_Players));
        }

        // Warcraft III doesn't seem to respond to empty actions

        m_LastLagScreenResetTime = Time;
      }

      // check if anyone has stopped lagging normally
      // we consider a player to have stopped lagging if they're less than half m_SyncLimit keepalives behind

      for (auto & player : m_Players)
      {
        if (player->GetLagging() && m_SyncCounter - player->GetSyncCounter() < m_SyncLimit / 2)
        {
          // stop the lag screen for this player

          Print("[GAME: " + m_GameName + "] stopped lagging on [" + player->GetName() + "]");
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

      // reset m_LastActionSentTicks because we want the game to stop running while the lag screen is up

      m_LastActionSentTicks = Ticks;

      // keep track of the last lag screen time so we can avoid timing out players

      m_LastLagScreenTime = Time;
    }
  }

  // send actions every m_Latency milliseconds
  // actions are at the heart of every Warcraft 3 game but luckily we don't need to know their contents to relay them
  // we queue player actions in EventPlayerAction then just resend them in batches to all players here

  if (m_GameLoaded && !m_Lagging && Ticks - m_LastActionSentTicks >= m_Latency - m_LastActionLateBy)
    SendAllActions();

  // end the game if there aren't any players left

  if (m_Players.empty() && (m_GameLoading || m_GameLoaded))
  {
    Print("[GAME: " + m_GameName + "] is over (no players left)");
    Print("[GAME: " + m_GameName + "] saving game data to database");

    return true;
  }

  // check if the game is loaded

  if (m_GameLoading)
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
      m_LastActionSentTicks = Ticks;
      m_GameLoading = false;
      m_GameLoaded = true;
      EventGameLoaded();
    }
  }

  // start the gameover timer if there's only one player left

  if (m_Players.size() == 1 && m_FakePlayers.empty() && m_GameOverTime == 0 && (m_GameLoading || m_GameLoaded))
  {
    Print("[GAME: " + m_GameName + "] gameover timer started (one player left)");
    m_GameOverTime = Time;
  }

  // finish the gameover timer

  if (m_GameOverTime != 0 && Time - m_GameOverTime >= 60)
  {
    for (auto & player : m_Players)
    {
      if (!player->GetDeleteMe())
      {
        Print("[GAME: " + m_GameName + "] is over (gameover timer finished)");
        StopPlayers("was disconnected (gameover timer finished)");
        break;
      }
    }
  }

  if (m_GameLoaded)
    return m_Exiting;

  // refresh every 3 seconds

  if (!m_RefreshError && !m_CountDownStarted && m_GameState == GAME_PUBLIC && GetSlotsOpen() > 0 && Time - m_LastRefreshTime >= 3)
  {
    m_LastRefreshTime = Time;
  }

  // send more map data

  if (!m_GameLoading && !m_GameLoaded && Ticks - m_LastDownloadCounterResetTicks >= 1000)
  {
    // hackhack: another timer hijack is in progress here
    // since the download counter is reset once per second it's a great place to update the slot info if necessary

    if (m_SlotInfoChanged)
      SendAllSlotInfo();

    m_DownloadCounter = 0;
    m_LastDownloadCounterResetTicks = Ticks;
  }

  if (!m_GameLoading && Ticks - m_LastDownloadTicks >= 100)
  {
    uint32_t Downloaders = 0;

    for (auto & player : m_Players)
    {
      if (player->GetDownloadStarted() && !player->GetDownloadFinished())
      {
        ++Downloaders;

        if (m_Aura->m_MaxDownloaders > 0 && Downloaders > m_Aura->m_MaxDownloaders)
          break;

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
          if (player->GetLastMapPartSent() == 0)
          {
            // overwrite the "started download ticks" since this is the first time we've sent any map data to the player
            // prior to this we've only determined if the player needs to download the map but it's possible we could have delayed sending any data due to download limits

            player->SetStartedDownloadingTicks(Ticks);
          }

          // limit the download speed if we're sending too much data
          // the download counter is the # of map bytes downloaded in the last second (it's reset once per second)

          if (m_Aura->m_MaxDownloadSpeed > 0 && m_DownloadCounter > m_Aura->m_MaxDownloadSpeed * 1024)
            break;

          Send(player, m_Protocol->SEND_W3GS_MAPPART(GetHostPID(), player->GetPID(), player->GetLastMapPartSent(), m_Map->GetMapData()));
          player->SetLastMapPartSent(player->GetLastMapPartSent() + 1442);
          m_DownloadCounter += 1442;
        }
      }
    }

    m_LastDownloadTicks = Ticks;
  }

  // countdown every 500 ms

  if (m_CountDownStarted && Ticks - m_LastCountDownTicks >= 500)
  {
    if (m_CountDownCounter > 0)
    {
      // we use a countdown counter rather than a "finish countdown time" here because it might alternately round up or down the count
      // this sometimes resulted in a countdown of e.g. "6 5 3 2 1" during my testing which looks pretty dumb
      // doing it this way ensures it's always "5 4 3 2 1" but each int32_terval might not be *exactly* the same length

      SendAllChat(to_string(m_CountDownCounter--) + ". . .");
    }
    else if (!m_GameLoading && !m_GameLoaded)
      EventGameStarted();

    m_LastCountDownTicks = Ticks;
  }

  // create the virtual host player

  if (!m_GameLoading && !m_GameLoaded && GetNumPlayers() < 12)
    CreateVirtualHost();

  // accept new connections

  if (m_Socket)
  {
    CTCPSocket *NewSocket = m_Socket->Accept((fd_set *) fd);

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
    player->GetSocket()->DoSend((fd_set *) send_fd);

  for (auto & potential : m_Potentials)
  {
    if (potential->GetSocket())
      potential->GetSocket()->DoSend((fd_set *) send_fd);
  }
}

void CGame::Send(CGamePlayer *player, const BYTEARRAY &data)
{
  if (player)
    player->Send(data);
}

void CGame::Send(uint8_t PID, const BYTEARRAY &data)
{
  Send(GetPlayerFromPID(PID), data);
}

void CGame::Send(const BYTEARRAY &PIDs, const BYTEARRAY &data)
{
  for (auto & PID : PIDs)
    Send(PID, data);
}

void CGame::SendAll(const BYTEARRAY &data)
{
  for (auto & player : m_Players)
    player->Send(data);
}

void CGame::SendChat(uint8_t fromPID, CGamePlayer *player, const string &message)
{
  // send a private message to one player - it'll be marked [Private] in Warcraft 3

  if (player)
  {
    if (!m_GameLoading && !m_GameLoaded)
    {
      if (message.size() > 254)
        Send(player, m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, CreateByteArray(player->GetPID()), 16, BYTEARRAY(), message.substr(0, 254)));
      else
        Send(player, m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, CreateByteArray(player->GetPID()), 16, BYTEARRAY(), message));
    }
    else
    {
      uint8_t ExtraFlags[] = { 3, 0, 0, 0 };

      // based on my limited testing it seems that the extra flags' first byte contains 3 plus the recipient's colour to denote a private message

      uint8_t SID = GetSIDFromPID(player->GetPID());

      if (SID < m_Slots.size())
        ExtraFlags[0] = 3 + m_Slots[SID].GetColour();

      if (message.size() > 127)
        Send(player, m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, CreateByteArray(player->GetPID()), 32, CreateByteArray(ExtraFlags, 4), message.substr(0, 127)));
      else
        Send(player, m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, CreateByteArray(player->GetPID()), 32, CreateByteArray(ExtraFlags, 4), message));
    }
  }
}

void CGame::SendChat(uint8_t fromPID, uint8_t toPID, const string &message)
{
  SendChat(fromPID, GetPlayerFromPID(toPID), message);
}

void CGame::SendChat(CGamePlayer *player, const string &message)
{
  SendChat(GetHostPID(), player, message);
}

void CGame::SendChat(uint8_t toPID, const string &message)
{
  SendChat(GetHostPID(), toPID, message);
}

void CGame::SendAllChat(uint8_t fromPID, const string &message)
{
  // send a public message to all players - it'll be marked [All] in Warcraft 3

  if (GetNumHumanPlayers() > 0)
  {
    Print("[GAME: " + m_GameName + "] [Local] " + message);

    if (!m_GameLoading && !m_GameLoaded)
    {
      if (message.size() > 254)
        SendAll(m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, GetPIDs(), 16, BYTEARRAY(), message.substr(0, 254)));
      else
        SendAll(m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, GetPIDs(), 16, BYTEARRAY(), message));
    }
    else
    {
      if (message.size() > 127)
        SendAll(m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, GetPIDs(), 32, CreateByteArray((uint32_t) 0, false), message.substr(0, 127)));
      else
        SendAll(m_Protocol->SEND_W3GS_CHAT_FROM_HOST(fromPID, GetPIDs(), 32, CreateByteArray((uint32_t) 0, false), message));
    }
  }
}

void CGame::SendAllChat(const string &message)
{
  SendAllChat(GetHostPID(), message);
}

void CGame::SendAllSlotInfo()
{
  if (!m_GameLoading && !m_GameLoaded)
  {
    SendAll(m_Protocol->SEND_W3GS_SLOTINFO(m_Slots, m_RandomSeed, m_Map->GetMapLayoutStyle(), m_Map->GetMapNumPlayers()));
    m_SlotInfoChanged = false;
  }
}

void CGame::SendVirtualHostPlayerInfo(CGamePlayer *player)
{
  if (m_VirtualHostPID == 255)
    return;

  const BYTEARRAY IP = {0, 0, 0, 0};

  Send(player, m_Protocol->SEND_W3GS_PLAYERINFO(m_VirtualHostPID, m_VirtualHostName, IP, IP));
}

void CGame::SendFakePlayerInfo(CGamePlayer *player)
{
  if (m_FakePlayers.empty())
    return;

  const BYTEARRAY IP = {0, 0, 0, 0};

  for (auto & fakeplayer : m_FakePlayers)
    Send(player, m_Protocol->SEND_W3GS_PLAYERINFO(fakeplayer, "Troll[" + to_string(fakeplayer) + "]", IP, IP));
}

void CGame::SendAllActions()
{
  m_GameTicks += m_Latency;
  ++m_SyncCounter;

  // we aren't allowed to send more than 1460 bytes in a single packet but it's possible we might have more than that many bytes waiting in the queue

  if (!m_Actions.empty())
  {
    // we use a "sub actions queue" which we keep adding actions to until we reach the size limit
    // start by adding one action to the sub actions queue

    queue<CIncomingAction *> SubActions;
    CIncomingAction *Action = m_Actions.front();
    m_Actions.pop();
    SubActions.push(Action);
    uint32_t SubActionsLength = Action->GetLength();

    while (!m_Actions.empty())
    {
      Action = m_Actions.front();
      m_Actions.pop();

      // check if adding the next action to the sub actions queue would put us over the limit (1452 because the INCOMING_ACTION and INCOMING_ACTION2 packets use an extra 8 bytes)

      if (SubActionsLength + Action->GetLength() > 1452)
      {
        // we'd be over the limit if we added the next action to the sub actions queue
        // so send everything already in the queue and then clear it out
        // the W3GS_INCOMING_ACTION2 packet handles the overflow but it must be sent *before* the corresponding W3GS_INCOMING_ACTION packet

        SendAll(m_Protocol->SEND_W3GS_INCOMING_ACTION2(SubActions));

        while (!SubActions.empty())
        {
          delete SubActions.front();
          SubActions.pop();
        }

        SubActionsLength = 0;
      }

      SubActions.push(Action);
      SubActionsLength += Action->GetLength();
    }

    SendAll(m_Protocol->SEND_W3GS_INCOMING_ACTION(SubActions, m_Latency));

    while (!SubActions.empty())
    {
      delete SubActions.front();
      SubActions.pop();
    }
  }
  else
    SendAll(m_Protocol->SEND_W3GS_INCOMING_ACTION(m_Actions, m_Latency));

  const uint32_t Ticks = GetTicks();
  const uint32_t ActualSendInterval = Ticks - m_LastActionSentTicks;
  const uint32_t ExpectedSendInterval = m_Latency - m_LastActionLateBy;
  m_LastActionLateBy = ActualSendInterval - ExpectedSendInterval;

  if (m_LastActionLateBy > m_Latency)
  {
    // something is going terribly wrong - Aura++ is probably starved of resources
    // print a message because even though this will take more resources it should provide some information to the administrator for future reference
    // other solutions - dynamically modify the latency, request higher priority, terminate other games, ???

    // this program is SO FAST, I've yet to see this happen *coolface*

    Print("[GAME: " + m_GameName + "] warning - the latency is " + to_string(m_Latency) + "ms but the last update was late by " + to_string(m_LastActionLateBy) + "ms");
    m_LastActionLateBy = m_Latency;
  }

  m_LastActionSentTicks = Ticks;
}

void CGame::EventPlayerDeleted(CGamePlayer *player)
{
  Print("[GAME: " + m_GameName + "] deleting player [" + player->GetName() + "]: " + player->GetLeftReason());

  m_LastPlayerLeaveTicks = GetTicks();

  // in some cases we're forced to send the left message early so don't send it again

  if (player->GetLeftMessageSent())
    return;

  if (m_GameLoaded)
    SendAllChat(player->GetName() + " " + player->GetLeftReason() + ".");

  if (player->GetLagging())
    SendAll(m_Protocol->SEND_W3GS_STOP_LAG(player));

  // tell everyone about the player leaving

  SendAll(m_Protocol->SEND_W3GS_PLAYERLEAVE_OTHERS(player->GetPID(), player->GetLeftCode()));

  // abort the countdown if there was one in progress

  if (m_CountDownStarted && !m_GameLoading && !m_GameLoaded)
  {
    SendAllChat("Countdown aborted!");
    m_CountDownStarted = false;
  }
}

void CGame::EventPlayerDisconnectTimedOut(CGamePlayer *player)
{
  // not only do we not do any timeouts if the game is lagging, we allow for an additional grace period of 10 seconds
  // this is because Warcraft 3 stops sending packets during the lag screen
  // so when the lag screen finishes we would immediately disconnect everyone if we didn't give them some extra time

  if (GetTime() - m_LastLagScreenTime >= 10)
  {
    player->SetDeleteMe(true);
    player->SetLeftReason("has lost the connection (timed out)");
    player->SetLeftCode(PLAYERLEAVE_DISCONNECT);

    if (!m_GameLoading && !m_GameLoaded)
      OpenSlot(GetSIDFromPID(player->GetPID()), false);
  }
}

void CGame::EventPlayerDisconnectSocketError(CGamePlayer *player)
{
  player->SetDeleteMe(true);
  player->SetLeftReason("has lost the connection (connection error - " + player->GetSocket()->GetErrorString() + ")");
  player->SetLeftCode(PLAYERLEAVE_DISCONNECT);

  if (!m_GameLoading && !m_GameLoaded)
    OpenSlot(GetSIDFromPID(player->GetPID()), false);
}

void CGame::EventPlayerDisconnectConnectionClosed(CGamePlayer *player)
{
  player->SetDeleteMe(true);
  player->SetLeftReason("has lost the connection (connection closed by remote host)");
  player->SetLeftCode(PLAYERLEAVE_DISCONNECT);

  if (!m_GameLoading && !m_GameLoaded)
    OpenSlot(GetSIDFromPID(player->GetPID()), false);
}

void CGame::EventPlayerJoined(CPotentialPlayer *potential, CIncomingJoinPlayer *joinPlayer)
{
  // check the new player's name

  if (joinPlayer->GetName().empty() || joinPlayer->GetName().size() > 15 || joinPlayer->GetName() == m_VirtualHostName || /*GetPlayerFromName(joinPlayer->GetName(), false) ||*/ joinPlayer->GetName().find(" ") != string::npos || joinPlayer->GetName().find("|") != string::npos)
  {
    Print("[GAME: " + m_GameName + "] player [" + joinPlayer->GetName() + "|" + potential->GetExternalIPString() + "] invalid name (taken, invalid char, spoofer, too long)");
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
      Print("[GAME: " + m_GameName + "] player [" + joinPlayer->GetName() + "|" + potential->GetExternalIPString() + "] is trying to join the game over LAN but used an incorrect entry key");
      potential->Send(m_Protocol->SEND_W3GS_REJECTJOIN(REJECTJOIN_WRONGPASSWORD));
      potential->SetDeleteMe(true);
      return;
    }
  }

  // try to find an empty slot
  uint8_t SID = GetEmptySlot();
  if (SID == 255 && IsOwner(joinPlayer->GetName()))
  {
    // the owner player is trying to join the game but it's full and we couldn't even find a reserved slot, kick the player in the lowest numbered slot
    // updated this to try to find a player slot so that we don't end up kicking a computer

    SID = 0;

    for (uint8_t i = 0; i < m_Slots.size(); ++i)
    {
      if (m_Slots[i].GetSlotStatus() == SLOTSTATUS_OCCUPIED && m_Slots[i].GetComputer() == 0)
      {
        SID = i;
        break;
      }
    }

    CGamePlayer *KickedPlayer = GetPlayerFromSID(SID);

    if (KickedPlayer)
    {
      KickedPlayer->SetDeleteMe(true);
      KickedPlayer->SetLeftReason("was kicked to make room for the owner player [" + joinPlayer->GetName() + "]");
      KickedPlayer->SetLeftCode(PLAYERLEAVE_LOBBY);

      // send a playerleave message immediately since it won't normally get sent until the player is deleted which is after we send a playerjoin message
      // we don't need to call OpenSlot here because we're about to overwrite the slot data anyway

      SendAll(m_Protocol->SEND_W3GS_PLAYERLEAVE_OTHERS(KickedPlayer->GetPID(), KickedPlayer->GetLeftCode()));
      KickedPlayer->SetLeftMessageSent(true);
    }
  }

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

  Print("[GAME: " + m_GameName + "] player [" + joinPlayer->GetName() + "|" + potential->GetExternalIPString() + "] joined the game");
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
  SendFakePlayerInfo(Player);

  for (auto & player : m_Players)
  {
    if (!player->GetLeftMessageSent() && player != Player)
    {
      // send info about the new player to every other player

      player->Send(m_Protocol->SEND_W3GS_PLAYERINFO(Player->GetPID(), Player->GetName(), Player->GetExternalIP(), Player->GetInternalIP()));

      // send info about every other player to the new player
      Player->Send(m_Protocol->SEND_W3GS_PLAYERINFO(player->GetPID(), player->GetName(), player->GetExternalIP(), player->GetInternalIP()));
    }
  }

  // send a map check packet to the new player

  Player->Send(m_Protocol->SEND_W3GS_MAPCHECK(m_Map->GetMapPath(), m_Map->GetMapSize(), m_Map->GetMapInfo(), m_Map->GetMapCRC(), m_Map->GetMapSHA1()));

  // send slot info to everyone, so the new player gets this info twice but everyone else still needs to know the new slot layout

  SendAllSlotInfo();

  // check for multiple IP usage

  string Others;

  for (auto & player : m_Players)
  {
    if (Player != player && Player->GetExternalIPString() == player->GetExternalIPString())
    {
      if (Others.empty())
        Others = player->GetName();
      else
        Others += ", " + player->GetName();
    }
  }

  if (!Others.empty())
    SendAllChat("Player [" + joinPlayer->GetName() + "] has the same IP address as: " + Others);

  // abort the countdown if there was one in progress

  if (m_CountDownStarted && !m_GameLoading && !m_GameLoaded)
  {
    SendAllChat("Countdown aborted!");
    m_CountDownStarted = false;
  }

  if (!m_CountDownStarted)
  {
	  switch (m_Aura->m_AutoStart)
	  {
	  case 1:
		  StartCountDown(true);
		  break;
	  case 2:
		  if (GetEmptySlot() == 255)
		  {
			  StartCountDown(true);
		  }
		  break;
	  }
  }
}

void CGame::EventPlayerLeft(CGamePlayer *player, uint32_t reason)
{
  // this function is only called when a player leave packet is received, not when there's a socket error, kick, etc...

  player->SetDeleteMe(true);
  player->SetLeftReason("has left the game voluntarily");
  player->SetLeftCode(PLAYERLEAVE_LOST);

  if (!m_GameLoading && !m_GameLoaded)
    OpenSlot(GetSIDFromPID(player->GetPID()), false);
}

void CGame::EventPlayerLoaded(CGamePlayer *player)
{
  Print("[GAME: " + m_GameName + "] player [" + player->GetName() + "] finished loading in " + to_string((float)(player->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f) + " seconds");


  SendAll(m_Protocol->SEND_W3GS_GAMELOADED_OTHERS(player->GetPID()));
}

void CGame::EventPlayerAction(CGamePlayer *player, CIncomingAction *action)
{
  m_Actions.push(action);

  // check for players saving the game and notify everyone

  if (!action->GetAction()->empty() && (*action->GetAction())[0] == 6)
  {
    Print("[GAME: " + m_GameName + "] player [" + player->GetName() + "] is saving the game");
    SendAllChat("Player [" + player->GetName() + "] is saving the game");
  }
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
      Print("[GAME: " + m_GameName + "] desync detected");
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
    if (chatPlayer->GetType() == CIncomingChatPlayer::CTH_MESSAGE || chatPlayer->GetType() == CIncomingChatPlayer::CTH_MESSAGEEXTRA)
    {
      // relay the chat message to other players

      bool Relay = true;
      const BYTEARRAY ExtraFlags = chatPlayer->GetExtraFlags();

      // calculate timestamp

      string MinString = to_string((m_GameTicks / 1000) / 60);
      string SecString = to_string((m_GameTicks / 1000) % 60);

      if (MinString.size() == 1)
        MinString.insert(0, "0");

      if (SecString.size() == 1)
        SecString.insert(0, "0");

      // handle bot commands

      const string Message = chatPlayer->GetMessage();

      if (!Message.empty() && (Message[0] == m_Aura->m_CommandTrigger || Message[0] == '/'))
      {
        // extract the command trigger, the command, and the payload
        // e.g. "!say hello world" -> command: "say", payload: "hello world"

        string Command, Payload;
        string::size_type PayloadStart = Message.find(" ");

        if (PayloadStart != string::npos)
        {
          Command = Message.substr(1, PayloadStart - 1);
          Payload = Message.substr(PayloadStart + 1);
        }
        else
          Command = Message.substr(1);

        transform(begin(Command), end(Command), begin(Command), ::tolower);

        // don't allow EventPlayerBotCommand to veto a previous instruction to set Relay to false
        // so if Relay is already false (e.g. because the player is muted) then it cannot be forced back to true here

        EventPlayerBotCommand(player, Command, Payload);
        Relay = false;
      }

      if (Relay)
        Send(chatPlayer->GetToPIDs(), m_Protocol->SEND_W3GS_CHAT_FROM_HOST(chatPlayer->GetFromPID(), chatPlayer->GetToPIDs(), chatPlayer->GetFlag(), chatPlayer->GetExtraFlags(), chatPlayer->GetMessage()));
    }
    else
    {
      if (!m_CountDownStarted)
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
}

bool CGame::EventPlayerBotCommand(CGamePlayer *player, string &command, string &payload)
{
  return true;
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
    Print("[GAME: " + m_GameName + "] player [" + player->GetName() + "] voted to drop laggers");
    SendAllChat("Player [" + player->GetName() + "] voted to drop laggers");

    // check if at least half the players voted to drop

    int32_t Votes = 0;

    for (auto & player : m_Players)
    {
      if (player->GetDropVote())
        ++Votes;
    }

    if ((float) Votes / m_Players.size() > 0.50f)
      StopLaggers("lagged out (dropped by vote)");
  }
}

void CGame::EventPlayerMapSize(CGamePlayer *player, CIncomingMapSize *mapSize)
{
  if (m_GameLoading || m_GameLoaded)
    return;

  uint32_t MapSize = ByteArrayToUInt32(m_Map->GetMapSize(), false);

  if (mapSize->GetSizeFlag() != 1 || mapSize->GetMapSize() != MapSize)
  {
    // the player doesn't have the map

    if (m_Aura->m_AllowDownloads)
    {
      string *MapData = m_Map->GetMapData();

      if (!MapData->empty())
      {
        if (m_Aura->m_AllowDownloads == 1 || (m_Aura->m_AllowDownloads == 2 && player->GetDownloadAllowed()))
        {
          if (!player->GetDownloadStarted() && mapSize->GetSizeFlag() == 1)
          {
            // inform the client that we are willing to send the map

            Print("[GAME: " + m_GameName + "] map download started for player [" + player->GetName() + "]");
            Send(player, m_Protocol->SEND_W3GS_STARTDOWNLOAD(GetHostPID()));
            player->SetDownloadStarted(true);
            player->SetStartedDownloadingTicks(GetTicks());
          }
          else
            player->SetLastMapPartAcked(mapSize->GetMapSize());
        }
      }
      else
      {
        player->SetDeleteMe(true);
        player->SetLeftReason("doesn't have the map and there is no local copy of the map to send");
        player->SetLeftCode(PLAYERLEAVE_LOBBY);
        OpenSlot(GetSIDFromPID(player->GetPID()), false);
      }
    }
    else
    {
      player->SetDeleteMe(true);
      player->SetLeftReason("doesn't have the map and map downloads are disabled");
      player->SetLeftCode(PLAYERLEAVE_LOBBY);
      OpenSlot(GetSIDFromPID(player->GetPID()), false);
    }
  }
  else if (player->GetDownloadStarted())
  {
    // calculate download rate

    const float Seconds = (float)(GetTicks() - player->GetStartedDownloadingTicks()) / 1000.f;
    const float Rate = (float) MapSize / 1024.f / Seconds;
    Print("[GAME: " + m_GameName + "] map download finished for player [" + player->GetName() + "] in " + to_string(Seconds) + " seconds");
    SendAllChat("Player [" + player->GetName() + "] downloaded the map in " + to_string(Seconds) + " seconds (" + to_string(Rate) + " KB/sec)");
    player->SetDownloadFinished(true);
    player->SetFinishedDownloadingTime(GetTime());
  }

  uint8_t NewDownloadStatus = (uint8_t)((float) mapSize->GetMapSize() / MapSize * 100.f);
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

void CGame::EventPlayerPongToHost(CGamePlayer *player)
{
  // autokick players with excessive pings but only if they're not reserved and we've received at least 3 pings from them
  // also don't kick anyone if the game is loading or loaded - this could happen because we send pings during loading but we stop sending them after the game is loaded
  // see the Update function for where we send pings

  if (!m_GameLoading && !m_GameLoaded && !player->GetDeleteMe() && player->GetNumPings() >= 3 && player->GetPing(m_Aura->m_LCPings) > m_Aura->m_AutoKickPing)
  {
    // send a chat message because we don't normally do so when a player leaves the lobby

    SendAllChat("Autokicking player [" + player->GetName()  + "] for excessive ping of " + to_string(player->GetPing(m_Aura->m_LCPings)));
    player->SetDeleteMe(true);
    player->SetLeftReason("was autokicked for excessive ping of " + to_string(player->GetPing(m_Aura->m_LCPings)));
    player->SetLeftCode(PLAYERLEAVE_LOBBY);
    OpenSlot(GetSIDFromPID(player->GetPID()), false);
  }
}

void CGame::EventGameStarted()
{
  Print("[GAME: " + m_GameName + "] started loading with " + to_string(GetNumHumanPlayers()) + " players");

  // send a final slot info update if necessary
  // this typically won't happen because we prevent the !start command from completing while someone is downloading the map
  // however, if someone uses !start force while a player is downloading the map this could trigger
  // this is because we only permit slot info updates to be flagged when it's just a change in download status, all others are sent immediately
  // it might not be necessary but let's clean up the mess anyway

  if (m_SlotInfoChanged)
    SendAllSlotInfo();

  m_StartedLoadingTicks = GetTicks();
  m_LastLagScreenResetTime = GetTime();
  m_GameLoading = true;

  // since we use a fake countdown to deal with leavers during countdown the COUNTDOWN_START and COUNTDOWN_END packets are sent in quick succession
  // send a start countdown packet

  SendAll(m_Protocol->SEND_W3GS_COUNTDOWN_START());

  // remove the virtual host player

  DeleteVirtualHost();

  // send an end countdown packet

  SendAll(m_Protocol->SEND_W3GS_COUNTDOWN_END());

  // send a game loaded packet for the fake player (if present)

  for (auto & fakeplayer : m_FakePlayers)
    SendAll(m_Protocol->SEND_W3GS_GAMELOADED_OTHERS(fakeplayer));

  // record the number of starting players

  m_StartPlayers = GetNumHumanPlayers();

  // close the listening socket

  delete m_Socket;
  m_Socket = nullptr;

  // delete any potential players that are still hanging around

  for (auto & potential : m_Potentials)
    delete potential;

  m_Potentials.clear();

  // delete the map data

  delete m_Map;
  m_Map = nullptr;

  // move the game to the games in progress vector

  m_Aura->m_CurrentGame = nullptr;
  m_Aura->m_Games.push_back(this);
}

void CGame::EventGameLoaded()
{
  Print("[GAME: " + m_GameName + "] finished loading with " + to_string(GetNumHumanPlayers()) + " players");

  // send shortest, longest, and personal load times to each player

  CGamePlayer *Shortest = nullptr;
  CGamePlayer *Longest = nullptr;

  for (auto & player : m_Players)
  {
    if (!Shortest || player->GetFinishedLoadingTicks() < Shortest->GetFinishedLoadingTicks())
      Shortest = player;

    if (!Longest || player->GetFinishedLoadingTicks() > Longest->GetFinishedLoadingTicks())
      Longest = player;
  }

  if (Shortest && Longest)
  {
    SendAllChat("Shortest load by player [" + Shortest->GetName() + "] was " + to_string((float)(Shortest->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f) + " seconds");
    SendAllChat("Longest load by player [" + Longest->GetName() + "] was " + to_string((float)(Longest->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f) + " seconds");
  }

  for (auto & player : m_Players)
    SendChat(player, "Your load time was " + to_string((float)(player->GetFinishedLoadingTicks() - m_StartedLoadingTicks) / 1000.f) + " seconds");
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

CGamePlayer *CGame::GetPlayerFromPID(uint8_t PID)
{
  for (auto & player : m_Players)
  {
    if (!player->GetLeftMessageSent() && player->GetPID() == PID)
      return player;
  }

  return nullptr;
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

CGamePlayer *CGame::GetPlayerFromName(string name, bool sensitive)
{
  if (!sensitive)
    transform(begin(name), end(name), begin(name), ::tolower);

  for (auto & player : m_Players)
  {
    if (!player->GetLeftMessageSent())
    {
      string TestName = player->GetName();

      if (!sensitive)
        transform(begin(TestName), end(TestName), begin(TestName), ::tolower);

      if (TestName == name)
        return player;
    }
  }

  return nullptr;
}

uint32_t CGame::GetPlayerFromNamePartial(string name, CGamePlayer **player)
{
  transform(begin(name), end(name), begin(name), ::tolower);
  uint32_t Matches = 0;
  *player = nullptr;

  // try to match each player with the passed string (e.g. "Varlock" would be matched with "lock")

  for (auto & realplayer : m_Players)
  {
    if (!realplayer->GetLeftMessageSent())
    {
      string TestName = realplayer->GetName();
      transform(begin(TestName), end(TestName), begin(TestName), ::tolower);

      if (TestName.find(name) != string::npos)
      {
        ++Matches;
        *player = realplayer;

        // if the name matches exactly stop any further matching

        if (TestName == name)
        {
          Matches = 1;
          break;
        }
      }
    }
  }

  return Matches;
}

CGamePlayer *CGame::GetPlayerFromColour(uint8_t colour)
{
  for (uint8_t i = 0; i < m_Slots.size(); ++i)
  {
    if (m_Slots[i].GetColour() == colour)
      return GetPlayerFromSID(i);
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

    for (auto & fakeplayer : m_FakePlayers)
    {
      if (fakeplayer == TestPID)
      {
        InUse = true;
        break;
      }
    }

    if (InUse)
      continue;

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

BYTEARRAY CGame::GetPIDs(uint8_t excludePID)
{
  BYTEARRAY result;

  for (auto & player : m_Players)
  {
    if (!player->GetLeftMessageSent() && player->GetPID() != excludePID)
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

  // try to find the fakeplayer next

  if (!m_FakePlayers.empty())
    return m_FakePlayers[0];

  // try to find the owner player next

  for (auto & player : m_Players)
  {
    if (!player->GetLeftMessageSent() && IsOwner(player->GetName()))
      return player->GetPID();
  }

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

void CGame::OpenSlot(uint8_t SID, bool kick)
{
  if (SID < m_Slots.size())
  {
    if (kick)
    {
      CGamePlayer *Player = GetPlayerFromSID(SID);

      if (Player)
      {
        Player->SetDeleteMe(true);
        Player->SetLeftReason("was kicked when opening a slot");
        Player->SetLeftCode(PLAYERLEAVE_LOBBY);
      }
    }

    CGameSlot Slot = m_Slots[SID];
    m_Slots[SID] = CGameSlot(0, 255, SLOTSTATUS_OPEN, 0, Slot.GetTeam(), Slot.GetColour(), Slot.GetRace());
    SendAllSlotInfo();
  }
}

void CGame::CloseSlot(uint8_t SID, bool kick)
{
  if (SID < m_Slots.size())
  {
    if (kick)
    {
      CGamePlayer *Player = GetPlayerFromSID(SID);

      if (Player)
      {
        Player->SetDeleteMe(true);
        Player->SetLeftReason("was kicked when closing a slot");
        Player->SetLeftCode(PLAYERLEAVE_LOBBY);
      }
    }

    CGameSlot Slot = m_Slots[SID];
    m_Slots[SID] = CGameSlot(0, 255, SLOTSTATUS_CLOSED, 0, Slot.GetTeam(), Slot.GetColour(), Slot.GetRace());
    SendAllSlotInfo();
  }
}

void CGame::ComputerSlot(uint8_t SID, uint8_t skill, bool kick)
{
  if (SID < m_Slots.size() && skill < 3)
  {
    if (kick)
    {
      CGamePlayer *Player = GetPlayerFromSID(SID);

      if (Player)
      {
        Player->SetDeleteMe(true);
        Player->SetLeftReason("was kicked when creating a computer in a slot");
        Player->SetLeftCode(PLAYERLEAVE_LOBBY);
      }
    }

    CGameSlot Slot = m_Slots[SID];
    m_Slots[SID] = CGameSlot(0, 100, SLOTSTATUS_OCCUPIED, 1, Slot.GetTeam(), Slot.GetColour(), Slot.GetRace(), skill);
    SendAllSlotInfo();
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

void CGame::OpenAllSlots()
{
  bool Changed = false;

  for (auto & slot : m_Slots)
  {
    if (slot.GetSlotStatus() == SLOTSTATUS_CLOSED)
    {
      slot.SetSlotStatus(SLOTSTATUS_OPEN);
      Changed = true;
    }
  }

  if (Changed)
    SendAllSlotInfo();
}

void CGame::CloseAllSlots()
{
  bool Changed = false;

  for (auto & slot : m_Slots)
  {
    if (slot.GetSlotStatus() == SLOTSTATUS_OPEN)
    {
      slot.SetSlotStatus(SLOTSTATUS_CLOSED);
      Changed = true;
    }
  }

  if (Changed)
    SendAllSlotInfo();
}

void CGame::ShuffleSlots()
{
  // we only want to shuffle the player slots
  // that means we need to prevent this function from shuffling the open/closed/computer slots too
  // so we start by copying the player slots to a temporary vector

  vector<CGameSlot> PlayerSlots;

  for (auto & slot : m_Slots)
  {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetComputer() == 0 && slot.GetTeam() != 12)
      PlayerSlots.push_back(slot);
  }

  // now we shuffle PlayerSlots

  if (m_Map->GetMapOptions() & MAPOPT_CUSTOMFORCES)
  {
    // rather than rolling our own probably broken shuffle algorithm we use random_shuffle because it's guaranteed to do it properly
    // so in order to let random_shuffle do all the work we need a vector to operate on
    // unfortunately we can't just use PlayerSlots because the team/colour/race shouldn't be modified
    // so make a vector we can use

    vector<uint8_t> SIDs;

    for (uint8_t i = 0; i < PlayerSlots.size(); ++i)
      SIDs.push_back(i);

    random_shuffle(begin(SIDs), end(SIDs));

    // now put the PlayerSlots vector in the same order as the SIDs vector

    vector<CGameSlot> Slots;

    // as usual don't modify the team/colour/race

    for (uint8_t i = 0; i < SIDs.size(); ++i)
      Slots.push_back(CGameSlot(PlayerSlots[SIDs[i]].GetPID(), PlayerSlots[SIDs[i]].GetDownloadStatus(), PlayerSlots[SIDs[i]].GetSlotStatus(), PlayerSlots[SIDs[i]].GetComputer(), PlayerSlots[i].GetTeam(), PlayerSlots[i].GetColour(), PlayerSlots[i].GetRace()));

    PlayerSlots = Slots;
  }
  else
  {
    // regular game
    // it's easy when we're allowed to swap the team/colour/race!

    random_shuffle(begin(PlayerSlots), end(PlayerSlots));
  }

  // now we put m_Slots back together again

  auto CurrentPlayer = begin(PlayerSlots);
  vector<CGameSlot> Slots;

  for (auto & slot : m_Slots)
  {
    if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetComputer() == 0 && slot.GetTeam() != 12)
    {
      Slots.push_back(*CurrentPlayer);
      ++CurrentPlayer;
    }
    else
      Slots.push_back(slot);
  }

  m_Slots = Slots;

  // and finally tell everyone about the new slot configuration

  SendAllSlotInfo();
}

bool CGame::IsOwner(string name)
{
  string OwnerLower = m_OwnerName;
  transform(begin(name), end(name), begin(name), ::tolower);
  transform(begin(OwnerLower), end(OwnerLower), begin(OwnerLower), ::tolower);

  return name == OwnerLower;
}

bool CGame::IsDownloading()
{
  // returns true if at least one player is downloading the map

  for (auto & player : m_Players)
  {
    if (player->GetDownloadStarted() && !player->GetDownloadFinished())
      return true;
  }

  return false;
}

void CGame::StartCountDown(bool force)
{
  if (!m_CountDownStarted)
  {
    if (force)
    {
      m_CountDownStarted = true;
      m_CountDownCounter = 5;
    }
    else
    {
      // check if everyone has the map

      string StillDownloading;

      for (auto & slot : m_Slots)
      {
        if (slot.GetSlotStatus() == SLOTSTATUS_OCCUPIED && slot.GetComputer() == 0 && slot.GetDownloadStatus() != 100)
        {
          CGamePlayer *Player = GetPlayerFromPID(slot.GetPID());

          if (Player)
          {
            if (StillDownloading.empty())
              StillDownloading = Player->GetName();
            else
              StillDownloading += ", " + Player->GetName();
          }
        }
      }

      if (!StillDownloading.empty())
        SendAllChat("Players still downloading the map: " + StillDownloading);

      // check if everyone has been pinged enough (3 times) that the autokicker would have kicked them by now
      // see function EventPlayerPongToHost for the autokicker code

      string NotPinged;

      for (auto & player : m_Players)
      {
        if (player->GetNumPings() < 3)
        {
          if (NotPinged.empty())
            NotPinged = player->GetName();
          else
            NotPinged += ", " + player->GetName();
        }
      }

      if (!NotPinged.empty())
        SendAllChat("Players not yet pinged 3 times: " + NotPinged);

      // if no problems found start the game

      if (StillDownloading.empty() && NotPinged.empty())
      {
        m_CountDownStarted = true;
        m_CountDownCounter = 5;
      }
    }
  }
}

void CGame::StopPlayers(const string &reason)
{
  // disconnect every player and set their left reason to the passed string
  // we use this function when we want the code in the Update function to run before the destructor (e.g. saving players to the database)
  // therefore calling this function when m_GameLoading || m_GameLoaded is roughly equivalent to setting m_Exiting = true
  // the only difference is whether the code in the Update function is executed or not

  for (auto & player : m_Players)
  {
    player->SetDeleteMe(true);
    player->SetLeftReason(reason);
    player->SetLeftCode(PLAYERLEAVE_LOST);
  }
}

void CGame::StopLaggers(const string &reason)
{
  for (auto & player : m_Players)
  {
    if (player->GetLagging())
    {
      player->SetDeleteMe(true);
      player->SetLeftReason(reason);
      player->SetLeftCode(PLAYERLEAVE_DISCONNECT);
    }
  }
}

void CGame::CreateVirtualHost()
{
  if (m_VirtualHostPID != 255)
    return;

  m_VirtualHostPID = GetNewPID();

  const BYTEARRAY IP = {0, 0, 0, 0};

  SendAll(m_Protocol->SEND_W3GS_PLAYERINFO(m_VirtualHostPID, m_VirtualHostName, IP, IP));
}

void CGame::DeleteVirtualHost()
{
  if (m_VirtualHostPID == 255)
    return;

  SendAll(m_Protocol->SEND_W3GS_PLAYERLEAVE_OTHERS(m_VirtualHostPID, PLAYERLEAVE_LOBBY));
  m_VirtualHostPID = 255;
}

void CGame::CreateFakePlayer()
{
  if (m_FakePlayers.size() > 10)
    return;

  uint8_t SID = GetEmptySlot();

  if (SID < m_Slots.size())
  {
    if (GetNumPlayers() >= 11)
      DeleteVirtualHost();

    const uint8_t FakePlayerPID = GetNewPID();
    const BYTEARRAY IP = {0, 0, 0, 0};

    SendAll(m_Protocol->SEND_W3GS_PLAYERINFO(FakePlayerPID, "Troll[" + to_string(FakePlayerPID) + "]", IP, IP));
    m_Slots[SID] = CGameSlot(FakePlayerPID, 100, SLOTSTATUS_OCCUPIED, 0, m_Slots[SID].GetTeam(), m_Slots[SID].GetColour(), m_Slots[SID].GetRace());
    m_FakePlayers.push_back(FakePlayerPID);
    SendAllSlotInfo();
  }
}

void CGame::DeleteFakePlayers()
{
  if (m_FakePlayers.empty())
    return;

  for (auto & fakeplayer : m_FakePlayers)
  {
    for (auto & slot : m_Slots)
    {
      if (slot.GetPID() == fakeplayer)
      {
        slot = CGameSlot(0, 255, SLOTSTATUS_OPEN, 0, slot.GetTeam(), slot.GetColour(), slot.GetRace());
        SendAll(m_Protocol->SEND_W3GS_PLAYERLEAVE_OTHERS(fakeplayer, PLAYERLEAVE_LOBBY));
        break;
      }
    }
  }

  m_FakePlayers.clear();
  SendAllSlotInfo();
}
