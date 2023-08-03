/*
 ============================================================================
 Name        : set_cover.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to apply the set cover algorithm
 ============================================================================
 */

#include "set_cover.h"

#include "types/class_offsets_t.h"
#include "types/dataset_t.h"
#include "types/dm_t.h"
#include "types/oknok_t.h"
#include "types/steps_t.h"
#include "types/word_t.h"
#include "utils/bit.h"

#include <stdint.h>

int64_t get_best_attribute_index(const uint32_t* totals,
								 const uint32_t n_attributes)
{
	uint32_t max_total	  = 0;
	int64_t max_attribute = -1;

	for (uint32_t i = 0; i < n_attributes; i++)
	{
		if (totals[i] > max_total)
		{
			max_total	  = totals[i];
			max_attribute = i;
		}
	}

	return max_attribute;
}

oknok_t generate_steps(dataset_t* dataset, dm_t* dm, steps_t* steps)
{

	uint32_t nc	   = dataset->n_classes;
	uint32_t nobs  = dataset->n_observations;
	word_t** opc   = dataset->observations_per_class;
	uint32_t* nopc = dataset->n_observations_per_class;

	// TODO: I think we can optimize the order of the lines
	// to optimize cache usage and get faster results
	// when calculating the attributes totals

	uint32_t cl		  = 0;
	uint32_t cl_start = dm->s_offset;
	uint32_t cl_end	  = dm->s_offset + dm->s_size;
	uint32_t cs		  = 0;
	uint32_t ca		  = 0;
	uint32_t ia		  = 0;
	uint32_t cb		  = 0;
	uint32_t ib		  = 0;

	// TODO: is there a better way?

	for (ca = 0; ca < nc - 1; ca++)
	{
		for (cb = ca + 1; cb < nc; cb++)
		{
			for (ia = 0; ia < nopc[ca]; ia++)
			{
				for (ib = 0; ib < nopc[cb]; ib++, cl++)
				{
					if (cl < cl_start)
					{
						// Skip to s_offset
						continue;
					}

					if (cl == cl_end)
					{
						// We have all the steps we need
						return OK;
					}

					// Generate next step
					word_t** bla = opc + ca * nobs;
					word_t** blb = opc + cb * nobs;

					steps[cs].lineA = *(bla + ia);
					steps[cs].lineB = *(blb + ib);
					cs++;
				}
			}
		}
	}

	return OK;
}

oknok_t calculate_attribute_totals(steps_t* steps, word_t* covered_lines,
								   uint32_t n_matrix_lines, uint32_t n_words,
								   uint32_t* attribute_totals)
{
	for (uint32_t c_word = 0; c_word < n_words; c_word += N_WORDS_PER_CYCLE)
	{
		// We may not have N_WORDS_PER_CYCLE words to process
		uint32_t end_word = c_word + N_WORDS_PER_CYCLE;
		if (end_word > n_words)
		{
			end_word = n_words;
		}

		for (uint32_t cl = 0; cl < n_matrix_lines; cl++)
		{
			/**
			 * Is this line covered?
			 * Yes: skip
			 * No: add
			 */
			uint32_t cl_word = cl / WORD_BITS;
			uint8_t cl_bit	 = WORD_BITS - cl % WORD_BITS - 1;

			// Is this line not covered?
			if (!BIT_CHECK(covered_lines[cl_word], cl_bit))
			{
				// This line is uncovered: calculate attributes totals

				word_t* la = steps[cl].lineA;
				word_t* lb = steps[cl].lineB;

				/**
				 * Current attribute
				 */
				uint32_t c_attribute = c_word * WORD_BITS;

				for (uint32_t n_word = c_word; n_word < end_word; n_word++)
				{
					word_t lxor = la[n_word] ^ lb[n_word];

					for (int8_t bit = WORD_BITS - 1; bit >= 0;
						 bit--, c_attribute++)
					{
						attribute_totals[c_attribute] += BIT_CHECK(lxor, bit);
					}
				}
			}
		}
	}

	return OK;
}

oknok_t update_covered_lines(steps_t* steps, word_t* covered_lines,
							 uint32_t n_lines_matrix, int64_t best_attribute)
{
	/**
	 * Which word has the best attribute
	 */
	uint32_t best_word = best_attribute / WORD_BITS;

	/**
	 * In which bit?
	 */
	uint32_t best_bit = WORD_BITS - best_attribute % WORD_BITS - 1;

	for (uint32_t cl = 0; cl < n_lines_matrix; cl++)
	{
		uint32_t cl_word = cl / WORD_BITS;
		uint8_t cl_bit	 = WORD_BITS - cl % WORD_BITS - 1;

		word_t* la = steps[cl].lineA;
		word_t* lb = steps[cl].lineB;

		word_t lxor = la[best_word] ^ lb[best_word];

		if (BIT_CHECK(lxor, best_bit))
		{
			BIT_SET(covered_lines[cl_word], cl_bit);
		}
	}

	return OK;
}

oknok_t mark_attribute_as_selected(word_t* selected_attributes,
								   int64_t attribute)
{
	uint32_t attribute_word = attribute / WORD_BITS;
	uint8_t attribute_bit	= WORD_BITS - (attribute % WORD_BITS) - 1;

	BIT_SET(selected_attributes[attribute_word], attribute_bit);

	return OK;
}
