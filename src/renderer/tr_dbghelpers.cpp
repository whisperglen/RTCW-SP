
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
#include <WinUser.h>

static std::map<std::string, int> occurences;
static std::vector<std::string> ordered_calls;

inputs_t prevstate = { 0 };
inputs_t prevstate_clr = { 0 };

#define IS_PRESSED(X) (((X) & 0x8000) != 0)

extern "C" inputs_t get_keypressed()
{
	inputs_t ret = { 0 };

	bool isShift = false;

	///
	if (IS_PRESSED(GetAsyncKeyState(VK_SHIFT)))
	{
		isShift = true;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_CONTROL)))
	{
		ret.ctrl = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_MENU)))
	{
		ret.alt = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_UP)))
	{
		prevstate.updown = 1;
		if (isShift)
			ret.updown = 1;
	}
	else if (IS_PRESSED(GetAsyncKeyState(VK_DOWN)))
	{
		prevstate.updown = -1;
		if (isShift)
			ret.updown = -1;
	}
	else if (prevstate.updown != 0)
	{
		ret.updown = prevstate.updown;
		//prevstate.updown = 0;
		prevstate_clr.updown = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_LEFT)))
	{
		prevstate.leftright = -1;
		if (isShift)
			ret.leftright = -1;
	}
	else if (IS_PRESSED(GetAsyncKeyState(VK_RIGHT)))
	{
		prevstate.leftright = 1;
		if (isShift)
			ret.leftright = 1;
	}
	else if (prevstate.leftright != 0)
	{
		ret.leftright = prevstate.leftright;
		//prevstate.leftright = 0;
		prevstate_clr.leftright = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('X')))
	{
		prevstate.x = 1;
	}
	else if (prevstate.x == 1)
	{
		ret.x = 1;
		//prevstate.x = 0;
		prevstate_clr.x = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('Y')))
	{
		prevstate.y = 1;
	}
	else if (prevstate.y == 1)
	{
		ret.y = 1;
		//prevstate.y = 0;
		prevstate_clr.y = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('Z')))
	{
		prevstate.z = 1;
	}
	else if (prevstate.z == 1)
	{
		ret.z = 1;
		//prevstate.z = 0;
		prevstate_clr.z = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('I')))
	{
		prevstate.i = 1;
	}
	else if (prevstate.i == 1)
	{
		ret.i = 1;
		//prevstate.i = 0;
		prevstate_clr.i = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('O')))
	{
		prevstate.o = 1;
	}
	else if (prevstate.o == 1)
	{
		ret.o = 1;
		//prevstate.o = 0;
		prevstate_clr.o = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('U')))
	{
		prevstate.u = 1;
	}
	else if (prevstate.u == 1)
	{
		ret.u = 1;
		//prevstate.u = 0;
		prevstate_clr.u = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState('C')))
	{
		prevstate.c = 1;
	}
	else if (prevstate.c == 1)
	{
		ret.c = 1;
		//prevstate.c = 0;
		prevstate_clr.c = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_F10)))
	{
		prevstate.f10 = 1;
	}
	else if (prevstate.f10 == 1)
	{
		ret.f10 = 1;
		//prevstate.f10 = 0;
		prevstate_clr.f10 = 1;
	}

	return ret;
}

extern "C" void keypress_frame_ended()
{
	if ( prevstate_clr.all )
	{
		prevstate.all = prevstate.all & ~prevstate_clr.all;
		prevstate_clr.all = 0;
	}
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