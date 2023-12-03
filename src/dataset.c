/*
 ============================================================================
 Name        : dataset.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage datasets
 ============================================================================
 */

#include "dataset.h"

#include "types/dataset_t.h"
#include "types/oknok_t.h"
#include "types/word_t.h"
#include "utils/bit.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_dataset(dataset_t* dataset)
{
	dataset->data					  = NULL;
	dataset->n_attributes			  = 0;
	dataset->n_bits_for_class		  = 0;
	dataset->n_bits_for_jnsqs		  = 0;
	dataset->n_classes				  = 0;
	dataset->n_observations			  = 0;
	dataset->n_words				  = 0;
}

uint64_t get_class(const word_t* line, const uint64_t n_attributes,
				   const uint64_t n_words, const uint8_t n_bits_for_class)
{
	// How many attributes remain on last word with attributes
	uint8_t remaining = n_attributes % WORD_BITS;

	if (n_bits_for_class == 1 || (remaining + n_bits_for_class <= WORD_BITS))
	{
		// All class bits are on the same word

		// Class starts here
		uint8_t at = (uint8_t) (WORD_BITS - remaining - n_bits_for_class);

		return (uint64_t) get_bits(line[n_words - 1], at, n_bits_for_class);
	}

	// Class bits are split between 2 words

	// Number of class bits on penultimate word
	uint8_t n_bits_p = WORD_BITS - remaining;

	// Number of class bits on last word
	uint8_t n_bits_l = n_bits_for_class - n_bits_p;

	// Class bits from penultimate word
	uint64_t high_b = (uint64_t) get_bits(line[n_words - 2], 0, n_bits_p);

	// Class bits from last word
	uint64_t low_b = (uint64_t) get_bits(line[n_words - 1],
										 WORD_BITS - n_bits_l, n_bits_l);

	// Merge bits
	high_b <<= n_bits_l;
	high_b |= low_b;

	// Return class
	return high_b;
}

int compare_lines_extra(const void* a, const void* b, void* n_words)
{
	const word_t* ula = (const word_t*) a;
	const word_t* ulb = (const word_t*) b;
	word_t va		  = 0;
	word_t vb		  = 0;

	uint64_t n_l = *(uint64_t*) n_words;

	for (uint64_t i = 0; i < n_l; i++)
	{
		va = ula[i];
		vb = ulb[i];

		if (va > vb)
		{
			return 1;
		}

		if (va < vb)
		{
			return -1;
		}
	}

	return 0;
}

int compare_lines_by_class(const void* a, const void* b, void* n_words)
{
	uint64_t nw = *(uint64_t*) n_words;
	const word_t* ula = (const word_t*) a;
	const word_t* ulb = (const word_t*) b;
	word_t va		  = ula[nw-1];
	word_t vb		  = ulb[nw-1];

		if (va > vb)
		{
			return 1;
		}

		if (va < vb)
		{
			return -1;
		}

	return 0;
}

bool has_same_attributes(const word_t* line_a, const word_t* line_b,
						 const uint64_t n_attributes)
{
	// How many full words are used for attributes?
	uint64_t n_words = (uint64_t) (n_attributes / WORD_BITS);

	// How many attributes remain on last word
	uint8_t remaining = n_attributes % WORD_BITS;

	// Current word
	uint64_t c_word = 0;

	// Check full words
	for (c_word = 0; c_word < n_words; c_word++)
	{
		if (line_a[c_word] != line_b[c_word])
		{
			return false;
		}
	}

	if (remaining == 0)
	{
		// Attributes only use full words. Nothing more to check
		return true;
	}

	// We need to check last word
	word_t last_word = get_bits((line_a[c_word] ^ line_b[c_word]),
								WORD_BITS - remaining, remaining);

	return (last_word == 0);
}

uint64_t remove_duplicates(dataset_t* dataset)
{
	word_t* line = dataset->data;
	word_t* last = line;

	uint64_t n_words   = dataset->n_words;
	uint64_t n_obs	   = dataset->n_observations;
	uint64_t n_uniques = 1;

	for (uint64_t i = 0; i < n_obs - 1; i++)
	{
		NEXT_LINE(line, n_words);
		if (compare_lines_extra(line, last, &n_words) != 0)
		{
			NEXT_LINE(last, n_words);
			n_uniques++;
			if (last != line)
			{
				memcpy(last, line, sizeof(word_t) * n_words);
			}
		}
	}

	// Update number of observations, so the code ignores the remaining lines
	dataset->n_observations = n_uniques;
	return (n_obs - n_uniques);
}

oknok_t fill_class_arrays(dataset_t* dataset)
{
	// Number of longs in a line
	uint64_t n_words = dataset->n_words;

	// Number of observations
	uint64_t n_obs = dataset->n_observations;

	// Array that stores class info
	line_class_t* classes = dataset->classes;

	// Current line
	word_t* line = dataset->data;

	// This is the current index
	for (uint64_t i = 0; i < n_obs; i++)
	{
		uint64_t lc = line[n_words-1];

		if (classes[lc].n_observations==0) {
			// First observation
			classes[lc].first_observation_address=line;
		}

		classes[lc].n_observations++;

		NEXT_LINE(line, n_words);
	}

	return OK;
}

void free_dataset(dataset_t* dataset)
{
	free(dataset->data);
	free(dataset->classes);

	dataset->data					  = NULL;
	dataset->classes				  = NULL;
}

void print_dataset(dataset_t * dataset, uint64_t n_attributes, uint64_t n_observations){

	return;

	for (uint64_t l=0;l<n_observations;l++){
				printf("[%lu] ",l);
				print_line(dataset->data+l*dataset->n_words, dataset->n_words, n_attributes);
			}
}

void print_line(word_t*line,uint64_t n_words, uint64_t n_attributes){
	uint64_t na=0;
	for (uint64_t i=0;i<n_words && na<n_attributes;i++){
		word_t w = line[i];
		for (int8_t bit=WORD_BITS-1;bit>=0 && na < n_attributes;bit--,na++){
			if (BIT_CHECK(w, bit)) {
			printf("1");
			} else {
				printf("0");
			}
		}
		printf(" ");
	}
	printf(" [%lu]", line[n_words-1]);
	printf("\n");
}
