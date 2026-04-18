# Opus/GLM Review Prompt: FreeBSD Xe DRM 6.12 Port

Date: 2026-04-18

Copy/paste the prompt below into Opus, GLM, or another strong reasoning model.

---

You are reviewing a long-term FreeBSD graphics-driver porting plan.

Act as a skeptical senior FreeBSD DRM/LinuxKPI developer with experience
reviewing `drm-kmod`, Linux DRM imports, LinuxKPI compatibility work, TTM,
dma-buf, dma-fence, dma-resv, GPU VM, firmware loading, PCI/DMAR/IOMMU, and
real hardware bring-up.

Do not give generic encouragement.
Critique the plan for upstreamability, missing dependencies, bad patch shape,
wrong assumptions, and risky staging.

## Project Goal

Port the Linux Intel Xe GPU driver to FreeBSD's current `drm-kmod` /
LinuxKPI model.

The semantic baseline is Linux 6.12 LTS.
The target is stock FreeBSD first.
The Phase 1.0 OS is only a secondary compatibility target because it remains
FreeBSD-based at this stage.

This is not a DMO/DMI project.
This is not a Mach-native GPU architecture effort.
Do not redesign GPU memory management around `memory_object`, `mach_vm`, or
future native Mach concepts.

The goal is an upstreamable FreeBSD-style Xe port that fits the existing
Linux-derived DRM stack.

## Local Trees

Primary trees:

- `../nx/linux-6.12`
  - Linux 6.12 LTS reference
  - authoritative source-semantic baseline

- `../nx/freebsd-src-drm-6.12`
  - FreeBSD source tree for the 6.12 DRM/LinuxKPI lane
  - intended home for generic LinuxKPI, PCI, DMA, VM, firmware, and kernel
    compatibility changes

- `../nx/drm-kmod-6.12`
  - FreeBSD DRM module tree for the 6.12 lane
  - intended home for Xe import, module build plumbing, Xe UAPI import, and
    Xe-specific compatibility work

Planning repo:

- `./`
  - Git-tracked planning/design repo
  - large logs, build products, RPMs, traces, VM images, and crash dumps must
    live outside it, preferably under `../wip-drm-xe-artifacts/`

## Important 6.12 Strategy

The DRM baseline is pinned to Linux 6.12 not because 6.12 supports every
future target GPU perfectly.

The reasons are:

- Linux 6.12 is an LTS line.
- FreeBSD's DRM 6.12 lane is the near-term integration target.
- A fixed baseline gives FreeBSD developers a reviewable target.
- Newer hardware support or fixes should be explicit, documented backports,
  not a silent drift to Linux master.

For RDNA3+, Intel Arc+, B580, or other newer hardware gaps:

- keep the base pinned to Linux 6.12
- identify post-6.12 fixes explicitly
- document the upstream commit/source
- justify the hardware need
- carry them as targeted backports

## Current Findings

Linux 6.12:

- `drivers/gpu/drm/xe/` exists and is substantial.
- `include/drm/intel/xe_pciids.h` includes DG2 and BMG/Battlemage IDs.
- `drivers/gpu/drm/xe/xe_pci.c` wires `XE_BMG_IDS` to a Battlemage descriptor.
- B580 PCI ID `0xE20B` is in `XE_BMG_IDS`.
- Xe expects modern DRM helpers and Linux MM semantics, including:
  `DRM_BUDDY`, `DRM_EXEC`, `DRM_GPUVM`, `DRM_TTM`, `DRM_SCHED`,
  `MMU_NOTIFIER`, `HMM_MIRROR`, and `SYNC_FILE`.

`drm-kmod-6.12`:

- no full `drivers/gpu/drm/xe/` subtree exists yet
- no `xe` kmod build plumbing exists yet
- `include/uapi/drm/xe_drm.h` is missing
- `include/drm/intel/xe_pciids.h` already exists
- `scripts/drmgeneratepatch` currently excludes `include/uapi/drm/xe_drm.h`
- generic DRM support already present includes:
  `drm_exec`, `drm_gpuvm`, `drm_buddy`, TTM resource management, dma-resv,
  dma-fence, sync_file

`freebsd-src-drm-6.12` / LinuxKPI:

- firmware loading exists
- xarray exists
- ww_mutex exists
- workqueue support exists
- iosys-map exists
- PCI helpers exist
- `linux/mmu_notifier.h` is currently dummy/partial
- `linux/hmm.h` is dummy-only
- `linux/mei_aux.h` is dummy-only
- `linux/relay.h` is dummy-only
- `linux/devcoredump.h` is minimal/stub-like
- real auxiliary-bus / MEI auxiliary integration is not clearly present

## Precedent Findings

AMDGPU is the best precedent for shared memory/sync architecture:

- FreeBSD split TTM into its own module.
- FreeBSD split dma-buf into its own module.
- amdgpu sits on common DRM helpers, TTM, dma-buf, dma-fence, dma-resv,
  firmware, and LinuxKPI.
- This suggests Xe should rely on shared DRM/TTM/dma-buf infrastructure rather
  than invent FreeBSD-local memory management.

i915 is the best Intel-specific precedent:

- large Intel driver imports can be staged and trimmed through build logic
- unsupported Linux semantics are often made explicit
- userptr is gated around `CONFIG_MMU_NOTIFIER`
- missing MEI/relay paths are handled explicitly
- the bad precedent to avoid is accumulating many long-lived FreeBSD-specific
  driver ifdefs where a shared helper would be better

FreeBSD DRM history suggests:

- small, exact LinuxKPI changes in `freebsd-src`
- mostly intact Linux DRM imports in `drm-kmod`
- shared infrastructure before driver consumers
- build hygiene and Linux diff reduction matter
- dummy headers can be scaffolding but must not be treated as real semantics

## Proposed Port Strategy

Do not start with a tiny hand-written probe-only Xe clone.

Use Linux 6.12 Xe structure as much as possible, but stage the build/import:

1. Produce a precise Linux 6.12 Xe source inventory.
2. Import `include/uapi/drm/xe_drm.h`.
3. Add `xe` module build plumbing in `drm-kmod-6.12`.
4. Import the non-display Xe core first, preserving Linux layout.
5. Defer display, tests, SR-IOV, OA/observation, full devcoredump, relay
   logging, and HECI GSC if they block initial bring-up.
6. Explicitly make `MAP_USERPTR` / HMM-backed userptr unsupported at first
   because FreeBSD lacks real MMU interval notifier and HMM semantics.
7. Bring up DG2/A380 first as a secondary GPU while Alder Lake iGPU remains
   the stable `i915` display.
8. Use Rocky Linux 10.1 as a Linux 6.12 operational A/B reference for Xe.
9. Later use B580 as a Battlemage/Xe reference case, since Rocky 10.1 packages
   `xe.ko`, has PCI alias `8086:E20B`, and includes BMG GuC/HuC firmware.

## Proposed Patch Split

Put in `freebsd-src-drm-6.12` when generic:

- real MMU interval notifier support
- real HMM support
- auxiliary-bus or `mei_aux` support
- generic firmware, PCI, DMA, VM, LinuxKPI, locking, or workqueue fixes
- compatibility helpers useful to non-Xe DRM drivers

Put in `drm-kmod-6.12` when Xe-specific:

- Xe subtree import
- Xe module build plumbing
- `xe_drm.h` import
- Xe-local feature gates
- temporary Xe-local unsupported paths for userptr, HECI GSC, relay,
  devcoredump, OA, display, or SR-IOV

Decision rule:

- if a change helps future non-Xe DRM ports, it probably belongs in
  `freebsd-src-drm-6.12`
- if it exists only because Xe needs it, it probably belongs in
  `drm-kmod-6.12`

## Proposed First Hardware Milestone

First honest milestone:

1. `xe` builds and links as a FreeBSD kmod
2. module load is real
3. A380 PCI match/probe occurs
4. attach starts without panic
5. firmware paths resolve
6. GuC/HuC-related firmware load gets far enough to diagnose real failures
7. MMIO, GT, VRAM, and IRQ init complete or fail at a clearly understood point
8. `drm_dev_register()` succeeds if the early init path gets far enough
9. render node appears only after the above is stable

Userptr, HMM, display replacement, HECI GSC, SR-IOV, OA, relay logging, and
full devcoredump parity are not required for the first milestone.

## Testing Policy

Existing tests must stay in their existing framework and pass.

For new project-owned tests:

- use Elixir for performance-insensitive orchestration, A/B comparison,
  log classification, report generation, bhyve coordination, and long-running
  hardware workflows
- use Zig for performance-sensitive, low-level, ioctl-facing, binary-parsing,
  timing-sensitive, stress, mmap, BO, VM-bind, fence, sync-object, and minimal
  DRM userspace probes
- when a test has both, Elixir should orchestrate and call Zig helper binaries

Raw logs, RPMs, build artifacts, traces, VM images, and crash dumps should not
be committed. Store them outside the repo, preferably in
`../wip-drm-xe-artifacts/`.

## Linux A/B Reference

Rocky Linux 10.x / RHEL 10.x are useful because they are 6.12-series enterprise
kernel environments.

Use:

- native Rocky Linux 10.1 boot for the cleanest same-hardware Linux reference
- Rocky Linux 10.1 under bhyve with GPU passthrough as a secondary workflow
  when useful, but do not treat bhyve passthrough as identical to native boot

B580 verification:

- Intel table maps Arc B580 to PCI ID `E20B`, architecture Xe2, codename
  Battlemage.
- local Linux 6.12 `XE_BMG_IDS` includes `0xE20B`.
- Rocky 10.1 kernel package contains `drivers/gpu/drm/xe/xe.ko.xz`.
- extracted Rocky `xe.ko` contains alias
  `pci:v00008086d0000E20Bsv*sd*bc03sc*i*`.
- Rocky `linux-firmware` contains:
  `usr/lib/firmware/xe/bmg_guc_70.bin.xz`
  `usr/lib/firmware/xe/bmg_huc.bin.xz`

This confirms B580 is a Linux `xe` path, not an `i915` path, for kernel-level
A/B testing.

## Questions For You

Please answer as a skeptical reviewer.

1. Is this patch split between `freebsd-src-drm-6.12` and `drm-kmod-6.12`
   correct for FreeBSD upstream review?
2. What important LinuxKPI or DRM dependencies are missing from the findings?
3. Is deferring userptr/HMM the right first-stage decision, or will Xe depend
   on those paths earlier than expected?
4. Is HECI GSC safe to stage after base attach/GT bring-up, or is there a
   hidden dependency that makes it an earlier blocker?
5. Is a non-display Xe core import realistic for A380/B580 early bring-up, or
   will display code be entangled enough that it must come earlier?
6. What would be the smallest honest Xe source subset to import first without
   creating a misleading toy driver?
7. What should the first FreeBSD hardware milestone be: build/load, PCI attach,
   firmware init, DRM node, render node, or basic submission?
8. Which pieces should be explicit `-ENODEV` / `-EOPNOTSUPP` at first?
9. Which FreeBSD APIs or LinuxKPI shims are most likely to cause subtle runtime
   bugs rather than compile failures?
10. What should the first 10 patches in an upstreamable series look like?
11. What FreeBSD developer objections should this plan anticipate?
12. Is the Rocky Linux 10.1 A/B strategy sound, and what extra data should be
   captured from Linux before debugging FreeBSD?
13. Is the Elixir/Zig testing split sensible for project-owned tests while
   preserving FreeBSD-native upstream tests?
14. What assumptions in this plan are weak, overconfident, or likely wrong?
15. What should be done next before writing any Xe port code?

## Desired Output

Please structure your answer as:

1. Major concerns
2. Missing dependencies or unknowns
3. Recommended patch-series structure
4. First hardware milestone recommendation
5. Testing/A-B strategy corrections
6. Specific next actions before coding

Be direct.
If the plan is wrong, say exactly where and why.
