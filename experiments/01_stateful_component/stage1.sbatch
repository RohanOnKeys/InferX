#!/bin/bash
#SBATCH --job-name=inferx-s1
#SBATCH --partition=medusa
#SBATCH --nodes=1
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --output=stage1_%j.out
#
# Build the stage 1 harness and run its scenarios on one node. Submit from
# the repository root after sourcing bench/setup/rostam_env.sh:
#   sbatch experiments/01_stateful_component/stage1.sbatch
# Optional: TRIALS (default 2, the smoke run), SCENARIOS (default F0 to F5),
# HPX_INSTALL. A full run: TRIALS=30 sbatch --time=06:00:00 ...
#
# Follows langX bench/hpx_repro/repro.sbatch.

cd "${SLURM_SUBMIT_DIR:-$PWD}"
. bench/setup/rostam_env.sh   # before set -u: Lmod is not -u safe
set -uo pipefail
: "${HPX_INSTALL:?set HPX_INSTALL to a supervision-enabled HPX install}"

E=experiments/01_stateful_component
echo "=== identity: job=${SLURM_JOB_ID:-none} node=$(hostname) commit=$(git rev-parse --short HEAD) hpx_install=$HPX_INSTALL"
B=$E/build_$(echo "$HPX_INSTALL" | md5sum | cut -c1-8)   # one per HPX install
cmake -S $E -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HPX_INSTALL" || { echo "CONFIGURE FAILED"; exit 11; }
cmake --build $B -j"$(nproc)" || { echo "BUILD FAILED"; exit 12; }
echo "=== linked: $(ldd $B/stateful_component | awk '$1 == "libhpx_core.so" {print $3; exit}')"

OUT=$E/_runs/${SLURM_JOB_ID:-local}
for S in ${SCENARIOS:-F0 F1 F2 F3 F4 F5}; do
  bash $E/run_stage1.sh $B/stateful_component $OUT "$S" "${TRIALS:-2}"
done
echo "=== done"
