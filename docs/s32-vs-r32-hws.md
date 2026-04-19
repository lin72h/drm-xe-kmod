# S32 vs R32 Scheduler Models: Similarities and Differences

Date: 2026-04-19

## Purpose

This memo compares:

- `docs/r32-hws.md`
  - AMD's official RDNA3+ Micro Engine Scheduler specification
- `docs/s32-guc-submit.md`
  - a source-derived working note for the Intel Xe GuC submission backend and
    host-visible scheduling contract in Linux 6.12

The comparison is semantic, not symmetrical.
`r32-hws` is an official firmware scheduler specification.
`s32-guc-submit` is a reverse-engineered host/firmware contract reconstructed
from the Linux Xe driver and GuC ABI headers.

## Comparison Rule

The first rule is to not over-claim.

`r32-hws` documents the scheduler firmware directly.
`s32-guc-submit` documents what the host driver can prove about Xe scheduling,
submission, policy, and reset behavior.

So the right question is not:

- "Does Xe have the same public scheduler model AMD exposes?"

The right question is:

- "What parts of AMD's published HWS model have a real Xe analogue in host-side
  source, and what parts remain opaque inside GuC?"

## Short Conclusion

The strongest similarity is architectural:

- both `r32` and `s32` put firmware in the scheduling path
- both rely on a host driver to create schedulable queue/context objects
- both expose priority, quantum, suspend/resume, and reset control in some form
- both depend on a firmware control path plus host-side fence and recovery logic

The strongest difference is disclosure and control shape:

- `r32-hws` exposes a rich, explicit scheduler API with process, gang, queue,
  VMID, GDS, and reset semantics
- `s32-guc-submit` exposes a much narrower visible contract: `exec_queue`, LRC,
  DRM scheduler jobs, GuC registration, per-context policy KLVs, and
  enable/submit/disable/reset flow

There is no public Xe equivalent to the AMD MES specification shape.

In practice:

- `r32-hws` looks like a public firmware scheduler operating system
- `s32-guc-submit` looks like a host-driven execution queue model that delegates
  context switching and arbitration to opaque GuC firmware

## Major Similarities

### 1. Firmware is in the scheduling loop, but with different authority splits

Both designs are not just "host writes ring and hardware runs forever."

- `r32` uses MES firmware as the scheduler
- `s32` uses GuC as the scheduler and submission endpoint

In both cases, firmware is a real runtime actor in queue lifetime, scheduling,
and recovery.
The split is different:

- `r32` is closer to a firmware-autonomous scheduler
- `s32` is closer to host-driven policy with firmware-delegated context
  switching and arbitration

### 2. The host driver creates schedulable objects

Both designs have a host-visible object that represents GPU work submission.

- `r32` uses user queues, process state, gang state, and mapped hardware queues
- `s32` uses `xe_exec_queue`, LRC state, `xe_sched_job`, and a GuC ID

The names differ, but both have:

- queue creation
- queue destruction
- queue registration / activation
- queue policy updates
- queue removal / recovery

### 3. Priority and time-slicing exist in both designs

Both models expose scheduler policy knobs rather than only a binary
"submit or do not submit" model.

- `r32` explicitly documents priority bands, quantum, and queue connection
  policy
- `s32` exposes per-context scheduling priority, execution quantum, and
  preemption timeout through GuC policy KLVs

### 4. Both support suspend/resume/reset semantics, but by different mechanisms

Both models clearly need more than normal submission.

They each include:

- suspend-like control
- resume-like control
- timeout / reset handling
- cleanup after failed or hung work

The similarity is semantic, not mechanistic:

- `r32` uses published scheduler-firmware operations such as
  `SUSPEND` / `RESUME` / `RESET`
- `s32` uses host-driven DRM scheduler and `xe_sched_msg` control flow plus
  GuC CT operations and G2H notifications

## Major Differences

| Area | `r32-hws` | `s32-guc-submit` |
| --- | --- | --- |
| Authority | Official firmware specification | Reverse-engineered from driver source |
| Scheduler object model | Process, gang, queue, hardware queue, connected queue | `exec_queue`, LRC, job, GuC ID |
| Firmware API visibility | Large explicit packet/API surface | Narrow visible GuC action and KLV surface |
| Scheduling algorithm disclosure | Explicit fairness, oversubscription, priority rules | Internal GuC arbitration policy mostly opaque |
| Resource programming | VMID, GDS, GWS, OA, SE mode, gang submit documented | Resource management is largely kept in the host driver rather than exposed as a broad scheduler API |
| Queue-state disclosure | Explicit unmapped/mapped/disconnected/connected states | Host-visible queue state bits, but not a full published firmware state machine |
| Logging | Formal scheduler log structure | No comparable public scheduler log spec |
| Reset contract | Explicit MES reset / hang-detect API | Host-driven: host owns timeout detection, ban decision, and GT reset initiation; GuC sends notifications |

## Process-Level vs Queue-Level Scheduling Granularity

This is a deeper difference than simple naming.

`r32-hws` exposes a scheduler database with first-class process and gang state.
That means AMD MES can:

- track VMID, page-table, and other process state as scheduler state
- track gang-level priority, quantum, and carryover as scheduler state
- manipulate process or gang state independently of a single queue

`s32-guc-submit` does not expose an equivalent host-visible process or gang
entity.
The primary schedulable object visible to the host is the `exec_queue`.
The VM is a memory-isolation container, not a scheduler grouping object.

That means the two systems differ not only in disclosure, but in visible
scheduling granularity:

- AMD exposes process-level and gang-level scheduler control
- Xe exposes queue-level scheduler control

The GuC may internally track broader groupings, but Linux 6.12 does not expose
them as first-class host-managed scheduler objects.

## Object Model Comparison

### `r32`: process/gang/queue/resource model

`r32-hws` describes a layered scheduler database:

- process state
- gang state
- queue lists
- hardware queue descriptors
- VMID and GDS-related resources
- pipe-local and priority-local scheduling state

This is a direct scheduler-firmware worldview.

### `s32`: exec_queue/job/context model

`s32-guc-submit` only exposes a narrower host-visible worldview:

- `xe_exec_queue` as the main schedulable object
- one or more logical ring contexts under that queue
- a separate GuC-visible registration identity for that queue
- `xe_sched_job` as the host submission object
- GuC IDs and backend queue state for firmware interaction
- DRM scheduler entities for host-side ordering and timeout management

This is not a process/gang resource database in the AMD sense.
It is closer to:

- userspace queue object
- host scheduler wrapper
- firmware-registered queue identity

Do not conflate the LRC with the GuC registration identity:

- the LRC is hardware execution-state storage
- the GuC ID is the firmware-visible identifier for the registered queue

## Control Plane Comparison

### `r32`: explicit scheduler command surface

`r32-hws` publishes a large control API including:

- hardware resource setup
- queue add/remove
- scheduling configuration
- suspend/resume/reset
- root page-table update
- debug VMID programming
- GDS programming
- gang-submit pairing
- miscellaneous register and invalidate operations

That means the AMD driver is speaking to firmware through a public scheduler
packet vocabulary.

### `s32`: context registration and policy control

The visible Xe/GuC surface is materially smaller.
The Linux 6.12 driver clearly shows:

- context registration
- multi-LRC registration
- context mode set
- context enable / disable
- context kick / submit
- per-context scheduling policy update
- deregistration and recovery interaction

So the Xe host/firmware contract is real, but it is not documented as a broad
public scheduler API comparable to MES.

## Scheduling Algorithm Visibility

### `r32` is explicit

`r32-hws` documents:

- queue state transitions
- round-robin behavior
- oversubscription handling
- aggregated doorbell wakeups
- priority bands
- hardware queue connection policy
- preemption and queue manager interaction

This means `r32-hws` can describe scheduler behavior, not just scheduler entry
points.

### `s32` is mostly implicit

The Xe driver makes some policy visible:

- queue priority mapping
- execution quantum
- preemption timeout
- submission width and parallel workqueue setup
- host timeout and reset decisions

But it does not publish the full GuC arbitration model.
From source we can infer:

- how the host describes a context to GuC
- how the host updates policy
- how the host kicks work
- how resets and fences feed back into recovery

We cannot honestly claim from Linux 6.12 source alone:

- the exact GuC arbitration algorithm
- the internal queue arbitration order
- the full oversubscription policy
- an AMD-style scheduler context layout

## Priority, Quantum, and Preemption

### Similarity

Both systems support more than FIFO execution.
They each expose:

- priority
- time-slice or quantum-like control
- preemption-related policy

### Difference

`r32-hws` publishes a much richer priority model:

- Real time
- Focus
- Normal
- Low

and ties those bands to explicit scheduling expectations.

`s32-guc-submit` exposes a smaller visible policy surface:

- low / normal / high / kernel-style queue priority mapping
- per-context execution quantum
- per-context preemption timeout
- preempt-to-idle on quantum expiry

That is enough to prove real scheduler policy exists, but not enough to claim a
fully documented public arbitration hierarchy like AMD's.

## Long-Running Work Distinction

Both ecosystems distinguish long-running work from ordinary timeout-monitored
work, but again by different mechanisms.

- `r32-hws` exposes `is_long_running` as part of the published queue model
- `s32-guc-submit` exposes `XE_EXEC_QUEUE_SCHEDULE_MODE_LONG_RUNNING` in the
  host-visible queue model

This matters because long-running work should not be collapsed into the same
timeout and arbitration story as ordinary queues.

Xe also exposes preempt-fence mode around compute workloads, which has no
obvious direct AMD MES analogue in the public `r32-hws` material.

## Queue State Model

### `r32`

AMD explicitly distinguishes:

- unmapped queue
- mapped and disconnected queue
- mapped and connected queue

That is a formal scheduler state machine.

### `s32`

Xe exposes host-visible backend state such as:

- registered
- enabled
- pending enable
- pending disable
- suspended
- reset
- killed
- banned
- wedged

This is a real queue lifecycle, but it is not the same abstraction.
It is closer to:

- host/firmware queue lifetime and failure state

than to AMD's published hardware-queue connection model.

## Memory and VM Resource Programming

### `r32`

The AMD spec explicitly includes scheduler-visible resource management for:

- VMIDs
- root page tables
- GDS / GWS / OA
- special debug VMIDs
- gang submit relationships

### `s32`

Xe clearly involves VM, bind, and execution context state, but the scheduler
surface exposed in host code is narrower.
The visible scheduling contract does not present a public equivalent of:

- `SET_HW_RSRC`
- `PROGRAM_GDS`
- `SET_DEBUG_VMID`
- `UPDATE_ROOT_PAGE_TABLE`

This is not just a documentation gap.
The visible Xe design keeps more resource management in the host driver instead
of exposing it through a broad firmware scheduler API.

## Reset, Timeout, and Logging

### `r32`

AMD publishes a formal scheduler reset and log story:

- hang-detect and reset APIs
- returned hung-queue information
- API history
- event history
- interrupt history

### `s32`

Xe exposes a different shape:

- host timeout detection
- DRM scheduler timeout and recovery coupling
- GuC notifications
- queue disable / ban / deregister flow
- GT reset interaction
- debug surfaces and dumps around failure

So both ecosystems treat recovery as core scheduler behavior, but they do so at
different control points:

- `r32` publishes firmware-driven reset and hang-detect operations
- `s32` leaves reset authority primarily with the host driver

## What `s32` Can Honestly Claim

The Xe note can honestly claim all of the following:

- Xe has a real firmware-mediated scheduler path through GuC
- `exec_queue` is the primary schedulable host object
- GuC registration and per-context policy updates are visible in source
- submission, timeout, fence, disable, and reset lifecycles are visible enough
  to document
- priority, quantum, and preemption timeout are real scheduling knobs
- the host driver and GuC together form the closest visible Xe equivalent to a
  scheduler contract

## What `s32` Must Not Claim

The Xe note should not claim:

- that Xe exposes an AMD MES-equivalent public firmware scheduler API
- that GuC internal arbitration policy is fully known from host code
- that Xe has a published process/gang/connected-queue scheduler model
- that AMD's queue-state model maps directly onto Xe internals
- that every resource-programming concept in `r32-hws` has a visible Xe twin

## What Xe Documents Better

The comparison should also acknowledge one asymmetry that cuts the other way.

`r32-hws` is stronger at published firmware-scheduler structure, but
`s32-guc-submit` benefits from fully readable host-driver source.

That means the Xe note can often document host-side behavior more concretely
than an outsider can document AMD MES host/firmware interaction beyond the
published API surface.

## Practical Naming Guidance

Use `s32-guc-submit.md` as the actual note name.
Keep `s32` only as the local codename.

- `r32-hws` is an official hardware scheduler specification
- `s32-guc-submit` is a reverse-engineered Xe host/GuC contract note

That distinction should stay in the title and opening lines, otherwise the
pairing becomes misleading.

## Final Rule

The shortest correct summary is:

`r32-hws` describes a published firmware scheduler architecture with explicit
resource and queue-management APIs.
`s32-guc-submit` describes the host-visible Xe/GuC scheduling contract:
exec queues, jobs, GuC registration, policy update, submission, timeout, and
reset.

They are similar in role and broad architecture, but not equal in disclosure,
API richness, or scheduler-algorithm visibility.
