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

#include "fileutil.h"
#include <sys/stat.h>

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <string.h>
#endif

using namespace std;



#ifdef WIN32
bool utf8_to_wide(const std::string& utf8string, std::wstring& widestring)
{
	if (utf8string.empty())
	{
		widestring = L"";
		return true;
	}

	int widesize = ::MultiByteToWideChar(CP_UTF8, 0, utf8string.c_str(), -1, NULL, 0);

	// Invalid UTF-8 sequence.
	if (ERROR_NO_UNICODE_TRANSLATION == widesize ||
		0 == widesize)
	{
		widestring = L"";
		return false;
	}

	std::vector<wchar_t> resultstring(widesize);

	int convresult = ::MultiByteToWideChar(CP_UTF8, 0, utf8string.c_str(), -1, &resultstring[0], widesize);

	// Error in convert.
	if (convresult != widesize)
	{
		widestring = L"";
		return false;
	}

	widestring = std::wstring(&resultstring[0]);
	return true;
}

std::wstring utf8_to_wide(const std::string& utf8string)
{
	std::wstring widestring;
	if (utf8string.empty())
	{
		return widestring;
	}

	if (!utf8_to_wide(utf8string, widestring))
	{
		widestring = L"";
	}

	return widestring;
}
#endif

string FileRead(const string &file)
{
  ifstream IS;
#ifdef WIN32
  IS.open(utf8_to_wide(file).c_str(), ios::binary);
#else
  IS.open(file.c_str(), ios::binary);
#endif

  if (IS.fail())
  {
    Print("[UTIL] warning - unable to read file [" + file + "]");
    return string();
  }

  // get length of file

  IS.seekg(0, ios::end);
  uint32_t FileLength = IS.tellg();
  IS.seekg(0, ios::beg);

  // read data

  auto Buffer = new char[FileLength];
  IS.read(Buffer, FileLength);
  string BufferString = string(Buffer, IS.gcount());
  IS.close();
  delete[] Buffer;

  if (BufferString.size() == FileLength)
    return BufferString;
  else
    return string();
}
