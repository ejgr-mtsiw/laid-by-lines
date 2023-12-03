/*
 ============================================================================
 Name        : dataset_t.h
 Author      : Eduardo Ribeiro
 Description : Datatype representing one dataset
 ============================================================================
 */

#ifndef LINE_CLASS_T_H
#define LINE_CLASS_T_H

#include "word_t.h"

#include <stdint.h>

typedef struct line_class_t
{
	/**
	 * Number of observations of this class
	 */
	uint64_t n_observations;

	/**
	 * Adress of firts observation of this class
	 * This is the adress in the original dataset, sorted by class,
	 *  where the first obsrvation of this class is found
	 */
	word_t* first_observation_address;
} line_class_t;

#endif // LINE_CLASS_T_H
