# Intel Xe on FreeBSD 6.12: Stage 0 Inventory and Gap Memo

Date: 2026-04-18

## Scope

This memo records the current local truth for the Xe-on-FreeBSD effort.

Target assumptions:

- Linux 6.12 LTS is the semantic baseline
- stock FreeBSD is the primary target
- the Phase 1.0 OS is a secondary compatibility target
- this is not a DMO, DMI, or Mach-native GPU redesign effort

## Why Pin DRM 6.12

The project should pin the DRM baseline to Linux 6.12 for integration reasons,
not because Linux 6.12 is assumed to contain final support for every GPU this
larger effort may eventually care about.

The practical reasons are:

- Linux 6.12 is an LTS line, so fixes will keep flowing against a stable base.
- FreeBSD's 6.12 DRM lane is already the near-term upstream integration target.
- aligning with that lane gives FreeBSD developers a reviewable base instead
  of asking them to chase a moving Linux `master` snapshot.

For newer RDNA3+ or Intel Arc+ hardware gaps, the rule should be:

- keep the base pinned to 6.12
- identify required post-6.12 fixes explicitly
- carry them as documented, targeted backports
- do not silently raise the whole semantic baseline

## Linux 6.12 A/B Reference

The 6.12 pin also creates a practical Linux-side oracle.
Rocky Linux 10.x and RHEL 10.x ship 6.12-series enterprise kernels, so Rocky
Linux 10.x can be used side-by-side with FreeBSD to compare expected Xe
behavior on the same A380 hardware.

Recommended stance:

- use native Rocky Linux 10.x boot as the cleanest hardware A/B comparison
- use Rocky Linux 10.x under bhyve with A380 passthrough as a fast secondary
  reference when the FreeBSD host can reserve the A380 for `ppt`
- keep `../nx/linux-6.12` as the source-semantic reference
- treat Rocky/RHEL-specific kernel behavior as diagnostic evidence, not as an
  automatic source baseline change

Detailed strategy:

- [xe-freebsd-linux-ab-testing.md](xe-freebsd-linux-ab-testing.md)

## Local Trees

Primary reference trees:

- `../nx/linux-6.12`
- `../nx/freebsd-src-drm-6.12`
- `../nx/drm-kmod-6.12`

This repo is the local design workspace for the effort.

## Tree Truth

### Linux 6.12 reference

- `../nx/linux-6.12/drivers/gpu/drm/xe/` exists and is complete
- the local Xe subtree is substantial, roughly 396 checked-in files
- `drivers/gpu/drm/Makefile` wires Xe in with `obj-$(CONFIG_DRM_XE) += xe/`
- `drivers/gpu/drm/xe/Kconfig` expects modern DRM helpers and Linux MM
  semantics including `DRM_BUDDY`, `DRM_EXEC`, `DRM_GPUVM`, `DRM_TTM`,
  `DRM_SCHED`, `MMU_NOTIFIER`, `HMM_MIRROR`, and `SYNC_FILE`

### `drm-kmod-6.12`

- there is no imported `drivers/gpu/drm/xe/` subtree yet
- there is no `xe` kmod in the top-level `Makefile`
- `include/uapi/drm/xe_drm.h` is missing
- `include/drm/intel/xe_pciids.h` already exists
- `scripts/drmgeneratepatch` still excludes `include/uapi/drm/xe_drm.h`

### `freebsd-src-drm-6.12` and shared 6.12 support already present

The 6.12 lane already has the main generic DRM building blocks Xe expects:

- `drm_exec`
- `drm_gpuvm`
- `drm_buddy`
- `drm_sched`
- TTM resource support including `ttm_resource`
- `dma-resv`
- `dma-fence`
- `dma_fence_chain`
- `dma_fence_array`
- `sync_file`

LinuxKPI already provides several useful pieces:

- firmware loading support
- `xarray`
- `ww_mutex`
- `workqueue`
- `iosys-map`
- PCI support and related helpers

## Honest Minimum Import Shape

The right unit of import is not a tiny custom probe subset.
It is the Linux 6.12 non-display Xe core with the original structure largely
preserved.

Recommended first import stance:

- preserve Linux 6.12 file layout where possible
- import the non-display core first
- leave display support out of the first serious bring-up series

Likely first-round deferrals:

- `display/*`
- `i915-display/*`
- `i915-soc/*`
- tests
- hwmon
- SR-IOV
- OA and observation if they block early progress

Likely first-round staged or stubbed areas:

- HECI GSC auxiliary-device integration
- full devcoredump parity

## Gap Classification

### Already present and usable

- `drm_exec`
- `drm_gpuvm`
- `drm_buddy`
- TTM resource management
- `dma-resv` and fence infrastructure
- `sync_file`
- firmware request and release path
- general LinuxKPI concurrency and container helpers

### Present but only partial for Xe semantics

- `linux/mmu_notifier.h` exists, but is only a dummy structure today
- `linux/devcoredump.h` exists, but is only minimally stubbed
- runtime PM support is mostly an always-on stub today
- `iosys-map` exists, but VRAM BAR WC/UC correctness still needs validation
- workqueue cancellation and flush behavior exists, but must be validated under
  Xe load/unload and self-rescheduling work patterns

### Missing or dummy for Xe

- no Xe driver subtree in `drm-kmod-6.12`
- no `xe` module build plumbing
- no imported `include/uapi/drm/xe_drm.h`
- `linux/hmm.h` is dummy-only
- `linux/mei_aux.h` is dummy-only
- `linux/relay.h` is dummy-only
- there is no evident real `linux/auxiliary_bus.h` implementation in the
  local 6.12 lane

## Major Porting Conclusions

### 1. Userptr is not ready

Linux Xe 6.12 expects:

- real MMU interval notifier behavior
- real HMM behavior
- `mmu_interval_notifier_*()` semantics
- `hmm_range_fault()` support

The local FreeBSD 6.12 lane does not provide that today.

The external review confirms that, in Linux 6.12, `xe_hmm.c` is used through
the userptr path and BO-backed explicit VM_BIND does not route through HMM.
That makes userptr/HMM deferral viable for the first milestone.

Short-term honest answer:

- make Xe `MAP_USERPTR` unsupported on FreeBSD first
- keep that cut local to Xe in `drm-kmod-6.12`
- return a clear unsupported error for userptr-related UAPI
- reject `DRM_XE_VM_CREATE_FLAG_FAULT_MODE` at first, including on DG2/A380,
  because DG2 also advertises `has_usm = 1`

Longer-term generic answer:

- add real MMU interval notifier and HMM support in
  `freebsd-src-drm-6.12`

### 2. HECI GSC is not the first blocker

`xe_heci_gsc_init(xe)` is not the first hard probe gate and cleans up on
failure. That makes it a good staged follow-up instead of a day-one
requirement.

Practical implication:

- early Xe bring-up can focus on attach, firmware, MMIO, GT, IRQ, and DRM
  registration first
- HECI GSC can be staged after the base device path is alive
- on A380, missing GSC mainly affects HuC authentication and media paths
- on B580, GSC/CSCFI may be more important, so B580 should follow A380 rather
  than lead the FreeBSD bring-up

### 3. DG2/A380 is the right first hardware target

The cleanest first hardware milestone is:

- DG2 discrete attach for the Arc A380
- Alder Lake iGPU stays on `i915`
- no first-series requirement to replace the host display stack

That keeps early review and debugging focused on Xe itself rather than driver
ownership conflicts on the host display GPU.

## First Honest Hardware Milestone

1. `xe` builds and loads in the 6.12 lane.
2. The A380 probes and attaches while Alder Lake remains on `i915`.
3. MMIO BAR mapping succeeds.
4. VRAM probing reports a sane size and BAR aperture.
5. Firmware loading for the GuC and related uc path is real enough to
   diagnose failures.
6. GuC CT initializes or fails at a clearly understood point.
7. GT init and IRQ setup complete without panic, or fail with a useful trace.
8. `drm_dev_register()` succeeds if initialization reaches that stage.
9. The render node appears only after the lower milestones are stable.

For each milestone, capture a paired Rocky Linux 10.x A/B log when practical.
The comparison should show whether FreeBSD fails before, at, or after the same
Linux Xe milestone on the same A380.

Track runtime semantic risks separately:

- [xe-runtime-semantic-risks.md](xe-runtime-semantic-risks.md)

## Patch Split

### `freebsd-src-drm-6.12`

Put changes here when they are generic:

- real MMU interval notifier support
- real HMM or userptr-enabling VM support
- generic auxiliary-bus or `mei_aux` support
- generic firmware, PCI, DMA, VM, or LinuxKPI fixes

### `drm-kmod-6.12`

Put changes here when they are Xe-specific:

- Xe subtree import
- `xe` Makefile and top-level kmod wiring
- `xe_drm.h` import
- DG2-first attach policy and force-probe behavior
- temporary Xe-local unsupported paths for userptr, HECI GSC, OA, or
  devcoredump staging

## Immediate Bring-Up Sequence

1. Add `xe` build plumbing in `drm-kmod-6.12`.
2. Import `include/uapi/drm/xe_drm.h`.
3. Import the non-display Xe core with Linux 6.12 structure preserved.
4. Add only the minimum FreeBSD-local cut-downs needed to compile honestly.
5. Start DG2/A380 probe and firmware bring-up on real hardware.

## Final Stage-0 Summary

The 6.12 FreeBSD lane already has much of the generic DRM substrate Xe wants.
The largest real missing semantic area is userptr and HMM.
That makes the clean first strategy:

- import Xe mostly intact
- defer userptr honestly
- stage HECI GSC and other secondary paths
- target DG2/A380 attach first
