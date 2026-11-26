# Reproducing the results

Every result in `docs/results/` comes from a script in this repository. This page lists, per
write-up, the command that produces it and the Rostam jobs it was first run as. All commands run
from the repository root on Rostam.

## One-time setup

| Step | Command | Notes |
| --- | --- | --- |
| Environment | `. bench/setup/rostam_env.sh` | modules gcc 15.1.0, cmake 3.29.2, boost 1.91.0, hwloc 2.12.0, Python 3.12.3; sets `HPX_INSTALL` and `LD_LIBRARY_PATH` |
| HPX | `sbatch -p medusa -N1 --exclusive -t 02:00:00 -o hpx_build_%j.out bench/setup/build_hpx.sh` | HPX master `161df7fc6` with supervision and the TCP parcelport; clone or fetch HPX on the login node first |

Source `bench/setup/rostam_env.sh` in the shell before `sbatch`; Slurm passes the environment on.

## Per result

| Write-up | What | Command | Jobs |
| --- | --- | --- | --- |
| [`stage1.md`](results/stage1.md) | smoke, 2 trials per scenario | `sbatch experiments/01_stateful_component/stage1.sbatch` | 225622 (`3597e17`), 225623 (`7e6f02a`) |
| [`stage1.md`](results/stage1.md) | full run, F0 to F5, 30 trials each | `TRIALS=30 sbatch --time=03:00:00 experiments/01_stateful_component/stage1.sbatch` | 225624 (`4b0e943`) |
| [`stage1.md`](results/stage1.md) | tables from a job log | `bash experiments/01_stateful_component/summarize.sh stage1_<job>.out` | |

Job 225622 predates the probe timing fix (PR #6); its results are valid, its probe times are not.

## Outputs

Job logs land in the directory the job was submitted from. Raw run data goes under `_runs/` next to
each script and is ignored by git. The numbers in the write-ups are copied from those logs.
