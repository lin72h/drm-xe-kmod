# Opus / GLM Review Prompt: R32 HWS, S32 GuC Submit, Comparison, Phase 1.5

Use this as a copy-paste prompt for Opus or GLM.

```text
You are reviewing a long-term FreeBSD-first Linux-6.12-based Intel Xe porting
project.

Act as a skeptical senior Linux DRM / FreeBSD DRM reviewer.
I want technical criticism, not encouragement.

Scope of review:

1. `docs/r32-hws.md`
   - AMD's official RDNA3+ Micro Engine Scheduler specification converted to
     markdown
2. `docs/s32-guc-submit.md`
   - a reverse-engineered working note for the Intel Xe GuC submission backend
     and host-visible scheduling contract in Linux 6.12 source
3. `docs/s32-vs-r32-hws.md`
   - a comparison memo describing similarities and differences between the AMD
     and Xe scheduler models
4. a roadmap addition called Phase 1.5
   - intended to move from an A380-secondary-only host layout to a real
     dual-Xe lab layout

Context and project boundary:

- This is a FreeBSD-first, upstream-oriented Xe port.
- Linux 6.12 is the semantic baseline.
- Stock FreeBSD is the primary target.
- Phase 1.0 OS compatibility is secondary and achieved only by staying
  FreeBSD-clean.
- This is not a DMO/DMI or Mach-native GPU architecture project.
- We are trying to understand Xe honestly enough to document and port it,
  without pretending Intel published an AMD-MES-style scheduler spec.

Important constraint:

Do not answer as if Intel has a public MES-equivalent scheduler spec.
The point of this review is exactly to test where the AMD analogy is useful and
where it becomes misleading.

What I need from you:

- red-team the accuracy and framing of the `s32-guc-submit` note
- red-team the new `s32-vs-r32-hws` comparison memo
- red-team the scope and sequencing of the new Phase 1.5 roadmap step
- identify anything overclaimed, under-evidenced, badly named, too
  AMD-shaped, or likely to mislead future implementation work

What `r32-hws` contains:

The AMD note is an official scheduler specification with:

- scheduling requirements
- hardware architecture overview
- scheduler firmware architecture
- queue state transitions
- round-robin and priority behavior
- a public MES API surface including:
  - SET_HW_RSRC
  - ADD_QUEUE / REMOVE_QUEUE
  - SET_SCHEDULING_CONFIG
  - SUSPEND / RESUME / RESET
  - QUERY_SCHEDULER_STATUS
  - PROGRAM_GDS
  - SET_DEBUG_VMID
  - UPDATE_ROOT_PAGE_TABLE
  - SET_SE_MODE
  - SET_GANG_SUBMIT
  - MISC register / wait / invalidate operations
- explicit scheduler logging and history structures

That means `r32-hws` is not just "driver source interpretation."
It is a published firmware scheduler contract.

Current `s32-guc-submit` thesis:

The Xe note argues that the honest Xe equivalent to an AMD-style public
scheduler spec is not a public firmware packet API, but a layered
host/firmware contract:

1. userspace creates `exec_queue` objects and submits batch addresses
2. the host driver builds `xe_sched_job` objects and tracks dependencies,
   fences, and VM state
3. the host-side DRM scheduler tracks dependency readiness and run-queue order,
   while a separate scheduler-message path serializes backend control
   operations
4. the GuC submission backend registers contexts with the GuC, updates
   per-context scheduling policy, and kicks execution
5. the GuC performs actual context scheduling, emits completion or reset
   notifications, and owns opaque fairness details

The note therefore frames `s32-guc-submit` as:

- a host-visible execution queue model
- a GuC context registration and control model
- a per-context policy model
- a job / fence / reset lifecycle model

and explicitly not as:

- a direct clone of AMD MES process/gang/resource APIs
- a complete firmware scheduler database
- a silicon contract

Source-derived claims behind `s32-guc-submit`:

1. User-visible scheduler surface in Linux 6.12 centers on:
   - `DRM_IOCTL_XE_EXEC_QUEUE_CREATE`
   - `DRM_IOCTL_XE_EXEC_QUEUE_DESTROY`
   - `DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY`
   - `DRM_IOCTL_XE_EXEC`
   - `DRM_IOCTL_XE_VM_BIND`
   - `struct drm_xe_sync`

2. `struct xe_exec_queue` is the main host scheduling object:
   - VM-bound
   - engine-class / placement bound
   - single-LRC or parallel width
   - has scheduling properties
   - has backend-specific GuC state

3. Linux 6.12 host scheduler layer uses:
   - `struct xe_gpu_scheduler`
   - DRM scheduler
   - `struct xe_sched_job`
   - scheduler messages for backend state changes

4. GuC backend state is visible in `struct xe_guc_exec_queue`:
   - scheduler wrapper
   - entity
   - static messages
   - atomic queue state bits
   - GuC ID
   - suspend waitqueue
   - parallel workqueue indices

5. Visible GuC-facing actions used by the driver include:
   - `XE_GUC_ACTION_REGISTER_CONTEXT`
   - `XE_GUC_ACTION_REGISTER_CONTEXT_MULTI_LRC`
   - `XE_GUC_ACTION_DEREGISTER_CONTEXT`
   - `XE_GUC_ACTION_SCHED_CONTEXT_MODE_SET`
   - `XE_GUC_ACTION_SCHED_CONTEXT`
   - `XE_GUC_ACTION_HOST2GUC_UPDATE_CONTEXT_POLICIES`

6. Visible policy KLVs used by the driver include:
   - execution quantum
   - preemption timeout
   - scheduling priority
   - preempt-to-idle on quantum expiry

7. Visible host scheduling properties include:
   - priority
   - timeslice
   - internally also preempt timeout and job timeout

8. Visible host priority model:
   - low
   - normal
   - high
   - kernel

9. Linux 6.12 maps host queue priority to GuC priority buckets in
   `xe_guc_submit.c`, rather than exposing a public process/gang band model
   like AMD MES.

10. Parallel execution in Xe is not just "more rings":
    - it uses `guc_submit_parallel_scratch`
    - `guc_sched_wq_desc`
    - `WQ_TYPE_MULTI_LRC`
    - a firmware-visible shared workqueue descriptor and item format
    - the CT kick is a notification; the ring-tail state is already visible in
      memory

11. Queue lifecycle visible from host code is approximately:
    - allocated
    - registered
    - enabled
    - submitted / kicked
    - suspended / resumed
    - disabled
    - deregistered
    - destroyed
    with failure states such as reset, killed, wedged, banned

12. Long-running queues are a distinct scheduling mode:
    - `XE_EXEC_QUEUE_SCHEDULE_MODE_LONG_RUNNING`
    - they should not be collapsed into the same timeout story as ordinary
      queues

13. Timeout / reset handling is host-driven with GuC notifications:
    - scheduler timeout path stops submission
    - may disable scheduling to sample state
    - may re-enable if timeout was false
    - otherwise bans queue, signals errors, and may trigger GT reset
    - G2H / reset / memory-cat notifications feed back into this

14. The note explicitly says that several things are still opaque and should
    not be overclaimed:
    - GuC internal fairness algorithm
    - process/gang-like internal scheduler objects
    - global resource partition policy
    - exact starvation / carryover behavior

Current `s32-vs-r32-hws` thesis:

The comparison memo currently argues:

- the strongest similarity is architectural:
  - both are firmware-mediated GPU scheduling systems
  - both rely on a host driver to create schedulable objects
  - both expose priority, quantum, suspend/resume, and reset in some form
  - both depend on firmware control plus host-side fence and recovery logic

- the strongest difference is disclosure and control shape:
  - `r32-hws` is a published scheduler firmware contract with process, gang,
    queue, resource, and reset APIs
  - `s32-guc-submit` is only a host-visible Xe/GuC contract with exec queues, jobs,
    LRCs, GuC registration, policy update, submit, timeout, and reset flow

- `r32-hws` looks like a public firmware scheduler operating system
- `s32-guc-submit` looks like a host-driven execution queue model that hands real
  arbitration to opaque GuC firmware

- `r32-hws` explicitly documents:
  - process/gang/queue/resource model
  - queue-state transitions
  - oversubscription behavior
  - priority bands
  - reset and logging APIs

- `s32-guc-submit` only honestly documents:
  - `exec_queue`
  - LRCs
  - jobs
  - GuC IDs
  - GuC registration / enable / submit / disable / deregister
  - policy updates
  - host timeout / reset / fence lifecycle

Current concern:

I want to know whether the comparison memo is technically fair, or whether it
still smuggles in too much AMD vocabulary and accidentally implies a stronger
Xe scheduler analogy than Linux 6.12 source really supports.

I also want to know whether the similarities are stated at the right level.
For example:

- Is "firmware-mediated GPU scheduling" a valid common abstraction?
- Is "host creates schedulable objects with priority and reset control" the
  right common denominator?
- Or is the comparison still too loose to be useful?

Current naming concern:

I want to know whether the local codename `s32-hws` is still too strong even
after renaming the actual note to `s32-guc-submit.md`, and whether that
codename risks misleading future work into thinking Xe has a public,
AMD-MES-like "hardware scheduler spec" shape when Linux 6.12 source really
only justifies a host/GuC contract memo.

If the name is too strong, suggest a better one.

Phase 1.5 roadmap addition:

The roadmap currently has:

- Phase 1 / first hardware milestone:
  - A380 first
  - Alder Lake iGPU remains on `i915`
  - Xe brought up as secondary GPU
  - no immediate display takeover

The proposed new Phase 1.5 says:

- after the first A380 milestone is stable, move to a lab machine that
  actually has two Xe-managed devices and keep the external A380 installed
- use this as the first dual-Xe host topology
- prove:
  - both Xe devices enumerate and attach in one boot
  - per-device firmware, GuC, CT, GT, and IRQ state remain isolated
  - DRM minors and render nodes are stable for both devices
  - attach failure / unload / reprobe on one Xe device does not corrupt the
    other
  - PCI topology, BAR sizing, and IOMMU behavior remain understood
- explicitly do not let Phase 1.5 silently become:
  - first display enablement
  - first host-console takeover
  - first userptr / HMM milestone
  - first B580 / Battlemage milestone

Current concern:

I want to know whether this Phase 1.5 addition is the right place in the
roadmap, or whether it risks introducing a second topology too early before the
single-device A380 path is truly stable.

Known source anchors behind the `s32-guc-submit` claims:

- `drivers/gpu/drm/xe/xe_exec_queue.c`
- `drivers/gpu/drm/xe/xe_exec_queue_types.h`
- `drivers/gpu/drm/xe/xe_exec.c`
- `drivers/gpu/drm/xe/xe_sched_job.c`
- `drivers/gpu/drm/xe/xe_gpu_scheduler.c`
- `drivers/gpu/drm/xe/xe_gpu_scheduler_types.h`
- `drivers/gpu/drm/xe/xe_guc_submit.c`
- `drivers/gpu/drm/xe/xe_guc_submit_types.h`
- `drivers/gpu/drm/xe/xe_guc_exec_queue_types.h`
- `drivers/gpu/drm/xe/abi/guc_actions_abi.h`
- `drivers/gpu/drm/xe/abi/guc_klvs_abi.h`
- `include/uapi/drm/xe_drm.h`

What I want from you specifically:

1. Is `s32-guc-submit` the right note name, and is the local codename
   `s32-hws` still too suggestive of a public firmware architecture/spec that
   the source does not actually expose?
2. Is the layered Xe model above accurate, or is it still too AMD/MES-shaped in
   its framing?
3. Which `s32-guc-submit` claims are sound and well supported by Linux 6.12
   source?
4. Which `s32-guc-submit` claims are weak, ambiguous, or overconfident?
5. Which parts of `s32-vs-r32-hws` are technically fair and useful?
6. Which parts of `s32-vs-r32-hws` are too loose, too analogy-driven, or too
   suggestive of a stronger equivalence than the source supports?
7. What important Xe scheduler concepts visible in Linux 6.12 are still
   missing from the note and the comparison?
8. Are there hidden scheduler objects or abstractions in Xe that this note is
   flattening too aggressively?
9. Is the distinction between host scheduler responsibility and GuC scheduler
   responsibility stated correctly?
10. Is the note right to emphasize exec_queue / job / fence / reset lifecycle
    instead of process/gang APIs?
11. Is the parallel-submission description accurate enough, or is it omitting a
    crucial detail that changes how `s32-guc-submit` should be framed?
12. Is the reset / timeout model described too host-centric, or does the source
    support that emphasis?
13. Does the note properly avoid claiming HMM / page-fault / userptr semantics
    that belong elsewhere?
14. What terminology would you change so future work is less likely to confuse
    this with AMD MES or a native silicon scheduler contract?
15. Is Phase 1.5 as described the right next roadmap layer after the first
    A380 milestone?
16. What additional guardrails should be added so Phase 1.5 does not quietly
    become "turn on display and hope"?
17. What dual-Xe failure modes should be named explicitly in Phase 1.5?
18. What evidence should be required before saying Phase 1.5 is reached?
19. If you were rewriting these docs, how would you restructure `s32-guc-submit`,
    `s32-vs-r32-hws`, and the Phase 1.5 roadmap so they are more technically
    defensible?

Desired output format:

1. Executive judgment
2. Naming and framing corrections for `s32-guc-submit`
3. Review of `s32-vs-r32-hws`
4. Claims that are well supported by source
5. Claims that are weak or overclaimed
6. Missing scheduler concepts or source angles
7. Host vs GuC responsibility corrections
8. Phase 1.5 roadmap review
9. Specific terminology or structure edits
10. Concrete next edits before these docs are committed
```
