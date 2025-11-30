# Transactional Barriers for Matrix Profile Algorithms

## Overview

This repository contains an experimental framework to study **transactional memory–based barriers** in the context of **matrix profile** algorithms for time series analysis. The code is built around SCAMP-like matrix profile implementations, extended with custom transactional barriers and detailed hardware statistics collection.

The project is organised in two main variants:

- `IBM/`: implementation targeting IBM POWER / HTM intrinsics (via `htmintrin.h`).
- `Intel/`: implementation targeting Intel TSX / RTM intrinsics.

Both variants share the same high-level algorithmic structure but use different low-level transactional primitives and barrier implementations, which enables a comparative study of the behaviour and performance of transactional barriers across architectures.

Typical use cases for this code include:

- Evaluating the performance of transactional barriers versus traditional synchronisation mechanisms in matrix profile computation.
- Exploring scalability of SCAMP-style matrix profile on many-core systems.
- Collecting detailed statistics about hardware transactional memory (HTM) usage, abort reasons, and fallbacks.

> **Status:** This code base is research-oriented and primarily intended for experimentation and reproducibility of results, rather than for production deployment.

## Background: Matrix Profile and SCAMP

The **matrix profile** of a time series is a data structure that stores, for each subsequence, the distance (or similarity) to its nearest neighbour subsequence. It is widely used for motif discovery, anomaly detection, and pattern mining in time series.

The implementations here are based on a **SCAMP-like** algorithm (Scalable Matrix Profile) that:

- Uses sliding-window statistics (`means`, `norms`, `df`, `dg`) to compute z-normalised distances efficiently.
- Traverses the distance matrix along diagonals and/or tiles to exploit cache locality and parallelism.
- Exploits **OpenMP** for parallelisation across CPU cores.

The **transactional barriers** introduced in this project are used to coordinate threads while minimising explicit locking, by relying on HTM/RTM support when available. This is particularly relevant when multiple threads concurrently update shared data structures such as the global matrix profile (`profile`, `profileIndex`).

## Repository Layout

At the top level:

- `README.md` – this document.
- `IBM/` – IBM/POWER-oriented implementation and experiments.
- `Intel/` – Intel TSX-oriented implementation and experiments.

Each architecture-specific directory has a similar structure:

- `Makefile` – build rules for the different executables.
- `scamp.cpp` – baseline SCAMP-style matrix profile implementation.
- `scampTilesDiag.cpp` – tiled/diagonal SCAMP implementation without transactional barriers.
- `scampTilesUnprot.cpp` – tiled implementation without protection of shared data (for comparison).
- `specScampTilesDiag.cpp` – **speculative / transactional** tiled implementation using the custom barriers and statistics.
- `lib/`
	- `barriers.c`, `barriers.h` – implementation of transactional barriers and supporting data structures.
	- `stats.c`, `stats.h` – collection and reporting of HTM/RTM statistics (abort reasons, commits, fallbacks, retries, etc.).
	- `htmintrin.h` (IBM only) – IBM HTM intrinsics header.
- `timeseries/` – input time series used in experiments (e.g. `power_demand.txt`, `seismology-*.txt`, `audio-*.txt`, etc.).
- `results/` – output directory for matrix profile results.
- `stats/` – output directory for transactional statistics.
- `figs/` – Python and shell scripts to post-process results and generate plots.
- `z_run.sh` – helper script to run a suite of experiments over different time series and thread counts.

## Architecture-Specific Notes

### IBM variant (`IBM/`)

- Uses `htmintrin.h` and POWER HTM intrinsics.
- Transactional primitives and barrier macros are defined in `lib/barriers.h` and implemented in `lib/barriers.c`.
- Statistics are collected through `lib/stats.h` / `lib/stats.c`, which track, for each transaction and thread:
	- number of commits and fallbacks;
	- total aborts and categorised abort reasons (capacity, conflicts, explicit aborts, etc.);
	- number of retries.
- The executable `specScampTilesDiag` uses transactional barriers to protect shared updates to the global matrix profile while allowing optimistic concurrency.

### Intel variant (`Intel/`)

- Uses Intel TSX/RTM intrinsics with `-mrtm` and `-qopenmp` in the `Makefile`.
- Barrier macros in `lib/barriers.h` map to Intel-specific transactional instructions (e.g. `_xbegin`, `_xend`, `_xsusldtrk`, `_xresldtrk`) and a fallback lock.
- The structure and purpose of `specScampTilesDiag.cpp` mirrors the IBM version but adapted to Intel intrinsics and cache-line sizes.

## Building the Code

### Prerequisites

- A C++11-capable compiler (e.g. `g++`).
- OpenMP support (e.g. via `-fopenmp` or Intel `-qopenmp`).
- A system with hardware transactional memory support if you want to run the transactional variants:
	- IBM POWER with HTM for `IBM/`.
	- Intel CPU with TSX/RTM for `Intel/`.

> The code can still be compiled on systems without HTM support, but transactional sections may always fall back to the lock-based path or may not be available depending on your toolchain.

### Build targets

From within `IBM/`:

```bash
cd IBM
make
```

This produces the following executables:

- `scamp` – baseline SCAMP matrix profile.
- `scampTilesDiag` – tiled diagonal SCAMP.
- `scampTilesUnprot` – tiled implementation without protection of shared profile updates.
- `specScampTilesDiag` – transactional/tiled SCAMP using IBM HTM-based barriers and statistics.

From within `Intel/`:

```bash
cd Intel
make
```

This produces analogous executables, but built for an Intel environment and linked with Intel’s OpenMP runtime and RTM support.

To clean builds and generated outputs:

```bash
cd IBM
make clean

cd ../Intel
make clean
```

## Input Data and Matrix Profile Computation

The `timeseries/` directory (in both `IBM/` and `Intel/`) contains several benchmark time series used in the experiments, including:

- Power demand series (`power_demand.txt`, `power-MPIII-SVF*.txt`).
- Seismology traces (`seismology-MPIII-SVE*.txt`).
- Audio, human activity, taxi trajectories, and synthetic random series.

Each executable expects:

- an input time series file path; and
- a window size (subsequence length).

For example, a typical invocation may look like (IBM variant):

```bash
./scampTilesDiag ./timeseries/power_demand.txt 1325
```

or, for the transactional variant:

```bash
./specScampTilesDiag ./timeseries/seismology-MPIII-SVE.txt 50
```

The exact command-line options depend on the implementation in the corresponding `*.cpp` files; they usually include parameters such as:

- time series path
- subsequence/window length
- exclusion zone
- number of OpenMP threads

Refer to the top of each `main` function for the precise parameter order and defaults.

Matrix profile results are typically written to the `results/` directory, often together with metadata such as execution time and configuration parameters.

## Transactional Barriers and Statistics

The core research contribution of this repository is the implementation and evaluation of **transactional barriers** built on top of HTM/RTM.

Key components:

- `lib/barriers.h` / `lib/barriers.c`
	- Define a global `g_specvars` structure that holds barrier state (e.g. `nb_threads`, `remain`).
	- Provide macros to initialise per-thread transactional descriptors (`TX_DESCRIPTOR_INIT`) and global barrier metadata (`BARRIER_DESCRIPTOR_INIT`).
	- Implement the logic to enter and leave transactional regions, retry failed transactions, and fall back to a lock when necessary.
- `lib/stats.h` / `lib/stats.c`
	- Define a `Stats` structure and a global `stats` array, indexed by thread and transaction identifier.
	- Track counts of commits, fallbacks, and aborts, detailed by abort reason (capacity, conflicts, explicit aborts, etc.).
	- Provide `statsFileInit` and `dumpStats` functions to initialise statistics files and write summaries at the end of a run.

In `specScampTilesDiag.cpp`, transactional barriers are used around updates to the shared `profile` and `profileIndex` arrays along the diagonals/tiles of the distance matrix, enabling threads to speculatively execute in parallel while maintaining correctness.

## Running Batch Experiments

Both `IBM/` and `Intel/` include a helper script `z_run.sh` that automates running experiments over multiple benchmarks and thread counts.

The script:

- sets `OMP_PLACES=cores` to control OpenMP thread placement;
- defines a list of benchmark `(time_series_path, window_size)` pairs; and
- loops over a set of thread counts (e.g. `1 2 4 8 16 32 64 128`).

To run the IBM batch script, for example:

```bash
cd IBM
chmod +x z_run.sh
./z_run.sh
```

The script will populate the `results/` and `stats/` directories with outputs for further analysis.

## Plotting and Post-Processing

The `figs/` directory contains utilities for visualising and post-processing experimental results:

- `newplotscamp.py` – Python script that parses output files in `results/` and `stats/` and generates plots (e.g. speedup, scalability, abort breakdown).
- `zgen_plots.sh` – wrapper script to run the plotting pipeline.

Usage typically involves running the experiments first and then calling the plotting script from within `figs/`. Inspect the Python script to adapt the plotting logic to your own datasets or naming conventions.

## Reproducibility and Experimental Configuration

When reporting results or reproducing experiments, it is important to document:

- CPU model and microarchitecture (including HTM/TSX availability and configuration).
- Compiler version and flags (in particular optimisation level, OpenMP, and HTM/RTM options).
- Operating system and kernel version.
- Number of physical cores and logical threads used (`OMP_NUM_THREADS`, `OMP_PLACES`, etc.).
- Exact input time series and window sizes.

The `Makefile` and scripts in this repository encode the baseline configuration used in the original experiments, but you may need to adjust flags for your specific environment.

## Limitations and Notes

- The code targets research use and may assume a relatively controlled environment (e.g. no oversubscription, dedicated machine).
- HTM/RTM behaviour is highly hardware-dependent; abort rates and performance may vary significantly across microarchitectures and even across runs.
- The implementations focus on **throughput and scalability**, not on generic usability or error handling.
- The set of time series included in `timeseries/` is not exhaustive; you can add your own datasets following the same format (usually one value per line).

## How to Cite

If you use this code or build upon its ideas in academic work, please consider citing the repository. A generic citation entry might look like:

> Author(s), *Transactional Barriers for Matrix Profile Algorithms*, experimental code repository, `https://github.com/pepesanyu00/transactional_barriers`.

You can adapt the exact reference format to the requirements of your venue (conference, journal, thesis, etc.) and include additional bibliographic details if available.

## Contact and Contributions

This repository is primarily maintained as part of a research effort on transactional memory and time series analytics.

- For questions about the code or experiments, please open an issue in the GitHub repository.
- Contributions (bug fixes, portability improvements, documentation updates) are welcome via pull requests, especially if they clarify behaviour on new architectures or compilers.
