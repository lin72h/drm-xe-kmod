# DPX Agent Handoff

Date: 2026-04-19

## Purpose

This document onboards the separate agent responsible for the `DPX` subproject.

`DPX` is a parallel architecture and runtime-programming-model effort. It is
related to the Xe port because it studies GPU scheduler + DMA programming
models, but it is not part of the immediate FreeBSD Xe driver port.

The current Xe agent should return focus to:

- Linux 6.12 Intel Xe on FreeBSD `drm-kmod`
- GuC / CT / BO / VM_BIND / FreeBSD LinuxKPI bring-up
- upstreamable FreeBSD-first driver-porting work

The new DPX agent should own:

- the DPX programming model
- DPX API shape
- DPX backend family analysis
- first prototype planning
- compatibility between hardware-backed and software-backed DPX paths

## Core Thesis

`DPX` is the abstract task-channel / async-transfer / completion-routing model
above several backend families:

- GPU scheduler + DMA backends
  - AMD `MES/HWS + SDMA` (`r32`)
  - Intel Xe `GuC-visible queue model + blit/copy` (`s32`)
- CPU accelerator backend
  - Intel `DLB + DSA`
- scheduler fallback backend
  - DPDK `eventdev`
- accelerated host fallback backend
  - `eventdev + ioat`
- pure software fallback backend
  - `eventdev + CPU memcpy/memfill`

The project is not claiming these are identical execution architectures.

The honest claim is narrower:

> These backends share enough queueing, ordering, async-transfer, completion,
> and continuation structure that one upper programming model is justified.

## Boundary With The Xe Port

Do not let DPX distort the FreeBSD Xe port.

The Xe port remains:

- FreeBSD-first
- Linux 6.12 semantic baseline
- `drm-kmod` / LinuxKPI shaped
- upstream-oriented
- stock-FreeBSD-first

The Xe port should not start depending on:

- DPX
- DMO/DMI
- Mach VM concepts
- a new GPU runtime API
- DPX memory abstractions

DPX may learn from Xe scheduler and blit/copy semantics, but Xe bring-up must
remain a normal FreeBSD DRM driver-porting effort.

## Current Local Files

Primary DPX documents:

- `docs/dpx-programming-model.md`
  - umbrella DPX architecture note
- `docs/dpx-api.md`
  - API sketch and capability model
- `include/dpx.h`
  - draft public header for the DPX API
- `docs/dpx-g32.md`
  - shared GPU-side semantic layer between AMD `r32` and Intel `s32`
- `docs/dpx-r32.md`
  - AMD `MES/HWS + SDMA` mapping
- `docs/dpx-s32.md`
  - Intel Xe `GuC-visible queue model + blit/copy` mapping
- `docs/dpx-dlb-dsa.md`
  - Intel `DLB + DSA` CPU-accelerator mapping
- `docs/dpx-eventdev.md`
  - generic DPDK `eventdev` scheduler fallback
- `docs/dpx-eventdev-ioat.md`
  - `eventdev + ioat` accelerated FreeBSD fallback

Review and prompt documents:

- `docs/opus-dpx-review.md`
  - OPUS review feedback on DPX
- `docs/opus-glm-dpx-review-prompt.md`
  - reusable copy/paste prompt for future OPUS / GLM review

Related context documents:

- `docs/r32-hws.md`
  - AMD official RDNA3+ Micro Engine Scheduler specification converted to MD
- `docs/s32-guc-submit.md`
  - reverse-engineered Intel Xe GuC submission working spec
- `docs/s32-vs-r32-hws.md`
  - comparison of AMD `r32` and Intel `s32` scheduler models
- `docs/dlb-dsa-cpu-hws-programming-model.md`
  - deeper DLB + DSA analysis

Current git state at handoff:

- the DPX files are newly created and currently uncommitted
- keep future DPX artifacts in this repo if they are source/docs/API-sized
- put large captures, build products, generated corpora, and benchmark output
  in the parent directory, not in git

## Suggested Read Order

Start with:

1. `docs/dpx-programming-model.md`
2. `docs/dpx-api.md`
3. `include/dpx.h`
4. `docs/opus-dpx-review.md`
5. `docs/dpx-eventdev.md`
6. `docs/dpx-dlb-dsa.md`
7. `docs/dpx-eventdev-ioat.md`
8. `docs/dpx-g32.md`
9. `docs/dpx-r32.md`
10. `docs/dpx-s32.md`

Then read the source references listed below.

## Local Source References

Use these local trees before web research:

- `../nx/dlb`
  - Intel DLB local source / SDK tree
- `../nx/linux-6.12/drivers/dma/idxd`
  - Linux DSA / `idxd` driver
- `../nx/linux-6.12/include/uapi/linux/idxd.h`
  - Linux DSA UAPI
- `../nx/dpdk-25.11/lib/eventdev/rte_eventdev.h`
  - DPDK eventdev API
- `../nx/dpdk-25.11/lib/eventdev/rte_event_dma_adapter.h`
  - DPDK event-DMA adapter API
- `../nx/dpdk-25.11/doc/guides/prog_guide/eventdev/eventdev.rst`
  - DPDK eventdev programming guide
- `../nx/dpdk-25.11/doc/guides/prog_guide/eventdev/event_dma_adapter.rst`
  - DPDK event-DMA adapter programming guide
- `../nx/dpdk-25.11/drivers/event/dlb2`
  - DLB2 eventdev PMD
- `../nx/dpdk-25.11/drivers/event/sw`
  - software eventdev PMD
- `../nx/dpdk-25.11/drivers/dma/idxd`
  - DPDK DSA / IDXD DMA PMD
- `../nx/dpdk-25.11/drivers/dma/ioat`
  - DPDK IOAT DMA PMD
- `/usr/src/share/man/man4/ioat.4`
  - FreeBSD `ioat(4)` manpage
- `/usr/src/sys/dev/ioat/ioat.h`
  - FreeBSD IOAT kernel interface
- `/usr/ports/net/dpdk`
  - FreeBSD ports integration for DPDK

## Current Design Decisions

### 1. DPX API Is Backend-Opaque

The public DPX API must not expose raw backend mechanisms.

Do not expose:

- AMD `MES` packets
- Intel Xe `GuC` CT actions
- DLB queue / port objects
- DSA descriptors
- IOAT descriptors
- eventdev port operations

Backend adapters can use these internally, but they must not become public DPX
types.

### 2. Backend Selection Is Init-Time

Backend switching is API-compatible and capability-aware, not transparent.

A DPX domain is created on one scheduler/transfer backend pair. That pair
stays fixed for the lifetime of the domain.

To switch backend:

- drain or destroy the current domain
- create a new domain on a different backend pair

Do not design for mid-flight backend migration.

### 3. Buffer References Are Registered Handles

DPX work items should carry `dpx_buffer_ref`, not raw backend addresses.

The backend adapter resolves the buffer reference into:

- GPU VA / BO / VM binding
- DSA-accessible host memory
- IOAT bus-DMA mapping
- CPU pointer

This is the main mechanism that keeps memory-model differences out of the
public work-item format.

### 4. Backpressure Is Explicit

DPX submit must be allowed to accept:

- zero work items
- some work items
- all work items

Applications must handle `would block` / partial submit behavior.

This is required because backend capacity differs:

- GPU queue/ring depth
- DLB credits
- eventdev queue / port pressure
- DSA or IOAT transfer queue depth
- software queue limits

### 5. Ordering Must Be Explicit

DPX channels should declare their ordering contract:

- total order
- per-flow order
- no order

This avoids pretending GPU queues, DLB atomic/ordered flows, and eventdev
ordering scopes are identical.

### 6. Completion Is Normalized, Not Identical

DPX should normalize completion into:

- success continuation
- failure continuation
- completion batch polling
- error batch polling

But the transport remains backend-specific:

- GPU fence / notification
- DSA completion polling
- eventdev enqueue / forward / release
- IOAT callback
- software signal

### 7. `eventdev` Is Important Prior Art

DPDK `eventdev` already gives DPX:

- event queues
- event ports
- priorities
- flow IDs
- atomic / ordered / parallel scheduling
- enqueue / forward / release semantics

DPDK `rte_event_dma_adapter` is especially important because it already models
a bridge between:

- event scheduling
- DMA submission
- DMA completion reinjection into event scheduling

That is the same structural problem DPX must solve.

## Backend Family Summary

### GPU `r32`: AMD MES/HWS + SDMA

Strengths:

- richest published scheduler control model
- explicit process / gang / queue concepts
- formal scheduler API
- formal logging and reset semantics
- SDMA gives real transfer-engine mapping

Risk:

- DPX must not over-flatten AMD's process/gang hierarchy
- richer grouping should become optional capability, not mandatory DPX surface

### GPU `s32`: Intel Xe GuC + Blit/Copy

Strengths:

- `xe_exec_queue` maps naturally to a DPX task channel
- GuC submission backend gives a real queue lifecycle
- blit/copy engines provide GPU transfer-stage mapping

Risk:

- scheduler policy is more opaque than AMD MES
- long-running queues and preemption behavior need careful capability modeling
- do not confuse Xe's host-driven reset authority with AMD firmware-driven
  reset APIs

### CPU Accelerator: DLB + DSA

Strengths:

- strongest non-GPU hardware validation backend
- DLB provides scheduler fabric
- DSA provides async transfer engine
- easier to instrument than GPU bring-up

Critical deployment constraints:

- DSA user work queues require SVA/PASID support
- Linux `idxd` direct portal `mmap()` is gated by `user_submission_safe` or
  `CAP_SYS_RAWIO`
- unsafe devices block batch descriptors
- software bridge from DSA completion back to DLB is mandatory

### Generic Scheduler Fallback: eventdev

Strengths:

- preserves task-channel / flow / priority / continuation semantics
- has both hardware and software PMDs
- gives a portable scheduler substrate for CI and correctness tests

Cost:

- software PMD uses a service core
- performance is not equivalent to DLB hardware

### FreeBSD Accelerated Fallback: eventdev + ioat

Strengths:

- FreeBSD has `ioat(4)` for DMA copy / fill / CRC / null operations
- useful on Xeon systems without DSA
- preserves the DPX mental model with lower performance

Implementation rule:

- start with `eventdev + CPU memcpy`
- only add `ioat` after profiling justifies the user/kernel integration cost

## Immediate Next Tasks For DPX Agent

### Task 1: Harden `include/dpx.h`

Turn the current sketch into a more coherent `v0` API draft.

Add or refine:

- error codes
- flags
- capability bit definitions
- lifecycle comments
- ownership rules
- batch submission semantics
- buffer lifetime rules
- completion/error polling semantics
- versioning / ABI marker

Do not prematurely freeze ABI.

### Task 2: Define The First Prototype Scope

The recommended first implementation order is:

1. `eventdev + CPU memcpy`
2. `DLB + DSA`
3. `eventdev + ioat`
4. GPU backends later

Reason:

- `eventdev + CPU memcpy` is easiest to deploy
- it pressure-tests the API without hardware-specific shortcuts
- `DLB + DSA` then proves hardware acceleration can fit the same API
- `ioat` should wait until the fallback path works

### Task 3: Write A Prototype Plan

Create a concrete plan for:

- one domain
- two task channels
  - one ordered/per-flow
  - one parallel
- one transfer opcode
  - copy
- registered source/destination buffers
- partial submit / backpressure handling
- completion polling
- error polling
- shared functional tests across at least two backends

### Task 4: Define Test Policy

Carry forward the project testing policy:

- existing tests should keep passing
- new performance-insensitive tests can use Elixir
- performance-sensitive tests should use Zig
- upstream-facing FreeBSD tests should prefer FreeBSD-friendly conventions
  such as shell, Python, ATF, kyua, or existing project test patterns

For DPX specifically:

- correctness tests should run on the software/eventdev path first
- latency/throughput tests should be backend-specific
- completion and backpressure tests are mandatory

### Task 5: Produce A Memory Model Note

The biggest abstraction leak is memory.

Create `docs/dpx-memory-model.md` or fold equivalent content into
`docs/dpx-api.md`.

It should define how `dpx_buffer_ref` maps to:

- GPU buffer / VM binding
- DSA host memory
- IOAT bus-DMA mapping
- CPU pointer

### Task 6: Produce An Error Model Note

Create `docs/dpx-error-model.md` or fold equivalent content into
`docs/dpx-api.md`.

It should define:

- item-local errors
- channel-local errors
- transfer-engine-local errors
- domain-local errors
- whole-device errors
- retry policy
- teardown policy
- recovery policy

## Strategic Questions For DPX Agent

Answer these early:

1. Is the current `dpx.h` shape sufficient to implement `eventdev + CPU memcpy`
   without backend leaks?
2. Can `eventdev + CPU memcpy` and `DLB + DSA` pass the same functional test
   suite through the same API?
3. Does `dpx_buffer_ref` need to be a small integer handle, pointer-sized
   token, or opaque struct?
4. Should completion polling be domain-wide only, or should per-channel polling
   also exist?
5. What is the minimum telemetry surface needed to debug backpressure?
6. Does `rte_event_dma_adapter` map cleanly enough to influence the DPX
   adapter contract?
7. Should `g32` stay as a separate document, or merge into the umbrella until
   shared GPU adapter code exists?
8. What feature set must remain optional capabilities rather than common DPX
   guarantees?

## Non-Goals

Do not attempt these in the DPX first phase:

- implement a GPU backend
- modify the FreeBSD Xe port
- replace DRM / TTM / VM_BIND concepts
- define a universal VM model
- promise performance transparency across backends
- support mid-flight backend migration
- expose raw backend descriptors in public DPX API
- make DPX depend on DMO/DMI or Mach-native GPU architecture

## Expected Outputs

The DPX agent should leave behind:

1. hardened `include/dpx.h` draft
2. updated `docs/dpx-api.md`
3. memory-model note
4. error-model note
5. prototype plan for `eventdev + CPU memcpy`
6. follow-up prototype plan for `DLB + DSA`
7. backend capability matrix
8. shared functional test plan
9. benchmark plan for latency / throughput / backpressure
10. clear list of what remains backend-specific forever

## Final Rule

The shortest correct summary is:

> DPX is a backend-opaque task-channel + async-transfer + completion-routing
> programming model. Prove it first with software/eventdev, then DLB+DSA, then
> optional ioat, and only later GPU backends. Keep the Xe port independent.
