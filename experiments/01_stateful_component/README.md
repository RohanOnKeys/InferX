# 01: one stateful component under supervision

Codes used in this document (full list: [plan, codes](../../docs/plan.md#codes)):

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
| H1 to H4 | stage 1 hypotheses, [plan section 4](../../docs/plan.md#4-hypotheses) |

**Status:** not started. Design: [plan section 7](../../docs/plan.md#7-stage-1-one-stateful-component-under-supervision).
HPX: master `161df7fc6`.

## Question

When the locality that holds a component's state is killed or frozen, what do clients see on a
direct call, through fenced dispatch, at admission, and when they look the component up by name?
Does a resumed old incarnation still answer, and can a replacement take over the component's name?

## Roles

| Role | What it does |
| --- | --- |
| `root` | work-free locality 0 |
| `host` | holds one `state_holder` component (a counter and a small vector) and binds it by basename |
| `client` | joins the host through supervision and runs the probes |
| `client2` | never joins the host; runs P1 and P4 for P5 |
| `replacement` | joins after the fence and tries to bind the same name (P6) |

## Matrix

| Scenario | Probes |
| --- | --- |
| F0 | P1 to P4 |
| F1 | P1 to P4 |
| F2 | in-flight call, P1, P2 |
| F3 | P1 to P5 |
| F4 | P1 to P5 |
| F5 | P4, P6 |

The client runs the probes twice: once it sees the host fenced, and again after the root's
`force_disconnect` of the host. Probe waits are bounded at 10 s, the in-flight call in F2 at 30 s;
a wait that reaches its bound is recorded as `no_answer`. Poll timeout 500 ms. Smoke runs use 2
trials per scenario, full runs 30, all on one node.

Expected answers, with the reason for each, are in
[plan section 7.3](../../docs/plan.md#73-matrix).

## Approach

One C++ binary with the five roles, started by a bash runner with handshake files on one node,
following langX
[`bench/hpx_repro/`](https://github.com/RohanOnKeys/langX/tree/main/bench/hpx_repro). Each probe
prints one `R:` line. Results go to `docs/results/stage1.md`, and the job behind each one to
[`docs/reproduce.md`](../../docs/reproduce.md).
