# Intel Xe on FreeBSD 6.12: Compile Bootstrap Findings

Date: 2026-04-19

## Purpose

This memo integrates the latest GLM review against the local Linux 6.12,
`drm-kmod-6.12`, and `freebsd-src-drm-6.12` trees.

The main correction is structural:

> The first hard problem is not runtime semantics.
> The first hard problem is making `xe.ko` compile and link.

Runtime risk remains important, but there are already confirmed compile and
link blockers that must be treated as Phase 0.

## Verified Local Compile Blockers

The following blockers were checked against the local trees.

### 1. `xe_hmm.c` compile trap

Local Linux 6.12 facts:

- `drivers/gpu/drm/xe/Kconfig` unconditionally selects `HMM_MIRROR`
- `drivers/gpu/drm/xe/Makefile` has `xe-$(CONFIG_HMM_MIRROR) += xe_hmm.o`
- local FreeBSD LinuxKPI `dummy/include/linux/hmm.h` is zero bytes

Consequence:

- `xe_hmm.c` will be compiled unless the FreeBSD build overrides
  `CONFIG_HMM_MIRROR=n`
- even if BO-backed VM_BIND does not use HMM at runtime, the source still does
  not compile with the current dummy header

Phase-0 options:

- override `CONFIG_HMM_MIRROR=n` in `drm-kmod-6.12` and add the needed guards
- or add FreeBSD-local `#if IS_ENABLED(CONFIG_HMM_MIRROR)` guards and no-op
  stubs around `xe_hmm` entry points

### 2. Missing `drmm_mutex_init`

Local Linux 6.12 facts:

- Xe uses `drmm_mutex_init()` in:
  - `xe_guc_ct.c`
  - `xe_guc_submit.c`
  - `xe_guc_pc.c`
  - `xe_pcode.c`
  - `xe_pm.c`
  - `xe_oa.c`
  - `xe_sriov_pf.c`
- Linux implements `drmm_mutex_init` in `include/drm/drm_managed.h`
- Linux exports `__drmm_mutex_release` from `drivers/gpu/drm/drm_managed.c`

Local FreeBSD facts:

- `../nx/drm-kmod-6.12/include/drm/drm_managed.h` lacks both
  `drmm_mutex_init` and `__drmm_mutex_release`

Consequence:

- this is a hard build/link blocker
- it is also in the CT path because `xe_guc_ct.c` uses `drmm_mutex_init()`

Likely ownership:

- `drm-kmod-6.12`, because this is DRM managed-resource infrastructure rather
  than a generic LinuxKPI device-model function

### 3. Missing `devm_release_action`

Local Linux 6.12 fact:

- `xe_bo.c` calls `devm_release_action()` at line 1665

Local FreeBSD facts:

- `linux_devres.c` implements `devm_add_action()` and
  `devm_add_action_or_reset()`
- there is no `devm_release_action()`

Consequence:

- hard link blocker for `xe_bo.c`

Likely ownership:

- `freebsd-src-drm-6.12`, because this is generic LinuxKPI devres behavior

### 4. Missing `devm_ioremap_wc`

Local Linux 6.12 facts:

- `xe_ttm_stolen_mgr.c` calls `devm_ioremap_wc()`
- other DRM drivers also use this helper upstream

Local FreeBSD facts:

- current LinuxKPI does not provide `devm_ioremap_wc()`

Consequence:

- early compile blocker
- also relevant to MMIO / cache-attribute correctness even if initially
  aliased to `devm_ioremap()`

Likely ownership:

- `freebsd-src-drm-6.12`

### 5. Missing `linux/auxiliary_bus.h`

Local FreeBSD facts:

- `linux/mei_aux.h` exists only as a zero-byte dummy header
- there is no `linux/auxiliary_bus.h` at all in the local 6.12 lane

Consequence:

- `xe_heci_gsc.c` cannot compile through the include chain unless at least a
  stub `auxiliary_bus.h` exists
- HECI GSC may be staged at runtime, but it still needs compile scaffolding

Likely ownership:

- `freebsd-src-drm-6.12` for the stub header and generic auxiliary-bus
  scaffolding

### 6. Kconfig symbol bootstrap gap

Local Linux 6.12 facts:

- Xe Kconfig selects a large set of DRM and generic kernel symbols
- `../nx/drm-kmod-6.12/kconfig.mk` currently declares only a small subset of
  the symbols Xe expects

Examples of likely missing `CONFIG_*` definitions to bootstrap:

- `DRM_BUDDY`
- `DRM_TTM`
- `DRM_TTM_HELPER`
- `DRM_SCHED`
- `DRM_PANEL`
- `DRM_SUBALLOC_HELPER`
- `DRM_DISPLAY_HELPER`
- `DRM_DISPLAY_DP_HELPER`
- `DRM_DISPLAY_HDCP_HELPER`
- `DRM_DISPLAY_HDMI_HELPER`
- `SYNC_FILE`
- `IOSF_MBI`
- `CRC32`
- `INTERVAL_TREE`
- `SHMEM`
- `TMPFS`
- `IRQ_WORK`
- `VMAP_PFN`
- `WANT_DEV_COREDUMP`

Consequence:

- the build will not resolve `#ifdef` and `IS_ENABLED()` checks correctly until
  the Xe Kconfig surface is represented in `kconfig.mk`

Likely ownership:

- `drm-kmod-6.12`

## Corrected Bring-Up Order

The ladder should start with compile bootstrap, not hardware.

### Phase 0: Compile bootstrap

0a. Add missing `CONFIG_*` defines in `drm-kmod-6.12/kconfig.mk`

0b. Set `CONFIG_DRM_XE_DISPLAY=n` for the initial import

0c. Resolve `CONFIG_HMM_MIRROR` strategy:

- either override it to `n`
- or add FreeBSD-local guards and stubs around `xe_hmm`

0d. Add `drmm_mutex_init` and `__drmm_mutex_release`

0e. Add `devm_release_action`

0f. Add `devm_ioremap_wc`

0g. Add stub `linux/auxiliary_bus.h`

0h. Import `include/uapi/drm/xe_drm.h`

0i. Create `drivers/gpu/drm/xe/` build plumbing and iterate until `xe.ko`
links cleanly

### Phase 1: Load and probe

1a. `xe.ko` builds and links

1b. module loads without panic

1c. A380 PCI match fires

1d. MMIO BAR mapping succeeds

1e. VRAM probing reports sane size and aperture

1f. early attach completes without panic

### Phase 2: Firmware and transport

2a. GuC firmware is found and validated

2b. MMIO communication with GuC succeeds

2c. ADS setup succeeds

2d. CT buffer allocation and GGTT pinning succeed

2e. first fire-and-forget H2G CT message succeeds

2f. first blocking H2G/G2H exchange succeeds

2g. GT init and IRQ setup complete

### Phase 3: DRM registration

3a. `drm_dev_register()` succeeds with display disabled

3b. render node appears

3c. minimal `xe_drm.h` ioctls are reachable from userspace

### Phase 4: BO, memory, and VM

4a. VRAM BO allocation works

4b. system BO allocation works

4c. BO mmap works

4d. eviction under memory pressure works

4e. BO-backed VM_BIND works with fault mode disabled

### Phase 5: Submission

5a. exec queue create/destroy works

5b. basic job submission completes

5c. completion fence signals cleanly

## MMIO Before CT

The GLM review refined one earlier statement.

CT remains the first major shared-memory runtime gate before submission, but
there is an earlier proof point:

- MMIO communication with GuC during early firmware setup and CT registration

This means the corrected wording is:

- MMIO is the first firmware transport proof
- CT is the first shared-memory runtime gate

The plan should not skip the MMIO proof step between firmware load and CT.

## Recommended First CT Checks

Recommended first fire-and-forget CT test:

- `pc_action_reset()` in `xe_guc_pc.c`

Recommended first blocking CT test:

- `xe_guc_auth_huc()` in `xe_guc.c`

The reason is simple:

- `pc_action_reset()` is small and exercises H2G send
- `xe_guc_auth_huc()` exercises both H2G and G2H handling

## Earliest Falsification Test

Add a new earliest falsification test before hardware:

1. `xe.ko` links without unresolved symbols

If that fails, the current import and support surface is still incomplete and
the runtime plan is premature.

## Final Rule

Do not treat compile bootstrap as routine glue work.
For Xe on FreeBSD 6.12, compile bootstrap is Phase 0 engineering.

