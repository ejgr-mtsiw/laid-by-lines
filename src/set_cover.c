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

oknok_t calculate_initial_attribute_totals(const dataset_t* dataset,
										   const dm_t* dm, uint64_t* totals)
{
	// Reset attributes totals
	memset(totals, 0, dataset->n_attributes * sizeof(uint64_t));

	uint64_t nc	   = dataset->n_classes;
	uint64_t nobs  = dataset->n_observations;
	word_t** opc   = dataset->observations_per_class;
	uint64_t* nopc = dataset->n_observations_per_class;

	for (uint64_t cw = 0; cw < dataset->n_words; cw += N_WORDS_PER_CYCLE)
	{

		uint64_t ew = cw + N_WORDS_PER_CYCLE;
		if (ew > dataset->n_words)
		{
			ew = dataset->n_words;
		}

		uint64_t ca = dm->initial_class_offsets.classA;
		uint64_t ia = dm->initial_class_offsets.indexA;
		uint64_t cb = dm->initial_class_offsets.classB;
		uint64_t ib = dm->initial_class_offsets.indexB;

		uint64_t cl = 0;

		while (ca < nc - 1 && cl < dm->s_size)
		{
			while (ia < nopc[ca] && cl < dm->s_size)
			{
				while (cb < nc && cl < dm->s_size)
				{
					while (ib < nopc[cb] && cl < dm->s_size)
					{
						// Add attributes totals

						word_t** bla = opc + ca * nobs;
						word_t* la	 = *(bla + ia);
						word_t** blb = opc + cb * nobs;
						word_t* lb	 = *(blb + ib);

						/**
						 * Current attribute
						 */
						uint64_t catt = cw * WORD_BITS;

						// Process words
						for (uint64_t ccw = cw; ccw < ew; ccw++)
						{
							word_t lxor = la[ccw] ^ lb[ccw];

							for (int8_t bit = WORD_BITS - 1; bit >= 0;
								 bit--, catt++)
							{
								totals[catt] += BIT_CHECK(lxor, bit);
							}
						}

						ib++;
						cl++;
					}
					cb++;
					ib = 0;
				}
				ia++;
				cb = ca + 1;
				ib = 0;
			}
			ca++;
			ia = 0;
			cb = ca + 1;
			ib = 0;
		}
	}

	return OK;
}

oknok_t calculate_attribute_totals_add(const dataset_t* dataset, const dm_t* dm,
									   const word_t* covered_lines,
									   uint64_t* totals)
{
	// Reset attributes totals
	memset(totals, 0, dataset->n_attributes * sizeof(uint64_t));

	uint64_t nc	   = dataset->n_classes;
	uint64_t nobs  = dataset->n_observations;
	word_t** opc   = dataset->observations_per_class;
	uint64_t* nopc = dataset->n_observations_per_class;

	for (uint64_t cw = 0; cw < dataset->n_words; cw += N_WORDS_PER_CYCLE)
	{

		uint64_t ew = cw + N_WORDS_PER_CYCLE;
		if (ew > dataset->n_words)
		{
			ew = dataset->n_words;
		}

		uint64_t ca = dm->initial_class_offsets.classA;
		uint64_t ia = dm->initial_class_offsets.indexA;
		uint64_t cb = dm->initial_class_offsets.classB;
		uint64_t ib = dm->initial_class_offsets.indexB;

		uint64_t cl = 0;

		while (ca < nc - 1 && cl < dm->s_size)
		{
			while (ia < nopc[ca] && cl < dm->s_size)
			{
				while (cb < nc && cl < dm->s_size)
				{
					while (ib < nopc[cb] && cl < dm->s_size)
					{
						/**
						 * Is this line covered?
						 * Yes: skip
						 * No: add
						 */
						uint64_t cl_word = cl / WORD_BITS;
						uint8_t cl_bit	 = WORD_BITS - cl % WORD_BITS - 1;

						// Is this line not covered?
						if (!BIT_CHECK(covered_lines[cl_word], cl_bit))
						{
							// This line is uncovered: calculate attributes
							// totals

							word_t** bla = opc + ca * nobs;
							word_t* la	 = *(bla + ia);
							word_t** blb = opc + cb * nobs;
							word_t* lb	 = *(blb + ib);

							/**
							 * Current attribute
							 */
							uint64_t catt = cw * WORD_BITS;

							// Process words
							for (uint64_t ccw = cw; ccw < ew; ccw++)
							{
								word_t lxor = la[ccw] ^ lb[ccw];

								for (int8_t bit = WORD_BITS - 1; bit >= 0;
									 bit--, catt++)
								{
									totals[catt] += BIT_CHECK(lxor, bit);
								}
							}
						}

						ib++;
						cl++;
					}
					cb++;
					ib = 0;
				}
				ia++;
				cb = ca + 1;
				ib = 0;
			}
			ca++;
			ia = 0;
			cb = ca + 1;
			ib = 0;
		}
	}

	return OK;
}
oknok_t calculate_attribute_totals_sub(const dataset_t* dataset, const dm_t* dm,
									   const word_t* covered_lines,
									   uint64_t* totals)
{
	uint64_t nc	   = dataset->n_classes;
	uint64_t nobs  = dataset->n_observations;
	word_t** opc   = dataset->observations_per_class;
	uint64_t* nopc = dataset->n_observations_per_class;

	for (uint64_t cw = 0; cw < dataset->n_words; cw += N_WORDS_PER_CYCLE)
	{

		uint64_t ew = cw + N_WORDS_PER_CYCLE;
		if (ew > dataset->n_words)
		{
			ew = dataset->n_words;
		}

		uint64_t ca = dm->initial_class_offsets.classA;
		uint64_t ia = dm->initial_class_offsets.indexA;
		uint64_t cb = dm->initial_class_offsets.classB;
		uint64_t ib = dm->initial_class_offsets.indexB;

		uint64_t cl = 0;

		while (ca < nc - 1 && cl < dm->s_size)
		{
			while (ia < nopc[ca] && cl < dm->s_size)
			{
				while (cb < nc && cl < dm->s_size)
				{
					while (ib < nopc[cb] && cl < dm->s_size)
					{
						/**
						 * Is this line covered?
						 * Yes: sub
						 * No: skip
						 */
						uint64_t clw = cl / WORD_BITS;
						uint8_t clb	 = WORD_BITS - cl % WORD_BITS - 1;

						// Is this line not covered?
						if (BIT_CHECK(covered_lines[clw], clb))
						{
							// This line is covered: calculate attributes totals

							word_t** bla = opc + ca * nobs;
							word_t* la	 = *(bla + ia);
							word_t** blb = opc + cb * nobs;
							word_t* lb	 = *(blb + ib);

							/**
							 * Current attribute
							 */
							uint64_t catt = cw * WORD_BITS;

							// Process words
							for (uint64_t ccw = cw; ccw < ew; ccw++)
							{
								word_t lxor = la[ccw] ^ lb[ccw];

								for (int8_t bit = WORD_BITS - 1; bit >= 0;
									 bit--, catt++)
								{
									totals[catt] -= BIT_CHECK(lxor, bit);
								}
							}
						}

						ib++;
						cl++;
					}
					cb++;
					ib = 0;
				}
				ia++;
				cb = ca + 1;
				ib = 0;
			}
			ca++;
			ia = 0;
			cb = ca + 1;
			ib = 0;
		}
	}

	return OK;
}

oknok_t mark_attribute_as_selected(word_t* attributes, int64_t attribute)
{
	uint64_t attribute_word = attribute / WORD_BITS;
	uint8_t attribute_bit	= WORD_BITS - (attribute % WORD_BITS) - 1;

	BIT_SET(attributes[attribute_word], attribute_bit);

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
