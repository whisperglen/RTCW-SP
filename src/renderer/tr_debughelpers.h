
#ifndef _TR_DEBUGHELPERS_H
#define _TR_DEBUGHELPERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../game/q_shared.h"
#include <stdint.h>

void function_called(const char *name);
void functions_dump();

union inputs_u
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
};
typedef union inputs_u inputs_t;

inputs_t get_keypressed();
void keypress_frame_ended();

#define MAX_HELPER_VALUES 10
#define MAX_HELPER_STRINGSIZE 1024

extern byte helper_values_initialised[];

int* helper_value( unsigned int index );

const char* helper_value_str( unsigned int index );
void helper_value_str_store( unsigned int index, const char* str );

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

inline int helper_value_less( unsigned int index, int check )
{
	return (check > *helper_value(index));
}

inline int helper_value_greater( unsigned int index, int check )
{
	return (check < *helper_value(index));
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

struct bitmask_s
{
	uint32_t* ptr;
	size_t size;
};
typedef struct bitmask_s bitmask_t;

void bitmask_alloc( bitmask_t* bm, size_t numbits );
void bitmask_realloc(bitmask_t* bm, size_t numbits);
void bitmask_free( bitmask_t* bm );
void bitmask_set( bitmask_t* bm, uint32_t bitnum );
uint32_t bitmask_is_set( bitmask_t* bm, uint32_t bitnum );
void bitmask_clear(bitmask_t* bm);

#ifdef __cplusplus
struct bitmaskx_s : public bitmask_t
{
	bitmaskx_s() { ptr = NULL; size = 0; }
	~bitmaskx_s() { bitmask_free(this); }
	void alloc(size_t numbits) { bitmask_alloc(this, numbits); }
	void realloc(size_t numbits) { bitmask_realloc(this, numbits); }
	void free() { bitmask_free(this); }
	void set(uint32_t bitnum) { bitmask_set(this, bitnum); }
	uint32_t is_set(uint32_t bitnum) { return bitmask_is_set(this, bitnum); }
	void clear() { bitmask_clear(this); }
};
typedef struct bitmaskx_s bitmaskx_t;
#endif

#ifdef __cplusplus
}
#endif
#endif