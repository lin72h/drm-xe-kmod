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

Recent OPUS/GLM source-role findings:

- [xe-recent-opus-glm-findings.md](xe-recent-opus-glm-findings.md)
- [opus-xe-port-red-team-review.md](opus-xe-port-red-team-review.md)

Recent compile-bootstrap findings:

- [xe-compile-bootstrap-findings.md](xe-compile-bootstrap-findings.md)

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

## Phase 0 Reality: Compile Bootstrap Comes First

The current planning state needs one correction:

> The first hard problem is making `xe.ko` compile and link.

There are already confirmed Phase-0 blockers in the local trees:

- `xe_hmm.c` is forced by `CONFIG_HMM_MIRROR`, but FreeBSD's dummy
  `linux/hmm.h` is zero bytes
- `drmm_mutex_init` and `__drmm_mutex_release` are missing from
  `drm-kmod-6.12`
- `devm_release_action` is missing from LinuxKPI devres
- `devm_ioremap_wc` is missing from LinuxKPI
- `linux/auxiliary_bus.h` does not exist
- `kconfig.mk` does not yet express the full Xe Kconfig surface

This is now tracked in:

- [xe-compile-bootstrap-findings.md](xe-compile-bootstrap-findings.md)

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

## First Runtime Gate: GuC CT

The earlier planning docs need one refinement:

- MMIO `GET_HWCONFIG` is the first firmware transport proof
- GuC CT is the first shared-memory runtime gate before submission

Smallest honest A380 GuC/CT path:

1. GuC ABI headers under `drivers/gpu/drm/xe/abi/guc_*_abi.h`
2. `xe_uc_fw.c`
3. `xe_uc.c` and `xe_guc.c`
4. `xe_guc_ads.c`
5. `xe_guc_ct.c`

`xe_guc_submit.c` is still direct Xe porting work, but it should follow CT
because CT first proves:

- firmware load and ABI compatibility
- system-memory BO allocation for CT buffers
- GGTT pinning and invalidation
- DMA/cache coherency for firmware-visible memory
- H2G/G2H message dispatch
- early IRQ/G2H behavior

Additional early-path facts now verified in Linux 6.12:

- `xe_sa` suballocation sits under ADS and CT allocation
- `xe_pcode_probe_early()` is in the early device path
- `xe_guc_pc` is part of the GuC/GT power-control path
- `xe_oa_init()` sits before `drm_dev_register()`, so OA must either succeed
  or be stubbed/staged cleanly for first registration
- `xe_guc_hwconfig_init()` performs the concrete early MMIO proof through
  `XE_GUC_ACTION_GET_HWCONFIG`
- `guc_ct_control_toggle()` is the MMIO bootstrap step that enables CT after
  the CT buffers are allocated and GGTT-pinned
- the first good blocking CT round-trip candidate is
  `pc_action_query_task_state()` in `xe_guc_pc.c`, not a made-up ping

The Phase-0/1/2 order is therefore:

1. compile and link
2. module load and MMIO `GET_HWCONFIG` proof
3. firmware validation
4. ADS and CT prerequisites
5. CT enable and one existing blocking CT request/reply
6. only then submission

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
- `drmm_mutex_init` and `__drmm_mutex_release` are missing
- `devm_release_action` is missing
- `devm_ioremap_wc` is missing

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

### 4. Phase 1.5 should move to a dual-Xe lab machine

After the first A380 milestone is stable, the roadmap should add a deliberate
Phase 1.5 hardware move:

- transition to a lab system that actually provides two Xe-managed devices
- keep the Arc A380 as the external secondary Xe device
- treat this as a topology expansion milestone, not as instant display
  enablement

For Phase 1.5, "stable" should mean at least:

- A380 probe, firmware load, CT, and `drm_dev_register()` work
- BO allocation, GGTT pinning, and basic VRAM access work
- at least one BO-backed VM_BIND succeeds
- at least one GuC submission completes and a fence signals
- load/unload does not leak or panic across 50 cycles

Hardware warning:

- a typical Alder Lake iGPU remains an `i915` device and does not count as
  dual-Xe
- if the planned "internal Arc/Xe" machine is really `i915 + Xe`, that is
  still Phase 1-style coexistence, not Phase 1.5 dual-Xe
- if no internal Xe-managed platform is available, use an alternative dual-Xe
  target such as `A380 + B580` or `A380 + A380`

The point of Phase 1.5 is to stop testing only the "old i915 host plus one Xe
card" shape and begin testing the actual multi-Xe environment we care about.

Phase 1.5 should prove at least:

- internal Xe and external A380 both enumerate and attach cleanly
- firmware, GuC, CT, GT, and IRQ setup remain independent per device
- render-node and DRM-minor creation stay stable with two Xe devices present
- detach, unload, reprobe, and error handling do not corrupt the other Xe
  device
- PCI topology, BAR sizing, and IOMMU behavior are still understood in the new
  host layout
- no cross-device BO sharing or PRIME/dma-buf experiments are required
- no cross-device VM sharing is attempted
- display remains disabled on both devices
- the Linux 6.12 semantic baseline does not change

Phase 1.5 should not silently become:

- first display-enablement
- first host-console takeover
- first userptr/HMM milestone
- first B580/Battlemage milestone

Name the likely dual-Xe failure modes explicitly:

- GuC ID namespace collision between devices
- second-device firmware-load or firmware-cache confusion
- GGTT address-space isolation failure
- concurrent firmware load for the same blob name races or leaks state
- DRM minor or render-node allocation breaks on the second device
- LinuxKPI global state assumes one GPU and races during dual probe
- IRQ registration or MSI/MSI-X setup collides between devices
- TTM device or memory-pool isolation failure
- memory-pressure behavior changes when both GPUs can allocate VRAM-backed BOs
- module unload or attach failure on one device frees shared state still used
  by the other

## First Honest Hardware Milestone

0. `xe.ko` compiles and links without unresolved symbols.
1. `xe` loads in the 6.12 lane.
2. The A380 probes and attaches while Alder Lake remains on `i915`.
3. MMIO BAR mapping succeeds.
4. VRAM probing reports a sane size and BAR aperture.
5. Firmware loading for the GuC and related uc path is real enough to
   diagnose failures.
6. Early GuC MMIO `GET_HWCONFIG` communication succeeds.
7. `xe_sa` suballocator setup succeeds.
8. ADS setup reaches a known ready/fail state.
9. CT buffer allocation and GGTT pinning work.
10. `guc_ct_control_toggle()` succeeds and CT reaches a known enabled or
    diagnosable failed state.
11. One existing blocking CT request/reply succeeds or fails at a clearly
    understood point.
12. `xe_pcode` mailbox path behaves correctly.
13. GT init and IRQ setup complete without panic, or fail with a useful trace.
14. IRQ dispatch is stable and unload/reload does not leave leaked
    IRQ/workqueue/render-node state.
15. `xe_oa_init()` is stubbed or succeeds so registration can proceed.
16. `drm_dev_register()` succeeds if initialization reaches that stage.
17. The render node appears only after the lower milestones are stable.

For each milestone, capture a paired Rocky Linux 10.x A/B log when practical.
The comparison should show whether FreeBSD fails before, at, or after the same
Linux Xe milestone on the same A380.

For Phase 1.5, repeat that discipline on the dual-Xe lab platform:

- capture Linux and FreeBSD logs for the internal Xe-managed device
- capture Linux and FreeBSD logs for the external A380
- record device enumeration order, render-node order, and firmware versions for
  both devices
- note whether failures are per-device or only appear when both Xe devices are
  present
- require evidence that both devices report correct BAR sizing, distinct render
  nodes, distinct VRAM sizes, and separate GuC/CT init sequences
- require evidence that both devices reach `drm_dev_register()` in one boot
- require evidence that BO allocation and VM_BIND work independently on each
  device
- require evidence that load/unload or attach-failure testing on one device
  leaves the other intact

Track runtime semantic risks separately:

- [xe-runtime-semantic-risks.md](xe-runtime-semantic-risks.md)

## Patch Split

### `freebsd-src-drm-6.12`

Put changes here when they are generic:

- real MMU interval notifier support
- real HMM or userptr-enabling VM support
- generic auxiliary-bus or `mei_aux` support
- `devm_release_action`
- `devm_ioremap_wc`
- stub `linux/auxiliary_bus.h` if it is introduced as generic LinuxKPI
- generic firmware, PCI, DMA, VM, or LinuxKPI fixes

### `drm-kmod-6.12`

Put changes here when they are Xe-specific:

- Xe-related `CONFIG_*` bootstrap in `kconfig.mk`
- initial `CONFIG_DRM_XE_DISPLAY=n` and `CONFIG_HMM_MIRROR` policy
- Xe subtree import
- `xe` Makefile and top-level kmod wiring
- `xe_drm.h` import
- `drmm_mutex_init` and `__drmm_mutex_release` in DRM managed-resource code
- DG2-first attach policy and force-probe behavior
- temporary Xe-local unsupported paths for userptr, HECI GSC, OA, or
  devcoredump staging

## Immediate Bring-Up Sequence

1. Add missing Xe-related `CONFIG_*` symbols to `drm-kmod-6.12/kconfig.mk`.
2. Set `CONFIG_DRM_XE_DISPLAY=n` for the initial import.
3. Resolve the `CONFIG_HMM_MIRROR` compile strategy.
4. Add `drmm_mutex_init` and `__drmm_mutex_release`.
5. Add `devm_release_action` and `devm_ioremap_wc`.
6. Add stub `linux/auxiliary_bus.h`.
7. Add `xe` build plumbing in `drm-kmod-6.12`.
8. Import `include/uapi/drm/xe_drm.h`.
9. Import the non-display Xe core with Linux 6.12 structure preserved.
10. Iterate until `xe.ko` links cleanly.
11. Only then start DG2/A380 probe, firmware, MMIO, ADS, and CT bring-up on
    real hardware.
12. Treat MMIO as the first firmware-transport proof and CT as the first
    shared-memory runtime gate before submission.

Detailed ladder:

- [xe-recent-opus-glm-findings.md](xe-recent-opus-glm-findings.md)
- [opus-xe-port-red-team-review.md](opus-xe-port-red-team-review.md)

## Final Stage-0 Summary

The 6.12 FreeBSD lane already has much of the generic DRM substrate Xe wants,
but the immediate blockers are compile/bootstrap issues before hardware bring-up.
After that, the first realistic failure mode is almost-right runtime behavior in
attach failure, unload, IRQ, workqueue, and cleanup ordering. The largest
semantic gap remains userptr and HMM.
That makes the corrected first strategy:

- make the import compile and link first
- prove MMIO `GET_HWCONFIG` before CT
- prove `guc_ct_control_toggle()` before the first true blocking CT exchange
- import Xe mostly intact
- defer userptr honestly
- stage HECI GSC and other secondary paths
- target DG2/A380 attach first
