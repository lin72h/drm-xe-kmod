# Intel Xe on FreeBSD 6.12: Recent OPUS/GLM Findings

Date: 2026-04-19

## Purpose

This memo integrates the recent parent-agent OPUS/GLM briefing from:

- `../wip-gpt/docs/xe-agent-recent-findings.md`
- `../wip-gpt/docs/source-lx-s32.md`

It records only the parts that should shape the FreeBSD Xe port.
The DMO/DMI architecture material remains background and must not become a
dependency of this driver port.

## Boundary Reconfirmed

The Xe port remains:

- Linux 6.12 semantic baseline
- stock FreeBSD first
- `drm-kmod` / LinuxKPI shaped
- Phase 1.0 compatible only by staying FreeBSD-clean
- upstream-oriented and reviewable by FreeBSD DRM developers

The Xe port must not depend on:

- Mach IPC
- `memory_object`
- `mach_vm`
- DMO / DMI
- native GPU VM
- native fences

The port should report hardware and substrate evidence back to later DMO/DMI
planning. It should not consume that future architecture.

## Phase 0 Correction

The recent GLM review adds one important correction:

> The first hard problem is making `xe.ko` compile and link.

Compile/bootstrap blockers are now tracked separately in:

- [xe-compile-bootstrap-findings.md](xe-compile-bootstrap-findings.md)

## Highest-Value Runtime Finding: GET_HWCONFIG MMIO First, CT Second

The earlier CT-first wording needs one refinement:

- MMIO GuC messaging is the first transport proof
- GuC command transport is the first shared-memory runtime gate before
  submission

Do not start by proving submission.
First prove that the driver can:

1. load firmware
2. complete early MMIO communication with GuC, specifically the
   `GET_HWCONFIG` path in `xe_guc_hwconfig.c`
3. allocate and pin the CT buffer
4. exchange at least one H2G/G2H message with GuC

The smallest honest A380 GuC/CT path is:

1. GuC ABI headers under `drivers/gpu/drm/xe/abi/guc_*_abi.h`
2. `xe_uc_fw.c` for firmware load and validation
3. `xe_uc.c` and `xe_guc.c` for UC / GuC lifecycle
4. `xe_guc_ads.c` for ADS setup
5. `xe_guc_ct.c` for CT communication

`xe_guc_submit.c` is direct Xe DRM-lane porting work, but it should come after
CT is proven because it depends on `drm_sched`, `dma_fence`, exec queue
lifetime, GuC IDs, and Linux object ownership.

## Why MMIO And CT Matter

CT registration itself depends on earlier MMIO communication.
If early MMIO transport is wrong, CT will fail later for reasons that are easy
to misclassify.

The most important concrete MMIO proof in Linux 6.12 is:

- `xe_guc_hwconfig_init()` in `xe_guc_hwconfig.c`
- it sends `XE_GUC_ACTION_GET_HWCONFIG` through `xe_guc_mmio_send()`
- it also exercises a minimal managed BO plus GGTT target for the hwconfig copy

That means:

- CT is not the first transport primitive
- CT is still the first serious shared-memory runtime gate

`xe_guc_ct.c` allocates its CT buffer through the Xe BO path, using system
memory and GGTT pinning.

Therefore CT failure is not just a firmware issue.
It can expose:

- Xe BO allocation defects
- TTM/system-memory placement problems
- GGTT insertion or invalidation bugs
- DMA/cache-coherency issues
- firmware path or ABI mismatches
- IRQ or G2H dispatch problems

If CT fails because BO allocation, GGTT mapping, or invalidation is missing,
that is a FreeBSD DRM/TTM/LinuxKPI bring-up problem. It is not a DMO problem.

## Direct DRM-Lane Porting Work

The following paths are direct Linux 6.12 Xe porting work in
`drm-kmod-6.12`, even if they also teach later architecture lessons:

- `xe_uc_fw.c`
- `xe_uc.c`
- `xe_guc.c`
- `xe_guc_ads.c`
- `xe_guc_ct.c`
- `xe_guc_submit.c`
- `xe_bo.c`
- `xe_bo_evict.c`
- `xe_ttm_vram_mgr.c`
- `xe_ttm_sys_mgr.c`
- `xe_ttm_stolen_mgr.c`
- `xe_vram.c`
- `xe_migrate.c`
- `xe_vm.c`
- `xe_pt.c`
- `xe_ggtt.c`
- `xe_sa.c`
- `xe_irq.c`
- `xe_device.c`
- `xe_pcode.c`
- `xe_guc_pc.c`

Do not treat these files as native DMO/DMI architecture.
For this port, they remain Linux-shaped DRM driver code.

## Device And IRQ Cautions

`xe_device.c` is straightforward but depends heavily on Linux managed-resource
ordering.

Porting requirement:

- document the actual init/fini order as the FreeBSD port evolves
- verify `devm_*` and `drmm_*` cleanup ordering through forced attach failures
- keep module unload and detach testing early

`xe_irq.c` is also straightforward but depends on Linux IRQ and threaded-IRQ
assumptions.

Porting requirement:

- validate FreeBSD LinuxKPI IRQ bridging under WITNESS
- verify fence signaling from interrupt or completion context
- check that G2H/CT dispatch does not violate lock order

Additional verified init-path notes:

- `xe_oa_init(xe)` sits before `drm_dev_register()` in `xe_device.c`
- if OA is not ready for first bring-up, it needs a stub or staged exclusion
  that still lets registration proceed
- `xe_pcode_probe_early(xe)` is in the early device path and must be treated as
  more than an afterthought
- `xe_guc_pc` is part of the GuC/GT power-control path and brings workqueue and
  wait-path risk into early bring-up
- `xe_sa` suballocation sits under CT and ADS, so CT/ADS work also depends on
  the suballocator being alive

The parent kernel review adds one more practical warning:

- init/unwind behavior is the first place the port is likely to go wrong
- attach failure, unload, IRQ teardown, workqueue drain, and partial GT/UC init
  must be treated as first-class bring-up paths, not later hardening

## HMM / USM Remains Out Of First Bring-Up

The first bring-up must reject or disable:

- `DRM_XE_VM_BIND_OP_MAP_USERPTR`
- HMM-backed userptr
- fault-mode VM creation
- recoverable USM page-fault mode

The useful early VM test is explicit BO-backed VM_BIND with fault mode off:

1. create an Xe VM
2. allocate a system BO
3. bind the BO to a GPU VA
4. verify PTEs are populated with expected encoding
5. unbind and destroy

`xe_hmm.c` is Phase 3 reference material for this project, not a first-bring-up
dependency.

## User Fences As Evidence

`xe_wait_user_fence.c` and `xe_hw_fence.c` are worth studying early.

For the FreeBSD Xe port:

- keep `dma_fence`, `drm_syncobj`, and Linux UAPI compatibility
- verify interrupt-context fence signaling under WITNESS
- validate waitqueue wakeups in user-fence paths

For later DMO/DMI planning, record the hardware fact:

- GPU writes a monotonic value into GPU-visible memory
- CPU or userspace observes the value reaching a target
- kernel fence objects wrap that hardware writeback but are not the hardware
  truth

This is the Intel counterpart to the AMD `amdgpu_seq64.c` pattern.

## Bring-Up Ladder

Use this as the first detailed ladder:

1. imports build in `drm-kmod-6.12`
2. LinuxKPI gaps are classified, not patched blindly
3. `xe.ko` links without unresolved symbols
4. A380 PCI probe fires through `xe_pci.c`
5. MMIO BAR access works through `xe_mmio.c`
6. firmware load and validation works through `xe_uc_fw.c`
7. early MMIO communication with GuC succeeds through `GET_HWCONFIG`
8. UC / GuC lifecycle reaches a known ready/fail state
9. `xe_sa` suballocator setup succeeds
10. ADS setup succeeds
11. CT buffer allocation and GGTT pinning work
12. CT enable succeeds
13. one existing blocking CT round-trip succeeds
14. IRQ dispatch works without WITNESS violations
15. unload/reload leaves no leaked IRQ, workqueue, or devfs/render-node state
16. `xe_pcode` mailbox path behaves correctly
17. `xe_oa_init()` is stubbed or succeeds so registration can proceed
18. BO allocation/free works for system and local memory
19. GGTT insertion works for CT and simple objects
20. explicit BO-backed VM_BIND succeeds without HMM/userptr
21. simple exec queue create/destroy succeeds
22. basic job submission completes and fence signals
23. memory pressure / eviction smoke tests run without corrupting BOs

Every failure should be classified as one of:

- LinuxKPI gap
- FreeBSD VM/pager substrate issue
- FreeBSD DMA/IOMMU/busdma issue
- firmware/hardware contract issue
- DRM object-model assumption
- Xe-only bug

## Fast Falsification Tests

These tests are designed to disprove weak assumptions quickly:

1. `xe.ko` links without unresolved symbols. If it does not, the import and
   support surface are still incomplete.
2. Load `xe.ko`, probe A380, load GuC firmware, and complete MMIO
   `GET_HWCONFIG` without DMO. If this requires DMO, the project boundary is
   wrong.
3. Allocate the CT BO, GGTT-pin it, enable CT, and complete one existing
   blocking CT request/reply. If this fails, focus on BO/GGTT/TTM/LinuxKPI,
   MMIO, or IRQ/G2H before submission.
4. Unload and reload after the MMIO/CT path. If IRQ, workqueue, devm/drmm, or
   render-node state leaks, the unwind model is wrong.
5. Create a BO-backed VM_BIND path with fault mode disabled. If this requires
   HMM or MMU notifiers, the explicit-bind assumption is wrong.
6. Signal a `dma_fence` from the GPU completion or IRQ path under WITNESS. If
   this deadlocks or violates lock order, the LinuxKPI fence/IRQ bridge needs
   work.
7. Run BO allocation plus eviction under memory pressure. If BOs corrupt or
   become inaccessible, the TTM/FreeBSD VM integration is not ready.

## Tests To Mirror

Mirror Linux Xe test intent, not necessarily KUnit mechanics.

| Linux test | Project-level use |
| --- | --- |
| `tests/xe_bo.c` | reimplement BO placement and move checks in Zig |
| `tests/xe_migrate.c` | reimplement migration data-integrity checks in Zig |
| `tests/xe_pci.c` / `tests/xe_pci_test.c` | port directly if KUnit is available; otherwise expose as Elixir probe tests |
| `tests/xe_guc_db_mgr_test.c` | reimplement doorbell allocator behavior in Zig |
| `tests/xe_guc_id_mgr_test.c` | reimplement GuC ID allocator bounds in Zig |
| `tests/xe_wa_test.c` | preserve workaround application checks |
| `tests/xe_rtp_test.c` | preserve register table processing checks |

Skip for the first A380 bring-up:

- `tests/xe_guc_relay_test.c`
- `tests/xe_gt_sriov_pf_service_test.c`
- `tests/xe_lmtt_test.c`

Those are relay-specific, SR-IOV-specific, or too KUnit-specific for the first
FreeBSD A380 milestone.

## Report-Back Contract

As the Xe port advances, report these findings back to the main architecture
planning effort:

1. exact LinuxKPI gaps, classified as VM, DMA, PCI, firmware, locking, IRQ, or
   scheduling
2. whether GuC CT buffer allocation and GGTT pinning use TTM or direct
   allocation in the port
3. whether BO-backed VM_BIND works end-to-end without HMM/userptr
4. which parts of `xe_bo`, `xe_vm`, and `xe_pt` are hardware-format code versus
   Linux infrastructure
5. whether `dma_fence` signaling from interrupt context works under FreeBSD
   LinuxKPI and WITNESS
6. whether `wait_event` / `wait_event_timeout` wakes correctly in Xe fence wait
   paths
7. how Xe BO mmap faults are handled on FreeBSD: `ttm_bo_vm_fault`,
   `cdev_pager_ops`, or another bridge
8. how Xe behaves under memory pressure: `xe_bo_move()`, eviction, shrinker
   behavior, and active submissions
9. whether FreeBSD `bus_dma(9)` and IOMMU paths provide the scatter-gather and
   cache-attribute behavior Xe expects
10. which tests or selftests are worth translating into the project-level
    Elixir/Zig policy

Do not report DMO object shape, Mach port identity, or `memory_object` design as
if the Xe port owns those decisions.

## Do Not Copy Into DMO/DMI Core

The Xe agent may port these in the DRM lane, but they must not become native
DMO/DMI architecture:

- `drm_gpuvm` object model
- `drm_exec` / `ww_mutex` locking model
- TTM as pager
- `dma_resv` as native reservation state
- `dma_fence` as native fence identity
- GEM handles as native memory identity
- `xe_vm.c` object model
- `xe_bo` as the center of GPU memory
- `xe_hmm.c` / HMM / userptr as Phase 2 substrate

## Final Rule

Be Linux-accurate in the driver port, FreeBSD-native in substrate glue, and
architecture-neutral toward DMO/DMI.
