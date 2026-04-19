# DPX R32: AMD MES/HWS + SDMA Mapping

Date: 2026-04-19

## Purpose

This note maps the abstract `DPX` model onto:

- `r32`
  - AMD RDNA3+ `MES/HWS + SDMA`

Primary local basis:

- `docs/dpx-programming-model.md`
- `docs/dpx-g32.md`
- `docs/r32-hws.md`

## Short Answer

`r32` is the strongest public GPU-side DPX target because AMD exposes the most
explicit scheduler model.

The strongest honest summary is:

> `r32` is a DPX backend where the scheduler side is unusually explicit:
> process/gang/queue/resource control is part of the published MES model,
> and SDMA is the natural transfer-stage backend.

## Why `r32` Fits DPX Well

The AMD MES specification already exposes most of the things DPX wants:

- scheduler-visible queue objects
- priority bands
- suspend / resume / reset
- queue mapping / unmapping
- process and gang state
- hardware resource setup
- query / health / log style control

This means DPX does not need to guess the scheduler worldview on `r32`.

## `r32` Object Mapping

The closest DPX mapping is:

| DPX object | `r32` mapping |
| --- | --- |
| DPX domain | MES-managed process/resource scope on one device |
| Task channel | queue within a gang, with gang/process context above it |
| Work item | user-queue work and associated scheduling context |
| Transfer stage | SDMA queue/work |
| Completion edge | fence / scheduler completion and queue-progress signals |
| Error domain | MES suspend/reset/hang-detect and host recovery flow |

Important caveat:

- DPX task channels are flatter than AMD's process/gang/queue hierarchy

So a good `r32` adapter should treat:

- process
- gang
- queue

as backend internals under a DPX-facing task-channel API.

## What `r32` Gives DPX That Other Backends Do Not

### 1. Published scheduler authority

AMD MES gives DPX a concrete scheduler packet/control vocabulary.

That includes visible commands for:

- resource setup
- add/remove queue
- scheduling config
- suspend/resume/reset
- root page-table update
- debug and misc operations

### 2. Visible process/gang structure

`r32` makes it possible to give the DPX backend richer grouping semantics:

- task-channel family
- process-level policy
- gang-level policy

without reverse-engineering hidden firmware behavior.

### 3. Strong scheduler log and debug story

The AMD scheduler log model is unusually valuable for DPX backend validation.

It can help answer:

- was the scheduler healthy
- did the queue map/unmap correctly
- did suspend/reset happen as expected

It also means the `r32` backend should expose richer backend-specific telemetry
than the common DPX surface requires:

- scheduler log snapshots
- queue state history
- event/interrupt history
- reset diagnostics

## What The DPX Adapter Must Preserve On `r32`

Do not over-flatten `r32`.

The adapter must preserve:

- the difference between process, gang, and queue
- queue priority / quantum semantics
- explicit reset/suspend flows
- resource programming that matters for scheduler behavior

If the adapter hides too much, it loses the main advantage of `r32`:

- the scheduler model is actually published

## What Should Stay Above The Backend

The upper DPX runtime can still stay generic for:

- task-channel lifecycle
- work-item DAGs
- transfer-stage declaration
- success/error continuations
- fallback selection

That means applications can still think in DPX terms even though the `r32`
backend owns a richer firmware structure under the hood.

## Recommended Implementation Stance

Treat `r32` as:

- the reference GPU backend with the richest visible scheduler contract

That means:

- let the backend adapter carry process/gang/queue mapping logic
- keep the DPX API task-channel centric
- allow optional backend-specific tuning for gang/process policy later

## Bottom Line

The strongest correct summary is:

> `r32` is a first-class DPX backend and probably the most explicit one.

And:

> its published MES scheduler model should be preserved by the backend adapter,
> not flattened away, even though the upper DPX runtime stays simpler than the
> raw AMD process/gang/resource hierarchy.
