/*
 ============================================================================
 Name        : set_cover.h
 Author      : Eduardo Ribeiro
 Description : Structures and functions to apply the set cover algorithm
 ============================================================================
 */

#ifndef SET_COVER_H
#define SET_COVER_H

#include "types/class_offsets_t.h"
#include "types/dataset_t.h"
#include "types/dm_t.h"
#include "types/oknok_t.h"
#include "types/word_t.h"

#include <stdint.h>

/**
 * Number of words to process per disjoint matrix cycle
 */
#define N_WORDS_PER_CYCLE 8

/**
 * Searches the attribute totals array for the highest score and returns the
 * correspondent attribute index.
 * Returns -1 if there are no more attributes available.
 */
int64_t get_best_attribute_index(const uint64_t* totals,
								 const uint64_t n_attributes);

/**
 * Calculates the current attributes totals
 */
oknok_t calculate_initial_attribute_totals(const dataset_t* dataset,
										   const dm_t* dm, uint64_t* totals);

oknok_t calculate_attribute_totals_add(const dataset_t* dataset, const dm_t* dm,
									   const word_t* covered_lines,
									   uint64_t* totals);
oknok_t calculate_attribute_totals_sub(const dataset_t* dataset, const dm_t* dm,
									   const word_t* covered_lines,
									   uint64_t* totals);

/**
 * Sets this attribute as selected
 */
oknok_t mark_attribute_as_selected(word_t* attributes, int64_t attribute);

/**
 * Updates the list of covered lines, adding the lines covered by the best
 * attribute
 */
oknok_t update_covered_lines(const word_t* best_column,
							 const uint64_t n_words_in_a_column,
							 word_t* covered_lines);

#endif
