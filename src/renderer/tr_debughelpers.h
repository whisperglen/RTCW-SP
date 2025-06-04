
#ifndef _TR_DEBUGHELPERS_H
#define _TR_DEBUGHELPERS_H
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "../game/q_shared.h"

void function_called(const char *name);
void functions_dump();

typedef union inputs
{
	struct {
		int updown : 2;
		int leftright : 2;
		unsigned int ctrl : 1;
		unsigned int alt : 1;
		unsigned int x : 1;
		unsigned int y : 1;
		unsigned int z : 1;
		unsigned int i : 1;
		unsigned int o : 1;
		unsigned int u : 1;
		unsigned int c : 1;
		unsigned int imgui : 1;
	};
	unsigned int all;
} inputs_t;

inputs_t get_keypressed();
void keypress_frame_ended();

#define MAX_HELPER_VALUES 10
extern byte helper_values_initialised[];

int* helper_value( unsigned int index );

inline void helper_value_initial_value( unsigned int index, int value )
{
	if ( index < MAX_HELPER_VALUES && helper_values_initialised[index] == 0 )
	{
		helper_values_initialised[index] = 1;
		*helper_value( index ) = value;
	}
}

inline int helper_value_equals( unsigned int index, int check )
{
	return (check == *helper_value(index));
}

inline int helper_value_clamp( unsigned int index, int lower, int upper )
{
	int val = *helper_value( index );
	if ( val < lower )
		val = lower;
	if ( val > upper )
		val = upper;
	return val;
}

#ifdef __cplusplus
}
#endif