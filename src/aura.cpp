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
#include "crc32.h"
#include "sha1.h"
#include "config.h"
#include "socket.h"
#include "map.h"
#include "gameplayer.h"
#include "gameprotocol.h"
#include "game.h"
#include "util.h"
#include "fileutil.h"

#include <csignal>
#include <cstdlib>
#include <ctime>

#define __STORMLIB_SELF__
#include <StormLib.h>

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

#define VERSION "1.24"

using namespace std;

static CAura *gAura = nullptr;
bool gRestart = false;

uint32_t GetTime()
{
#ifdef WIN32
  // don't use GetTickCount anymore because it's not accurate enough (~16ms resolution)
  // don't use QueryPerformanceCounter anymore because it isn't guaranteed to be strictly increasing on some systems and thus requires "smoothing" code
  // use timeGetTime instead, which typically has a high resolution (5ms or more) but we request a lower resolution on startup

  return timeGetTime() / 1000;
#elif __APPLE__
  const uint64_t current = mach_absolute_time();
  static mach_timebase_info_data_t info = { 0, 0 };

  // get timebase info

  if (info.denom == 0)
    mach_timebase_info(&info);

  const uint64_t elapsednano = current * (info.numer / info.denom);

  // convert ns to s

  return elapsednano / 1000000000;
#else
  static struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec;
#endif
}

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

#include "logging.h"
void Print(const string &message)
{
	cout << message << endl;
	logging::logger() << message;
}

//
// main
//

int main(int, char *argv[])
{
  // seed the PRNG

  srand((uint32_t) time(nullptr));

  // disable sync since we don't use cstdio anyway

  ios_base::sync_with_stdio(false);

  // read config file

  CConfig CFG;
  CFG.Read("ydhost.cfg");

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
      Print("[AURA] error setting Windows timer resolution to " + to_string(i) + " milliseconds, trying a higher resolution");
    else
    {
      Print("[AURA] error setting Windows timer resolution");
      return 1;
    }
  }

  Print("[AURA] using Windows timer with resolution " + to_string(TimerResolution) + " milliseconds");
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

  if (!gAura->m_Map)
  {
	  Print("[AURA] could not found the map");
	  return 1;
  }

  gAura->CreateGame(gAura->m_Map, CFG.GetString("bot_defaultgamename", ""));

  // check if it's properly configured

  if (gAura->GetReady())
  {
    // loop

    while (!gAura->Update());
  }
  else
    Print("[AURA] check your aura.cfg and configure Aura properly");

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

  // restart the program

  if (gRestart)
  {
#ifdef WIN32
    _spawnl(_P_OVERLAY, argv[0], argv[0], nullptr);
#else
    execl(argv[0], argv[0], nullptr);
#endif
  }

  return 0;
}

//
// CAura
//

CAura::CAura(CConfig *CFG)
  : m_UDPSocket(new CUDPSocket()),
    m_CRC(new CCRC32()),
    m_SHA(new CSHA1()),
    m_CurrentGame(nullptr),
    m_Map(nullptr),
    m_Version(VERSION),
    m_HostCounter(1),
    m_Exiting(false),
    m_Enabled(true),
    m_Ready(true)
{
  Print("[AURA] Aura++ version " + m_Version);

  // get the general configuration variables

  m_UDPSocket->SetBroadcastTarget(CFG->GetString("udp_broadcasttarget", string()));
  m_UDPSocket->SetDontRoute(CFG->GetInt("udp_dontroute", 0) == 0 ? false : true);

  m_CRC->Initialize();
  m_HostPort = CFG->GetInt("bot_hostport", 0);
  m_LANWar3Version = CFG->GetInt("lan_war3version", 26);

  // read the rest of the general configuration

  SetConfigs(CFG);


  m_Map = new CMap(this, CFG, "");

}

CAura::~CAura()
{
  delete m_UDPSocket;
  delete m_CRC;
  delete m_SHA;

  if (m_Map)
    delete m_Map;

  delete m_CurrentGame;

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

  // 1. the current game's server and player sockets

  if (m_CurrentGame)
    NumFDs += m_CurrentGame->SetFD(&fd, &send_fd, &nfds);

  // 2. all running games' player sockets

  for (auto & game : m_Games)
    NumFDs += game->SetFD(&fd, &send_fd, &nfds);

  // before we call select we need to determine how long to block for
  // 50 ms is the hard maximum

  unsigned long usecBlock = 50000;

  for (auto & game : m_Games)
  {
    if (game->GetNextTimedActionTicks() * 1000 < usecBlock)
      usecBlock = game->GetNextTimedActionTicks() * 1000;
  }

  static struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = usecBlock;

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

  bool Exit = false;

  // update running games

  for (auto i = begin(m_Games); i != end(m_Games);)
  {
    if ((*i)->Update(&fd, &send_fd))
    {
      Print("[AURA] deleting game [" + (*i)->GetGameName() + "]");
      EventGameDeleted(*i);
      delete *i;
      i = m_Games.erase(i);
    }
    else
    {
      (*i)->UpdatePost(&send_fd);
      ++i;
    }
  }

  // update current game

  if (m_CurrentGame)
  {
    if (m_CurrentGame->Update(&fd, &send_fd))
    {
      Print("[AURA] deleting current game [" + m_CurrentGame->GetGameName() + "]");
      delete m_CurrentGame;
      m_CurrentGame = nullptr;
    }
    else if (m_CurrentGame)
      m_CurrentGame->UpdatePost(&send_fd);
  }

  return m_Exiting || Exit;
}

void CAura::EventGameDeleted(CGame *game)
{
	ExitProcess(-1);
}

void CAura::ReloadConfigs()
{
  CConfig CFG;
  CFG.Read("aura.cfg");
  SetConfigs(&CFG);
}

void CAura::SetConfigs(CConfig *CFG)
{
  // this doesn't set EVERY config value since that would potentially require reconfiguring the battle.net connections
  // it just set the easily reloadable values

  m_BindAddress = CFG->GetString("bot_bindaddress", string());
  m_MaxGames = CFG->GetInt("bot_maxgames", 20);
  string BotCommandTrigger = CFG->GetString("bot_commandtrigger", "!");
  m_CommandTrigger = BotCommandTrigger[0];

  m_MapCFGPath = AddPathSeparator(CFG->GetString("bot_mapcfgpath", string()));
  m_MapPath = AddPathSeparator(CFG->GetString("bot_mappath", string()));
  m_VirtualHostName = CFG->GetString("bot_virtualhostname", "|cFF4080C0YDWE");

  if (m_VirtualHostName.size() > 15)
  {
    m_VirtualHostName = "|cFF4080C0YDWE";
    Print("[AURA] warning - bot_virtualhostname is longer than 15 characters, using default virtual host name");
  }

  m_AllowDownloads = CFG->GetInt("bot_allowdownloads", 0);
  m_MaxDownloaders = CFG->GetInt("bot_maxdownloaders", 3);
  m_MaxDownloadSpeed = CFG->GetInt("bot_maxdownloadspeed", 100);
  m_LCPings = CFG->GetInt("bot_lcpings", 1) == 0 ? false : true;
  m_AutoKickPing = CFG->GetInt("bot_autokickping", 300);
  m_Latency = CFG->GetInt("bot_latency", 100);
  m_SyncLimit = CFG->GetInt("bot_synclimit", 50);
  m_AutoStart = CFG->GetInt("bot_autostart", 1);
}

void CAura::CreateGame(CMap *map, string gameName)
{
  if (!m_Enabled)
  {
    return;
  }

  if (gameName.size() > 31)
  {
    return;
  }

  if (!map->GetValid())
  {
    return;
  }

  if (m_CurrentGame)
  {
    return;
  }

  if (m_Games.size() >= m_MaxGames)
  {
    return;
  }

  Print("[AURA] creating game [" + gameName + "]");

  m_CurrentGame = new CGame(this, map, m_HostPort, gameName);
}
