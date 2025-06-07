
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
#include "tr_debughelpers.h"
#include <Windows.h>
#include <WinUser.h>


static std::map<std::string, int> occurences;
static std::vector<std::string> ordered_calls;

static int helper_values[MAX_HELPER_VALUES] = { 0 };
byte helper_values_initialised[MAX_HELPER_VALUES] = { 0 };

inputs_t prevstate = { 0 };
inputs_t prevstate_upd = { 0 };

#define IS_PRESSED(X) (((X) & 0x8000) != 0)

#define HANDLE_KEY(KEY, var) \
	if (IS_PRESSED(GetAsyncKeyState(KEY))) \
	{ \
		if ( prevstate.var == 0 ) \
		{ \
			ret.var = 1; \
			prevstate_upd.var = 1; \
		} \
	} \
	else \
	{ \
		prevstate.var = 0; \
	}

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
	/// ALT
	if (IS_PRESSED(GetAsyncKeyState(VK_MENU)))
	{
		ret.alt = 1;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_UP)))
	{
		if ( prevstate.updown == 0 || isShift )
		{
			ret.updown = 1;
			prevstate_upd.updown = 1;
		}
	}
	else if (IS_PRESSED(GetAsyncKeyState(VK_DOWN)))
	{
		if ( prevstate.updown == 0 || isShift )
		{
			ret.updown = -1;
			prevstate_upd.updown = 1;
		}
	}
	else
	{
		prevstate.updown = 0;
	}
	///
	if (IS_PRESSED(GetAsyncKeyState(VK_LEFT)))
	{
		if ( prevstate.leftright == 0 || isShift )
		{
			ret.leftright = -1;
			prevstate_upd.leftright = 1;
		}
	}
	else if (IS_PRESSED(GetAsyncKeyState(VK_RIGHT)))
	{
		if ( prevstate.leftright == 0 || isShift )
		{
			ret.leftright = 1;
			prevstate_upd.leftright = 1;
		}
	}
	else
	{
		prevstate.leftright = 0;
	}
	///
	HANDLE_KEY( 'X', x );
	///
	HANDLE_KEY( 'Y', y );
	///
	HANDLE_KEY( 'Z', z );
	///
	HANDLE_KEY( 'I', i );
	///
	HANDLE_KEY( 'O', o );
	///
	HANDLE_KEY( 'U', u );
	///
	HANDLE_KEY( 'C', c );
	///
	HANDLE_KEY( VK_F8, imgui );

	return ret;
}

extern "C" void keypress_frame_ended()
{
	if ( prevstate_upd.all )
	{
		prevstate.all = prevstate.all | prevstate_upd.all;
		prevstate_upd.all = 0;
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

extern "C" int* helper_value( unsigned int index )
{
	static int dummyval = 0x1000;
	int* ret = &dummyval;
	if ( index < MAX_HELPER_VALUES )
	{
		ret = &helper_values[index];
	}
	return ret;
}

void bitmask_set( uint32_t bitnum, uint32_t* storage, uint32_t storagesz )
{
	uint32_t slot = bitnum >> 5;
	uint32_t mask = 1 << (bitnum & 0x1F);

	if ( slot < storagesz )
	{
		storage[slot] |= mask;
	}
}

uint32_t bitmask_is_set( uint32_t bitnum, uint32_t* storage, uint32_t storagesz )
{
	uint32_t slot = bitnum >> 5;
	uint32_t mask = 1 << (bitnum & 0x1F);

	if ( slot < storagesz )
	{
		return (0 != (storage[slot] & mask));
	}

	return 0;
}