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

#ifndef AURA_GAMEPLAYER_H_
#define AURA_GAMEPLAYER_H_

#include "socket.h"
#include <queue>

class CTCPSocket;
class CGameProtocol;
class CGame;
class CIncomingJoinPlayer;

//
// CPotentialPlayer
//

class CPotentialPlayer
{
public:
	CGameProtocol *m_Protocol;
	CGame *m_Game;

protected:
	// note: we permit m_Socket to be NULL in this class to allow for the virtual host player which doesn't really exist
	// it also allows us to convert CPotentialPlayers to CGamePlayers without the CPotentialPlayer's destructor closing the socket

	CTCPSocket *m_Socket;
	CIncomingJoinPlayer *m_IncomingJoinPlayer;
	bool m_DeleteMe;

public:
	CPotentialPlayer(CGameProtocol *nProtocol, CGame *nGame, CTCPSocket *nSocket);
	~CPotentialPlayer();

	inline CTCPSocket *GetSocket() const                         { return m_Socket; }
	inline uint32_t GetExternalIP() const                        { return m_Socket->GetIP(); }
	inline std::string GetExternalIPString() const               { return m_Socket->GetIPString(); }
	inline bool GetDeleteMe() const                              { return m_DeleteMe; }
	inline CIncomingJoinPlayer *GetJoinPlayer() const            { return m_IncomingJoinPlayer; }

	inline void SetSocket(CTCPSocket *nSocket)                   { m_Socket = nSocket; }
	inline void SetDeleteMe(bool nDeleteMe)                      { m_DeleteMe = nDeleteMe; }

	// processing functions

	bool Update(void *fd);

	// other functions

	void Send(const BYTEARRAY &data) const;
};

//
// CGamePlayer
//

class CGamePlayer
{
public:
	CGameProtocol *m_Protocol;
	CGame *m_Game;

protected:
	CTCPSocket *m_Socket;                     // note: we permit m_Socket to be NULL in this class to allow for the virtual host player which doesn't really exist

private:
	uint32_t m_InternalIP;                    // the player's internal IP address as reported by the player when connecting
	std::queue<uint32_t> m_CheckSums;         // the last few checksums the player has sent (for detecting desyncs)
	std::string m_Name;                       // the player's name
	uint32_t m_LeftCode;                      // the code to be sent in W3GS_PLAYERLEAVE_OTHERS for why this player left the game
	uint32_t m_SyncCounter;                   // the number of keepalive packets received from this player
	uint32_t m_LastMapPartSent;               // the last mappart sent to the player (for sending more than one part at a time)
	uint32_t m_LastMapPartAcked;              // the last mappart acknowledged by the player
	uint32_t m_StartedLaggingTicks;           // GetTicks when the player started laggin
	uint8_t m_PID;                            // the player's PID
	bool m_DownloadStarted;                   // if we've started downloading the map or not
	bool m_DownloadFinished;                  // if we've finished downloading the map or not
	bool m_FinishedLoading;                   // if the player has finished loading or not
	bool m_Lagging;                           // if the player is lagging or not (on the lag screen)
	bool m_DropVote;                          // if the player voted to drop the laggers or not (on the lag screen)

protected:
	bool m_DeleteMe;

public:
	CGamePlayer(CPotentialPlayer *potential, uint8_t nPID, const std::string &nName, uint32_t nInternalIP);
	~CGamePlayer();

	inline CTCPSocket *GetSocket() const                                { return m_Socket; }
	inline uint32_t GetExternalIP() const                               { return m_Socket->GetIP(); }
	inline std::string GetExternalIPString() const                      { return m_Socket->GetIPString(); }
	inline bool GetDeleteMe() const                                     { return m_DeleteMe; }
	inline uint8_t GetPID() const                                       { return m_PID; }
	inline std::string GetName() const                                  { return m_Name; }
	inline uint32_t GetInternalIP() const                               { return m_InternalIP; }
	inline std::queue<uint32_t> *GetCheckSums()                         { return &m_CheckSums; }
	inline uint32_t GetLeftCode() const                                 { return m_LeftCode; }
	inline uint32_t GetSyncCounter() const                              { return m_SyncCounter; }
	inline uint32_t GetLastMapPartSent() const                          { return m_LastMapPartSent; }
	inline uint32_t GetLastMapPartAcked() const                         { return m_LastMapPartAcked; }
	inline uint32_t GetStartedLaggingTicks() const                      { return m_StartedLaggingTicks; }
	inline bool GetDownloadStarted() const                              { return m_DownloadStarted; }
	inline bool GetDownloadFinished() const                             { return m_DownloadFinished; }
	inline bool GetFinishedLoading() const                              { return m_FinishedLoading; }
	inline bool GetLagging() const                                      { return m_Lagging; }
	inline bool GetDropVote() const                                     { return m_DropVote; }

	inline void SetSocket(CTCPSocket *nSocket)                                           { m_Socket = nSocket; }
	inline void SetDeleteMe(bool nDeleteMe)                                              { m_DeleteMe = nDeleteMe; }
	inline void SetLeftCode(uint32_t nLeftCode)                                          { m_LeftCode = nLeftCode; }
	inline void SetSyncCounter(uint32_t nSyncCounter)                                    { m_SyncCounter = nSyncCounter; }
	inline void SetLastMapPartSent(uint32_t nLastMapPartSent)                            { m_LastMapPartSent = nLastMapPartSent; }
	inline void SetLastMapPartAcked(uint32_t nLastMapPartAcked)                          { m_LastMapPartAcked = nLastMapPartAcked; }
	inline void SetStartedLaggingTicks(uint32_t nStartedLaggingTicks)                    { m_StartedLaggingTicks = nStartedLaggingTicks; }
	inline void SetDownloadStarted(bool nDownloadStarted)                                { m_DownloadStarted = nDownloadStarted; }
	inline void SetDownloadFinished(bool nDownloadFinished)                              { m_DownloadFinished = nDownloadFinished; }
	inline void SetLagging(bool nLagging)                                                { m_Lagging = nLagging; }
	inline void SetDropVote(bool nDropVote)                                              { m_DropVote = nDropVote; }

	// processing functions

	bool Update(uint32_t Ticks, void *fd);

	// other functions

	void Send(const BYTEARRAY &data);
};

#endif  // AURA_GAMEPLAYER_H_
