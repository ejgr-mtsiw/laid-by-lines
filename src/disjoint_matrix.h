/*
 ============================================================================
 Name        : disjoint_matrix.h
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage the disjoint matrix
 ============================================================================
 */

#ifndef DISJOINT_MATRIX_H
#define DISJOINT_MATRIX_H

#include "types/dataset_t.h"

#include <stdint.h>

/**
 * Calculates the number of lines for the disjoint matrix
 */
uint64_t get_dm_n_lines(const dataset_t* dataset);

#endif
