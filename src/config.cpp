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

#include "config.h"

#include <fstream>
#include <algorithm>
#include <string>

void Print(const std::string &message);

//
// CConfig
//

CConfig::CConfig()
{

}

CConfig::~CConfig()
{

}

void CConfig::Read(const std::string &file)
{
	std::ifstream in;
	in.open(file.c_str());

	if (in.fail())
		Print("[CONFIG] warning - unable to read file [" + file + "]");
	else
	{
		Print("[CONFIG] loading file [" + file + "]");
		std::string Line;

		while (!in.eof())
		{
			std::getline(in, Line);

			// ignore blank lines and comments

			if (Line.empty() || Line[0] == '#' || Line == "\n")
				continue;

			// remove newlines and partial newlines to help fix issues with Windows formatted config files on Linux systems

			Line.erase(std::remove(std::begin(Line), std::end(Line), '\r'), std::end(Line));
			Line.erase(std::remove(std::begin(Line), std::end(Line), '\n'), std::end(Line));

			std::string::size_type Split = Line.find("=");

			if (Split == std::string::npos)
				continue;

			std::string::size_type KeyStart = Line.find_first_not_of(" ");
			std::string::size_type KeyEnd = Line.find(" ", KeyStart);
			std::string::size_type ValueStart = Line.find_first_not_of(" ", Split + 1);
			std::string::size_type ValueEnd = Line.size();

			if (ValueStart != std::string::npos)
				m_CFG[Line.substr(KeyStart, KeyEnd - KeyStart)] = Line.substr(ValueStart, ValueEnd - ValueStart);
		}

		in.close();
	}
}

bool CConfig::Exists(const std::string &key)
{
	return m_CFG.find(key) != std::end(m_CFG);
}

int32_t CConfig::GetInt(const std::string &key, int32_t x)
{
	if (m_CFG.find(key) == std::end(m_CFG))
		return x;
	else
		return atoi(m_CFG[key].c_str());
}

std::string CConfig::GetString(const std::string &key, const std::string &x)
{
	if (m_CFG.find(key) == std::end(m_CFG))
		return x;
	else
		return m_CFG[key];
}

void CConfig::Set(const std::string &key, const std::string &x)
{
	m_CFG[key] = x;
}
