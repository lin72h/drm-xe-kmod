# Intel Xe on FreeBSD 6.12: Runtime Semantic Risks

Date: 2026-04-19

## Purpose

This memo tracks the runtime semantic risks for the Xe-on-FreeBSD port.

These are not simple "missing header" problems.
They are cases where code can compile and even load, but behave differently
from Linux under real GPU load.

Compile and link blockers are tracked separately in:

- [xe-compile-bootstrap-findings.md](xe-compile-bootstrap-findings.md)

One additional correction from the parent kernel review:

> The most likely early failure mode is not a missing symbol.
> It is attach failure, unload, IRQ, workqueue, fence, TTM, and cleanup
> ordering behaving almost-right and corrupting state.

The CT and BO path are still the first major runtime proofs, but operational
bring-up risk should be ranked with unwind and teardown semantics at the top.

## Highest Risk Areas

### 1. TTM BO Allocation And Placement

Xe BO allocation sits on the critical path for CT buffers, ADS buffers, GGTT
objects, and later BO-backed VM work.

Risk:

- CT, ADS, or simple BO creation can fail before any meaningful runtime path
  exists.
- placement may work in normal cases but fail when Xe asks for specific
  system-memory or VRAM behavior.
- early BO failures can be misread as firmware or CT bugs.

Required checks:

- verify Xe BO create paths against Linux 6.12
- test system-memory and VRAM BO allocation as standalone bring-up steps
- classify any CT or ADS allocation failure as a TTM/placement issue first

### 2. GGTT Pinning And Aperture Mapping

CT and simple firmware-visible objects depend on GGTT insertion and correct GPU
address visibility.

Risk:

- GGTT insertion can fail even when BO allocation succeeds.
- CT registration and GuC-visible addresses can be wrong if aperture or insert
  logic diverges.
- failures here will look like GuC transport problems but are often memory-map
  problems.

Required checks:

- validate `xe_ggtt` paths before submission
- verify CT and simple objects are pinned at expected GGTT addresses
- compare GGTT behavior against Linux A/B logs

### 3. GuC CT Buffer Allocation And Coherency

GuC command transport uses driver-allocated buffers visible to firmware.
It is the first shared-memory runtime gate after PCI/MMIO/firmware.

Risk:

- buffers must be DMA-accessible, correctly aligned, and coherent enough for
  GuC communication
- FreeBSD TTM/system-memory allocation may not satisfy the same assumptions
- GuC CT failures can look like firmware bugs when they are DMA/cache bugs
- CT may fail because BO allocation, GGTT mapping, or invalidation is missing,
  which is a DRM/TTM/LinuxKPI problem rather than a DMO problem

Required checks:

- compare GuC CT buffer placement with Linux A/B logs
- verify DMA mask, busdma tag, alignment, and cache behavior
- log GuC CT initialization progress in detail
- verify CT buffer GGTT pinning and invalidation before submission tests
- send at least one H2G message and classify whether G2H response handling,
  IRQ dispatch, or firmware status is the blocker

### 4. PCI BAR, MMIO, And Cache Attributes

Xe firmware and register access start with MMIO BAR mapping, and CT depends on
correct MMIO setup before shared-memory transport can work.

Risk:

- BAR mappings may use the wrong cache attributes
- MMIO reads can return bad values even if the mapping exists
- VRAM BAR and resizable BAR behavior can diverge from Linux

Required checks:

- capture Linux and FreeBSD BAR layout for A380
- verify `devm_ioremap` / `devm_ioremap_wc` behavior
- validate MMIO reads before blaming higher-level GuC or GT code

### 5. `ww_mutex`, WITNESS, And `drm_exec`

Xe depends heavily on `drm_exec` and wound/wait mutex semantics to lock VMs,
BOs, and reservation objects without deadlocking.

Risk:

- FreeBSD WITNESS may report false lock-order problems.
- Linux-style `ww_mutex` retry loops may not map perfectly to FreeBSD lock
  diagnostics.
- Multi-BO VM_BIND and exec paths may panic or livelock despite compiling.

Required checks:

- compare `drm_exec` and `ww_mutex` signatures against Linux 6.12
- run BO/VM_BIND stress under WITNESS/INVARIANTS
- inspect all FreeBSD-local `ww_mutex` behavior before submission tests

### 6. `dma_fence` Signaling Rules

Linux has strict expectations around fence signaling context, lock ownership,
sleepability, and callback execution.

Risk:

- FreeBSD may not enforce the same constraints.
- Incorrect signaling context can deadlock under scheduler, VM_BIND, or reset
  load.
- `dma_fence_chain` and `dma_fence_array` may exist but still diverge subtly.

Required checks:

- audit `dma_fence_signal()` and callback chains for IRQ-safe behavior
- verify `dma_fence_chain` and `dma_fence_array` against Linux 6.12 behavior
- stress syncobj timelines and VM_BIND fences
- verify callbacks do not sleep in forbidden contexts

### 7. `drm_sched`

Xe uses the DRM GPU scheduler for jobs, entities, scheduler fences, and timeout
handling.

Risk:

- FreeBSD's scheduler import may be present but mismatched at the API or
  semantic level.
- `run_job`, `free_job`, timeout, recovery, and scheduler stop/start paths may
  diverge.

Required checks:

- compare `struct drm_sched_backend_ops` to Linux 6.12
- compare `drm_sched_job`, `drm_sched_entity`, and `drm_sched_fence`
- test timeout and reset behavior after basic submission works

### 8. TTM Eviction And Memory Pressure

Xe depends on TTM for BO placement, validation, eviction, and VRAM/system
memory movement after the initial bring-up path is alive.

Risk:

- placement may work in normal cases but fail under memory pressure
- eviction may interact badly with FreeBSD VM pressure
- shrinker-like behavior may not match Linux

Required checks:

- test BO allocation under memory pressure
- test VRAM oversubscription after basic bring-up
- keep first milestone below eviction/stress complexity

### 9. Workqueue Flush And Cancel Semantics

Xe uses Linux workqueues for GuC work, GT work, cleanup, rebind, and teardown.

Risk:

- `flush_workqueue`, `cancel_work_sync`, delayed work cancellation, and
  self-rescheduling work items may behave differently on FreeBSD taskqueues
- attach failure and module unload may race with live work items

Required checks:

- test repeated load/unload cycles early
- test attach failure cleanup paths
- verify all workqueues are flushed before memory teardown

### 10. `devm_*` And `drmm_*` Cleanup Ordering

Xe relies on Linux managed-resource cleanup for attach failure and detach.

Risk:

- cleanup order must be LIFO and match Linux's assumptions
- wrong order can cause use-after-free during attach failure or module unload

Required checks:

- audit FreeBSD LinuxKPI `devm_*` action ordering
- audit DRM `drmm_*` action ordering
- force early attach failures and verify cleanup is stable

### 11. Runtime PM

FreeBSD's LinuxKPI runtime PM support is mostly stub-like.

Risk:

- Linux Xe assumes `pm_runtime_get` / `put` state transitions.
- FreeBSD should not pretend dGPU runtime suspend works initially.

Required stance:

- treat the GPU as always-on at first
- document runtime PM as unsupported
- avoid adding suspend/resume promises to the first milestone

### 12. Wait Queues And Signal Handling

Xe uses Linux wait primitives for user-visible waits.

Risk:

- `wait_event_interruptible` and timeout behavior may differ under FreeBSD
  signals
- interrupted waits may not restart or return Linux-compatible errors

Required checks:

- test interrupted waits after ioctl paths exist
- verify timeout conversion and error returns
- audit `xe_pcode` mailbox waits and any early init polling loops that rely on
  timeout semantics

### 13. Firmware Loading And Paths

Linux expects firmware under Linux paths such as `/lib/firmware/xe/`.
FreeBSD has different firmware conventions.

Risk:

- firmware request APIs exist but path mapping may be wrong
- GuC/HuC/BMG firmware can fail for packaging reasons rather than driver bugs

Required checks:

- document exact FreeBSD firmware path policy
- capture Linux firmware filenames and versions from Rocky
- verify FreeBSD can load the same firmware payloads by the expected names

### 14. `sync_file` FD Lifecycle

`sync_file` wraps a `dma_fence` in a file-descriptor lifecycle.

Risk:

- fence lifetime, fd lifetime, and close/dup interactions can diverge from
  Linux
- the issue is subtle and may only appear once submission and syncobj export
  are alive

Required checks:

- audit `sync_file` reference handling in `drm-kmod`
- test close/dup/wait behavior once submission works

## Initial Risk Ranking

Highest operational risk after compile/bootstrap:

1. `devm_*` / `drmm_*` cleanup ordering on attach failure and unload
2. IRQ and threaded-IRQ semantics, especially fence signaling from interrupt
   context
3. workqueue flush/cancel and self-rescheduling behavior
4. `ww_mutex` / WITNESS / `drm_exec`
5. TTM placement, eviction, mmap fault, and memory-pressure behavior
6. busdma / IOMMU / cache-attribute correctness for CT, GGTT, and VRAM
   mappings
7. runtime PM stubs lying about device state
8. wait-event wake and restart semantics
9. firmware loading path and lifetime semantics
10. `drm_gpuvm` concurrent range tracking under VM_BIND
11. `sync_file` / fd lifetime
12. MSI/MSI-X vector setup and teardown

Still-critical first-runtime-proof dependencies:

1. TTM BO allocation and placement
2. GGTT pinning and aperture mapping
3. GuC CT buffer allocation and coherency
4. PCI BAR / MMIO / cache attributes

Additional missing-risk reminders:

- FLR/reset/reprobe on external A380 hardware
- render-node lifetime and devfs teardown on unload
- resize-BAR assumptions and WC/UC mismatches
- attach-failure memory leaks masked by managed cleanup

## Testing Implication

The first hardware milestone must not stop at "module loaded."
It must deliberately exercise:

- attach failure cleanup
- load/unload cycles
- firmware failure paths
- GuC CT buffer allocation, GGTT pinning, and H2G/G2H exchange
- BAR/VRAM probing
- DRM registration and render-node creation

Only after those are stable should BO/VM_BIND, submission, eviction, or
performance tests become primary signals.
