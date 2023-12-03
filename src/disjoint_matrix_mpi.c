/*
 ============================================================================
 Name        : disjoint_matrix_mpi.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix
 ============================================================================
 */

#include "disjoint_matrix_mpi.h"
#include "types/dataset_t.h"
#include "types/dm_t.h"
#include "types/line_class_t.h"
#include "types/oknok_t.h"
#include "types/word_t.h"
#include "utils/bit.h"

#include <stdint.h>
#include <string.h>

oknok_t get_column(const dataset_t* dataset, const dm_t* dm,
				   const int64_t attribute, word_t* column)
{

	// Which word has the index attribute
	uint64_t attribute_word = attribute / WORD_BITS;

	// Which bit?
	uint8_t attribute_bit = WORD_BITS - (attribute % WORD_BITS) - 1;

	// Reset best_column
	memset(column, 0, dm->n_words_in_a_column * sizeof(word_t));

	uint64_t nw = dataset->n_words;
	uint64_t nc	   = dataset->n_classes;
	//uint64_t nobs  = dataset->n_observations;
	line_class_t *classes = dataset->classes;

	uint64_t ca = dm->initial_class_offsets.classA;
	uint64_t ia = dm->initial_class_offsets.indexA;
	uint64_t cb = dm->initial_class_offsets.classB;
	uint64_t ib = dm->initial_class_offsets.indexB;

	uint64_t cl = 0;

	while (ca < nc - 1)
	{
		while (ia < classes[ca].n_observations)
		{
			while (cb < nc)
			{
				while (ib < classes[cb].n_observations)
				{
					if (cl == dm->s_size)
					{
						return OK;
					}

					word_t* la = classes[ca].first_observation_address + ia*nw;
					word_t* lb = classes[cb].first_observation_address + ib*nw;

					word_t lxor = la[attribute_word] ^ lb[attribute_word];
					if (BIT_CHECK(lxor, attribute_bit))
					{
						uint64_t w = cl / WORD_BITS;
						uint8_t b  = WORD_BITS - (cl % WORD_BITS) - 1;

						BIT_SET(column[w], b);
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

oknok_t calculate_class_offsets(const dataset_t* dataset, const uint64_t line,
								class_offsets_t* class_offsets)
{

	/**
	 * Number of classes in dataset
	 */
	uint64_t nc = dataset->n_classes;

	/**
	 * Class info
	 */
	line_class_t *classes=dataset->classes;

	/**
	 * Calculate the conditions for the first element of the disjoint matrix
	 * for this process. We always process the matrix in sequence, so this
	 * data is enough.
	 */
	if (nc == 2)
	{
		// For 2 classes the calculation is direct
		// This process will start working from here
		class_offsets->classA = 0;
		class_offsets->classB = 1;

		class_offsets->indexA = line / classes[class_offsets->classB].n_observations;
		class_offsets->indexB = line % classes[class_offsets->classB].n_observations;
	}
	else
	{
		uint64_t cl = 0;
		uint64_t ca = 0;
		uint64_t ia = 0;
		uint64_t cb = 0;
		uint64_t ib = 0;

		// I still haven't found a better way for more than 2 classes...
		// TODO: is there a better way?
		for (ca = 0; ca < nc - 1; ca++)
		{
			for (ia = 0; ia < classes[ca].n_observations; ia++)
			{
				for (cb = ca + 1; cb < nc; cb++)
				{
					for (ib = 0; ib < classes[cb].n_observations; ib++, cl++)
					{
						if (cl == line)
						{
							// This process will start working from here
							class_offsets->classA = ca;
							class_offsets->indexA = ia;
							class_offsets->classB = cb;
							class_offsets->indexB = ib;

							return OK;
						}
					}
				}
			}
		}
	}

	return NOK;
}
