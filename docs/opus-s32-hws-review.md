# S32 HWS, S32-vs-R32 HWS, and Phase 1.5 Review

Date: 2026-04-19

## 1. Executive Judgment

`s32-hws.md` is technically sound and unusually honest for a reverse-engineered
document. It correctly avoids claiming what the source does not actually show.
The comparison memo `s32-vs-r32-hws.md` is fair and useful but has framing
problems that need correction.

The main risk across all three documents is a naming and analogy problem, not a
factual one. The name "HWS" and the structural parallel with `r32-hws` creates
an implicit expectation that Xe has a comparable public firmware scheduler
specification. It does not, and the documents mostly say so — but the framing
still invites the wrong reading.

Phase 1.5 (dual-Xe topology) is correctly positioned but needs explicit
entrance criteria and a few more named failure modes.

## 2. Naming and Framing Corrections for `s32-hws`

### The name `s32-hws` is too strong

"HWS" stands for "Hardware Scheduler" in the AMD context (`r32-hws` is a
MES specification). In the Intel/Xe context, the GuC is a firmware scheduler,
but the document does not describe the hardware scheduler — it describes the
host driver's interface to GuC.

The document itself says this clearly in the opening paragraphs. But the
filename and codename override that caveat. Someone scanning the docs
directory will see `r32-hws.md` and `s32-hws.md` side by side and expect
comparable scope and authority.

Recommended rename:

- `s32-hws.md` → `s32-xesc.md` (Xe Scheduling Contract)
- or → `s32-guc-sched.md` (GuC Scheduling Working Note)
- or → `s32-exec-model.md` (Xe Execution Model Working Note)

Any of these correctly signal "this is a working note about the Xe scheduling
contract as visible from host source" rather than "this is a hardware scheduler
specification."

If the name must stay `s32-hws` for consistency, add a subtitle:

```
# S32 HWS: Xe Host/GuC Scheduling Contract Working Note
```

and make the first sentence: "This is not a hardware scheduler specification."

### The five-layer model is accurate but AMD-shaped in presentation

The five layers (userspace → host queue object → host scheduler → GuC
backend → GuC firmware) are factually correct. But presenting them as a
numbered layer stack invites comparison with a firmware architecture document.

The Xe scheduling model is better described as **two actors and a protocol**:

- **Actor 1: host driver** — owns exec_queue lifecycle, job objects, fence
  tracking, dependency resolution, timeout detection, and reset decisions.
- **Actor 2: GuC firmware** — owns actual context scheduling, GPU time
  allocation, and completion/error notification.
- **Protocol: CT-based actions and KLVs** — register, enable, submit, disable,
  deregister, policy update.

This framing more accurately reflects the asymmetry: the host does most of the
visible work, and GuC is a delegated scheduler whose internals are opaque.

### "Reverse-engineered" is the right word but needs a stronger caveat

The document uses "reverse-engineered" which is correct. But it should also
state that:

- The GuC ABI headers in the Linux kernel are published by Intel
- The `guc_actions_abi.h` and `guc_klvs_abi.h` definitions are authored by
  Intel engineers, not reverse-engineered from binary
- What IS reverse-engineered is the behavioral model: how the host driver
  uses those ABI headers, what state transitions occur, and what the combined
  behavior implies

So the correct framing is:

> The ABI is published. The behavioral contract is inferred from driver source.

## 3. Review of `s32-vs-r32-hws`

### What is technically fair and useful

The comparison memo correctly identifies the core asymmetry:

- `r32-hws` is a published firmware scheduler specification
- `s32-hws` is a host-visible contract inferred from driver source

This is the most important thing the comparison does, and it does it well.

The "Major Similarities" section (firmware in the loop, host creates schedulable
objects, priority/quantum exist, suspend/resume/reset are first-class) is
accurate at the right level of abstraction.

The "Major Differences" table is accurate and useful.

### What is too loose or too analogy-driven

**Problem 1: "firmware-mediated GPU scheduling system" is stated as a common
category, but the firmware roles are materially different.**

In `r32`, MES firmware IS the scheduler. It owns queue connection, priority
arbitration, oversubscription, preemption decisions, and resource assignment. The
host driver tells MES "add this queue" and MES decides when it runs.

In `s32`, GuC is the delegated scheduler but the host driver retains much more
scheduling control. The host driver decides when to enable/disable scheduling,
handles timeout detection, makes reset decisions, and manages queue ban policy.
GuC owns the actual GPU time-slice arbitration, but the host is more involved
in policy than AMD's MES model.

The comparison should note this: **both are firmware-mediated, but the
host/firmware responsibility split is different. AMD MES is closer to a
self-contained scheduler OS. Intel GuC is closer to a delegated context
switcher with host-driven policy.**

**Problem 2: The object model comparison table implies structural equivalence
that does not exist.**

The comparison says `r32` has process/gang/queue/HW-queue and `s32` has
exec_queue/LRC/job/GuC-ID, which could be read as "four objects map to four
objects." They do not:

- AMD's process and gang are grouping/isolation objects with no Xe equivalent
  visible in Linux 6.12 source
- AMD's HW-queue connection/disconnection model has no visible Xe equivalent
- Xe's exec_queue is closer to AMD's "queue" but without the process/gang
  context above it

The comparison should explicitly say: **Xe has no visible process or gang
abstraction. The grouping level above exec_queue is the VM, which is a memory
isolation container, not a scheduler group.**

**Problem 3: The resource programming section understates the difference.**

The comparison notes that `s32` lacks public equivalents of SET_HW_RSRC,
PROGRAM_GDS, SET_DEBUG_VMID, UPDATE_ROOT_PAGE_TABLE. But it frames this as
"the Linux-visible model does not publish them in the same way," which is too
soft.

The stronger statement is: **Xe's GuC scheduling contract is deliberately
narrower than AMD's MES API. Intel chose to keep resource management (VMID
allocation, page-table configuration) in the host driver rather than exposing
it through the firmware scheduler API. This is a design choice, not a
documentation gap.**

### Missing comparison point

The comparison does not mention **long-running (LR) mode**, which is a
significant scheduling concept in Xe Linux 6.12.

`xe_vm_in_lr_mode()` (`xe_vm.h:192`) controls a fundamentally different
execution path:

- LR mode uses `MAX_SCHEDULE_TIMEOUT` (`xe_guc_submit.c:1413`) instead of a
  normal job timeout
- LR mode changes exec behavior: no implicit sync, no BO-list rebind
  (`xe_exec.c:164, 197, 221, 242, 273, etc.`)
- LR mode is the basis for compute workloads

This is an important scheduling concept that has no direct AMD MES equivalent
in `r32-hws`. AMD handles long-running compute differently (through MES suspend
/ resume). The comparison should include this.

The comparison also does not mention **preempt-fence mode**
(`xe_vm_in_preempt_fence_mode`, `xe_vm.h:197`), which is LR mode without
fault mode. In preempt-fence mode, compute exec queues are tracked on the VM
and participate in preempt-fence rebind flows. This is host-driven scheduling
infrastructure with no AMD MES analogue.

## 4. Claims That Are Well Supported by Source

These claims in `s32-hws.md` are correct and backed by verified source:

| Claim | Evidence |
| --- | --- |
| Visible GuC actions include REGISTER/DEREGISTER/SCHED/MODE_SET/UPDATE_POLICIES | `guc_actions_abi.h:120-138` |
| Priority mapping: LOW→NORMAL, NORMAL→KMD_NORMAL, HIGH→HIGH, KERNEL→KMD_HIGH | `xe_guc_submit.c:405-408` |
| GuC priority has 4 levels: KMD_HIGH(0), HIGH(1), KMD_NORMAL(2), NORMAL(3) | `xe_guc_fwif.h:22-26` |
| Queue state bits include REGISTERED, ENABLED, PENDING_ENABLE/DISABLE, DESTROYED, SUSPENDED, RESET, KILLED, WEDGED, BANNED, CHECK_TIMEOUT, EXTRA_REF | `xe_guc_submit.c:57-68` |
| Parallel submission uses `guc_submit_parallel_scratch` with WQ_TYPE_MULTI_LRC | `xe_guc_submit.c:442-647` |
| Policy updates use KLVs for execution quantum, preemption timeout, scheduling priority | `guc_klvs_abi.h` |
| CT control toggle is the first firmware exchange | `xe_guc_ct.c:313-327` |
| Host timeout path can disable scheduling, sample, re-enable, or ban | `xe_guc_submit.c` timeout handler |

## 5. Claims That Are Weak or Overclaimed

### "The closest Xe equivalent to an AMD-style scheduler spec"

This phrasing is slightly misleading. It implies that `s32-hws` occupies the
same conceptual slot as an AMD spec, just at lower resolution. The more accurate
framing is: **there is no Xe equivalent to an AMD-style scheduler spec. What
exists is a host/GuC contract visible in driver source.**

### "GuC performs actual context scheduling, emits completion or reset notifications, and owns the opaque fairness details"

"Opaque fairness details" is vague. More precisely: GuC owns context-level
time-slice arbitration among registered contexts. The host can influence this
through priority KLVs and execution quantum, but cannot override GuC's internal
ordering. The word "fairness" is AMD vocabulary that may not match GuC's
actual scheduling model.

### Layer 5 ("GuC firmware") is underspecified

The note says GuC "exposes action codes and KLV-based policy updates" but
does not list what comes BACK from GuC. The G2H notification side is
equally important:

- `XE_GUC_ACTION_SCHED_CONTEXT_MODE_DONE` (scheduling mode change complete)
- `XE_GUC_ACTION_DEREGISTER_CONTEXT_DONE` (deregister complete)
- Context reset notification
- Engine memory CAT error notification

The host/GuC protocol is bidirectional. The note documents H2G well but
shortchanges G2H.

## 6. Missing Scheduler Concepts

### Long-running mode

As noted above, `xe_vm_in_lr_mode()` fundamentally changes the scheduling
model. LR queues get `MAX_SCHEDULE_TIMEOUT`, skip normal BO-list rebind, and
follow different exec semantics. This is not a minor mode flag — it determines
whether the host scheduler ever times out the job.

### Preempt-fence mode and compute queues

`xe_vm_in_preempt_fence_mode()` (`xe_vm.h:197-200`) defines a scheduling mode
where LR compute queues are tracked on the VM and participate in preempt-fence
rebind. This is how Xe handles compute workloads that must be preemptable for
VM rebind without using the normal DRM scheduler timeout path.

`xe_vm_add_compute_exec_queue()` (`xe_vm.c:220`) explicitly manages this list.

### VM-bind as a scheduling concept

The `s32-hws` note lists `DRM_IOCTL_XE_VM_BIND` in the user-visible surface
but does not explain its scheduling significance. VM_BIND operations go through
the scheduler (on a special kernel bind exec_queue) and have fence
dependencies. They are scheduled work, not just address-space management.

### GuC ID pressure

The note mentions GuC IDs but does not explain the pressure model. GuC IDs
are a scarce resource (managed by `xe_guc_id_mgr.c`). When IDs are exhausted,
queue registration must wait or steal. This is a scheduling constraint not
mentioned in the note.

### Min run period before suspend

The note mentions "suspend may wait for a minimum run period" but does not
explain why. The GuC may need a minimum execution window after enable before
the host can safely disable scheduling again. This is a firmware behavioral
requirement that affects suspend/resume timing.

## 7. Host vs GuC Responsibility Corrections

The note's split is mostly correct but understates the host's role in two
areas:

**Timeout detection is entirely host-driven.** The note says "timeout handling
is host-driven, with GuC participation" but it is stronger than that: GuC does
NOT have a visible timeout mechanism exposed to the host. The host runs the DRM
scheduler timeout, samples hardware state, and decides whether to ban/reset.
GuC reports context resets and CAT errors, but the timeout clock is on the
host side.

**Reset policy is host-decided.** The note correctly describes the flow but
should emphasize: the host decides whether a timeout is a false alarm (re-enable)
or a real hang (ban + reset). GuC does not autonomously ban or wedge queues.
This is a significant difference from AMD MES, where the firmware specification
documents its own hang-detection and reset behavior.

## 8. Phase 1.5 Roadmap Review

### Position is correct

Phase 1.5 (dual-Xe topology on a newer Alder Lake machine with internal
Arc/Xe GPU plus external A380) is correctly positioned after the single-device
A380 milestone is stable. The motivation is sound: multi-device isolation is a
real porting concern that is best tested deliberately rather than discovered in
the field.

### Entrance criteria needed

Phase 1.5 should not start until these Phase 1 criteria are met:

1. A380 probe, firmware load, CT, and `drm_dev_register` all work
2. Load/unload cycle (50x) completes without panic or resource leak
3. Basic BO allocation and free works
4. At least one BO-backed VM_BIND succeeds
5. At least one GuC submission completes and fence signals

Without these, Phase 1.5 is premature and will produce confusing failures that
could be single-device bugs rather than multi-device isolation problems.

### Missing failure modes

The Phase 1.5 scope mentions per-device isolation but should explicitly name:

1. **GuC ID namespace collision** — are GuC IDs per-device or global? If
   per-device (they should be), verify isolation.
2. **Firmware loading for the second device** — does the second Xe device
   load its own firmware image, or share the cached copy? Different steppings
   may need different firmware.
3. **GGTT isolation** — each device must have its own GGTT. Verify no
   cross-device GGTT address confusion.
4. **IRQ routing** — two Xe devices mean two MSI-X allocations. Verify
   interrupts route to the correct device handler.
5. **TTM device isolation** — each device must have its own TTM device with
   independent memory pools and eviction. Verify no shared-state corruption.
6. **DRM minor allocation** — two devices mean two DRM minors and two render
   nodes. Verify `/dev/dri/` nodes are stable and correctly associated.

### Phase 1.5 should NOT include

The document correctly excludes display, B580, userptr, and HMM from
Phase 1.5. Add one more explicit exclusion:

- **Do not attempt cross-device BO sharing or dma-buf export/import between
  the two Xe devices in Phase 1.5.** That is a Phase 2+ concern.

### Evidence for Phase 1.5 completion

Phase 1.5 should be considered complete when:

1. Both Xe devices reach `drm_dev_register` in a single boot
2. Independent load/unload of one device does not affect the other
3. Independent BO allocation and VM_BIND work on each device
4. Independent GuC CT communication works on each device
5. No WITNESS violations from multi-device operation
6. PCI topology and IOMMU groups are documented

## 9. Specific Terminology and Structure Edits

### `s32-hws.md`

1. **Rename or add subtitle** as discussed in section 2.
2. **Add G2H notifications** to the GuC Control Interface section. Currently
   only H2G actions are listed.
3. **Add long-running mode** as a section under Scheduling Properties or as a
   new section. It fundamentally changes timeout and exec behavior.
4. **Add preempt-fence mode** as a subsection. It is how compute workloads
   interact with VM rebind.
5. **Add GuC ID pressure** as a subsection under Scheduler Objects.
6. **Change "fairness" to "arbitration"** in the Reverse-Engineering Gaps
   section. "Fairness" is AMD vocabulary. "Arbitration" is more neutral.
7. **Add VM_BIND scheduling significance** — note that VM_BIND operations are
   scheduled through a kernel bind exec_queue, not executed immediately.

### `s32-vs-r32-hws.md`

1. **Rewrite the "firmware-mediated" similarity** to note the different
   host/firmware responsibility splits (AMD: firmware-autonomous scheduler;
   Intel: host-driven policy with firmware-delegated context switching).
2. **Add explicit statement** that Xe has no visible process or gang
   abstraction.
3. **Add long-running mode** as a difference that has no direct AMD MES
   equivalent.
4. **Strengthen the resource programming difference** from "not published in
   the same way" to "deliberately kept in the host driver."
5. **Add a "What is better documented in Xe than AMD" section.** The Xe host
   driver code is fully readable; AMD's MES firmware internals are closed.
   The Xe note can document host-side behavior more completely than an
   outsider can document MES internal behavior. This asymmetry cuts both ways.

### Phase 1.5 roadmap

1. **Add entrance criteria** (section 8 above).
2. **Add named failure modes** (section 8 above).
3. **Add completion evidence** (section 8 above).
4. **Add explicit exclusion** of cross-device sharing.

## 10. Concrete Next Edits Before Commit

In priority order:

1. **Add G2H notification list to `s32-hws.md` section "GuC Control
   Interface."** This is a factual gap — the protocol is bidirectional and
   the document only covers one direction.

2. **Add long-running mode section to `s32-hws.md`.** This is a
   scheduling-significant concept that changes timeout, exec, and fence
   behavior. It cannot be omitted from a scheduling contract document.

3. **Add preempt-fence mode and compute-queue tracking.** These are
   visible host scheduling concepts that differ from AMD MES.

4. **Rename `s32-hws.md` or add clarifying subtitle.** The name matters
   because the r32/s32 parallel is the first thing readers will notice.

5. **Fix the "firmware-mediated" common category in `s32-vs-r32-hws.md`** to
   note the different host/firmware splits.

6. **Add "no process/gang abstraction" as an explicit difference** in the
   comparison.

7. **Add Phase 1.5 entrance criteria, failure modes, and completion
   evidence** to whatever document hosts the Phase 1.5 roadmap.

8. **Add VM_BIND scheduling significance** to `s32-hws.md`.

9. **Change "fairness" to "arbitration"** where referring to GuC internal
   behavior.

10. **Consider adding a "What Xe documents better" section** to the
    comparison, noting that full host-driver source visibility is itself
    a documentation advantage that AMD MES does not offer to outsiders.
