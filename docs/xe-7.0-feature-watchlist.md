# Intel Xe 7.0 Feature Watchlist

Date: 2026-04-19

## Purpose

This file turns the `6.12 -> 7.0` comparison into a decision queue.

It is for one practical question:

> Which `7.0` Xe deltas should force a full rebase, which are selective
> backport candidates, and which should stay deferred for the FreeBSD-first
> port?

This watchlist is based on the local snapshot trees:

- `../nx/linux-6.12`
- `../nx/linux-7.0`

and the planning memos:

- `docs/xe-612-700-delta.md`
- `docs/xe-6.12-vs-7.0-and-backports.md`

## Decision Labels

- `rebase-trigger`
  - if this becomes required, the whole Xe baseline likely needs to move
- `backport-candidate`
  - try to pull this forward onto the `6.12` baseline before considering a
    full rebase
- `stable-fix-tracking`
  - watch `6.12.y` and newer lines for correctness fixes on already-targeted
    hardware paths
- `defer`
  - real functionality, but intentionally out of scope for the first FreeBSD
    bring-up
- `ignore-for-now`
  - not important enough to drive baseline choice for the FreeBSD-first port

## Watchlist

| Item | 7.0 delta type | Why watch it | Current label | Promote when | FreeBSD dependency / risk | Mesa / userspace watch |
| --- | --- | --- | --- | --- | --- | --- |
| `Panther Lake` / Xe3 PCI IDs and IP descriptors | new hardware coverage | `6.12` has zero `Panther Lake` mentions; `7.0` adds `PLATFORM(PANTHERLAKE)` and broader Xe3 IP description work | `rebase-trigger` | PTL or other post-BMG/Xe3 hardware becomes a real target | Requires newer Xe platform tables, descriptors, and likely newer supporting assumptions | none yet; this is hardware-driven |
| FreeBSD DRM lane movement: DRM core / TTM / GPUVM / scheduler | external shared-infra movement | strongest external trigger; Xe does not live in isolation | `rebase-trigger` | FreeBSD imported DRM lane moves beyond `6.12` | Version mismatch between Xe code and imported DRM core / TTM / GPUVM becomes worse than rebasing | indirect only |
| `DG2/BMG` correctness fixes in `xe_vm.c` / `xe_guc_submit.c` | mixed: bug fixes plus feature growth | some of the `7.0` growth is likely hardening for hardware already covered by `6.12` | `stable-fix-tracking` | `6.12.y` stops receiving needed fixes, or newer lines fix real bugs on A380/BMG first-port paths | may be hard to split from neighboring VM / GuC-submit changes; can become near all-or-nothing pulls | none directly, but bugs can break ordinary userspace behavior |
| `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY` | new UAPI | most plausible near-term userspace compatibility pressure point | `backport-candidate` | Mesa or other userspace requires dynamic queue-property changes for correct `DG2/BMG` operation | may depend on broader queue-property plumbing in submit / exec paths | `high`: explicitly track whether Mesa requires it |
| `DRM_IOCTL_XE_MADVISE` | new UAPI | affects BO discard and memory-pressure policy | `backport-candidate` | memory-pressure behavior becomes an observable compatibility gap | ties into BO policy and possibly VM / reclaim behavior | `medium`: check whether userspace starts depending on it |
| `xe_pci_rebar.c` | new Xe support file | practical hardware concern for A380/B580 BAR aperture sizing | `backport-candidate` | lab hardware shows resizeable-BAR / aperture sizing problems | depends on FreeBSD PCI / BAR behavior, but more isolated than SVM work | none directly |
| `xe_page_reclaim.c` | genuinely new pagefault-adjacent infrastructure | matters only if later phases want more advanced fault / reclaim behavior | `defer` | fault-mode VM or reclaim-heavy behavior becomes a real requirement | touches VM, memory pressure, and recovery semantics | low today |
| `xe_pagefault.c` + `xe_guc_pagefault.c` | refactor / split of existing pagefault area | `6.12` already has `xe_gt_pagefault.c`; `7.0` reorganizes and expands the area | `defer` | later phases need a cleaner / fuller pagefault story than the first port | tied to VM fault handling and GuC interactions | low today |
| `DRM_XE_GPUSVM` / `DRM_XE_PAGEMAP` / `DRM_GPUSVM` | new Kconfig / Linux-MM direction | explicit move into `ZONE_DEVICE`, device-private memory, and CPU-address-mirror territory | `defer` | project explicitly decides to pursue SVM or CPU-address-mirror semantics | high Linux-MM dependence; poor FreeBSD substrate match | none for early graphics bring-up |
| CPU-address-mirror VM bind and related capability flags | new ABI / memory model | major semantic expansion beyond the first honest port | `defer` | compute-oriented later phase requires it | deep MM and VM semantics; not a first-port problem | low today |
| `DRM_IOCTL_XE_VM_QUERY_MEM_RANGE_ATTRS` | new UAPI | paired with newer memory-policy features | `defer` | memory-range attribute semantics become required | depends on broader memory model growth | low today |
| `xe_pxp*` / protected content | new subsystem | important for DRM/content-protection use cases, not first-port bring-up | `defer` | protected-content support becomes a declared goal | significant security / firmware / policy surface | low for current scope |
| `xe_pmu*` | new telemetry | useful for profiling and introspection, not load-bearing | `ignore-for-now` | later performance / observability work demands it | extra debug surface, not core bring-up | none |
| `xe_eu_stall*` | new telemetry | useful for performance analysis, not required for first success | `ignore-for-now` | later profiling work needs it | debug / profiling only | none |
| `xe_guc_capture.c` / `xe_hw_error.c` | new debug / error infrastructure | can improve failure analysis, but does not decide baseline | `ignore-for-now` | debugging pain becomes high enough to justify extra error-capture work | useful, but not a bring-up gate | none |
| `xe_configfs.c` | Linux configfs integration | not meaningful for FreeBSD-first porting | `ignore-for-now` | only if a FreeBSD equivalent policy surface is later desired | FreeBSD does not implement Linux ConfigFS | none |
| `xe_soc_remapper.c` / `xe_mert.c` | platform-specific SoC plumbing | likely not central to discrete-GPU-first FreeBSD bring-up | `ignore-for-now` | a specific platform proves to depend on it | platform-specific, low general value today | none |
| SR-IOV PF / VF expansion | expanded virtualization support | real functionality, but not for the first desktop/workstation bring-up | `defer` | virtualization becomes an explicit later milestone | large scope increase and test burden | none for current scope |

## Priority Queue

If only a small amount of follow-up work can be done, use this order:

1. Track FreeBSD DRM lane movement.
2. Track `DG2/BMG` correctness fixes in `xe_vm.c` and `xe_guc_submit.c`.
3. Verify whether Mesa requires `DRM_IOCTL_XE_EXEC_QUEUE_SET_PROPERTY`.
4. Keep `xe_pci_rebar.c` on watch for A380/B580 lab issues.
5. Leave GPUSVM / pagemap / CPU-address-mirror work deferred unless the
   project scope changes.

## Watchlist Maintenance Rules

- Update the `Mesa / userspace watch` column whenever Mesa's minimum Xe kernel
  assumptions become clearer.
- Update `FreeBSD DRM lane movement` whenever the local drm-kmod / LinuxKPI
  target lane changes.
- When a bug is found on `DG2/BMG`, check first whether it is already fixed in:
  - `6.12.y`
  - `7.0`
  - `xekmd-backports`
- Do not convert a `backport-candidate` into a full rebase trigger unless:
  - the dependency chain is too entangled to pull forward cleanly, or
  - FreeBSD's imported DRM lane has already moved forward

## Current Default Rule

The default rule remains:

> Keep `6.12` as the FreeBSD Xe semantic baseline.
> Treat `7.0` as the tracked delta line.
> Promote items from this watchlist only when a real trigger appears.
