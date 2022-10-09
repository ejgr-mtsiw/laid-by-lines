#!/bin/bash

#SBATCH --job-name=run@hpc
#SBATCH --time=0:0:5
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=16

# Be sure to request the correct partition to avoid the job to be held in the queue, furthermore
#	on CIRRUS-B (Minho)  choose for example HPC_4_Days
#	on CIRRUS-A (Lisbon) choose for example hpc
#SBATCH --partition=hpc

# Used to guarantee that the environment does not have any other loaded module
module purge

# Load software modules. Please check session software for the details
module load hdf5/1.12.0
##module load gcc83/openmpi/4.1.1
module load clang/openmpi/4.0.3

#Prepare
exe="../bin/laid-hdf5-mpi"
dsetname="dados"
dsetfile="../datasets/bench_dataset.h5"

# Run
echo "=== Running ==="
if [ -e $exe ]; then
    chmod u+x $exe
    mpiexec --display-map -np $SLURM_NTASKS $exe -d $dsetname -f $dsetfile
fi

echo "Finished with job $SLURM_JOBID"