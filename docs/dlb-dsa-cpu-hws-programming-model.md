# DLB + DSA Event-Driven Async Work Runtime

Date: 2026-04-19

## Purpose

This note studies whether the GPU-style:

- hardware scheduler
- user queue
- async copy / DMA engine

programming model can be replicated on the CPU side using:

- Intel DLB as the hardware scheduler
- Intel DSA, via Linux `idxd`, as the DMA engine

Local source basis:

- `docs/r32-hws.md`
- `docs/s32-guc-submit.md`
- `docs/s32-vs-r32-hws.md`
- `../nx/dlb`
- `../nx/linux-6.12/drivers/dma/idxd`
- `../nx/linux-6.12/include/uapi/linux/idxd.h`

This is an architecture/design note.
It is not claiming that DLB + DSA is identical to AMD MES or Intel Xe GuC
submission.

## Short Answer

Yes, **a meaningful CPU-side analogue** can be built out of:

- `DLB` for hardware-assisted queue scheduling and delivery
- `DSA` for asynchronous copy/fill/compare-style DMA execution
- host software glue for queue state, dependencies, completions, and memory
  ownership

But it is only honest to say this replicates the **programming model shape**,
not the exact hardware contract.

The closest correct statement is:

> DLB + DSA can implement an event-driven async work runtime whose object model
> feels like HWS + DMA, but the composition is software-owned.

That means:

- DLB is the scheduler half
- DSA is the DMA/copy half
- the runtime is the thing that makes them behave like one system

This is much closer to a:

- hardware-assisted event runtime

than to:

- a literal GPU firmware scheduler clone

## The Core Distinction

GPU HWS designs usually integrate all of these into one device/runtime story:

- queue registration
- scheduling policy
- hardware execution context dispatch
- DMA/copy engine work
- completion / fault / reset handling
- memory model assumptions

DLB + DSA do **not** arrive as one integrated scheduler/engine pair.

Instead:

- `DLB` schedules **events to CPU consumers**
- `DSA` executes **descriptor-driven memory operations**
- host software must connect:
  - submission
  - continuation
  - completion routing
  - backpressure
  - ordering
  - error propagation

That is the architectural truth.

## What DLB Gives You

From the local DLB guide and `libdlb` API:

- a **scheduling domain**
- **load-balanced queues**
- **directed queues**
- **load-balanced ports**
- **directed ports**
- credit pools
- consumer queues (CQ)
- explicit event scheduling types:
  - `SCHED_ATOMIC`
  - `SCHED_UNORDERED`
  - `SCHED_ORDERED`
  - `SCHED_DIRECTED`

Important DLB facts from the local tree:

- queue elements are compact event records
  - the guide describes a `QE` as a 16-byte unit
- `dlb_event_t` carries:
  - user payload fields
  - destination queue ID
  - scheduling type
  - priority
  - flow ID
- a load-balanced queue supports:
  - atomic + ordered
  - or atomic + unordered
  - but not all three at once
- ordered queues are created by giving the queue non-zero sequence numbers
- a load-balanced port can link up to 8 load-balanced queues
- a directed port receives from one directed queue
- load-balanced events must be:
  - released
  - forwarded
  - or token-returned correctly
- CQ depth, event-state entries, and credits are finite and can deadlock a bad
  pipeline

So DLB is a real hardware queue scheduler, not just a software work queue.
It already exposes:

- queue topology
- scheduling modes
- credits / backpressure
- priorities
- directed vs load-balanced delivery

In that respect, DLB exposes an explicit resource model:

- domains
- queues
- ports
- credits
- priorities
- scheduling modes

that structurally resembles what a host-visible HWS API would look like.

That is more explicit than the host-visible Xe GuC contract, where more policy
is hidden behind firmware.

But the similarity is structural, not functional:

- DLB schedules events to CPU consumers
- Xe GuC schedules execution contexts to GPU engines

## What DSA / `idxd` Gives You

From the local `idxd` sources and UAPI:

- a device with one or more **work queues**
- work queue attributes including:
  - `type`
    - `kernel`
    - `user`
  - `mode`
  - `priority`
  - `size`
  - `threshold`
  - `group_id`
  - `max_transfer_size`
  - `max_batch_size`
- descriptor submission through a **portal**
- aligned **completion records**
- completion status codes
- descriptor opcodes such as:
  - `DSA_OPCODE_MEMMOVE`
  - `DSA_OPCODE_MEMFILL`
  - `DSA_OPCODE_COMPARE`
  - `DSA_OPCODE_COMPVAL`
  - `DSA_OPCODE_DUALCAST`
  - `DSA_OPCODE_BATCH`
  - `DSA_OPCODE_DRAIN`
  - CRC/DIF-related operations

Important local `idxd` facts:

- the driver has explicit `kernel` and `user` work queue types
- work queue priority is relative within a group
- user WQ access is exposed through the char device path
- `idxd_cdev_open()` binds SVA/PASID for the process when supported
- `idxd_cdev_mmap()` maps a work-queue portal page
- `idxd_cdev_write()` submits user descriptors
- `poll()` exposes error notification
- the driver tracks completion records and error/event-log handling

So DSA gives:

- a real asynchronous descriptor engine
- with queueing, completion, and QoS controls

But DSA does **not** schedule arbitrary user work the way DLB schedules events.
It schedules only descriptor execution on the DMA engine.

## DSA Deployment Constraints

This is the first hard reality check.

The attractive user-space DSA path:

1. open user WQ
2. bind PASID/SVA
3. map the portal page
4. submit descriptors directly

is not universally available.

Important constraints from the local `idxd` tree:

- user work queues depend on IOMMU SVA/PASID support
- if SVA/PASID is not enabled, the driver refuses user-WQ enablement
- every shipping DSA device in Linux 6.12 sets `user_submission_safe = false`
- direct portal `mmap()` is therefore gated by the `user_submission_safe`
  check on all current DSA hardware
- when `user_submission_safe` is false, `idxd_cdev_mmap()` requires
  `CAP_SYS_RAWIO`
- `DSA_OPCODE_BATCH` is also blocked on unsafe devices

This means the first prototype must assume one of these realities:

- privileged/internal development environment with `CAP_SYS_RAWIO`
- kernel-mediated submission through `write()`
- a hybrid kernel/user design
- or future hardware where the user-submission path is relaxed

So the direct portal path is an optimization target, not the baseline
architectural assumption.

Stated plainly:

- a user-space-first direct-portal prototype cannot work on shipping Linux 6.12
  DSA hardware without root privileges
- or without falling back to a kernel-mediated submission path

## The Best-Fit Mapping

The honest mapping is:

| GPU/HWS concept | CPU-side analogue | Implemented by |
| --- | --- | --- |
| scheduling domain / VAS-like execution scope | DLB scheduling domain | DLB |
| schedulable queue object | task channel backed by a DLB queue | DLB + runtime |
| queue ordering mode | atomic / ordered / unordered / directed queue type | DLB |
| worker / engine dispatch target | DLB port bound to a CPU thread or service lane | DLB + runtime |
| async copy engine | DSA work queue | DSA / `idxd` |
| DMA descriptor ring / submission path | DSA descriptors and work-queue portal | DSA / `idxd` |
| queue continuation after DMA | completion bridge event | runtime |
| queue state machine | software task-channel/work-item records | runtime |
| memory ownership / dependency graph | software task metadata | runtime |
| per-flow serialization | DLB ordered or atomic flow handling | DLB + runtime |
| dedicated-engine affinity | DLB directed queue + dedicated DSA WQ | DLB + DSA + runtime |

The key pattern is:

- DLB decides **who should look at the work next**
- DSA performs **the memory operation**
- software decides **what the next work item is**

## Proposed Object Model

If this is to become a project-owned programming model, use a **software
submission object** above both accelerators.

Suggested objects:

### 1. Runtime domain

Contains:

- one DLB scheduling domain
- one or more DSA work queues
- software memory pools / descriptor slabs
- completion pollers / injectors
- credit/backpressure accounting

### 2. Task channel

A runtime object, not a raw hardware queue.

Contains:

- destination DLB queue ID
- scheduling mode:
  - atomic
  - ordered
  - unordered
  - directed
- flow key policy
- priority
- optional preferred DSA work queue
- continuation target on success
- error target on failure

Important:

- this is the CPU-side analogue of a user queue
- it is **not** identical to a DLB queue
- the DLB queue is only the scheduler-visible destination
- the task channel is the user-facing contract
- work items are submitted to task channels, not to raw DLB queues

### 3. Work item

Must be stored in shared memory, because DLB events are too small to carry a
full rich work packet.

Suggested work-item payload:

- opcode
  - CPU callback
  - DSA memmove
  - DSA memfill
  - compare/validate
  - hybrid stage
- source and destination pointers
- transfer size
- dependency count or predecessor handles
- flow key
- completion cookie
- next queue on success
- next queue on failure

Then the DLB event carries only:

- work-item handle or pointer ID
- queue ID
- scheduling mode
- priority
- flow ID

### 4. Completion bridge

This is mandatory.

It watches DSA completion records and re-injects follow-on events into DLB.

Without this bridge, DLB and DSA remain two unrelated accelerators.

But the bridge is also the critical coupling point between two independent
backpressure systems.

It must:

- poll or receive DSA completion notifications
- translate completion status into runtime status
- acquire DLB credits before emitting continuation events
- avoid blocking DSA completion processing when DLB is temporarily congested
- route failures into an error path without losing ownership of the work item

It should be treated as two closely related pieces:

- a DSA completion poller
- a DLB continuation injector

not as an incidental helper thread.

### 5. Credit manager

This is the object that prevents the runtime from deadlocking itself.

It tracks the coupled capacity state across:

- DLB credits
- DLB CQ depth
- DLB event release/pop obligations
- DSA work-queue depth
- DSA completion-record availability
- internal continuation-buffer occupancy

Without an explicit credit/backpressure manager, a stressed DLB + DSA pipeline
will eventually hit a state where each side is waiting for the other to drain.

### 6. Error domain

The runtime needs a unified error-handling path for:

- DSA descriptor failures
- DSA page faults / IOMMU faults
- DLB credit starvation or queue congestion
- worker crashes or stalled stages
- timeout and retry decisions

DLB and DSA do not provide one shared recovery model.
The runtime has to.

## Recommended Execution Path

The cleanest programming model looks like this:

### Stage 1: submit

User or runtime:

1. allocates a work item from shared memory
2. fills high-level operation metadata
3. sends a DLB event to the chosen queue

### Stage 2: dispatch

A worker thread attached to a DLB port:

1. receives the event from DLB
2. resolves the work-item pointer
3. decides whether this stage is:
   - CPU work
   - DSA work
   - mixed work

### Stage 3A: CPU work

If it is CPU work:

1. execute directly on the worker
2. optionally forward to another DLB queue
3. release the current event correctly

### Stage 3B: DSA work

If it is DSA work:

1. build a DSA descriptor
2. choose the target DSA work queue
3. submit through:
   - portal
   - or kernel-mediated write path
4. mark the work item pending completion
5. release or transition the DLB-side token according to runtime policy
6. account for outstanding DSA depth and continuation-credit needs

### Stage 4: completion

The completion subsystem:

1. polls or handles DSA completion records
2. translates completion status into runtime status
3. places ready continuations into a bounded internal buffer if DLB is
   temporarily congested
4. emits a new DLB event to:
   - a success queue
   - or an error queue

### Stage 5: continuation

Another DLB-dispatched worker:

1. receives the completion event
2. continues the pipeline
3. retires or recycles the work item

This is the exact place where the DLB + DSA pair becomes HWS-like.

## Completion Bridge Cost Model

The bridge is not free.

A full DSA-offloaded stage transition includes at least:

1. descriptor preparation and submission
2. DSA execution latency
3. completion polling or wakeup latency
4. continuation injection into DLB
5. DLB delivery to the next worker

So the runtime must assume:

- very small data movement may be faster inline on CPU
- DSA offload only wins once transfer size and overlap are large enough
- the bridge cost is part of the steady-state pipeline cost, not a side detail

The first prototype should measure:

- DLB send-to-receive latency
- DSA submit-to-completion latency
- full DSA completion to DLB continuation latency

before making any policy claims about when offload is worthwhile.

Working estimate from the current review pass:

- a full bridge-mediated stage transition is likely on the order of
  `5-15 us`
- DSA offload likely loses to direct `memcpy` below roughly `64 KB-256 KB`
  transfers

Those are not promises.
They are planning estimates that need validation on target hardware.

## Where DLB Maps Surprisingly Well

DLB is a strong match for several HWS ideas:

### 1. Ordered vs unordered vs atomic traffic

This is directly exposed in DLB.

That means:

- per-object serialization
- ordered stages
- flow-key pinning
- parallel work classes

can be represented in hardware queue scheduling rather than only in software
locks.

### 2. Directed lanes

Directed queues/ports are a good analogue for:

- dedicated service lanes
- completion lanes
- affinity-sensitive workers
- control-plane queues

### 3. Hardware backpressure

Credits, CQ depth, and event-state limits force the runtime to respect queue
capacity honestly.

That is much better than pretending the scheduler is unbounded.

### 4. Forward / release model

DLB's forward/release semantics are well-suited for pipeline stages.

That gives the runtime a natural way to model:

- consume
- continue
- retire

instead of collapsing everything into generic thread-pool tasks.

## Where DSA Maps Well

DSA is a good analogue for:

- `SDMA`
- `BLIT`
- copy/fill/compare side engines

especially for:

- memmove
- memfill
- compare/validate
- copy + integrity side operations

This makes it a reasonable CPU-side substitute for:

- offloaded data movement stages
- staging buffers
- copy chains
- memory transform phases

But the DSA offload path is only attractive when its total round-trip is
honestly better than doing the work inline on a CPU worker.

That round-trip includes:

- descriptor preparation
- submission
- DSA execution
- completion polling or wakeup
- continuation injection back into DLB

So the runtime must expect a crossover point:

- tiny transfers should stay on CPU
- larger transfers may justify DSA

The exact threshold is hardware- and topology-dependent and must be measured,
not assumed.

## What This Does Not Replicate

This is the most important section.

DLB + DSA does **not** replicate the following GPU-HWS properties:

### 1. Hardware execution context management

There is no GPU-style:

- LRC/HQD/MQD context scheduling
- hardware context save/restore
- queue residency management
- engine context preemption

DLB schedules **events**, not execution contexts.

Any GPU-HWS concept that depends on:

- hardware context save/restore
- residency management
- preemption of in-flight execution
- context-local timeout handling

does not have a direct DLB analogue.

### 2. Integrated VM / address-space control

There is no GPU-style:

- per-queue GPU VM
- VM bind page-table programming
- residency / eviction into VRAM
- device-local memory manager

DSA uses host memory and IOMMU/PASID/SVA semantics, not GPU-local VM.

### 3. Integrated scheduler+DMA firmware

In GPU HWS designs, the scheduler and DMA/copy engines are part of one device
story.

Here:

- DLB and DSA are separate devices/functions
- software owns the composition

### 4. Hardware completion chaining into scheduler

DSA completion does not automatically become a DLB event.
That bridge must be built in software.

### 5. Unified fault / reset / recovery model

GPU HWS often has integrated fault and reset logic.

Here:

- DLB has its own credits/CQ/token model
- DSA has its own WQ, completion, portal, and fault model
- software must unify errors and retries

### 6. Unified QoS / fairness model

DLB and DSA each expose their own priority or bandwidth controls.

They do **not** provide one shared scheduler policy across:

- event dispatch
- DMA queue depth
- completion reinjection

So fairness, starvation control, and service differentiation remain runtime
policy, not a hardware property.

## The Real Design Rule

The correct architecture rule is:

> Use DLB as the scheduler substrate.
> Use DSA as the copy-engine substrate.
> Put the actual queue semantics in a software runtime above both.

That keeps the design honest.

## Required Runtime Rules

If this becomes a real project technology, the runtime should follow these
rules:

### 1. Do not treat a DLB event as the whole work item

DLB events should carry:

- a handle
- routing fields
- small metadata

not the whole payload.

### 2. Do not expose raw DSA descriptors as the first user ABI

The first user ABI should be:

- a channel-level contract
- plus runtime work items

not raw DSA descriptors.

Reason:

- raw DSA descriptors are too low-level
- they bake in portal/completion details
- they make portability and validation harder
- task channels are the stable routing/scheduling object

### 3. Keep DLB and DSA backpressure separate but coupled

The runtime must track at least:

- DLB credits
- DLB CQ depth
- DLB release/pop obligations
- DSA WQ depth
- DSA completion record availability

Otherwise it will deadlock or livelock.

### 4. Keep ordering in DLB, not in ad hoc software locks

If a stage is:

- per-flow ordered
- or logically atomic

use DLB queue semantics and flow IDs first.

### 5. Use directed queues for service lanes

Good fits:

- completion bridge
- control-plane events
- error handling
- housekeeping / reclamation threads

### 6. Start with uniform event weight unless measurement proves otherwise

DLB event weight adds another capacity-planning dimension.

So the first prototype should prefer:

- uniform event weight
- simple CQ accounting

and only introduce variable weight after real latency and occupancy data
shows it is needed.

## Known Deadlock Modes

These need to be treated as design constraints, not debugging surprises.

### 1. Credit starvation loop

If:

- workers submit DSA work
- DSA completes
- the bridge cannot re-inject continuation events because DLB credits are
  exhausted
- downstream workers therefore never drain

then the pipeline can deadlock with both accelerators technically healthy.

### 2. CQ depth exhaustion

If workers keep ownership of DLB-side tokens while waiting on slow DSA work,
CQ slots can fill and DLB can stop delivering useful work even though the DSA
engine is still making progress.

### 3. Directed error-queue fan-in

If too many failures or retries funnel into one narrow directed queue, the
error path itself can become the blocking point for unrelated work.

### 4. DSA WQ full while DLB keeps dispatching

If DLB keeps feeding work faster than DSA WQ capacity drains, the runtime can
enter a retry storm unless admission control is tied to current DSA depth.

### 5. Device-loss asymmetry

If one side resets or wedges while the other side continues:

- DSA completions may arrive for a dead DLB pipeline
- DLB workers may continue dispatching into a dead DSA path

The runtime needs explicit cross-device failure correlation.

## Recommended First Prototype

The right first prototype is Linux-first and deployment-reality-first.

Do not start by assuming the direct user-portal fast path exists.

### Stage 0: deployment verification

Before building the runtime, verify:

1. DLB device presence and working userspace stack
2. DSA work queue availability
3. IOMMU SVA/PASID support for the target process model
4. whether direct portal `mmap()` is available or blocked by the
   `user_submission_safe` / `CAP_SYS_RAWIO` gate
5. whether dedicated user WQs are available
6. whether the first prototype must use `write()` submission instead

### Prototype scope

Build a small runtime with:

- one DLB scheduling domain
- one or two load-balanced queues
- one directed completion queue
- one DSA work queue
  - preferably a dedicated user WQ if available
- one producer thread
- N DLB worker threads
- one completion poller/injector path
- explicit credit/backpressure accounting

### Prototype flow

1. Producer allocates work item.
2. Producer emits DLB event.
3. Worker receives event and submits DSA memmove through the conservative path:
   - dedicated WQ if available
   - `write()` path first
   - direct portal only after it is proven usable
4. Completion subsystem polls DSA completion record.
5. Completion subsystem sends directed or load-balanced continuation event.
6. Final worker validates completion and retires item.

### Then add

- ordered queues with flow IDs
- multiple stages
- mixed CPU + DSA stages
- error path queue
- DSA backpressure tests
- DLB credit starvation tests

### Portability note

This prototype should be treated as Linux-only.

Neither the local DLB tree nor Linux `idxd` imply a ready FreeBSD portability
story for:

- device drivers
- PASID/SVA
- portal mapping rules
- userspace ABI expectations

If this ever becomes a FreeBSD target, that is a separate porting effort.

## Validation Checklist

The first questions to answer experimentally are:

1. Can a user process reliably:
   - open a DSA user WQ
   - obtain PASID/SVA
   - determine whether portal `mmap()` is actually permitted
   - fall back to `write()` submission if not
   - submit descriptors at the needed rate?
2. Can DLB event latency stay low enough that the runtime does not spend more
   time scheduling than moving data?
3. What transfer-size range actually justifies DSA offload versus direct CPU
   execution?
4. What is the right completion model:
   - polling
   - interrupt/eventfd style wakeup
   - hybrid?
5. How do DLB credits and DSA WQ depth interact under bursty load?
6. Which stages benefit from `directed` completion routing versus
   load-balanced continuation?
7. How much ordering can be kept in DLB before software sequencing is needed?

## Bottom Line

The strongest honest summary is:

> DLB + DSA can support an event-driven async work runtime whose work model is
> strongly analogous to HWS + DMA.

But also:

> the scheduler/engine integration is not in hardware; it must be built by the
> runtime.

So the right goal is **not**:

- "pretend DLB + DSA is literally GPU HWS"

The right goal is:

- "build a hardware-assisted CPU runtime whose queueing, ordering, and async
  copy behavior deliberately mirrors the HWS programming model where that
  analogy is actually valid"
