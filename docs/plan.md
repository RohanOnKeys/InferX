# Recoverable Resident State on HPX: Experiment Plan

## Codes

Every short code this project defines, named once here. Each other document repeats the ones it
uses in a table under its title, with a link back here.

Failure scenarios (matrices in sections 7.3 and 8.5). "Host" is the locality that holds the
resident state under test.

| Code | Scenario | What is done |
| --- | --- | --- |
| F0 | No failure | Nothing; the control run |
| F1 | Kill idle host | SIGKILL the host while no call to its component is in flight |
| F2 | Kill host mid-call | SIGKILL the host while a call to its component is in flight |
| F3 | Zombie host | SIGSTOP the host until HPX fences it, then SIGCONT it |
| F4 | Slow, not dead | SIGSTOP the host for less than the detection time, then SIGCONT |
| F5 | Kill and replace | SIGKILL the host; a replacement locality joins and takes ownership of the state |
| F6 | Kill during snapshot | SIGKILL a host while it takes a snapshot (stage 2) |
| F7 | Kill neighbours | SIGKILL the hosts of two adjacent partitions in one step (stage 2) |

Stage 1 follow-up variants (section 7.5):

| Code | Variant | What is done |
| --- | --- | --- |
| J0 | Late joiner | No failure; a new locality joins after the clients, binds its own name and pings the root and the client first |
| J0q | Quiet late joiner | J0 without the joiner's first pings |
| F5a | Replace without removal | F5 without the root's `force_disconnect` of the dead host |
| F5b | Quiet replacement | F5 without the replacement's first pings |

Probes: what stage 1 records after each scenario (section 7.2).

| Code | Probe | Records |
| --- | --- | --- |
| P1 | Direct call | A plain action call on the component's id: value, error, or no answer within the bound |
| P2 | Fenced dispatch | The same action through `dispatch_work`: value, `target_fenced` error, other error, or no answer |
| P3 | Admission | `check_admission` for the host locality at its join epoch: `admitted` or `rejected_fenced` |
| P4 | Name lookup | `find_from_basename` for the component's name, and P1 on the id it returns |
| P5 | Zombie answer | Whether a resumed host answers P1 and P2, seen from the client that observed the fence and from one that did not |
| P6 | Name takeover | Whether a replacement can bind the dead host's name, with and without unregistering it first, and what P4 returns afterwards |
| J4 | Joiner lookup | P4 for the joiner's own name, and P1 on the id it returns (J0, J0q) |

Gates: checks every stage 2 run must pass (section 9).

| Code | Gate | Passes when |
| --- | --- | --- |
| G1 | Correct result | The final field equals the failure-free reference, value for value |
| G2 | Single owner | At most one owner epoch per partition is accepted at any step |
| G3 | No stale write | No state or boundary exchange from a fenced owner epoch is accepted |
| G4a | Run completes | The run finishes within a bound |
| G4b | Exit paths | Recorded, not gated: how each process left (clean, timeout, killed by the harness) |
| G5 | Detection from HPX only | Every fence comes from HPX state or an HPX dispatch error, never from the harness |

Hypotheses (section 4):

| Code | Hypothesis |
| --- | --- |
| H1 | A plain call to a component on a killed host does not resolve by itself while the host is only fenced |
| H2 | A name bound by a killed host stays bound to the dead id |
| H3 | A resumed zombie host still answers; only a check above HPX rejects its results |
| H4 | A replacement can take over a dead host's name only after the old binding is removed |
| H5 | Owner epochs and snapshots give the failure-free result under F1 to F7 |
| H6 | Recovery time follows the number of recomputed steps |
| H7 | Failure-free cost of owner epochs and snapshots is small relative to a stencil step |

Source tags: **[langX]** marks a behaviour measured in langX; **[src]** marks a statement read in
the HPX source at `161df7fc6` and not yet measured; **[s1]** to **[s4]** mark a behaviour measured
in that InferX stage.

Status: stage 1 done ([`results/stage1.md`](results/stage1.md)); its follow-up (section 7.5)
isolated the trigger of the unreachable replacement; stage 2 next. This plan is the single source of truth for scope; findings from each
stage are folded back into it.

---

## 0. Sources

| Source | Read | Notes |
| --- | --- | --- |
| [langX](https://github.com/RohanOnKeys/langX) plan, report and results | Fully | Behaviour carried over in section 3.2 |
| [RayX](https://github.com/byekrang/RayX) README and experiment 70 | Through langX | Origin of the supervision harness |
| HPX `supervision` and `supervision_dispatch` at `161df7fc6` | `supervision_api.hpp`, `dispatch_work.hpp`, `testing.hpp` | Sections 3.1 and 3.3 |
| HPX AGAS and basename registration at `161df7fc6` | `basename_registration*.hpp`, `basename_registration.cpp`, `symbol_namespace.cpp`, `locality_namespace_server.cpp`, `addressing_service.cpp` | Section 3.3 |
| HPX checkpoint at `161df7fc6` | `libs/full/checkpoint/include/hpx/checkpoint/checkpoint.hpp` | Section 6.3 |
| HPX migration and resiliency at `161df7fc6` | `migrate_component.hpp`, `async_replay_distributed.hpp`, `resiliency_cpos.hpp` | Section 3.3 |
| HPX `examples/1d_stencil/` at `161df7fc6` | `1d_stencil_5` to `_8` | Section 8.1 |

---

## 1. Goal

When an HPX locality that holds resident state dies, find out:

1. what logical state was lost;
2. whether an older incarnation of that locality can still produce results;
3. how a replacement safely takes ownership of reconstructed state.

HPX should not need to understand application state (a KV cache, a stencil partition). It may need
clear identity, failure and ownership semantics so that the layer above can recover that state.
The result is useful either way: if current HPX semantics are enough, the stages show how to use
them; if not, they show which piece is missing, and that goes upstream.

Out of scope: section 5.

### 1.1 Terminology

| Term | Meaning here |
| --- | --- |
| Resident state | Application data that lives in a component on one locality across many calls (a stencil partition, a KV cache) |
| Host | The locality that holds a piece of resident state |
| Incarnation | One run of a host process. A restarted process is a new incarnation with a new locality id and join epoch |
| Join epoch | The HPX supervision epoch a locality has when the coordinator joins it |
| Owner epoch | A counter per piece of state, kept above HPX, increased on every change of owner. Separate from the join epoch |
| Owner | The (host, owner epoch) pair currently allowed to change a piece of state |
| Snapshot | A serialized copy of a piece of state at a known step, kept off its host |
| Coordinator | The locality that keeps the ownership table and decides recovery. Late-connecting, as in langX |
| Root | Work-free HPX locality 0. Anchors AGAS and runs no application work. Outside the failure scope |

Consequences:

- Losing a host loses its state since the last snapshot. Recovery is a rebuild from the snapshot
  plus recomputation, never a retry of the last call.
- A zombie host can still hold a complete, plausible copy of the state. Only the owner epoch tells
  it apart from the current owner.

---

## 2. Where this comes from

### 2.1 Line of work

RayX asked how much of the distributed execution Ray is usually used for could run on HPX instead,
especially for fine-grained AI and LLM workloads. Its experiment 70 established HPX locality
supervision. langX used that supervision to run LangGraph agent steps with no lost step, no double
commit and no stale commit under nine failure scenarios. langX steps were stateless: recovery was
re-dispatch from the last committed state. InferX moves to state that lives on a locality.

Ray stays out of the core experiments, so that every recovery is attributable to HPX and the
ownership layer above it, not to an outer restart.

### 2.2 Reused from langX

Files derived from langX name the source file in their header.

| langX path | Use here |
| --- | --- |
| `bench/setup/rostam_env.sh`, `build_hpx.sh` | Environment and HPX build, pinned to `161df7fc6` |
| `bench/hpx_repro/` | Pattern for the stage 1 harness: one C++ binary with roles, a bash runner, handshake files on one node |
| `python/src/langx/island.py`, `signal_agent.py` | Launching localities across nodes, signals, remote ready files (stage 2 onward) |
| `experiments/01_durable_langgraph/run_exp01.py`, `analyze_exp01.py` | One process per trial, fault injection, gates, JSON lines, aggregate |
| `bench/check_md_links.py`, `.github/workflows/ci.yml` | Link check and CI |

---

## 3. HPX as used here

### 3.1 Calls

| Area | Calls | Header at `161df7fc6` |
| --- | --- | --- |
| Supervision | `init`, `finalize`, `discover_and_join`, `snapshot_peers`, `query_state`, `await_terminal`, `check_admission` | `hpx/supervision/supervision_api.hpp` |
| Fenced dispatch | `dispatch_work<Action>(target, epoch, args...)`; failure surfaces as `hpx::error::target_fenced` | `hpx/supervision_dispatch/dispatch_work.hpp` |
| Detection hook | `testing::set_failure_detection_poll_timeout_for_testing` | `hpx/supervision_dispatch/testing.hpp` |
| Membership | `hpx::force_disconnect` | as in langX |
| Components | `component_base`, `HPX_REGISTER_COMPONENT`, `hpx::new_<>`, `client_base` | as in `1d_stencil_5` to `_8` |
| Naming | `register_with_basename`, `find_from_basename`, `unregister_with_basename` | `hpx/components/basename_registration_fwd.hpp` |
| Snapshots | `hpx::util::save_checkpoint`, `restore_checkpoint` | `hpx/checkpoint/checkpoint.hpp` |

### 3.2 Behaviour carried over from langX

Measured in langX. The commit is stated for each row; rows marked "both" were measured on
`cc2be553` and re-checked on `161df7fc6`.

| Behaviour | Finding | Commit | Source |
| --- | --- | --- | --- |
| SIGKILL detection | Three poll windows: 746, 1496, 2997 ms at 250, 500, 1000 ms poll timeout | `cc2be553` | [langX] |
| Dispatch in flight to a killed locality | Never completes; still not ready 30 s after the kill. Open on [hpx#7834](https://github.com/TheHPXProject/hpx/issues/7834) | both | [langX] |
| `check_admission` with a wrong epoch | Admitted for epochs 0, 2 and 101 against join epoch 1. Open on hpx#7834 | both | [langX] |
| Resumed SIGSTOP locality | Its late result arrives as `ok`; its own re-check admits new work; only the caller's view rejects it | `cc2be553` | [langX] |
| SIGSTOP shorter than one poll window | No eviction | `cc2be553` | [langX] |
| Locality leaving right after `finalize()` | Aborted on `cc2be553` ([hpx#7825](https://github.com/TheHPXProject/hpx/issues/7825)); exits cleanly on master after [#7835](https://github.com/TheHPXProject/hpx/pull/7835) | both | [langX] |
| Dead observer and a healthy target | Target marked failed on `cc2be553`; stays running on master after [#7897](https://github.com/TheHPXProject/hpx/pull/7897) | both | [langX] |
| `force_disconnect` from a late-connecting locality | Refused on `cc2be553`; `rc = 0` on master after [#7901](https://github.com/TheHPXProject/hpx/pull/7901) | both | [langX] |
| `snapshot_peers()` after a peer is removed | Still listed on `cc2be553`; gone on master after [#7922](https://github.com/TheHPXProject/hpx/pull/7922) and [#8027](https://github.com/TheHPXProject/hpx/pull/8027) | both | [langX] |
| Workers after the coordinator is lost | Fenced on `cc2be553`; not fenced on master | both | [langX] |
| HPX-only round trip on one node | 75.6 µs `hpx::async`, 83.0 µs `dispatch_work` | `161df7fc6` | [langX] |

### 3.3 What the source says about host loss

Read in the HPX source at `161df7fc6`. Rows that stage 1 measured are repeated with the result in
3.4.

| Topic | Reading | Source |
| --- | --- | --- |
| Admission for a component | `dispatch_work` checks admission against `get_locality_from_id(target)`, so fencing the host locality also refuses dispatch to its components | [src] |
| Admission for an unknown target | `check_admission` returns `admitted` for a target it has no latch for, and never checks the epoch against the join epoch | [src] |
| Re-check on the target | `invoke_fenced_action` re-checks `check_admission(hpx::find_here(), epoch)`, the host's view of itself; a resumed zombie still sees itself as live | [src] |
| Where basename names live | A basename binding is the AGAS name `/<basename>/<n>`, stored on locality `hash(name) % get_initial_num_localities()`. That count is `hpx.localities`, default 1; with `--hpx:ignore-batch-env` and no `--hpx:localities`, every locality maps names to locality 0, the root, so names survive a host's death | [src] |
| Names after locality removal | `locality_namespace::free` unbinds the removed locality's service instances, not names that point at its objects, so a dead host's names stay bound to its ids | [src] |
| Lookup of an unbound name | `find_from_basename` waits on `on_symbol_namespace_event`; it becomes ready only once the name is bound, so a lookup needs a bound | [src] |
| Rebinding a name | `register_with_basename` fails if the sequence number is already bound | [src] |
| Calls after `force_disconnect` | Resolving a locality that AGAS removed throws `locality_was_disconnected` instead of a generic error | [src] |
| Snapshots of components | `save_checkpoint` accepts a component client and serializes the server object through `hpx::get_ptr`, which needs the component on the calling locality; the resulting `checkpoint` is serializable and can be sent as a value | [src] |
| Migration | `hpx::components::migrate` moves a live component; it gives nothing once the source is dead | [src] |
| Resiliency | Distributed `async_replay` re-runs an action on the next locality when a call fails; it is a retry for stateless actions and depends on the failed call returning an error | [src] |

### 3.4 Measured in stage 1

Job 225624, 30 trials per scenario, one node, poll timeout 500 ms; full tables in
[`results/stage1.md`](results/stage1.md).

| Topic | Finding | Source |
| --- | --- | --- |
| New call to a killed, fenced host | Fails in 2 ms with `locality_was_disconnected`, before `force_disconnect`, from a client that joined the host and from one that did not | [s1] |
| Call in flight at the kill | Plain and fenced: no answer at 30 s | [s1] |
| Fenced dispatch and admission after the fence | `target_fenced` and `rejected_fenced`, also after the host resumes | [s1] |
| Name of a dead host's component | Still bound to the dead id after the fence and after `force_disconnect` | [s1] |
| Resumed zombie | Answers plain calls with its full state, to both clients; `force_disconnect` cuts it off | [s1] |
| Brief freeze (200 ms) | No fence | [s1] |
| Name takeover | Bind refused while the old binding exists; unregister returns the dead id; rebind succeeds; lookups return the new id | [s1] |
| Reaching a newcomer | After the root force-disconnects a dead locality, existing localities reach a locality that joins later in 2 of 120 calls; without that removal, or with no failure, in 180 of 180. The newcomer reaches them in every case. Mechanism not established | [s1] |

### 3.5 Design rules that follow

1. **Never wait unbounded on a call to a host.** Abandon it once HPX shows the host fenced, and
   record the abandonment [langX].
2. **Owner epochs are checked above HPX.** HPX admission does not check epochs [langX], so every
   message that carries state carries (piece, owner epoch) and the receiver checks it.
3. **Never match on error strings.** Map every call exception to "fenced" or "failed".
4. **Epochs only increase.** An owner epoch is never reused or decreased.
5. **Detection comes from HPX.** The harness knows when it killed a process; the system under test
   does not use that knowledge.
6. **Names are not proof of life.** A name can outlive its host [src]; the ownership table, not
   AGAS, says who owns a piece of state.

---

## 4. Hypotheses

| ID | Hypothesis | Stage | Falsified if | Outcome |
| --- | --- | --- | --- | --- |
| H1 | A plain call to a component on a killed host does not resolve by itself while the host is only fenced (from langX's in-flight result) | 1 | P1 returns a value or an error within the bound in F1 before `force_disconnect` | Falsified for new calls (error in 2 ms); holds for calls in flight at the kill [s1] |
| H2 | A name bound by a killed host stays bound to the dead id [src] | 1 | P4 returns no id, or a live one, after F1 | Holds, 30/30 [s1] |
| H3 | A resumed zombie host still answers; only a check above HPX rejects its results [langX] | 1 | P5 shows no answer from the zombie in F3 | Holds, 30/30 [s1] |
| H4 | A replacement can take over a dead host's name only after the old binding is removed [src] | 1 | P6 binds without unregistering, or fails after unregistering | Holds, 30/30 [s1] |
| H5 | Owner epochs and snapshots give the failure-free result under F1 to F7 | 2 | Any run fails G1, G2 or G3 | Open |
| H6 | Recovery time follows the number of recomputed steps | 2 | Recovery time does not grow with recomputed steps | Open |
| H7 | Failure-free cost of owner epochs and snapshots is small relative to a stencil step | 2 | Cost above 10 percent of step time at the chosen grain size | Open |

H1 to H4 were expectations from langX and the source; the outcomes come from stage 1, job 225624.

---

## 5. Non-goals

- Root-loss recovery (out of scope since RayX experiment 70).
- Coordinator-loss recovery in stages 1 and 2. The coordinator holds the snapshots; its loss is a
  later stage.
- Process checkpointing (CRIU and similar): a restored process is exactly the stale incarnation the
  owner epoch must reject.
- Accelerators and GPU memory before stage 4.
- Performance claims beyond recovery cost and failure-free overhead.
- Ray as part of any recovery measured in stages 1 and 2.
- Model quality of any kind in stage 4.

---

## 6. Architecture

### 6.1 Processes

```text
Root, locality 0              work-free; anchors AGAS and the basename names; outside the failure scope
Coordinator, late-connecting  ownership table: piece of state -> (host, join epoch, owner epoch);
                              joins every host; decides recovery; holds the snapshots
Hosts 1..N, late-connecting   hold resident state in components; serve actions on it
Spare hosts                   started idle; take ownership on recovery (F5)
```

Stage 1 uses a reduced form: root, one host, two clients and a replacement (section 7.1).

### 6.2 Ownership

- The coordinator is the only writer of the ownership table.
- Changing an owner: see the old host fenced through HPX, increase the owner epoch, create the
  state on the new host from a snapshot, rebind the name, then publish the new owner.
- Every state-carrying call and reply carries (piece, owner epoch). A receiver drops anything whose
  owner epoch is not the current one, and counts it.

### 6.3 Snapshots

- Taken every k steps by each host with `save_checkpoint` on its own components, since
  `save_checkpoint` needs the component local [src]. The `checkpoint` value is sent to the
  coordinator, which keeps it in memory.
- Not on the shared file system: langX saw files written on one Rostam node appear about 30 s late
  on another.
- A snapshot counts only once the coordinator holds it complete for every partition at that step;
  F6 tests a kill before that point.

---

## 7. Stage 1: one stateful component under supervision

### 7.1 Roles

One C++ binary with roles, started by a bash runner with handshake files on one node, following
langX `bench/hpx_repro/`:

| Role | What it does |
| --- | --- |
| `root` | Work-free locality 0 with `--hpx:expect-connecting-localities` |
| `host` | Creates one `state_holder` component (a counter and a small vector), binds it with `register_with_basename`, serves its actions |
| `client` | Late-connecting; joins the host through supervision; holds the component id; runs the probes |
| `client2` | Late-connecting; never joins the host; runs P1 and P4 only, for the view of a locality that did not observe the fence (P5) |
| `replacement` | Joins after the host is fenced; tries to bind the same name (P6) |

`state_holder` actions: `read` returns the counter and a checksum of the vector; `update(n)` adds n
and returns the new value; `slow_update(n, ms)` sleeps, then updates, for F2.

### 7.2 Probes and bounds

After each scenario the client runs the probes twice: once after it sees the host fenced, and once
after the root's `force_disconnect` of the host. The source predicts different answers for the two
(section 3.3).

| Probe | Bound | Recorded |
| --- | --- | --- |
| P1 | 10 s | `value`, `error:<code>`, or `no_answer` |
| P2 | 10 s | `value`, `target_fenced`, `error:<code>`, or `no_answer` |
| P3 | none needed (local read) | `admitted` or `rejected_fenced` |
| P4 | 10 s for the lookup, then P1 on the result | `id_old`, `id_new`, `no_answer` |
| P5 | 10 s | P1 and P2 from `client` and `client2` after SIGCONT |
| P6 | 10 s | bind without unregister, bind after `unregister_with_basename`, then P4 |
| In-flight call (F2) | 30 s, as langX | `ready` or `not_ready` at the bound |

A wait that reaches its bound is recorded as `no_answer`, never retried silently. Each probe prints
one line, `R: scenario=<F> phase=<P> probe=<P> role=<R> result=<...> t_ms=<...>`, with phases
`before`, `fenced`, `resumed`, `removed` and `replaced`. Poll timeout
500 ms through the testing hook on every locality.

### 7.3 Matrix

| Scenario | Probes | Expected | Measured [s1] |
| --- | --- | --- | --- |
| F0 | P1 to P4 | All answer; P3 `admitted`; control | As expected |
| F1 | P1 to P4 | P1 `no_answer` while fenced, `error:locality_was_disconnected` after removal (H1, [src]); P2 `target_fenced`; P3 `rejected_fenced`; P4 `id_old` (H2) | P1 `error:locality_was_disconnected` already while fenced; the rest as expected |
| F2 | in-flight call, P1, P2 | In-flight call `not_ready` at 30 s [langX]; new calls as in F1 | As expected, for plain and fenced in-flight calls |
| F3 | P1 to P5 | Zombie answers P1 (H3); P2 from `client` `target_fenced`; P5 shows what `client2` sees | As expected; `client2` sees the same answers as `client` |
| F4 | P1 to P5 | No eviction [langX, `cc2be553`]; all answer | As expected |
| F5 | P4, P6 | Bind without unregister fails; bind after unregister succeeds; P4 `id_new` (H4) | As expected; then neither client reaches the replacement |

### 7.4 Exit criterion

Every cell of 7.3 recorded on `161df7fc6`: a 2-trial smoke run, then 30 trials per scenario, one
node. Write-up in `docs/results/stage1.md`. Each new gap is drafted as an upstream issue or comment.

Met: job 225624, 180 trials, [`results/stage1.md`](results/stage1.md).

### 7.5 Follow-up: reaching a replacement

In every F5 trial of job 225624, neither existing client reached the replacement locality, while
the replacement reached both the root and the client. Stage 2 depends on spare hosts that existing
localities can reach, so the cause is separated first, 30 trials each:

| Variant | What it separates |
| --- | --- |
| J0 | Whether any locality that connects after the clients is unreachable |
| J0q | The same, without the joiner's first contact |
| F5a | Whether removing the dead host matters |
| F5b | Whether the replacement's own first contact matters (1 of 2 smoke trials without it reached the replacement) |

Each trial records the client's known localities, a ping to the root as control, a ping to the new
locality, and a call on the new component found by name, from both clients.

If a variant isolates an HPX behaviour, it is drafted as an upstream issue with a reproducer.

Result (job 225632, 30 trials each): J0, J0q and F5a reach the newcomer in 30/30 trials from both
clients; F5b in 2/30 from the client and 0/30 from client2. The trigger is the root's
`force_disconnect` of the dead locality before the newcomer joins; the newcomer's first contact does
not matter. Remaining: the order stage 2 would use (replacement joins, then removal, then calls),
and a minimal reproducer for upstream.

---

## 8. Stage 2: stateful 1D stencil

### 8.1 Starting point

`examples/1d_stencil/1d_stencil_8.cpp` at `161df7fc6`. Versions 5 to 7 already keep partitions in
`partition_server` components, but one locality drives every step: version 5 runs on one locality,
and versions 6 and 7 place partitions across localities with a dataflow driven from the main one. Version 8 runs one
`stepper_server` component per locality, which owns that locality's partitions and pushes boundary
partitions to its neighbours (`from_left`, `from_right`) into receive buffers keyed by step. The
neighbours are found with `find_from_basename` under sequence number `get_locality_id()`. That is
a clean ownership boundary: one stepper per host, one name per stepper. Code derived from it keeps
its copyright header and names the source file.

Changes for InferX:

- the root does no work, so sequence numbers and the neighbour ring skip locality 0;
- every boundary message carries the sender's owner epoch, and the receive buffer drops stale ones
  (a zombie's `from_left(t, p)` is otherwise accepted for step t);
- each stepper snapshots its partitions every k steps (section 6.3).

### 8.2 Recovery

- Owner epoch per stepper, kept by the coordinator.
- On a host loss: global rollback first. Every stepper restores the last complete snapshot step,
  because a partition's neighbours need boundary values from the same step. Local recovery from
  logged boundary values is a later comparison, not the first design.
- The replacement creates its stepper from the snapshot, unbinds the dead host's name and binds
  its own (H4), and the coordinator publishes the new owner epoch.

### 8.3 Reference

Each point of the next step is `heat(left, middle, right)` of the previous step, a pure function of
three values [src]. The result is therefore the same whatever the scheduling, and G1 compares
values exactly against a failure-free run with the same parameters.

### 8.4 Parameters

Points per partition, partitions per host, steps and snapshot interval k are chosen in a 2-trial
sweep so that one step takes long enough to measure H7 and a run stays under a few minutes.

### 8.5 Matrix

F0 to F7, 30 trials each after a 2-trial smoke run, on one node, then across nodes.

### 8.6 Exit criterion

G1 to G5 pass across F0 to F7 on Rostam, or a failing gate is explained and reported upstream.
Recovery cost (H6) and failure-free overhead (H7) are reported.

---

## 9. Gates (stage 2)

- **G1:** final field equals the reference in every run.
- **G2:** at most one owner epoch per stepper accepted per step.
- **G3:** zero accepted messages from a fenced owner epoch.
- **G4a:** every run completes within a bounded multiple of the reference time.
- **G4b (recorded, not gated):** exit path of every locality.
- **G5:** every fence comes from HPX supervision state or a dispatch error. Application `failed`
  publications are zero.

---

## 10. Metrics and records

| Metric | Definition |
| --- | --- |
| Detection latency | kill signal to coordinator seeing the host fenced |
| Recovery latency | kill signal to the first step completed after recovery |
| Recomputed steps | steps between the snapshot used and the failure |
| Snapshot cost | time per snapshot, and bytes |
| Failure-free overhead | step time with owner epochs and snapshots minus without |
| Poll timeout | value set through the testing hook, always reported with detection latency |

- 30 trials per scenario and parameter point after a 2-trial smoke run; median, p95, min and max.
- Every wait bounded; a hang is its own exit path.
- Environment recorded: HPX commit, compiler, nodes, parcelport, cores, kernel.
- Raw records as JSON lines under `_runs/` (ignored); curated `results/*.json` tracked; figures made
  by script only.

---

## 11. Rostam setup

1. HPX master `161df7fc6` with `HPX_WITH_SUPERVISION=ON` (which turns on force disconnect and fault
   tolerance), networking and the TCP parcelport. Build: `bench/setup/build_hpx.sh`.
2. Toolchain: gcc 15.1.0, cmake 3.29.2, boost 1.91.0, hwloc 2.12.0, Python 3.12.3.
3. Connect mode: root with `--hpx:expect-connecting-localities`, every other locality with
   `--hpx:agas`, `--hpx:hpx`, `--hpx:ignore-batch-env`, explicit `--hpx:threads`, `--hpx:bind=none`.
4. Clone, fetch and install on the login node; compute nodes may have no internet.
5. Job scripts resolve paths from `$SLURM_SUBMIT_DIR` and source the environment before
   `set -euo pipefail`.
6. Across nodes: readiness over TCP, not files; ports picked on the node that binds them; remote
   processes stopped by signal; SIGTERM to `srun`, then SIGKILL.

---

## 12. Stages

Stages run in order; each ends with a write-up in `docs/results/`.

| Stage | Exit criterion |
| --- | --- |
| 1: stateful component | Section 7.4; met. Follow-up 7.5: trigger isolated, removal order and reproducer open |
| 2: stateful 1D stencil | Section 8.6 |
| 3: Octo-Tiger | The stage 2 recovery on a real application; criterion set after stage 2 |
| 4: LLM runtime with resident KV cache | Recovery of a request's KV cache on an open model; criterion set after stage 2 |

Stages 3 and 4 each depend only on stage 2; their order is not fixed yet.

---

## 13. Repository and conventions

Layout as in the README. `experiments/NN_<name>/` holds the code, job scripts and curated results of
stage NN; `bench/setup/` the Rostam environment and HPX build; `docs/` the plan, `results/` and
`reproduce.md`.

- No placeholder directories; a folder appears with its first file.
- Requirements pinned to the versions on Rostam, in separate files for runtime, build, dev and
  plots, added when first needed.
- C++ follows HPX style with Doxygen comments; Python follows PEP 8 with PEP 257 docstrings.
- Commit subjects imperative, at most 50 characters. Feature branches and pull requests; nothing
  pushed to `main` directly.
- Every result has its command and Rostam job in `docs/reproduce.md`.
- Write-ups: dated, cluster and HPX commit stated, jobs listed, setup table, results tables, "Not
  established", "Claims and non-claims". Inferences are marked as inferences.

---

## 14. Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Calls in flight to a dead host never return [s1] | High | Bound every wait; abandon on fence (3.5) |
| Names outlive their host [s1] | High | Ownership table is authoritative (3.5 rule 6); the replacement unregisters and rebinds (8.2) |
| Existing localities cannot reach a locality that joins after a `force_disconnect` [s1] | High | Stage 2 joins spare hosts before any failure, or removes the dead host only after the replacement has joined (7.5) |
| Snapshot needs the component local [src] | Medium | Each host snapshots its own components and sends the value (6.3) |
| Global rollback hides per-partition costs | Medium | Report recomputed steps; compare local recovery later |
| Detection too slow without the testing hook | Medium | Report the hook value |
| Scope creep into stages 3 and 4 | High | Stage order and exit criteria (section 12) |

---

## 15. Open questions for HPX maintainers

1. A new call to a killed, fenced host fails with `locality_was_disconnected`, but a call in
   flight at the kill never returns [s1]. Is the in-flight case meant to stay unresolved
   (hpx#7834)?
2. Should removing a locality also drop or mark AGAS names bound to its objects?
3. Is unregister then register the intended way for a replacement to take over a name?
4. Would an "is epoch E still live for target T" check at completion time fit in HPX?
5. Should admission reject an epoch that does not match the target's current epoch (hpx#7834)?
6. After `force_disconnect` of a dead locality, localities already running almost never reach a
   locality that connects later, while it reaches them (section 7.5). Is that expected?

---

## 16. Definition of done

1. Stage 1 and stage 2 exit criteria met on Rostam.
2. Results and figures come from documented commands on a clean checkout.
3. A subset reproduced from a fresh checkout by someone else.
4. Limits stated: root and coordinator out of scope, detection measured with the testing hook,
   rollback scope.
5. Code documented, derived files credited, merged through pull requests.
