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

No results yet.

## Outputs

Job logs land in the directory the job was submitted from. Raw run data goes under `_runs/` next to
each script and is ignored by git. The numbers in the write-ups are copied from those logs.
