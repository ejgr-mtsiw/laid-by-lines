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
#include <string.h>

int64_t get_best_attribute_index(const uint64_t* totals,
								 const uint64_t n_attributes)
{
	uint64_t max_total	  = 0;
	int64_t max_attribute = -1;

	for (uint64_t i = 0; i < n_attributes; i++)
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

	uint64_t nc	   = dataset->n_classes;
	uint64_t nobs  = dataset->n_observations;
	word_t** opc   = dataset->observations_per_class;
	uint64_t* nopc = dataset->n_observations_per_class;

	// TODO: I think we can optimize the order of the lines
	// to optimize cache usage and get faster results
	// when calculating the attributes totals

	uint64_t cl		  = 0;
	uint64_t cl_start = dm->s_offset;
	uint64_t cl_end	  = dm->s_offset + dm->s_size;
	uint64_t cs		  = 0;
	uint64_t ca		  = 0;
	uint64_t ia		  = 0;
	uint64_t cb		  = 0;
	uint64_t ib		  = 0;

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

oknok_t calculate_initial_attribute_totals(const dataset_t* dataset,
										   const dm_t* dm, const steps_t* steps,
										   uint64_t* attribute_totals)
{
	// Reset attributes totals
	// memset(attribute_totals, 0, dataset->n_attributes * sizeof(uint64_t));

	for (uint64_t c_word = 0; c_word < dataset->n_words;
		 c_word += N_WORDS_PER_CYCLE)
	{
		// We may not have N_WORDS_PER_CYCLE words to process
		uint64_t end_word = c_word + N_WORDS_PER_CYCLE;
		if (end_word > dataset->n_words)
		{
			end_word = dataset->n_words;
		}

		for (uint64_t cl = 0; cl < dm->s_size; cl++)
		{
			word_t* la = steps[cl].lineA;
			word_t* lb = steps[cl].lineB;

			// Current attribute
			uint64_t c_attribute = c_word * WORD_BITS;

			for (uint64_t n_word = c_word; n_word < end_word; n_word++)
			{
				word_t lxor = la[n_word] ^ lb[n_word];

				for (int8_t bit = WORD_BITS - 1; bit >= 0; bit--, c_attribute++)
				{
					attribute_totals[c_attribute] += BIT_CHECK(lxor, bit);
				}
			}
		}
	}

	return OK;
}

oknok_t calculate_attribute_totals_add(const dataset_t* dataset, const dm_t* dm,
									   const steps_t* steps,
									   const word_t* covered_lines,
									   uint64_t* attribute_totals)
{
	// Reset attributes totals
	memset(attribute_totals, 0, dataset->n_attributes * sizeof(uint64_t));

	for (uint64_t c_word = 0; c_word < dataset->n_words;
		 c_word += N_WORDS_PER_CYCLE)
	{
		// We may not have N_WORDS_PER_CYCLE words to process
		uint64_t end_word = c_word + N_WORDS_PER_CYCLE;
		if (end_word > dataset->n_words)
		{
			end_word = dataset->n_words;
		}

		for (uint64_t cl = 0; cl < dm->s_size; cl++)
		{
			/**
			 * Is this line covered?
			 * Yes: skip
			 * No: add
			 */
			uint64_t cl_word = cl / WORD_BITS;
			uint8_t cl_bit	 = WORD_BITS - (cl % WORD_BITS) - 1;

			// Is this line not covered?
			if (!BIT_CHECK(covered_lines[cl_word], cl_bit))
			{
				// This line is uncovered: calculate attributes totals

				word_t* la = steps[cl].lineA;
				word_t* lb = steps[cl].lineB;

				// Current attribute
				uint64_t c_attribute = c_word * WORD_BITS;

				for (uint64_t n_word = c_word; n_word < end_word; n_word++)
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

oknok_t calculate_attribute_totals_sub(const dataset_t* dataset, const dm_t* dm,
									   const steps_t* steps,
									   const word_t* covered_lines,
									   uint64_t* attribute_totals)
{

	for (uint64_t c_word = 0; c_word < dataset->n_words;
		 c_word += N_WORDS_PER_CYCLE)
	{
		// We may not have N_WORDS_PER_CYCLE words to process
		uint64_t end_word = c_word + N_WORDS_PER_CYCLE;
		if (end_word > dataset->n_words)
		{
			end_word = dataset->n_words;
		}

		for (uint64_t cl = 0; cl < dm->s_size; cl++)
		{
			/**
			 * Is this line covered?
			 * Yes: skip
			 * No: add
			 */
			uint64_t cl_word = cl / WORD_BITS;
			uint8_t cl_bit	 = WORD_BITS - (cl % WORD_BITS) - 1;

			// Is this line covered?
			if (BIT_CHECK(covered_lines[cl_word], cl_bit))
			{
				// This line is now covered: calculate attributes totals

				word_t* la = steps[cl].lineA;
				word_t* lb = steps[cl].lineB;

				// Current attribute
				uint64_t c_attribute = c_word * WORD_BITS;

				for (uint64_t n_word = c_word; n_word < end_word; n_word++)
				{
					word_t lxor = la[n_word] ^ lb[n_word];

					for (int8_t bit = WORD_BITS - 1; bit >= 0;
						 bit--, c_attribute++)
					{
						attribute_totals[c_attribute] -= BIT_CHECK(lxor, bit);
					}
				}
			}
		}
	}

	return OK;
}

oknok_t update_covered_lines(const word_t* best_column,
							 const uint64_t n_words_in_a_column,
							 word_t* covered_lines)
{
	for (uint64_t w = 0; w < n_words_in_a_column; w++)
	{
		covered_lines[w] |= best_column[w];
	}

	return OK;
}

oknok_t mark_attribute_as_selected(word_t* selected_attributes,
								   int64_t attribute)
{
	uint64_t attribute_word = attribute / WORD_BITS;
	uint8_t attribute_bit	= WORD_BITS - (attribute % WORD_BITS) - 1;

	BIT_SET(selected_attributes[attribute_word], attribute_bit);

	return OK;
}
