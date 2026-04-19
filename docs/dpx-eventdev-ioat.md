# DPX Eventdev + IOAT: Accelerated Host Fallback

Date: 2026-04-19

## Purpose

This note defines the accelerated host-side fallback for `DPX` using:

- DPDK `eventdev` for scheduling
- FreeBSD `ioat(4)` for async transfer

Primary local basis:

- `docs/dpx-programming-model.md`
- `docs/dpx-eventdev.md`
- `/usr/src/share/man/man4/ioat.4`
- `/usr/src/sys/dev/ioat/ioat.h`
- `/usr/ports/net/dpdk/pkg-plist`

## Short Answer

Yes, `eventdev + ioat` is a credible DPX fallback backend.

The strongest honest summary is:

> `eventdev + ioat` can preserve the DPX mental model on FreeBSD-hosted server
> hardware by combining a generic event scheduler with a real host DMA engine,
> even though it is weaker and more deployment-sensitive than `DLB + DSA`.

## Why This Backend Exists

This backend matters because it gives DPX a path on systems that have:

- no DLB
- no DSA
- but still have:
  - eventdev support through DPDK
  - IOAT hardware in FreeBSD

That means the software can keep:

- the same task-channel model
- the same continuation model
- the same backend-selection idea

even when the exact hardware pair changes.

## DPX Mapping

The mapping is:

| DPX object | `eventdev + ioat` mapping |
| --- | --- |
| DPX domain | eventdev instance plus one or more IOAT DMA engines |
| Task channel | event queue plus runtime metadata |
| Work item | shared-memory work record |
| Transfer stage | IOAT copy/blockfill/CRC/null operation |
| Completion edge | IOAT callback or completion wake routed back into eventdev |
| Error domain | eventdev congestion, IOAT operation failure, worker/runtime error |

## Why `ioat(4)` Is Real Enough For DPX

The local FreeBSD manpage and headers show:

- multiple DMA channels per CPU package
- sequential operation ordering on each channel
- engine acquire/release
- copy
- blockfill
- CRC and copy+CRC
- null operations
- interrupt and callback completion
- interrupt coalescing
- `DMA_FENCE` for dependent operation ordering

That is enough to make `ioat` a legitimate transfer backend for:

- copy stages
- fill stages
- marker/fence-like stages
- low-level integrity-assisted stages

## Important Integration Constraint

`ioat(4)` is not a DSA-style userspace descriptor path.

It is primarily a FreeBSD kernel DMA-engine interface.

That means a real DPX backend likely needs one of these shapes:

### 1. Hybrid user/runtime + kernel transfer shim

- user-space eventdev runtime
- kernel helper or service for IOAT submission/completion

### 2. Mostly-kernel backend

- kernel-resident scheduling/transfer adapter
- user-facing DPX control layer above it

### 3. Eventdev scheduler with CPU-copy fallback first

- keep `ioat` as an optional acceleration path
- preserve the software contract even if the IOAT integration is deferred

This should be treated as the preferred first implementation shape on
FreeBSD-hosted systems.

The practical rule is:

- start with `eventdev + CPU memcpy`
- make that path correct and testable first
- add `ioat` only after profiling shows that DMA offload is worth the added
  user/kernel integration cost

This is the biggest reason `eventdev + ioat` is a fallback backend rather than
the primary DPX implementation.

## Another Important Constraint: Callback Context

The local `ioat(4)` docs explicitly say it is invalid to submit new DMA work
from the callback context.

That means the DPX backend must not model IOAT completion as:

- recursive direct continuation submission from the callback itself

Instead it should do:

- callback records completion
- callback wakes or signals a runtime-owned continuation path
- runtime reinjects continuation through the scheduler

That actually aligns well with DPX's completion-edge model.

## Scheduler Side Strength

This backend still preserves the good part of DPX:

- event-routed staged work
- per-flow ordering
- priorities
- continuation through task channels

So even if the transfer side is weaker than `DSA`, the scheduling model can
remain consistent.

## When This Backend Is Worth Using

This backend is worth exploring when:

- the platform is FreeBSD-first
- DLB/DSA is absent or unavailable
- IOAT hardware is present
- preserving the DPX model matters more than absolute performance

That last point is important.

The point of this backend is not:

- beat `DLB + DSA`

The point is:

- preserve the programming model
- preserve upper-layer code shape
- enable API-compatible backend selection to better hardware later

## Recommended Implementation Rule

Treat `eventdev + ioat` as:

- an accelerated fallback backend
- a FreeBSD-hosted experimentation backend
- a bridge between pure software DPX and dedicated hardware DPX

Do not make it the semantic definition of DPX itself.

## Bottom Line

The strongest correct summary is:

> `eventdev + ioat` is a legitimate DPX fallback backend for FreeBSD-class
> server systems.

And:

> it is valuable not because it is the fastest path, but because it lets the
> project keep one mental model and one upper software shape while still having
> a real DMA-backed fallback below it.
