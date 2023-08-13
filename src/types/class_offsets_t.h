/*
 * class_offsets_t.h
 *
 * Created on: 29/07/2023
 * Author: Eduardo Ribeiro
 * Description: Datatype representing the initial classes
 * and offsets for a process to generate the disjoint matrix
 */

#ifndef TYPES_CLASS_OFFSETS_T_H_
#define TYPES_CLASS_OFFSETS_T_H_

#include <stdint.h>

/**
 * Classes and corresponding offsets for the first step in the
 * disjoint matrix
 */
typedef struct class_offsets_t
{
	uint64_t classA;
	uint64_t indexA;
	uint64_t classB;
	uint64_t indexB;
} class_offsets_t;

#endif /* TYPES_CLASS_OFFSETS_T_H_ */
