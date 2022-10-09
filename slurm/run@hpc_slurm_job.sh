#!/bin/bash

##CHANGE THIS!

#SBATCH --job-name="lines-bench_dataset@hpc"
#SBATCH --time=0:10:0
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1

## The operation changes the dataset file used so we need to make a copy
DATASET_FILE="../../datasets/bench_dataset.hd5.original"
DATASET_NAME="dados"

## MAYBE CHANGE THIS!

EXE="../bin/laid-hdf5-mpi-lines"

# Used to guarantee that the environment does not have any other loaded module
module purge

# Load software modules. Please check session software for the details
module load hdf5/1.12.0
##module load gcc83/openmpi/4.1.1
module load clang/openmpi/4.0.3

## DON'T CHANGE THIS!

# Be sure to request the correct partition to avoid the job to be held in the queue, furthermore
#	on CIRRUS-B (Minho)  choose for example HPC_4_Days
#	on CIRRUS-A (Lisbon) choose for example hpc
#SBATCH --partition=hpc

# Run
echo "=== Copy dataset ==="
if [ -f "$DATASET_FILE" ]; then
    echo "=== Running ==="
    if [ -f "$EXE" ]; then
        chmod u+x $EXE
        mpiexec --display-map -np $SLURM_NTASKS $EXE -d $DATASET_NAME -f $DATASET_FILE
    fi
else
    echo "Input dataset not found! [$DATASET_FILE]"
fi

echo "Finished with job $SLURM_JOBID"
