<p align="center">
  <img src="docs/assets/inferx-banner.png" alt="InferX" width="100%">
</p>

---

<p align="center">
  Stateful inference on HPX supervision dispatch: recoverable resident state across HPX localities.
</p>


## The question

When an [HPX](https://github.com/TheHPXProject/hpx) locality that holds resident state dies:

* what logical state was lost,
* can an older incarnation of that locality still produce results, and
* how does a replacement safely take ownership of the reconstructed state?

HPX should not need to understand application state, such as a KV cache or a stencil partition.
It may need clear identity, failure and ownership semantics so that the layer above can recover
that state. InferX builds real stateful consumers of HPX supervision and dispatch to find out
which semantics are already there and which are missing.

## Where it comes from

InferX continues a line of work that started with [RayX](https://github.com/byekrang/RayX), which
asked how much of the distributed execution Ray is usually used for could run on HPX instead,
especially for fine-grained AI and LLM workloads. RayX's experiment 70 established the HPX
locality supervision that [langX](https://github.com/RohanOnKeys/langX) then used to run LangGraph
agent steps fault-tolerantly. InferX takes the next step: from recomputable steps to resident
state, such as an LLM's KV cache, that has to survive the loss of the HPX locality holding it,
while keeping an outer layer like Ray to a minimum.

RayX asked how far HPX can replace Ray. langX showed HPX supervision keeps stateless agent steps
correct. InferX tests the same for state that lives on a locality.

InferX follows [langX](https://github.com/RohanOnKeys/langX), which ran LangGraph nodes as HPX
actions under supervision dispatch. langX covered stateless, recomputable steps: when a worker
died, the step ran again elsewhere and the final state matched a stock LangGraph run. Its report
is in [`docs/report.md`](https://github.com/RohanOnKeys/langX/blob/main/docs/report.md) of that
repo.

Retrying the last call is not enough once a worker holds state that outlives a single call. An
LLM runtime is the motivating case: a worker can hold a request's KV cache, and losing the worker
loses the cache. HPX already has components, migration and resiliency support; InferX looks at
how the newer supervision and dispatch semantics interact with stateful components when their
host is lost.

## Stages

| Stage | What |
| --- | --- |
| 1 | C++ reproducer: one stateful component under supervision; kill its host; record what clients, name lookups and admission see |
| 2 | Stateful 1D stencil: partitions as components, an owner epoch per partition, recovery from snapshots, a failure matrix with pass/fail gates |
| 3 | Octo-Tiger: the same recovery on a real application (planned, unconfirmed) |
| 4 | LLM runtime with a resident KV cache on an open model |

Stages 3 and 4 each build on stage 2; their order is not fixed yet.

## Known HPX gaps that shape the design

Two items found during langX are still open upstream
([hpx#7834](https://github.com/TheHPXProject/hpx/issues/7834)) and affect stateful recovery:

* A dispatch in flight to a killed locality never completes. InferX abandons such calls once HPX
  reports the target fenced.
* Admission (`check_admission`, `dispatch_work`) accepts any epoch. InferX checks ownership epochs
  itself before accepting results or state.

## Status

Stage 1 is done: [`docs/results/stage1.md`](docs/results/stage1.md). Stage 2 is next, after one
follow-up on reaching a replacement locality. The experiment plan, with the failure scenarios, probes,
gates and exit criteria of each stage, is in [`docs/plan.md`](docs/plan.md).

| Item | Value |
| --- | --- |
| HPX | master `161df7fc6` |
| Cluster | Rostam (LSU) |

## Layout

```text
experiments/01_stateful_component/  stage 1: one stateful component under supervision
bench/setup/                        Rostam environment and HPX build
bench/check_md_links.py             markdown link check, run in CI
docs/plan.md                        experiment plan: codes, design, failure matrices, gates, stages
docs/reproduce.md                   the command and Rostam job behind every result
docs/results/                       one write-up per stage, added with its first result
```

Builds and runs happen on Rostam; [`docs/reproduce.md`](docs/reproduce.md) has the setup. CI
checks shell and Python syntax and markdown links only.

## Related work

* [RayX](https://github.com/byekrang/RayX): how far HPX can replace Ray for distributed AI and
  LLM workloads; experiment 70 introduced the HPX locality supervision used here.
* [langX](https://github.com/RohanOnKeys/langX): durable LangGraph execution on HPX supervision
  dispatch (stateless steps).
* [HPX](https://github.com/TheHPXProject/hpx): the runtime this work builds on.
