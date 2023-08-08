/*
 ============================================================================
 Name        : disjoint_matrix_mpi.h
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix in MPIIO
 ============================================================================
 */

#ifndef MPI_DISJOINT_MATRIX_H
#define MPI_DISJOINT_MATRIX_H

#include "types/dm_t.h"
#include "types/oknok_t.h"
#include "types/steps_t.h"
#include "types/word_t.h"

#include <stdint.h>

oknok_t get_column(const dm_t* dm, const steps_t* steps, const int64_t index,
				   word_t* column);

#endif // MPI_DISJOINT_MATRIX_H
