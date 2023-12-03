/*
 ============================================================================
 Name        : disjoint_matrix.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix
 ============================================================================
 */

#include "disjoint_matrix.h"

#include "types/dataset_t.h"
#include "types/line_class_t.h"

#include <stdint.h>

uint64_t get_dm_n_lines(const dataset_t* dataset)
{
	// Calculate number of lines for the matrix
	uint64_t n = 0;

	uint64_t n_classes	  = dataset->n_classes;
	line_class_t*classes = dataset->classes;

	for (uint64_t class_a = 0; class_a < n_classes - 1; class_a++)
	{
		for (uint64_t class_b = class_a + 1; class_b < n_classes; class_b++)
		{
			n += (uint64_t)classes[class_a].n_observations * (uint64_t)classes[class_b].n_observations;
		}
	}

	return n;
}
