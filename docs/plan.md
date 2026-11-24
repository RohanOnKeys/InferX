# Recoverable Resident State on HPX: Experiment Plan

## Codes

Every short code this project defines, named once here. Each other document repeats the ones it
uses in a table under its title, with a link back here.

Failure scenarios (full matrix in sections 7.2 and 8.4). "Host" is the locality that holds the
resident state under test.

| Code | Scenario | What is done |
| --- | --- | --- |
| F0 | No failure | Nothing; the control run |
| F1 | Kill idle host | SIGKILL the host while no call to its component is in flight |
| F2 | Kill host mid-call | SIGKILL the host while a call to its component is in flight |
| F3 | Zombie host | SIGSTOP the host until HPX fences it, then SIGCONT it |
| F4 | Slow, not dead | SIGSTOP the host for less than the detection time, then SIGCONT |
| F5 | Kill and replace | SIGKILL the host; a replacement locality joins and takes ownership of the state |
| F6 | Kill during snapshot | SIGKILL the host while it writes a snapshot (stage 2) |
| F7 | Kill neighbours | SIGKILL the hosts of two adjacent partitions in one step (stage 2) |

Probes: what stage 1 records after each scenario (section 7.1).

| Code | Probe | Records |
| --- | --- | --- |
| P1 | Direct call | A plain action call on the component's id: value, error, or no answer within the bound |
| P2 | Fenced dispatch | The same action through `dispatch_work`: `dispatch_outcome`, error, or no answer |
| P3 | Admission | `check_admission` for the host at its join epoch |
| P4 | Name lookup | `find_from_basename` for the component's registered name, and a call on the id it returns |
| P5 | Zombie answer | Whether a resumed host (F3, F4) still answers calls, and from which client's view |
| P6 | Name takeover | Whether a replacement can register the same name, and what a later P4 returns |

Gates: checks every stage 2 run must pass (full text in section 9).

| Code | Gate | Passes when |
| --- | --- | --- |
| G1 | Correct result | The final field equals the failure-free reference, value for value |
| G2 | Single owner | At most one owner epoch per partition is accepted at any step |
| G3 | No stale write | No state or ghost exchange from a fenced owner epoch is accepted |
| G4a | Run completes | The run finishes within a bound |
| G4b | Exit paths | Recorded, not gated: how each process left (clean, timeout, killed by the harness) |
| G5 | Detection from HPX only | Every fence comes from HPX state or an HPX dispatch outcome, never from the harness |

Hypotheses (section 4):

| Code | Hypothesis |
| --- | --- |
| H1 | A call to a component on a killed host does not resolve by itself |
| H2 | A component name registered by a killed host still resolves to the dead id |
| H3 | A resumed zombie host still answers calls; only a check above HPX rejects its results |
| H4 | A replacement can take over a dead host's component name |
| H5 | Owner epochs and snapshots give the failure-free result under F1 to F7 |
| H6 | Recovery cost grows with the snapshot interval and nothing else in the run |
| H7 | Failure-free cost of owner epochs and snapshots is small relative to a stencil step |

Source tags: **[langX]** marks a behaviour measured in langX; **[s1]** to **[s4]** mark a
behaviour measured in that InferX stage; **[VERIFY]** marks an assumption still to check before
code depends on it.

Status: setup. No stage has results yet. This plan is the single source of truth for scope;
findings from each stage are folded back into it.

---

## 0. Sources

| Source | Read | Notes |
| --- | --- | --- |
| [langX](https://github.com/RohanOnKeys/langX) plan, report and results | Fully | Behaviour carried over in section 3.2 |
| [RayX](https://github.com/byekrang/RayX) README and experiment 70 | Read for langX | Origin of the supervision harness |
| HPX `examples/1d_stencil/` at `161df7fc6` | Headers and component code | Section 8.1 |
| HPX `supervision`, `supervision_dispatch`, `checkpoint`, `runtime_distributed` (migration), AGAS basename calls at `161df7fc6` | Names located, semantics not yet read | Section 3.1, each tagged [VERIFY] |
| HPX resiliency (`libs/core/resiliency`, `libs/full/resiliency_distributed`) | Not yet | Replay and replicate; compare in stage 2 |

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
| Owner epoch | A counter per piece of state, kept above HPX, increased on every change of owner. Not the same as the join epoch |
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

Ray is kept to a minimum: every recovery should be attributable to HPX and the ownership layer
above it, not to an outer restart.

### 2.2 Reused from langX

Files derived from langX name the source file in their header.

| langX path | Use here |
| --- | --- |
| `bench/setup/rostam_env.sh`, `build_hpx.sh` | Environment and HPX build, pinned to `161df7fc6` |
| `bench/hpx_repro/` | Pattern for the stage 1 C++ harness: one binary with roles, a bash runner, handshake files on one node |
| `python/src/langx/island.py`, `signal_agent.py` | Launching localities across nodes, signals, remote ready files (stage 2 onward) |
| `experiments/01_durable_langgraph/run_exp01.py`, `analyze_exp01.py` | One process per trial, fault injection, gates, JSON lines, aggregate |
| `bench/check_md_links.py`, `.github/workflows/ci.yml` | Link check and CI |

---

## 3. HPX as used here

### 3.1 Calls

| Area | Calls | Status |
| --- | --- | --- |
| Supervision | `init`, `finalize`, `discover_and_join`, `snapshot_peers`, `query_state`, `check_admission`, `await_terminal`, `dispatch_work`, `dispatch_outcome` | Used in langX [langX] |
| Membership | `hpx::force_disconnect` | Used in langX [langX] |
| Components | `hpx::components::component_base`, `HPX_REGISTER_COMPONENT`, `hpx::new_<>` | As in `1d_stencil_5` to `_8` [VERIFY] behaviour on host loss |
| Naming | `hpx::register_with_basename`, `hpx::find_from_basename`, `hpx::unregister_with_basename` | As in `1d_stencil_8` [VERIFY] behaviour on host loss |
| Snapshots | `hpx::util::save_checkpoint`, `restore_checkpoint` (`libs/full/checkpoint`) | [VERIFY] fits component state |
| Migration | `hpx::components::migrate` (`runtime_distributed`) | Needs a live source; compared, not relied on [VERIFY] |

### 3.2 Behaviour carried over from langX

Measured in langX on HPX `cc2be553` and re-checked on master `161df7fc6` where stated.

| Behaviour | Finding | Source |
| --- | --- | --- |
| SIGKILL detection | Three poll windows: 746, 1496, 2997 ms at 250, 500, 1000 ms poll timeout (`cc2be553`) | [langX] |
| Dispatch in flight to a killed locality | Never completes; the caller must abandon it once the target is fenced. Still open on `161df7fc6` ([hpx#7834](https://github.com/TheHPXProject/hpx/issues/7834)) | [langX] |
| `check_admission`, `dispatch_work` with a wrong epoch | Admitted; the epoch is not checked. Still open on `161df7fc6` (hpx#7834) | [langX] |
| Resumed SIGSTOP locality | Its late result arrives as `ok`; only the caller's own view rejects it (`cc2be553`) | [langX] |
| SIGSTOP shorter than one poll window | No eviction (`cc2be553`) | [langX] |
| `finalize()` with pending `await_terminal` | Fixed on master by [#7835](https://github.com/TheHPXProject/hpx/pull/7835) | [langX] |
| Dead observer fences a healthy target | Fixed on master by [#7897](https://github.com/TheHPXProject/hpx/pull/7897) | [langX] |
| `force_disconnect` from a late-connecting locality | Refused on `cc2be553`; fixed on master by [#7901](https://github.com/TheHPXProject/hpx/pull/7901) | [langX] |
| `snapshot_peers()` keeps a removed peer | Fixed on master by [#7922](https://github.com/TheHPXProject/hpx/pull/7922), with #7924 and #8027 | [langX] |
| Losing the coordinator fences its workers | Not seen on `161df7fc6` | [langX] |

### 3.3 Design rules that follow

1. **Never wait unbounded on a call to a host.** Abandon it once HPX shows the host fenced, and
   record the abandonment.
2. **Owner epochs are checked above HPX.** HPX admission does not check epochs [langX], so every
   message that carries state carries (partition, owner epoch) and the receiver checks it.
3. **Never match on error strings.** Map every dispatch exception to "fenced" or "failed".
4. **Epochs only increase.** An owner epoch is never reused or decreased.
5. **Detection comes from HPX.** The harness knows when it killed a process; the system under test
   must not use that knowledge.

---

## 4. Hypotheses

| ID | Hypothesis | Stage | Falsified if |
| --- | --- | --- | --- |
| H1 | A call to a component on a killed host does not resolve by itself (an inference from langX's in-flight dispatch result) | 1 | P1 returns a value or an error within the bound in F1 or F2 |
| H2 | A name registered by a killed host still resolves to the dead id | 1 | P4 returns no id, or a live one, after F1 |
| H3 | A resumed zombie host still answers calls; only a check above HPX rejects them | 1 | P5 shows no answer from the zombie in F3 |
| H4 | A replacement can take over a dead host's name | 1 | P6 cannot register the name, or P4 still returns the dead id afterwards |
| H5 | Owner epochs and snapshots give the failure-free result under F1 to F7 | 2 | Any run fails G1, G2 or G3 |
| H6 | Recovery cost grows with the snapshot interval and nothing else in the run | 2 | Recovery time does not follow recomputed steps |
| H7 | Failure-free cost of owner epochs and snapshots is small relative to a stencil step | 2 | Cost above 10 percent of step time at the chosen grain size |

H1 to H4 are expectations, not claims. Stage 1 exists to replace them with measurements.

---

## 5. Non-goals

- Root-loss recovery (out of scope since RayX experiment 70).
- Process checkpointing (CRIU and similar): a restored process is exactly the stale incarnation the
  owner epoch must reject.
- Accelerators and GPU memory before stage 4.
- Performance claims beyond recovery cost and failure-free overhead.
- Ray, except as an outer layer named in a stage that needs it.
- Model quality of any kind in stage 4.

---

## 6. Architecture

### 6.1 Processes

```text
Root, locality 0              work-free; anchors AGAS; serves membership; outside the failure scope
Coordinator, late-connecting  ownership table: piece of state -> (host, join epoch, owner epoch);
                              joins every host; decides recovery; holds or locates snapshots
Hosts 1..N, late-connecting   hold resident state in components; serve actions on it
Spare hosts                   started idle; take ownership on recovery (F5)
```

Stage 1 uses a reduced form: root, one host, one or two clients, and a replacement.

### 6.2 Ownership

- The coordinator is the only writer of the ownership table.
- Changing an owner: fence the old host (seen through HPX), increase the owner epoch, create the
  state on the new host from a snapshot, register it, then publish the new owner.
- Every state-carrying call and reply carries (piece, owner epoch). A receiver drops anything whose
  owner epoch is not the current one, and counts it.

### 6.3 Snapshots

- Taken every k steps, off the host. Store and format are chosen in stage 2 after reading
  `libs/full/checkpoint` [VERIFY].
- A snapshot is usable only once complete: it is published to the coordinator after it is written,
  never before (F6 tests this).

---

## 7. Stage 1: one stateful component under supervision

### 7.1 Setup and probes

One C++ binary with roles, as langX `bench/hpx_repro/`:

```text
root   work-free locality 0
host   creates a component holding a counter and a small vector, registers it by basename
client late-connecting; joins the host through supervision; holds the component id
client2  optional second client that did not observe the fence (P5)
replacement  joins after the host is fenced and registers the same name (P6)
```

After each scenario the client runs the probes P1 to P6 with every wait bounded, and prints one
line per probe. A wait that hits its bound is recorded as `no_answer`, never retried silently.

### 7.2 Matrix

| Scenario | Probes | Expected (hypothesis) |
| --- | --- | --- |
| F0 | P1 to P4 | All answer; control |
| F1 | P1 to P4 | P1 no answer (H1); P2 `target_fenced` once fenced; P3 refused; P4 dead id (H2) |
| F2 | P1, P2 | The in-flight call never resolves (H1, langX); the call issued after the fence is refused |
| F3 | P1 to P5 | Zombie answers (H3); which client sees it, and whether P2 still refuses |
| F4 | P1 to P5 | No eviction expected; recorded |
| F5 | P4, P6 | Replacement takes the name (H4); P4 returns the new id |

### 7.3 Exit criterion

Every cell of 7.2 recorded on `161df7fc6`: 2-trial smoke, then 30 trials per scenario, one node.
Write-up in `docs/results/stage1.md`. Each new gap drafted as an upstream issue or comment.

---

## 8. Stage 2: stateful 1D stencil

### 8.1 Starting point

`examples/1d_stencil/1d_stencil_6.cpp` to `_8.cpp` at `161df7fc6` keep each partition in a
`partition_server` component created with `hpx::new_<>`. `1d_stencil_8` adds one stepper component
per locality, found by neighbours through `register_with_basename` and `find_from_basename`.
Stage 2 starts from `1d_stencil_8` [VERIFY: which version keeps the cleanest ownership boundary].
Code derived from it keeps its copyright header and names the source file.

### 8.2 Recovery

- Owner epoch per partition, kept by the coordinator.
- Snapshot every k steps.
- On a host loss: global rollback first (every partition back to the last complete snapshot step),
  because a partition's neighbours need ghost cells from the same step. Local recovery from logged
  boundary values is a later comparison, not the first design.

### 8.3 Reference

A failure-free run with the same parameters. The stencil is deterministic for a fixed partitioning
and step order [VERIFY], so G1 compares values exactly.

### 8.4 Matrix

F0 to F7, 30 trials each after a 2-trial smoke, on one node and then across nodes.

### 8.5 Exit criterion

G1 to G5 pass across F0 to F7 on Rostam, or a failing gate is explained and reported upstream.
Recovery cost (H6) and failure-free overhead (H7) reported.

---

## 9. Gates (stage 2)

- **G1:** final field equals the reference in every run.
- **G2:** at most one owner epoch per partition accepted per step.
- **G3:** zero accepted messages from a fenced owner epoch.
- **G4a:** every run completes within a bounded multiple of the reference time.
- **G4b (recorded, not gated):** exit path of every locality.
- **G5:** every fence comes from HPX supervision state or a dispatch outcome. Application `failed`
  publications must be zero.

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

- 30 trials per scenario and parameter point after a 2-trial smoke; median, p95, min and max.
- Every wait bounded; a hang is its own exit path.
- Environment recorded: HPX commit, compiler, nodes, parcelport, cores, kernel.
- Raw records as JSON lines under `_runs/` (ignored); curated `results/*.json` tracked; figures made
  by script only.

---

## 11. Rostam setup

1. HPX master `161df7fc6` with `HPX_WITH_SUPERVISION=ON` (which turns on force disconnect and fault
   tolerance), networking and the TCP parcelport. Build: `bench/setup/build_hpx.sh`.
2. Toolchain: gcc 15.1.0, cmake 3.29.2, boost 1.91.0, hwloc 2.12.0, Python 3.12.3.
3. Launch in HPX connect mode: root with `--hpx:expect-connecting-localities`, every other locality
   with `--hpx:agas`, `--hpx:hpx`, `--hpx:ignore-batch-env`, explicit `--hpx:threads`,
   `--hpx:bind=none`.
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
| 1: stateful component | Section 7.3 |
| 2: stateful 1D stencil | Section 8.5 |
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
- C++ follows HPX style with Doxygen comments; Python PEP 8 with PEP 257 docstrings.
- Commit subjects imperative, at most 50 characters. Feature branches and pull requests; nothing
  pushed to `main` directly.
- Every result has its command and Rostam job in `docs/reproduce.md`.
- Write-ups: dated, cluster and HPX commit stated, jobs listed, setup table, results tables, "Not
  established", "Claims and non-claims". Inferences are marked as inferences.

---

## 14. Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Calls to a dead host hang | High | Bound every wait; abandon on fence (3.3) |
| AGAS keeps stale names or ids after host loss | High | P4 and P6 in stage 1 measure it before stage 2 depends on it |
| Snapshot API does not fit component state | Medium | Read `libs/full/checkpoint` before stage 2; fall back to explicit serialization |
| Global rollback hides per-partition costs | Medium | Report recomputed steps; compare local recovery later |
| Detection too slow without the testing hook | Medium | Report the hook value |
| Scope creep into stages 3 and 4 | High | Stage order and exit criteria (section 12) |

---

## 15. Open questions for HPX maintainers

1. What should a call to a component on a fenced host return: an error, or nothing until the
   caller gives up?
2. Should AGAS drop or mark names and ids of components whose host was fenced or force-disconnected?
3. Is there an intended way for a replacement to take over a component name, beyond unregister and
   register?
4. Would an "is epoch E still live for target T" check at completion time fit in HPX?
5. Should admission reject an epoch that does not match the target's current epoch (hpx#7834)?

---

## 16. Definition of done

1. Stage 1 and stage 2 exit criteria met on Rostam.
2. Results and figures come from documented commands on a clean checkout.
3. A subset reproduced from a fresh checkout by someone else.
4. Limits stated: root out of scope, detection measured with the testing hook, rollback scope.
5. Code documented, derived files credited, merged through pull requests.
