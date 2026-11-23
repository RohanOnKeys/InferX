<p align="center">
  <img src="docs/assets/inferx-banner.png" alt="inferX" width="640">
</p>

<p align="center">
  Stateful inference on HPX supervision dispatch: recoverable resident state across HPX localities.
</p>

---

## The question

When an [HPX](https://github.com/STEllAR-GROUP/hpx) locality that holds resident state dies:

* what logical state was lost,
* can an older incarnation of that locality still produce results, and
* how does a replacement safely take ownership of the reconstructed state?

HPX should not need to understand application state, such as a KV cache or a stencil partition.
It may need clear identity, failure and ownership semantics so that the layer above can recover
that state. inferX builds real stateful consumers of HPX supervision and dispatch to find out
which semantics are already there and which are missing.

## Where it comes from

inferX follows [langX](https://github.com/RohanOnKeys/langX), which ran LangGraph nodes as HPX
actions under supervision dispatch. langX covered stateless, recomputable steps: when a worker
died, the step ran again elsewhere and the final state matched a stock LangGraph run. Its report
is in [`docs/report.md`](https://github.com/RohanOnKeys/langX/blob/main/docs/report.md) of that
repo.

Retrying the last call is not enough once a worker holds state that outlives a single call. An
LLM runtime is the motivating case: a worker can hold a request's KV cache, and losing the worker
loses the cache. HPX already has components, migration and resiliency support; inferX looks at
how the newer supervision and dispatch semantics interact with stateful components when their
host is lost.

## Stages

| Stage | What |
| --- | --- |
| 1 | C++ reproducer: one stateful component under supervision; kill its host; record what clients, name lookups and admission see |
| 2 | Stateful 1D stencil: partitions as components, an owner epoch per partition, recovery from snapshots, a failure matrix with pass/fail gates |
| 3 | Octo-Tiger: the same recovery on a real application |
| 4 | LLM runtime with a resident KV cache on an open model |

Stages 3 and 4 each build on stage 2; their order is not fixed yet.

## Known HPX gaps that shape the design

Two items found during langX are still open upstream
([hpx#7834](https://github.com/STEllAR-GROUP/hpx/issues/7834)) and affect stateful recovery:

* A dispatch in flight to a killed locality never completes. inferX abandons such calls once HPX
  reports the target fenced.
* Admission (`check_admission`, `dispatch_work`) accepts any epoch. inferX checks ownership epochs
  itself before accepting results or state.

## Status

Early setup. No stage has results yet.

| Item | Value |
| --- | --- |
| HPX | master `161df7fc6` |
| Cluster | Rostam (LSU) |

## Related work

* [langX](https://github.com/RohanOnKeys/langX): durable LangGraph execution on HPX supervision
  dispatch (stateless steps).
* [HPX](https://github.com/STEllAR-GROUP/hpx): the runtime this work builds on.
