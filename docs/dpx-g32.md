# DPX G32: Shared GPU-Side Semantics

Date: 2026-04-19

## Purpose

This note defines `g32` as the shared DPX layer between:

- `r32`
  - AMD RDNA3+ `MES/HWS + SDMA`
- `s32`
  - Intel Xe `GuC-visible queue model + blit/copy`

The point of `g32` is not to erase the differences between AMD and Intel.
It is to identify the GPU-side scheduler + DMA semantics that are common enough
to support:

- one mental model
- one upper runtime shape
- one code path for DPX-facing software

while still letting the backend adapter map onto different hardware.

Important scope rule:

`g32` is not meant to be a large shared implementation layer.
It is meant to be a shared GPU-backend semantic contract that both GPU adapters
can implement under the common DPX API.

## Short Answer

`g32` is real, but only if it stays at the GPU control-model layer.

The strongest honest summary is:

> `g32` is the shared GPU-side DPX contract for queue-like submission,
> ordering, priority, async transfer stages, completion, and recovery
> boundaries.

It is **not**:

- a claim that AMD MES and Intel GuC expose the same firmware API
- a claim that AMD and Intel have the same object model
- a claim that one queue packet format can directly target both

## What `g32` Must Contain

These are the concepts that really do line up across `r32` and `s32`.

### 1. Firmware is in the scheduling path

Both backends put firmware in the scheduling loop.

That means `g32` can assume:

- host software does not fully own scheduling
- queue lifetime crosses a host/firmware boundary
- reset and recovery are partly firmware-mediated

### 2. The host creates schedulable queue-like objects

Both backends have a host-visible schedulable object:

- `r32`
  - queue within a broader process/gang model
- `s32`
  - `xe_exec_queue`

So `g32` can standardize on:

- task-channel creation
- task-channel destruction
- task-channel policy update
- task-channel enable/disable semantics

without requiring the same internal queue object.

### 3. Priority and time-slicing matter

Both backends expose:

- priority
- fairness / timeslice / quantum policy
- suspend/resume/reset style control

The exact knobs differ, but the semantic intake is shared.

### 4. Transfer engines are distinct from normal execution lanes

Both backends have a meaningful notion of:

- normal execution queues / contexts
- separate copy/DMA-style engines

So `g32` can treat transfer work as:

- first-class async stages
- with separate completion and continuation

### 5. Completion and recovery are not optional

Both backends require:

- fence-like completion handling
- timeout or hang detection
- cleanup and recovery flow

So `g32` should explicitly model:

- completion edge
- error edge
- reset / teardown boundary

### 6. Shared GPU task-channel semantic fields

If `g32` is to remain a separate document, it needs to define real common
semantic fields for GPU adapters.

Those fields should include:

- task-channel identity
- backend-visible queue identity
- DPX priority class
- scheduling mode
- work-item opcode class
- transfer-engine preference
- completion target
- failure target
- backend-private attachment for VM / resource / firmware state

This is not a raw packet format.
It is the minimum semantic record both GPU adapters should be able to consume.

### 7. Shared completion-edge representation

Both GPU adapters should normalize completion edges into one common shape:

- completion mode:
  - fence-like
  - notification-driven
- completion status:
  - success
  - retryable failure
  - fatal failure
- continuation target
- advisory latency class
- failure scope

This is where `g32` is stronger than the generic DPX layer:

- both GPU backends already live in a fence / scheduler / reset world
- both can expose a GPU-oriented completion shape even if the mechanisms differ

### 8. Shared priority classes

The common DPX priority classes for GPU backends should be:

- background
- normal
- interactive
- system

These are semantic classes, not raw backend values.

Suggested adapter mapping:

| DPX priority class | `r32` tendency | `s32` tendency |
| --- | --- | --- |
| background | low | low |
| normal | normal | normal |
| interactive | focus | high |
| system | real-time or privileged scheduler class | kernel/high, backend-specific |

This table is deliberately approximate.
It is an adapter mapping guide, not a claim of exact scheduler equivalence.

### 9. Shared GPU error classes

Both GPU adapters should classify failures into a common GPU-oriented set:

- channel timeout
- channel reset
- channel banned / disabled
- engine failure
- device reset required
- memory / binding failure
- unsupported capability

That gives the upper runtime a stable error vocabulary without pretending
AMD and Intel recover the same way.

## What `g32` Must Not Contain

If `g32` tries to abstract these too aggressively, it stops being honest.

### 1. One universal scheduler database

`r32` exposes:

- process
- gang
- queue
- resource programming

`s32` exposes mostly:

- `exec_queue`
- LRC
- job
- GuC-visible queue identity

So `g32` must not require:

- one public process/gang hierarchy
- one shared firmware packet vocabulary

### 2. One universal VM model

Do not define `g32` around:

- AMD VMID / GDS / GWS concepts
- Intel Xe VM / exec queue / VM bind internals

The correct abstraction is:

- transfer references
- dependencies
- completion

not one shared VM contract.

### 3. One universal reset authority model

`r32` is much closer to a published scheduler-firmware control surface.
`s32` is much more host-driven with opaque GuC arbitration underneath.

So `g32` can normalize:

- reset requested
- queue/channel failed
- queue/channel resumed or torn down

but not the exact reset ownership model.

## DPX Mapping For `g32`

At the shared GPU layer, the DPX objects map like this:

| DPX object | `g32` meaning |
| --- | --- |
| DPX domain | GPU-side execution scope under one device/runtime instance |
| Task channel | queue-like schedulable submission object |
| Work item | batch / dispatch / submission payload visible to the host path |
| Transfer stage | async copy/fill/blit stage on a DMA-like engine |
| Completion edge | fence / notification / completion signal that re-enters the runtime |
| Error domain | timeout / hang / reset / teardown path |

The important thing is that the DPX runtime can keep these semantics stable:

- submit work to a task channel
- preserve flow/ordering when requested
- route transfer stages to a transfer engine
- consume completion as a continuation
- propagate failure to an error path

## What Can Be Shared At The `g32` Layer

This is the real payoff, but it is mostly adapter contract and upper-runtime
shape rather than a large GPU-only code layer.

Software above the backend adapter should be able to share:

- task-channel lifecycle API
- work-item format or at least work-item semantic fields
- completion-edge representation
- priority classes
- error-classification scheme
- continuation graph / pipeline DAG
- dependency representation
- success/error continuation handling
- telemetry shape
- backend selection policy

This is how the project gets to:

- write once against DPX
- run on real hardware when available
- select a GPU backend at runtime initialization time
- keep one API surface while adapting to backend capability differences

## What Must Stay Backend-Specific Under `g32`

Backend adapters still have to own:

- firmware packet or CT message format
- queue registration details
- VM and memory binding details
- engine placement details
- reset and teardown procedures
- firmware-specific telemetry and debug interfaces

That is acceptable.
The upper model is still shared even when the adapter code is different.

## Recommended Implementation Rule

The right rule for `g32` is:

> Keep the DPX-facing API queue-like and continuation-driven.
> Push the firmware and VM differences down into the backend adapter.

That is the only way to keep:

- the mental model stable
- the code portable across real GPU backends
- the software fallback meaningful

## Bottom Line

The strongest correct summary is:

> `g32` is the shared GPU-side DPX semantic layer between `r32` and `s32`.

And:

> it is strong enough to support one upper programming model, but not strong
> enough to erase AMD-vs-Intel scheduler, VM, and recovery differences.

That is fine.
The point of `g32` is shared semantics and shared software shape, not fake
hardware symmetry.
