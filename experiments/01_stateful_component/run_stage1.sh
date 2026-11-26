#!/bin/bash
# Run one stage 1 scenario TRIALS times on this host and print its R: lines.
#
#   run_stage1.sh BIN OUTDIR SCENARIO TRIALS [extra --r-* options]
#
# Each trial: fresh directory and ports; root, host, client and client2 in
# that order; the scenario's fault on the host; the replacement in F5; then
# the R: lines of every role. Every wait is bounded and a hang is reported.
#
# Follows langX bench/hpx_repro/run_repro.sh.

set -u
BIN=$1 OUT=$2 SCENARIO=$3 TRIALS=$4
shift 4
EXTRA="$*"
COMMON="--hpx:bind=none --hpx:ignore-batch-env --hpx:threads=4"
H=localhost
STOP_MS=${STOP_MS:-200}   # F4: freeze shorter than one poll window (500 ms)

port() { python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])'; }
wait_f() { for _ in $(seq 1200); do [ -f "$1" ] && return 0; sleep 0.05; done; return 1; }

for TRIAL in $(seq 1 "$TRIALS"); do
  D=$OUT/${SCENARIO}_trial$TRIAL
  rm -rf "$D"; mkdir -p "$D"
  RP=$(port)
  ARGS="--r-dir $D --r-scenario $SCENARIO $EXTRA"
  start() {
    "$BIN" --r-role "$1" --hpx:agas=$H:$RP --hpx:hpx=$H:$(port) \
      $COMMON $ARGS > "$D/$1.log" 2>&1 &
  }
  echo "== $SCENARIO trial $TRIAL"

  "$BIN" --r-role root --hpx:agas=$H:$RP --hpx:hpx=$H:$RP \
    --hpx:expect-connecting-localities $COMMON $ARGS > "$D/root.log" 2>&1 &
  ROOT=$!
  if ! wait_f "$D/root.ready"; then echo "RESULT $SCENARIO trial $TRIAL: root did not start"; kill -9 $ROOT; continue; fi
  start host; HOST=$!
  wait_f "$D/host.ready" || echo "RESULT $SCENARIO trial $TRIAL: host not ready"
  start client; CLIENT=$!
  wait_f "$D/client.ready" || echo "RESULT $SCENARIO trial $TRIAL: client not ready"
  start client2; C2=$!
  wait_f "$D/client2.ready" || echo "RESULT $SCENARIO trial $TRIAL: client2 not ready"
  REPL=
  touch "$D/go"
  wait_f "$D/client.armed" || echo "RESULT $SCENARIO trial $TRIAL: client not armed"

  case $SCENARIO in
    F1) kill -9 $HOST; touch "$D/host.killed" ;;
    F2) wait_f "$D/client.dispatched"; sleep 0.3; kill -9 $HOST; touch "$D/host.killed" ;;
    F3) kill -STOP $HOST; touch "$D/host.stopped"
        wait_f "$D/client.fenced" || echo "RESULT $SCENARIO trial $TRIAL: client never saw the fence"
        kill -CONT $HOST; touch "$D/host.resumed" ;;
    F4) kill -STOP $HOST; touch "$D/host.stopped"
        sleep "$(awk "BEGIN{print $STOP_MS/1000}")"
        kill -CONT $HOST; touch "$D/host.resumed" ;;
    F5) kill -9 $HOST; touch "$D/host.killed"
        if wait_f "$D/client.want_replacement"; then start replacement; REPL=$!; fi ;;
  esac

  # Bounded wait for the client; 300 s covers every probe bound in sequence.
  for _ in $(seq 3000); do kill -0 $CLIENT 2>/dev/null || break; sleep 0.1; done
  if kill -0 $CLIENT 2>/dev/null; then EXIT=hung; else wait $CLIENT; EXIT=rc$?; fi
  grep -h '^R:' "$D"/*.log
  echo "RESULT $SCENARIO trial $TRIAL: client exit=$EXIT done=$([ -f "$D/client.done" ] && echo yes || echo no)"
  kill -9 $ROOT $HOST $CLIENT $C2 $REPL 2>/dev/null
  wait 2>/dev/null
done
