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

#include "aura.h"
#include "config.h"
#include "socket.h"
#include "map.h"
#include "game.h"

#include <csignal>
#include <cstdlib>
#include <ctime>
#include <iostream>

#ifdef WIN32
#include <ws2tcpip.h>
#include <winsock.h>
#include <process.h>
#else
#include <sys/time.h>
#endif

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

#ifdef WIN32
#define MILLISLEEP( x ) Sleep( x )
#else
#define MILLISLEEP( x ) usleep( ( x ) * 1000 )
#endif

static CAura *gAura = nullptr;

uint32_t GetTicks()
{
#ifdef WIN32
	// don't use GetTickCount anymore because it's not accurate enough (~16ms resolution)
	// don't use QueryPerformanceCounter anymore because it isn't guaranteed to be strictly increasing on some systems and thus requires "smoothing" code
	// use timeGetTime instead, which typically has a high resolution (5ms or more) but we request a lower resolution on startup

	return timeGetTime();
#elif __APPLE__
	const uint64_t current = mach_absolute_time();
	static mach_timebase_info_data_t info = { 0, 0 };

	// get timebase info

	if (info.denom == 0)
		mach_timebase_info(&info);

	const uint64_t elapsednano = current * (info.numer / info.denom);

	// convert ns to ms

	return elapsednano / 1000000;
#else
	static struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
#endif
}

#include "logging.h"
void Print(const std::string &message)
{
	std::cout << message << std::endl;
	logging::logger() << message;
}

static void SignalCatcher(int32_t)
{
	Print("[!!!] caught signal SIGINT, exiting NOW");

	if (gAura)
	{
		if (gAura->m_Exiting)
			exit(1);
		else
			gAura->m_Exiting = true;
	}
	else
		exit(1);
}

//
// main
//

int main(int, char *argv[])
{
	// seed the PRNG

	srand((uint32_t)time(nullptr));

	// disable sync since we don't use cstdio anyway

	std::ios_base::sync_with_stdio(false);

	// read config file

	CConfig CFG("ydhost.cfg");

	Print("[AURA] starting up");

	signal(SIGINT, SignalCatcher);

#ifndef WIN32
	// disable SIGPIPE since some systems like OS X don't define MSG_NOSIGNAL

	signal(SIGPIPE, SIG_IGN);
#endif

#ifdef WIN32
	// initialize timer resolution
	// attempt to set the resolution as low as possible from 1ms to 5ms

	uint32_t TimerResolution = 0;

	for (uint32_t i = 1; i <= 5; ++i)
	{
		if (timeBeginPeriod(i) == TIMERR_NOERROR)
		{
			TimerResolution = i;
			break;
		}
		else if (i < 5)
			Print("[AURA] error setting Windows timer resolution to " + std::to_string(i) + " milliseconds, trying a higher resolution");
		else
		{
			Print("[AURA] error setting Windows timer resolution");
			return 1;
		}
	}

	Print("[AURA] using Windows timer with resolution " + std::to_string(TimerResolution) + " milliseconds");
#elif !defined(__APPLE__)
	// print the timer resolution

	struct timespec Resolution;

	if (clock_getres(CLOCK_MONOTONIC, &Resolution) == -1)
		Print("[AURA] error getting monotonic timer resolution");
	else
		Print("[AURA] using monotonic timer with resolution " + to_string((double)(Resolution.tv_nsec / 1000)) + " microseconds");

#endif

#ifdef WIN32
	// initialize winsock

	Print("[AURA] starting winsock");
	WSADATA wsadata;

	if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
	{
		Print("[AURA] error starting winsock");
		return 1;
	}

	// increase process priority

	Print("[AURA] setting process priority to \"high\"");
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

	// initialize aura

	gAura = new CAura(&CFG);


	// loop
	while (!gAura->Update());

	// shutdown aura

	Print("[AURA] shutting down");
	delete gAura;

#ifdef WIN32
	// shutdown winsock

	Print("[AURA] shutting down winsock");
	WSACleanup();

	// shutdown timer

	timeEndPeriod(TimerResolution);
#endif

	return 0;
}

//
// CAura
//

CAura::CAura(CConfig *CFG)
	: m_UDPSocket(new CUDPSocket()),
	m_Map(nullptr),
	m_HostCounter(1),
	m_Exiting(false)
{
	Print("[AURA] Aura++ version 1.24");

	m_UDPSocket->SetBroadcastTarget(std::string());
	m_UDPSocket->SetDontRoute(false);

	std::string MapPath = CFG->GetString("bot_mappath", std::string());
	std::string MapCFGPath = CFG->GetString("bot_mapcfgpath", std::string());
	CConfig MAP(MapCFGPath);
	m_Map = new CMap(MapPath, &MAP);

	std::string GameName = CFG->GetString("bot_defaultgamename", "");
	std::string VirtualHostName = CFG->GetString("bot_virtualhostname", "|cFF4080C0YDWE");
	if (GameName.size() > 31)
	{
		GameName = GameName.substr(0, 31);
	}

	if (VirtualHostName.size() > 15)
	{
		VirtualHostName = VirtualHostName.substr(0, 15);
	}

	if (!m_Map->GetValid())
	{
		return;
	}

	Print("[AURA] creating game [" + GameName + "]");

	CGameConfig* config = new CGameConfig;
	config->GameName = GameName;
	config->VirtualHostName = VirtualHostName;
	config->War3Version = CFG->GetInt("lan_war3version", 26);
	config->Latency = CFG->GetInt("bot_latency", 100);
	config->AutoStart = CFG->GetInt("bot_autostart", 1);
	m_Games.push_back(new CGame(m_Map, config, m_UDPSocket, m_HostCounter++));
}

CAura::~CAura()
{
	delete m_UDPSocket;

	if (m_Map)
		delete m_Map;

	for (auto & game : m_Games)
		delete game;
}

bool CAura::Update()
{
	uint32_t NumFDs = 0;

	// take every socket we own and throw it in one giant select statement so we can block on all sockets

	int32_t nfds = 0;
	fd_set fd, send_fd;
	FD_ZERO(&fd);
	FD_ZERO(&send_fd);

	// 2. all running games' player sockets

	for (auto & game : m_Games)
		NumFDs += game->SetFD(&fd, &send_fd, &nfds);

	// before we call select we need to determine how long to block for
	// 50 ms is the hard maximum
	static struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 50000;

	static struct timeval send_tv;
	send_tv.tv_sec = 0;
	send_tv.tv_usec = 0;

#ifdef WIN32
	select(1, &fd, nullptr, nullptr, &tv);
	select(1, nullptr, &send_fd, nullptr, &send_tv);
#else
	select(nfds + 1, &fd, nullptr, nullptr, &tv);
	select(nfds + 1, nullptr, &send_fd, nullptr, &send_tv);
#endif

	if (NumFDs == 0)
	{
		// we don't have any sockets (i.e. we aren't connected to battle.net and irc maybe due to a lost connection and there aren't any games running)
		// select will return immediately and we'll chew up the CPU if we let it loop so just sleep for 200ms to kill some time

		MILLISLEEP(200);
	}

	// update running games

	for (auto i = begin(m_Games); i != end(m_Games);)
	{
		if ((*i)->Update(&fd, &send_fd))
		{
			Print("[AURA] deleting game [" + (*i)->GetGameName() + "]");
			delete *i;
			i = m_Games.erase(i);
		}
		else
		{
			(*i)->UpdatePost(&send_fd);
			++i;
		}
	}

	return m_Exiting || m_Games.size() == 0;
}
