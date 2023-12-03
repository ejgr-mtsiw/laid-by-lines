/*
 ============================================================================
 Name        : dataset_t.h
 Author      : Eduardo Ribeiro
 Description : Datatype representing one dataset
 ============================================================================
 */

#ifndef DATASET_T_H
#define DATASET_T_H

#include "word_t.h"
#include "line_class_t.h"

#include <stdint.h>

typedef struct dataset_t
{
	/**
	 * Number of attributes
	 */
	uint64_t n_attributes;

	/**
	 * Number of words needed to store a line
	 */
	uint64_t n_words;

	/**
	 * Number of bits needed to store jnsqs (max 32)
	 */
	uint8_t n_bits_for_jnsqs;

	/**
	 * Number of observations
	 */
	uint64_t n_observations;

	/**
	 * Number of classes
	 */
	uint64_t n_classes;

	/**
	 * Number of bits used to store the class (max 32)
	 */
	uint8_t n_bits_for_class;

	/**
	 * Dataset data
	 */
	word_t* data;

	/**
	 * Array with class info
	 */
	line_class_t* classes;
} dataset_t;

#endif // DATASET_T_H
