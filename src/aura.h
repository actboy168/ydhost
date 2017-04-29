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
	std::string m_VirtualHostName;                // config value: virtual host name
	uint32_t m_HostCounter;                       // the current host counter (a unique number to identify a game, incremented each time a game is created)
	bool m_Exiting;                               // set to true to force aura to shutdown next update (used by SignalCatcher)

	explicit CAura(CConfig *CFG);
	~CAura();
	CAura(CAura &) = delete;

	// processing functions

	bool Update();

	// other functions
	void CreateGame(CMap* Map, const std::string& GameName, uint8_t War3Version, uint32_t Latency, uint32_t AutoStart);
};

#endif  // AURA_AURA_H_
