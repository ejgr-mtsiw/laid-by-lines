/*
 ============================================================================
 Name        : laid_by_lines.c
 Author      : Eduardo Ribeiro
 Description : OpenMPI implementation of the LAID algorithm in C + HDF5
 ============================================================================
 */

#include "dataset.h"
#include "dataset_hdf5.h"
#include "disjoint_matrix.h"
#include "disjoint_matrix_mpi.h"
#include "jnsq.h"
#include "set_cover.h"
#include "types/dataset_hdf5_t.h"
#include "types/dataset_t.h"
#include "types/dm_t.h"
#include "types/word_t.h"
#include "utils/bit.h"
#include "utils/block.h"
#include "utils/clargs.h"
#include "utils/output.h"
#include "utils/ranks.h"
#include "utils/sort_r.h"
#include "utils/timing.h"

#include "hdf5.h"
#include "mpi.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/**
 * In this mode we don't write the disjoint matrix (DM).
 * Everytime we need a line or column from the DM it's generated from the
 * dataset in memory.
 *
 * Each node root will open the original dataset file and read the contents to
 * memory. This allows us to save memory in each node, without sacrificing much
 * performance, because we're not having to send data across nodes.
 *
 * The node root(s) sort the dataset in memory, remove duplicates and  adds
 * jnsqs bits if necessary.
 *
 * Then they build a list of the steps needed to generate the disjoint matrix.
 * This list of steps allows us to generate any line or column of the disjoint
 * matrix.
 *
 * TLDR:
 * Each node root
 *  - Reads dataset attributes from hdf5 file
 *  - Read dataset
 *  - Sort dataset
 *  - Remove duplicates
 *  - Add jnsqs
 *  - Builds steps for matrix generation
 *
 * All processes
 *  - Apply set covering algorithm
 *
 * Global root
 *  - Show solution
 */
int main(int argc, char** argv)
{
	/**
	 * Command line arguments set by the user
	 */
	clargs_t args;

	/**
	 * Parse command line arguments
	 */
	if (read_args(argc, argv, &args) == READ_CL_ARGS_NOK)
	{
		return EXIT_FAILURE;
	}

	/*
	 * Initialize MPI
	 */
	if (MPI_Init(&argc, &argv) != MPI_SUCCESS)
	{
		printf("Error initializing MPI environment!\n");
		return EXIT_FAILURE;
	}

	/**
	 * Rank of process
	 */
	int rank;

	/**
	 * Number of processes
	 */
	int size;

	/**
	 * Global communicator group
	 */
	MPI_Comm comm = MPI_COMM_WORLD;

	/**
	 * Setup global rank and size
	 */
	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &rank);

	/**
	 * Node communicator group
	 */
	MPI_Comm node_comm = MPI_COMM_NULL;

	/**
	 * Create node-local communicator
	 * This communicator is used to share memory with processes intranode
	 */
	MPI_Comm_split_type(comm, MPI_COMM_TYPE_SHARED, rank, MPI_INFO_NULL,
						&node_comm);

	/**
	 * In-node rank of process
	 */
	int node_rank;

	/**
	 * In-node number of processes
	 */
	int node_size;

	/**
	 * Setup node rank and size
	 */
	MPI_Comm_size(node_comm, &node_size);
	MPI_Comm_rank(node_comm, &node_rank);

	/**
	 * Timing for the full operation
	 */
	SETUP_TIMING_GLOBAL;

	/**
	 * Local timing structures
	 */
	SETUP_TIMING;

	/**
	 * The dataset
	 */
	dataset_t dataset;
	init_dataset(&dataset);

	/**
	 * The HDF5 dataset file
	 */
	dataset_hdf5_t hdf5_dset;

	// Open dataset file
	ROOT_SHOWS("Using dataset '%s'\n", args.filename);
	ROOT_SHOWS("Using %d processes\n\n", size);
	ROOT_SAYS("Initializing MPI Shared Dataset: ");
	TICK;

	/**
	 * Dataset data size
	 * Only rank 0 on a node actually reads the dataset and allocates memory
	 */
	uint64_t shared_data_size = 0;

	if (node_rank == LOCAL_ROOT_RANK)
	{
		if (hdf5_open_dataset(args.filename, args.datasetname, &hdf5_dset)
			== NOK)
		{
			return EXIT_FAILURE;
		}

		dataset.n_observations = hdf5_dset.dimensions[0];
		dataset.n_words		   = hdf5_dset.dimensions[1];

		shared_data_size = dataset.n_observations * dataset.n_words;
	}

	word_t* dset_data		= NULL;
	MPI_Win win_shared_dset = MPI_WIN_NULL;
	MPI_Win_allocate_shared(shared_data_size * sizeof(word_t), sizeof(word_t),
							MPI_INFO_NULL, node_comm, &dset_data,
							&win_shared_dset);

	// Set dataset data pointer
	if (node_rank == LOCAL_ROOT_RANK)
	{
		dataset.data = dset_data;
	}
	else
	{
		MPI_Aint win_size;
		int win_disp;
		MPI_Win_shared_query(win_shared_dset, LOCAL_ROOT_RANK, &win_size,
							 &win_disp, &dataset.data);
	}
	// All dataset.data pointers should now point to copy on noderank 0

	TOCK;

	// Setup dataset

	ROOT_SAYS("Reading dataset: ");
	TICK;

	if (node_rank == LOCAL_ROOT_RANK)
	{
		// Load dataset attributes
		hdf5_read_dataset_attributes(hdf5_dset.dataset_id, &dataset);

		// Load dataset data
		hdf5_read_dataset_data(hdf5_dset.dataset_id, dataset.data);

		TOCK;

		// Print dataset details
		ROOT_SHOWS("  Classes = %lu", dataset.n_classes);
		ROOT_SHOWS(" [%d bits]\n", dataset.n_bits_for_class);
		ROOT_SHOWS("  Attributes = %lu \n", dataset.n_attributes);
		ROOT_SHOWS("  Observations = %lu \n", dataset.n_observations);

		// We no longer need the dataset file
		hdf5_close_dataset(&hdf5_dset);

		// Sort dataset
		ROOT_SAYS("Sorting dataset: ");
		TICK;

		/**
		 * We need to know the number of longs in each line of the dataset
		 * so we can't use the standard qsort implementation
		 */
		sort_r(dataset.data, dataset.n_observations,
			   dataset.n_words * sizeof(word_t), compare_lines_extra,
			   &dataset.n_words);

		TOCK;

		// Remove duplicates
		ROOT_SAYS("Removing duplicates: ");
		TICK;

		uint64_t duplicates = remove_duplicates(&dataset);

		TOCK;
		ROOT_SHOWS("  %lu duplicate(s) removed\n", duplicates);
	}

	// Share current dataset attributes
	MPI_Bcast(&(dataset.n_attributes), 1, MPI_UINT64_T, LOCAL_ROOT_RANK,
			  node_comm);
	MPI_Bcast(&(dataset.n_observations), 1, MPI_UINT64_T, LOCAL_ROOT_RANK,
			  node_comm);
	MPI_Bcast(&(dataset.n_classes), 1, MPI_UINT64_T, LOCAL_ROOT_RANK,
			  node_comm);
	MPI_Bcast(&(dataset.n_bits_for_class), 1, MPI_UINT8_T, LOCAL_ROOT_RANK,
			  node_comm);
	MPI_Bcast(&(dataset.n_words), 1, MPI_UINT64_T, LOCAL_ROOT_RANK, node_comm);

	// Fill class arrays
	ROOT_SAYS("Checking classes: ");
	TICK;

	/**
	 * Array that stores the number of observations for each class
	 */
	dataset.n_observations_per_class
		= (uint64_t*) calloc(dataset.n_classes, sizeof(uint64_t));
	assert(dataset.n_observations_per_class != NULL);

	/**
	 * Matrix that stores the list of observations per class
	 */
	dataset.observations_per_class = (word_t**) calloc(
		dataset.n_classes * dataset.n_observations, sizeof(word_t*));
	assert(dataset.observations_per_class != NULL);

	// Must make sure the dataset is filled before proceeding
	MPI_Barrier(node_comm);

	if (fill_class_arrays(&dataset) != OK)
	{
		return EXIT_FAILURE;
	}

	TOCK;

	if (rank == ROOT_RANK)
	{
		for (uint64_t i = 0; i < dataset.n_classes; i++)
		{
			ROOT_SHOWS("  Class %lu: ", i);
			ROOT_SHOWS("%lu item(s)\n", dataset.n_observations_per_class[i]);
		}
	}

	// Must make sure everyone has finished before changing the dataset
	MPI_Barrier(node_comm);

	// Set JNSQ
	if (node_rank == LOCAL_ROOT_RANK)
	{
		ROOT_SAYS("Setting up JNSQ attributes: ");
		TICK;

		uint64_t max_inconsistency = add_jnsqs(&dataset);

		// Update number of bits needed for jnsqs
		if (max_inconsistency > 0)
		{
			// How many bits are needed for jnsq attributes
			uint8_t n_bits_for_jnsq = ceil(log2(max_inconsistency + 1));

			dataset.n_bits_for_jnsqs = n_bits_for_jnsq;
		}

		TOCK;
		ROOT_SHOWS("  Max JNSQ: %lu", max_inconsistency);
		ROOT_SHOWS(" [%d bits]\n", dataset.n_bits_for_jnsqs);
	}

	// Update dataset data because only node_root knows if we added jnsqs
	MPI_Bcast(&(dataset.n_bits_for_jnsqs), 1, MPI_UINT8_T, LOCAL_ROOT_RANK,
			  node_comm);

	// JNSQ attributes are treated just like all the other attributes from
	// this point forward
	dataset.n_attributes += dataset.n_bits_for_jnsqs;

	// n_words may have changed?
	// If we have 5 classes (3 bits) and only one bit is in the last word
	// If we only use 2 bits for jnsqs then we need one less n_words
	// This could impact all the calculations that assume n_words - 1
	// is the last word!
	dataset.n_words = dataset.n_attributes / WORD_BITS
		+ (dataset.n_attributes % WORD_BITS != 0);

	// End setup dataset

	MPI_Barrier(node_comm);

	// Setup disjoint matrix

	/**
	 * The disjoint matrix info
	 */
	dm_t dm;

	ROOT_SAYS("Calculating disjoint matrix lines to generate: ");
	TICK;

	// Calculate the number of disjoint matrix lines
	dm.n_matrix_lines = get_dm_n_lines(&dataset);

	// Calculate the offset and number of lines for this process
	dm.s_offset = BLOCK_LOW(rank, size, dm.n_matrix_lines);
	dm.s_size	= BLOCK_SIZE(rank, size, dm.n_matrix_lines);

	// Calculate initial offsets for each process
	calculate_class_offsets(&dataset, dm.s_offset, &dm.initial_class_offsets);

	TOCK;

	if (rank == ROOT_RANK)
	{
		double matrix_size = ((double) dm.n_matrix_lines * dataset.n_attributes)
			/ (1024.0 * 1024 * 1024 * 8);
		ROOT_SHOWS("  Estimated disjoint matrix size: %3.2fGB\n", matrix_size);

		fprintf(stdout, "  Number of lines in the disjoint matrix: %lu\n",
				dm.n_matrix_lines);

		for (int r = 0; r < size; r++)
		{
			uint64_t s_offset = BLOCK_LOW(r, size, dm.n_matrix_lines);
			uint64_t s_size	  = BLOCK_SIZE(r, size, dm.n_matrix_lines);
			if (s_size > 0)
			{
				fprintf(stdout,
						"    Process %d will generate %lu lines [%lu -> %lu]\n",
						r, s_size, s_offset, s_offset + s_size - 1);
			}
			else
			{
				fprintf(stdout, "    Process %d will generate 0 lines\n", r);
			}
		}
	}

	TICK;

	/**
	 * All:
	 *  - Setup line covered array -> 0
	 *  - Setup attribute covered array -> 0
	 *
	 *loop:
	 *  - Reset atributes totals -> 0
	 *  - Calculate attributes totals
	 *  - MPI_Reduce attributes totals
	 *
	 * ROOT:
	 *  - Selects the best attribute and blacklists it
	 *  - Sends attribute id to everyone else
	 *
	 * !ROOT:
	 *  - Wait for attribute message
	 *
	 * ALL:
	 *  - if there are no more lines to blacklist (attribute == -1):
	 *   - goto show solution
	 *
	 * ALL:
	 *  - Black list their lines covered by this attribute
	 *
	 * ALl:
	 *  - goto loop
	 */

	ROOT_SAYS("Applying set covering algorithm:\n");
	TICK;

	/**
	 * Number of words needed to store a column (attribute data)
	 */
	dm.n_words_in_a_column
		= dm.s_size / WORD_BITS + (dm.s_size % WORD_BITS != 0);

	/**
	 * The best attribute data bit array
	 */
	word_t* best_column
		= (word_t*) calloc(dm.n_words_in_a_column, sizeof(word_t));

	/**
	 * The covered lines bit array
	 */
	word_t* covered_lines
		= (word_t*) calloc(dm.n_words_in_a_column, sizeof(word_t));

	/**
	 * Number of uncovered lines.
	 */
	uint64_t n_uncovered_lines = dm.s_size;

	/**
	 * The local attribute totals
	 * Use n_words instead of n_attributes to avoid extra
	 * verifications on last word
	 */
	uint64_t* attribute_totals
		= (uint64_t*) calloc(dataset.n_words * WORD_BITS, sizeof(uint64_t));

	/**
	 * Global totals. Only root needs these
	 */

	/**
	 * Full total for each attribute.
	 * Use n_words instead of n_attributes on allocation to avoid
	 * extra verifications on last word
	 */
	uint64_t* global_attribute_totals = NULL;

	/**
	 * Selected attributes bit array aka the solution
	 */
	word_t* selected_attributes = NULL;

	/**
	 * Number of uncovered lines in the full matrix. Only root needs this.
	 */
	uint64_t global_n_uncovered_lines = dm.n_matrix_lines;

	if (rank == ROOT_RANK)
	{
		global_attribute_totals
			= (uint64_t*) calloc(dataset.n_words * WORD_BITS, sizeof(uint64_t));

		selected_attributes = (word_t*) calloc(dataset.n_words, sizeof(word_t));
	}

	// Calculate the totals for all attributes
	calculate_initial_attribute_totals(&dataset, &dm, attribute_totals);

	while (true)
	{
		// Calculate global totals
		MPI_Reduce(attribute_totals, global_attribute_totals,
				   dataset.n_attributes, MPI_UINT64_T, MPI_SUM, ROOT_RANK,
				   comm);

		// Get best attribute index
		int64_t best_attribute = -1;

		if (rank == ROOT_RANK)
		{
			best_attribute = get_best_attribute_index(global_attribute_totals,
													  dataset.n_attributes);

			ROOT_SHOWS("  Selected attribute #%ld, ", best_attribute);
			ROOT_SHOWS("covers %lu lines ",
					   global_attribute_totals[best_attribute]);
			TOCK;
			TICK;

			// Mark best attribute as selected
			mark_attribute_as_selected(selected_attributes, best_attribute);

			// Update number of lines remaining in the disjoint matrix
			global_n_uncovered_lines -= global_attribute_totals[best_attribute];

			// If we covered all of them, we can leave earlier
			if (global_n_uncovered_lines == 0)
			{
				best_attribute = -1;
			}
		}

		// Share best attribute with everyone
		MPI_Bcast(&best_attribute, 1, MPI_INT64_T, 0, comm);

		// If best_attribute is -1 we are done
		if (best_attribute < 0)
		{
			goto show_solution;
		}

		// Update number of lines remaining in the partial disjoint matrix
		n_uncovered_lines -= attribute_totals[best_attribute];

		// If we covered all of them, we can leave earlier?
		// No we don't: We need to participate in the MPI_Reduce
		if (n_uncovered_lines == 0)
		{
			// Reset attributes totals because we can have some remaining values
			// from previous run
			memset(attribute_totals, 0,
				   dataset.n_attributes * sizeof(uint64_t));
			continue;
		}

		get_column(&dataset, &dm, best_attribute, best_column);

		if (n_uncovered_lines < attribute_totals[best_attribute])
		{
			// Add
			// Update covered lines
			update_covered_lines(best_column, dm.n_words_in_a_column,
								 covered_lines);

			calculate_attribute_totals_add(&dataset, &dm, covered_lines,
										   attribute_totals);
		}
		else
		{
			// Sub
			for (uint64_t w = 0; w < dm.n_words_in_a_column; w++)
			{
				best_column[w] &= ~covered_lines[w];
			}

			calculate_attribute_totals_sub(&dataset, &dm, best_column,
										   attribute_totals);

			// Update covered lines
			update_covered_lines(best_column, dm.n_words_in_a_column,
								 covered_lines);
		}
	}

show_solution:
	// wait for everyone
	MPI_Barrier(comm);

	if (rank == ROOT_RANK)
	{
		fprintf(stdout, "Solution: { ");

		uint64_t current_attribute = 0;
		uint64_t solution_size	   = 0;

		for (uint64_t w = 0; w < dataset.n_words; w++)
		{
			for (int8_t bit = WORD_BITS - 1;
				 bit >= 0 && current_attribute < dataset.n_attributes;
				 bit--, current_attribute++)
			{
				if (selected_attributes[w] & AND_MASK_TABLE[bit])
				{
					// This attribute is set so it's part of the solution
					fprintf(stdout, "%lu ", current_attribute);
					solution_size++;
				}
			}
		}

		fprintf(stdout, "}\nSolution has %lu attributes: %lu / %lu = %3.4f%%\n",
				solution_size, solution_size, dataset.n_attributes,
				((float) solution_size / (float) dataset.n_attributes) * 100);

		fprintf(stdout, "All done! ");

		free(global_attribute_totals);
		global_attribute_totals = NULL;

		free(selected_attributes);
		selected_attributes = NULL;
	}

	free(covered_lines);
	covered_lines = NULL;

	free(best_column);
	best_column = NULL;

	free(attribute_totals);
	attribute_totals = NULL;

	// Free shared dataset
	MPI_Win_free(&win_shared_dset);
	dataset.data = NULL;
	free_dataset(&dataset);

	PRINT_TIMING_GLOBAL;

	/* shut down MPI */
	MPI_Finalize();

	return EXIT_SUCCESS;
}
