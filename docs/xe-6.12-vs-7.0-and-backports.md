# Intel Xe 6.12 vs 7.0 vs xekmd-backports

Date: 2026-04-19

## Purpose

This memo compares three local Xe code lines:

- `../nx/linux-6.12`
- `../nx/linux-7.0`
- `../nx/xekmd-backports`

The goal is not to choose a new immediate port baseline.
The goal is to answer a narrower Phase 3 planning question:

> Should the FreeBSD Xe effort keep Linux 6.12 as the semantic target, or is
> the Xe delta in Linux 7.0 large enough that Phase 3 should move to 7.0?

This memo uses the local source trees directly.
The Linux trees are zip/snapshot trees, not git repos, so the comparison is
done with direct file and subtree diffs, not commit history.

## Short Conclusion

Linux 7.0 Xe is not "6.12 plus a few fixes."
It is a materially larger driver line with:

- much larger UAPI
- much deeper VM and GuC submission changes
- new GPU-SVM / pagemap direction
- new PXP and memory-policy surfaces
- broader platform / IP coverage
- more SR-IOV, telemetry, error, and debug infrastructure

That means there **are** real benefits in 7.0.
But they do **not** automatically justify moving the FreeBSD port baseline.

The recommendation is:

- keep **Linux 6.12** as the actual FreeBSD port baseline through Phase 3
- do **not** retarget the whole port to 7.0 unless Phase 3 explicitly needs a
  7.0-only feature or a 7.0-only hardware family
- treat 7.0 as a **delta-tracking and selective-backport source**
- treat `xekmd-backports` as **shipping-evidence and compat-reference**, not as
  the semantic baseline

For the current FreeBSD-first upstream path, 6.12 remains the cleaner target.
For future work, 7.0 is the right place to mine selective follow-on features.
That recommendation is conditional on FreeBSD's own DRM lane remaining
`6.12`-aligned while the first Xe port is still being brought up.
If FreeBSD moves its DRM infrastructure forward first, this decision must be
revisited.
It also assumes that `6.12` continues to receive the correctness fixes needed
for the `DG2/BMG` paths we actually intend to use.
Some of the `7.0` growth in `xe_vm.c` and `xe_guc_submit.c` is likely
hardening for existing hardware behavior, not only future-facing feature work.

## Trees Compared

### Linux 6.12 snapshot

- path: `../nx/linux-6.12`
- role: current semantic baseline and FreeBSD DRM-lane target

### Linux 7.0 snapshot

- path: `../nx/linux-7.0`
- role: newer upstream Xe line under consideration for future Phase 3+ work

### Intel xekmd-backports

- path: `../nx/xekmd-backports`
- branch: `releases/main`
- role: Intel out-of-tree backport packaging / compat tree

Important local fact:

- `xekmd-backports` is a git repo
- `linux-6.12` and `linux-7.0` are snapshots, not git repos

## Quantitative Delta

Xe file counts in the local trees:

- Linux 6.12 Xe subtree: `410` files
- Linux 7.0 Xe subtree: `522` files
- `xekmd-backports` Xe subtree: `522` files

Direct `drivers/gpu/drm/xe` subtree delta from 6.12 to 7.0:

- `477 files changed`
- `60,496 insertions`
- `14,153 deletions`

This is not a maintenance-sized update.
It is a major semantic expansion.

Large changed files include:

- `xe_vm.c`: `3400 -> 4587` lines, `+1851 / -664`
- `xe_guc_submit.c`: `2264 -> 3400` lines, `+1611 / -475`
- `xe_pci.c`: `+555 / -290`
- `xe_exec.c`: `+56 / -38`
- `include/uapi/drm/xe_drm.h`: `1701 -> 2364` lines, `+678 / -15`

That file-level spread matters:

- this is not just more platform IDs
- this is scheduler, VM, and userspace ABI growth

It also helps rank where the real porting pain lives:

- `xe_vm.c` and `xe_guc_submit.c` are the primary semantic cost centers
- `xe_exec.c` is comparatively stable
- `xe_pci.c` grows substantially, but much of that growth is platform and IP
  description expansion rather than deep runtime semantic change

That distinction matters because not all of the VM / GuC-submit growth is
"new feature" growth.
Some portion is likely bug fixing, race handling, and correctness hardening for
hardware that `6.12` already nominally supports.

## What 7.0 Adds Over 6.12

### 1. Much larger UAPI

The most obvious difference is `include/uapi/drm/xe_drm.h`.
The 7.0 UAPI grows substantially.

New or expanded visible areas include:

- `DRM_IOCTL_XE_MADVISE`
- `DRM_IOCTL_XE_VM_QUERY_MEM_RANGE_ATTRS`
- `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY`
- PXP status query
- EU stall query
- low-latency hint reporting
- CPU address mirror capability reporting
- no-compression hint reporting
- new GEM create behavior and properties
- new VM bind flags and CPU-address-mirror semantics
- new exec-queue property setting surface

This means 7.0 is not just "same Xe driver, more hardware IDs."
It is a visibly broader userspace contract.

### 2. GPU-SVM / pagemap direction becomes explicit

`drivers/gpu/drm/xe/Kconfig` in 7.0 introduces or makes explicit:

- `DRM_XE_GPUSVM`
- `DRM_XE_PAGEMAP`
- `DRM_GPUSVM`

and ties Xe more directly to:

- `ZONE_DEVICE`
- device-private memory
- CPU address mirroring
- pagemap-backed SVM behavior

This is important for the FreeBSD port because it moves directly into the
Linux MM / HMM / device-memory territory we have intentionally deferred.

In practical terms:

- 7.0 makes the advanced memory model **more useful**
- 7.0 also makes the advanced memory model **more expensive to port**

### 3. Scheduler / VM complexity increases substantially

The three most important core files all grow heavily:

- `xe_vm.c`
- `xe_guc_submit.c`
- `xe_exec.c`

7.0 adds or expands:

- CPU address mirror bindings
- madvise-related policy flows
- more queue-property and multi-queue behavior
- more PXP integration into queue and VM behavior
- more detailed pagefault and reclaim plumbing

This is a major semantic delta, not just refactoring noise.
It is also where the "feature delta vs correctness delta" question matters
most.
The `7.0` growth in `xe_vm.c` and `xe_guc_submit.c` should not be read as
purely optional future capability.
Some of it may be correctness hardening for ordinary `DG2/BMG` GuC-submit and
VM-bind paths.

### 4. Platform and IP coverage expands

`xe_pci.c` is substantially reworked in 7.0.
Notable changes include:

- migration from the older `xe_pciids.h` include style to `drm/intel/pciids.h`
- more GMDID-based IP tables
- additional IP naming and identification structure
- explicit Xe3 / Xe3p-era graphics/media IP entries
- additional platform coverage beyond the 6.12 line

Local 7.0 `xe_pci.c` includes explicit platform/IP material for:

- Lunar Lake
- Battlemage
- Panther Lake
- NVL / Xe3-related entries
- Xe3 LPG / LPM / XPC style IP descriptions

By contrast, the local 6.12 tree already covers the current practical
near-term targets we care about first:

- DG2 / Arc A380
- Lunar Lake
- Battlemage

So 7.0's platform expansion is real, but it is mostly valuable if Phase 3 is
supposed to include **post-BMG / Xe3-family hardware**.
One concrete example is already visible:

- local `6.12` `xe_pci.c` has zero `Panther Lake` mentions
- local `7.0` `xe_pci.c` includes `PLATFORM(PANTHERLAKE)`

### 5. New subsystems appear

Examples of files added in 7.0 that do not exist in 6.12:

- `xe_pxp*.c` / `xe_pxp_submit.c`
- `xe_pmu*.c`
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
- more SR-IOV PF / VF support

This says 7.0 is pushing in several directions at once:

- protected content / PXP
- better telemetry / PMU / EU-stall style reporting
- more fault and reclaim infrastructure
- richer SR-IOV
- more platform-specific system plumbing

One correction is important here:

- `6.12` already has `xe_gt_pagefault.c`
- `7.0` refactors/splits that area into `xe_pagefault.c` and
  `xe_guc_pagefault.c`
- `xe_page_reclaim.c` is the genuinely new part of the cluster

### 6. Display and test surfaces expand too

7.0 also changes the display and test areas materially:

- display subtree shows significant churn and new files
- test coverage grows with new KUnit / helper tests

This does not directly decide the FreeBSD baseline, but it confirms 7.0 is a
moving target, not a quiet stabilization point.

## Shared DRM / TTM Delta

Xe does not live on the kernel in isolation.
Even a quick shared-infrastructure check shows nontrivial movement outside
`drivers/gpu/drm/xe` between `6.12` and `7.0`:

- `drivers/gpu/drm/ttm`: `25 files changed`, `1892 insertions`, `568 deletions`
- `include/drm`: `98 files changed`, `5797 insertions`, `1441 deletions`
- `drivers/gpu/drm/scheduler/sched_main.c`: `346 insertions`, `195 deletions`
- `drivers/gpu/drm/drm_gpuvm.c`: `633 insertions`, `141 deletions`
- `include/drm/drm_gpuvm.h`: `86 insertions`, `29 deletions`

This is enough to make one point clearly:

- the strongest external rebase trigger is not only "new Xe hardware appears"
- it is also "FreeBSD's imported DRM core / TTM / GPUVM lane moves forward"

## What 7.0 Adds That Matters Most

Not every 7.0 addition should be treated as equally important for the
FreeBSD-first port.

### Tier 1: strongest practical watch points

- `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY`
  - dynamic queue-property updates are the most plausible near-term
    userspace-compatibility pressure point
  - if Mesa requires this ioctl for correct DG2/BMG operation, it becomes a
    mandatory backport candidate rather than a nice-to-have
- `DRM_IOCTL_XE_MADVISE`
  - this affects BO behavior under memory pressure and is closer to core
    memory-management expectations than to optional debug infrastructure
- `xe_pci_rebar.c`
  - resizeable-BAR support is a practical hardware concern that could matter
    to A380/B580 bring-up sooner than many of the larger 7.0 subsystems

### Tier 2: later-phase but real

- `xe_guc_pagefault.c`
- `xe_pagefault.c`
- `xe_page_reclaim.c`
- `DRM_XE_GPUSVM`
- `DRM_XE_PAGEMAP`
- `DRM_GPUSVM`
- `xe_pxp*`

These matter if later phases decide to chase:

- fault-mode VM
- broader GPU-SVM semantics
- protected-content support

Also note the split:

- `xe_guc_pagefault.c` and `xe_pagefault.c` are refactor/expansion of an area
  already present in `6.12`
- `xe_page_reclaim.c` is the more clearly new addition

### Tier 3: useful, but not baseline-shifting

- `xe_pmu*`
- `xe_eu_stall*`
- `xe_configfs.c`
- `xe_mert.c`
- `xe_soc_remapper.c`

These may matter for observability or platform plumbing, but they do not by
themselves justify replacing the baseline.

## What This Means for FreeBSD Porting

### Why 7.0 is attractive

If we looked only at feature growth, 7.0 has obvious appeal:

- larger UAPI
- newer platform coverage
- richer memory-model features
- more queue and submission features
- more modern protected-content and telemetry plumbing

If Phase 3 were defined as:

- "chase newer Xe userspace ABI"
- "support Panther Lake / Xe3-family"
- "bring up CPU address mirroring / SVM"
- "bring up PXP-sensitive userspace"

then 7.0 would be the more relevant semantic line.

### Why 7.0 is risky for this project

For the FreeBSD-first upstream path, 7.0 also makes the problem materially
harder:

- much larger VM delta
- much larger GuC submission delta
- more Linux-MM dependency
- more UAPI promises to either implement or explicitly reject
- more subsystem surface to stage, stub, or port honestly

This is exactly the wrong trade if the near-term project goal remains:

- upstreamable FreeBSD Xe on the 6.12 DRM lane
- A380 first
- DG2/BMG-era bring-up first
- stock FreeBSD first

In other words:

- 7.0 has real benefits
- 7.0 also raises the floor of required correctness significantly

### Why this is not purely a project-internal choice

The baseline decision is constrained by FreeBSD's own DRM lane.

If FreeBSD's LinuxKPI / drm-kmod infrastructure moves from `6.12` to a newer
baseline before the Xe port is complete, then the Xe port will be forced to
follow regardless of the project's preference.

So the correct reading is not:

- "we can stay on 6.12 forever if we want"

It is:

- "6.12 is the right baseline while FreeBSD remains 6.12-aligned and the first
  Xe bring-up is still in progress"
- and while `6.12.y` continues to receive the `DG2/BMG` correctness fixes that
  the first-port paths need

## xekmd-backports Is Not a Clean 7.0 Baseline

The local `xekmd-backports` checkout is not "Linux 7.0 plus DKMS packaging."

Its own metadata says:

- `BACKPORTS_RELEASE_TAG="xeb_v6.17.13.48_260409.3"`
- `BASE_KERNEL_TAG="xe-v6.17.13.48"`

So the current local mainline backport branch is based on a **6.17.13.48**
Xe line, not the 7.0 snapshot.

That matters a lot:

- it is ahead of 7.0 in some directions
- it is adapted for out-of-tree compatibility
- it is not a clean semantic baseline candidate for a FreeBSD in-tree port

### What backports adds

Compared with the Linux 7.0 Xe subtree, local backports adds compatibility or
preliminary material such as:

- many extra `compat-i915-headers/*`
- `prelim/xe_eudebug*`
- `prelim/xe_gt_debug*`
- older-style `xe_gt_pagefault.*`
- explicit `xe_hmm.*`
- some extra shared display/ext glue

This tells us the backport tree is willing to carry:

- compat scaffolding
- older API shapes
- preliminary / not-upstream-mainline pieces

### What backports does not carry from 7.0

Compared with the local 7.0 snapshot, local backports is missing a number of
7.0 files, including examples like:

- `xe_guc_pagefault.*`
- `xe_pagefault.*`
- `xe_page_reclaim.*`
- `xe_pci_rebar.*`
- `xe_vm_madvise.h`
- `xe_soc_remapper.*`
- `xe_mert.*`
- several newer ABI headers

That means the backports branch is not just "7.0 with extra compat."
It is its own curated compatibility tree.

### backports UAPI is also not identical to 7.0

The local UAPI diff shows backports dropping or changing several 7.0 surfaces,
including examples such as:

- no `DRM_IOCTL_XE_MADVISE`
- no `DRM_IOCTL_XE_VM_QUERY_MEM_RANGE_ATTRS`
- no `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY`
- no `NO_COMPRESSION` create flag
- no 7.0-style `MADVISE_AUTORESET`
- no 7.0 multi-group / multi-queue property surface

and at least one semantic substitution:

- bit position used by `MADVISE_AUTORESET` in 7.0 becomes `DECOMPRESS` in the
  backport tree

That is enough to reject it as the FreeBSD semantic target.

## Recommendation for Phase 3

### Recommended baseline

Keep **Linux 6.12** as the FreeBSD semantic baseline through Phase 3.

That recommendation holds if Phase 3 is still mainly about:

- stabilizing DG2 / A380
- keeping BMG practical
- bringing up FreeBSD-clean VM_BIND / BO / submission paths
- preserving upstreamability against the FreeBSD 6.12 DRM lane

### Do not move the whole port to 7.0 yet

Do **not** move the whole FreeBSD port target to 7.0 just because:

- the delta is large
- Ubuntu or other distros may move there
- Intel backports newer lines to LTS kernels

Those are useful signals, but not enough to justify replacing the baseline.

### What to do instead

Use 7.0 as a **tracked follow-on source**.

Specifically:

1. Keep 6.12 as the actual port baseline.
2. Maintain a 7.0 delta tracker for features that may matter later.
3. Promote only selected 7.0 items into the plan as explicit backports when a
   real need appears.
4. Use `xekmd-backports` as evidence of what Intel considers important enough
   to ship onto older/LTS kernels, not as a baseline candidate.

That does not mean "cheap cherry-picks."
The `6.12 -> 7.0` delta is large enough that selective backport requires
dependency analysis.
VM, GuC submission, and pagefault/reclaim changes are intertwined enough that
lifting one visible feature may require several neighboring supporting changes.
In the worst case, a desired `7.0` correctness fix in the VM / GuC-submit
cluster may prove closer to an all-or-nothing pull than to a clean isolated
backport.

## When 7.0 Would Become Worth Targeting

### Hardware-forced or infrastructure-forced rebasing

Move the whole semantic target forward if one of these becomes true:

- `Panther Lake` / NVL / Xe3-family hardware support
- FreeBSD's DRM lane itself, including imported DRM core / TTM / GPUVM pieces,
  moves beyond `6.12`
- `6.12.y` stable no longer carries critical `DG2/BMG` correctness fixes while
  newer lines do

These are baseline-shifting events.

### Feature-driven selective backport first

Before replacing the whole baseline, treat these as explicit backport
candidates:

- `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY`
- `DRM_IOCTL_XE_MADVISE`
- `xe_pci_rebar.c`
- specific DG2/BMG-relevant bug fixes from newer Xe lines

The most practical near-term watch point is Mesa.
If Mesa requires `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY` for correct DG2/BMG
operation, that ioctl becomes a mandatory compatibility item rather than an
optional future feature.

There is a second practical watch point:

- whether critical `DG2/BMG` GuC-submit / VM fixes are landing in `6.12.y` or
  only in newer lines

### Later-phase feature pressure

These may justify a broader rebaseline later, but not by themselves today:

- fault-mode VM
- CPU address mirroring / broader GPU-SVM semantics
- PXP / protected-content functionality
- newer telemetry surfaces that a later phase explicitly decides to preserve

## Practical Phase 3 Rule

The practical rule should be:

> Keep Linux 6.12 as the FreeBSD Xe baseline.
> Mine Linux 7.0 selectively.
> Use xekmd-backports only as compatibility evidence, not as a semantic target.

and add one condition:

> This rule holds only while FreeBSD's DRM lane remains 6.12-aligned.

That gives the project:

- the cleaner upstream story of 6.12
- visibility into where newer Xe is going
- the ability to cherry-pick justified newer features later
- less risk of turning Phase 3 into a moving-target re-port

## Suggested Follow-Up

The next useful follow-up would be a narrower tracker:

- `docs/xe-7.0-feature-watchlist.md`

with columns like:

- feature / subsystem
- first appears in 7.0?
- needed for Phase 3?
- needed for Phase 4+?
- blocked on FreeBSD LinuxKPI / VM / DRM core?
- candidate for selective backport?
- required by Mesa for correct DG2/BMG operation?

That would turn this broad comparison into an actionable decision queue.
