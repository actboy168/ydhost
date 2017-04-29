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

#include "map.h"
#include "util.h"
#include "config.h"
#include "gameslot.h"

using namespace std;

//
// CMap
//

CMap::CMap(std::string const& MapPath, CConfig *MAP)
{
	Load(MapPath, MAP);
}

CMap::~CMap()
{

}

BYTEARRAY CMap::GetMapGameFlags() const
{
	uint32_t GameFlags = 0;

	// speed

	if (m_MapSpeed == MAPSPEED_SLOW)
		GameFlags = 0x00000000;
	else if (m_MapSpeed == MAPSPEED_NORMAL)
		GameFlags = 0x00000001;
	else
		GameFlags = 0x00000002;

	// visibility

	if (m_MapVisibility == MAPVIS_HIDETERRAIN)
		GameFlags |= 0x00000100;
	else if (m_MapVisibility == MAPVIS_EXPLORED)
		GameFlags |= 0x00000200;
	else if (m_MapVisibility == MAPVIS_ALWAYSVISIBLE)
		GameFlags |= 0x00000400;
	else
		GameFlags |= 0x00000800;

	// observers

	if (m_MapObservers == MAPOBS_ONDEFEAT)
		GameFlags |= 0x00002000;
	else if (m_MapObservers == MAPOBS_ALLOWED)
		GameFlags |= 0x00003000;
	else if (m_MapObservers == MAPOBS_REFEREES)
		GameFlags |= 0x40000000;

	// teams/units/hero/race

	if (m_MapFlags & MAPFLAG_TEAMSTOGETHER)
		GameFlags |= 0x00004000;

	if (m_MapFlags & MAPFLAG_FIXEDTEAMS)
		GameFlags |= 0x00060000;

	if (m_MapFlags & MAPFLAG_UNITSHARE)
		GameFlags |= 0x01000000;

	if (m_MapFlags & MAPFLAG_RANDOMHERO)
		GameFlags |= 0x02000000;

	if (m_MapFlags & MAPFLAG_RANDOMRACES)
		GameFlags |= 0x04000000;

	return CreateByteArray(GameFlags, false);
}

uint8_t CMap::GetMapLayoutStyle() const
{
	// 0 = melee
	// 1 = custom forces
	// 2 = fixed player settings (not possible with the Warcraft III map editor)
	// 3 = custom forces + fixed player settings

	if (!(m_MapOptions & MAPOPT_CUSTOMFORCES))
		return 0;

	if (!(m_MapOptions & MAPOPT_FIXEDPLAYERSETTINGS))
		return 1;

	return 3;
}

void CMap::Load(std::string const& MapPath, CConfig *MAP)
{
	m_Valid = true;

	m_MapPath = MapPath;

	m_MapSize = ExtractNumbers(MAP->GetString("map_size", string()), 4);
	m_MapInfo = ExtractNumbers(MAP->GetString("map_info", string()), 4);
	m_MapCRC = ExtractNumbers(MAP->GetString("map_crc", string()), 4);
	m_MapSHA1 = ExtractNumbers(MAP->GetString("map_sha1", string()), 20);

	Print("[MAP] map_size = " + ByteArrayToDecString(m_MapSize));
	Print("[MAP] map_info = " + ByteArrayToDecString(m_MapInfo));
	Print("[MAP] map_crc = " + ByteArrayToDecString(m_MapCRC));
	Print("[MAP] map_sha1 = " + ByteArrayToDecString(m_MapSHA1));

	m_MapOptions = MAP->GetInt("map_options", 0);
	m_MapWidth = ExtractNumbers(MAP->GetString("map_width", string()), 2);
	m_MapHeight = ExtractNumbers(MAP->GetString("map_height", string()), 2);

	Print("[MAP] map_options = " + to_string(m_MapOptions));
	Print("[MAP] map_width = " + ByteArrayToDecString(m_MapWidth));
	Print("[MAP] map_height = " + ByteArrayToDecString(m_MapHeight));

	m_MapNumPlayers = MAP->GetInt("map_numplayers", 0);
	m_Slots.clear();
	for (uint32_t Slot = 1; Slot <= 12; ++Slot)
	{
		string SlotString = MAP->GetString("map_slot" + to_string(Slot), string());
		Print("[MAP] map_slot" + to_string(Slot) + " = " + SlotString);
		if (SlotString.empty())
			break;
		BYTEARRAY SlotData = ExtractNumbers(SlotString, 9);
		m_Slots.push_back(CGameSlot(SlotData));
	}
	m_MapNumPlayers = m_Slots.size();

	m_MapSpeed = MAPSPEED_FAST;
	m_MapVisibility = MAPVIS_DEFAULT;
	m_MapObservers = MAPOBS_NONE;
	m_MapFlags = MAPFLAG_TEAMSTOGETHER | MAPFLAG_FIXEDTEAMS;

	if (m_MapOptions & MAPOPT_MELEE)
	{
		uint8_t Team = 0;
		for (auto & Slot : m_Slots)
		{
			(Slot).SetTeam(Team++);
			(Slot).SetRace(SLOTRACE_RANDOM);
		}
		// force melee maps to have observer slots enabled by default
		if (m_MapObservers == MAPOBS_NONE)
			m_MapObservers = MAPOBS_ALLOWED;
	}

	if (!(m_MapOptions & MAPOPT_FIXEDPLAYERSETTINGS))
	{
		for (auto & Slot : m_Slots)
			(Slot).SetRace((Slot).GetRace() | SLOTRACE_SELECTABLE);
	}

	// if random races is set force every slot's race to random

	if (m_MapFlags & MAPFLAG_RANDOMRACES)
	{
		Print("[MAP] forcing races to random");

		for (auto & slot : m_Slots)
			slot.SetRace(SLOTRACE_RANDOM);
	}

	// add observer slots

	if (m_MapObservers == MAPOBS_ALLOWED || m_MapObservers == MAPOBS_REFEREES)
	{
		Print("[MAP] adding " + to_string(12 - m_Slots.size()) + " observer slots");

		while (m_Slots.size() < 12)
			m_Slots.push_back(CGameSlot(0, 255, SLOTSTATUS_OPEN, 0, 12, 12, SLOTRACE_RANDOM));
	}

	CheckValid();
}

void CMap::CheckValid()
{
	// TODO: should this code fix any errors it sees rather than just warning the user?

	if (m_MapPath.empty() || m_MapPath.length() > 53)
	{
		m_Valid = false;
		Print("[MAP] invalid map_path detected");
	}

	if (m_MapPath.find('/') != string::npos)
		Print("[MAP] warning - map_path contains forward slashes '/' but it must use Windows style back slashes '\\'");

	if (m_MapSize.size() != 4)
	{
		m_Valid = false;
		Print("[MAP] invalid map_size detected");
	}
	else if (!m_MapData.empty() && m_MapData.size() != ByteArrayToUInt32(m_MapSize, false))
	{
		m_Valid = false;
		Print("[MAP] invalid map_size detected - size mismatch with actual map data");
	}

	if (m_MapInfo.size() != 4)
	{
		m_Valid = false;
		Print("[MAP] invalid map_info detected");
	}

	if (m_MapCRC.size() != 4)
	{
		m_Valid = false;
		Print("[MAP] invalid map_crc detected");
	}

	if (m_MapSHA1.size() != 20)
	{
		m_Valid = false;
		Print("[MAP] invalid map_sha1 detected");
	}

	if (m_MapSpeed != MAPSPEED_SLOW && m_MapSpeed != MAPSPEED_NORMAL && m_MapSpeed != MAPSPEED_FAST)
	{
		m_Valid = false;
		Print("[MAP] invalid map_speed detected");
	}

	if (m_MapVisibility != MAPVIS_HIDETERRAIN && m_MapVisibility != MAPVIS_EXPLORED && m_MapVisibility != MAPVIS_ALWAYSVISIBLE && m_MapVisibility != MAPVIS_DEFAULT)
	{
		m_Valid = false;
		Print("[MAP] invalid map_visibility detected");
	}

	if (m_MapObservers != MAPOBS_NONE && m_MapObservers != MAPOBS_ONDEFEAT && m_MapObservers != MAPOBS_ALLOWED && m_MapObservers != MAPOBS_REFEREES)
	{
		m_Valid = false;
		Print("[MAP] invalid map_observers detected");
	}

	if (m_MapWidth.size() != 2)
	{
		m_Valid = false;
		Print("[MAP] invalid map_width detected");
	}

	if (m_MapHeight.size() != 2)
	{
		m_Valid = false;
		Print("[MAP] invalid map_height detected");
	}

	if (m_MapNumPlayers == 0 || m_MapNumPlayers > 12)
	{
		m_Valid = false;
		Print("[MAP] invalid map_numplayers detected");
	}

	if (m_Slots.empty() || m_Slots.size() > 12)
	{
		m_Valid = false;
		Print("[MAP] invalid map_slot<x> detected");
	}
}

const std::string* CMap::GetMapData() const
{
	// todo; 下载地图支持
	return &m_MapData;
}
