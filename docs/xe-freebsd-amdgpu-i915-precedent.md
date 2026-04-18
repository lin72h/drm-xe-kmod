# AMDGPU and i915 on FreeBSD: Porting Precedent for Xe

Date: 2026-04-18

## Purpose

This memo studies the two most relevant FreeBSD precedents for Xe:

- `amdgpu`, especially for TTM, dma-buf, fencing, and shared DRM helpers
- `i915`, as the Intel predecessor and the closest example of how FreeBSD has
  historically handled Intel-specific gaps and staged unsupported features

The goal is not to retell the full history.
It is to extract the porting strategy that should inform the Xe plan.

## Short Conclusion

The strongest local conclusion is:

> Xe should follow the `amdgpu` model for shared memory-management and
> synchronization infrastructure, and the `i915` model for staged Intel-
> specific deferrals and explicit unsupported behavior.

More concretely:

- follow `amdgpu` for module layering:
  `drm` + `ttm` + `dmabuf` + LinuxKPI + firmware
- follow `amdgpu` and recent DRM history for common-helper extraction before
  driver-specific consumers
- follow `i915` for honest `-ENODEV` and `-EOPNOTSUPP` behavior when Linux
  semantics are missing on FreeBSD
- avoid copying the older `i915` accumulation of bespoke FreeBSD ifdefs where
  a shared helper can instead absorb the difference

This strategy assumes the DRM base stays pinned to Linux 6.12.
That pin is not because 6.12 is the final answer for every RDNA3+ or Intel
Arc+ hardware target.
It is because Linux 6.12 is an LTS line and FreeBSD's 6.12 DRM lane is the
near-term mergeable base.
Hardware enablement beyond 6.12 should be handled as explicit, reviewable
backports on top of that base.

## Why `amdgpu` Is the Better Xe Precedent for Core Architecture

### 1. FreeBSD split the shared substrate out into dedicated modules

The local history shows that FreeBSD did not keep modern DRM support trapped
inside a single driver.
It deliberately separated the common layers first.

Representative commits:

- `ee2f541deb` `Make ttm its own module so things are initialized in correct order`
- `5d8be3b799` `dma-buf: Make it a separate module`
- `cbbfff7d91` `drm: move the buddy allocator from i915 into common drm`
- `2186c2e3d0` `drm: execution context for GEM buffers v7`
- `49883ada0e` `drm/gpuvm: Dual-licence the drm_gpuvm code GPL-2.0 OR MIT`
- `73d9784ff3` `drm/amd: Convert amdgpu to use suballocation helper`
- `25dbc59405` `drm/dp: Move DisplayPort helpers into separate helper module`
- `be63263658` `drm/display: Introduce a DRM display-helper module`

This is exactly the direction Xe should follow.

The porting strategy is not:

- import a driver first
- clean up shared infrastructure later

It is:

- land or import shared DRM pieces as shared DRM pieces
- keep drivers sitting on top of them

### 2. Current `amdgpu` already reflects the layered design

Current module dependencies show the real shape of the FreeBSD port:

- `amdgpu -> drmn`
- `amdgpu -> ttm`
- `amdgpu -> linuxkpi`
- `amdgpu -> linuxkpi_video`
- `amdgpu -> dmabuf`
- `amdgpu -> firmware`

That dependency shape comes from:

- [amdgpu_freebsd.c](../nx/drm-kmod-6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_freebsd.c)
- [ttm_module.c](../nx/drm-kmod-6.12/drivers/gpu/drm/ttm/ttm_module.c)
- [dma-buf-kmod.c](../nx/drm-kmod-6.12/drivers/dma-buf/dma-buf-kmod.c)

This matters because it shows how FreeBSD expects a modern GPU driver to fit
into its DRM lane:

- the driver depends on generic memory and synchronization modules
- the generic modules depend on LinuxKPI
- the driver itself stays closer to Linux because the OS adaptation burden is
  pushed downward

That is the right model for Xe.

### 3. `amdgpu` relies on generic TTM and dma-buf APIs rather than bespoke FreeBSD interfaces

Current `amdgpu` code shows that the driver is written against the generic DRM
and dma-buf abstractions, not a special FreeBSD-only memory stack.

Relevant files:

- [amdgpu_ttm.c](../nx/drm-kmod-6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_ttm.c)
- [amdgpu_dma_buf.c](../nx/drm-kmod-6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_dma_buf.c)

Representative usage visible in-tree today:

- `ttm_range_man_init()`
- `ttm_bo_validate()`
- `ttm_operation_ctx`
- `dma_map_sgtable()`
- `drm_prime_pages_to_sg()`
- `dma_fence_array`
- `dma-resv`

That is important for Xe because it argues strongly against inventing a
FreeBSD-only GPU VM or BO architecture for the first port.

The lesson from `amdgpu` is:

> keep the driver Linux-shaped, and make TTM, dma-buf, and generic DRM helpers
> do the portability work.

### 4. `amdgpu` has fewer direct FreeBSD-specific branches than `i915`

As a rough indicator from the current local tree, counting `__FreeBSD__` and
`BSDFIXME` markers yields approximately:

- `amdgpu`: 39
- `i915`: 137

This is not a perfect metric, but it matches the qualitative picture:

- `amdgpu` is more cleanly layered onto shared infrastructure
- `i915` carries more historical, Intel-specific, and long-lived FreeBSD
  adaptation branches

That is another reason `amdgpu` is the better precedent for Xe core structure.

## Why `i915` Is Still Critical as the Intel Predecessor

### 1. `i915` shows how FreeBSD handled a large Intel import over time

The older `i915` history shows the pattern of a long-lived import that was
progressively cleaned up, re-gated, and trimmed.

Representative commits:

- `7e755f4c27` `[i915] delete existing and copy in files from 4.11, update makefile`
- `71be26d17d` `i915: kconfig: Only add some files when the corresponding option is enabled`
- `35e4cecab1` `i915: Remove gtv and selftests, we do not use them`

This tells us two important things:

- FreeBSD is willing to trim unsupported Linux-only areas out of a driver
  import when they do not make sense locally
- the trim should happen in a visible and structured way, usually in build
  logic first

For Xe, this supports:

- dropping or deferring tests, display, SR-IOV, or other secondary pieces
  explicitly in the first series
- using Makefile and Kconfig-style gates rather than scattering ad hoc ifdefs
  everywhere

### 2. `i915` is the clearest precedent for Intel-specific explicit non-support

Current `i915` carries several examples of honest staged non-support:

- userptr gated by `CONFIG_MMU_NOTIFIER`
- explicit `-ENODEV` returns in userptr stubs
- explicit no-support around MEI-dependent functionality
- explicit no-support around relay-backed logging

Relevant files:

- [i915_gem_object.h](../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gem/i915_gem_object.h)
- [i915_gem_userptr.c](../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gem/i915_gem_userptr.c)
- [intel_huc.c](../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gt/uc/intel_huc.c)
- [intel_guc_log.c](../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gt/uc/intel_guc_log.c)

Representative precedent:

- if `CONFIG_MMU_NOTIFIER` is absent, i915 userptr helpers return `-ENODEV`
- missing MEI or relay support is made explicit rather than hidden

That is directly relevant to Xe because Xe has the same class of Intel-specific
problem around:

- userptr and HMM
- `mei_aux`
- relay-backed logging

For Xe, the `i915` lesson is:

> where FreeBSD lacks the Linux semantic, prefer explicit unsupported behavior
> over fake compatibility.

### 3. `i915` also shows that FreeBSD eventually moved Intel helpers into common DRM when appropriate

Even on the Intel side, FreeBSD did not keep everything trapped inside i915.

Representative commits:

- `cbbfff7d91` moved the buddy allocator out of i915 into common DRM
- `d6d7232933` synced a shared `drm/i915 & drm/xe` change before Xe itself
  was imported

So even though i915 is the Intel predecessor, the correct takeaway is not
"follow i915's local special cases".

The correct takeaway is:

- use i915 for staged Intel-specific deferrals
- use shared DRM extraction whenever the code is truly generic

## LinuxKPI Precedent Behind Both Drivers

The base-system history matters because FreeBSD did not solve these ports only
inside `drm-kmod`.
It also grew LinuxKPI in response to actual graphics-driver needs.

Representative older LinuxKPI commits:

- `0b7bd01a822` `Add a specialized function for DRM drivers to register themselves`
- `5098ed5f3b0` `Implement linux_pci_unregister_drm_driver in linuxkpi so that drm drivers can be unloaded`
- `4f109faadc8` `Implement pci_enable_msi() and pci_disable_msi() in the LinuxKPI`
- `4abbf816bf0` `LinuxKPI: upstream a collection of drm-kmod conflicting changes`

Representative newer LinuxKPI commits:

- `a6c2507d1ba` `LinuxKPI: add firmware loading support`
- `fb3c549738b` `LinuxKPI: firmware: add request_partial_firmware_into_buf()`
- `31ab4a71670` `linuxkpi: Define dev_err_probe*()`
- `49e8925f8e0` `linuxkpi: Define DEFINE_XARRAY*() macros`
- `cdef0906564` `linuxkpi: Add struct xa_limit support to xarray`
- `f3966d43b66` `linuxkpi: Add rwsem_is_contended()`

The pattern is very consistent:

- generic Linux semantics go to LinuxKPI
- they land in small, targeted patches
- they are often justified by specific DRM or amdgpu usage

That is the right model for Xe too.

## The Most Relevant AMDGPU and i915 Lessons for Xe

### 1. Copy `amdgpu` for the module and helper architecture

Xe should be shaped like:

- `drm`
- `ttm`
- `dmabuf`
- LinuxKPI
- firmware
- then `xe`

Not:

- one giant monolithic Xe port with custom local memory plumbing

### 2. Copy `amdgpu` and recent DRM history for generic helper extraction

Before or during Xe bring-up, use shared DRM or LinuxKPI work for:

- generic memory-management helpers
- generic synchronization helpers
- generic display helpers
- generic GPU VM helpers

Only keep something in Xe if it is truly Xe-only.

### 3. Copy `i915` for staged Intel-specific deferrals

For early Xe, it is acceptable and precedented to defer or gate:

- `MAP_USERPTR`
- `mei_aux` / HECI GSC integration
- relay-backed logging
- other Intel-specific optional subsystems

But the non-support must be:

- explicit
- reviewable
- honest at runtime

### 4. Prefer build-gated trimming over invasive code distortion

The i915 history around `kconfig.mk` and feature trimming shows that FreeBSD
prefers:

- excluding unsupported source files from the build when possible
- reducing the number of in-source FreeBSD conditionals when build logic can
  do the job instead

For Xe, that argues for:

- starting with non-display core import
- deferring display and tests via build plumbing
- deferring clearly isolated Linux-only features through build or top-level
  stubs, not broad driver rewrites

### 5. Keep userptr out of the first truthful milestone

This point is reinforced by both precedents:

- i915 gates userptr behind notifier support and returns `-ENODEV` when it is
  unavailable
- amdgpu HMM support is explicitly gated behind `CONFIG_HMM_MIRROR`

Relevant files:

- [amdgpu_hmm.h](../nx/drm-kmod-6.12/drivers/gpu/drm/amd/amdgpu/amdgpu_hmm.h)
- [i915_gem_object.h](../nx/drm-kmod-6.12/drivers/gpu/drm/i915/gem/i915_gem_object.h)

For Xe, the precedent is clear:

- do not fake HMM or interval-notifier support
- make userptr unsupported first
- add real MMU and HMM support later if needed in `freebsd-src-drm-6.12`

## Recommended Xe Strategy Derived from This Research

### Pin the base to DRM 6.12

- use 6.12 as the common review and integration baseline
- rely on the fact that 6.12 LTS remains maintained
- align with FreeBSD's near-merge 6.12 DRM lane
- add newer RDNA3+ or Arc+ enablement as explicit backports only when needed

### Use Rocky Linux 10.x as the Linux-side A/B reference

- use Rocky/RHEL 10.x 6.12-series kernels to observe Linux Xe behavior on the
  same A380 hardware
- prefer native side-by-side boot for authoritative hardware comparison
- use bhyve passthrough as a useful secondary workflow, not as identical native
  hardware truth
- keep `../nx/linux-6.12` as the source-semantic baseline even when Rocky/RHEL
  exposes useful downstream or 6.12.y fixes

### Use `amdgpu` as the primary architectural template

- separate `xe` from `ttm`, `dmabuf`, and `drm`
- rely on generic DRM helpers as much as possible
- keep the driver Linux-shaped

### Use `i915` as the Intel-specific risk-management template

- explicitly defer unsupported Intel-only paths
- use `-ENODEV` and `-EOPNOTSUPP` honestly
- trim build scope before rewriting behavior

### Avoid inheriting the wrong parts of old i915

Do not copy:

- years of accumulated local ifdefs unless absolutely necessary
- legacy feature baggage
- driver-local solutions for problems that now have shared DRM helpers

## Final Rule

The cleanest summary from the local AMDGPU and i915 precedent is:

> Port Xe the way FreeBSD now ports large Linux DRM drivers:
> push generic memory and synchronization work into shared `drm`, `ttm`,
> `dmabuf`, and LinuxKPI layers like `amdgpu` does, while handling Intel-
> specific missing semantics with the same explicit staged deferrals that
> existing `i915` code already uses.
