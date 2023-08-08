/*
 ============================================================================
 Name        : disjoint_matrix_mpi.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix
 ============================================================================
 */

#include "disjoint_matrix_mpi.h"

#include "types/dm_t.h"
#include "types/oknok_t.h"
#include "types/steps_t.h"
#include "types/word_t.h"
#include "utils/bit.h"

#include <stdint.h>
#include <string.h>

oknok_t get_column(const dm_t* dm, const steps_t* steps, const int64_t index,
				   word_t* column)
{

	// Which word has the index attribute
	uint32_t index_word = index / WORD_BITS;

	// Which bit?
	uint8_t index_bit = WORD_BITS - (index % WORD_BITS) - 1;

	// Reset best_column
	memset(column, 0, dm->n_words_in_a_column * sizeof(word_t));

	for (uint32_t cs = 0; cs < dm->s_size; cs++)
	{
		word_t* la = steps[cs].lineA;
		word_t* lb = steps[cs].lineB;

		word_t lxor = la[index_word] ^ lb[index_word];
		if (BIT_CHECK(lxor, index_bit))
		{

			uint32_t w = cs / WORD_BITS;
			uint32_t b = WORD_BITS - (cs % WORD_BITS) - 1;

			BIT_SET(column[w], b);
		}
	}

	return OK;
}
