/*
 ============================================================================
 Name        : disjoint_matrix_mpi.h
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix in MPIIO
 ============================================================================
 */

#ifndef MPI_DISJOINT_MATRIX_H
#define MPI_DISJOINT_MATRIX_H

#include "types/class_offsets_t.h"
#include "types/dataset_t.h"
#include "types/oknok_t.h"

#include <stdint.h>

/**
 * Calculates the class offsets that correspond to the requested
 * line of the disjoint matrix
 */
oknok_t calculate_class_offsets(const dataset_t* dataset, const uint32_t line,
								  class_offsets_t* class_offsets);

#endif // MPI_DISJOINT_MATRIX_H
