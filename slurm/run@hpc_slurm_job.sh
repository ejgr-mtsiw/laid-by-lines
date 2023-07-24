#!/bin/bash

##CHANGE THIS!

#SBATCH --job-name="P3-16-bench_dataset@hpc"
##SBATCH --time=0:10:0
##SBATCH --nodes=1
##SBATCH --ntasks-per-node=1
#SBATCH --ntasks=16

## The operation changes the dataset file used so we need to make a copy
DATASET_FILE="../../datasets/bench_dataset.hd5.original"
DATASET_NAME="dados"

## MAYBE CHANGE THIS!

EXE="../bin/laid-hdf5-mpi-lines"

## DON'T CHANGE THIS!

# Used to guarantee that the environment does not have any other loaded module
module purge

# Load software modules. Please check session software for the details
module load gcc11/libs/hdf5/1.14.0

# Disable warning for mismatched library versions
# Cirrus.8 has different hdf5 versions on short and hpc partitions
# even if we load the same module
# ##Headers are 1.14.0, library is 1.10.5
HDF5_DISABLE_VERSION_CHECK=2
export HDF5_DISABLE_VERSION_CHECK


# Run
if [ -f "$DATASET_FILE" ]; then
    if [ -f "$EXE" ]; then
        echo "=== Running ==="
        chmod u+x $EXE
        mpiexec -np $SLURM_NTASKS $EXE -d $DATASET_NAME -f $DATASET_FILE
    else
        echo "$EXE Not found!"
    fi
else
    echo "Input dataset not found! [$DATASET_FILE]"
fi

echo "Finished with job $SLURM_JOBID"
