# Intel Xe Shared DRM 6.12 -> 7.0 Delta

Date: 2026-04-19

## Purpose

This memo is the shared-infrastructure companion to the Xe subtree comparison.

The Xe driver does not live in isolation.
Any decision about:

- staying on a `6.12` Xe baseline
- selectively pulling forward `7.0` Xe pieces
- or fully rebasing to a newer Xe line

also depends on the shared DRM layers under it.

This note focuses on the most Xe-relevant shared areas between the local
snapshot trees:

- `../nx/linux-6.12`
- `../nx/linux-7.0`

Primary focus:

- `drivers/gpu/drm/ttm`
- `drivers/gpu/drm/scheduler`
- `drivers/gpu/drm/drm_gpuvm.c`
- `include/drm`

Related planning docs:

- `docs/xe-612-700-delta.md`
- `docs/xe-6.12-vs-7.0-and-backports.md`
- `docs/xe-7.0-feature-watchlist.md`

## Short Conclusion

The `6.12 -> 7.0` delta is not just inside `drivers/gpu/drm/xe`.
There is nontrivial movement in shared DRM infrastructure that Xe depends on.

The practical implication is:

- evaluating Xe alone is insufficient
- the strongest external rebase trigger is movement in FreeBSD's imported DRM
  core / TTM / GPUVM lane
- selective `7.0` Xe backports are easiest when they stay mostly Xe-local
- selective `7.0` Xe backports become much riskier when they depend on newer
  `drm_gpuvm`, `TTM`, or scheduler assumptions

This strengthens the current planning rule:

> Keep `6.12` as the Xe semantic baseline while FreeBSD stays `6.12`-aligned.
> Treat shared DRM lane movement as a first-class rebase trigger.

## Quantitative Delta

Quick shared-infrastructure diff summary:

- `drivers/gpu/drm/ttm`
  - `25 files changed`
  - `1892 insertions`
  - `568 deletions`
- `include/drm`
  - `98 files changed`
  - `5797 insertions`
  - `1441 deletions`
- `drivers/gpu/drm/scheduler`
  - `11 files changed`
  - `1795 insertions`
  - `321 deletions`
- `drivers/gpu/drm/drm_gpuvm.c`
  - `633 insertions`
  - `141 deletions`
- `include/drm/drm_gpuvm.h`
  - `86 insertions`
  - `29 deletions`

This is already enough to reject any simplistic reading like:

- "Xe changed a lot, but the rest of DRM stayed roughly the same"

That is not true.

## What Actually Matters To Xe

Broad `include/drm` churn overstates the impact if read naively, because a lot
of it is display-facing and irrelevant to a render-first Xe port.

The Xe-relevant shared areas are narrower:

### 1. `drm_gpuvm`

This is one of the most important shared deltas.

Observed movement:

- `drivers/gpu/drm/drm_gpuvm.c`
  - `633 insertions`, `141 deletions`
- `include/drm/drm_gpuvm.h`
  - `86 insertions`, `29 deletions`

Why it matters:

- Xe VM state is built around `drm_gpuvm`
- VM bind behavior, range tracking, reservations, and locking assumptions flow
  through this layer
- if `7.0` Xe relies on newer `drm_gpuvm` semantics, selectively backporting
  Xe code onto a `6.12` shared DRM lane becomes much harder

Practical read:

- any `7.0` Xe pull touching VM bind, pagefault, reclaim, or validation logic
  must be checked against the `drm_gpuvm` dependency chain

### 2. `TTM`

Observed movement:

- `drivers/gpu/drm/ttm`
  - `25 files changed`
  - `1892 insertions`
  - `568 deletions`

Largest visible TTM churn includes:

- `ttm_pool.c`
  - `661 insertions`, `129 deletions`
- `ttm_bo_util.c`
  - `342 insertions`, `92 deletions`
- `ttm_bo.c`
  - `198 insertions`, `76 deletions`
- new `ttm_backup.c`
  - not present in local `6.12`
  - present in local `7.0`

Why it matters:

- Xe BO allocation, migration, shrink, mmap, and eviction behavior sits on TTM
- if a `7.0` Xe feature depends on newer TTM pool or backup semantics, it is
  no longer a clean Xe-local backport
- memory-pressure behavior is especially sensitive here

Practical read:

- `DRM_IOCTL_XE_MADVISE`
- shrink / reclaim work
- BAR / stolen / migration paths

all need TTM-awareness, not just Xe subtree awareness

### 3. DRM Scheduler Core

Observed movement:

- `drivers/gpu/drm/scheduler`
  - `11 files changed`
  - `1795 insertions`
  - `321 deletions`
- `sched_main.c`
  - `346 insertions`, `195 deletions`

There is also significant new scheduler test coverage in `7.0`.

Why it matters:

- Xe uses DRM scheduler concepts directly
- timeout handling, runqueue behavior, dependency readiness, and fence flow
  are shaped partly by scheduler core assumptions
- even if the Xe-specific scheduler wrapper is local, it is not independent of
  scheduler core behavior

Practical read:

- if a `7.0` Xe fix touches timeout, scheduling order, or recovery sequencing,
  check whether it assumes newer scheduler core semantics

### 4. `drm_exec`

Observed movement:

- `include/drm/drm_exec.h`
  - no material local delta detected in this check
- `drivers/gpu/drm/drm_exec.c`
  - comparatively small movement in earlier spot checks

Why this matters:

- it is useful as a contrast point
- not every shared DRM layer moved equally

Practical read:

- `drm_exec` currently looks much less likely than `drm_gpuvm` or `TTM` to be
  the thing that forces a rebase by itself

### 5. Shared Headers Most Relevant To Xe

Broad `include/drm` stats are noisy.
The Xe-relevant signals are narrower:

- `drm_gpuvm.h`
  - real movement
- `drm_gpusvm.h`
  - absent in local `6.12`
  - present in local `7.0`
- `drm_gem.h`
  - `104 insertions`, `40 deletions`
- `drm_managed.h`
  - `23 insertions`
- `drm_buddy.h`
  - `11 insertions`, `13 deletions`

Why it matters:

- this is the layer where a nominally "Xe feature" can quietly become a shared
  DRM baseline mismatch

## What This Means For The Port

### 1. Why `6.12` Still Makes Sense

The strongest argument for keeping `6.12` is not only that the Xe subtree is
smaller.
It is also that the shared DRM assumptions are more likely to match the
FreeBSD lane we are already aligning to.

That reduces risk in:

- VM bind
- BO / TTM behavior
- scheduler timeout and recovery behavior
- lifetime / managed-resource assumptions

### 2. Why Shared DRM Movement Is A Rebase Trigger

If FreeBSD's imported DRM lane moves beyond `6.12`, then keeping the Xe port on
`6.12` stops being the conservative choice.

At that point, the mismatch becomes:

- newer shared DRM substrate
- older Xe semantic target

and that can be worse than rebasing.

This is why:

- "FreeBSD DRM lane movement"

belongs in the `rebase-trigger` category in the watchlist.

### 3. What Can Still Be Backported Safely

The safest `7.0` pulls are the ones that stay mostly Xe-local.

Better backport candidates:

- `xe_pci_rebar.c`
- clearly isolated PCI/platform descriptor updates
- clearly isolated UAPI additions whose implementation does not drag in newer
  `drm_gpuvm` / `TTM` / scheduler behavior

Higher-risk backport candidates:

- anything touching VM bind internals
- pagefault / reclaim integration
- memory-pressure behavior
- timeout / scheduling / recovery logic tied to newer scheduler semantics

### 4. Bug Fixes Need Different Handling Than Features

This note reinforces a key distinction:

- new features can usually wait
- correctness fixes on already-targeted hardware cannot

So for `DG2/BMG`:

- monitor `6.12.y` first
- then inspect `7.0`
- then inspect `xekmd-backports`

If the fix lives only in newer lines and depends on shared DRM movement, the
project may need either:

- a deeper backport than planned, or
- a real baseline re-evaluation

## Shared DRM Watch Points

These are the shared layers that should be checked before accepting any major
`7.0` Xe pull:

1. `drm_gpuvm.c` and `drm_gpuvm.h`
2. `drivers/gpu/drm/ttm/*`
3. `drivers/gpu/drm/scheduler/*`
4. `drm_gpusvm.h` and related GPUSVM direction
5. `drm_gem.h` / `drm_managed.h` when a patch touches BO lifetime or managed
   cleanup assumptions

## Practical Rule

Use this rule when evaluating a prospective `7.0` pull:

1. Ask whether the change is:
   - a hardware-enablement update
   - a correctness fix
   - a new feature
2. Ask whether it stays mostly inside `drivers/gpu/drm/xe`.
3. If it touches `drm_gpuvm`, `TTM`, or scheduler assumptions, treat it as a
   shared-lane problem, not just a Xe problem.
4. If FreeBSD's DRM lane has already moved, reevaluate the whole baseline
   rather than pretending the Xe subtree can stay frozen independently.

## Current Default Conclusion

The default conclusion remains:

> `6.12` stays the right Xe baseline while FreeBSD stays `6.12`-aligned.
> Shared DRM lane movement is a first-class trigger for retargeting.
> `7.0` Xe pulls should be screened first for shared DRM / TTM / GPUVM /
> scheduler assumptions before they are treated as "just Xe changes."
