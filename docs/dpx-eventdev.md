# DPX Eventdev: Generic Scheduler Fallback

Date: 2026-04-19

## Purpose

This note defines the generic scheduler fallback for `DPX` using:

- DPDK `eventdev`

Primary local basis:

- `docs/dpx-programming-model.md`
- `../nx/dpdk-25.11/lib/eventdev/rte_eventdev.h`
- `../nx/dpdk-25.11/lib/eventdev/rte_event_dma_adapter.h`
- `../nx/dpdk-25.11/doc/guides/prog_guide/eventdev/eventdev.rst`
- `../nx/dpdk-25.11/doc/guides/prog_guide/eventdev/event_dma_adapter.rst`

## Short Answer

`eventdev` is the right generic scheduler fallback for DPX.

The strongest honest summary is:

> when DLB hardware is unavailable, `eventdev` preserves the important DPX
> scheduler semantics well enough to keep the mental model and much of the
> code shape intact.

## Why `eventdev` Fits DPX

The local DPDK eventdev docs explicitly describe:

- a hardware or software-based event scheduler
- event queues
- event ports
- flow IDs
- queue IDs
- priority
- atomic / ordered / parallel scheduling
- dynamic load balancing
- pipelining
- order maintenance

That is already very close to the scheduler half of DPX.

## DPX Mapping

The mapping is:

| DPX object | `eventdev` mapping |
| --- | --- |
| DPX domain | eventdev instance |
| Task channel | event queue plus runtime-owned metadata |
| Worker endpoint | event port |
| Work item | shared-memory payload referenced by a compact event |
| Completion edge | enqueue/forward/release into the next queue |
| Error domain | software-defined error queue/path |

The runtime still owns:

- work-item layout
- continuation rules
- backpressure policy
- transfer backend selection

## Why This Preserves The Mental Model

With `eventdev`, DPX software can still think in terms of:

- task channels
- flows
- priorities
- ordered vs parallel stages
- continuations between stages

That means applications can keep the same upper model whether the scheduler is:

- DLB hardware
- eventdev hardware PMD
- eventdev software PMD

## What `eventdev` Does Not Give DPX

This backend is scheduler-only.

It does **not** provide:

- a DMA engine
- a GPU execution context model
- device-side completion chaining

So it must be paired with one of:

- DSA
- `ioat`
- CPU memcpy / memset fallback
- another transfer backend

## Relevant Prior Art: `rte_event_dma_adapter`

DPDK already contains direct prior art for one of the hardest DPX problems:

- bridging an event scheduler and a DMA engine
- turning DMA completion back into scheduler-visible continuation events

The local tree includes:

- `rte_event_dma_adapter`
- `RTE_EVENT_DMA_ADAPTER_OP_NEW`
- `RTE_EVENT_DMA_ADAPTER_OP_FORWARD`

That matters because it proves the following pattern is not hypothetical:

- event is routed into a DMA-capable stage
- DMA work is submitted
- completion is dequeued or observed by an adapter
- completion is re-enqueued into the event pipeline

This is not the whole of DPX:

- DPX spans GPU backends too
- DPX may use `ioat` or CPU memcpy instead of DPDK `dmadev`
- DPX still needs its own portable API and error model

But it is strong prior art for the completion-edge and bridge pattern.

In practice this means:

- the `eventdev` fallback path is not inventing the bridge concept from scratch
- DPDK already exposes both software-mediated and capability-aware adapter
  patterns that DPX can learn from

## Deployment Cost

The software eventdev backend has a real runtime cost.

From the local DPDK software-eventdev docs:

- the software eventdev is a centralized scheduler
- it requires a service core to perform event distribution
- it does not support distributed scheduling

That means the pure-software scheduler fallback is not free:

- one CPU core may be consumed primarily for scheduling work
- scheduling latency and throughput are materially different from hardware DLB

This is acceptable for a fallback path, but it must be treated as a deployment
cost, not hidden overhead.

## Why This Matters For Portability

This is the key strategic point.

If DPX depends directly on:

- DLB queue APIs
- GuC CT messages
- AMD MES packets

then there is no meaningful fallback.

If DPX instead depends on:

- task channels
- flow ordering
- continuations
- priorities

then `eventdev` can preserve most of the software shape even on platforms
without dedicated scheduler hardware.

## Recommended Implementation Rule

The `eventdev` backend should be the baseline scheduler implementation for:

- software fallback
- non-DLB hosts
- test harnesses
- CI and correctness validation

That way, hardware scheduler support becomes:

- an acceleration backend

not:

- a hard requirement for the programming model to exist at all

## Bottom Line

The strongest correct summary is:

> `eventdev` is the generic scheduler fallback for DPX.

And:

> it is good enough to preserve the DPX task-channel, ordering, and
> continuation model even when dedicated scheduler hardware is missing.
