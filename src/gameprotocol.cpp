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

#include "gameprotocol.h"
#include "util.h"
#include "crc32.h"
#include "gameslot.h"

void Print(const std::string &message);

//
// CGameProtocol
//

CGameProtocol::CGameProtocol()
{

}

CGameProtocol::~CGameProtocol()
{

}

///////////////////////
// RECEIVE FUNCTIONS //
///////////////////////

CIncomingJoinPlayer *CGameProtocol::RECEIVE_W3GS_REQJOIN(const BYTEARRAY &data)
{
	// DEBUG_Print( "RECEIVED W3GS_REQJOIN" );
	// DEBUG_Print( data );

	// 2 bytes                    -> Header
	// 2 bytes                    -> Length
	// 4 bytes                    -> Host Counter (Game ID)
	// 4 bytes                    -> Entry Key (used in LAN)
	// 1 byte                     -> ???
	// 2 bytes                    -> Listen Port
	// 4 bytes                    -> Peer Key
	// null terminated string			-> Name
	// 4 bytes                    -> ???
	// 2 bytes                    -> InternalPort (???)
	// 4 bytes                    -> InternalIP

	if (ValidateLength(data) && data.size() >= 20)
	{
		const uint32_t HostCounter = ByteArrayToUInt32(data, false, 4);
		const uint32_t EntryKey = ByteArrayToUInt32(data, false, 8);
		const std::string Name = ExtractCString(data, 19);

		if (!Name.empty() && data.size() >= Name.size() + 30)
		{
			uint32_t InternalIP = ByteArrayToUInt32(data, false, Name.size() + 26);
			return new CIncomingJoinPlayer(HostCounter, EntryKey, Name, InternalIP);
		}
	}

	return nullptr;
}

uint32_t CGameProtocol::RECEIVE_W3GS_LEAVEGAME(const BYTEARRAY &data)
{
	// DEBUG_Print( "RECEIVED W3GS_LEAVEGAME" );
	// DEBUG_Print( data );

	// 2 bytes					-> Header
	// 2 bytes					-> Length
	// 4 bytes					-> Reason

	if (ValidateLength(data) && data.size() >= 8)
		return ByteArrayToUInt32(data, false, 4);

	return 0;
}

bool CGameProtocol::RECEIVE_W3GS_GAMELOADED_SELF(const BYTEARRAY &data)
{
	// DEBUG_Print( "RECEIVED W3GS_GAMELOADED_SELF" );
	// DEBUG_Print( data );

	// 2 bytes					-> Header
	// 2 bytes					-> Length

	if (ValidateLength(data))
		return true;

	return false;
}

CIncomingAction *CGameProtocol::RECEIVE_W3GS_OUTGOING_ACTION(const BYTEARRAY &data, uint8_t PID)
{
	// DEBUG_Print( "RECEIVED W3GS_OUTGOING_ACTION" );
	// DEBUG_Print( data );

	// 2 bytes                -> Header
	// 2 bytes                -> Length
	// 4 bytes                -> CRC
	// remainder of packet		-> Action

	if (PID != 255 && ValidateLength(data) && data.size() >= 8)
	{
		const BYTEARRAY CRC = BYTEARRAY(begin(data) + 4, begin(data) + 8);
		const BYTEARRAY Action = BYTEARRAY(begin(data) + 8, end(data));
		return new CIncomingAction(PID, CRC, Action);
	}

	return nullptr;
}

uint32_t CGameProtocol::RECEIVE_W3GS_OUTGOING_KEEPALIVE(const BYTEARRAY &data)
{
	// DEBUG_Print( "RECEIVED W3GS_OUTGOING_KEEPALIVE" );
	// DEBUG_Print( data );

	// 2 bytes					-> Header
	// 2 bytes					-> Length
	// 1 byte           -> ???
	// 4 bytes					-> CheckSum

	if (ValidateLength(data) && data.size() == 9)
		return ByteArrayToUInt32(data, false, 5);

	return 0;
}

CIncomingChatPlayer *CGameProtocol::RECEIVE_W3GS_CHAT_TO_HOST(const BYTEARRAY &data)
{
	// DEBUG_Print( "RECEIVED W3GS_CHAT_TO_HOST" );
	// DEBUG_Print( data );

	// 2 bytes              -> Header
	// 2 bytes              -> Length
	// 1 byte               -> Total
	// for( 1 .. Total )
	//		1 byte            -> ToPID
	// 1 byte               -> FromPID
	// 1 byte               -> Flag
	// if( Flag == 16 )
	//		null term string	-> Message
	// elseif( Flag == 17 )
	//		1 byte            -> Team
	// elseif( Flag == 18 )
	//		1 byte            -> Colour
	// elseif( Flag == 19 )
	//		1 byte            -> Race
	// elseif( Flag == 20 )
	//		1 byte            -> Handicap
	// elseif( Flag == 32 )
	//		4 bytes           -> ExtraFlags
	//		null term string	-> Message

	if (ValidateLength(data))
	{
		uint32_t i = 5;
		const uint8_t Total = data[4];

		if (Total > 0 && data.size() >= i + Total)
		{
			const BYTEARRAY ToPIDs = BYTEARRAY(begin(data) + i, begin(data) + i + Total);
			i += Total;
			const uint8_t FromPID = data[i];
			const uint8_t Flag = data[i + 1];
			i += 2;

			if (Flag == 16 && data.size() >= i + 1)
			{
				// chat message

				const std::string Message = ExtractCString(data, i);
				return new CIncomingChatPlayer(FromPID, ToPIDs, Flag, Message);
			}
			else if ((Flag >= 17 && Flag <= 20) && data.size() >= i + 1)
			{
				// team/colour/race/handicap change request

				const uint8_t Byte = data[i];
				return new CIncomingChatPlayer(FromPID, ToPIDs, Flag, Byte);
			}
			else if (Flag == 32 && data.size() >= i + 5)
			{
				// chat message with extra flags

				const BYTEARRAY ExtraFlags = BYTEARRAY(begin(data) + i, begin(data) + i + 4);
				const std::string Message = ExtractCString(data, i + 4);
				return new CIncomingChatPlayer(FromPID, ToPIDs, Flag, Message, ExtraFlags);
			}
		}
	}

	return nullptr;
}

CIncomingMapSize *CGameProtocol::RECEIVE_W3GS_MAPSIZE(const BYTEARRAY &data)
{
	// DEBUG_Print( "RECEIVED W3GS_MAPSIZE" );
	// DEBUG_Print( data );

	// 2 bytes					-> Header
	// 2 bytes					-> Length
	// 4 bytes					-> ???
	// 1 byte           -> SizeFlag (1 = have map, 3 = continue download)
	// 4 bytes					-> MapSize

	if (ValidateLength(data) && data.size() >= 13)
		return new CIncomingMapSize(data[8], ByteArrayToUInt32(data, false, 9));

	return nullptr;
}

uint32_t CGameProtocol::RECEIVE_W3GS_PONG_TO_HOST(const BYTEARRAY &data)
{
	// DEBUG_Print( "RECEIVED W3GS_PONG_TO_HOST" );
	// DEBUG_Print( data );

	// 2 bytes					-> Header
	// 2 bytes					-> Length
	// 4 bytes					-> Pong

	// the pong value is just a copy of whatever was sent in SEND_W3GS_PING_FROM_HOST which was GetTicks( ) at the time of sending
	// so as long as we trust that the client isn't trying to fake us out and mess with the pong value we can find the round trip time by simple subtraction
	// (the subtraction is done elsewhere because the very first pong value seems to be 1 and we want to discard that one)

	if (ValidateLength(data) && data.size() >= 8)
		return ByteArrayToUInt32(data, false, 4);

	return 1;
}

////////////////////
// SEND FUNCTIONS //
////////////////////

BYTEARRAY CGameProtocol::SEND_W3GS_PING_FROM_HOST(uint32_t ticks)
{
	BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_PING_FROM_HOST, 8, 0 };
	AppendByteArray(packet, ticks, false);    // ping value
	return packet;
}

BYTEARRAY CGameProtocol::SEND_W3GS_SLOTINFOJOIN(uint8_t PID, uint16_t port, uint32_t externalIP, const std::vector<CGameSlot> &slots, uint32_t randomSeed, uint8_t layoutStyle, uint8_t playerSlots)
{
	BYTEARRAY packet;
	const uint8_t Zeros[] = { 0, 0, 0, 0 };
	const BYTEARRAY SlotInfo = EncodeSlotInfo(slots, randomSeed, layoutStyle, playerSlots);
	packet.push_back(W3GS_HEADER_CONSTANT);   // W3GS header constant
	packet.push_back(W3GS_SLOTINFOJOIN);   // W3GS_SLOTINFOJOIN
	packet.push_back(0);   // packet length will be assigned later
	packet.push_back(0);   // packet length will be assigned later
	AppendByteArray(packet, (uint16_t)SlotInfo.size(), false);    // SlotInfo length
	AppendByteArrayFast(packet, SlotInfo);   // SlotInfo
	packet.push_back(PID);   // PID
	packet.push_back(2);   // AF_INET
	packet.push_back(0);   // AF_INET continued...
	AppendByteArray(packet, port, false);   // port
	AppendByteArray(packet, externalIP, false);   // external IP
	AppendByteArray(packet, Zeros, 4);   // ???
	AppendByteArray(packet, Zeros, 4);   // ???
	AssignLength(packet);
	return packet;
}

BYTEARRAY CGameProtocol::SEND_W3GS_REJECTJOIN(uint32_t reason)
{
	BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_REJECTJOIN, 8, 0 };
	AppendByteArray(packet, reason, false);   // reason
	return packet;
}

BYTEARRAY CGameProtocol::SEND_W3GS_PLAYERINFO(uint8_t PID, const std::string &name, uint32_t externalIP, uint32_t internalIP)
{
	BYTEARRAY packet;

	if (!name.empty() && name.size() <= 15)
	{
		const uint8_t PlayerJoinCounter[] = { 2, 0, 0, 0 };
		const uint8_t Zeros[] = { 0, 0, 0, 0 };

		packet.push_back(W3GS_HEADER_CONSTANT);   // W3GS header constant
		packet.push_back(W3GS_PLAYERINFO);   // W3GS_PLAYERINFO
		packet.push_back(0);   // packet length will be assigned later
		packet.push_back(0);   // packet length will be assigned later
		AppendByteArray(packet, PlayerJoinCounter, 4);   // player join counter
		packet.push_back(PID);   // PID
		AppendByteArrayFast(packet, name);   // player name
		packet.push_back(1);   // ???
		packet.push_back(0);   // ???
		packet.push_back(2);   // AF_INET
		packet.push_back(0);   // AF_INET continued...
		packet.push_back(0);   // port
		packet.push_back(0);   // port continued...
		AppendByteArray(packet, externalIP, false);   // external IP
		AppendByteArray(packet, Zeros, 4);   // ???
		AppendByteArray(packet, Zeros, 4);   // ???
		packet.push_back(2);   // AF_INET
		packet.push_back(0);   // AF_INET continued...
		packet.push_back(0);   // port
		packet.push_back(0);   // port continued...
		AppendByteArray(packet, internalIP, false);   // internal IP
		AppendByteArray(packet, Zeros, 4);   // ???
		AppendByteArray(packet, Zeros, 4);   // ???
		AssignLength(packet);
	}
	else
		Print("[GAMEPROTO] invalid parameters passed to SEND_W3GS_PLAYERINFO");

	return packet;
}

BYTEARRAY CGameProtocol::SEND_W3GS_PLAYERLEAVE_OTHERS(uint8_t PID, uint32_t leftCode)
{
	if (PID != 255)
	{
		BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_PLAYERLEAVE_OTHERS, 9, 0, PID };
		AppendByteArray(packet, leftCode, false);   // left code (see PLAYERLEAVE_ constants in gameprotocol.h)
		return packet;
	}

	Print("[GAMEPROTO] invalid parameters passed to SEND_W3GS_PLAYERLEAVE_OTHERS");
	return BYTEARRAY();
}

BYTEARRAY CGameProtocol::SEND_W3GS_GAMELOADED_OTHERS(uint8_t PID)
{
	if (PID != 255)
		return BYTEARRAY{ W3GS_HEADER_CONSTANT, W3GS_GAMELOADED_OTHERS, 5, 0, PID };

	Print("[GAMEPROTO] invalid parameters passed to SEND_W3GS_GAMELOADED_OTHERS");

	return BYTEARRAY();
}

BYTEARRAY CGameProtocol::SEND_W3GS_SLOTINFO(const std::vector<CGameSlot> &slots, uint32_t randomSeed, uint8_t layoutStyle, uint8_t playerSlots)
{
	const BYTEARRAY SlotInfo = EncodeSlotInfo(slots, randomSeed, layoutStyle, playerSlots);
	const uint16_t SlotInfoSize = (uint16_t)SlotInfo.size();

	BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_SLOTINFO, 0, 0 };
	AppendByteArray(packet, SlotInfoSize, false); // SlotInfo length
	AppendByteArrayFast(packet, SlotInfo);        // SlotInfo
	AssignLength(packet);
	return packet;
}

BYTEARRAY CGameProtocol::SEND_W3GS_COUNTDOWN_START()
{
	return BYTEARRAY{ W3GS_HEADER_CONSTANT, W3GS_COUNTDOWN_START, 4, 0 };
}

BYTEARRAY CGameProtocol::SEND_W3GS_COUNTDOWN_END()
{
	return BYTEARRAY{ W3GS_HEADER_CONSTANT, W3GS_COUNTDOWN_END, 4, 0 };
}

BYTEARRAY CGameProtocol::SEND_W3GS_INCOMING_ACTION(const std::vector<CIncomingAction *>& actions, uint16_t sendInterval)
{
	BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_INCOMING_ACTION, 0, 0 };
	AppendByteArray(packet, sendInterval, false);   // send int32_terval

	// create subpacket

	if (!actions.empty())
	{
		BYTEARRAY subpacket;

		for (auto& act : actions)
		{
			subpacket.push_back(act->GetPID());
			AppendByteArray(subpacket, (uint16_t)act->GetAction()->size(), false);
			AppendByteArrayFast(subpacket, *act->GetAction());
		}

		// calculate crc (we only care about the first 2 bytes though)

		BYTEARRAY crc32 = CreateByteArray(CRC32(subpacket.data(), subpacket.size()), false);
		crc32.resize(2);

		// finish subpacket

		AppendByteArrayFast(packet, crc32);   // crc
		AppendByteArrayFast(packet, subpacket);   // subpacket
	}

	AssignLength(packet);
	return packet;
}

BYTEARRAY CGameProtocol::SEND_W3GS_CHAT_FROM_HOST(uint8_t fromPID, const BYTEARRAY &toPIDs, uint8_t flag, uint32_t flagExtra, const std::string &message)
{
	if (!toPIDs.empty() && !message.empty() && message.size() < 255)
	{
		BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_CHAT_FROM_HOST, 0, 0, (uint8_t)toPIDs.size() };
		AppendByteArrayFast(packet, toPIDs);      // receivers
		packet.push_back(fromPID);                // sender
		packet.push_back(flag);                   // flag
		AppendByteArray(packet, flagExtra, false);   // extra flag
		AppendByteArrayFast(packet, message);     // message
		AssignLength(packet);
		return packet;
	}

	Print("[GAMEPROTO] invalid parameters passed to SEND_W3GS_CHAT_FROM_HOST");
	return BYTEARRAY();
}

BYTEARRAY CGameProtocol::SEND_W3GS_START_LAG(const std::vector<std::pair<uint8_t, uint32_t>>& lags)
{
	if (!lags.empty())
	{
		BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_START_LAG, 0, 0, (uint8_t)lags.size() };

		for (auto& lag : lags)
		{
			packet.push_back(lag.first);
			AppendByteArray(packet, lag.second, false);
		}

		AssignLength(packet);
		return packet;
	}

	Print("[GAMEPROTO] no laggers passed to SEND_W3GS_START_LAG");
	return BYTEARRAY();
}

BYTEARRAY CGameProtocol::SEND_W3GS_STOP_LAG(uint8_t pid, uint32_t time)
{
	BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_STOP_LAG, 9, 0, pid };
	AppendByteArray(packet, time, false);
	return packet;
}

BYTEARRAY CGameProtocol::SEND_W3GS_GAMEINFO(uint8_t war3Version, uint32_t mapGameType, uint32_t mapFlags, uint16_t mapWidth, uint16_t mapHeight, const std::string &gameName, const std::string &hostName, uint32_t upTime, const std::string &mapPath, uint32_t mapCRC, uint32_t slotsTotal, uint32_t slotsOpen, uint16_t port, uint32_t hostCounter, uint32_t entryKey)
{
	if (!gameName.empty() && !hostName.empty() && !mapPath.empty())
	{
		const uint8_t Unknown2[] = { 1, 0, 0, 0 };

		// make the stat string

		BYTEARRAY StatString;
		AppendByteArray(StatString, mapFlags, false);
		StatString.push_back(0);
		AppendByteArray(StatString, mapWidth, false);
		AppendByteArray(StatString, mapHeight, false);
		AppendByteArray(StatString, mapCRC, false);
		AppendByteArrayFast(StatString, mapPath);
		AppendByteArrayFast(StatString, hostName);
		StatString.push_back(0);
		StatString = EncodeStatString(StatString);

		// make the rest of the packet

		BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_GAMEINFO, 0, 0, 80, 88, 51, 87, war3Version, 0, 0, 0 };
		AppendByteArray(packet, hostCounter, false);  // Host Counter
		AppendByteArray(packet, entryKey, false);     // Entry Key
		AppendByteArrayFast(packet, gameName);        // Game Name
		packet.push_back(0);                          // ??? (maybe game password)
		AppendByteArrayFast(packet, StatString);      // Stat String
		packet.push_back(0);                            // Stat String null terminator (the stat string is encoded to remove all even numbers i.e. zeros)
		AppendByteArray(packet, slotsTotal, false);   // Slots Total
		AppendByteArray(packet, mapGameType, false);     // Game Type
		AppendByteArray(packet, Unknown2, 4);         // ???
		AppendByteArray(packet, slotsOpen, false);    // Slots Open
		AppendByteArray(packet, upTime, false);       // time since creation
		AppendByteArray(packet, port, false);         // port
		AssignLength(packet);
		return packet;
	}

	Print("[GAMEPROTO] invalid parameters passed to SEND_W3GS_GAMEINFO");
	return BYTEARRAY();
}

BYTEARRAY CGameProtocol::SEND_W3GS_CREATEGAME(uint8_t war3Version)
{
	return BYTEARRAY{ W3GS_HEADER_CONSTANT, W3GS_CREATEGAME, 16, 0, 80, 88, 51, 87, war3Version, 0, 0, 0, 1, 0, 0, 0 };
}

BYTEARRAY CGameProtocol::SEND_W3GS_REFRESHGAME(uint32_t players, uint32_t playerSlots)
{
	BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_REFRESHGAME, 16, 0, 1, 0, 0, 0 };
	AppendByteArray(packet, players, false);      // Players
	AppendByteArray(packet, playerSlots, false);  // Player Slots
	return packet;
}

BYTEARRAY CGameProtocol::SEND_W3GS_DECREATEGAME()
{
	return BYTEARRAY{ W3GS_HEADER_CONSTANT, W3GS_DECREATEGAME, 8, 0, 1, 0, 0, 0 };
}

BYTEARRAY CGameProtocol::SEND_W3GS_MAPCHECK(const std::string &mapPath, uint32_t mapSize, uint32_t mapInfo, uint32_t mapCRC, const std::array<uint8_t, 20>& mapSHA1)
{
	if (!mapPath.empty())
	{
		BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_MAPCHECK, 0, 0, 1, 0, 0, 0 };
		AppendByteArrayFast(packet, mapPath);     // map path
		AppendByteArray(packet, mapSize, false);     // map size
		AppendByteArray(packet, mapInfo, false);     // map info
		AppendByteArray(packet, mapCRC, false);      // map crc
		AppendByteArray(packet, mapSHA1.data(), mapSHA1.size());     // map sha1
		AssignLength(packet);
		return packet;
	}

	Print("[GAMEPROTO] invalid parameters passed to SEND_W3GS_MAPCHECK");
	return BYTEARRAY();
}

BYTEARRAY CGameProtocol::SEND_W3GS_STARTDOWNLOAD(uint8_t fromPID)
{
	return BYTEARRAY{ W3GS_HEADER_CONSTANT, W3GS_STARTDOWNLOAD, 9, 0, 1, 0, 0, 0, fromPID };
}

BYTEARRAY CGameProtocol::SEND_W3GS_MAPPART(uint8_t fromPID, uint8_t toPID, uint32_t start, const std::string *mapData)
{
	if (start < mapData->size())
	{
		BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_MAPPART, 0, 0, toPID, fromPID, 1, 0, 0, 0 };
		AppendByteArray(packet, start, false);   // start position

		// calculate end position (don't send more than 1442 map bytes in one packet)

		uint32_t End = start + 1442;

		if (End > mapData->size())
			End = mapData->size();

		// calculate crc

		const BYTEARRAY crc32 = CreateByteArray(CRC32((const uint8_t*)mapData->c_str() + start, End - start), false);
		AppendByteArrayFast(packet, crc32);

		// map data

		const BYTEARRAY data = CreateByteArray((const uint8_t *)mapData->c_str() + start, End - start);
		AppendByteArrayFast(packet, data);
		AssignLength(packet);
		return packet;
	}

	Print("[GAMEPROTO] invalid parameters passed to SEND_W3GS_MAPPART");
	return BYTEARRAY();
}

BYTEARRAY CGameProtocol::SEND_W3GS_INCOMING_ACTION2(const std::vector<CIncomingAction *>& actions)
{
	BYTEARRAY packet = { W3GS_HEADER_CONSTANT, W3GS_INCOMING_ACTION2, 0, 0, 0, 0 };

	// create subpacket

	if (!actions.empty())
	{
		BYTEARRAY subpacket;

		for (auto& act : actions)
		{
			subpacket.push_back(act->GetPID());
			AppendByteArray(subpacket, (uint16_t)act->GetAction()->size(), false);
			AppendByteArrayFast(subpacket, *act->GetAction());
		}

		// calculate crc (we only care about the first 2 bytes though)

		BYTEARRAY crc32 = CreateByteArray(CRC32(subpacket.data(), subpacket.size()), false);
		crc32.resize(2);

		// finish subpacket

		AppendByteArrayFast(packet, crc32);     // crc
		AppendByteArrayFast(packet, subpacket); // subpacket
	}

	AssignLength(packet);
	return packet;
}

/////////////////////
// OTHER FUNCTIONS //
/////////////////////

bool CGameProtocol::ValidateLength(const BYTEARRAY &content)
{
	// verify that bytes 3 and 4 (indices 2 and 3) of the content array describe the length

	return ((uint16_t)(content[3] << 8 | content[2]) == content.size());
}

BYTEARRAY CGameProtocol::EncodeSlotInfo(const std::vector<CGameSlot> &slots, uint32_t randomSeed, uint8_t layoutStyle, uint8_t playerSlots)
{
	BYTEARRAY SlotInfo;
	SlotInfo.push_back((uint8_t)slots.size()); // number of slots

	for (auto & slot : slots)
	{
		AppendByteArray(SlotInfo, slot.GetPID());
		AppendByteArray(SlotInfo, slot.GetDownloadStatus());
		AppendByteArray(SlotInfo, slot.GetSlotStatus());
		AppendByteArray(SlotInfo, slot.GetComputer());
		AppendByteArray(SlotInfo, slot.GetTeam());
		AppendByteArray(SlotInfo, slot.GetColour());
		AppendByteArray(SlotInfo, slot.GetRace());
		AppendByteArray(SlotInfo, slot.GetComputerType());
		AppendByteArray(SlotInfo, slot.GetHandicap());
	}

	AppendByteArray(SlotInfo, randomSeed, false);     // random seed
	SlotInfo.push_back(layoutStyle);                  // LayoutStyle (0 = melee, 1 = custom forces, 3 = custom forces + fixed player settings)
	SlotInfo.push_back(playerSlots);                  // number of player slots (non observer)
	return SlotInfo;
}

//
// CIncomingJoinPlayer
//

CIncomingJoinPlayer::CIncomingJoinPlayer(uint32_t nHostCounter, uint32_t nEntryKey, const std::string &nName, uint32_t nInternalIP)
	: m_Name(nName),
	m_InternalIP(nInternalIP),
	m_HostCounter(nHostCounter),
	m_EntryKey(nEntryKey)
{

}

CIncomingJoinPlayer::~CIncomingJoinPlayer()
{

}

//
// CIncomingAction
//

CIncomingAction::CIncomingAction(uint8_t nPID, const BYTEARRAY &nCRC, const BYTEARRAY &nAction)
	: m_CRC(nCRC),
	m_Action(nAction),
	m_PID(nPID)
{

}

CIncomingAction::~CIncomingAction()
{

}

//
// CIncomingChatPlayer
//

CIncomingChatPlayer::CIncomingChatPlayer(uint8_t nFromPID, const BYTEARRAY &nToPIDs, uint8_t nFlag, const std::string &nMessage)
	: m_Message(nMessage),
	m_ToPIDs(nToPIDs),
	m_Type(CTH_MESSAGE),
	m_FromPID(nFromPID),
	m_Flag(nFlag),
	m_Byte(255)
{

}

CIncomingChatPlayer::CIncomingChatPlayer(uint8_t nFromPID, const BYTEARRAY &nToPIDs, uint8_t nFlag, const std::string &nMessage, const BYTEARRAY &nExtraFlags)
	: m_Message(nMessage),
	m_ToPIDs(nToPIDs),
	m_ExtraFlags(nExtraFlags),
	m_Type(CTH_MESSAGE),
	m_FromPID(nFromPID),
	m_Flag(nFlag),
	m_Byte(255)
{

}

CIncomingChatPlayer::CIncomingChatPlayer(uint8_t nFromPID, const BYTEARRAY &nToPIDs, uint8_t nFlag, uint8_t nByte)
	: m_ToPIDs(nToPIDs),
	m_FromPID(nFromPID),
	m_Flag(nFlag),
	m_Byte(nByte)
{
	if (nFlag == 17)
		m_Type = CTH_TEAMCHANGE;
	else if (nFlag == 18)
		m_Type = CTH_COLOURCHANGE;
	else if (nFlag == 19)
		m_Type = CTH_RACECHANGE;
	else if (nFlag == 20)
		m_Type = CTH_HANDICAPCHANGE;
}

CIncomingChatPlayer::~CIncomingChatPlayer()
{

}

//
// CIncomingMapSize
//

CIncomingMapSize::CIncomingMapSize(uint8_t nSizeFlag, uint32_t nMapSize)
	: m_MapSize(nMapSize),
	m_SizeFlag(nSizeFlag)
{

}

CIncomingMapSize::~CIncomingMapSize()
{

}
