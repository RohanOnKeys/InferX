#!/bin/bash
# Summarize a stage 1 job log: one line per distinct (scenario, phase, probe,
# role, result) with its count and the min and max of t_ms. The tables in
# docs/results/stage1.md are read from this output.
#
#   bash experiments/01_stateful_component/summarize.sh stage1_<job>.out

set -euo pipefail
LOG=$1

grep -E '^=== ' "$LOG"
grep RESULT "$LOG" | awk '{print $NF, $(NF-1)}' | sort | uniq -c
grep -h '^R:' "$LOG" | sed 's/^R: //' | awk '{
  t=$6; sub(/t_ms=/,"",t); k=$1" "$2" "$3" "$4" "$5
  n[k]++; if(!(k in lo)||t+0<lo[k]) lo[k]=t+0; if(t+0>hi[k]) hi[k]=t+0
} END { for(k in n) printf "%s n=%d t_ms=[%.0f,%.0f]\n", k, n[k], lo[k], hi[k] }' \
| sort -t' ' -k1,1 -k2,2 -k3,3 -k4,4 | grep -v 'phase=setup\|phase=end'
