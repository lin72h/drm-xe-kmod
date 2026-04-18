# Claude Feedback Integration

Date: 2026-04-19

## Purpose

This memo absorbs the two external Claude review files:

- `../wip-claude/xe-port-review-feedback.md`
- `../wip-claude/phase2-architecture-review-v4.md`

The first file directly reviews the Xe FreeBSD port plan.
The second is a Phase 2 DMO/DMI architecture review and is relevant only for
maintaining the boundary between this FreeBSD Xe port and later native GPU
architecture work.

## Direct Xe Port Decisions

### DG2/A380 also has USM

The plan previously treated fault-mode / USM risk as mostly a Battlemage/B580
concern.
That is too narrow.

Local Linux 6.12 confirms DG2 also has `has_usm = 1` through
`graphics_xehpg`.
Therefore the A380 can expose fault-mode VM behavior too.

Decision:

- reject `DRM_XE_VM_CREATE_FLAG_FAULT_MODE` on FreeBSD at first
- do this for DG2/A380 and BMG/B580
- keep hardware capability detection intact
- add a FreeBSD support gate rather than pretending the hardware lacks USM
- return an explicit unsupported error, preferably `-EOPNOTSUPP`

Preferred shape:

```c
if (args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE &&
    !xe_fault_mode_supported(xe))
	return -EOPNOTSUPP;
```

`xe_fault_mode_supported()` should return false on FreeBSD until the required
MMU notifier / HMM / fault-mode semantics are real.

### HMM is userptr-only for the first-stage path

Local Linux 6.12 confirms `xe_hmm_userptr_populate_range()` is reached through
the userptr path, not ordinary BO-backed VM_BIND.

Decision:

- BO-backed explicit VM_BIND remains a valid early target
- `DRM_XE_VM_BIND_OP_MAP_USERPTR` should be explicitly unsupported first
- HMM/MMU-notifier work can be deferred without blocking basic BO-backed VM_BIND

This is now a stronger conclusion than the earlier "likely" statement.

### SVM / GPUSVM is not a Linux 6.12 baseline dependency

Local Linux 6.12 does not contain:

- `xe_svm.c`
- `xe_svm.h`
- `CONFIG_DRM_XE_GPUSVM`
- `drm_gpusvm`
- `drm_pagemap`

Claude's review identifies this as post-6.12 work, likely from newer Linux
mainline rather than the 6.12 baseline.

Decision:

- ignore SVM/GPUSVM for the first FreeBSD Xe port
- do not let it influence Phase 1 patch structure
- track it only as a possible future explicit backport topic

### HECI GSC can be staged

Linux 6.12 calls `xe_heci_gsc_init(xe)` after GT init.
It returns `void` and cleans up internally on failure.

Decision:

- HECI GSC is not a first attach blocker
- for A380/DG2, lack of GSC mainly means HuC authentication and some media
  features may not work
- that is acceptable for the first milestone
- B580/Battlemage may make GSC/CSCFI more important, so B580 should remain
  second target after A380

### Display can probably be disabled early

Linux 6.12 has both `CONFIG_DRM_XE_DISPLAY` and `xe.probe_display`.
Many display paths no-op when `xe->info.probe_display` is false.

Decision:

- keep A380 as a secondary non-display Xe bring-up target
- keep Alder Lake iGPU on i915
- disable/probe-gate Xe display in the first bring-up if build plumbing allows
- verify that `drm_dev_register()` and render node creation still work without
  display

The main risk is display header pollution, especially i915 display compatibility
headers.

## Added Dependency Watch List

Claude's review adds several dependencies that must be treated as first-class
runtime risks, not footnotes:

- `drm_sched` API and runtime behavior
- `dma_fence_chain`
- `dma_fence_array`
- `iosys-map` correctness for VRAM BAR access and WC/UC mappings
- PCI resize BAR behavior
- `devm_*` and `drmm_*` cleanup ordering
- workqueue flush/cancel semantics during unload and error paths
- GuC CT buffer allocation, alignment, DMA accessibility, and coherency
- `wait_event_interruptible` and signal/restart behavior
- runtime PM, which should be treated as always-on initially

These are now tracked in:

- [xe-runtime-semantic-risks.md](xe-runtime-semantic-risks.md)

## Refined First Hardware Milestone

The first milestone should now be recorded as:

1. `xe.ko` builds
2. module loads without panic
3. A380 PCI match succeeds
4. `xe_pci_probe()` starts and completes expected early phases
5. MMIO BAR is mapped
6. VRAM is probed and size is reported
7. GuC firmware is found and loaded far enough to diagnose failures
8. GuC CT initializes or fails at a clear point
9. GT init completes or fails at a clear point
10. `drm_dev_register()` succeeds if initialization reaches that stage
11. render node appears only after the lower milestones are stable

Performance, display takeover, media, HuC authentication, userptr, fault mode,
SVM, OA, SR-IOV, and full devcoredump are not first-milestone goals.

## Phase 2 Boundary From Architecture Review

The Phase 2 review reinforces the boundary, not a change in this port's design.

For this Xe port:

- do not design around DMO/DMI
- do not introduce Mach vocabulary into Xe driver code
- do not avoid GEM, TTM, dma-buf, dma-fence, dma-resv, or DRM GPUVM
- do not treat Linux DRM as temporary inside this project

What this port can do that helps Phase 2 without contaminating itself:

- prove Xe works on FreeBSD at all
- document the exact Linux abstractions Xe depends on
- characterize FreeBSD VM, busdma, PCI, IOMMU, BAR, and pager behavior under
  real GPU load
- record limitations in a neutral gap map

The right relationship is:

> The Xe port builds the Linux-shaped FreeBSD floor.
> Phase 2 later builds a Mach-shaped ceiling.
> They may share hardware observations, but not design vocabulary.

## Added Next Document

Claude recommended one additional document before code:

- `docs/xe-runtime-semantic-risks.md`

That document is now part of the required pre-code planning set, along with:

- `docs/xe-linux-6.12-source-inventory.md`
- `docs/xe-freebsd-compat-gap-map.md`
- `docs/xe-first-patch-series-plan.md`
- `docs/xe-hardware-lab-runbook.md`

## Immediate Next Actions

1. Capture Rocky Linux 10.1 A380 baseline before writing code.
2. Produce the Linux 6.12 Xe source inventory.
3. Produce the FreeBSD compatibility gap map.
4. Maintain the runtime semantic risk list as a first-class planning artifact.
5. Only then write the first patch-series plan.
