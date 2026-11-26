# Stage 1: one stateful component under supervision, on HPX master (161df7fc6)

Codes used in this document (full list: [plan, codes](../plan.md#codes)):

| Code | Meaning |
| --- | --- |
| F0 | no failure; the control run |
| F1 | kill idle host: SIGKILL the host while no call to its component is in flight |
| F2 | kill host mid-call: SIGKILL the host while a call to its component is in flight |
| F3 | zombie host: SIGSTOP the host until HPX fences it, then SIGCONT it |
| F4 | slow, not dead: SIGSTOP the host for less than the detection time, then SIGCONT |
| F5 | kill and replace: SIGKILL the host; a replacement joins and takes ownership of the state |
| P1 | direct call: a plain action call on the component's id |
| P2 | fenced dispatch: the same action through `dispatch_work` |
| P3 | admission: `check_admission` for the host locality at its join epoch |
| P4 | name lookup: `find_from_basename` for the component's name, then P1 on the id it returns |
| P5 | zombie answer: whether a resumed host answers, from a client that observed the fence and one that did not |
| P6 | name takeover: whether a replacement can bind the dead host's name, with and without unregistering first |
| H1 | a plain call to a component on a killed host does not resolve by itself while the host is only fenced |
| H2 | a name bound by a killed host stays bound to the dead id |
| H3 | a resumed zombie host still answers; only a check above HPX rejects its results |
| H4 | a replacement can take over a dead host's name only after the old binding is removed |
| J0 | late joiner: no failure; a new locality joins after the clients and pings the root and the client first |
| J0q | J0 without the joiner's first pings |
| F5a | F5 without the root's `force_disconnect` of the dead host |
| F5b | F5 without the replacement's first pings |
| J4 | joiner lookup: P4 for the joiner's own name, then P1 on the id it returns |

**Date:** 2026-11-26. **Cluster:** Rostam, partition `medusa`, node medusa00, one node per job.
**HPX:** master `161df7fc6a85baa20b915894d4d07c83186cccbc`. **Plan:**
[`docs/plan.md`](../plan.md) section 7.

Code: [`experiments/01_stateful_component/`](../../experiments/01_stateful_component/)
(`stateful_component.cpp`, `run_stage1.sh`, `stage1.sbatch`, `summarize.sh`).

## Summary

* **Exit criterion met.** Every cell of the stage 1 matrix recorded on `161df7fc6`, 30 trials per
  scenario, 180 trials in all; every trial completed with the client exiting cleanly. Every probe
  gave the same answer in all 30 trials of its scenario.
* **A new call to a killed host fails fast; a call in flight at the kill never returns.** After
  the fence, a plain call fails with `locality_was_disconnected` in 2 ms, before any
  `force_disconnect` (H1 falsified for new calls). Plain and fenced calls already in flight when
  the host was killed are still unanswered 30 s later.
* **Names outlive their host.** The component's name resolves to the dead host's id after the
  fence and after the root's `force_disconnect` (H2 holds).
* **A resumed zombie answers with its full state.** After SIGCONT the frozen host answers plain
  calls from both clients with the state it held (`value:5`); fenced dispatch refuses it, and
  `force_disconnect` cuts it off for everyone (H3 holds).
* **A replacement takes over the name only after unregistering the old binding** (H4 holds).
* **New finding: after `force_disconnect`, existing localities cannot reach a locality that joins
  later.** In F5 the replacement reaches the root and the client within 1 ms, yet no call from
  either client reaches the replacement, not even a plain ping at 30 s (30/30). The follow-up
  variants isolate the trigger: without the root's `force_disconnect`, or with no failure at all,
  every call reaches the newcomer (180/180); with it, 2 of 120 do (F5 and F5b). Whether the newcomer contacts the
  clients first makes no difference. Why the removal has this effect is not established.

## Setup

| Item | Value |
| --- | --- |
| Topology | root (locality 0), host (1), client (2), client2 (3), replacement (4, F5 only); one process each, one node |
| Launch | HPX connect mode, TCP parcelport on `localhost`, `--hpx:threads=4 --hpx:bind=none --hpx:ignore-batch-env` |
| Toolchain | gcc 15.1.0, cmake 3.29.2, boost 1.91.0, hwloc 2.12.0 (`bench/setup/rostam_env.sh`) |
| Detection hook | poll timeout 500 ms on every locality |
| Component | `state_holder`: a counter set to 5 by the client before the scenario, and a vector of 1024 doubles |
| Bounds | 10 s per probe; 30 s for the F2 in-flight calls and for the F5 long call (`P4.call30`) |
| F4 freeze | 200 ms |
| Timing | `std::chrono::steady_clock` in the client. Detection time runs from the moment the client sees the harness's marker file (polled every 20 ms), not from the signal itself |

| Job | Commit | Purpose |
| --- | --- | --- |
| 225622 | `3597e17` | smoke, 2 trials per scenario; results used, probe times not (see "Harness problems") |
| 225623 | `7e6f02a` | smoke, 2 trials per scenario, valid times |
| 225624 | `4b0e943` | full run, 30 trials per scenario, 1 h 13 min 51 s; every number below unless stated |
| 225626 | `d3f1b8a` | follow-up variants J0, J0q, F5a, F5b, smoke, 2 trials each |
| 225632 | `d3f1b8a` | follow-up variants, 30 trials each, 17 min 40 s |

## Results (job 225624, 30 trials each)

Each row gave the same result in 30/30 trials. Times are min and max in ms.

### Detection

| Scenario | Host seen fenced after the marker |
| --- | --- |
| F1 | 1444 to 1505 |
| F2 | 1183 to 1204 |
| F3 | 1444 to 1505 |
| F5 | 1444 to 1505 |

About three poll windows, as in langX. F2 is 240 to 320 ms shorter: there the kill comes at a fixed
offset after a dispatch, so it falls at a different point of the poll cycle. That is an inference;
the poll phase was not recorded.

### Killed host (F1, F2)

| Probe | After the fence | After `force_disconnect` |
| --- | --- | --- |
| P1, client | `error:locality_was_disconnected`, 2 ms | same, 0 ms |
| P1, client2 | `error:locality_was_disconnected`, 2 ms | same, 0 ms |
| P2 | `target_fenced`, 0 ms | same |
| P3 | `rejected_fenced` | same |
| P4, both clients | dead host's id (locality 1) | same |
| P1 on the id P4 returns, both clients | `error:locality_was_disconnected`, 2 ms | same, 0 ms |

Calls in flight at the kill (F2), a plain `slow_update` and the same through `dispatch_work`:
both `no_answer` at 30017 to 30021 ms.

`force_disconnect` by the root returned `rc=0` in every trial of F1, F2, F3 and F5.

### Zombie host (F3)

| Probe | Frozen, fenced | After SIGCONT | After `force_disconnect` |
| --- | --- | --- | --- |
| P1, client | `no_answer` at 10 s | `value:5` | `error:locality_was_disconnected` |
| P1, client2 | `no_answer` at 10 s | `value:5` | `error:locality_was_disconnected` |
| P2 | `target_fenced` | `target_fenced` | `target_fenced` |
| P3 | `rejected_fenced` | `rejected_fenced` | `rejected_fenced` |
| P4, both clients | locality 1 | locality 1 | locality 1 |
| P1 on the id P4 returns, both clients | `no_answer` at 10 s | `value:5` | `error:locality_was_disconnected` |

The second client never joined the host through supervision and sees the same answers as the
client that did. Only fenced dispatch and admission, which consult the client's supervision state,
refuse the zombie.

### Brief freeze (F4)

A 200 ms SIGSTOP caused no fence: 3 s after SIGCONT, P1, P2 and P4 answered `value:5` and P3
`admitted` for both clients.

### Replacement (F5)

| Step | Result |
| --- | --- |
| Replacement binds the name without unregistering | `refused` |
| Replacement unregisters the name | returns the dead host's id (locality 1) |
| Replacement binds again | `bound` |
| Replacement calls its own new component | `value:0` |
| Replacement pings the root / the client | `value:0` / `value:2`, 0 to 1 ms |
| Client's known localities | `0,2,3,4` |
| Client pings the root | `value:0`, 0 ms |
| Client pings the replacement (locality 4) | `no_answer` at 10 s |
| P4 after the rebind, both clients | the replacement's id (locality 4) |
| P1 on that id, client and client2 | `no_answer` at 10 s |
| The same call with a 30 s bound, client | `no_answer` at 30 s |

The rebind works, and the lookup returns the new id. The new component holds a fresh state
(`value:0`): the old state is gone with its host, as section 1.1 of the plan expects. But neither
existing locality can reach the replacement, while the replacement reaches both.

In smoke job 225623, which did not yet have the replacement's pings to the root and the client, one
of two F5 trials reached the replacement (ping `value:4`, call `value:0`) and one did not.

## Follow-up: reaching a newcomer (job 225632, 30 trials each)

Plan section 7.5. Each variant changes one thing relative to F5. The newcomer is locality 4 in
every trial. "Reached" means the client's plain ping to locality 4 answered `value:4` and the call
on the component found by name answered `value:0`; each count is out of 30.

| Variant | Host killed | Root's `force_disconnect` before the newcomer joins | Newcomer pings first | Client reached | client2 reached |
| --- | --- | --- | --- | --- | --- |
| J0 | no | no | yes | 30 | 30 |
| J0q | no | no | no | 30 | 30 |
| F5a | yes | no | yes | 30 | 30 |
| F5b | yes | yes | no | 2 | 0 |
| F5 (job 225624) | yes | yes | yes | 0 | 0 |

In every variant the client's ping to the root answered `value:0`, and the newcomer's call on its
own component answered `value:0`. In F5a the client still lists the dead host's locality
(`0,1,2,3,4`); after the root's `force_disconnect` it does not (`0,2,3,4`). In F5b the client's ping
and its call each reached the newcomer in 2 trials; whether these are the same 2 trials is not
shown by the summary.

The newcomer is reachable whenever the dead host was not removed before it joined, and almost
never when it was (2 of 60 calls across both clients in F5b, 0 of 60 in F5). The newcomer's own
first contact does not change this.

| ID | Outcome |
| --- | --- |
| H1 | Falsified for new calls: they fail in 2 ms with `locality_was_disconnected` while the host is only fenced. Holds for calls in flight at the kill, which stay unanswered at 30 s |
| H2 | Holds, 30/30, after the fence and after `force_disconnect` |
| H3 | Holds, 30/30: the zombie answers plain calls from both clients with its state; fenced dispatch refuses it |
| H4 | Holds, 30/30, for the name. The new owner is then unreachable from existing localities when the dead host was force-disconnected first (follow-up above) |

## Harness problems found on the way

| Problem | Fix |
| --- | --- |
| Probe times in job 225622 read the clock before the wait: C++ leaves the order in which a call's arguments are evaluated unspecified, and `say(..., settle(f), ms_since(t0))` evaluated the time first | Result taken first, then the time (PR #6); results of 225622 unaffected |
| F5 calls to the replacement got no answer, without telling which side failed | Plain pings in both directions, a ping to the root as control, and the known-locality list (PR #7) |

## Not established

* Why the root's `force_disconnect` of a dead locality stops existing localities from reaching a
  locality that joins later. The trigger is isolated (follow-up above); the mechanism is not. An
  inference from the source: `force_disconnect` changes each locality's record of resolved and
  disconnecting localities in `addressing_service`, and that state may stop a new connection to a
  later locality. Not measured.
* Whether a replacement that joins before the dead host is removed stays reachable after the
  removal. Stage 2 would recover in that order; not run yet.
* Why 2 of 30 F5b trials, and 1 of 2 F5 smoke trials (job 225623), reached the newcomer.
* Why a new call fails in 2 ms before `force_disconnect`. The error name matches the parcelport's
  record of a disconnected locality in `addressing_service::resolve_locality`; that the TCP
  connection reset sets this record is an inference from the source, not measured.
* The F2 detection offset: inferred from the kill's place in the poll cycle, not measured.
* Behaviour across nodes. Every process ran on one node.

## Claims and non-claims

Claims, each from 30/30 trials of job 225624 on HPX `161df7fc6`, one node, poll timeout 500 ms:

* After a host is killed and fenced, new plain calls to its component fail with
  `locality_was_disconnected`; calls already in flight do not return within 30 s.
* `dispatch_work` refuses the fenced host with `target_fenced`, and `check_admission` returns
  `rejected_fenced`, also after the host resumes.
* The component's basename binding stays on the dead host's id until it is unregistered.
* A resumed frozen host answers plain calls with the state it held, to a client that observed the
  fence and to one that did not; the root's `force_disconnect` stops that.
* A replacement can bind the name after unregistering it, and lookups then return the new id.
* After the root force-disconnects a dead locality, existing localities reach a locality that
  joins later in 2 of 120 calls (F5 and F5b, both clients); without that removal, or with no
  failure, in 180 of 180 (J0, J0q, F5a, both clients). The newcomer reaches existing localities in
  every case.

Not claimed:

* A mechanism for the unreachable newcomer.
* That the reachability result holds across nodes or on other HPX commits.
* Anything about the state itself beyond what the probes read: the zombie's answer was checked
  through the counter only.

## Next step

Run the order stage 2 would use: the replacement joins first, then the root removes the dead host,
then the clients call the replacement. Write a minimal reproducer of the unreachable newcomer for
an upstream issue. Then stage 2.
