/*
 ============================================================================
 Name        : disjoint_matrix_mpi.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix
 ============================================================================
 */

#include "disjoint_matrix_mpi.h"

#include "types/class_offsets_t.h"
#include "types/dataset_t.h"
#include "types/oknok_t.h"

#include <stdint.h>

oknok_t calculate_class_offsets(const dataset_t* dataset, const uint32_t line,
								  class_offsets_t* class_offsets)
{

	/**
	 * Number of classes in dataset
	 */
	uint32_t nc = dataset->n_classes;

	/**
	 * Number of observations per class in dataset
	 */
	uint32_t* nopc = dataset->n_observations_per_class;

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
		class_offsets->indexA = line / nopc[1];
		class_offsets->classB = 1;
		class_offsets->indexB = line % nopc[1];
	}
	else
	{
		uint32_t cl = 0;
		uint32_t ca = 0;
		uint32_t ia = 0;
		uint32_t cb = 0;
		uint32_t ib = 0;

		// I still haven't found a better way for more than 2 classes...
		// TODO: is there a better way?
		for (ca = 0; ca < nc - 1; ca++)
		{
			for (ia = 0; ia < nopc[ca]; ia++)
			{
				for (cb = ca + 1; cb < nc; cb++)
				{
					for (ib = 0; ib < nopc[cb]; ib++, cl++)
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

	return OK;
}
