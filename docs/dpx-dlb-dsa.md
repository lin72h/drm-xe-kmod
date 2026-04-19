# DPX DLB + DSA: CPU-Accelerator Mapping

Date: 2026-04-19

## Purpose

This note maps the abstract `DPX` model onto:

- Intel `DLB + DSA`

This is the CPU-side accelerator backend, not the Intel GPU backend.

Primary local basis:

- `docs/dpx-programming-model.md`
- `docs/dlb-dsa-cpu-hws-programming-model.md`
- `../nx/dlb`
- `../nx/linux-6.12/drivers/dma/idxd`

## Short Answer

`DLB + DSA` is a strong DPX backend at the control-model layer.

The strongest honest summary is:

> `DLB + DSA` is the best current non-GPU hardware backend for DPX because
> DLB gives a real scheduler fabric and DSA gives a real async transfer engine,
> but software still has to compose them.

## DPX Mapping

The backend maps like this:

| DPX object | `DLB + DSA` backend mapping |
| --- | --- |
| DPX domain | DLB scheduling domain plus runtime-owned DSA resources |
| Task channel | runtime task channel backed by a DLB queue |
| Work item | shared-memory work record referenced by compact events |
| Transfer stage | DSA descriptor submission on a work queue |
| Completion edge | software bridge from DSA completion into DLB continuation event |
| Error domain | DLB congestion + DSA completion/error + worker failure |

## Why This Backend Is Valuable

### 1. It preserves the scheduler mental model

DLB exposes:

- domains
- queues
- ports
- priorities
- ordered/atomic/unordered/directed delivery
- credits and backpressure

That makes it a strong scheduler substrate for DPX.

### 2. It preserves the async transfer mental model

DSA exposes:

- dedicated work queues
- descriptor submission
- completion records
- copy/fill/compare class operations

That makes it a real transfer-stage backend, not just software memcpy.

### 3. It is the best place to prove the abstraction before GPU integration

This backend is easier to:

- instrument
- stress
- benchmark
- debug

than a full GPU DPX backend.

## Structural Limits

This backend is not a GPU execution backend.

The important non-equivalences are:

- DLB schedules events to CPU workers, not execution contexts to GPU engines
- there is no GPU VM or device-local memory
- there is no hardware completion chaining between scheduler and transfer
- software must bridge DSA completion back into DLB

So this backend is a strong DPX implementation, but not a literal GPU-HWS
equivalent.

## Operational Constraints

The backend has real deployment constraints.

From the current local Linux 6.12 `idxd` review:

- shipping DSA devices set `user_submission_safe = false`
- direct portal `mmap()` needs `CAP_SYS_RAWIO`
- user WQ enablement depends on SVA/PASID
- `DSA_OPCODE_BATCH` is blocked on unsafe devices

So a real DPX adapter must support:

- conservative `write()` submission paths
- privileged/internal deployment modes
- fallback to other transfer backends if DSA is unavailable

## Recommended Backend Rule

The correct implementation rule is:

> Use DLB as the scheduler backend, DSA as the transfer backend, and let the
> DPX runtime own channel semantics, completion routing, and failure handling.

This backend should be treated as:

- a first-class DPX implementation
- the best hardware validation backend before GPU-side rollout

## Bottom Line

The strongest correct summary is:

> `DLB + DSA` is the strongest CPU-side DPX backend.

And:

> it preserves the queueing, ordering, async-transfer, and continuation
> semantics well enough that software written to DPX can genuinely benefit
> from the hardware when the backend is enabled.
