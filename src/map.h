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

#ifndef AURA_MAP_H_
#define AURA_MAP_H_

#define MAPSPEED_SLOW                     1
#define MAPSPEED_NORMAL                   2
#define MAPSPEED_FAST                     3

#define MAPVIS_HIDETERRAIN                1
#define MAPVIS_EXPLORED                   2
#define MAPVIS_ALWAYSVISIBLE              3
#define MAPVIS_DEFAULT                    4

#define MAPOBS_NONE                       1
#define MAPOBS_ONDEFEAT                   2
#define MAPOBS_ALLOWED                    3
#define MAPOBS_REFEREES                   4

#define MAPFLAG_TEAMSTOGETHER             1
#define MAPFLAG_FIXEDTEAMS                2
#define MAPFLAG_UNITSHARE                 4
#define MAPFLAG_RANDOMHERO                8
#define MAPFLAG_RANDOMRACES               16

#define MAPOPT_HIDEMINIMAP                1 << 0
#define MAPOPT_MODIFYALLYPRIORITIES       1 << 1
#define MAPOPT_MELEE                      1 << 2  // the bot cares about this one...
#define MAPOPT_REVEALTERRAIN              1 << 4
#define MAPOPT_FIXEDPLAYERSETTINGS        1 << 5  // and this one...
#define MAPOPT_CUSTOMFORCES               1 << 6  // and this one, the rest don't affect the bot's logic
#define MAPOPT_CUSTOMTECHTREE             1 << 7
#define MAPOPT_CUSTOMABILITIES            1 << 8
#define MAPOPT_CUSTOMUPGRADES             1 << 9
#define MAPOPT_WATERWAVESONCLIFFSHORES    1 << 11
#define MAPOPT_WATERWAVESONSLOPESHORES    1 << 12

#define MAPFILTER_TYPE_MELEE              1
#define MAPFILTER_TYPE_SCENARIO           2

#define MAPGAMETYPE_UNKNOWN0              1       // always set except for saved games?
#define MAPGAMETYPE_BLIZZARD              1 << 3
#define MAPGAMETYPE_MELEE                 1 << 5
#define MAPGAMETYPE_SAVEDGAME             1 << 9
#define MAPGAMETYPE_PRIVATEGAME           1 << 11
#define MAPGAMETYPE_MAKERUSER             1 << 13
#define MAPGAMETYPE_MAKERBLIZZARD         1 << 14
#define MAPGAMETYPE_TYPEMELEE             1 << 15
#define MAPGAMETYPE_TYPESCENARIO          1 << 16
#define MAPGAMETYPE_SIZESMALL             1 << 17
#define MAPGAMETYPE_SIZEMEDIUM            1 << 18
#define MAPGAMETYPE_SIZELARGE             1 << 19
#define MAPGAMETYPE_OBSFULL               1 << 20
#define MAPGAMETYPE_OBSONDEATH            1 << 21
#define MAPGAMETYPE_OBSNONE               1 << 22

//
// CMap
//

#include "includes.h"

class CAura;
class CGameSlot;
class CConfig;

class CMap
{
public:
	CAura *m_Aura;

private:
	std::string m_MapData;              // the map data itself, for sending the map to players
	BYTEARRAY m_MapSHA1;                // config value: map sha1 (20 bytes)
	BYTEARRAY m_MapSize;                // config value: map size (4 bytes)
	BYTEARRAY m_MapInfo;                // config value: map info (4 bytes) -> this is the real CRC
	BYTEARRAY m_MapCRC;                 // config value: map crc (4 bytes) -> this is not the real CRC, it's the "xoro" value
	BYTEARRAY m_MapWidth;               // config value: map width (2 bytes)
	BYTEARRAY m_MapHeight;              // config value: map height (2 bytes)
	std::string m_MapPath;              // config value: map path
	std::vector<CGameSlot> m_Slots;
	uint32_t m_MapOptions;
	uint32_t m_MapNumPlayers;
	uint8_t m_MapSpeed;
	uint8_t m_MapVisibility;
	uint8_t m_MapObservers;
	uint8_t m_MapFlags;
	bool m_Valid;

public:
	CMap(CAura *nAura, std::string const& MapPath, CConfig *MAP);
	~CMap();

	inline bool GetValid() const                               { return m_Valid; }
	inline std::string GetMapPath() const                      { return m_MapPath; }
	inline BYTEARRAY GetMapSize() const                        { return m_MapSize; }
	inline BYTEARRAY GetMapInfo() const                        { return m_MapInfo; }
	inline BYTEARRAY GetMapCRC() const                         { return m_MapCRC; }
	inline BYTEARRAY GetMapSHA1() const                        { return m_MapSHA1; }
	inline uint8_t GetMapObservers() const                     { return m_MapObservers; }
	inline uint8_t GetMapFlags() const                         { return m_MapFlags; }
	inline uint32_t GetMapOptions() const                      { return m_MapOptions; }
	inline BYTEARRAY GetMapWidth() const                       { return m_MapWidth; }
	inline BYTEARRAY GetMapHeight() const                      { return m_MapHeight; }
	inline uint32_t GetMapNumPlayers() const                   { return m_MapNumPlayers; }
	inline std::vector<CGameSlot> GetSlots() const             { return m_Slots; }

	BYTEARRAY GetMapGameFlags() const;
	uint8_t GetMapLayoutStyle() const;
	const std::string *GetMapData() const;
	void Load(std::string const& MapPath, CConfig *MAP);
	void CheckValid();
};

#endif  // AURA_MAP_H_
