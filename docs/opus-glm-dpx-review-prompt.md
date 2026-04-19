# Opus / GLM Review Prompt: DPX Programming Model and Backend Family

Date: 2026-04-19

Use this as a copy-paste prompt for Opus, GLM, or another strong reasoning
model.

## Review Set

Primary DPX notes:

- [dpx-programming-model.md](/Users/me/wip-mach/wip-drm-xe/docs/dpx-programming-model.md)
- [dpx-api.md](/Users/me/wip-mach/wip-drm-xe/docs/dpx-api.md)
- [dpx.h](/Users/me/wip-mach/wip-drm-xe/include/dpx.h)
- [dpx-g32.md](/Users/me/wip-mach/wip-drm-xe/docs/dpx-g32.md)
- [dpx-r32.md](/Users/me/wip-mach/wip-drm-xe/docs/dpx-r32.md)
- [dpx-s32.md](/Users/me/wip-mach/wip-drm-xe/docs/dpx-s32.md)
- [dpx-dlb-dsa.md](/Users/me/wip-mach/wip-drm-xe/docs/dpx-dlb-dsa.md)
- [dpx-eventdev.md](/Users/me/wip-mach/wip-drm-xe/docs/dpx-eventdev.md)
- [dpx-eventdev-ioat.md](/Users/me/wip-mach/wip-drm-xe/docs/dpx-eventdev-ioat.md)

Important related context:

- [r32-hws.md](/Users/me/wip-mach/wip-drm-xe/docs/r32-hws.md)
- [s32-guc-submit.md](/Users/me/wip-mach/wip-drm-xe/docs/s32-guc-submit.md)
- [s32-vs-r32-hws.md](/Users/me/wip-mach/wip-drm-xe/docs/s32-vs-r32-hws.md)
- [dlb-dsa-cpu-hws-programming-model.md](/Users/me/wip-mach/wip-drm-xe/docs/dlb-dsa-cpu-hws-programming-model.md)

## Copy/Paste Prompt

```text
You are reviewing a long-term systems architecture effort that is trying to
define a premium cross-backend programming model called `DPX`.

Act as a skeptical senior systems / runtime / GPU / event-scheduler reviewer.
I want technical criticism, not encouragement.

You should assume this may become a top-tier product feature or flagship
runtime abstraction, so I want you to treat the review standard as high:

- the abstraction must be technically honest
- the backend split must be sustainable
- the API boundary must survive years of implementation pressure
- the fallback story must preserve semantics without lying about performance

Review targets:

1. `docs/dpx-programming-model.md`
   - umbrella DPX abstraction note
2. `docs/dpx-api.md`
   - concrete API and capability model
3. `include/dpx.h`
   - draft public header for the DPX API
4. `docs/dpx-g32.md`
   - shared GPU-side semantic layer between AMD `r32` and Intel `s32`
5. `docs/dpx-r32.md`
   - AMD `MES/HWS + SDMA` backend mapping
6. `docs/dpx-s32.md`
   - Intel Xe `exec_queue + GuC submit + blit/copy` backend mapping
7. `docs/dpx-dlb-dsa.md`
   - `DLB + DSA` CPU-accelerator backend mapping
8. `docs/dpx-eventdev.md`
   - generic `eventdev` scheduler fallback
9. `docs/dpx-eventdev-ioat.md`
   - `eventdev + ioat` accelerated fallback backend

Important related context:

- `docs/r32-hws.md`
  - AMD official RDNA3+ Micro Engine Scheduler specification
- `docs/s32-guc-submit.md`
  - reverse-engineered Xe host/GuC scheduling contract
- `docs/s32-vs-r32-hws.md`
  - comparison note between AMD `r32` and Intel `s32`
- `docs/dlb-dsa-cpu-hws-programming-model.md`
  - deeper DLB + DSA architecture note

Local source basis behind the notes:

- `../nx/dlb`
- `../nx/linux-6.12/drivers/dma/idxd`
- `../nx/linux-6.12/include/uapi/linux/idxd.h`
- `../nx/dpdk-25.11/lib/eventdev/rte_eventdev.h`
- `../nx/dpdk-25.11/lib/eventdev/rte_event_dma_adapter.h`
- `../nx/dpdk-25.11/doc/guides/prog_guide/eventdev/eventdev.rst`
- `../nx/dpdk-25.11/doc/guides/prog_guide/eventdev/event_dma_adapter.rst`
- `/usr/src/share/man/man4/ioat.4`
- `/usr/src/sys/dev/ioat/ioat.h`
- `/usr/ports/net/dpdk`

Project intent:

`DPX` is supposed to be a unified programming model above several backend
families:

- GPU-side scheduler + DMA backends
  - AMD `MES/HWS + SDMA`
  - Intel Xe `GuC-visible queue model + blit/copy`
- CPU-side accelerator backend
  - Intel `DLB + DSA`
- scheduler fallback backend
  - DPDK `eventdev`
- accelerated host fallback backend
  - `eventdev + ioat`
- pure software fallback backend
  - `eventdev + CPU memcpy/memfill`

The goal is not to claim these are identical execution architectures.

The goal is:

- keep one mental model
- keep one upper software shape
- keep a stable semantic intake for channels / ordering / async transfer /
  completion / continuation
- let different hardware pairs implement the same upper abstraction
- allow init-time backend selection to exploit better hardware when present
- keep a software fallback so code and model still run when hardware support
  is absent

That means the software path is allowed to be slower.
Performance equivalence is not the promise.
Semantic continuity and implementation preparedness are the promise.

Current DPX thesis:

1. `DPX` should be defined at the control-model layer:
   - task channels
   - ordering by flow
   - priority
   - transfer stages
   - completion edges
   - error domains

2. `g32` is the shared GPU-side semantic layer between:
   - `r32`
   - `s32`

3. `r32` and `s32` are real GPU backends under that shared semantic layer,
   even though:
   - `r32` is much more explicit and firmware-API-rich
   - `s32` is more host-driven and more opaque

4. `DLB + DSA` is the strongest non-GPU hardware backend because:
   - DLB is a real scheduler substrate
   - DSA is a real async transfer engine
   - software composes them into one runtime

5. `eventdev` is the scheduler fallback because it preserves:
   - event-routed staged work
   - atomic / ordered / parallel scheduling
   - flow ordering
   - dynamic load balancing
   - compact event handles

6. `ioat(4)` is the DMA fallback because it offers:
   - multiple DMA channels
   - sequential operations per channel
   - copy / blockfill / CRC / null
   - callbacks
   - interrupt coalescing
   - `DMA_FENCE` for dependent operations

7. Therefore DPX can be:
   - abstract enough to be stable
   - concrete enough to map onto real hardware
   - portable enough to keep a software/fallback story

Important design rule:

The upper DPX API should not be:

- raw AMD MES packets
- raw Xe GuC CT actions
- raw DLB queue calls
- raw DSA descriptors
- raw IOAT descriptors
- raw eventdev port operations

Instead it should be a task-channel / work-item / continuation runtime API.

What I need from you:

- red-team whether `DPX` is a real abstraction or just a stack of analogies
- identify where the document split is right and where it is artificial
- judge whether `g32` is a useful shared GPU semantic layer
- judge whether the fallback story is honest enough to preserve the model
- identify what the common API can safely abstract, and what must remain
  backend-specific forever
- tell me whether this is strong enough to justify itself as a premium,
  flagship-grade feature

Please answer these questions directly.

1. Executive judgment:
   - Is `DPX` as currently defined a sound long-term abstraction, a partially
     sound abstraction, or a misleading abstraction?

2. Biggest architectural risk:
   - What is the single biggest risk that could cause DPX to collapse under
     real backend implementation pressure?

3. Biggest omission:
   - What important semantic or product-level concern is still missing from the
     current DPX notes?

4. `g32` honesty check:
   - Is the shared GPU semantic layer (`dpx-g32.md`) real and useful?
   - Or is it flattening AMD and Intel differences too aggressively?
   - Is task-channel / async-transfer / completion-routing really the right
     shared GPU layer?

5. `r32` mapping check:
   - Is `dpx-r32.md` fair to AMD MES/HWS?
   - Does it preserve the value of AMD's explicit process/gang/queue/resource
     model, or does it over-flatten it into DPX?

6. `s32` mapping check:
   - Is `dpx-s32.md` fair to the Xe host-visible queue model?
   - Is `xe_exec_queue` really the right DPX task-channel mapping?
   - What part of the Xe story is still too opaque to treat as stable DPX
     backend behavior?

7. `DLB + DSA` backend check:
   - Is `dpx-dlb-dsa.md` the right way to treat the CPU-side accelerator backend?
   - Does it correctly separate structural similarity from literal
     architectural equivalence?
   - Is it strong enough to be the main non-GPU proving backend for DPX?

8. `eventdev` fallback check:
   - Is `dpx-eventdev.md` right that `eventdev` is the correct scheduler
     fallback?
   - Does `eventdev` preserve enough of DPX to justify one upper software
     shape?
   - What semantics are weakest when moving from DLB to generic eventdev?

9. `eventdev + ioat` fallback check:
   - Is `dpx-eventdev-ioat.md` technically honest?
   - Is `ioat(4)` serious enough to count as a DPX transfer backend, or is
     this too optimistic?
   - Is the likely user/kernel integration burden understated?

10. Common API boundary:
    - What must the stable DPX API contain?
    - What must absolutely remain backend-specific?
    - Which abstractions are likely to become leaky:
      - memory model
      - reset/recovery
      - completion transport
      - queue grouping
      - preemption/context semantics
      - capability discovery

11. Runtime-switch realism:
    - Is the project's hoped-for API-compatible, capability-aware backend
      selection story technically realistic?
    - If yes, under what conditions?
    - If no, what has to change for that to become true?

12. Product-quality question:
    - Is DPX strong enough to become a premium / flagship feature rather than
      just an internal portability layer?
    - If yes, what makes it premium?
    - If no, what is still missing before it could deserve that status?

13. Naming and document split:
    - Are these document names and boundaries right?
      - `dpx-programming-model`
      - `dpx-api`
      - `dpx-g32`
      - `dpx-r32`
      - `dpx-s32`
      - `dpx-dlb-dsa`
      - `dpx-eventdev`
      - `dpx-eventdev-ioat`
    - Should any be merged, renamed, or split further?

14. Strongest corrections:
    - If you had to rewrite the single most important sentence, section, or
      design rule in this DPX document set, what would it be?

Please structure your answer like this:

1. Executive Judgment
2. Major Flaws or Risks
3. What The DPX Model Gets Right
4. Where The Shared `g32` Layer Holds
5. Where The Backend Mappings Hold
6. Where The Abstraction Leaks
7. Fallback and Runtime-Switch Assessment
8. Product and API Recommendations
9. Concrete Edits To Improve The Documents

Important style request:

- do not give generic praise
- do not answer like a marketer
- criticize the abstraction as if it will have to survive years of
  implementation and user expectations
- explicitly call out anything that is elegant in theory but likely to break in
  real code
- if you think DPX is only viable as an internal implementation discipline and
  not as a premium product feature, say so plainly
```
