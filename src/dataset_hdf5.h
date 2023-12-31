/*
 ============================================================================
 Name        : dataset_hdf5.h
 Author      : Eduardo Ribeiro
 Description : Structures and functions to manage hdf5 datasets
 ============================================================================
 */

#ifndef HDF5_DATASET_H
#define HDF5_DATASET_H

#include "types/dataset_hdf5_t.h"
#include "types/dataset_t.h"
#include "types/oknok_t.h"

#include "hdf5.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * Attribute for number of classes
 */
#define N_CLASSES_ATTR "n_classes"

/**
 * Attribute for number of attributes
 */
#define N_ATTRIBUTES_ATTR "n_attributes"

/**
 * Attribute for number of observations
 */
#define N_OBSERVATIONS_ATTR "n_observations"

/**
 * Checks if dataset is present in file_id
 */
bool hdf5_dataset_exists(const hid_t file_id, const char* dataset);

/**
 * Checks if dataset is present from filename
 */
bool hdf5_file_has_dataset(const char* filename, const char* datasetname);

/**
 * Opens the file and dataset indicated
 */
oknok_t hdf5_open_dataset(const char* filename, const char* datasetname,
						  dataset_hdf5_t* dataset);

/**
 * Reads the dataset attributes from the hdf5 file
 */
oknok_t hdf5_read_dataset_attributes(hid_t dataset_id, dataset_t* dataset);

/**
 * Reads the value of one attribute from the dataset
 */
oknok_t hdf5_read_attribute(hid_t dataset_id, const char* attribute,
							hid_t datatype, void* value);

/**
 * Reads the entire dataset data from the hdf5 file
 */
oknok_t hdf5_read_dataset_data(hid_t dataset_id, word_t* data);

/**
 * Returns the dataset dimensions stored in the hdf5 dataset
 */
void hdf5_get_dataset_dimensions(hid_t dataset_id, hsize_t* dataset_dimensions);

/**
 * Free resources and closes open connections
 */
void hdf5_close_dataset(dataset_hdf5_t* dataset);

#endif
