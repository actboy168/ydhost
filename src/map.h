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

//
// CMap
//

#include <array>
#include <vector>
#include <stdint.h>

class CAura;
class CGameSlot;
class CConfig;

class CMap
{
public:
	enum class MAPSPEED
	{
		SLOW   = 1,
		NORMAL = 2,
		FAST   = 3,
	};

	enum class MAPVIS
	{
		HIDETERRAIN   = 1,
		EXPLORED      = 2,
		ALWAYSVISIBLE = 3,
		DEFAULT       = 4,
	};

	enum class MAPOBS
	{
		NONE     = 1,
		ONDEFEAT = 2,
		ALLOWED  = 3,
		REFEREES = 4,
	};

	enum class MAPFLAG
	{
		TEAMSTOGETHER = 1,
		FIXEDTEAMS    = 2,
		UNITSHARE     = 4,
		RANDOMHERO    = 8,
		RANDOMRACES   = 16, 
	};

	enum class MAPOPT
	{
		HIDEMINIMAP             = 1 << 0,
		MODIFYALLYPRIORITIES    = 1 << 1,
		MELEE                   = 1 << 2, // the bot cares about this one...
		REVEALTERRAIN           = 1 << 4,
		FIXEDPLAYERSETTINGS     = 1 << 5, // and this one...
		CUSTOMFORCES            = 1 << 6, // and this one, the rest don't affect the bot's logic
		CUSTOMTECHTREE          = 1 << 7,
		CUSTOMABILITIES         = 1 << 8,
		CUSTOMUPGRADES          = 1 << 9,
		WATERWAVESONCLIFFSHORES = 1 << 11,
		WATERWAVESONSLOPESHORES = 1 << 12,
	};

public:
	CMap(std::string const& MapPath, CConfig *MAP);
	~CMap();

	inline bool GetValid() const                               { return m_Valid; }
	inline std::string GetMapPath() const                      { return m_MapPath; }
	inline uint32_t GetMapSize() const                         { return m_MapSize; }
	inline uint32_t GetMapInfo() const                         { return m_MapInfo; }
	inline uint32_t GetMapCRC() const                          { return m_MapCRC; }
	inline std::array<uint8_t, 20> GetMapSHA1() const          { return m_MapSHA1; }
	inline MAPOBS GetMapObservers() const                      { return m_MapObservers; }
	inline uint32_t GetMapFlags() const                         { return m_MapFlags; }
	inline uint32_t GetMapOptions() const                      { return m_MapOptions; }
	inline uint16_t GetMapWidth() const                        { return m_MapWidth; }
	inline uint16_t GetMapHeight() const                       { return m_MapHeight; }
	inline uint32_t GetMapNumPlayers() const                   { return m_MapNumPlayers; }
	inline std::vector<CGameSlot> GetSlots() const             { return m_Slots; }

	uint32_t GetMapGameFlags() const;
	uint8_t GetMapLayoutStyle() const;
	const std::string *GetMapData() const;
	void Load(std::string const& MapPath, CConfig *MAP);
	void CheckValid();

private:
	std::string m_MapData;              // the map data itself, for sending the map to players
	std::array<uint8_t, 20> m_MapSHA1;  // config value: map sha1 (20 bytes)
	uint32_t m_MapSize;                 // config value: map size (4 bytes)
	uint32_t m_MapInfo;                 // config value: map info (4 bytes) -> this is the real CRC
	uint32_t m_MapCRC;                  // config value: map crc (4 bytes) -> this is not the real CRC, it's the "xoro" value
	uint16_t m_MapWidth;                // config value: map width (2 bytes)
	uint16_t m_MapHeight;               // config value: map height (2 bytes)
	std::string m_MapPath;              // config value: map path
	std::vector<CGameSlot> m_Slots;
	uint32_t m_MapOptions;
	uint32_t m_MapNumPlayers;
	MAPSPEED m_MapSpeed;
	MAPVIS   m_MapVisibility;
	MAPOBS   m_MapObservers;
	uint32_t  m_MapFlags;
	bool m_Valid;
};

inline uint32_t operator&(uint32_t a, CMap::MAPFLAG b)
{
	return a & (uint32_t)b;
}

inline uint32_t operator|(CMap::MAPFLAG a, CMap::MAPFLAG b)
{
	return (uint32_t)a | (uint32_t)b;
}

inline uint32_t operator&(uint32_t a, CMap::MAPOPT b)
{
	return a & (uint32_t)b;
}

#endif  // AURA_MAP_H_
