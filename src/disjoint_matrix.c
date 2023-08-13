/*
 ============================================================================
 Name        : disjoint_matrix.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix
 ============================================================================
 */

#include "disjoint_matrix.h"

#include "types/dataset_t.h"

#include <stdint.h>

uint64_t get_dm_n_lines(const dataset_t* dataset)
{
	uint64_t n = 0;

	uint64_t n_classes	  = dataset->n_classes;
	uint64_t* n_class_obs = dataset->n_observations_per_class;

	for (uint64_t i = 0; i < n_classes - 1; i++)
	{
		for (uint64_t j = i + 1; j < n_classes; j++)
		{
			n += (uint64_t) n_class_obs[i] * (uint64_t) n_class_obs[j];
		}
	}

	return n;
}
