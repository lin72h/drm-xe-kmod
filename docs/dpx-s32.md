# DPX S32: Intel Xe GuC + Blit Mapping

Date: 2026-04-19

## Purpose

This note maps the abstract `DPX` model onto:

- `s32`
  - Intel Xe Linux 6.12 host-visible `exec_queue + GuC submit + blit/copy`
    path

Primary local basis:

- `docs/dpx-programming-model.md`
- `docs/dpx-g32.md`
- `docs/s32-guc-submit.md`
- `docs/s32-vs-r32-hws.md`

## Short Answer

`s32` is a valid DPX backend, but it is a narrower and more host-driven one
than `r32`.

The strongest honest summary is:

> `s32` maps well to DPX because `xe_exec_queue` already looks like a natural
> task-channel object, but much more of the scheduler contract is implicit in
> host driver code and opaque GuC behavior.

## Why `s32` Fits DPX

The Xe Linux 6.12 driver already has the right broad shape for DPX:

- schedulable execution queues
- host-side jobs and fences
- per-queue policy
- firmware registration
- copy/blit engines
- completion and reset flow

That means DPX does not need to invent a queue abstraction on `s32`.
It can map directly onto the existing host-visible Xe queue model.

## `s32` Object Mapping

The closest DPX mapping is:

| DPX object | `s32` mapping |
| --- | --- |
| DPX domain | Xe device/runtime scope with VM and scheduler context |
| Task channel | `xe_exec_queue` |
| Work item | `xe_sched_job` / host submission object |
| Transfer stage | blit/copy engine work |
| Completion edge | fences, GuC notifications, and host scheduler completion |
| Error domain | timeout, ban, disable, deregister, GT reset flow |

This is the cleanest DPX-to-backend mapping in the current document set.

## What `s32` Gives DPX

### 1. A natural task-channel object

Unlike `r32`, where process/gang/queue layering is more visible, `s32` already
centers the host-visible contract on one main schedulable object:

- `xe_exec_queue`

That lines up naturally with the DPX task-channel idea.

### 2. Host-visible policy knobs

The GuC submission path exposes enough policy to support DPX semantics:

- priority
- execution quantum
- preemption timeout
- enable/disable
- submit / kick

### 3. Clear job and completion plumbing

The Linux Xe path already uses:

- jobs
- fences
- scheduler entities
- timeout / reset handling

So the completion-edge side of DPX has a real backend anchor.

### 4. Real scheduling distinctions that DPX should not ignore

The Xe backend also has queue distinctions that matter even if the first DPX
API version does not expose them directly:

- long-running queues
- preemption timeout policy
- backend-specific timeout/reset behavior

Those should be treated as backend capabilities, not assumed away.

## What `s32` Does Not Give DPX

### 1. A published scheduler database

`s32` does not expose an AMD-like public process/gang/resource scheduler API.

So the DPX adapter must not assume visibility into:

- full GuC arbitration
- full firmware queue database
- broad firmware-side resource programming

### 2. A broad public firmware packet surface

`s32` backend work is more adapter-heavy because the host/GuC contract is:

- real
- source-visible
- but narrower and less explicit than `r32`

### 3. Process-level scheduler objects

The main schedulable host object is the queue.
That is actually good for DPX, but it means the backend does not expose the
same grouping vocabulary as AMD MES.

## Recommended DPX Adapter Stance On `s32`

The right adapter rule is:

- treat `xe_exec_queue` as the DPX task channel
- treat jobs/fences as the primary completion path
- treat GuC registration and policy updates as backend internals
- keep opaque firmware arbitration out of the common DPX contract

This keeps the abstraction honest while still getting strong code reuse.

## Where `s32` Is Stronger Than `r32` For DPX

At the pure upper-runtime level, `s32` may actually be easier to map because:

- the schedulable host object is already queue-centric
- the host submission path is explicit in Linux source
- DPX task-channel semantics align well with `exec_queue`

So `s32` is weaker as a published scheduler spec, but stronger as a natural
task-channel backend.

## Bottom Line

The strongest correct summary is:

> `s32` is a first-class DPX backend whose best mapping is:
> DPX task channel = `xe_exec_queue`.

And:

> the backend must preserve that Xe is more host-driven and more opaque than
> AMD MES, instead of pretending the GuC contract is a published scheduler
> operating system.
