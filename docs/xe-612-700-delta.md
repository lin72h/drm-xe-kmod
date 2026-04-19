# Intel Xe 6.12 -> 7.0 Delta

Date: 2026-04-19

## Purpose

This note is the compact delta memo for the local Xe driver comparison between:

- `../nx/linux-6.12`
- `../nx/linux-7.0`

The Linux trees are local snapshot/zip trees, not git repos.
So this comparison is based on direct source-tree diffs, file counts, Kconfig
changes, and UAPI deltas.

This file is the short-form companion to:

- `docs/xe-6.12-vs-7.0-and-backports.md`

## Short Answer

Linux 7.0 Xe is a major expansion over Linux 6.12 Xe.
It is not a small maintenance update.

There are real benefits in 7.0:

- more platform coverage
- much larger UAPI
- deeper VM and GuC submission changes
- more explicit GPU-SVM / pagemap direction
- more PXP, telemetry, fault, and debug infrastructure

But for the FreeBSD-first upstream-oriented port, the recommendation remains:

- keep `6.12` as the actual semantic baseline
- use `7.0` as a tracked delta and selective-backport source
- do not move the whole port target to `7.0` unless Phase 3 explicitly needs
  7.0-only hardware coverage or 7.0-only features

That recommendation is conditional, not absolute.
It holds as long as FreeBSD's own DRM lane remains `6.12`-aligned.
If FreeBSD moves its DRM infrastructure forward before the Xe port is stable,
the baseline decision must be revisited even if the project itself would prefer
to stay on `6.12`.

It also assumes that `6.12` receives the correctness fixes needed for the
actual hardware we are targeting first.
Some of the `7.0` growth in VM and GuC-submit paths is likely hardening for
existing `DG2/BMG` behavior, not only new feature work.
So "6.12 baseline" should be read as:

- `6.12` as the semantic anchor
- plus ongoing tracking of `6.12.y` stable backports
- plus selective newer correctness pulls if `DG2/BMG`-relevant fixes do not
  land in the maintained `6.12` line

## Quantitative Delta

Local Xe subtree size:

- Linux 6.12: `410` files
- Linux 7.0: `522` files

Direct `drivers/gpu/drm/xe` delta from 6.12 to 7.0:

- `477 files changed`
- `60,496 insertions`
- `14,153 deletions`

Important large-file deltas:

- `xe_vm.c`: `3400 -> 4587` lines, `+1851 / -664`
- `xe_guc_submit.c`: `2264 -> 3400` lines, `+1611 / -475`
- `xe_pci.c`: `+555 / -290`
- `xe_exec.c`: `+56 / -38`
- `include/uapi/drm/xe_drm.h`: `1701 -> 2364` lines, `+678 / -15`

This is enough to treat 7.0 as a materially newer semantic line, not a small
step on top of 6.12.

It also helps separate dangerous churn from mostly mechanical churn:

- `xe_vm.c` and `xe_guc_submit.c` are the real semantic cost centers
- `xe_exec.c` changes are comparatively small
- `xe_pci.c` grows substantially, but much of that growth is platform and IP
  descriptor expansion rather than deep runtime semantic change

That does not mean every `xe_vm.c` or `xe_guc_submit.c` line in `7.0` is a new
feature.
Some fraction of that growth is likely bug fixes, race handling, and
correctness hardening for hardware that `6.12` already nominally supports.

## Main 7.0 Gains

### 1. Larger userspace ABI

The 7.0 UAPI grows significantly.
Examples include:

- `DRM_IOCTL_XE_MADVISE`
- `DRM_IOCTL_XE_VM_QUERY_MEM_RANGE_ATTRS`
- `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY`
- new queue-property surface
- CPU-address-mirror related flags
- low-latency and no-compression hint reporting
- additional PXP and EU-stall related query surface

Meaning:

- 7.0 offers more user-visible functionality
- 7.0 also creates more ABI surface the FreeBSD port must either implement or
  reject honestly

### 2. More explicit advanced memory-model direction

In local 7.0 Kconfig, Xe moves more clearly into:

- `DRM_XE_GPUSVM`
- `DRM_XE_PAGEMAP`
- `DRM_GPUSVM`

This pushes harder into:

- device-private memory
- CPU address mirroring
- pagemap-backed memory flows
- Linux MM territory we are intentionally deferring in the first FreeBSD port

Meaning:

- 7.0 is better if the goal is advanced SVM-like memory behavior
- 7.0 is worse if the goal is an honest, staged, upstreamable first FreeBSD Xe
  port on the 6.12 DRM lane

### 3. Much heavier scheduler and VM churn

The biggest growth is in:

- `xe_vm.c`
- `xe_guc_submit.c`
- `xe_exec.c`

That indicates real expansion in:

- VM policy
- pagefault / reclaim direction
- queue behavior and queue properties
- GuC submission machinery
- protected-content interaction

For our project, this matters more than raw file count because these are the
same areas already identified as the hardest to port cleanly.

It is also the place where the "new features vs correctness fixes" distinction
matters most.
The growth in `xe_vm.c` and `xe_guc_submit.c` should not be read as purely
future-facing feature expansion.
Part of it may be hardening for paths already relevant to:

- `DG2`
- `Battlemage`
- the ordinary GuC-submit / VM-bind paths we want first

### 4. Broader platform/IP coverage

7.0 `xe_pci.c` grows materially and adds broader GMDID/IP description work.
The local 7.0 tree clearly moves further into newer platform/IP coverage,
including Xe3-era direction beyond the current near-term bring-up targets.

That is useful only if Phase 3 is meant to include hardware beyond the current
first practical target set.

The local 6.12 tree already covers the near-term first-port targets we care
about first:

- DG2 / Arc A380
- Lunar Lake
- Battlemage

One concrete hardware gate is already visible:

- `Panther Lake` has zero mentions in local `6.12` `xe_pci.c`
- `Panther Lake` appears in local `7.0` `xe_pci.c`

### 5. More subsystems and support machinery

7.0 adds substantial new support areas, including examples such as:

- `xe_pxp*`
- `xe_pmu*`
- `xe_guc_pagefault.c`
- `xe_pagefault.c`
- `xe_page_reclaim.c`
- `xe_pci_rebar.c`
- `xe_mert.c`
- `xe_soc_remapper.c`
- `xe_guc_capture.c`
- `xe_hw_error.c`
- `xe_eu_stall.c`
- `xe_configfs.c`
- more SR-IOV PF/VF material

These are real capabilities, but they are also additional porting scope.

The pagefault point needs one correction:

- `6.12` already has `xe_gt_pagefault.c`
- `7.0` refactors/splits pagefault handling into `xe_pagefault.c` and
  `xe_guc_pagefault.c`
- `xe_page_reclaim.c` is the genuinely new part of that cluster

So the `7.0` pagefault story is not "pagefault support appears from nothing."
It is "pagefault handling is refactored, expanded, and paired with reclaim."

## Shared DRM / TTM Delta

This note is primarily about the Xe subtree, but Xe does not live in
isolation.

A quick `6.12 -> 7.0` shared-infrastructure check already shows nontrivial
movement in areas Xe depends on:

- `drivers/gpu/drm/ttm`: `25 files changed`, `1892 insertions`, `568 deletions`
- `include/drm`: `98 files changed`, `5797 insertions`, `1441 deletions`
- `drivers/gpu/drm/scheduler/sched_main.c`: `346 insertions`, `195 deletions`
- `drivers/gpu/drm/drm_gpuvm.c`: `633 insertions`, `141 deletions`
- `include/drm/drm_gpuvm.h`: `86 insertions`, `29 deletions`

This means a future retargeting decision cannot be made from `drivers/gpu/drm/xe`
alone.
The strongest external rebase trigger is not just hardware IDs.
It is movement in FreeBSD's imported DRM core / TTM / GPUVM lane.

## What 7.0 Matters Most

Not all 7.0 growth is equally important to a real FreeBSD Xe port.

### Tier 1: highest practical watch points

- `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY`
  - dynamic queue-property changes are a real userspace-compatibility concern
  - if Mesa requires this ioctl for correct DG2/BMG operation, it becomes a
    mandatory backport candidate rather than an optional future feature
- `DRM_IOCTL_XE_MADVISE`
  - affects memory-pressure and BO discard policy
  - this is closer to core BO behavior than to optional debug infrastructure
- `xe_pci_rebar.c`
  - resizeable-BAR support is a practical A380/B580 concern, not just a
    far-future feature
  - if BAR aperture sizing becomes a real lab issue, this is a strong
    near-term selective-backport candidate

### Tier 2: later-phase functionality

- `xe_guc_pagefault.c`
- `xe_pagefault.c`
- `xe_page_reclaim.c`
- `DRM_XE_GPUSVM`
- `DRM_XE_PAGEMAP`
- `DRM_GPUSVM`
- `xe_pxp*`

These matter if the project later decides to pursue:

- fault-mode VM
- broader GPU-SVM / pagemap semantics
- protected-content functionality

They are important, but they are not first-port requirements.
Also note the split:

- `xe_guc_pagefault.c` and `xe_pagefault.c` are a refactor/expansion of a path
  that already exists in `6.12`
- `xe_page_reclaim.c` is the more clearly new part

### Tier 3: useful but not baseline-shifting

- `xe_pmu*`
- `xe_eu_stall*`
- `xe_configfs.c`
- `xe_mert.c`
- `xe_soc_remapper.c`

These may be useful for profiling, platform plumbing, or diagnostics, but they
do not justify moving the whole FreeBSD baseline on their own.

## Why 6.12 Still Wins As The Port Baseline

For this project, the main question is not "which tree has more features?"
The real question is "which tree gives the cleanest upstreamable FreeBSD-first
port path?"

For that question, 6.12 still wins:

- it is the FreeBSD DRM lane we are already aligning to
- it already covers the first practical hardware targets
- it is smaller and easier to stage honestly
- it reduces VM, scheduler, and Linux-MM churn during the first port
- it keeps the patch story cleaner for FreeBSD review

There is also a review-context advantage:

- FreeBSD DRM reviewers are reviewing against a `6.12`-aligned lane today
- a `7.0`-derived port against a `6.12` review context creates avoidable
  review friction even before the code is discussed on technical merit

## Selective Backport Warning

"Track 7.0 and pull forward selected features" should not be read as
"cheap cherry-picks."

The `6.12 -> 7.0` delta is large enough that selective backport requires real
dependency analysis.
In particular:

- VM changes
- GuC submission changes
- pagefault / reclaim changes

are intertwined enough that lifting one visible feature may require supporting
infrastructure from several neighboring files.
In the worst case, a desired `7.0` fix in the VM / GuC-submit cluster may turn
out to be closer to an all-or-nothing pull than to a clean isolated backport.

## When 7.0 Would Become Worth Retargeting

### Hardware-forced or infrastructure-forced retargeting

Move the whole effort to `7.0` if one of these becomes true:

- `Panther Lake` or other post-Battlemage / Xe3-family hardware must be
  supported
- FreeBSD's own DRM lane, including shared DRM core / TTM / GPUVM imports,
  moves beyond `6.12` before the Xe port is complete
- critical `DG2/BMG` correctness fixes stop landing in `6.12.y` and exist only
  in newer Xe lines

These are baseline-shifting conditions, not feature preferences.

### Planned later-phase retargeting

Reconsider a `7.0` semantic target if later phases require:

- fault-mode VM
- broader GPU-SVM / pagemap semantics
- protected-content functionality on hardware that depends on the newer line

### Selective backport without full retargeting

These are better treated as explicit backport candidates first:

- `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY`
- `DRM_IOCTL_XE_MADVISE`
- `xe_pci_rebar.c`
- specific 7.0 bug fixes proven relevant to DG2/BMG

The highest immediate watch point is Mesa compatibility.
If Mesa requires `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY` for correct DG2/BMG
operation, that ioctl becomes a mandatory backport candidate and may become the
earliest practical pressure toward a `7.0`-style userspace contract.

## Recommendation

The practical rule should be:

> Keep Linux 6.12 as the FreeBSD Xe semantic baseline.
> Track Linux 7.0 as the next-feature delta line.
> Pull forward only clearly justified 7.0 features later.

and read that with one explicit condition:

> This holds only while FreeBSD's DRM lane stays 6.12-aligned.

That keeps the current port:

- FreeBSD-first
- upstream-oriented
- reviewable
- 6.12-aligned
- honest about what is and is not in scope

and it should be paired with one active maintenance rule:

- monitor `6.12.y` stable and newer DG2/BMG-relevant correctness fixes for the
  VM / GuC-submit paths we actually use
