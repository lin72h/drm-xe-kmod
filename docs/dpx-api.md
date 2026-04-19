# DPX API Sketch

Date: 2026-04-19

## Purpose

This note turns the DPX object model into a concrete API sketch.

It is not yet a final ABI specification.
It is the bridge between:

- architecture note
- backend family
- product-grade runtime contract

The prose in this note is paired with:

- `include/dpx.h`
  - a draft public header that forces the API into concrete types and
    function signatures

Primary local basis:

- `docs/dpx-programming-model.md`
- `include/dpx.h`
- `docs/dpx-g32.md`
- `docs/dpx-r32.md`
- `docs/dpx-s32.md`
- `docs/dpx-dlb-dsa.md`
- `docs/dpx-eventdev.md`
- `docs/dpx-eventdev-ioat.md`

## Design Rule

The strongest API rule is:

> DPX backend switching is API-compatible and capability-aware, not
> automatically performance-transparent.

That means:

- upper-layer software can target one DPX API
- different backends may expose different capabilities and latency classes
- software must query capabilities and adapt where needed
- backend choice is fixed for the lifetime of a domain

## Core Types

The minimal common types should be:

- `dpx_domain`
- `dpx_channel`
- `dpx_work_item`
- `dpx_buffer_ref`
- `dpx_completion`
- `dpx_error`
- `dpx_capability_set`

## Capability Model

Every backend pair must answer a capability query.

Suggested fields:

- `scheduler_type`
  - `DPX_SCHED_GPU_FIRMWARE`
  - `DPX_SCHED_HARDWARE_EVENT`
  - `DPX_SCHED_SOFTWARE_EVENT`
- `transfer_type`
  - `DPX_XFER_GPU_DMA`
  - `DPX_XFER_DSA`
  - `DPX_XFER_IOAT`
  - `DPX_XFER_CPU_MEMCPY`
- `max_channels`
- `max_flow_id`
- `scheduling_modes`
  - atomic
  - ordered
  - parallel
  - directed
- `transfer_ops`
  - copy
  - fill
  - compare
  - crc
  - null
- `completion_mode`
  - fence
  - polling
  - callback
  - software_signal
- `supports_preemption`
- `supports_grouping`
- `supports_memory_registration`
- `typical_completion_latency_ns`
- `failure_scope_model`

## Buffer References

The API should not make raw pointers the portable work-item currency.

Use:

- `dpx_buffer_ref`

as an opaque handle.

Backend interpretation:

- GPU backend:
  - GPU allocation / VM binding / registered buffer object
- `DLB + DSA`:
  - host memory reference resolved for the transfer backend
- `eventdev + ioat`:
  - DMA-capable memory reference resolved through the backend adapter
- pure software:
  - host pointer or software-managed allocation

## Channel Model

A `dpx_channel` should carry:

- scheduling mode
- ordering contract
- priority class
- flow behavior
- preferred transfer backend
- success continuation
- failure continuation

The API should treat the channel as:

- the user-visible schedulable object

not:

- a raw firmware queue
- a raw DLB queue
- a raw eventdev queue

Suggested ordering contracts:

- `DPX_ORDER_TOTAL`
  - all items in the channel are serialized
- `DPX_ORDER_PER_FLOW`
  - items with the same flow ID are serialized
- `DPX_ORDER_NONE`
  - no ordering guarantee beyond backend-local completion reporting

## Work Item Model

A `dpx_work_item` should minimally carry:

- opcode
- source buffer ref
- destination buffer ref
- length / shape
- flow ID
- dependency handle(s)
- success continuation
- failure continuation
- backend-private attachment hook

The first draft does not need a heap-only work-item API.

It is acceptable, and probably cleaner, for the public API to treat
`dpx_work_item` as:

- a concrete struct the caller can fill
- submitted by pointer to a backend-owned queue

Helper constructors or builders can be layered on later if they improve
developer experience.

## Required Common API

Suggested required API surface:

```c
int dpx_backend_detect(uint32_t flags, dpx_backend_desc *out, size_t *count);
int dpx_backend_query_caps(const dpx_backend_desc *backend,
    dpx_capability_set *caps);

dpx_domain *dpx_domain_create(const dpx_domain_config *cfg);
void dpx_domain_destroy(dpx_domain *dom);

int dpx_domain_query_capabilities(dpx_domain *dom, dpx_capability_set *caps);

dpx_channel *dpx_channel_create(dpx_domain *dom, const dpx_channel_config *cfg);
void dpx_channel_destroy(dpx_channel *ch);
int dpx_channel_query_depth(dpx_channel *ch, dpx_depth_info *out);

int dpx_buffer_register(dpx_domain *dom, const dpx_buffer_desc *desc,
    dpx_buffer_ref *out);
int dpx_buffer_unregister(dpx_domain *dom, dpx_buffer_ref ref);

int dpx_channel_submit(dpx_channel *ch, const dpx_work_item *items,
    uint32_t count, uint32_t *submitted);

int dpx_completion_poll(dpx_domain *dom, uint64_t timeout_ns,
    dpx_completion_batch *out);
int dpx_error_poll(dpx_domain *dom, uint64_t timeout_ns,
    dpx_error_batch *out);
```

## Submission And Backpressure

The submit path must expose honest backpressure.

Recommended contract:

- submit may accept zero, some, or all requested work
- submit should be able to report `would block`
- applications must be prepared to retry or drain completions before retrying

This is necessary because different backends have different saturation points:

- GPU queue / ring depth
- `DLB` credits and queue depth
- `eventdev` queue / port pressure
- `DSA` or `ioat` transfer queue availability

So DPX should standardize:

- a portable backpressure result
- not a fake guarantee that every backend is effectively unbounded

## Domain Lifetime Rule

The API should document this explicitly:

- backend choice happens before `dpx_domain_create()`
- the chosen backend pair is fixed for that domain
- a domain must be drained or destroyed before moving to another backend
- `dpx_completion_poll()` and `dpx_error_poll()` are the portable way to drain
  in-flight state

## Optional API

These should be capability-gated, not globally required:

```c
int dpx_channel_set_preemption_policy(dpx_channel *ch,
    dpx_preemption_policy policy);
int dpx_channel_set_quantum(dpx_channel *ch, uint64_t quantum_ns);
int dpx_channel_set_group(dpx_channel *ch, uint32_t group_id);
```

These are most natural on GPU backends and possibly some hardware-event
backends, but not on pure software paths.

## Error Model

The error model should include both status and scope.

Suggested dimensions:

- status:
  - success
  - retryable
  - fatal
  - unsupported
- scope:
  - item-local
  - channel-local
  - transfer-engine-local
  - domain-local
  - whole-device

Without scope, upper software cannot tell whether to:

- retry one item
- rebuild one channel
- reinitialize the whole backend

## Telemetry Surface

If DPX is meant to become a premium feature, it needs observability.

At minimum the API should support querying:

- per-channel queue depth
- submission count
- completion count
- error count
- backpressure count
- completion latency distribution
- fallback / degraded-mode state

## Backend Adapter Contract

Every backend adapter should implement:

- domain init / destroy
- capability query
- channel create / destroy
- buffer registration / resolution
- work-item submit
- completion emission
- error emission
- telemetry query

Everything below that line is backend-private:

- GuC CT messages
- AMD MES packets
- DLB queue/port config
- DSA descriptor formatting
- IOAT submission and callback handling

## Product Rule

DPX is only product-grade if it can promise:

- one API surface
- explicit capability discovery
- honest degradation across backends
- measurable telemetry

It must not promise:

- identical latency
- identical throughput
- identical memory behavior

across radically different backends.

## Bottom Line

The strongest correct summary is:

> `dpx-api.md` should be the concrete contract that turns DPX from an elegant
> architecture note into an implementable premium runtime feature.

And:

> without capability discovery, buffer references, and an explicit adapter
> contract, DPX remains a strong design discipline but not yet a product-grade
> interface.
