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
#include "types/oknok_t.h"
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

oknok_t calculate_attribute_totals(dataset_t* dataset,
								   class_offsets_t* class_offsets,
								   word_t* covered_lines,
								   uint32_t n_matrix_lines,
								   uint32_t* attribute_totals)
{

	/**
	 * Number of words with WORD_BITS attributes
	 */
	uint32_t n_full_words = dataset->n_attributes / WORD_BITS;

	/**
	 * Last bit to process in the last word
	 */
	uint8_t last_bit_last_word
		= WORD_BITS - (dataset->n_attributes % WORD_BITS);

	uint32_t nc	   = dataset->n_classes;
	uint32_t nobs  = dataset->n_observations;
	word_t** opc   = dataset->observations_per_class;
	uint32_t* nopc = dataset->n_observations_per_class;

	uint32_t ca = class_offsets->classA;
	uint32_t ia = class_offsets->indexA;
	uint32_t cb = class_offsets->classB;
	uint32_t ib = class_offsets->indexB;

	uint32_t cl = 0;

	while (ca < nc - 1)
	{
		word_t** bla = opc + ca * nobs;

		while (ia < nopc[ca])
		{
			word_t* la = *(bla + ia);

			while (cb < nc)
			{
				word_t** blb = opc + cb * nobs;

				while (ib < nopc[cb])
				{
					if (cl == n_matrix_lines)
					{
						return OK;
					}

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

						// This line is uncovered: calculate attributes
						// totals

						word_t* lb = *(blb + ib);

						/**
						 * Current attribute
						 */
						uint32_t c_attribute = 0;

						/**
						 * Current word
						 */
						uint32_t c_word = 0;

						// Process full words
						for (c_word = 0; c_word < n_full_words; c_word++)
						{
							word_t lxor = la[c_word] ^ lb[c_word];

							for (int8_t bit = WORD_BITS - 1; bit >= 0;
								 bit--, c_attribute++)
							{
								attribute_totals[c_attribute]
									+= BIT_CHECK(lxor, bit);
							}
						}

						// Process last word
						word_t lxor = la[c_word] ^ lb[c_word];

						for (int8_t bit = WORD_BITS - 1;
							 bit >= last_bit_last_word; bit--, c_attribute++)
						{
							attribute_totals[c_attribute]
								+= BIT_CHECK(lxor, bit);
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

	return OK;
}

oknok_t update_covered_lines(dataset_t* dataset, class_offsets_t* class_offsets,
							 word_t* covered_lines, uint32_t n_lines_matrix,
							 int64_t best_attribute)
{
	/**
	 * Which word has the best attribute
	 */
	uint32_t best_word = best_attribute / WORD_BITS;

	/**
	 * In which bit?
	 */
	uint32_t best_bit = WORD_BITS - best_attribute % WORD_BITS - 1;

	uint32_t nc	   = dataset->n_classes;
	uint32_t nobs  = dataset->n_observations;
	word_t** opc   = dataset->observations_per_class;
	uint32_t* nopc = dataset->n_observations_per_class;

	uint32_t ca = class_offsets->classA;
	uint32_t ia = class_offsets->indexA;
	uint32_t cb = class_offsets->classB;
	uint32_t ib = class_offsets->indexB;

	uint32_t cl = 0;

	while (ca < nc - 1)
	{
		word_t** bla = opc + ca * nobs;

		while (ia < nopc[ca])
		{
			word_t* la = *(bla + ia);

			while (cb < nc)
			{
				word_t** blb = opc + cb * nobs;

				while (ib < nopc[cb])
				{
					if (cl == n_lines_matrix)
					{
						return OK;
					}

					uint32_t cl_word = cl / WORD_BITS;
					uint8_t cl_bit	 = WORD_BITS - cl % WORD_BITS - 1;

					word_t* lb = *(blb + ib);

					word_t lxor = la[best_word] ^ lb[best_word];

					if (BIT_CHECK(lxor, best_bit))
					{
						BIT_SET(covered_lines[cl_word], cl_bit);
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
