
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

typedef char helper_string_t[MAX_HELPER_STRINGSIZE];
static helper_string_t helper_strings[1] = { 0 };

static inputs_t keystate = { 0 };
static inputs_t pressed = { 0 };
static bool new_frame = true;

#define IS_PRESSED(X) (((X) & 0x8000) != 0)

#define HANDLE_KEY(KEY, var) \
	if (IS_PRESSED(GetAsyncKeyState(KEY))) \
	{ \
		if ( keystate.var == 0 ) \
		{ \
			pressed.var = 1; \
			keystate.var = 1; \
		} \
	} \
	else \
	{ \
		keystate.var = 0; \
	}

extern "C" inputs_t get_keypressed()
{
	if ( new_frame )
	{
		new_frame = false;

		bool isShift = false;
		///
		if ( IS_PRESSED( GetAsyncKeyState( VK_SHIFT ) ) )
		{
			isShift = true;
		}
		///
		if ( IS_PRESSED( GetAsyncKeyState( VK_CONTROL ) ) )
		{
			pressed.ctrl = 1;
		}
		/// ALT
		if ( IS_PRESSED( GetAsyncKeyState( VK_MENU ) ) )
		{
			pressed.alt = 1;
		}
		///
		if ( IS_PRESSED( GetAsyncKeyState( VK_UP ) ) )
		{
			if ( keystate.updown == 0 || isShift )
			{
				pressed.updown = 1;
				keystate.updown = 1;
			}
		}
		else if ( IS_PRESSED( GetAsyncKeyState( VK_DOWN ) ) )
		{
			if ( keystate.updown == 0 || isShift )
			{
				pressed.updown = -1;
				keystate.updown = -1;
			}
		}
		else
		{
			keystate.updown = 0;
		}
		///
		if ( IS_PRESSED( GetAsyncKeyState( VK_LEFT ) ) )
		{
			if ( keystate.leftright == 0 || isShift )
			{
				pressed.leftright = -1;
				keystate.leftright = -1;
			}
		}
		else if ( IS_PRESSED( GetAsyncKeyState( VK_RIGHT ) ) )
		{
			if ( keystate.leftright == 0 || isShift )
			{
				pressed.leftright = 1;
				keystate.leftright = 1;
			}
		}
		else
		{
			keystate.leftright = 0;
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
	}

	return pressed;
}

extern "C" void keypress_frame_ended()
{
	new_frame = true;
	pressed.all = 0;
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

extern "C" const char* helper_value_str( unsigned int index )
{
	static char dummyval[1] = "";
	const char* ret = dummyval;
	if ( index < ARRAYSIZE(helper_strings) )
	{
		ret = helper_strings[index];
	}
	return ret;
}

extern "C" void helper_value_str_store( unsigned int index, const char *str )
{
	if ( index < ARRAYSIZE(helper_strings) )
	{
		strncpy_s( helper_strings[index], str, MAX_HELPER_STRINGSIZE );
	}
}

const size_t bitmask_alloc_ex = 255;
inline size_t bitmask_alloc_calc_sz(size_t in) { return ((in + bitmask_alloc_ex) & ~bitmask_alloc_ex); }

extern "C" void bitmask_alloc( bitmask_t* bm, size_t numbits )
{
	if (bm)
	{
		size_t numbytes = numbits / 8;
		void* p = malloc(numbytes);
		if (p)
		{
			bm->ptr = (uint32_t*)p;
			bm->size = numbytes / sizeof(uint32_t);
			bitmask_clear(bm);
		}
	}
}

extern "C" void bitmask_realloc(bitmask_t* bm, size_t numbits)
{
	if (bm)
	{
		if (bm->ptr)
		{
			size_t numslots = numbits / 32;
			if (numslots > bm->size)
			{
				void* p = realloc(bm->ptr, numbits / 8);
				if (p)
				{
					memset((uint32_t*)p + bm->size, 0, (numslots - bm->size) * sizeof(uint32_t));
					bm->ptr = (uint32_t*)p;
					bm->size = numslots;
				}
			}
		}
		else
		{
			bitmask_alloc(bm, numbits);
		}
	}
}

extern "C" void bitmask_free(bitmask_t* bm)
{
	if (bm)
	{
		if (bm->ptr)
		{
			free(bm->ptr);
		}
		bm->ptr = NULL;
		bm->size = 0;
	}
}

extern "C" void bitmask_clear(bitmask_t* bm)
{
	if (bm && bm->ptr)
	{
		memset(bm->ptr, 0, bm->size * sizeof(uint32_t));
	}
}

extern "C" void bitmask_set( bitmask_t* bm, uint32_t bitnum)
{
	if (bm)
	{
		uint32_t slot = bitnum >> 5;
		uint32_t mask = 1 << (bitnum & 0x1F);

		if(slot >= bm->size)
			bitmask_realloc(bm, bitmask_alloc_calc_sz(bitnum + 1) );

		if (slot < bm->size)
		{
			bm->ptr[slot] |= mask;
		}
	}
}

extern "C" uint32_t bitmask_is_set( bitmask_t* bm, uint32_t bitnum)
{
	uint32_t ret = 0;
	if (bm)
	{
		uint32_t slot = bitnum >> 5;
		uint32_t mask = 1 << (bitnum & 0x1F);

		if (slot < bm->size)
		{
			ret = (0 != (bm->ptr[slot] & mask));
		}
	}

	return ret;
}