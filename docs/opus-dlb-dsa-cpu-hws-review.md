# DLB + DSA CPU-Side HWS Programming Model Review

Date: 2026-04-19

## 1. Executive Judgment

The memo's core claim is **technically sound and unusually honest**. It correctly
identifies that DLB + DSA can support a CPU-side event-scheduled async-work
runtime whose object model resembles HWS + DMA, and it correctly refuses to
claim this is a literal GPU firmware scheduler clone.

The architectural decomposition — DLB schedules events, DSA executes memory
operations, software bridges them — is the right one.

However, the memo has one critical omission that could derail the entire
prototype, and several places where the HWS analogy is carried further than
the hardware actually supports. The memo is better as an architecture note
than as a deployment plan.

**Bottom line**: the idea is viable. The memo is honest about what it is not.
But it underestimates two hard problems: DSA user-mode access restrictions
and the completion bridge latency cost. These need to be confronted before
prototyping, not discovered during it.

## 2. Major Flaws or Risks

### Biggest flaw: INTEL-SA-01084 kills the user-mode DSA story on all current hardware

The memo's prototype plan assumes user-space DSA access through the cdev path:
open, PASID/SVA bind, portal mmap, descriptor submission via write. This is
the correct programming model.

But in Linux 6.12, **every DSA device ID** sets `user_submission_safe = false`:

```c
/* init.c:51 */
[IDXD_TYPE_DSA] = {
    .user_submission_safe = false, /* See INTEL-SA-01084 security advisory */
},
[IDXD_TYPE_IAX] = {
    .user_submission_safe = false, /* See INTEL-SA-01084 security advisory */
},
```

This means `idxd_cdev_mmap()` (cdev.c:412) requires `CAP_SYS_RAWIO` to map
the portal page:

```c
if (!idxd->user_submission_safe && !capable(CAP_SYS_RAWIO))
    return -EPERM;
```

Furthermore, batch descriptors are blocked entirely on unsafe devices
(cdev.c:449-451).

**Practical consequence**: the memo's user-space-first prototype cannot work
on any shipping DSA hardware without root privileges or a kernel-mediated
submission path. This is not a theoretical concern — it is a hard gate.

The memo must either:

1. Acknowledge that the first prototype requires `CAP_SYS_RAWIO` (acceptable
   for internal development, not for production)
2. Design a kernel-mediated submission path as the primary DSA interface
3. Wait for hardware where `user_submission_safe = true`
4. Use the kernel DMA channel path (`drivers/dma/idxd/dma.c`) instead of
   the user cdev path

This changes the architecture materially: if DSA submission must go through
the kernel, the completion bridge design changes too, and the "user-space-first"
prototype scope shifts to kernel-space or hybrid.

### Biggest omission: completion bridge latency is unanalyzed

The memo correctly identifies the completion bridge as mandatory. But it does
not estimate the cost.

The completion bridge must:

1. Poll or receive interrupts for DSA completion records
2. Translate each completion into a DLB event
3. Submit the DLB event through `dlb_send()` on a port

Each step has latency:

- **DSA completion polling**: the completion record is in host memory. Polling
  adds CPU cost proportional to outstanding descriptors. Interrupt-driven
  completion adds wakeup latency (microseconds).
- **DLB event injection**: `dlb_send()` writes to the DLB portal. This is a
  device MMIO write. Round-trip through DLB scheduling adds device latency.
- **Worker wakeup**: the continuation worker must receive the event from its
  CQ. If the worker is already blocked on `dlb_recv()`, there is a wakeup
  latency.

For the whole round-trip (DSA submit → DSA complete → bridge → DLB event →
worker receives), the realistic floor is likely **5–15 microseconds** on
current hardware, depending on polling strategy and DLB CQ configuration.

This matters because:

- If the data movement is small (< 4KB), the round-trip through
  DSA + bridge + DLB may be **slower than a direct memcpy** on the worker
  thread
- The crossover point where DSA offload wins is likely in the **64KB–256KB
  range** for memmove, depending on memory topology

The memo should include a simple latency model and state the minimum transfer
size where DSA offload is expected to win. Without this, the prototype may
prove the programming model works while demonstrating it is slower than the
naive approach for all realistic workloads.

## 3. What The Memo Gets Right

### Architectural honesty

The memo's strongest quality is that it never overclaims. The explicit
"What This Does Not Replicate" section (§ on hardware execution contexts,
integrated VM, integrated firmware, completion chaining, unified fault/reset)
is exactly right.

### Two-accelerator composition is correctly identified

The decomposition into "DLB decides who processes, DSA does the memory
operation, software connects them" is the correct architectural pattern.
Attempting to force DLB or DSA into a role it does not naturally fill would
produce a fragile system.

### Event-as-handle pattern is correct

The recommendation to use DLB events as lightweight handles (pointer + routing
metadata) rather than full work payloads is correct and essential. The DLB
event structure is exactly 16 bytes:

- `udata64` (8 bytes) — work item handle
- `udata16` (2 bytes) — additional metadata
- `queue_id` (1 byte) — destination
- `sched_type` (2 bits) — scheduling mode
- `priority` (3 bits) — priority level
- `flow_id` (2 bytes) — flow key

That is enough for a handle + routing tag, but not for a work payload. The
memo is right to put the real work item in shared memory.

### Forward/release model is correctly valued

DLB's `dlb_send` / `dlb_forward` / `dlb_release` / `dlb_pop_cq` model is a
genuine hardware-enforced pipeline discipline. The memo correctly identifies
this as stronger than ad-hoc thread-pool task semantics.

### Directed queue for service lanes

Using directed queues for the completion bridge, error handling, and
control-plane events is the right topology. It avoids polluting load-balanced
queues with non-data-path traffic and gives dedicated service lanes a
guaranteed delivery path.

## 4. Where The DLB Analogy Holds

### DLB as hardware event scheduler: strong analogy

DLB is a real hardware scheduler. It decides which events go to which
consumers, enforces ordering (atomic/ordered), distributes load, manages
credits, and handles backpressure. This is genuinely more HWS-like than
most CPU-side alternatives (epoll, io_uring, thread pools).

The claim that "DLB is closer to an explicit HWS resource model than Xe GuC
is from the host-visible side" is **defensible but needs qualification**:

- DLB exposes its scheduling topology (domains, queues, ports, credits)
  more explicitly than Xe exposes GuC internals
- DLB scheduling modes (atomic, ordered, unordered, directed) are richer
  than any single GPU HWS queue-type taxonomy
- But DLB schedules **events to CPU consumers**, not **execution contexts
  to hardware engines** — the fundamental unit of work is different

So the claim is fair **at the object-model level** but misleading **at the
execution-model level**. Recommend adding the qualification.

### DLB ordered/atomic as per-flow serialization: strong analogy

Atomic scheduling with flow IDs gives per-flow mutual exclusion without
software locks. Ordered scheduling gives ordered-commit semantics. These
map well to GPU-style per-queue or per-context serialization.

### DLB credit pool as backpressure: strong analogy

GPU HWS systems have ring-buffer flow control (ring full → cannot submit).
DLB has credit pools (no credits → cannot send). The mechanism differs but
the architectural role is equivalent: hardware-enforced admission control.

## 5. Where The DSA Analogy Holds

### DSA as async copy engine: correct analogy

DSA performs memmove, memfill, compare, CRC, and batch operations
asynchronously from descriptor submission. This is directly analogous to
GPU SDMA/blit engines that do copy/fill work independently of the main
execution pipeline.

### DSA descriptor submission as DMA ring: correct analogy

The portal-based submission model (write descriptor to device-mapped page)
is architecturally similar to GPU DMA ring submission (write descriptors to
a ring buffer that the hardware reads).

### DSA completion record as fence: reasonable analogy

The completion record (status byte in host memory) is a simple form of
GPU fence (GPU writes to shared memory, CPU polls or gets interrupt). The
mechanism is simpler (no sequence number, just status per descriptor) but
the pattern is the same.

### DSA user WQ with PASID: correct analogy

User-mode access with per-process PASID binding maps well to GPU user-queue
models with per-process VM isolation. (Subject to the INTEL-SA-01084
constraint discussed above.)

## 6. Where The HWS Analogy Breaks

### Break 1: No execution context management (critical)

GPU HWS schedules **execution contexts** — LRCs, HQDs, MQDs — that
represent a full GPU pipeline state. The scheduler context-switches between
contexts on hardware engines, saving and restoring register state.

DLB schedules **events to CPU threads**. There is no hardware context save/
restore, no execution-context residency, no preemption with state preservation.
The CPU thread IS the execution context, and the OS scheduler manages it.

**This is the single most important non-equivalence.** The memo states it but
does not emphasize it enough. Every GPU HWS concept that depends on
hardware-managed execution context (context priority, context preemption,
context timeout, hardware queue residency) has no DLB analogue.

Recommend adding a bold warning:

> DLB schedules event delivery, not execution contexts. Any GPU HWS concept
> that depends on hardware context management (save/restore, preemption,
> residency, timeout) does not translate.

### Break 2: No integrated memory model

GPU HWS operates within a GPU virtual memory space. Queue objects have
per-queue or per-process GPU page tables. The scheduler interacts with the
memory model (page faults, eviction, residency).

DLB + DSA operate in host physical/IOMMU address space. There is no
device-local memory, no GPU-style page table, no scheduler-driven eviction.
The memo says this but should emphasize: **the entire GPU memory-management
half of HWS has no analogue here.** DMO/DMI concepts from the Mach project
would not apply to this runtime.

### Break 3: No hardware completion chaining

In GPU designs, the hardware pipeline can chain operations: a copy engine
completion can directly trigger a compute dispatch. In DLB + DSA, every
inter-device transition goes through software. The completion bridge is a
**software hot path in every multi-stage pipeline**, not a fallback.

This means the runtime's steady-state performance is gated by bridge latency,
not just by accelerator throughput.

### Break 4: No unified fault/reset model

GPU HWS has integrated hang detection, context reset, and fault recovery.
DLB and DSA have independent fault models:

- DLB: credit starvation, CQ overflow, domain failure
- DSA: descriptor error, page fault (via IOMMU), completion record error,
  event log overflow

Software must detect, correlate, and recover from faults across two
independent devices. A DSA page fault does not automatically propagate to
DLB, and vice versa. The runtime must handle:

- DSA descriptor fails → what happens to the DLB pipeline?
- DLB credit starvation → what happens to pending DSA completions?
- Worker thread crash → who drains the DLB CQ and releases tokens?

The memo mentions this generally but should enumerate specific failure
scenarios.

### Break 5: DLB event weight is a hidden complexity

The memo mentions credits but does not discuss event weight. DLB events can
have weight 0-3, occupying 1/2/4/8 CQ slots respectively. If the runtime uses
variable-weight events (e.g., heavy work items get weight 2 to limit CQ
occupancy), this adds a capacity-planning dimension that GPU HWS ring buffers
do not have.

If the runtime does not use event weight, the CQ can fill with many small
events while a worker processes one heavy item, creating head-of-line blocking.
The weight mechanism exists to solve this, but it adds design complexity.

## 7. Runtime Boundary Recommendation

### The four-object model is almost right

The proposed objects (runtime domain, CPU-side execution queue, work item,
completion bridge) are a clean decomposition. Suggested adjustments:

**Rename "CPU-side execution queue" to "task channel."**

Rationale: "execution queue" implies GPU-style execution context scheduling.
What the runtime actually provides is a named destination for event-routed
work with associated scheduling policy. "Task channel" or "work channel"
better describes this: a logical pipeline endpoint with scheduling attributes
(mode, priority, flow policy) and optional DSA affinity.

**Split the completion bridge into two concerns:**

1. **DSA completion poller** — watches completion records, calls callbacks
2. **Continuation injector** — takes completed work items and sends DLB events

These are logically separate: the poller is DSA-specific, the injector is
DLB-specific. Splitting them makes it possible to:

- Replace DSA with a different async engine (IAA, NIC, etc.)
- Replace DLB with a different event system
- Test each half independently

**Add a fifth object: error domain.**

The runtime needs a unified error-handling path that receives:

- DSA descriptor errors
- DSA page faults
- DLB credit exhaustion events
- Worker thread failures
- Timeout detection results

Without a named error domain object, error handling will be ad hoc and
inconsistent. GPU HWS has explicit reset/recovery paths for exactly this
reason.

### Where the runtime boundary should be drawn

The runtime should own:

- Work item allocation and lifecycle
- Event routing decisions (which DLB queue, which DSA WQ)
- Completion bridging
- Error correlation and recovery
- Backpressure policy (when to stop accepting new work)
- Statistics and observability

The runtime should NOT own:

- DLB domain configuration (delegate to a setup/config layer)
- DSA WQ configuration (delegate to sysfs/accel-config)
- Thread scheduling (use OS scheduler)
- Memory allocation strategy (use existing allocators)

## 8. Concrete Edits To Improve The Memo

In priority order:

### 1. Add INTEL-SA-01084 section

This is the highest-priority edit. Add a section titled "DSA User-Mode Access
Constraints" that states:

- All shipping DSA devices in Linux 6.12 set `user_submission_safe = false`
- User-mode portal mmap requires `CAP_SYS_RAWIO`
- Batch descriptor submission is blocked on unsafe devices
- The prototype must either accept root-privilege requirement, use
  kernel-mediated submission, or plan for future hardware

This constraint affects the entire user-space-first prototype strategy.

### 2. Add completion bridge latency analysis

Add a subsection estimating round-trip cost:

- DSA descriptor submission latency (portal MMIO write)
- DSA execution time (device-dependent, typically 1–5μs for small transfers)
- Completion polling latency (cache-line poll or interrupt wakeup)
- DLB event injection latency (portal MMIO write + scheduling)
- DLB CQ delivery latency (device scheduling + CQ poll)
- Total estimated round-trip: 5–15μs per stage transition

State the crossover point where DSA offload beats direct CPU memcpy
(estimated 64KB–256KB depending on memory topology and core count).

### 3. Add explicit non-equivalence warning for execution contexts

Strengthen the "What This Does Not Replicate" section with a prominent
statement:

> DLB schedules event delivery to CPU consumers. It does not schedule
> hardware execution contexts. Any GPU HWS concept that depends on
> hardware context save/restore, preemption, or residency management
> has no DLB analogue.

### 4. Rename "CPU-side execution queue" to "task channel"

Throughout the memo, replace "CPU-side execution queue" with "task channel"
(or "work channel"). The current name implies hardware execution context
management that does not exist.

### 5. Add failure scenario enumeration

Add a section listing specific cross-device failure modes:

- DSA page fault during in-flight memmove: work item is stuck, DLB pipeline
  is stalled unless timeout detects it
- DLB credit exhaustion: completion bridge cannot inject continuation events,
  DSA completions accumulate with no forward progress
- Worker thread crash: unreleased DLB tokens block that CQ slot, downstream
  pipeline stages starve
- DSA WQ full: descriptor submission fails, worker must retry or fail the
  work item
- DLB domain reset during active DSA operations: DSA completions arrive for
  events that no longer exist in DLB

### 6. Add event weight discussion

Explain the CQ weight mechanism and its design implications for the runtime.
State whether the prototype will use uniform weight (simpler) or variable
weight (better backpressure control).

### 7. Add IAA as a potential third accelerator

The local `idxd` driver also supports IAA (Intel Analytics Accelerator) with
compression/decompression opcodes. If the runtime is designed as a
multi-accelerator event pipeline, IAA is the obvious third engine. Mention
this as a future extension that validates the completion-bridge architecture:
if the bridge design works for DSA, adding IAA should require only a new
poller and new opcodes, not architectural changes.

### 8. Fix the DLB/GuC comparison claim

Change:

> DLB is actually closer to an explicit HWS resource model than Xe GuC is
> from the host-visible side

To:

> DLB exposes its scheduling topology (domains, queues, ports, credits,
> ordering modes) more explicitly than Xe exposes GuC internals. At the
> object-model level, DLB's scheduling vocabulary is richer than the
> host-visible Xe/GuC contract. But DLB schedules event delivery to CPU
> consumers, not execution contexts to hardware engines — the execution
> model is fundamentally different.

### 9. Add FreeBSD portability note

If this runtime is eventually meant to run on FreeBSD (given the project
context), note that:

- DLB has no in-tree FreeBSD driver (the `dlb` tree is Linux-only)
- DSA/idxd has no FreeBSD driver
- IOMMU/SVA/PASID infrastructure differs materially on FreeBSD
- The first prototype must be Linux-only
- FreeBSD portability is a Phase 2+ concern requiring its own porting plan

### 10. Add recommended naming

If this should not be called "CPU-side HWS," suggest the better name.

The memo should be titled:

> **DLB + DSA Event-Driven Async Work Runtime**

or:

> **DLB + DSA Hardware-Assisted Task Pipeline**

"CPU-side HWS" is useful as internal shorthand for the team that already
understands the GPU context, but it should not appear in any document that
might be read by someone who expects "HWS" to mean hardware execution
context scheduling. The subtitle can say "inspired by GPU HWS programming
models" but the primary name should describe what the system actually does:
event-driven async work routing with hardware scheduling and DMA offload.
