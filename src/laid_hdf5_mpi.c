/*
 ============================================================================
 Name        : laid_hdf5_mpi.c
 Author      : Eduardo Ribeiro
 Description : OpenMPI implementation of the LAID algorithm in C + HDF5
 ============================================================================
 */

#include "dataset.h"
#include "dataset_hdf5.h"
#include "disjoint_matrix.h"
#include "jnsq.h"
#include "set_cover.h"
#include "types/dataset_hdf5_t.h"
#include "types/dataset_t.h"
#include "types/dm_t.h"
#include "types/steps_t.h"
#include "types/word_t.h"
#include "utils/bit.h"
#include "utils/block.h"
#include "utils/clargs.h"
#include "utils/math.h"
#include "utils/output.h"
#include "utils/ranks.h"
#include "utils/sort_r.h"
#include "utils/timing.h"

#include "hdf5.h"
#include "mpi.h"

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
		/**
		 * Timing for the full operation
		 */
		main_tick = time(0);
	}

	/**
	 * Only rank 0 on a node actually reads the dataset and allocates memory
	 */
	uint64_t dset_data_size = 0;

	/**
	 * Open dataset file
	 */
	ROOT_SHOWS("Using dataset '%s'\n", args.filename);
	ROOT_SHOWS("Using %d processes\n\n", size);
	ROOT_SAYS("Initializing MPI RMA: ");
	TICK;

	if (node_rank == LOCAL_ROOT_RANK)
	{
		if (hdf5_open_dataset(args.filename, args.datasetname, &hdf5_dset)
			== NOK)
		{
			return EXIT_FAILURE;
		}

		dataset.n_observations = hdf5_dset.dimensions[0];
		dataset.n_words		   = hdf5_dset.dimensions[1];

		dset_data_size = dataset.n_observations * dataset.n_words;
	}

	word_t* dset_data		= NULL;
	MPI_Win win_shared_dset = MPI_WIN_NULL;
	MPI_Win_allocate_shared(dset_data_size * sizeof(word_t), sizeof(word_t),
							MPI_INFO_NULL, node_comm, &dset_data,
							&win_shared_dset);

	/**
	 * Set dataset data pointer
	 */
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
	 * All table pointers should now point to copy on noderank 0
	 */
	TOCK;

	/**
	 * Setup dataset
	 */
	if (node_rank == LOCAL_ROOT_RANK)
	{
		ROOT_SAYS("Reading dataset: ");
		TICK;

		/**
		 * Load dataset attributes
		 */
		hdf5_read_dataset_attributes(hdf5_dset.dataset_id, &dataset);

		/**
		 * Load dataset data
		 */
		hdf5_read_dataset_data(hdf5_dset.dataset_id, dataset.data);

		TOCK;
		/**
		 * Print dataset details
		 */
		ROOT_SHOWS("  Classes = %d", dataset.n_classes);
		ROOT_SHOWS(" [%d bits]\n", dataset.n_bits_for_class);
		ROOT_SHOWS("  Attributes = %d \n", dataset.n_attributes);
		ROOT_SHOWS("  Observations = %d \n", dataset.n_observations);

		/**
		 * We no longer need the dataset file
		 */
		hdf5_close_dataset(&hdf5_dset);

		/**Sort dataset
		 *
		 */
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

		/**
		 * Remove duplicates
		 */
		ROOT_SAYS("Removing duplicates: ");
		TICK;

		unsigned int duplicates = remove_duplicates(&dataset);

		TOCK;
		ROOT_SHOWS("  %d duplicate(s) removed\n", duplicates);

		/**
		 * Fill class arrays
		 */
		ROOT_SAYS("Checking classes: ");
		TICK;

		if (fill_class_arrays(&dataset) != OK)
		{
			return EXIT_FAILURE;
		}

		TOCK;

		for (unsigned int i = 0; i < dataset.n_classes; i++)
		{
			ROOT_SHOWS("  Class %d: ", i);
			ROOT_SHOWS("%d item(s)\n", dataset.n_observations_per_class[i]);
		}

		/**
		 * Set JNSQ
		 */
		ROOT_SAYS("Setting up JNSQ attributes: ");
		TICK;

		unsigned int max_jnsq = add_jnsqs(&dataset);

		TOCK;
		ROOT_SHOWS("  Max JNSQ: %d", max_jnsq);
		ROOT_SHOWS(" [%d bits]\n", dataset.n_bits_for_jnsqs);
	}

	/**
	 * End setup dataset
	 */

	// MPI_Barrier(node_comm);

	dm.n_matrix_lines = 0;
	if (node_rank == LOCAL_ROOT_RANK)
	{
		dm.n_matrix_lines = get_dm_n_lines(&dataset);
	}

	steps_t* localsteps		 = NULL;
	MPI_Win win_shared_steps = MPI_WIN_NULL;
	MPI_Win_allocate_shared(dm.n_matrix_lines * sizeof(steps_t),
							sizeof(steps_t), MPI_INFO_NULL, node_comm,
							&localsteps, &win_shared_steps);

	/**
	 * The steps
	 */
	steps_t* steps = NULL;

	/**
	 * Set dataset data pointer
	 */
	if (node_rank == LOCAL_ROOT_RANK)
	{
		steps = localsteps;
	}
	else
	{
		MPI_Aint win_size;
		int win_disp;
		MPI_Win_shared_query(win_shared_steps, LOCAL_ROOT_RANK, &win_size,
							 &win_disp, &steps);
	}
	/**
	 * All table pointers should now point to copy on noderank 0
	 */

	/**
	 * Setup steps
	 */
	if (node_rank == LOCAL_ROOT_RANK)
	{
		ROOT_SAYS("Generating matrix steps: ");
		TICK;

		uint32_t nc	   = dataset.n_classes;
		uint32_t no	   = dataset.n_observations;
		uint32_t* opc  = dataset.observations_per_class;
		uint32_t* nopc = dataset.n_observations_per_class;

		/**
		 * DO IT
		 */
		uint32_t cs = 0;

		for (uint32_t ca = 0; ca < nc - 1; ca++)
		{
			for (uint32_t ia = 0; ia < nopc[ca]; ia++)
			{
				for (uint32_t cb = ca + 1; cb < nc; cb++)
				{
					for (uint32_t ib = 0; ib < nopc[cb]; ib++)
					{
						steps[cs].indexA = opc[ca * no + ia];
						steps[cs].indexB = opc[cb * no + ib];

						cs++;
					}
				}
			}
		}

		free(dataset.n_observations_per_class);
		free(dataset.observations_per_class);

		dataset.n_observations_per_class = NULL;
		dataset.observations_per_class	 = NULL;

		TOCK;
		ROOT_SHOWS("  %d matrix steps generated\n", dm.n_matrix_lines);

		double matrix_size
			= ((double) dm.n_matrix_lines
			   * (dataset.n_attributes + dataset.n_bits_for_jnsqs))
			/ (1024 * 1024 * 8);
		ROOT_SHOWS("  Estimated disjoint matrix size: %3.2fMB\n", matrix_size);
	}

	ROOT_SAYS("Distributing steps: ");
	TICK;

	uint64_t toshare[4];
	if (node_rank == LOCAL_ROOT_RANK)
	{
		toshare[0] = dataset.n_attributes;
		toshare[1] = dataset.n_observations;
		toshare[2] = dataset.n_words;
		toshare[3] = dm.n_matrix_lines;

		MPI_Bcast(&toshare, 4, MPI_UINT64_T, LOCAL_ROOT_RANK, node_comm);
	}
	else
	{
		MPI_Bcast(&toshare, 4, MPI_UINT64_T, LOCAL_ROOT_RANK, node_comm);

		dataset.n_attributes   = toshare[0];
		dataset.n_observations = toshare[1];
		dataset.n_words		   = toshare[2];
		dm.n_matrix_lines	   = toshare[3];
	}

	/**
	 * Steps distribution per processor
	 */
	dm.s_offset = BLOCK_LOW(rank, size, dm.n_matrix_lines);
	dm.s_size	= BLOCK_SIZE(rank, size, dm.n_matrix_lines);

	/**
	 * Each step has the info needed to generate a line of the
	 * disjoint matrix, the steps for this process start here
	 */
	dm.steps = steps + dm.s_offset;

	TOCK;

	//	for (int r = 0; r < size; r++)
	//	{
	//		if (r == rank)
	//		{
	//			printf("Steps for rank %d\n", rank);
	//			for (uint32_t i = 0; i < dm.s_size; i++)
	//			{
	//				printf(" - [%d] [%d] x [%d]\n", i, dm.steps[i].indexA + 1,
	//					   dm.steps[i].indexB + 1);
	//			}
	//		}
	//		sleep(1);
	//	}

	/**
	 * All:
	 *  - Setup line covered array -> 0
	 *  - Setup attribute covered array -> 0
	 *
	 *  - Calculate attributes totals
	 *  - MPI_Reduce attributes totals
	 *
	 *loop:
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
	 *  - Update atributes totals
	 *  - MPI_Reduce attributes totals
	 *
	 * ROOT:
	 *  - Subtract atribute totals from global attributes total
	 *
	 * ALl:
	 *  - goto loop
	 */

	ROOT_SAYS("Applying set covering algorithm:\n");
	TICK;

	/**
	 * Number of words with WORD_BITS attributes
	 */
	uint32_t n_full_words = dataset.n_words - 1;

	/**
	 * Last bit to process in the last word
	 */
	uint8_t n_last_word = WORD_BITS - (dataset.n_attributes % WORD_BITS);

	// The covered lines and covered attributes are bit arrays
	uint32_t n_words_in_column
		= dm.n_matrix_lines / WORD_BITS + (dm.n_matrix_lines % WORD_BITS != 0);

	word_t* best_column = (word_t*) calloc(n_words_in_column, sizeof(word_t));

	word_t* covered_lines = (word_t*) calloc(n_words_in_column, sizeof(word_t));

	/**
	 * The local attribute totals
	 */
	uint32_t* attribute_totals
		= (uint32_t*) calloc(dataset.n_attributes, sizeof(uint32_t));

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
	 * Buffer to store the subtotal for each loop
	 */
	uint32_t* attribute_totals_buffer = NULL;

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

		attribute_totals_buffer
			= (uint32_t*) calloc(dataset.n_attributes, sizeof(uint32_t));

		selected_attributes = (word_t*) calloc(dataset.n_words, sizeof(word_t));

		/**
		 * No line covered so far
		 */
		n_uncovered_lines = dm.n_matrix_lines;
	}

	//*********************************************************/
	// BUILD INITIAL TOTALS
	//*********************************************************/
	for (uint32_t step = 0; step < dm.s_size; step++)
	{

		word_t* la = dataset.data + dm.steps[step].indexA * dataset.n_words;
		word_t* lb = dataset.data + dm.steps[step].indexB * dataset.n_words;

		/**
		 * Current attribute
		 */
		uint32_t c_attribute = 0;

		/**
		 * Current word
		 */
		uint32_t c_word = 0;

		// Process full words
		for (c_word = 0; c_word < n_full_words; c_word++)
		{
			word_t lxor = la[c_word] ^ lb[c_word];

			for (int8_t bit = WORD_BITS - 1; bit >= 0; bit--, c_attribute++)
			{
				attribute_totals[c_attribute] += BIT_CHECK(lxor, bit);
			}
		}

		// Process last word
		word_t lxor = la[c_word] ^ lb[c_word];

		for (int8_t bit = WORD_BITS - 1; bit >= n_last_word;
			 bit--, c_attribute++)
		{
			attribute_totals[c_attribute] += BIT_CHECK(lxor, bit);
		}
	}

	MPI_Reduce(attribute_totals, global_attribute_totals, dataset.n_attributes,
			   MPI_UINT32_T, MPI_SUM, ROOT_RANK, comm);

	//*********************************************************/
	// END BUILD INITIAL TOTALS
	//*********************************************************/

	while (true)
	{
		/**
		 * Get best attribute index
		 */
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

			/**
			 * Which word has the best attribute
			 */
			uint32_t best_word = best_attribute / WORD_BITS;

			/**
			 * Which bit?
			 */
			uint32_t best_bit = WORD_BITS - best_attribute % WORD_BITS - 1;

			/**
			 * Mark best attribute as selected
			 */
			BIT_SET(selected_attributes[best_word], best_bit);

			/**
			 * Update number of lines remaining
			 */
			n_uncovered_lines -= global_attribute_totals[best_attribute];

			/**
			 * If we covered all of them, we can leave earlier
			 */
			if (n_uncovered_lines == 0)
			{
				best_attribute = -1;
			}
		}

		/**
		 * Share with everyone
		 */
		MPI_Bcast(&best_attribute, 1, MPI_INT64_T, 0, comm);

		/**
		 * If best_attribute is -1 we are done
		 */
		if (best_attribute < 0)
		{
			goto show_solution;
		}

		/**
		 * We can get some lopsized distribution, and some process might finish
		 * earlier, but we need to participate in the mpi reduce
		 */
		if (dm.s_size == 0)
		{
			SAY("NOTHING TO DO!\n");
			goto mpi_reduce;
		}

		/**
		 * Which word has the best attribute
		 */
		uint32_t best_word = best_attribute / WORD_BITS;

		/**
		 * Which bit?
		 */
		uint32_t best_bit = WORD_BITS - best_attribute % WORD_BITS - 1;

		//***********************************************************/
		// BUILD BEST COLUMN
		//***********************************************************/
		for (uint32_t line = 0; line < dm.s_size; line++)
		{
			word_t* la = dataset.data + dm.steps[line].indexA * dataset.n_words
				+ best_word;
			word_t* lb = dataset.data + dm.steps[line].indexB * dataset.n_words
				+ best_word;

			word_t lxor = *la ^ *lb;

			if (BIT_CHECK(lxor, best_bit))
			{
				/**
				 * Where to save it
				 */
				uint32_t current_word = line / WORD_BITS;

				/**
				 * Which bit?
				 */
				uint32_t current_bit = WORD_BITS - line % WORD_BITS - 1;

				BIT_SET(best_column[current_word], current_bit);
			}
		}

		//***********************************************************/
		// END BUILD BEST COLUMN
		//***********************************************************/

		//***************************************************************/
		// UPDATE ATTRIBUTES TOTALS
		//***************************************************************/

		/**
		 * Reset attributes totals
		 */
		memset(attribute_totals, 0, dataset.n_attributes * sizeof(uint32_t));

		/**
		 * Get the totals for the uncovered lines covered by the best attribute.
		 */
		for (uint32_t line = 0; line < dm.s_size; line++)
		{
			/**
			 * Is this line covered?
			 * Yes: skip
			 * No. Is it covered by the best attribute?
			 * Yes: add
			 * No: skip
			 */
			uint32_t current_word = line / WORD_BITS;
			uint8_t current_bit	  = WORD_BITS - line % WORD_BITS - 1;

			/**
			 * Is this line already covered?
			 */
			if (BIT_CHECK(covered_lines[current_word], current_bit))
			{
				/**
				 * This line is already covered: skip
				 */
				continue;
			}

			/**
			 * Is this line covered by the best attribute?
			 */
			if (!BIT_CHECK(best_column[current_word], current_bit))
			{
				/**
				 * This line is NOT covered: skip
				 */
				continue;
			}

			/**
			 * This line was uncovered, but is covered now
			 * Calculate attributes totals
			 */

			word_t* la = dataset.data + dm.steps[line].indexA * dataset.n_words;
			word_t* lb = dataset.data + dm.steps[line].indexB * dataset.n_words;

			/**
			 * Current attribute
			 */
			uint32_t c_attribute = 0;

			/**
			 * Current word
			 */
			uint32_t c_word = 0;

			// Process full words
			for (c_word = 0; c_word < n_full_words; c_word++)
			{
				word_t lxor = la[c_word] ^ lb[c_word];

				for (int8_t bit = WORD_BITS - 1; bit >= 0; bit--, c_attribute++)
				{
					attribute_totals[c_attribute] += BIT_CHECK(lxor, bit);
				}
			}

			// Process last word
			word_t lxor = la[c_word] ^ lb[c_word];

			for (int8_t bit = WORD_BITS - 1; bit >= n_last_word;
				 bit--, c_attribute++)
			{
				attribute_totals[c_attribute] += BIT_CHECK(lxor, bit);
			}
		}

mpi_reduce:
		MPI_Reduce(attribute_totals, attribute_totals_buffer,
				   dataset.n_attributes, MPI_UINT32_T, MPI_SUM, 0, comm);

		//***************************************************************/
		// END UPDATE ATTRIBUTES TOTALS
		//***************************************************************/

		//***************************************************************/
		// UPDATE COVERED LINES
		//***************************************************************/
		for (uint32_t w = 0; w < n_words_in_column; w++)
		{
			covered_lines[w] |= best_column[w];
		}

		//***************************************************************/
		// END UPDATE COVERED LINES
		//***************************************************************/

		//***************************************************************/
		// UPDATE GLOBAL TOTALS
		//***************************************************************/

		if (rank == ROOT_RANK)
		{
			for (uint32_t i = 0; i < dataset.n_attributes; i++)
			{
				global_attribute_totals[i] -= attribute_totals_buffer[i];
			}
		}
		//***************************************************************/
		//  END UPDATE GLOBAL TOTALS
		//***************************************************************/
	}

show_solution:
	if (rank == ROOT_RANK)
	{
		fprintf(stdout, "Solution: { ");
		uint32_t current_attribute = 0;
		for (uint32_t w = 0; w < dataset.n_words; w++)
		{
			for (int8_t bit = WORD_BITS - 1; bit >= 0;
				 bit--, current_attribute++)
			{
				if (BIT_CHECK(selected_attributes[w], bit))
				{
					fprintf(stdout, "%d ", current_attribute);
				}
			}
		}
		fprintf(stdout, "}\n");
		fprintf(stdout, "All done! ");
		main_tock = time(0);
		fprintf(stdout, "[%lds]\n", main_tock - main_tick);

		free(global_attribute_totals);
		global_attribute_totals = NULL;

		free(attribute_totals_buffer);
		attribute_totals_buffer = NULL;

		free(selected_attributes);
		selected_attributes = NULL;
	}

	//  wait for everyone
	MPI_Barrier(comm);

	/**
	 * Free memory
	 */

	// Free shared steps
	MPI_Win_free(&win_shared_steps);
	dataset.data = NULL;

	// Free shared dataset
	MPI_Win_free(&win_shared_dset);
	dm.steps = NULL;

	free(best_column);
	best_column = NULL;

	free(covered_lines);
	covered_lines = NULL;

	free(attribute_totals);
	attribute_totals = NULL;

	dataset.data = NULL;
	dm.steps	 = NULL;

	free_dataset(&dataset);
	free_dm(&dm);

	/* shut down MPI */
	MPI_Finalize();

	return EXIT_SUCCESS;
}
