/*
 ============================================================================
 Name        : disjoint_matrix_mpi.h
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix in MPIIO
 ============================================================================
 */

#ifndef MPI_DISJOINT_MATRIX_H
#define MPI_DISJOINT_MATRIX_H

#include "types/dataset_t.h"
#include "types/dm_t.h"
#include "types/oknok_t.h"
#include "types/word_t.h"

#include <stdint.h>

oknok_t get_column(const dataset_t* dataset, const dm_t* dm,
				   const int64_t attribute, word_t* column);

/**
 * Calculates the class offsets that correspond to the requested
 * line of the disjoint matrix
 */
oknok_t calculate_class_offsets(const dataset_t* dataset, const uint64_t line,
								class_offsets_t* class_offsets);

#endif // MPI_DISJOINT_MATRIX_H
