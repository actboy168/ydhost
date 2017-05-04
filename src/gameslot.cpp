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

#include "gameslot.h"

//
// CGameSlot
//
CGameSlot::CGameSlot(uint8_t nPID, uint8_t nDownloadStatus, uint8_t nSlotStatus, uint8_t nComputer, uint8_t nTeam, uint8_t nColour, uint8_t nRace, uint8_t nComputerType, uint8_t nHandicap)
	: m_PID(nPID),
	m_DownloadStatus(nDownloadStatus),
	m_SlotStatus(nSlotStatus),
	m_Computer(nComputer),
	m_Team(nTeam),
	m_Colour(nColour),
	m_Race(nRace),
	m_ComputerType(nComputerType),
	m_Handicap(nHandicap)
{

}

CGameSlot::~CGameSlot()
{

}
