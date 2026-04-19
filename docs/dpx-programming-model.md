# DPX Programming Model

Date: 2026-04-19

## Purpose

This note defines `DPX` as the abstract programming model that sits above:

- GPU-side scheduler + DMA architectures
- CPU-side accelerator scheduler + DMA architectures
- software-emulated fallback implementations

The immediate motivation is to preserve one common mental model across:

- GPU backends using `HWS + GDMA`
- CPU accelerator backends using `DLB + DSA`
- fallback backends using `eventdev + ioat`
- pure software backends using `eventdev + CPU load/store`

This is not claiming these backends are identical.
It is claiming they are similar enough at the queueing / ordering / continuation
layer that one abstract runtime model is justified.

## Related Notes

This umbrella note defines the abstract model.
The backend-specific DPX notes are:

- `docs/dpx-api.md`
  - concrete API and capability model
- `include/dpx.h`
  - draft public header for the DPX API shape
- `docs/dpx-g32.md`
  - shared GPU-side DPX semantics between `r32` and `s32`
- `docs/dpx-r32.md`
  - AMD `MES/HWS + SDMA` mapping
- `docs/dpx-s32.md`
  - Intel Xe `GuC-visible queue model + blit/copy` mapping
- `docs/dpx-dlb-dsa.md`
  - `DLB + DSA` CPU-accelerator mapping
- `docs/dpx-eventdev.md`
  - generic `eventdev` scheduler fallback mapping
- `docs/dpx-eventdev-ioat.md`
  - `eventdev + ioat` accelerated fallback mapping

Suggested read order:

1. this note
2. `dpx-api.md`
3. `include/dpx.h`
4. `dpx-g32.md`
5. the specific backend note you care about

## Short Answer

Yes, this abstraction is worth defining.

The strongest honest summary is:

> `DPX` should be the abstract task-channel / async-transfer / continuation
> programming model above multiple concrete backends.

That means:

- `DPX` is not `DLB + DSA`
- `DPX` is not `AMD MES + SDMA`
- `DPX` is not `Intel Xe GuC + blit`
- `DPX` is the common queueing and continuation model that those backends can
  approximate

The right way to think about it is:

- define the programming model once
- map it onto hardware backends when available
- emulate it with software when needed

## The DPX Opacity Rule

No backend-specific type, handle, descriptor, packet, or action code should
appear in the public DPX API.

That means the public model must not expose:

- raw AMD `MES` packets
- raw Intel Xe `GuC` CT actions
- raw `DLB` queue or port objects
- raw `DSA` descriptors
- raw `ioat(4)` descriptors
- raw `eventdev` port operations

Backends are allowed to use all of those internally.
But if they leak into the DPX public contract, the abstraction is already
broken.

So the rule is:

> DPX is allowed to standardize the control model.
> It is not allowed to standardize raw backend mechanisms.

## The 16-Byte Scheduler Event Envelope

There is one structural convergence that matters more than it first appears.

The strongest scheduler-oriented CPU-side backends already converge on a small,
fixed event envelope:

- Intel `DLB` events are 16 bytes
- DPDK `rte_event` is 16 bytes

Both carry the same class of scheduler-visible metadata:

- compact user payload / handle
- queue identity
- scheduling mode
- priority
- flow identity

That is not an accidental resemblance.
`DLB` is the reference hardware substrate behind the broader `eventdev` model,
so DPX inherits a natural compact dispatch envelope from both.

This is why DPX should treat the scheduler-visible event as:

- routing metadata
- plus a compact handle to the real work item

and not as the full payload itself.

## Why The Abstraction Is Real

The similarity is strongest at the scheduler-visible control layer:

- enqueue work to a schedulable channel
- preserve ordering by flow where required
- route work by priority and class
- offload transfer-like operations to a separate engine when possible
- receive completion
- continue the pipeline

That pattern appears in different forms across:

- GPU HWS + DMA designs
- DLB + DSA
- DPDK eventdev + DMA engines
- software event schedulers plus CPU memcpy/memfill

The similarity is weaker at the execution-model layer:

- GPU hardware schedules execution contexts
- DLB / eventdev schedule events to CPU workers
- software fallback schedules work in host threads

So `DPX` is a truthful abstraction only if it stays centered on:

- channels
- ordering
- priorities
- transfer stages
- completion routing
- continuation

and does **not** pretend that all backends provide the same:

- context preemption
- VM model
- device-local memory
- reset semantics

## DPX Core Objects

These are the objects that should remain backend-independent.

### 1. DPX domain

A DPX domain is the execution scope containing:

- schedulable channels
- worker-facing ports/endpoints
- transfer engines
- completion routing
- error routing
- backpressure accounting

On different backends this may map to:

- GPU process/VM or queue-group scope
- DLB scheduling domain
- DPDK eventdev instance
- software scheduler instance

### 2. Task channel

A task channel is the main schedulable object in DPX.

It carries:

- scheduling mode
  - atomic
  - ordered
  - parallel/unordered
  - directed/service
- ordering contract
  - total
  - per-flow
  - none
- flow policy
- priority
- preferred transfer backend
- continuation target
- error target

This is the user-visible queue-like object.

It is intentionally not named after:

- a DLB queue
- a GPU HQD/MQD/LRC
- an eventdev queue

because each backend maps it differently.

### 3. Work item

A work item is the payload object.

It should live in shared memory or backend-owned memory and contain:

- opcode
- source and destination buffer references
- length / shape
- dependency metadata
- flow ID
- completion cookie
- success continuation
- failure continuation

The scheduler-visible event should carry only:

- handle/pointer/ID
- routing metadata
- priority
- flow information

That rule is consistent across:

- DLB
- DPDK eventdev
- software schedulers

and is a reasonable abstraction for GPU-side submission as well.

### 4. Buffer reference

A buffer reference is an opaque handle to memory used by a work item.

This is required because different backends resolve memory differently:

- GPU backends:
  - VM-bound GPU buffers or backend-owned allocations
- `DLB + DSA`:
  - host memory with IOMMU/SVA-aware resolution where applicable
- `eventdev + ioat`:
  - kernel-managed DMA-addressable memory
- pure software fallback:
  - host pointer or software-owned allocation

So DPX must not define the work item around raw pointers alone.

The common contract should be:

- work item carries `dpx_buffer_ref`
- backend adapter resolves it to backend-specific addressing/materialization

### 5. Transfer stage

This is the part of DPX that corresponds to:

- GPU SDMA / BLIT
- DSA
- IOAT
- CPU memcpy / memset fallback

The abstract operation set should stay modest:

- copy
- fill
- compare / validate
- null / marker / fence-like stage
- optionally checksum / CRC if the backend supports it

Do not let the DPX abstraction depend on exotic backend-specific transfer
features in the first version.

### 6. Completion edge

Every async transfer or async stage must produce a continuation edge.

This is the central DPX rule:

- no backend is complete unless completion can re-enter the scheduler model

Depending on backend this may be:

- hardware / firmware notification
- DSA completion polling
- IOAT callback
- software future / task completion

But the DPX runtime should normalize all of them into:

- success continuation
- failure continuation

What DPX must still expose about completion:

- completion mode
  - fence
  - polling
  - callback
  - software signal
- advisory latency class
- failure scope
- reinjection/backpressure behavior

### 7. Error domain

Different backends fail differently.

So DPX needs one explicit error-routing abstraction for:

- transfer failure
- scheduling congestion
- backend reset/loss
- worker fault
- unsupported feature request

The common abstraction must include failure scope, not just failure existence:

- task/channel-local
- transfer-engine-local
- domain-local
- whole-device

## DPX Backend Capabilities

Every backend pair must provide a capability query.

At minimum, the query should expose:

- scheduler type:
  - `gpu_firmware`
  - `hardware_event`
  - `software_event`
- transfer type:
  - `gpu_dma`
  - `dsa`
  - `ioat`
  - `cpu_memcpy`
- maximum task channels
- maximum flow ID space
- supported scheduling modes:
  - atomic
  - ordered
  - parallel
  - directed
- supported transfer operations:
  - copy
  - fill
  - compare
  - crc
  - null
- completion mode:
  - fence
  - polling
  - callback
  - software signal
- supports preemption
- supports queue/channel grouping
- supports memory registration
- advisory completion latency
- expected failure scope model

## Backend Selection Matrix

The right model is not a single linear ladder.
It is a scheduler-backend plus transfer-backend pairing.

The preferred pairs are:

| Scheduler backend | Transfer backend | Tier | Notes |
| --- | --- | --- | --- |
| GPU firmware scheduler | GPU DMA engine | 1 | Highest-fidelity native GPU path |
| DLB hardware | DSA | 2 | Strongest CPU-side hardware backend |
| DLB hardware | CPU memcpy | 2a | Keeps scheduler hardware when DSA is absent |
| eventdev hardware PMD | ioat | 3 | Generic hardware scheduler plus host DMA |
| eventdev hardware PMD | CPU memcpy | 3a | Scheduler acceleration without DMA engine |
| eventdev software PMD | ioat | 4 | Software scheduler with DMA-backed transfer |
| eventdev software PMD | CPU memcpy | 5 | Widest portability, lowest performance |

The runtime should select the best available pair at initialization time,
subject to capability and policy.

## Backend Selection Lifetime

Backend choice is an initialization-time or domain-creation-time decision.

The correct rule is:

- a DPX domain is created on one scheduler/transfer pair
- that pair does not change for the lifetime of the domain
- in-flight work is not migrated across backends
- switching backends means:
  - drain or destroy the current domain
  - create a new domain on a different backend pair

So DPX should promise:

- API-compatible backend selection

It should not promise:

- transparent mid-flight backend migration
- identical latency or throughput after a backend change

### Pair A: Native GPU DPX

This is the highest-fidelity backend.

Representative shape:

- scheduler side:
  - AMD MES/HWS
  - Intel Xe GuC submission / scheduler-visible queue model
- transfer side:
  - AMD SDMA
  - Intel blit/copy engines

Strengths:

- real device-side async engines
- hardware-managed queue/context machinery
- integrated completion/fault behavior relative to the device
- best performance and lowest continuation overhead

Limits:

- backend-specific VM and memory model
- backend-specific reset semantics
- queue object is not identical across AMD and Intel

Conclusion:

- this is the best target for DPX
- but it must still be treated as a backend, not the definition of DPX itself

### Pair B: CPU Accelerator DPX

Representative shape:

- scheduler side:
  - Intel DLB
- transfer side:
  - Intel DSA

Strengths:

- strong scheduler resource model
- explicit priorities / flow ordering / service lanes
- real async transfer engine
- close structural match to the DPX control model

Limits:

- schedules events, not GPU execution contexts
- no GPU VM or device-local memory
- DSA completion must be bridged back into the scheduler in software

Conclusion:

- this is the strongest non-GPU hardware backend for early DPX work

### Pair C: Fallback Accelerated Host DPX

Representative shape:

- scheduler side:
  - DPDK `eventdev`
- transfer side:
  - FreeBSD `ioat(4)`
  - or DPDK `dma/ioat` where deployment actually supports it

Why this is plausible:

- DLB is already a DPDK eventdev backend in the broader ecosystem
- DPDK eventdev explicitly abstracts both hardware and software event
  schedulers
- the local DPDK tree contains:
  - `drivers/event/dlb2`
  - `drivers/event/sw`
  - `drivers/dma/idxd`
  - `drivers/dma/ioat`
- the local DPDK eventdev API describes:
  - event devices
  - event queues
  - event ports
  - atomic / ordered / parallel scheduling
  - flow-based ordering
  - dynamic load balancing
- the local FreeBSD ports tree for `net/dpdk` installs:
  - `rte_eventdev`
  - `dpdk-test-eventdev`
  - `librte_dma_ioat.so`
- the FreeBSD base system ships `ioat(4)` with:
  - multiple DMA channels per package
  - sequential operations per channel
  - copy / blockfill / CRC / null operations
  - callback completion
  - interrupt coalescing
  - `DMA_FENCE` for dependent operations

Important limits:

- `eventdev + ioat` is not a drop-in replacement for `DLB + DSA`
- the scheduler side is generalized software/hardware eventdev, not DLB-specific
- the transfer side on FreeBSD is not identical to Linux `idxd`
- userspace and kernel boundary choices matter much more here

Conclusion:

- this is a legitimate accelerated fallback target
- especially for FreeBSD-hosted experimentation on server hardware with IOAT

### Pair D: Pure Software DPX

Representative shape:

- scheduler side:
  - DPDK software eventdev PMD
  - or a project-owned software scheduler
- transfer side:
  - CPU memcpy / memset / compare
  - optional software CRC/checksum

Strengths:

- widest portability
- easiest deployment
- allows DPX application code to run even without accelerator hardware

Limits:

- highest CPU cost
- weakest fidelity to the hardware continuation model
- no true async transfer engine unless software worker threads emulate one

Conclusion:

- this backend is mandatory if DPX is meant to be a real portable programming
  model rather than a hardware-only experiment

## Why `eventdev` Is The Right Scheduler Fallback

This is the key architectural observation.

From the local DPDK eventdev API and guide:

- an event device can be hardware or software-based
- eventdev exposes:
  - event queues
  - event ports
  - flow IDs
  - queue IDs
  - priorities
  - atomic / ordered / parallel scheduling
  - enqueue / forward / release style event movement
- eventdev is explicitly designed for:
  - automatic multicore scaling
  - dynamic load balancing
  - pipelining
  - order maintenance
  - synchronization
  - prioritization / QoS

That is exactly why it is the right scheduler fallback for DPX.

It preserves the important part of the model:

- event-routed staged work

without requiring DLB hardware.

Also useful:

- DPDK's `rte_event` is a compact 16-byte event representation
- that aligns well with the design rule already established for DLB:
  - event carries handle and routing
  - real payload lives elsewhere

## Why `ioat` Is A Serious DMA Fallback

FreeBSD `ioat(4)` is not just a vague "DMA exists" story.
The local manpage and headers show a real DMA engine interface with:

- engine acquisition / release
- per-channel sequential operation ordering
- copy
- blockfill
- CRC
- null descriptors
- callbacks
- interrupt coalescing
- explicit dependency fencing with `DMA_FENCE`

That means `ioat` can play the role of:

- lower-performance transfer backend
- host-side async copy backend
- FreeBSD-native DMA backend for DPX experiments

This is not equivalent to DSA.
But it is close enough to be a meaningful DPX transfer backend.

## What Must Stay Backend-Specific

If DPX is to survive real implementation pressure, it must keep these outside
the common contract.

### 1. Memory model

Do not require one universal memory model across:

- GPU VM / residency
- DSA host memory
- IOAT host DMA
- pure CPU load/store

DPX should abstract transfers and dependencies, not force one VM story.

### 2. Preemption and context scheduling

GPU backends may have:

- real queue/context preemption
- hardware residency
- device scheduling state

CPU/event backends do not.

So DPX must not promise those semantics globally.

### 3. Reset / recovery semantics

Each backend has its own failure model:

- GPU hang/reset
- DLB credit starvation
- DSA completion or IOMMU fault
- IOAT engine reset or callback failure
- software worker crash

DPX should normalize errors into one error domain, but not pretend that
recovery behavior is identical.

### 4. Completion transport

Some backends have:

- firmware/hardware completion signals

Others need:

- polling
- callback injection
- software continuation threads

DPX must normalize the result, not the transport.

## Recommended DPX API Shape

The first DPX API should look like a task-channel runtime, not like:

- raw GPU queue packets
- raw DSA descriptors
- raw IOAT descriptors
- raw eventdev port calls

Suggested first-layer API concepts:

- create / destroy domain
- query backend capabilities
- create / destroy task channel
- register / unregister buffer reference
- submit work item to task channel
- declare transfer operation
- declare success continuation
- declare failure continuation
- query completion
- observe error domain / congestion state

Backend-specific APIs should live below that layer.

## Recommended Backend Selection Order

For the project, the practical order should be:

1. define DPX at the abstract model and API layer
2. implement the widest-portability pair
   - `eventdev_sw + CPU memcpy`
3. validate the strongest CPU-side hardware pair
   - `DLB + DSA`
4. add the FreeBSD-leaning accelerated fallback pair
   - `eventdev + ioat`
5. only then make GPU backends consume the same DPX object model

Reason:

- the CPU/eventdev/ioat side is easier to instrument
- it forces discipline about which semantics are truly abstract
- it prevents the API from accidentally becoming GPU-only

## Bottom Line

The strongest correct summary is:

> `DPX` is a real abstraction if it is defined as a task-channel +
> async-transfer + completion-routing model above multiple backends.

And:

> `GPU HWS + DMA`, `DLB + DSA`, `eventdev + ioat`, and `eventdev + CPU copy`
> can all be treated as members of the same backend family, but only at the
> control-model layer, not as identical execution architectures.

And:

> backend switching should be treated as API-compatible and capability-aware,
> not automatically performance-transparent.

So the correct project direction is:

- make `DPX` the abstract programming model
- treat hardware pairs as backend implementations
- require a software fallback
- let `eventdev` be the scheduler fallback and `ioat`/CPU copy be the transfer
  fallback

That is the clean way to make the model portable without lying about what the
hardware really is.
