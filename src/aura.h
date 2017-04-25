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

#ifndef AURA_AURA_H_
#define AURA_AURA_H_

#include "includes.h"

//
// CAura
//

class CUDPSocket;
class CTCPSocket;
class CTCPServer;
class CGPSProtocol;
class CCRC32;
class CGame;
class CMap;
class CConfig;

class CAura
{
public:
  CUDPSocket *m_UDPSocket;                      // a UDP socket for sending broadcasts and other junk (used with !sendlan)
  CCRC32 *m_CRC;                                // for calculating CRC's
  CGame *m_CurrentGame;                         // this game is still in the lobby state
  std::vector<CGame *> m_Games;                 // these games are in progress
  CMap *m_Map;                                  // the currently loaded map
  std::string m_MapCFGPath;                     // config value: map cfg path
  std::string m_MapPath;                        // config value: map path
  std::string m_VirtualHostName;                // config value: virtual host name
  std::string m_BindAddress;                    // config value: the address to host games on
  uint32_t m_MaxGames;                          // config value: maximum number of games in progress
  uint32_t m_HostCounter;                       // the current host counter (a unique number to identify a game, incremented each time a game is created)
  uint32_t m_AllowDownloads;                    // config value: allow map downloads or not
  uint32_t m_MaxDownloaders;                    // config value: maximum number of map downloaders at the same time
  uint32_t m_MaxDownloadSpeed;                  // config value: maximum total map download speed in KB/sec
  uint32_t m_AutoKickPing;                      // config value: auto kick players with ping higher than this
  uint32_t m_Latency;                           // config value: the latency (by default)
  uint32_t m_SyncLimit;                         // config value: the maximum number of packets a player can fall out of sync before starting the lag screen (by default)
  uint16_t m_HostPort;                          // config value: the port to host games on
  uint8_t m_LANWar3Version;                     // config value: LAN warcraft 3 version
  bool m_Exiting;                               // set to true to force aura to shutdown next update (used by SignalCatcher)
  bool m_LCPings;                               // config value: use LC style pings (divide actual pings by two)
  uint32_t m_AutoStart;

  explicit CAura(CConfig *CFG);
  ~CAura();
  CAura(CAura &) = delete;

  // processing functions

  bool Update();

  // events

  void EventGameDeleted(CGame *game);

  // other functions
  void CreateGame(CMap *map, std::string gameName);
};

#endif  // AURA_AURA_H_
