# Opus Deep Review Prompt: FreeBSD Xe DRM 6.12 Port

Date: 2026-04-18

Copy/paste the section below into Opus.

---

You are Opus reviewing a long-term operating-system graphics-driver port.

I want a serious technical review, not encouragement.
Act as a skeptical senior FreeBSD DRM/LinuxKPI developer and Linux DRM reviewer.
Assume you understand:

- FreeBSD kernel development
- FreeBSD `drm-kmod`
- LinuxKPI
- Linux DRM driver imports
- Intel i915 and Xe
- AMDGPU as a FreeBSD DRM precedent
- TTM, dma-buf, dma-fence, dma-resv
- DRM GPUVM, drm_exec, sync_file
- firmware loading
- PCI, MSI/MSI-X, DMAR/IOMMU, busdma
- devfs, module load/unload, attach/detach
- real hardware bring-up on unstable kernel code

Please challenge the plan.
If something is wrong, overconfident, or missing, say so directly.

## Project Context

The goal is to port Intel's Linux Xe GPU driver to FreeBSD's current
`drm-kmod` / LinuxKPI model.

This is a 1-2 year effort, not a quick patch.
We are still in design/research.
Do not rush into code.

This project is:

- FreeBSD-first
- upstream-oriented
- LinuxKPI/drm-kmod-shaped
- pinned to Linux 6.12 LTS semantics
- stock FreeBSD first
- Phase 1.0 OS compatible only by staying FreeBSD-clean

This project is not:

- a DMO/DMI prototype
- a Mach-native GPU memory architecture
- a `memory_object` experiment
- a `mach_vm` GPU VM redesign
- a replacement for GEM, TTM, dma-fence, dma-resv, or DRM GPUVM

The long-term OS project may eventually explore DMO/DMI/Mach-native GPU ideas,
but those must not distort this Xe port.

## Why Linux 6.12 Is Pinned

Linux 6.12 is the semantic baseline.

This is not because Linux 6.12 supports every future target perfectly.
It is pinned because:

- Linux 6.12 is LTS.
- FreeBSD's 6.12 DRM lane is close to being the mergeable/upstream review
  target.
- A fixed baseline lets FreeBSD developers review the port.
- Chasing Linux master would make the project unreviewable.

For newer hardware gaps, including RDNA3+ and Intel Arc+/Battlemage:

- keep the base at 6.12
- identify post-6.12 fixes explicitly
- record the upstream commit/source
- justify the hardware need
- carry them as deliberate backports
- do not silently raise the semantic baseline

## Local Trees

The local source trees are:

- `../nx/linux-6.12`
  - authoritative Linux 6.12 reference
  - source-semantic baseline

- `../nx/freebsd-src-drm-6.12`
  - FreeBSD source / LinuxKPI 6.12 lane
  - home for generic LinuxKPI, PCI, DMA, VM, firmware, bus, and kernel API
    changes

- `../nx/drm-kmod-6.12`
  - FreeBSD DRM module 6.12 lane
  - home for Xe import, DRM-side module build plumbing, Xe UAPI, and Xe-local
    compatibility work

- `./`
  - planning/design repo
  - Git tracked
  - large logs, RPMs, traces, VM images, build trees, and crash dumps should
    stay outside this repo under `../wip-drm-xe-artifacts/`

## Current Planning Documents

The planning repo currently contains:

- `docs/xe-freebsd-stage0-inventory.md`
- `docs/xe-freebsd-port-design-principles.md`
- `docs/xe-freebsd-amdgpu-i915-precedent.md`
- `docs/xe-freebsd-linux-ab-testing.md`
- `docs/xe-freebsd-testing-policy.md`
- `docs/repo-hygiene.md`
- `docs/opus-glm-xe-port-review-prompt.md`
- `RL10-xe.md`

I want your review to inform the next documents:

- `docs/xe-linux-6.12-source-inventory.md`
- `docs/xe-freebsd-compat-gap-map.md`
- `docs/xe-first-patch-series-plan.md`
- `docs/xe-hardware-lab-runbook.md`

## Current Verified Findings

### Linux 6.12 Xe tree

Local findings from `../nx/linux-6.12`:

- `drivers/gpu/drm/xe/` exists.
- It is roughly 396 checked-in files.
- `drivers/gpu/drm/Makefile` has `obj-$(CONFIG_DRM_XE) += xe/`.
- `drivers/gpu/drm/xe/Kconfig` selects or depends on modern DRM/Linux pieces:
  `DRM_BUDDY`, `DRM_EXEC`, `DRM_GPUVM`, `DRM_TTM`, `DRM_SCHED`,
  `MMU_NOTIFIER`, `HMM_MIRROR`, `SYNC_FILE`, `AUXILIARY_BUS`, `RELAY`,
  `WANT_DEV_COREDUMP`, and several display helpers.
- `include/uapi/drm/xe_drm.h` exists in Linux 6.12.
- `include/drm/intel/xe_pciids.h` exists and contains DG2 and BMG IDs.
- `XE_BMG_IDS` includes `0xE20B`.
- `xe_pci.c` wires `XE_BMG_IDS` to a Battlemage descriptor.

Important Linux files to inspect:

- `drivers/gpu/drm/xe/Kconfig`
- `drivers/gpu/drm/xe/Makefile`
- `drivers/gpu/drm/xe/xe_pci.c`
- `drivers/gpu/drm/xe/xe_device.c`
- `drivers/gpu/drm/xe/xe_module.c`
- `drivers/gpu/drm/xe/xe_vm.c`
- `drivers/gpu/drm/xe/xe_hmm.c`
- `drivers/gpu/drm/xe/xe_pt.c`
- `drivers/gpu/drm/xe/xe_gt_pagefault.c`
- `drivers/gpu/drm/xe/xe_bo.c`
- `drivers/gpu/drm/xe/xe_ttm_vram_mgr.c`
- `drivers/gpu/drm/xe/xe_uc_fw.c`
- `drivers/gpu/drm/xe/xe_guc.c`
- `drivers/gpu/drm/xe/xe_guc_ct.c`
- `drivers/gpu/drm/xe/xe_guc_ads.c`
- `drivers/gpu/drm/xe/xe_heci_gsc.c`
- `drivers/gpu/drm/xe/display/xe_display.c`
- `include/uapi/drm/xe_drm.h`
- `include/drm/intel/xe_pciids.h`

### `drm-kmod-6.12`

Local findings:

- there is no imported `drivers/gpu/drm/xe/` subtree yet
- there is no `xe` kmod build plumbing yet
- `include/uapi/drm/xe_drm.h` is missing
- `include/drm/intel/xe_pciids.h` already exists
- `scripts/drmgeneratepatch` still excludes `include/uapi/drm/xe_drm.h`
- common DRM helpers already present include:
  `drm_exec`, `drm_gpuvm`, `drm_buddy`, TTM resource support, dma-resv,
  dma-fence, and sync_file

Important FreeBSD DRM files to inspect:

- `../nx/drm-kmod-6.12/kconfig.mk`
- `../nx/drm-kmod-6.12/scripts/drmgeneratepatch`
- `../nx/drm-kmod-6.12/include/drm/intel/xe_pciids.h`
- `../nx/drm-kmod-6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_freebsd.c`
- `../nx/drm-kmod-6.12/drivers/gpu/drm/ttm/ttm_module.c`
- `../nx/drm-kmod-6.12/drivers/dma-buf/dma-buf-kmod.c`
- `../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gem/i915_gem_userptr.c`
- `../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gem/i915_gem_object.h`
- `../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gt/uc/intel_huc.c`
- `../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gt/uc/intel_guc_log.c`

### `freebsd-src-drm-6.12` / LinuxKPI

Local findings:

- firmware loading support exists
- xarray exists
- ww_mutex exists
- workqueue support exists
- iosys-map exists
- PCI helpers exist
- `linux/mmu_notifier.h` is dummy/partial
- `linux/hmm.h` is dummy-only
- `linux/mei_aux.h` is dummy-only
- `linux/relay.h` is dummy-only
- `linux/devcoredump.h` is minimal/stub-like
- no mature real `linux/auxiliary_bus.h` implementation is obvious in the
  local 6.12 lane

This suggests the biggest semantic gap is not "can it compile?" but "which
runtime semantics are fake or absent?"

## FreeBSD Porting Precedent

### AMDGPU precedent

AMDGPU is the best precedent for shared memory-management and synchronization
architecture.

FreeBSD history shows:

- TTM was split into its own module.
- dma-buf was split into its own module.
- amdgpu sits on common DRM helpers, TTM, dma-buf, firmware, dma-fence,
  dma-resv, and LinuxKPI.
- common helpers are preferred over driver-local FreeBSD hacks.

The lesson:

> Xe should rely on shared DRM/TTM/dma-buf/fence infrastructure rather than
> inventing a FreeBSD-local GPU memory architecture.

### i915 precedent

i915 is the closest Intel predecessor.

FreeBSD history shows:

- large Intel imports can be staged
- unsupported Linux-only areas can be trimmed through build logic
- userptr is gated around MMU notifier support
- MEI and relay gaps are handled explicitly
- old i915 has accumulated local FreeBSD ifdefs that Xe should avoid where a
  shared helper can solve the problem

The lesson:

> Use i915 for Intel-specific staging and explicit unsupported paths, but do
> not copy its long-term ifdef accumulation.

## Current Theory Of The Port

My current working theory is:

1. Do not build a tiny hand-written toy Xe probe driver.
2. Preserve Linux 6.12 Xe layout as much as possible.
3. Import enough of the non-display Xe core to be honest.
4. Keep display out of the first serious bring-up if possible.
5. Make userptr/HMM unsupported first.
6. Stage HECI GSC / `mei_aux` after base device path if it can fail
   gracefully.
7. Use A380/DG2 as first hardware target.
8. Keep Alder Lake iGPU on i915 as stable host display.
9. Use Rocky Linux 10.1 as a Linux 6.12 operational A/B reference.
10. Use B580/Battlemage as a later or parallel Xe2 reference case.

## Patch Split Theory

Put in `freebsd-src-drm-6.12` when generic:

- real MMU interval notifier support
- real HMM support
- real auxiliary-bus support
- real `mei_aux` support
- generic firmware, PCI, DMA, VM, busdma, or LinuxKPI fixes
- locking/workqueue/device-model helpers useful to other DRM drivers

Put in `drm-kmod-6.12` when Xe-specific:

- Xe subtree import
- Xe kmod build plumbing
- `xe_drm.h` import
- Xe UAPI integration
- Xe-specific feature gates
- temporary Xe-local unsupported paths for MAP_USERPTR, HECI GSC, relay,
  devcoredump, OA, SR-IOV, or display staging

Decision rule:

- if a change would help a future non-Xe DRM port, it probably belongs in
  `freebsd-src-drm-6.12`
- if a change exists only because Xe needs it, it probably belongs in
  `drm-kmod-6.12`

## First Hardware Milestone Theory

The first milestone should not be performance.
It should not be "replace the system display stack."

Candidate first honest milestone:

1. `xe` builds as a FreeBSD kmod
2. module load works
3. A380 PCI match/probe occurs
4. attach starts without panic
5. firmware paths resolve
6. GuC/HuC firmware load gets far enough to diagnose failures
7. MMIO, GT, VRAM, and IRQ init complete or fail at a clearly understood point
8. `drm_dev_register()` succeeds if init reaches that stage
9. render node appears only after the above is stable

I need you to tell me if this milestone is too ambitious, too weak, or ordered
wrong.

## Specific Xe Dependency Concerns

### 1. Userptr / HMM in Linux 6.12

Linux 6.12 userptr appears to use:

- `DRM_XE_VM_BIND_OP_MAP_USERPTR`
- `xe_vm.c`
- `xe_hmm.c`
- `struct mmu_interval_notifier`
- `mmu_interval_notifier_insert`
- `hmm_range_fault`
- `hmm_pfn_to_page`
- sg table construction
- DMA mapping for system pages

`xe_hmm.o` is built behind `CONFIG_HMM_MIRROR`.
`DRM_XE` selects `HMM_MIRROR`.

FreeBSD's current 6.12 lane appears to have only dummy/partial HMM and
MMU-notifier headers.

My current plan:

- reject or disable `DRM_XE_VM_BIND_OP_MAP_USERPTR` initially
- return explicit unsupported errors
- keep the cut Xe-local first
- only add real HMM/MMU notifier support later as generic `freebsd-src` work

Concern:

- Is there any unavoidable path where Xe's base VM or page-table machinery
  depends on HMM/userptr even for BO-backed VM_BIND?

### 2. SVM / GPUSVM confusion

GLM feedback mentioned:

- `xe_svm.c`
- `xe_svm.h`
- `CONFIG_DRM_XE_GPUSVM`
- `drm_gpusvm`
- `drm_pagemap`
- `dev_pagemap`
- `MEMORY_DEVICE_PRIVATE`
- `drm_pagemap_migrate_to_devmem`
- `drm_pagemap_evict_to_ram`
- `xe_svm_copy`
- `xe_svm_handle_pagefault`

I checked the local `../nx/linux-6.12` tree and did not find `xe_svm.c`,
`xe_svm.h`, `CONFIG_DRM_XE_GPUSVM`, `drm_gpusvm`, or `drm_pagemap`.

Concern:

- Are these post-6.12 Xe developments?
- Are they in Rocky/RHEL downstream kernels?
- Are they 6.12.y LTS backports?
- Are they from a separate future branch?
- Should they be ignored for first port planning, or explicitly tracked as
  later backport candidates?

### 3. Explicit VM_BIND without userptr

My current assumption:

- explicit BO-to-GPU-VA VM_BIND should not require userptr/HMM
- it should rely on `drm_gpuvm`, `drm_exec`, TTM, `dma_resv`, `dma_fence`, Xe
  page-table updates, migration, and BO validation

Concern:

- Does Linux 6.12 Xe route BO-backed explicit VM_BIND through GPU page-fault or
  USM machinery in a way that makes userptr/HMM deferral unsafe?

### 4. GPU page faults and USM

Linux 6.12 has `xe_gt_pagefault.c`.

Local observations:

- page-fault handling is gated by `xe->info.has_usm`
- Xe2/Battlemage sets `has_usm = 1`
- `DRM_XE_VM_CREATE_FLAG_FAULT_MODE` requires `has_usm`
- page-fault handling can rebind VMAs
- if VMA is userptr, the fault path can repin userptr pages

Concern:

- Should FreeBSD initially disable fault-mode VM creation?
- Can fault-mode be allowed only for BO-backed VMAs?
- If GPU page faults arrive before the path is ready, what is the honest
  failure behavior?

### 5. HECI GSC / MEI auxiliary device

Linux 6.12 `xe_heci_gsc.c`:

- includes `linux/mei_aux.h`
- creates an `auxiliary_device`
- creates a `mei_aux_device`
- allocates an IRQ descriptor
- `xe_heci_gsc_init` returns `void`
- on failure it calls `xe_heci_gsc_fini`

FreeBSD local state:

- `linux/mei_aux.h` appears dummy-only
- real auxiliary bus support is not obvious

Concern:

- Can HECI GSC be staged without blocking base Xe attach?
- Is it enough that `xe_heci_gsc_init` returns `void`, or do later paths
  assume the aux device exists?
- Does Battlemage/B580 make GSC more mandatory than DG2/A380?

### 6. Display entanglement

Linux 6.12 has:

- `CONFIG_DRM_XE_DISPLAY`
- `xe.probe_display`
- many display paths that no-op when `xe->info.probe_display` is false

Current plan:

- keep Alder Lake iGPU on i915
- bring up A380 as secondary GPU
- compile out or disable Xe display first if possible

Concern:

- Can non-display Xe still reach `drm_dev_register()` cleanly?
- Is display code sufficiently gated, or will display headers and compat i915
  headers drag in too much?
- For A380/B580, is display required for anything before render-node bring-up?

### 7. Runtime semantic bugs

I am more worried about compile-clean runtime-wrong behavior than missing
headers.

Risk areas:

- `dma_resv` locking semantics
- `ww_mutex` deadlock detection
- `drm_exec` retry behavior under FreeBSD locking and witness
- TTM placement under memory pressure
- TTM eviction and shrinker behavior
- busdma and DMA address constraints
- VRAM BAR and resize BAR behavior
- PCI resource mapping and cache attributes
- dma-fence timeline signaling
- sync_file fd lifecycle
- drm_gpuvm range tracking under concurrent VM_BIND
- module unload while fences/workqueues/timers are active
- devm/drmm cleanup ordering under attach failure
- firmware request lifetime and error paths
- workqueue/taskqueue semantic mismatch
- interrupt masking/unmasking and MSI/MSI-X behavior
- runtime PM and power-management assumptions
- reset/recovery paths

Please expand or reorder this risk list.

## Linux A/B Strategy

Rocky Linux 10.x / RHEL 10.x are useful because they are 6.12-series
enterprise kernels.

Native A/B:

- boot same machine into FreeBSD with developing Xe port
- boot same machine into Rocky Linux 10.1
- compare same GPU behavior without hypervisor layer

bhyve A/B:

- FreeBSD host keeps Alder Lake iGPU as display
- pass A380 or B580 to Rocky Linux 10.1 guest
- useful for faster Linux reference testing
- not identical to native due to IOMMU, reset, BAR placement, MSI/MSI-X, and
  hypervisor effects

Rocky/B580 verification:

- Rocky Linux 10.1 ships kernel `6.12.0-124.8.1`
- Rocky kernel package contains `drivers/gpu/drm/xe/xe.ko.xz`
- extracted `xe.ko` contains alias for `8086:E20B`
- Rocky `linux-firmware` contains `xe/bmg_guc_70.bin.xz` and
  `xe/bmg_huc.bin.xz`
- Intel table maps B580 to PCI ID `E20B`, Xe2, Battlemage

Concern:

- What exact Linux logs and sysfs/debugfs data should be captured before
  FreeBSD debugging?
- What Rocky/RHEL downstream behavior might mislead us compared with upstream
  Linux 6.12?
- Should B580 be a first-class bring-up target now, or only after A380/DG2 is
  stable?

## Testing Policy

Existing tests:

- keep in their native framework
- make them pass
- do not rewrite existing FreeBSD/Linux tests into a new language

New project-owned tests:

- Elixir for performance-insensitive orchestration, A/B comparison, log
  classification, VM control, report generation, and long-running hardware
  workflows
- Zig for performance-sensitive, low-level, ioctl-facing, binary-parsing,
  timing-sensitive, stress, mmap, BO, VM_BIND, fence, sync-object, and minimal
  DRM userspace probes
- when mixed, Elixir orchestrates and calls Zig helper binaries

Concern:

- Is this sensible, or will introducing Elixir/Zig create friction for FreeBSD
  collaboration?
- How should upstream-facing tests be separated from project-owned lab tests?

## My Current Plan Before Writing Port Code

I think the next work should be documentation and source inventory, not code.

Planned documents:

1. `docs/xe-linux-6.12-source-inventory.md`
   - list every Linux 6.12 Xe file
   - classify as import-now, defer, stub, generated, display-only, test-only,
     SR-IOV, OA, GSC, HMM/userptr, core required
   - identify dependencies per file group

2. `docs/xe-freebsd-compat-gap-map.md`
   - map Xe dependencies to FreeBSD 6.12 support
   - classify as present, partial, dummy-only, missing generic LinuxKPI,
     missing DRM helper, or Xe-local workaround
   - decide `freebsd-src` vs `drm-kmod` ownership

3. `docs/xe-first-patch-series-plan.md`
   - propose first 10-20 patches
   - keep generic LinuxKPI changes separate from Xe import
   - define reviewable patch boundaries

4. `docs/xe-hardware-lab-runbook.md`
   - A380/DG2 first
   - Alder Lake iGPU stays on i915
   - Rocky Linux 10.1 A/B reference
   - B580/Battlemage reference
   - log capture policy
   - artifact storage outside repo

Only after that:

- start a code branch in `drm-kmod-6.12`
- import UAPI and build plumbing
- attempt a non-display core build
- make unsupported features explicit
- then test real hardware

## What I Need From You

Please give a red-team review.

Do not just answer "yes, good plan."
Assume there are traps.
Find them.

Answer these questions:

1. What is the biggest architectural mistake in this plan, if any?
2. Is Linux 6.12 the right semantic baseline for FreeBSD upstream work?
3. Is the split between `freebsd-src-drm-6.12` and `drm-kmod-6.12` correct?
4. What dependencies am I underestimating?
5. What missing LinuxKPI semantics will hurt most at runtime?
6. Is userptr/HMM deferral viable for the first milestone?
7. Is explicit BO-backed VM_BIND viable without userptr/HMM?
8. How should GPU fault-mode / USM be handled initially?
9. Can HECI GSC / `mei_aux` be staged, or is it an early blocker?
10. Can display be compiled out or disabled for first bring-up?
11. What is the smallest honest Xe import subset?
12. What should the first hardware milestone be?
13. What should the first 10-20 patches look like?
14. What FreeBSD developer objections should I expect?
15. What data should Rocky Linux 10.1 A/B testing capture?
16. Is B580 useful now, or should A380 remain the only initial target?
17. Is the Elixir/Zig testing policy wise or distracting?
18. What should be documented before any code is written?
19. What assumptions above are weak or likely wrong?
20. What should I do next?

## Desired Output Format

Please structure your answer as:

1. Executive judgment
2. Major flaws or risks
3. Missing dependencies
4. Corrected staging plan
5. Recommended first patch series
6. First hardware milestone
7. Userptr/HMM/SVM/fault-mode recommendation
8. HECI GSC/display recommendation
9. A/B testing improvements
10. FreeBSD upstreaming concerns
11. Concrete next actions before coding

Be direct.
If a part of the plan is wrong, say exactly why.
If a part is sound, say what evidence supports it.
