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
class CGame;
class CMap;
class CConfig;

class CAura
{
public:
	CUDPSocket *m_UDPSocket;                      // a UDP socket for sending broadcasts and other junk (used with !sendlan)
	std::vector<CGame *> m_Games;                 // these games are in progress
	CMap *m_Map;                                  // the currently loaded map
	std::string m_MapCFGPath;                     // config value: map cfg path
	std::string m_MapPath;                        // config value: map path
	std::string m_VirtualHostName;                // config value: virtual host name
	std::string m_BindAddress;                    // config value: the address to host games on
	uint32_t m_HostCounter;                       // the current host counter (a unique number to identify a game, incremented each time a game is created)
	uint32_t m_Latency;                           // config value: the latency (by default)
	uint32_t m_SyncLimit;                         // config value: the maximum number of packets a player can fall out of sync before starting the lag screen (by default)
	uint8_t m_LANWar3Version;                     // config value: LAN warcraft 3 version
	bool m_Exiting;                               // set to true to force aura to shutdown next update (used by SignalCatcher)
	uint32_t m_AutoStart;

	explicit CAura(CConfig *CFG);
	~CAura();
	CAura(CAura &) = delete;

	// processing functions

	bool Update();

	// other functions
	void CreateGame(CMap *map, std::string gameName);
};

#endif  // AURA_AURA_H_
