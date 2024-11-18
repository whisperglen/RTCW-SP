
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <stdio.h>
#include <string.h>
extern "C" {
#include "..\game\q_shared.h"
#include "..\qcommon\qcommon.h"
}
#include <Windows.h>
#include <Winuser.h>

static std::map<std::string, int> occurences;
static std::vector<std::string> ordered_calls;

inputs_t prevstate = { 0 };

#define IS_PRESSED(X) (((X) & 0x8000) != 0)

extern "C" inputs_t get_keypressed()
{
	inputs_t ret = { 0 };

	if (IS_PRESSED(GetAsyncKeyState('X')))
	{
		prevstate.x = 1;
	}
	else if (prevstate.x == 1)
	{
		ret.x = 1;
		prevstate.x = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('Y')))
	{
		prevstate.y = 1;
	}
	else if (prevstate.y == 1)
	{
		ret.y = 1;
		prevstate.y = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('Z')))
	{
		prevstate.z = 1;
	}
	else if (prevstate.z == 1)
	{
		ret.z = 1;
		prevstate.z = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_CONTROL)))
	{
		prevstate.ctrl = 1;
	}
	else if (prevstate.ctrl == 1)
	{
		ret.ctrl = 1;
		prevstate.ctrl = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_UP)))
	{
		prevstate.updown = 1;
	}
	else if (IS_PRESSED(GetAsyncKeyState(VK_DOWN)))
	{
		prevstate.updown = -1;
	}
	else if (prevstate.updown != 0)
	{
		ret.updown = prevstate.updown;
		prevstate.updown = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_LEFT)))
	{
		prevstate.leftright = -1;
	}
	else if (IS_PRESSED(GetAsyncKeyState(VK_RIGHT)))
	{
		prevstate.leftright = 1;
	}
	else if (prevstate.leftright != 0)
	{
		ret.leftright = prevstate.leftright;
		prevstate.leftright = 0;
	}

	return ret;
}

extern "C" void function_called(const char *name)
{
	return;
	std::string _name(name);
	int count = 1;
	if (occurences.find(_name) != occurences.end())
	{
		count += occurences[_name];
	}
	else
	{
		ordered_calls.push_back(_name);
	}

	occurences[_name] = count;
}

extern "C" void functions_dump()
{
	char out[1024];
	std::ofstream file("called_functions.txt");

	if (file.is_open())
	{
		//auto it = occurences.begin();
		//for (; it != occurences.end(); it++)
		//{
		//	file.write(it->first.c_str(), it->first.size());
		//	file.write("\n", strlen("\n"));
		//}
		auto it = ordered_calls.begin();
		for (; it != ordered_calls.end(); it++)
		{
			snprintf(out, sizeof(out), "%s -> %d\n", it->c_str(), occurences.at(*it));
			file.write(out, strnlen(out, sizeof(out)));
		}

		file.close();
	}
}