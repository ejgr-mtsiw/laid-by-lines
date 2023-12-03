/*
 ============================================================================
 Name        : utils/math.c
 Author      : Eduardo Ribeiro
 Description : Mathematical helpers
 ============================================================================
 */

#include "utils/math.h"

#include <stdint.h>

uint64_t roundUp(uint64_t numToRound, uint64_t multiple)
{
	if (multiple == 0)
	{
		return numToRound;
	}

	uint64_t remainder = numToRound % multiple;
	if (remainder == 0)
	{
		return numToRound;
	}
	return numToRound + multiple - remainder;
}
