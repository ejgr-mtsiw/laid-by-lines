/*
 ============================================================================
 Name        : dataset_hdf5.c
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage HDF5 datasets
 ============================================================================
 */

#include "dataset.h"
#include "dataset_hdf5.h"
#include "utils/math.h"

#include "types/dataset_t.h"
#include "types/oknok_t.h"
#include "types/word_t.h"

#include "hdf5.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

oknok_t hdf5_open_dataset(const char* filename, const char* datasetname,
						  dataset_hdf5_t* dataset)
{
	// Open the data file
	hid_t file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
	if (file_id < 1)
	{
		// Error creating file
		fprintf(stderr, "Error opening file %s\n", filename);
		return NOK;
	}

	// Open input dataset
	hid_t dataset_id = H5Dopen(file_id, datasetname, H5P_DEFAULT);
	if (dataset_id < 1)
	{
		// Error opening dataset
		fprintf(stderr, "Dataset %s not found!\n", datasetname);
		H5Fclose(file_id);
		return NOK;
	}

	dataset->file_id	= file_id;
	dataset->dataset_id = dataset_id;
	hdf5_get_dataset_dimensions(dataset_id, dataset->dimensions);

	return OK;
}

oknok_t hdf5_read_dataset_attributes(hid_t dataset_id, dataset_t* dataset)
{
	uint32_t n_classes = 0;
	hdf5_read_attribute(dataset_id, N_CLASSES_ATTR, H5T_NATIVE_UINT,
						&n_classes);

	if (n_classes < 2)
	{
		fprintf(stderr, "Dataset must have at least 2 classes\n");
		return NOK;
	}

	uint32_t n_observations = 0;
	// Number of observations (lines) in the dataset
	hdf5_read_attribute(dataset_id, N_OBSERVATIONS_ATTR, H5T_NATIVE_UINT,
						&n_observations);

	if (n_observations < 2)
	{
		fprintf(stderr, "Dataset must have at least 2 observations\n");
		return NOK;
	}

	uint32_t n_attributes = 0;
	// Number of attributes in the dataset
	hdf5_read_attribute(dataset_id, N_ATTRIBUTES_ATTR, H5T_NATIVE_UINT,
						&n_attributes);

	if (n_attributes < 1)
	{
		fprintf(stderr, "Dataset must have at least 1 attribute\n");
		return NOK;
	}

	// Store data
	dataset->n_attributes	  = n_attributes;
	dataset->n_bits_for_class = (uint8_t) ceil(log2(n_classes));
	dataset->n_bits_for_jnsqs = 0;
	dataset->n_classes		  = n_classes;
	dataset->n_observations	  = n_observations;

	// Bits needed for attributes and jnsq (max)
	uint64_t total_bits = dataset->n_attributes + dataset->n_bits_for_class;

	// Round up to the nearest multiple of 512
	// 512 bits = 1 cache line
	total_bits=roundUp(total_bits, 512);

	// How many words (64 bits) will be allocated
	uint64_t n_words = total_bits / WORD_BITS + (total_bits % WORD_BITS != 0);

	// Add one word for the line class
	n_words++;

	dataset->n_words=n_words;

	return OK;
}

oknok_t hdf5_read_attribute(hid_t dataset_id, const char* attribute,
							hid_t datatype, void* value)
{
	herr_t status = H5Aexists(dataset_id, attribute);
	if (status < 0)
	{
		// Error reading attribute
		fprintf(stderr, "Error reading attribute %s", attribute);
		return NOK;
	}

	if (status == 0)
	{
		// Attribute does not exist
		fprintf(stderr, "Attribute %s does not exist", attribute);
		return NOK;
	}

	// Open the attribute
	hid_t attr = H5Aopen(dataset_id, attribute, H5P_DEFAULT);
	if (attr < 0)
	{
		fprintf(stderr, "Error opening the attribute %s", attribute);
		return NOK;
	}

	// read the attribute value
	status = H5Aread(attr, datatype, value);
	if (status < 0)
	{
		fprintf(stderr, "Error reading attribute %s", attribute);
		return NOK;
	}

	// close the attribute
	status = H5Aclose(attr);
	if (status < 0)
	{
		fprintf(stderr, "Error closing the attribute %s", attribute);
		return NOK;
	}

	return OK;
}

oknok_t hdf5_read_dataset_data(hid_t dataset_id, word_t* data)
{
	// Fill dataset from hdf5 file
	herr_t status = H5Dread(dataset_id, H5T_NATIVE_ULONG, H5S_ALL, H5S_ALL,
							H5P_DEFAULT, data);

	if (status < 0)
	{
		fprintf(stderr, "Error reading the dataset data\n");

		data = NULL;
		return NOK;
	}

	return OK;
}

void hdf5_get_dataset_dimensions(hid_t dataset_id, hsize_t* dataset_dimensions)
{
	// Get filespace handle first.
	hid_t dataset_space_id = H5Dget_space(dataset_id);

	// Get dataset dimensions.
	H5Sget_simple_extent_dims(dataset_space_id, dataset_dimensions, NULL);

	// Close dataspace
	H5Sclose(dataset_space_id);
}

void hdf5_close_dataset(dataset_hdf5_t* dataset)
{
	H5Dclose(dataset->dataset_id);
	H5Fclose(dataset->file_id);
}

oknok_t hdf5_read_dataset_data_by_line(dataset_hdf5_t* hdf5_dataset, dataset_t *dataset)
{
	uint64_t n_words=dataset->n_words;
	word_t*buffer=(word_t*)malloc(n_words*sizeof(word_t));
	uint64_t line_class=0;

	uint64_t file_n_words = hdf5_dataset->dimensions[1];

	for (uint64_t l = 0;l < dataset->n_observations;l++){
		hdf5_read_line(hdf5_dataset, l, file_n_words, buffer);
		line_class=get_class(buffer, dataset->n_attributes, file_n_words, dataset->n_bits_for_class);

		memcpy(dataset->data+l*n_words, buffer, file_n_words*sizeof(word_t));
		dataset->data[(l+1)*n_words-1]=line_class;
//
//		printf("\n[%lu] lc[%lu] lc2[%lu]\n", l, line_class, dataset->data[(l+1)*n_words-1]);
//
//		for (uint64_t i=0;i<file_n_words;i++){
//			printf("[%lu] b[%lu] d[%lu]\n",i, buffer[i], dataset->data[l*n_words+i]);
//		}
	}

	free(buffer);

	return OK;
}

oknok_t hdf5_read_line(const dataset_hdf5_t* dataset, const uint32_t index,
					   const uint32_t n_words, word_t* line)
{
	return hdf5_read_lines(dataset, index, n_words, 1, line);
}

oknok_t hdf5_read_lines(const dataset_hdf5_t* dataset, const uint32_t index,
						const uint32_t n_words, const uint32_t n_lines,
						word_t* lines)
{
	// Setup offset
	hsize_t offset[2] = { index, 0 };

	// Setup count
	hsize_t count[2] = { n_lines, n_words };

	const hsize_t dimensions[2] = { n_lines, n_words };

	// Create a memory dataspace to indicate the size of our buffer/chunk
	hid_t memspace_id = H5Screate_simple(2, dimensions, NULL);

	// Setup line dataspace
	hid_t dataspace_id = H5Dget_space(dataset->dataset_id);

	// Select hyperslab on file dataset
	H5Sselect_hyperslab(dataspace_id, H5S_SELECT_SET, offset, NULL, count,
						NULL);

	// Read line from dataset
	H5Dread(dataset->dataset_id, H5T_NATIVE_UINT64, memspace_id, dataspace_id,
			H5P_DEFAULT, lines);

	H5Sclose(dataspace_id);
	H5Sclose(memspace_id);

	return OK;
}
