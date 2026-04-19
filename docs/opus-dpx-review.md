# DPX Programming Model and Backend Family Review

Date: 2026-04-19

## 1. Executive Judgment

DPX as currently defined is a **sound long-term abstraction** with genuine
technical substance. It is not a stack of analogies wearing a trenchcoat.

The core insight — that GPU HWS+DMA, DLB+DSA, eventdev+ioat, and
eventdev+memcpy all share a common control-model shape (task channels,
ordering, async transfer, completion routing, continuation) even though
their execution models differ fundamentally — is correct and verified across
all four source bases.

Three structural facts confirm the abstraction is real, not forced:

1. **Event representation convergence**: DLB `dlb_event_t` is 16 bytes.
   DPDK `rte_event` is 16 bytes. Both carry {handle, queue_id, sched_type,
   priority, flow_id} as first-class fields. This is not a coincidence — DLB
   is the reference hardware for the DPDK eventdev abstraction. DPX inherits
   this convergence naturally.

2. **Scheduling mode convergence**: DLB exposes ATOMIC/ORDERED/UNORDERED/
   DIRECTED. DPDK eventdev exposes ATOMIC/ORDERED/PARALLEL. Both GPU backends
   have queue-level priority and ordering semantics. The vocabulary differs
   but the capabilities overlap meaningfully.

3. **Transfer engine convergence**: DSA descriptors (memmove/fill/compare/CRC),
   IOAT operations (copy/blockfill/CRC/null), and GPU blit/SDMA engines all
   provide the same abstract operation set: async memory-to-memory data
   movement with completion notification.

**However**, the current documents have a critical structural weakness: they
define DPX entirely as a description exercise and never confront the hardest
design question — **what does the actual API look like?** Until there is a
concrete API definition (even a draft), DPX remains an architecture note, not
a programming model. Architecture notes do not survive implementation
pressure. API contracts do.

**Is DPX premium/flagship grade?** Not yet. It is currently at the quality
level of a well-reasoned internal architecture memo. To reach premium grade,
it needs: a concrete API, a working prototype on at least one backend, a
performance characterization, and a developer experience story. The
foundation is strong enough to justify that investment.

## 2. Major Flaws or Risks

### Risk 1: The abstraction has no concrete API definition (critical)

Seven documents describe what DPX *is*. None of them show what DPX *looks
like to a programmer*. The recommended API section in `dpx-programming-model.md`
lists concepts (create domain, create task channel, submit work item, etc.)
but no signatures, no types, no error returns, no lifecycle rules.

This is dangerous because the gap between "these concepts exist" and "these
types compose correctly" is where abstractions die. Every concept in the
current DPX document set is reasonable in isolation. The question is whether
they compose into a coherent API that a developer can hold in their head.

**Concrete risk**: without an API draft, the backend-specific documents
(`dpx-r32`, `dpx-s32`, `dpx-intel`, etc.) are making mapping promises that
may be impossible to honor when the actual types are defined. For example,
`dpx-r32` says "process/gang/queue should be backend internals under a
DPX-facing task-channel API." But what if the DPX task-channel type needs
gang-level policy to work correctly on AMD? Then the abstraction leaks at
the type level, not the concept level.

**Required next step**: define `dpx.h` — even as a 200-line draft with
incomplete implementations. The act of writing type definitions and function
signatures will immediately reveal which backend mappings are real and which
are aspirational.

### Risk 2: The g32 shared GPU layer may be too thin to justify itself

`dpx-g32.md` identifies five shared GPU-side concepts:

1. Firmware is in the scheduling path
2. Host creates schedulable queue-like objects
3. Priority and time-slicing exist
4. Transfer engines are distinct from execution lanes
5. Completion and recovery are not optional

These are all true. But they are also true of DLB+DSA and eventdev+ioat.
There is nothing in this list that is GPU-specific. The "shared GPU semantic
layer" is actually the DPX abstraction itself, restated for GPU backends.

This means `g32` is either:

- **redundant** — it restates DPX for a GPU audience but adds nothing new
- **a future code-sharing layer** — justified only when there is actual shared
  GPU backend adapter code that is neither r32-specific nor s32-specific

Currently it is the first. It should earn its existence by becoming the
second. If no shared GPU adapter code materializes, merge `g32` into the
umbrella document and treat `r32` and `s32` as direct DPX backends.

### Risk 3: The backend switch story is underdefined

The documents repeatedly promise that DPX enables a "runtime/backend switch."
But none of them define:

- When can the switch happen? (Init-time only? Per-domain? Per-channel?)
- What state must be migrated? (Can a work item submitted on DLB+DSA be
  completed on eventdev+memcpy if the backend changes?)
- What happens to in-flight work during a switch?
- Is the switch manual (application selects backend) or automatic (runtime
  selects based on hardware detection)?

If the answer is "init-time only, no migration, application selects," that
is fine — but it should be stated explicitly. If the answer is "transparent
runtime migration," that is an order of magnitude harder and the documents
should not imply it without confronting the design cost.

### Risk 4: Transfer stage abstraction will leak on memory model

The DPX transfer stage abstracts copy/fill/compare. But these operations have
fundamentally different memory models on different backends:

| Backend | Memory model |
| --- | --- |
| GPU SDMA/blit | GPU virtual addresses, GGTT or per-VM PPGTT |
| DSA | Host virtual addresses via IOMMU/PASID/SVA |
| IOAT | Host physical addresses (bus_addr_t) |
| CPU memcpy | Host virtual addresses, direct |

A DPX transfer stage that says "copy from src to dst, length N" must resolve
`src` and `dst` into the backend's address space. This is not a detail — it is
the central question of the transfer abstraction.

The documents correctly identify "memory model must stay backend-specific."
But they do not explain how a single DPX work item describes source and
destination in a way that is meaningful to all backends. Options:

- **Abstract handles**: DPX defines its own memory reference type that each
  backend resolves. Clean but adds a translation layer.
- **Virtual addresses**: Use host virtual addresses and let each backend
  resolve them. Works for DSA and CPU, not directly for GPU or IOAT.
- **Registered buffers**: Require DPX buffers to be pre-registered with the
  domain so each backend can prepare its own address translation. Most
  realistic but adds lifecycle complexity.

The registered-buffer approach is the right one. It maps to:
- GPU BO/VM_BIND registration
- DSA PASID mapping (implicit via process address space)
- IOAT bus_dma mapping
- CPU direct access (no mapping needed)

But this must be explicit in the API, not deferred.

### Risk 5: Error domain is sketched but not designed

`dpx-programming-model.md` lists error sources (transfer failure, scheduling
congestion, backend reset/loss, worker fault, unsupported feature). But it
does not define:

- How errors propagate (synchronous return? Error event? Callback?)
- Whether errors are per-work-item, per-channel, or per-domain
- Whether error recovery is automatic or application-driven
- How partial completion is reported (10 of 20 items completed, then failure)

Error handling is where abstractions break first. GPU reset semantics (queue
ban, GT reset, wedged state) are fundamentally different from DLB credit
exhaustion or IOAT channel error. The DPX error domain must be designed
with the same care as the happy path.

## 3. What The DPX Model Gets Right

### The control-model centering is exactly right

DPX is defined at the task-channel / ordering / transfer / completion layer,
not at the execution-context or memory-model layer. This is the correct
abstraction boundary. It captures what all backends genuinely share and
excludes what they genuinely don't.

### The backend ladder is well-structured

The four-tier backend hierarchy (native GPU → CPU accelerator → accelerated
host fallback → pure software) is the right architecture. Each tier preserves
the semantic model while accepting lower performance. This is how portable
runtimes actually work (cf. Vulkan compute → Metal → CPU fallback, or CUDA →
OpenCL → reference implementation).

### The naming is good

"DPX" as a codename is neutral and does not promise any specific hardware.
"Task channel" is better than "execution queue" (avoids GPU context
implication). "Transfer stage" is better than "DMA operation" (avoids
implying a specific engine type). "Completion edge" correctly models the
graph nature of async pipelines.

### The 16-byte event alignment is a hidden structural advantage

The DPX documents do not emphasize this enough, but it is significant:

- DLB `dlb_event_t`: 16 bytes — `{udata64, udata16, queue_id, sched_type, priority, flow_id}`
- DPDK `rte_event`: 16 bytes — `{flow_id, sub_event_type, event_type, op, sched_type, queue_id, priority, impl_opaque} + {u64/event_ptr/mbuf}`

Both are compact event handles that carry routing metadata and a 64-bit
payload/pointer. This means DPX work-item dispatch has a natural 16-byte
event envelope on the two strongest scheduler backends. The pure software
backend can use the same layout. This structural convergence should be
documented as a deliberate design feature, not a coincidence.

### The "do not expose raw backend APIs" rule is essential

The explicit prohibition against making raw MES packets, GuC CT actions,
DLB queue calls, DSA descriptors, IOAT descriptors, or eventdev port
operations part of the DPX API is the most important design rule in the
entire document set. It is what makes DPX an abstraction rather than a
multiplexer.

## 4. Where The Shared `g32` Layer Holds

### As a concept: weak

As noted above, the five `g32` concepts are DPX concepts, not GPU-specific
concepts. The `g32` document is currently a GPU-flavored restatement of the
umbrella document.

### As a future code layer: conditionally strong

`g32` becomes genuinely valuable if and when there is shared backend adapter
code for:

- Common fence/completion plumbing that works for both AMD and Intel GPU fences
- Common timeout/reset state machine that normalizes GPU hang into DPX error
- Common engine-class mapping (render, copy, video → DPX channel classes)
- Common priority mapping (GPU priority levels → DPX priority levels)

If those materialize, `g32` is the right place for them. Until then, it is
documentation overhead.

### Recommendation

Keep `dpx-g32.md` but add a clear section: "This layer is justified only if
shared GPU adapter code materializes. If r32 and s32 adapters share no code,
merge this document into dpx-programming-model.md."

## 5. Where The Backend Mappings Hold

### `dpx-r32` (AMD MES+SDMA): Strong

The mapping is honest and correctly identifies r32 as the richest backend.
The key insight — that AMD's process/gang/queue hierarchy should be preserved
inside the backend adapter, not flattened away — is correct.

**One weakness**: the document does not discuss how AMD's richer grouping
model (process/gang) could optionally surface as DPX capabilities. For
example, a future DPX API could have `dpx_channel_group_create()` that maps
to a MES gang on r32 and is a no-op on s32/DLB. This kind of optional
capability layering is how mature abstractions preserve backend richness
without requiring it.

### `dpx-s32` (Intel Xe GuC+blit): Strong

The `xe_exec_queue → DPX task channel` mapping is natural and well-justified.
The document correctly notes that s32 is a "narrower and more host-driven"
backend.

**One weakness**: the document says the mapping is "the cleanest DPX-to-backend
mapping" but does not explain why. It is cleanest because `xe_exec_queue` is
already a queue-centric object without AMD's process/gang superstructure, so
it maps 1:1 to a task channel. That should be stated explicitly as a design
advantage.

**One concern**: the document does not mention long-running mode or
preempt-fence mode. These Xe scheduling concepts (identified in the previous
`s32-hws` review) affect how task channels should behave for compute
workloads. A DPX task channel for long-running compute has fundamentally
different timeout semantics than one for short render jobs.

### `dpx-intel` (DLB+DSA): Strong with known constraints

The mapping is correct and the document already incorporates the INTEL-SA-01084
constraint from the earlier review. The identification of DLB+DSA as "the
best hardware validation backend before GPU-side rollout" is strategically
correct — it is the easiest to instrument and the hardest to accidentally
make GPU-specific.

### `dpx-eventdev` (generic scheduler fallback): Sound

The document correctly identifies eventdev as the right scheduler fallback.
The structural alignment between DLB and eventdev (same event model, same
scheduling modes, same DPDK abstraction) is the strongest evidence that DPX
is not a forced analogy.

**One important gap**: the document does not discuss eventdev **adapter**
mechanisms. DPDK has explicit event-DMA adapters
(`rte_event_dma_adapter`) that bridge event scheduling with DMA engines.
This is directly relevant to DPX — it means DPDK already has a partial
implementation of the DPX completion bridge pattern. The documents should
reference this as prior art and potential implementation substrate.

### `dpx-eventdev-ioat` (accelerated fallback): Honest but fragile

The document is technically honest about IOAT's limitations. The key
concerns are correctly identified:

- IOAT is a kernel API (`bus_dmaengine_t`), not a userspace one
- You cannot submit new DMA from the callback context
- The user/kernel integration burden is significant

**The biggest weakness** is that this backend requires combining:
- A userspace runtime (DPDK eventdev)
- A kernel DMA engine (FreeBSD ioat)
- A user/kernel bridge for submission and completion

This is architecturally awkward. The document suggests three shapes
(hybrid shim, mostly-kernel, eventdev+CPU-copy first with optional ioat).
The third option is clearly correct for a first implementation. The document
should be more assertive: **start with eventdev + CPU memcpy as the FreeBSD
fallback. Add ioat acceleration only after the CPU-copy path works and
profiling shows the DMA offload is worthwhile.**

## 6. Where The Abstraction Leaks

These are the places where the DPX abstraction will be pressured by real
implementation:

### Leak 1: Memory references (critical, discussed above)

How does a DPX work item describe `src` and `dst` in a backend-independent
way? This is the #1 leaky abstraction and must be solved in the API, not
deferred.

### Leak 2: Completion transport latency

On GPU backends, completion is a hardware fence signal — sub-microsecond
from operation end to host visibility. On DLB+DSA, completion requires a
software bridge — 5-15 microseconds. On eventdev+memcpy, completion is a
function return — effectively zero latency but no async benefit.

If DPX application code is written assuming low-latency completion (as it
would be on a GPU backend), it will perform badly on DLB+DSA and pointlessly
on CPU memcpy. The DPX API should expose completion latency as a queryable
backend property so applications can make informed pipelining decisions.

### Leak 3: Queue capacity and backpressure

DLB has explicit credits. DPDK eventdev may or may not. GPU queues have ring
buffer limits. CPU queues have whatever limit the software imposes. The DPX
API must define a backpressure model, or applications will deadlock on some
backends and waste memory on others.

The simplest correct approach: DPX domain creation takes a capacity hint.
Each backend translates this into its own resource allocation. The DPX submit
function returns a "would block" status when the backend is full. Application
code must handle this.

### Leak 4: Ordering scope

DLB atomic ordering is per-flow within a queue. DPDK eventdev ordering is
per-flow within a queue. GPU queue ordering is typically total within a queue
(all work in a queue is serialized unless explicitly parallel).

If a DPX application assumes per-queue total ordering (correct on GPU), it
will get per-flow ordering on DLB/eventdev. If it assumes per-flow ordering,
it will get unnecessarily strict total ordering on GPU. The DPX API should
make the ordering contract explicit per channel: total-order, per-flow-order,
or unordered.

### Leak 5: Preemption and fairness

GPU backends may preempt a long-running task channel to give time to a higher-
priority channel. DLB does not preempt — it schedules the next event to a
different port. The DPX abstraction should not promise preemption semantics.
If an application needs guaranteed preemption (e.g., interactive latency),
that is a backend capability, not a DPX guarantee.

## 7. Fallback and Runtime-Switch Assessment

### Is the runtime switch realistic?

**Yes, but only at init-time or domain-creation-time.** Transparent mid-flight
backend switching is not realistic because:

- In-flight work items have backend-specific state (GPU fences, DSA completion
  records, DLB tokens)
- Backend switch would require draining all in-flight work, which defeats the
  purpose of async pipelining
- Memory references may be resolved differently per backend

**The realistic switch model**: application queries available backends at
startup, creates a DPX domain on the best available backend, and runs to
completion on that backend. If hardware is added or removed, the next domain
creation can use a different backend.

This is how Vulkan device selection works, and it is the right model for DPX.

### What conditions make the switch work

1. DPX API types are fully backend-independent (no backend pointers leak
   through the API)
2. Work-item format is identical across backends (same struct)
3. Memory references are resolved at submit time, not at work-item creation
   time
4. Application code never calls backend-specific APIs directly

### What conditions break the switch

1. Work items embed backend-specific handles (GPU BO handles, DSA portal
   addresses)
2. Completion tracking uses backend-specific mechanisms (raw fence FDs,
   DLB tokens)
3. Error handling assumes backend-specific recovery behavior

## 8. Product and API Recommendations

### Is DPX premium/flagship grade?

**Not yet, but it can become so.** What makes it potentially premium:

- It is the only known abstraction that spans GPU scheduler+DMA, CPU hardware
  event scheduler+DMA, and software fallback in one coherent model
- The 16-byte event convergence between DLB and DPDK eventdev provides a
  structural foundation that is not easily replicated
- The backend ladder (native GPU → hardware accelerator → software fallback)
  is the right architecture for a portable async runtime
- The "same mental model, different hardware" value proposition is
  immediately understandable

What it still needs to reach premium:

1. **A concrete API** (`dpx.h`) — even draft quality
2. **A working prototype on one backend** — DLB+DSA is the right first target
3. **A performance characterization** — round-trip latency, throughput
   crossover points, overhead vs raw backend
4. **A developer experience story** — what does it feel like to write code
   against DPX? Is it simpler than using the backend directly?
5. **A capability discovery mechanism** — how does an application learn what
   the current backend supports?

### Recommended API skeleton

```
dpx_domain_t     dpx_domain_create(dpx_domain_config_t *cfg);
void              dpx_domain_destroy(dpx_domain_t domain);

dpx_channel_t    dpx_channel_create(dpx_domain_t domain,
                                     dpx_channel_config_t *cfg);
void              dpx_channel_destroy(dpx_channel_t channel);

dpx_buffer_t     dpx_buffer_register(dpx_domain_t domain,
                                      void *addr, size_t len,
                                      uint32_t flags);
void              dpx_buffer_deregister(dpx_buffer_t buf);

int               dpx_submit(dpx_channel_t channel,
                              dpx_work_item_t *items, uint32_t count);

int               dpx_poll(dpx_domain_t domain,
                            dpx_completion_t *completions, uint32_t max);

dpx_backend_t    dpx_backend_detect(uint32_t flags);
int               dpx_backend_query_caps(dpx_backend_t backend,
                                          dpx_caps_t *caps);
```

Key design decisions in this skeleton:

- **Buffers are registered with the domain**, not passed raw at submit time.
  This solves the memory-model leak.
- **Submit returns int** (count submitted or error), allowing backpressure.
- **Poll is domain-wide**, not per-channel. This allows the implementation
  to batch completions efficiently.
- **Backend detection is explicit**, supporting init-time selection.
- **Capabilities are queryable**, supporting conditional feature use.

### Recommended document restructure

Current structure (7 documents) is slightly over-decomposed for the current
maturity level. Recommended:

| Current | Recommendation |
| --- | --- |
| `dpx-programming-model.md` | Keep — this is the umbrella |
| `dpx-g32.md` | Demote to a section in the umbrella until shared GPU code exists |
| `dpx-r32.md` | Keep as `dpx-backend-amd.md` |
| `dpx-s32.md` | Keep as `dpx-backend-xe.md` |
| `dpx-intel.md` | Keep as `dpx-backend-dlb-dsa.md` |
| `dpx-eventdev.md` | Merge with `dpx-eventdev-ioat.md` into `dpx-backend-eventdev.md` |
| `dpx-eventdev-ioat.md` | Merge into above |

Add new:

| New document | Purpose |
| --- | --- |
| `dpx.h` (draft) | Concrete API definition |
| `dpx-memory-model.md` | How buffer registration and address resolution work per backend |
| `dpx-error-model.md` | How errors propagate, per-item vs per-channel vs per-domain |

## 9. Concrete Edits To Improve The Documents

In priority order:

### 1. Write `dpx.h` draft

This is the single most important next step. A 200-line header with type
definitions, function signatures, and doc comments will do more to validate
the abstraction than any amount of prose. It will immediately reveal:

- Whether the task-channel type can be defined without leaking backend state
- Whether the work-item type can describe transfers backend-independently
- Whether the completion type can normalize across backends
- Whether the error type can capture the right failure modes

### 2. Add buffer registration to the programming model

The current `dpx-programming-model.md` describes work items with "source and
destination references" but does not define what those references are. Add a
section:

> **DPX Buffer**: a memory region registered with a DPX domain. Registration
> allows the backend to prepare address translation (GPU VM mapping, IOMMU/PASID
> setup, bus_dma mapping, or direct pointer validation). Transfer stages
> reference registered buffers, not raw pointers or backend-specific handles.

### 3. Add completion latency as a documented backend property

Each backend document should include an estimated round-trip latency:

- GPU SDMA/blit: ~1-5μs (hardware fence to host visibility)
- DLB+DSA: ~5-15μs (DSA complete → bridge → DLB event → worker)
- eventdev+ioat: ~10-50μs (ioat callback → eventdev injection → worker)
- eventdev+memcpy: ~0μs async overhead (synchronous on worker thread)

This helps application developers understand the performance characteristics
of each backend and make informed pipelining decisions.

### 4. Define the backend switch model explicitly

Add a section to the umbrella document:

> **Backend selection is an init-time decision.** A DPX domain is created on
> a specific backend. The backend cannot change for the lifetime of the domain.
> To switch backends, destroy the domain and create a new one. In-flight work
> items are not migrated; the application must drain the domain before
> destroying it.

### 5. Add DPDK event-DMA adapter as prior art

In `dpx-eventdev.md`, add a reference to DPDK's `rte_event_dma_adapter` API.
This is an existing DPDK mechanism that bridges event scheduling with DMA
engines — exactly the completion bridge pattern that DPX needs. If DPX uses
DPDK eventdev as its scheduler fallback, the event-DMA adapter is a potential
implementation substrate for the DLB+DSA and eventdev+ioat backends.

### 6. Add the 16-byte event convergence as a design feature

In `dpx-programming-model.md`, add a section documenting the structural
alignment between DLB events and DPDK rte_events:

> Both DLB `dlb_event_t` and DPDK `rte_event` are 16-byte structures carrying
> {user data, queue ID, scheduling type, priority, flow ID}. This convergence
> is not coincidental — DLB is the reference hardware for the DPDK eventdev
> abstraction. DPX inherits this 16-byte event envelope as its natural
> scheduler-visible dispatch unit.

### 7. Strengthen the "do not expose raw backend APIs" rule

The rule exists but should be promoted to a first-class design principle with
a name. Suggest:

> **The DPX Opacity Rule**: No backend-specific type, handle, descriptor,
> packet, or action code may appear in the DPX public API. Backend-specific
> operations are accessed only through the DPX API surface. Violation of this
> rule is a design defect, not a feature.

### 8. Add capability discovery to the programming model

Add a section defining queryable capabilities:

- Scheduling modes supported (atomic, ordered, parallel, directed)
- Maximum channels per domain
- Maximum priority levels
- Transfer operations supported (copy, fill, compare, CRC)
- Maximum transfer size
- Completion model (polling, interrupt, callback)
- Buffer registration requirements
- Estimated completion latency

This allows application code to adapt to backend capabilities without
backend-specific code paths.

### 9. Add ordering contract to task-channel creation

Task channel configuration should include an explicit ordering contract:

- `DPX_ORDER_TOTAL`: all items in this channel are serialized (GPU default)
- `DPX_ORDER_PER_FLOW`: items with the same flow_id are serialized, different
  flows may execute in parallel (DLB/eventdev default)
- `DPX_ORDER_NONE`: no ordering guarantee

This prevents the ordering-scope abstraction leak discussed above.

### 10. Define the first prototype scope

The documents recommend validating DLB+DSA first. The prototype scope should
be:

1. `dpx.h` draft implementation against DLB+DSA
2. One DPX domain, two task channels (one ordered, one parallel)
3. One transfer stage (DSA memmove)
4. Completion bridge
5. Round-trip latency measurement
6. Then: implement the same `dpx.h` against eventdev + CPU memcpy
7. Verify both backends pass the same functional test suite
8. Compare latency/throughput

If both backends pass the same tests through the same API, DPX is real.
If they cannot, the API needs revision before adding more backends.
