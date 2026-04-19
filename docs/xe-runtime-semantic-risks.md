# Intel Xe on FreeBSD 6.12: Runtime Semantic Risks

Date: 2026-04-19

## Purpose

This memo tracks the runtime semantic risks for the Xe-on-FreeBSD port.

These are not simple "missing header" problems.
They are cases where code can compile and even load, but behave differently
from Linux under real GPU load.

## Highest Risk Areas

### 1. `ww_mutex`, WITNESS, and `drm_exec`

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

### 2. Workqueue Flush And Cancel Semantics

Xe uses Linux workqueues for rebind, page-fault handling, GuC work, GT work,
cleanup, and teardown paths.

Risk:

- `flush_workqueue`, `cancel_work_sync`, delayed work cancellation, and work
  items that reschedule themselves may behave differently on FreeBSD taskqueues.
- Attach failure and module unload may race with live work items.

Required checks:

- test repeated load/unload cycles early
- test attach failure cleanup paths
- verify all workqueues are flushed before memory teardown

### 3. `dma_fence` Signaling Rules

Linux has strict expectations around fence signaling context, lock ownership,
sleepability, and callback execution.

Risk:

- FreeBSD may not enforce the same constraints.
- Incorrect signaling context can deadlock under scheduler, VM_BIND, or reset
  load.
- `dma_fence_chain` and `dma_fence_array` may exist but still diverge subtly.

Required checks:

- verify `dma_fence_chain` and `dma_fence_array` against Linux 6.12 behavior
- stress syncobj timelines and VM_BIND fences
- verify callbacks do not sleep in forbidden contexts

### 4. `drm_sched`

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

### 5. TTM Placement, Eviction, And Memory Pressure

Xe depends on TTM for BO placement, validation, eviction, and VRAM/system
memory movement.

Risk:

- placement may work in normal cases but fail under memory pressure
- eviction may interact badly with FreeBSD VM pressure
- shrinker-like behavior may not match Linux

Required checks:

- test BO allocation under memory pressure
- test VRAM oversubscription after basic bring-up
- keep first milestone below eviction/stress complexity

### 6. PCI BAR, Resize BAR, And VRAM Mapping

Xe VRAM probing depends on correct PCI BAR mapping and may benefit from
resizable BAR support.

Risk:

- A380 may expose only a small BAR window.
- resize-BAR support may differ from Linux.
- cache attributes for BAR mappings may be wrong.

Required checks:

- capture Linux and FreeBSD BAR layout for A380
- verify FreeBSD `pci_rebar_*` behavior where available
- compare VRAM size and BAR aperture with Rocky Linux 10.1

### 7. `iosys-map` And VRAM BAR Access

Xe and common DRM helpers use `iosys-map` to abstract system memory versus I/O
memory access.

Risk:

- `iosys_map_memcpy_to`, read/write helpers, and `memcpy_toio` paths may not
  preserve the needed WC/UC semantics for VRAM BAR access.
- Small register-like writes may work while bulk VRAM copies fail or corrupt.

Required checks:

- verify `iosys-map` helpers against Linux 6.12 behavior
- test BAR-backed BO vmap/vunmap paths
- test simple VRAM write/readback before deeper execution tests

### 8. GuC CT Buffer Allocation And Coherency

GuC command transport uses driver-allocated buffers visible to firmware.
It is the first runtime gate after PCI/MMIO/firmware because it exercises the
Xe BO path, system-memory placement, GGTT pinning, firmware-visible memory,
and H2G/G2H dispatch before submission.

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

### 9. `devm_*` And `drmm_*` Cleanup Ordering

Xe relies on Linux managed-resource cleanup for attach failure and detach.

Risk:

- cleanup order must be LIFO and match Linux's assumptions
- wrong order can cause use-after-free during attach failure or module unload

Required checks:

- audit FreeBSD LinuxKPI `devm_*` action ordering
- audit DRM `drmm_*` action ordering
- force early attach failures and verify cleanup is stable

### 10. Runtime PM

FreeBSD's LinuxKPI runtime PM support is mostly stub-like.

Risk:

- Linux Xe assumes `pm_runtime_get` / `put` state transitions.
- FreeBSD should not pretend dGPU runtime suspend works initially.

Required stance:

- treat the GPU as always-on at first
- document runtime PM as unsupported
- avoid adding suspend/resume promises to the first milestone

### 11. Wait Queues And Signal Handling

Xe uses Linux wait primitives for user-visible waits.

Risk:

- `wait_event_interruptible` and timeout behavior may differ under FreeBSD
  signals
- interrupted waits may not restart or return Linux-compatible errors

Required checks:

- test interrupted waits after ioctl paths exist
- verify timeout conversion and error returns

### 12. Firmware Loading And Paths

Linux expects firmware under Linux paths such as `/lib/firmware/xe/`.
FreeBSD has different firmware conventions.

Risk:

- firmware request APIs exist but path mapping may be wrong
- GuC/HuC/BMG firmware can fail for packaging reasons rather than driver bugs

Required checks:

- document exact FreeBSD firmware path policy
- capture Linux firmware filenames and versions from Rocky
- verify FreeBSD can load the same firmware payloads by the expected names

## Initial Risk Ranking

Highest priority before hardware attach:

1. `ww_mutex` / WITNESS / `drm_exec`
2. workqueue flush/cancel and unload behavior
3. `dma_fence`, `dma_fence_chain`, and `dma_fence_array`
4. `drm_sched`
5. GuC CT buffer allocation, GGTT pinning, and coherency
6. PCI BAR / resize BAR / VRAM mapping
7. `devm_*` / `drmm_*` cleanup ordering
8. `iosys-map` BAR access correctness
9. runtime PM stubbing
10. wait queues and signal behavior

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
