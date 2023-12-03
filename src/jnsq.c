/*
 ============================================================================
 Name        : jnsq.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage JNSQ
 ============================================================================
 */

#include "jnsq.h"

#include "dataset.h"
#include "types/dataset_t.h"
#include "types/word_t.h"
#include "utils/bit.h"

#include <math.h>
#include <stdint.h>

void set_jnsq_bits(word_t* line, uint64_t inconsistency,
				   const uint64_t n_attributes, const uint64_t n_words,
				   const uint8_t n_bits_for_class)
{

	// Words that have attributes
	uint64_t true_n_words =  n_attributes/ WORD_BITS + (n_attributes% WORD_BITS != 0);

	// n jnsq bits needed
	uint8_t n_bits_needed = n_bits_for_class+0*n_words;

	// How many attributes remain free on last word with attributes
	uint8_t attributes_last_word = n_attributes % WORD_BITS;

	uint8_t free_bits_last_word = 0;

	// The first word with jnsq
	uint64_t word_with_jnsq = 0;

	if (attributes_last_word==0){
		word_with_jnsq=true_n_words;
		free_bits_last_word=WORD_BITS;
	} else {
		word_with_jnsq=true_n_words-1;
		free_bits_last_word=WORD_BITS-attributes_last_word;
	}

	if (n_bits_needed > free_bits_last_word) {
		// We must split the jnsq attributes

		// n jnsq bits on first word
		uint8_t n_bits = free_bits_last_word;

		// Invert consistency
		if (n_bits>1){
			inconsistency = invert_n_bits((word_t) inconsistency, n_bits);
		}

		line[word_with_jnsq]=set_bits(line[word_with_jnsq], inconsistency, 0, n_bits);

		// Remove used bits from inconsistency
		inconsistency >>= n_bits;

		// n jnsq bits on next word
		n_bits_needed -= n_bits;

		// next word_with_jnsq
		word_with_jnsq++;

		// There's no more attributes
		free_bits_last_word=WORD_BITS;
	}

	// All remaining jnsq bits are in the same word
	uint8_t jnsq_start = free_bits_last_word-n_bits_needed;

	// Invert consistency, if needed
	if (n_bits_needed>1){
		inconsistency = invert_n_bits((word_t) inconsistency, n_bits_needed);
	}

	line[word_with_jnsq] = set_bits(line[word_with_jnsq], inconsistency, jnsq_start, n_bits_needed);
}

/**
 * Adds the JNSQs attributes to the dataset
 */
uint64_t add_jnsqs(dataset_t* dataset)
{
	// Current line
	word_t* current = dataset->data;

	// Previous line
	word_t* prev = current;

	// Number of attributes
	uint64_t n_attributes = dataset->n_attributes;

	// Number of longs in a line
	uint64_t n_words = dataset->n_words;

	// Number of observations in the dataset
	uint64_t n_observations = dataset->n_observations;

	// Number of bits needed to store class
	uint8_t n_bits_for_class = dataset->n_bits_for_class;

	// Last line
	word_t* last = GET_LAST_LINE(dataset->data, n_observations, n_words);

	// Inconsistency
	uint64_t inconsistency = 0;

	// Max inconsistency found
	uint64_t max_inconsistency = 0;

	// first line has jnsq=0
	set_jnsq_bits(current, 0, n_attributes, n_words, n_bits_for_class);

	// Now do the others
	for (prev = dataset->data; prev < last; NEXT_LINE(prev, n_words))
	{
		NEXT_LINE(current, n_words);

		if (has_same_attributes(current, prev, n_attributes))
		{
			// Inconsistency!
			// Because observations with the same attributes end up sorted by class
			inconsistency++;

			// Update max
			if (inconsistency > max_inconsistency)
			{
				max_inconsistency = inconsistency;
			}
		}
		else
		{
			// Differente attributes - reset JNSQ
			inconsistency = 0;
		}

		// Set the line JNSQ
		set_jnsq_bits(current, inconsistency, n_attributes, n_words, n_bits_for_class);
	}

	return max_inconsistency;
}
