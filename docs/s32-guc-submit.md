NOTE: This is not an Intel-published scheduler specification.
It is a reverse-engineered host/GuC contract derived from Linux 6.12 source.

# S32 GuC Submit: Reverse-Engineered Xe Host/GuC Contract

Date: 2026-04-19

## Purpose

This memo is a source-derived working note for the Intel Xe GuC submission
backend and host-visible scheduling contract in Linux 6.12, using `s32` as the
local codename for the Intel Arc / Xe+ line we care about.

It is intentionally not written as if Intel had published an official MES-like
firmware scheduler specification. They have not.

Instead, this document records what can be inferred from:

- `../nx/linux-6.12/drivers/gpu/drm/xe/`
- `../nx/linux-6.12/include/uapi/drm/xe_drm.h`
- the GuC action and KLV ABI headers used by the Xe driver

Important caveat:

- the GuC ABI headers shipped in Linux are published Intel-authored ABI
  definitions
- what is reverse-engineered here is the behavioral contract: how the host
  driver uses that ABI, what state transitions occur, and what the combined
  host/firmware behavior implies

This is a reverse-engineered software contract memo, not a silicon contract.
Anything that depends on opaque GuC internal policy should be treated as an
inference, not a guaranteed hardware fact.

## Scope

This document covers the Xe scheduling path that is visible in the Linux 6.12
driver:

- user-visible exec queue creation and execution
- host-side queue objects, jobs, fences, and scheduler entities
- host-to-GuC control flow over CT
- GuC-facing queue registration, policy update, submit, suspend, resume,
  disable, and deregister flow
- timeout, reset, cleanup, and recovery behavior

It does not claim to describe:

- GuC internal scheduling heuristics in full
- a complete firmware process database like AMD MES exposes
- opaque firmware queue arbitration details not visible in the driver
- page-fault / HMM / userptr behavior beyond what the Linux 6.12 host driver
  clearly routes

## Short Conclusion

The closest Intel Xe equivalent to an AMD-style scheduler spec is not a single
firmware API document.
It is the host-visible GuC submission contract in Linux 6.12.

The Xe-specific scheduling content starts at the GuC submission backend and the
GuC firmware interface.
The earlier userspace, queue-object, and DRM-scheduler layers are still
important context, but they are not a published Intel scheduler architecture.

That means this note should be read as:

- an `exec_queue` and LRC model
- a GuC submission backend state machine
- a GuC context registration and policy ABI
- a job/fence/timeout/reset lifecycle model

not as a direct clone of AMD MES process/gang/resource APIs.

The shortest accurate mental model is:

- actor 1: the host driver
- actor 2: the GuC firmware
- protocol: CT actions, G2H notifications, and policy KLVs

## Architectural Model

Layers 1-3 are host-side context.
The Xe-specific scheduling contract starts in layers 4-5, where the GuC
submission backend and the GuC firmware interface become visible.

### Layer 1: userspace contract

The user-visible scheduler surface is centered on:

- `DRM_IOCTL_XE_EXEC_QUEUE_CREATE`
- `DRM_IOCTL_XE_EXEC_QUEUE_DESTROY`
- `DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY`
- `DRM_IOCTL_XE_EXEC`
- `DRM_IOCTL_XE_VM_BIND`
- sync objects and user fences in `struct drm_xe_sync`

Key source:

- `../nx/linux-6.12/include/uapi/drm/xe_drm.h`

Userspace does not directly program a firmware scheduler object comparable to
AMD's public MES API packets.
Instead, userspace creates an execution queue bound to a VM and engine-class
placement, then submits batch addresses and explicit sync objects.

### Layer 2: host execution queue object

The core host object is `struct xe_exec_queue`.

Key fields visible in the Linux 6.12 driver:

- engine class
- logical engine mask
- width for parallel submission
- VM binding
- per-queue scheduling properties
- backend-specific state (`q->guc` for the GuC path)
- in the GuC backend, a 1:1 relationship with a scheduler entity
- logical ring contexts (`LRCs`)

Key source:

- `../nx/linux-6.12/drivers/gpu/drm/xe/xe_exec_queue_types.h`
- `../nx/linux-6.12/drivers/gpu/drm/xe/xe_exec_queue.c`

### Layer 3: host scheduler entity and jobs

The host driver uses DRM scheduler infrastructure, wrapped by
`struct xe_gpu_scheduler`.

This layer owns:

- dependency tracking and job readiness through fence chains
- run queue ordering on the host
- a separate scheduler-message queue for backend control operations
- timeout handling
- job object lifetime
- fence completion plumbing

The actual job object is `struct xe_sched_job`.
It pre-allocates fences, records batch addresses, and bridges submission to the
backend.

Key source:

- `../nx/linux-6.12/drivers/gpu/drm/xe/xe_gpu_scheduler.c`
- `../nx/linux-6.12/drivers/gpu/drm/xe/xe_gpu_scheduler_types.h`
- `../nx/linux-6.12/drivers/gpu/drm/xe/xe_sched_job.c`

### Layer 4: GuC submission backend

The GuC submission backend adds Xe-specific scheduling state in
`struct xe_guc_exec_queue`.

That state includes:

- a per-exec-queue GPU scheduler wrapper
- scheduler entity
- static backend messages
- GuC-visible queue state flags
- GuC ID
- suspend waitqueue
- parallel submission workqueue indices

Key source:

- `../nx/linux-6.12/drivers/gpu/drm/xe/xe_guc_exec_queue_types.h`
- `../nx/linux-6.12/drivers/gpu/drm/xe/xe_guc_submit.c`

### Layer 5: GuC firmware

The GuC is the real scheduling firmware endpoint.
From the driver side, it exposes action codes and KLV-based policy updates.

The driver clearly uses GuC for:

- context registration / deregistration
- context enable / disable
- submission kicks
- context policy updates
- reset notifications and failure handling

What the driver does not expose in detail is the GuC's full internal scheduler
database or arbitration algorithm.

Key source:

- `../nx/linux-6.12/drivers/gpu/drm/xe/abi/guc_actions_abi.h`
- `../nx/linux-6.12/drivers/gpu/drm/xe/abi/guc_klvs_abi.h`

## Scheduler Objects

### Exec queue

An `exec_queue` is the primary scheduling object visible to userspace and the
host driver.

Important characteristics:

- tied to a VM
- tied to an engine class and placement set
- can be single-LRC or multi-LRC parallel width
- has mutable scheduling properties
- has a GuC registration lifetime

This is the closest Xe equivalent to the schedulable "queue" object in MES,
and the primary schedulable object visible to the host.
There is no public process/gang API layering on top.

### Logical ring context

Each exec queue owns one or more LRCs.

The driver registers GuC execution state using LRC descriptors and, for
parallel queues, a scratch area containing a workqueue descriptor and work
items.

### Job

A `xe_sched_job` is the host submission object.

It contains:

- backpointer to exec queue
- DRM scheduler job object
- fence objects for completion tracking
- batch addresses
- per-width pointer slots for parallel jobs

### GuC ID

Each registered exec queue receives a GuC ID from the driver's GuC ID manager.
That GuC ID is the primary identifier used in host-to-GuC control messages and
GuC-to-host notifications.

The GuC ID is not the same thing as an LRC.
The LRC is hardware execution-state storage; the GuC ID is the firmware-visible
identifier for the registered queue.

GuC IDs are also a finite resource.
They are managed by the GuC ID allocator, so registration pressure and ID
availability are part of the practical scheduling constraint surface.

## Queue Classes And Placements

Userspace selects an engine class and placement set through
`struct drm_xe_engine_class_instance`.

Visible engine classes include:

- render
- copy
- video decode
- video enhance
- compute
- VM bind (kernel-only logical class)

The host driver validates the placement array and computes a logical engine mask
for the queue.

For parallel submission, placements must be logically contiguous.

VM bind queues are real schedulable kernel objects.
They compete for GuC IDs and submission bandwidth even though they are not
ordinary userspace render or compute queues.

## Scheduling Properties

The driver exposes per-queue scheduling properties at creation time through
user extensions.

Visible property controls include:

- priority
- timeslice

Internally the queue also tracks:

- preemption timeout
- job timeout

The visible host priority enum is:

- low
- normal
- high
- kernel

User-visible max priority is intentionally capped:

- `HIGH` if the caller has `CAP_SYS_NICE`
- otherwise `NORMAL`

The GuC-facing mapping used by the host driver is:

- `LOW -> GUC_CLIENT_PRIORITY_NORMAL`
- `NORMAL -> GUC_CLIENT_PRIORITY_KMD_NORMAL`
- `HIGH -> GUC_CLIENT_PRIORITY_HIGH`
- `KERNEL -> GUC_CLIENT_PRIORITY_KMD_HIGH`

Policy updates are sent through
`XE_GUC_ACTION_HOST2GUC_UPDATE_CONTEXT_POLICIES` using KLVs for:

- execution quantum
- preemption timeout
- scheduling priority
- preempt-to-idle on quantum expiry

Another visible GuC policy KLV is:

- SLPM GT frequency

Key source:

- `xe_exec_queue.c`
- `xe_guc_submit.c`
- `guc_klvs_abi.h`

## Long-Running Queues

Linux 6.12 also exposes a distinct long-running scheduling mode through
`XE_EXEC_QUEUE_SCHEDULE_MODE_LONG_RUNNING`.

That matters because long-running queues are not treated the same way as normal
timeout-monitored queues:

- they are a first-class scheduling distinction
- they bypass the ordinary DRM scheduler timeout path
- they should not be collapsed into the same lifecycle description as ordinary
  user queues

## Preempt-Fence Mode And Compute Queues

Linux 6.12 also exposes a preempt-fence mode tied to compute workloads.

That matters because:

- preempt-fence mode is not just another timeout setting
- compute exec queues can be tracked on the VM for rebind and preemption
  coordination
- long-running compute queues can participate in VM-driven preempt-fence flows
- this is host-side scheduling infrastructure with no obvious direct analogue
  in the AMD MES public spec

## Submission Model

### High-level flow

The Linux 6.12 Xe model deliberately keeps execution simpler than older DRM
drivers:

- no BO list is passed at exec time
- VM binds are separate operations
- dependencies are explicit through sync objects and dma-fence plumbing
- ring flow control is handled through DRM scheduler job limits

The high-level flow in `xe_exec.c` is:

1. parse `DRM_IOCTL_XE_EXEC`
2. resolve exec queue and sync objects
3. lock VM and validate / rebind as needed
4. create `xe_sched_job`
5. add dependency fences
6. arm the job
7. install out-fences and user fences
8. push the job into the scheduler

### VM_BIND as scheduled work

`DRM_IOCTL_XE_VM_BIND` is not just passive address-space bookkeeping.
VM_BIND work runs through a special kernel bind exec queue, participates in
fence dependencies, and consumes scheduler resources.

### Single-LRC submission

For a non-parallel queue:

- the GuC submission backend updates the LRC ring tail in memory
- if needed, it enables the context through GuC
- it then sends `XE_GUC_ACTION_SCHED_CONTEXT`

The CT action is the notification or kick.
It is not the payload of the submission itself; the ring-tail state is already
visible through the LRC memory image.

### Parallel submission

For a parallel queue:

- the backend builds a work item in a shared scratch buffer
- the scratch buffer contains a GuC scheduler workqueue descriptor
- the work item includes GuC ID and per-LRC ring tail state
- the backend updates the shared workqueue tail
- if needed, it enables the context
- it then kicks scheduling

This means parallel submission is not just "multiple rings."
It has a firmware-visible shared scratch/workqueue format.

Key source:

- `xe_exec.c`
- `xe_sched_job.c`
- `xe_guc_submit.c`
- `xe_guc_submit_types.h`

## GuC Control Interface

From the host driver's point of view, the important GuC scheduler actions are:

- `XE_GUC_ACTION_REGISTER_CONTEXT`
- `XE_GUC_ACTION_REGISTER_CONTEXT_MULTI_LRC`
- `XE_GUC_ACTION_DEREGISTER_CONTEXT`
- `XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET`
- `XE_GUC_ACTION_SCHED_CONTEXT`
- `XE_GUC_ACTION_HOST2GUC_UPDATE_CONTEXT_POLICIES`

The host/GuC protocol is bidirectional.
The host sends H2G registration, mode, submit, and policy actions, and the
GuC sends G2H completion, reset, and error notifications back.

### Context registration

Registration sends GuC enough information to schedule the queue:

- context index / GuC ID
- engine class
- logical submit mask
- LRC descriptor address
- for parallel queues:
  - workqueue descriptor address
  - workqueue base
  - workqueue size
  - additional LRC descriptors

### Context mode set

The host uses `XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET` to:

- enable scheduling for a queue
- disable scheduling for a queue

The driver expects `SCHED_CONTEXT_MODE_DONE`-style completion behavior and
tracks pending enable / pending disable state around it.

### Context kick

The host uses `XE_GUC_ACTION_SCHED_CONTEXT` to tell GuC there is work ready on
the queue.

This is the "submit/kick" control point after ring tail or parallel workqueue
updates are already visible.

### Policy update

The host uses `UPDATE_CONTEXT_POLICIES` KLVs to change:

- scheduling priority
- execution quantum
- preemption timeout

This is the main evidence that Xe's public host contract is per-context-policy
oriented rather than exposing a larger MES-like process/gang resource API.

### G2H notifications

Visible GuC-to-host completion and error notifications include:

- scheduling mode change completion
- deregister completion
- context reset notification
- scheduling-done style notifications
- engine memory CAT error notification

## Queue State Model

The host driver keeps an explicit queue state bitmap in `xe_guc_submit.c`.

Important state bits include:

- registered
- enabled
- pending enable
- pending disable
- destroyed
- suspended
- reset
- killed
- wedged
- banned
- check-timeout
- extra-ref

This is a bitmap-based lifecycle, not a simple linear ladder.
Pending enable and pending disable can overlap, destroyed queues may still need
cleanup work, and failure states can be reached from multiple points.

It is still the best host-visible equivalent to queue lifecycle documentation,
but it should not be simplified to a single linear activation sequence.

## Suspend, Resume, Disable, Deregister

The GuC submission backend serializes control operations through scheduler
messages:

- cleanup
- set scheduling properties
- suspend
- resume

Suspend / resume is not a raw immediate CT write from arbitrary context.
The DRM scheduler run queue still orders jobs, but a separate `xe_sched_msg`
path serializes backend control operations such as suspend, resume, cleanup,
and property changes.

Important behavior visible in the code:

- suspend may wait for a minimum run period before disabling scheduling
- suspend completion is reported through a waitqueue
- resume may require re-enabling scheduling
- cleanup may disable scheduling and then deregister the context

The minimum-run-period behavior matters because the host may not be able to
disable scheduling immediately after enable; firmware behavior still shapes the
timing constraints around suspend.

## Timeout And Reset Model

Timeout handling is host-driven.
The visible timeout clock and timeout decision path live on the host side, with
GuC participation only through notifications and context-state changes.

The DRM scheduler timeout path:

- stops submission
- may disable scheduling to sample timestamp state
- checks whether the job actually exceeded timeout
- may re-enable scheduling if the timeout was a false alarm
- otherwise logs timeout, captures devcoredump, bans queue, and triggers reset

GuC-originated G2H indications include:

- context reset notification
- scheduling-done style notifications
- engine memory CAT error notification
- related reset and failure paths

The host then maps those notifications into queue state transitions, cleanup,
fence erroring, or GT reset.

The host is the reset authority:

- it detects timeout
- it decides whether the queue is banned
- it drives deregistration and cleanup
- it initiates GT reset when needed

The GuC participates by sending notifications and by performing the
hardware-level context-switch mechanics, including saving and restoring LRC
state across preemption boundaries.

This is different from AMD's public MES reset API style.
Here, the host/GuC split is visible, but the scheduler firmware's internal
policy for hang detection is not fully exposed.

## Fences And Synchronization

The Xe scheduling path is fence-heavy and explicit.

Visible pieces:

- `dma_fence`
- `dma_fence_chain`
- DRM scheduler finished fence
- syncobj / timeline syncobj
- user fences
- queue last-fence tracking

Important consequence:

- Xe submission depends on explicit fence plumbing more than on implicit BO-list
  synchronization
- VM bind and exec are designed to look similar from userspace, both using
  explicit sync objects

This is part of what this note needs to document, because the scheduler
behavior is inseparable from the fence model.

## What Xe Exposes Less Clearly Than AMD MES

Compared to the AMD MES public spec shape, the Xe 6.12 driver exposes much less
about firmware-internal scheduler state.

What is visible:

- queue registration ABI
- per-context policy ABI
- enable/disable / submit / deregister ABI
- reset and notification ABI
- workqueue format for parallel submission

What is not clearly visible from driver source alone:

- exact GuC arbitration algorithm
- global priority band accounting
- starvation and carryover policy
- internal process / gang scheduler database
- detailed hardware resource partition policy

So the honest framing is:

> host-visible queue and GuC context scheduling contract

not:

> full firmware scheduler internals

## Working S32 GuC Submit Model

If we need a compact internal model for later `r32-hws` / `s32` comparison,
the Xe side currently looks like this:

- schedulable object: `exec_queue`
- address-space object: `VM`
- execution object: `xe_sched_job`
- host scheduler: DRM scheduler + Xe wrapper
- backend control queue: `xe_sched_msg`
- firmware endpoint: GuC CT actions
- firmware identifier: GuC ID
- scheduling knobs: priority, timeslice, preempt timeout,
  preempt-to-idle-on-quantum-expiry
- queue lifecycle: bitmap-based registered/enabled/pending/failure state
- parallel execution model: shared scratch workqueue with multi-LRC items
- completion model: fence signaling plus GuC G2H notifications
- failure model: timeout sampling, queue ban, deregister, GT reset

## Reverse-Engineering Gaps

Areas still not fully documented from the current source pass:

- exact GuC G2H message payload semantics beyond the handlers the driver uses
- whether GuC internally groups queues into a process-like or gang-like object
  comparable to AMD MES terminology
- how arbitration differs between render, compute, copy, and media classes
  under
  heavy contention
- whether there is a stronger public distinction between kernel queues, VM bind
  queues, long-running queues, and ordinary user queues than the host code
  currently reveals
- how much of scheduling policy is hard-coded in firmware rather than derived
  from host-supplied context policy KLVs

## Next Source Targets

If we want a deeper `s32` pass later, the next files to study are:

- `xe_guc_submit.c`
- `xe_exec.c`
- `xe_exec_queue.c`
- `xe_sched_job.c`
- `xe_wait_user_fence.c`
- `xe_hw_fence.c`
- `xe_vm.c`
- `xe_pt.c`
- GuC trace/debugfs output once the driver runs on hardware

## Final Rule

Treat this note as a reverse-engineered host/GuC submission contract derived
from Linux 6.12 Xe, not as an official Intel scheduler architecture manual.
