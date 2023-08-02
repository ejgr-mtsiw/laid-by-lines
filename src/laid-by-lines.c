/*
 ============================================================================
 Name        : laid-by-lines.c
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
#include "types/class_offsets_t.h"
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

	// Parse command line arguments
	if (read_args(argc, argv, &args) == READ_CL_ARGS_NOK)
	{
		return EXIT_FAILURE;
	}

	// Initialize MPI
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

	// Setup global rank and size
	MPI_Comm_size(comm, &size);
	MPI_Comm_rank(comm, &rank);

	/**
	 * Intranode communicator group
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

	// Setup node rank and size
	MPI_Comm_size(node_comm, &node_size);
	MPI_Comm_rank(node_comm, &node_rank);

	/**
	 * The dataset
	 */
	dataset_t dataset;
	init_dataset(&dataset);

	/**
	 * The HDF5 dataset file
	 */
	dataset_hdf5_t hdf5_dset;

	/**
	 * The disjoint matrix info
	 */
	dm_t dm;

	/**
	 * Timing for the full operation
	 */
	time_t main_tick = 0, main_tock = 0;

	/**
	 * Local timing structures
	 */
	time_t tick = 0, tock = 0;

	if (rank == ROOT_RANK)
	{
		// Update start time for the full operation
		main_tick = time(0);
	}

	/**
	 * Only rank 0 on a node actually reads the dataset data and allocates
	 * memory
	 */
	uint64_t dset_data_size = 0;

	// Open dataset file
	ROOT_SHOWS("Using dataset '%s'\n", args.filename);
	ROOT_SHOWS("Using %d processes\n\n", size);
	ROOT_SAYS("Initializing MPI RMA: ");
	TICK;

	if (hdf5_open_dataset(args.filename, args.datasetname, &hdf5_dset) == NOK)
	{
		return EXIT_FAILURE;
	}

	dataset.n_observations = hdf5_dset.dimensions[0];
	dataset.n_words		   = hdf5_dset.dimensions[1];

	if (node_rank == LOCAL_ROOT_RANK)
	{
		dset_data_size = dataset.n_observations * dataset.n_words;
	}

	// Allocate shared data
	word_t* dset_data		= NULL;
	MPI_Win win_shared_dset = MPI_WIN_NULL;
	MPI_Win_allocate_shared(dset_data_size * sizeof(word_t), sizeof(word_t),
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
	/**
	 * All dataset.data pointers should now point to copy on noderank 0
	 */
	TOCK;

	// Setup dataset

	ROOT_SAYS("Reading dataset: ");
	TICK;

	// Load dataset attributes
	hdf5_read_dataset_attributes(hdf5_dset.dataset_id, &dataset);

	if (node_rank == LOCAL_ROOT_RANK)
	{

		// Load dataset data
		hdf5_read_dataset_data(hdf5_dset.dataset_id, dataset.data);

		TOCK;
		// Print dataset details
		ROOT_SHOWS("  Classes = %d", dataset.n_classes);
		ROOT_SHOWS(" [%d bits]\n", dataset.n_bits_for_class);
		ROOT_SHOWS("  Attributes = %d \n", dataset.n_attributes);
		ROOT_SHOWS("  Observations = %d \n", dataset.n_observations);
	}

	// We no longer need the dataset file
	hdf5_close_dataset(&hdf5_dset);

	if (node_rank == LOCAL_ROOT_RANK)
	{
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

		unsigned int duplicates = remove_duplicates(&dataset);

		TOCK;
		ROOT_SHOWS("  %d duplicate(s) removed\n", duplicates);
	}

	// Update n_observations because only root knows if we removed lines
	MPI_Bcast(&(dataset.n_observations), 1, MPI_UINT32_T, LOCAL_ROOT_RANK,
			  node_comm);

	// Fill class arrays
	ROOT_SAYS("Checking classes: ");
	TICK;

	/**
	 * Array that stores the number of observations for each class
	 */
	dataset.n_observations_per_class
		= (uint32_t*) calloc(dataset.n_classes, sizeof(uint32_t));
	assert(dataset.n_observations_per_class != NULL);

	/**
	 * Matrix that stores the list of observations per class
	 */
	dataset.observations_per_class = (word_t**) calloc(
		dataset.n_classes * dataset.n_observations, sizeof(word_t*));
	assert(dataset.observations_per_class != NULL);

	if (fill_class_arrays(&dataset) != OK)
	{
		return EXIT_FAILURE;
	}

	TOCK;

	if (rank == ROOT_RANK)
	{
		for (unsigned int i = 0; i < dataset.n_classes; i++)
		{
			ROOT_SHOWS("  Class %d: ", i);
			ROOT_SHOWS("%d item(s)\n", dataset.n_observations_per_class[i]);
		}
	}

	MPI_Barrier(node_comm);

	// Set JNSQ

	if (node_rank == LOCAL_ROOT_RANK)
	{

		ROOT_SAYS("Setting up JNSQ attributes: ");
		TICK;

		uint32_t max_inconsistency = add_jnsqs(&dataset);

		// Update number of bits needed for jnsqs
		if (max_inconsistency > 0)
		{
			// How many bits are needed for jnsq attributes
			uint8_t n_bits_for_jnsq = ceil(log2(max_inconsistency + 1));

			dataset.n_bits_for_jnsqs = n_bits_for_jnsq;
		}

		TOCK;
		ROOT_SHOWS("  Max JNSQ: %d", max_inconsistency);
		ROOT_SHOWS(" [%d bits]\n", dataset.n_bits_for_jnsqs);
	}

	// Update dataset data because only node_root knows if we added jnsqs
	MPI_Bcast(&(dataset.n_bits_for_jnsqs), 1, MPI_UINT8_T, LOCAL_ROOT_RANK,
			  node_comm);

	// JNSQ attributes are treated just like all the other attributes from
	// this point forward
	dataset.n_attributes += dataset.n_bits_for_jnsqs;

	// TODO: CONFIRM FIX: n_words may have changed?
	// If we have 5 classes (3 bits) and only one bit is in the last word
	// If we only use 2 bits for jnsqs then we need one less n_words
	// This could impact all the calculations that assume n_words - 1
	// is the last word!
	dataset.n_words = dataset.n_attributes / WORD_BITS
		+ (dataset.n_attributes % WORD_BITS != 0);

	// End setup dataset

	// Distribute disjoint matrix lines
	ROOT_SAYS("Calculating disjoint matrix lines to generate: ");
	TICK;

	// Calculate the number of disjoint matrix lines
	dm.n_matrix_lines = get_dm_n_lines(&dataset);

	// Calculate the offset and number of lines for this process
	dm.s_offset = BLOCK_LOW(rank, size, dm.n_matrix_lines);
	dm.s_size	= BLOCK_SIZE(rank, size, dm.n_matrix_lines);

	TOCK;

	if (rank == ROOT_RANK)
	{
		double matrix_size = ((double) dm.n_matrix_lines * dataset.n_attributes)
			/ (1024.0 * 1024 * 1024 * 8);
		ROOT_SHOWS("  Estimated disjoint matrix size: %3.2fGB\n", matrix_size);

		fprintf(stdout, "  Number of lines in the disjoint matrix: %u\n",
				dm.n_matrix_lines);

		for (int r = 0; r < size; r++)
		{
			uint32_t s_offset = BLOCK_LOW(r, size, dm.n_matrix_lines);
			uint32_t s_size	  = BLOCK_SIZE(r, size, dm.n_matrix_lines);
			if (s_size > 0)
			{
				fprintf(stdout,
						"    Process %d will generate %u lines [%u -> %u]\n", r,
						s_size, s_offset, s_offset + s_size - 1);
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
	 * Number of words in a column
	 */
	uint32_t n_words_in_column
		= dm.s_size / WORD_BITS + (dm.s_size % WORD_BITS != 0);

	/**
	 * The covered lines bit array
	 */
	word_t* covered_lines = (word_t*) calloc(n_words_in_column, sizeof(word_t));

	/**
	 * The local attribute totals
	 */
	uint32_t* attribute_totals
		= (uint32_t*) malloc(dataset.n_attributes* sizeof(uint32_t));

	/**
	 * Global totals. Only root needs these
	 */

	/**
	 * Full total for each attribute.
	 * It's filled at the start using the sum of all the totals for each
	 * process.
	 */
	uint32_t* global_attribute_totals = NULL;

	/**
	 * Selected attributes aka the solution
	 */
	word_t* selected_attributes = NULL;

	/**
	 * Number of uncovered lines. Only root needs this.
	 */
	uint32_t n_uncovered_lines = 0;

	if (rank == ROOT_RANK)
	{
		global_attribute_totals
			= (uint32_t*) calloc(dataset.n_attributes, sizeof(uint32_t));

		selected_attributes = (word_t*) calloc(dataset.n_words, sizeof(word_t));

		// No line covered so far
		n_uncovered_lines = dm.n_matrix_lines;
	}

	// Get class offsets for first step of the computation
	class_offsets_t class_offsets;
	calculate_class_offsets(&dataset, dm.s_offset, &class_offsets);

	while (true)
	{
		// Reset attributes totals
		memset(attribute_totals, 0, dataset.n_attributes * sizeof(uint32_t));

		// Calculate partial totals
		calculate_attribute_totals(&dataset, &class_offsets, covered_lines,
								   dm.s_size, attribute_totals);

		// Calculate global totals
		MPI_Reduce(attribute_totals, global_attribute_totals,
				   dataset.n_attributes, MPI_UINT32_T, MPI_SUM, ROOT_RANK,
				   comm);

		// Get best attribute index
		int64_t best_attribute = -1;

		if (rank == ROOT_RANK)
		{
			best_attribute = get_best_attribute_index(global_attribute_totals,
													  dataset.n_attributes);

			ROOT_SHOWS("  Selected attribute #%ld, ", best_attribute);
			ROOT_SHOWS("covers %d lines ",
					   global_attribute_totals[best_attribute]);
			TOCK;
			TICK;

			// Mark best attribute as selected
			mark_attribute_as_selected(selected_attributes, best_attribute);

			// Update number of lines remaining
			n_uncovered_lines -= global_attribute_totals[best_attribute];

			// If we covered all of them, we can leave earlier
			if (n_uncovered_lines == 0)
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

		// Update covered lines
		update_covered_lines(&dataset, &class_offsets, covered_lines, dm.s_size,
							 best_attribute);
	}

show_solution:
	// wait for everyone
	MPI_Barrier(comm);

	if (rank == ROOT_RANK)
	{
		fprintf(stdout, "Solution: { ");

		uint32_t current_attribute = 0;
		uint32_t solution_size	   = 0;

		for (uint32_t w = 0; w < dataset.n_words; w++)
		{
			for (int8_t bit = WORD_BITS - 1;
				 bit >= 0 && current_attribute < dataset.n_attributes;
				 bit--, current_attribute++)
			{
				if (selected_attributes[w] & AND_MASK_TABLE[bit])
				{
					// This attribute is set so it's part of the solution
					fprintf(stdout, "%d ", current_attribute);
					solution_size++;
				}
			}
		}

		fprintf(stdout, "}\nSolution has %d attributes: %d / %d = %3.4f%%\n",
				solution_size, solution_size, dataset.n_attributes,
				((float) solution_size / (float) dataset.n_attributes) * 100);

		fprintf(stdout, "All done! ");

		main_tock = time(0);
		fprintf(stdout, "[%lds]\n", main_tock - main_tick);

		free(global_attribute_totals);
		global_attribute_totals = NULL;

		free(selected_attributes);
		selected_attributes = NULL;
	}

	// Free shared dataset
	MPI_Win_free(&win_shared_dset);
	dataset.data = NULL;
	free_dataset(&dataset);

	free(covered_lines);
	covered_lines = NULL;

	free(attribute_totals);
	attribute_totals = NULL;

	/* shut down MPI */
	MPI_Finalize();

	return EXIT_SUCCESS;
}