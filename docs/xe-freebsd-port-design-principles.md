# Intel Xe on FreeBSD 6.12: Design Principles from FreeBSD DRM History

Date: 2026-04-18

## Purpose

This memo translates the local `drm-kmod-6.12` and
`freebsd-src-drm-6.12` histories into concrete design rules for the Xe port.

The question here is not only "what is missing?" but:

- how does FreeBSD usually port Linux graphics code
- what style produces the cleanest upstream path
- how should Xe fit that existing pattern

## Short Conclusion

The dominant pattern in the local history is:

> small, semantics-accurate LinuxKPI additions in `freebsd-src`, mostly
> intact Linux DRM imports in `drm-kmod`, common infrastructure before
> driver-specific use, and explicit unsupported paths where FreeBSD does not
> yet provide the Linux semantics.

For Xe, that means:

- Linux 6.12 stays frozen as the semantic and integration baseline
- generic LinuxKPI work stays small and precise
- Xe driver code stays close to upstream Linux layout and logic
- missing semantics are surfaced honestly, not papered over with fake stubs

The 6.12 pin is strategic.
It is not a claim that Linux 6.12 already contains final support for every
RDNA3+ or Intel Arc+ target.
The rationale is that 6.12 is an LTS line and FreeBSD's 6.12 DRM lane is the
realistic near-term merge target.
Newer hardware fixes should enter as named, justified backports rather than by
moving the whole port to a later Linux baseline.

The 6.12 pin also enables a practical A/B test setup.
Rocky Linux 10.x and RHEL 10.x use 6.12-series enterprise kernels, so Rocky
Linux 10.x can serve as a Linux operational reference for the A380 while
FreeBSD remains the port target.
Native side-by-side boot is the cleanest comparison; bhyve passthrough is a
useful secondary workflow when its hypervisor and passthrough differences are
kept explicit.

Testing follows a separate policy:
existing tests stay in their existing framework and must pass; new
performance-insensitive project tests use Elixir; new performance-sensitive or
low-level tests use Zig.
See [xe-freebsd-testing-policy.md](xe-freebsd-testing-policy.md).

External review feedback is integrated in:

- [claude-feedback-integration.md](claude-feedback-integration.md)
- [xe-runtime-semantic-risks.md](xe-runtime-semantic-risks.md)

## What the History Shows

### 1. LinuxKPI grows in small, exact increments

Representative `freebsd-src-drm-6.12` commits:

- `31ab4a71670` `linuxkpi: Define dev_err_probe*()`
- `49e8925f8e0` `linuxkpi: Define DEFINE_XARRAY*() macros`
- `cdef0906564` `linuxkpi: Add struct xa_limit support to xarray`
- `f3966d43b66` `linuxkpi: Add rwsem_is_contended()`
- `aaf6129c9dc` `linuxkpi: Add devm_add_action and devm_add_action_or_reset`
- `a6c2507d1ba` `LinuxKPI: add firmware loading support`

Pattern:

- one semantic unit at a time
- clear explanation of which Linux-side behavior now matters
- generic placement when more than one driver can benefit
- no attempt to hide unsupported behavior

Implication for Xe:

- do not submit a giant "all missing Xe LinuxKPI pieces" patch
- split notifier, firmware, device-core, and bus work into reviewable units

### 2. Dummy headers are accepted as scaffolding, not as completion

Representative commits:

- `d4eeb029869` `linuxkpi: Add a bunch of dummy include`
- `1bce29bcf12` `LinuxKPI: Add some header pollution and dummy headers`
- `cfe72b9f09f` `linuxkpi: Add mmu_notifier.h`

Pattern:

- FreeBSD is willing to add dummy headers so imports can compile
- that does not mean the underlying runtime semantics are present

This matters directly to Xe because the current lane still has:

- dummy `linux/hmm.h`
- dummy `linux/mei_aux.h`
- dummy `linux/relay.h`
- only a dummy `linux/mmu_notifier.h`

Implication for Xe:

- do not treat userptr, HMM, relay, or `mei_aux` as "basically done"
- either add the real semantics or fail the feature explicitly

### 3. `drm-kmod` imports upstream DRM code with minimal local delta

Representative `drm-kmod-6.12` commits:

- `cbbfff7d91` `drm: move the buddy allocator from i915 into common drm`
- `2186c2e3d0` `drm: execution context for GEM buffers v7`
- `49883ada0e` `drm/gpuvm: Dual-licence the drm_gpuvm code GPL-2.0 OR MIT`
- `c576b604b4` `drm/dp: Add support for DP tunneling`

Pattern:

- import the upstream helper nearly whole
- add minimal Makefile or Kconfig glue
- avoid rewriting the helper into a FreeBSD-specific architecture

Implication for Xe:

- Xe should sit on `drm_exec`, `drm_gpuvm`, `drm_buddy`, TTM, and
  `dma-resv`, not replace them with a custom FreeBSD VM design

### 4. Shared infrastructure moves out of drivers before later users depend on it

The history around `drm_buddy`, `drm_exec`, and `drm_gpuvm` shows a consistent
sequence:

1. generic code lands in common DRM
2. drivers consume it later

Implication for Xe:

- if something is truly common, land it commonly first
- do not hide reusable DRM or LinuxKPI work inside a monolithic Xe import

### 5. Build hygiene and Linux diff reduction are explicit values

Representative commits:

- `3492b0996a` `Add linuxkpi_version.mk for cross-project version tracking`
- `5b2279ae2a` `Remove ${.CURDIR}-relative linuxkpi include`
- `27efa85a03` `Remove remaining ${.CURDIR}-relative dummy includes`
- `7fafe85edf` `drm: Reduce diff with Linux 6.12 [FreeBSD]`

Pattern:

- build plumbing is treated seriously
- FreeBSD wants smaller diffs against Linux, not bigger ones
- local integration quirks get cleaned up over time

Implication for Xe:

- keep the Linux 6.12 tree shape intact
- update import tooling deliberately
- minimize FreeBSD-only churn that makes later sync harder

### 6. Staged unsupported behavior already exists in current FreeBSD DRM code

Current in-tree precedent already includes:

- i915 userptr paths guarded by `CONFIG_MMU_NOTIFIER`
- radeon and amdgpu HMM or notifier dependence
- FreeBSD-specific non-support around MEI integration
- FreeBSD-specific non-support around relay-backed logging

Representative history:

- `251ae6d0ac` `drm/i915/gsc: add gsc as a mei auxiliary device`
- `0625ae7e50` `drm/i915/huc: Don't fail the probe if HuC init fails`

Implication for Xe:

- first bring-up does not have to make every firmware or auxiliary path fatal
- explicit `-ENODEV` or `-EOPNOTSUPP` style behavior is acceptable when honest

## Xe-Specific Design Rules

### 1. Userptr is the first hard semantic boundary

Linux Xe 6.12 uses:

- `mmu_interval_notifier_*()`
- HMM-backed fault and repin logic
- `hmm_range_fault()`
- userptr state integrated into core VM and bind paths

The current FreeBSD 6.12 lane does not provide those semantics.

Design rule:

- do not fake HMM or interval-notifier support
- make `DRM_XE_VM_BIND_OP_MAP_USERPTR` explicitly unsupported at first
- reject `DRM_XE_VM_CREATE_FLAG_FAULT_MODE` at first even on DG2/A380, because
  DG2 advertises `has_usm = 1`
- keep the early cut local to Xe in `drm-kmod-6.12`
- treat real userptr enablement as a later, separate LinuxKPI MM effort
- keep BO-backed explicit VM_BIND as an early target because Linux 6.12 HMM is
  userptr-specific in the baseline source

### 2. HECI GSC should be staged, not made day-one success criteria

Local truth:

- Xe uses `linux/mei_aux.h` and auxiliary-device plumbing
- FreeBSD only has dummy header coverage there today
- `xe_heci_gsc_init(xe)` is not the first hard probe gate

Design rule:

- do not block the first real attach milestone on full HECI GSC support
- stage auxiliary-bus and `mei_aux` work separately if it proves reusable
- accept that A380 media/HuC paths may be incomplete until GSC is real
- treat B580/Battlemage as a second target because CSCFI/GSC may matter more

### 3. Relay logging and devcoredump completeness are secondary

Local truth:

- `linux/devcoredump.h` is present but stubbed
- `linux/relay.h` is dummy-only
- i915 already carries explicit missing-relay behavior on FreeBSD

Design rule:

- keep relay-backed logging out of the first milestone
- keep full devcoredump parity out of the first milestone
- make any non-support visible and temporary

### 4. DG2/A380 should be the first hardware target

The cleanest first target is:

- DG2 discrete attach on the Arc A380
- Alder Lake iGPU remains on `i915`
- no first-series requirement to replace the host display path

Design rule:

- separate secondary-GPU Xe bring-up from any host-display takeover question

## Recommended Patch Shape

### `freebsd-src-drm-6.12`

A change belongs here if it is genuinely reusable:

- MMU interval notifier support
- HMM support
- generic auxiliary-bus support
- generic `mei_aux` support
- generic firmware, DMA, PCI, VM, or LinuxKPI fixes

### `drm-kmod-6.12`

A change belongs here if it is Xe-specific:

- Xe subtree import
- `xe` kmod Makefiles and registration
- `xe_drm.h` import
- DG2-first force-probe or attach policy
- early unsupported paths for userptr, HECI GSC, OA, or devcoredump staging

## Recommended Staging

### Phase A: freeze semantics at Linux 6.12

- use `../nx/linux-6.12` as the primary reference
- use 6.12 because it is maintained LTS and matches FreeBSD's near-term DRM
  integration lane
- treat newer Linux code only as explicit backports
- document each backport by hardware need and upstream source

### Phase B: land truly generic prerequisites first

- LinuxKPI additions only when they are clearly reusable
- keep them small and independently reviewable

### Phase C: import Xe mostly intact

- import `include/uapi/drm/xe_drm.h`
- import the Xe subtree with Linux structure preserved
- add minimal FreeBSD build plumbing

### Phase D: make first unsupported cuts explicit

Likely first deferrals:

- userptr
- HECI GSC
- relay-backed logging
- full devcoredump integration
- display
- SR-IOV
- OA if it blocks early bring-up

### Phase E: target the first honest hardware milestone

The first hardware milestone should be:

1. `xe` builds and loads
2. DG2/A380 probes and attaches
3. MMIO BAR mapping and VRAM probing are real
4. GuC firmware and GuC CT reach a diagnosable state
5. GT and IRQ init complete or fail with useful logs
6. `drm_dev_register()` succeeds if initialization reaches that stage
7. render node appears only after the lower milestones are stable

### Phase F: compare against a Linux 6.12 operational oracle

- use Rocky Linux 10.x native boot for the cleanest same-hardware A/B signal
- use Rocky Linux 10.x under bhyve with A380 passthrough for faster reference
  testing when the host can reserve the A380 for `ppt`
- compare PCI, firmware, GuC/HuC/GSC, BAR, IRQ, GT, VRAM, and DRM-node
  milestones before comparing performance
- classify any difference as upstream 6.12, 6.12.y LTS, Rocky/RHEL downstream,
  FreeBSD LinuxKPI, FreeBSD DRM, or Xe-port local
- keep the detailed test strategy in
  [xe-freebsd-linux-ab-testing.md](xe-freebsd-linux-ab-testing.md)

### Phase G: make testing reproducible

- keep existing FreeBSD, drm-kmod, Linux, Mesa, or userspace tests in their
  native frameworks and make them pass
- use Elixir for new orchestration, A/B comparison, log classification, and
  long-running hardware workflow tests
- use Zig for new low-level, ioctl-facing, timing-sensitive, binary-parsing, or
  stress tests
- keep the detailed policy in
  [xe-freebsd-testing-policy.md](xe-freebsd-testing-policy.md)

### Phase H: audit runtime semantics before code

- verify `drm_sched` APIs against Linux 6.12
- verify `dma_fence_chain` and `dma_fence_array`, not only generic
  `dma-fence`
- validate `iosys-map` behavior for VRAM BAR mappings and WC/UC access
- validate PCI resize-BAR behavior for A380
- audit `devm_*` and `drmm_*` cleanup ordering
- audit workqueue flush/cancel semantics under load/unload
- treat runtime PM as always-on initially
- maintain [xe-runtime-semantic-risks.md](xe-runtime-semantic-risks.md)

## What Not To Do

- do not turn Xe into a DMO or DMI experiment
- do not redesign DRM memory management around future Mach-native ideas
- do not hide missing semantics behind empty shims
- do not make the Phase 1.0 OS the primary target before stock FreeBSD
- do not silently drift to post-6.12 Linux behavior
- do not treat the 6.12 pin as proof that all future RDNA3+ or Arc+ hardware
  needs are already covered
- do not treat Rocky/RHEL downstream behavior as an automatic replacement for
  the pinned upstream 6.12 source baseline
- do not treat bhyve passthrough results as identical to native hardware boot
- do not tie the first milestone to host display replacement
- do not rewrite existing tests into Elixir or Zig just because new
  project-owned tests prefer those languages
- do not let Phase 2 DMO/DMI architecture vocabulary leak into Xe patches
- do not include post-6.12 SVM/GPUSVM work unless it is explicitly chosen as a
  documented backport

## Review Discipline

Each future Xe patch should answer:

1. What exact Linux 6.12 behavior requires this change?
2. Why is this generic LinuxKPI or DRM work, or why is it Xe-only?
3. If the full semantics are absent, why is the staged deferral still honest?
4. What real hardware milestone does the patch move forward?
5. If Linux A/B data exists, does FreeBSD fail before, at, or after the same
   Rocky/RHEL 10.x Xe milestone?
6. Which existing tests still pass, and which new Elixir or Zig tests were run?
7. Which runtime semantic risk does this patch reduce or expose?

## Final Rule

The cleanest FreeBSD-shaped summary is:

> Port Xe the same way FreeBSD has been porting modern Linux graphics code:
> small generic LinuxKPI increments in `freebsd-src`, mostly intact Linux DRM
> imports in `drm-kmod`, common infrastructure before driver-specific use, and
> explicit staged deferrals where FreeBSD semantics do not yet exist.
