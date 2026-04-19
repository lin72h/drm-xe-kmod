# Intel Xe on FreeBSD 6.12: Red-Team Review

Date: 2026-04-19

## 1. Executive Judgment

The plan is technically sound and well-ordered. It follows proven FreeBSD DRM
porting precedent. The documentation quality is unusually strong for a pre-code
planning phase.

The two things most likely to cost real time:

1. The init/fini ordering between `devm_*`, `drmm_*`, `xe_device_probe`, and
   the 17+ subsystem init calls in `xe_device.c:547-740`. This is where "compiles
   clean, panics on unload" lives. No amount of planning substitutes for forced
   early-failure testing on real hardware.

2. The gap between LinuxKPI `ww_mutex` and WITNESS. This will not announce itself
   as a clean failure. It will appear as a WITNESS splat under multi-BO VM_BIND
   stress that looks like a false positive but may be a real ordering violation
   the Linux code happens to tolerate.

Overall: proceed. The staging is correct, the boundary between this port and
DMO/DMI is correctly maintained, and the documentation is ready for FreeBSD
review.

## 2. Major Flaws or Risks

### 2.1 `xe_oa_init` is in the critical path before `drm_dev_register`

The init sequence in `xe_device.c:700-740` is:

```
xe_display_init_noaccel(xe)    line 705 — returns 0 without CONFIG_DRM_XE_DISPLAY
xe_gt_init(gt)                 line 712 — per-GT, heavy
xe_heci_gsc_init(xe)           line 717 — void, self-cleaning
xe_oa_init(xe)                 line 719 — returns int, blocks on failure
xe_display_init(xe)            line 723 — returns 0 without CONFIG_DRM_XE_DISPLAY
drm_dev_register(&xe->drm, 0) line 727
```

`xe_oa_init` returns an error code and sits between HECI GSC and
`drm_dev_register`. If OA init fails, the driver never registers. The plan
mentions OA as deferrable, but the Linux code does not make it optional — it is
not gated behind a Kconfig or a `has_oa` flag. You will need either:

- a FreeBSD stub that makes `xe_oa_init` return 0 (acceptable precedent: i915
  OA was stubbed on FreeBSD)
- or a build-time exclusion of `xe_oa.c` with a header that stubs it out

This is not hard, but it is in the critical path and will block
`drm_dev_register` if missed.

### 2.2 The "display off" path is clean but needs explicit action

Without `CONFIG_DRM_XE_DISPLAY`, all display functions become inline stubs
returning 0 (`xe_display.h:46-69`). This is confirmed clean. However:

- `xe_device.c` includes `display/xe_display.h` unconditionally
- the `display/` directory must either be imported (with the stubs active) or
  the header path must resolve without it

For the FreeBSD build: either import the `display/` directory and exclude the
`.c` files from the Makefile, or create a local `display/xe_display.h` with
only the stub path. The former is cleaner and closer to upstream.

### 2.3 No mention of `xe_pcode`

`xe_pcode.c` handles mailbox communication with the SoC power controller. It
sits in the init path (`xe_gt_init` calls pcode). On VFs, pcode is skipped
(`skip_pcode = 1`), but on bare-metal DG2 it runs. Pcode uses
`wait_event_timeout` with a polling loop. If the pcode mailbox hangs on FreeBSD
because of a timer, wait-queue, or MMIO issue, GT init will stall silently.

Add `xe_pcode` to the runtime risk list, specifically the `wait_event_timeout`
+ MMIO polling interaction.

### 2.4 `xe_guc_pc` (GuC power control) is also in the GT init path

`xe_guc_pc.c` manages power/frequency through GuC. It runs during GT init and
uses workqueues, delayed work, and runtime PM callbacks. On FreeBSD with runtime
PM stubbed as always-on, the GuC PC init should mostly be safe, but the
workqueue interactions during module unload are a risk.

### 2.5 `xe_sa` (suballocator) is a dependency for CT and ADS

CT buffer and ADS buffer allocation goes through `xe_sa` (suballocator), which
allocates sub-regions from a larger BO. `xe_sa_bo_new()` creates the parent BO,
`xe_sa_bo_manager_init()` sets up the drm_suballoc manager. This is a thin
layer but it sits between CT/ADS and the BO path. Verify it works before
assuming CT BO allocation is straightforward.

## 3. Corrected Bring-Up Order

The proposed order is mostly correct. Corrections:

1. xe builds and loads — correct
2. A380 PCI match/probe — correct
3. MMIO BAR mapping — correct
4. VRAM probing — correct
5. **xe_sa (suballocator) init** — ADD. CT and ADS depend on it.
6. GuC firmware found and validated — correct
7. ADS setup — correct, uses xe_sa
8. CT buffer allocation and GGTT pinning — correct, uses xe_sa + xe_bo
9. CT H2G/G2H exchange — correct
10. **xe_pcode mailbox** — ADD. Part of GT init.
11. GT init and IRQ setup — correct
12. **xe_oa_init stub** — ADD. Must return 0 to reach registration.
13. drm_dev_register — correct
14. Render node appears — correct

The key additions are `xe_sa`, `xe_pcode`, and the `xe_oa` stub. Without
these three, the milestone ladder has silent gaps.

## 4. GuC CT Recommendation

### CT is the right first runtime gate

Confirmed. The reasoning is correct:

- CT buffer allocation exercises `xe_bo_create_pin_map` → TTM → system memory →
  GGTT insert
- CT communication exercises firmware ABI, DMA coherency, and IRQ dispatch
- CT failure cleanly separates memory/DMA bugs from submission bugs

### First H2G/G2H test

The first honest message is `GUC_ACTION_HOST2GUC_CONTROL_CTB` at
`xe_guc_ct.c:313-327`. This is the CT enable message:

```c
static int guc_ct_control_toggle(struct xe_guc_ct *ct, bool enable)
{
    u32 request[HOST2GUC_CONTROL_CTB_REQUEST_MSG_LEN] = {
        FIELD_PREP(GUC_HXG_MSG_0_ORIGIN, GUC_HXG_ORIGIN_HOST) |
        FIELD_PREP(GUC_HXG_MSG_0_TYPE, GUC_HXG_TYPE_REQUEST) |
        FIELD_PREP(GUC_HXG_REQUEST_MSG_0_ACTION,
                   GUC_ACTION_HOST2GUC_CONTROL_CTB),
        ...
    };
    int ret = xe_guc_mmio_send(ct_to_guc(ct), request, ARRAY_SIZE(request));
```

This is sent via MMIO (not CT itself), and the response comes back via MMIO
status register. It tells GuC to enable the CT channel. If this succeeds, CT
is live.

**But that is not the first true CT message.** It is an MMIO-based bootstrap.
The first true CT H2G message would be whatever the driver sends after CT is
enabled — typically GuC self-config KLVs or ADS notifications sent during
`xe_guc_ct_enable()`.

Practical recommendation: the first test milestone is `guc_ct_control_toggle`
returning 0. That proves:

- firmware loaded
- MMIO works
- GuC is alive and responding
- CT buffers are allocated and GGTT-pinned (required before enable)

The first G2H event is the GuC response to the first real H2G message after CT
enable. That exercises the interrupt path.

### What comes before CT

There is no smaller honest firmware transport proof. Before CT, there is only
MMIO-based `xe_guc_mmio_send()` which is used for bootstrap self-config and CT
enable. MMIO send itself proves firmware is alive, but CT proves the full BO →
GGTT → DMA → firmware → IRQ chain.

## 5. Userptr / HMM / Fault-Mode Recommendation

### Technically safe for first A380 bring-up

Confirmed. The analysis is correct:

- `xe_hmm.c` is only reached from userptr paths (verified: `xe_hmm.o` is gated
  by `CONFIG_HMM_MIRROR`)
- BO-backed `VM_BIND` does not route through HMM
- `DRM_XE_VM_CREATE_FLAG_FAULT_MODE` requires `has_usm` at `xe_vm.c:1736` —
  gate this to always-reject on FreeBSD

### Hidden path question

There is no hidden path where BO-backed explicit VM_BIND requires HMM, MMU
notifiers, or GPU page-fault machinery. The bind path for BO-backed VMA
operations goes through `xe_vm_bind_ioctl` → `vm_bind_ioctl_ops_parse` →
`xe_vma_ops_execute` → `xe_pt_update_ops_run`. None of these call into
`xe_hmm_*`.

The one subtle dependency: `xe_vm.c` includes `xe_hmm.h` and references
`xe_hmm_userptr_populate_range()` in the userptr VMA case of
`xe_vma_userptr_check_repin()`. When userptr is rejected at the ioctl level,
these code paths are dead. But the build still needs `xe_hmm.h` to exist.
Either stub the header or conditionally compile the userptr branches.

### `has_usm` gate

DG2 advertises `has_usm = 1` in `xe_pci.c:149`. The plan correctly identifies
this. The recommended gate:

```c
static inline bool xe_vm_fault_mode_supported(struct xe_device *xe)
{
    return false; /* FreeBSD: no MMU notifier, no HMM, no fault mode */
}
```

Use this in the `VM_CREATE` ioctl to reject `FLAG_FAULT_MODE`. Do not remove
`has_usm` from the device info — userspace may query it. Just reject the
feature at the ioctl gate.

## 6. Display and HECI GSC Recommendation

### Display

Non-display Xe can reach `drm_dev_register()` cleanly. The display stubs
are confirmed inline no-ops at `display/xe_display.h:46-69`. Without
`CONFIG_DRM_XE_DISPLAY`:

- `xe_display_init_noaccel()` returns 0
- `xe_display_init()` returns 0
- `xe_display_register()` is a no-op
- all IRQ and PM display paths are no-ops

The render node will appear. Display is not entangled in the non-display init
path. This is clean.

Build recommendation: import the `display/` directory but do not compile any
`display/*.c` files. Keep only the header with stubs.

### HECI GSC

Safe to stage. Confirmed:

- `xe_heci_gsc_init()` returns void (`xe_heci_gsc.c:174`)
- on failure, calls `xe_heci_gsc_fini()` to clean up (`xe_heci_gsc.c:215`)
- has an early-return guard: `if (!HAS_HECI_GSCFI(xe) && !HAS_HECI_CSCFI(xe))
  return;` (`xe_heci_gsc.c:180`)
- for DG2: uses `heci_gsc_def_dg2` (`xe_heci_gsc.c:189`)
- requires `auxiliary_bus` and `mei_aux` — both dummy on FreeBSD

For the first A380 milestone: make `xe_heci_gsc_init` a no-op stub on FreeBSD.
Either:

- `#ifdef __FreeBSD__ return; #endif` at the top (ugly but precedented)
- or make the auxiliary_bus/mei_aux registration functions return `-ENODEV`,
  which will trigger the `goto fail` → `xe_heci_gsc_fini` path naturally

The second approach is cleaner because it does not hide the failure — it logs
it and continues.

**Hidden dependency check:** After HECI GSC, `xe_oa_init` and `xe_display_init`
run. Neither depends on GSC having succeeded. The GSC path mainly enables:

- HuC authentication via GSC proxy
- GSC firmware loading for media protection

On A380 without GSC: HuC authentication will fail or be skipped, which means
protected media content decryption will not work. This is acceptable for the
first milestone. No other init path depends on GSC success.

## 7. Runtime Semantic Risk Ranking

Reordered by actual FreeBSD bring-up risk, with additions:

### Tier 1: Will hit during first probe/attach

1. **`devm_*` / `drmm_*` cleanup ordering** — PROMOTED. This is the first
   thing that fails on attach error or module unload. FreeBSD LinuxKPI
   managed-resource ordering must be LIFO and must match the 17+ subsystem
   init calls in `xe_device.c`. One wrong cleanup order = use-after-free.

2. **firmware path mapping** — FreeBSD firmware location differs from Linux.
   The very first GuC firmware request will fail if the path is wrong. This is
   the easiest to test and the earliest blocker.

3. **PCI BAR / VRAM mapping / cache attributes** — DG2 VRAM probing and BAR
   mapping run before firmware. Wrong cache attributes on BAR mappings
   (WC vs UC vs WB) corrupt VRAM and MMIO silently.

4. **IRQ / MSI-X setup** — `xe_irq.c` runs during early init. FreeBSD MSI-X
   allocation and interrupt handler registration differ from Linux. If IRQ
   setup fails, no G2H, no fence signaling, nothing works.

### Tier 2: Will hit during CT and first runtime

5. **GuC CT buffer coherency** — the CT buffer must be DMA-coherent. If
   `bus_dma` or TTM allocation does not produce a coherent mapping, CT messages
   will silently corrupt. This looks like "firmware is broken" when it is
   actually a DMA/cache bug.

6. **`wait_event_timeout` and signal handling** — used by pcode, CT, and
   fence waits. If timeout conversion or signal restart differs, init stalls.

7. **workqueue flush/cancel during unload** — `xe_device_remove` must flush
   all workqueues before teardown. Self-rescheduling work items
   (GuC CT receiver, GT workers) can race with cleanup.

### Tier 3: Will hit during BO/VM/submission

8. **`ww_mutex` / WITNESS / `drm_exec`** — multi-BO locking. WITNESS may
   report false lock-order violations that are actually correct ww_mutex
   wound/wait behavior. Distinguishing real bugs from false positives is the
   hard part.

9. **`dma_fence` signaling context** — fence signal from IRQ context must
   not violate FreeBSD's non-sleepable context rules. Linux has
   `dma_fence_begin_signalling` / `end_signalling` annotations that FreeBSD
   LinuxKPI may not enforce.

10. **TTM eviction under memory pressure** — TTM's shrinker and LRU-based
    eviction interact with FreeBSD VM pressure through LinuxKPI. If the
    shrinker callback does not fire or fires at the wrong time, eviction fails
    silently.

11. **`drm_sched` timeout and recovery** — job timeout triggers GPU reset.
    The reset path must quiesce all queues, signal fenced jobs as error,
    and restart. One missed fence signal = deadlock.

### Tier 4: Will hit during stress / advanced features

12. **runtime PM** — always-on stub is correct for now, but any code path
    that calls `pm_runtime_get_sync` and expects it to actually power-gate
    will behave differently.

13. **`dma_fence_chain` / `dma_fence_array`** — timeline syncobj and
    multi-fence operations. These exist in the 6.12 lane but may have
    subtle behavioral differences.

14. **`iosys-map` VRAM BAR access** — bulk VRAM copies through iosys-map
    may fail where small MMIO reads succeed.

15. **sync_file fd lifecycle** — fd-based explicit fencing. UAPI-facing.
    Needs correct fd create/close semantics.

16. **`drm_gpuvm` range tracking under concurrent VM_BIND** — interval
    tree correctness under concurrent operations.

### Missing risks not in the original list

17. **`xe_pcode` mailbox hang** — `wait_event_timeout` + MMIO polling
    during GT init. Can stall indefinitely if timeout behavior differs.

18. **`xe_sa` suballocator** — thin layer between CT/ADS and BO allocation.
    If sub-allocation alignment or mapping is wrong, CT fails.

19. **`dma_map_sgtable` / scatter-gather DMA mapping** — BO DMA mapping
    uses `dma_map_sgtable`. FreeBSD LinuxKPI must produce valid bus
    addresses for GPU access.

20. **GGTT invalidation ordering** — GGTT updates must complete before the
    GuC reads CT buffers. Missing TLB invalidation or fence = firmware reads
    stale addresses.

## 8. Patch-Series Structure

### Phase 0: freebsd-src-drm-6.12 prerequisites (before any Xe code)

```
01  linuxkpi: Add missing xe firmware path helpers
02  linuxkpi: Ensure iosys-map memcpy_toio handles WC correctly
03  linuxkpi: Add dev_err_probe variants if missing
04  linuxkpi: Verify dma_map_sgtable produces valid bus addresses
05  linuxkpi: Ensure wait_event_timeout returns Linux-compatible values
```

These should be independently reviewable and useful to other DRM drivers.

### Phase 1: drm-kmod-6.12 Xe scaffolding

```
06  drm-kmod: Unhide xe_drm.h from drmgeneratepatch exclusion list
07  drm-kmod: Import include/uapi/drm/xe_drm.h from Linux 6.12
08  drm-kmod: Import include/drm/xe_drm.h internal header
09  drm-kmod: Add xe/ directory structure and top-level Makefile entry
10  drm-kmod: Import xe ABI headers (abi/guc_*_abi.h)
```

### Phase 2: drm-kmod-6.12 non-display Xe core import

```
11  drm/xe: Import PCI, device, tile, step, MMIO core
12  drm/xe: Import GT, topology, PAT, register SR, workarounds
13  drm/xe: Import BO, TTM managers (vram, sys, stolen), VRAM discovery
14  drm/xe: Import GGTT, suballocator, migration
15  drm/xe: Import UC, UC firmware, GuC core, GuC ADS
16  drm/xe: Import GuC CT, GuC doorbell manager, GuC ID manager
17  drm/xe: Import IRQ, wait_user_fence, hw_fence, preempt_fence
18  drm/xe: Import VM, PT, PT walk, TLB invalidation, page fault
19  drm/xe: Import exec queue, sched job, GPU scheduler, exec, sync
20  drm/xe: Import display stubs (header-only, no display .c files)
```

### Phase 3: drm-kmod-6.12 FreeBSD adaptation

```
21  drm/xe: Add FreeBSD module registration (xe_freebsd.c)
22  drm/xe: Stub xe_oa_init/fini for FreeBSD
23  drm/xe: Stub xe_heci_gsc for FreeBSD (return early or -ENODEV)
24  drm/xe: Gate MAP_USERPTR with -EOPNOTSUPP on FreeBSD
25  drm/xe: Gate fault-mode VM creation on FreeBSD
26  drm/xe: Stub xe_hmm when CONFIG_HMM_MIRROR is not set
27  drm/xe: Stub devcoredump and relay for FreeBSD
28  drm/xe: Add FreeBSD Makefile and kmod build rules
```

### Patch split: what goes where

Put in `freebsd-src-drm-6.12` (generic):

- LinuxKPI `wait_event_timeout` fixes
- LinuxKPI `iosys-map` / WC correctness
- LinuxKPI firmware path helpers
- LinuxKPI `dma_map_sgtable` validation
- any `devm_*` / `drmm_*` ordering fixes
- any `ww_mutex` or `drm_exec` LinuxKPI corrections

Put in `drm-kmod-6.12` (Xe-specific):

- everything in phases 1-3 above
- all Xe-local stubs, gates, and build plumbing

**What FreeBSD developers would reject if placed in the wrong tree:**

- Xe-specific hacks in `freebsd-src-drm-6.12` (e.g., `#ifdef XE` in LinuxKPI)
- generic LinuxKPI fixes in `drm-kmod-6.12` (e.g., fixing `wait_event_timeout`
  inside the xe/ directory instead of in sys/compat/linuxkpi)
- any Mach/DMO/DMI references in either tree

## 9. Linux A/B Data Checklist

Capture from Rocky Linux 10.1 with A380 before any FreeBSD debugging:

### System identification

```
uname -a
cat /etc/os-release
rpm -q kernel-core kernel-modules linux-firmware
```

### PCI and BAR

```
lspci -nn -vv -s <A380_BDF>
# Capture: device ID, BAR sizes, BAR types (mem/io), prefetchable,
# 64-bit, MSI-X capability, IOMMU group, power state
setpci -s <A380_BDF> COMMAND
# Check memory-space enable, bus-master enable
```

### Resize BAR

```
lspci -vv -s <A380_BDF> | grep -i rebar
dmesg | grep -i rebar
# Capture: whether resize-BAR is enabled, what BAR sizes were requested
# vs granted
```

### Module and firmware

```
modinfo xe
modprobe xe
lsmod | grep xe
# Capture: module version, firmware files, module parameters

ls -la /lib/firmware/xe/
sha256sum /lib/firmware/xe/dg2_*
# Capture: exact firmware filenames and checksums
```

### DRM device

```
ls -la /dev/dri/
# Capture: card*, renderD* nodes
cat /sys/class/drm/card*/device/vendor
cat /sys/class/drm/card*/device/device
```

### dmesg filtering

```
dmesg -T | grep -iE 'xe |drm|guc|huc|gsc|firmware|CT|ggtt|vram|bar|msi|irq|gt[^a-z]|tile|pcode|oa|display' > dmesg-xe-a380.log
```

Key milestones to extract from dmesg:

- PCI probe: `[drm] Registered xe ...`
- VRAM size and BAR: `[drm] VRAM: ... BAR: ...`
- firmware version: `[drm] GuC firmware ...` and `[drm] HuC firmware ...`
- GuC CT: `[drm] GuC CT ...` or any CT-related message
- GT init: `[drm] GT0: ...`
- IRQ: any MSI-X or IRQ allocation message
- pcode: any pcode-related message
- display: any display/connector message (to know what non-display path skips)
- DRM registration: `[drm] Registered ...`

### sysfs / debugfs

```
cat /sys/module/xe/parameters/*
cat /sys/kernel/debug/dri/*/info  # or drm_info if available
cat /sys/kernel/debug/dri/*/gt0/uc/guc_info
cat /sys/kernel/debug/dri/*/gt0/uc/huc_info
ls /sys/kernel/debug/dri/*/gt0/
```

### GuC and GT state

```
cat /sys/kernel/debug/dri/*/gt0/uc/guc_ct_selftest  # if available
cat /sys/kernel/debug/dri/*/gt0/pcode_status  # if available
cat /sys/kernel/debug/dri/*/gt0/topology
```

### IRQ and MSI-X

```
cat /proc/interrupts | grep xe
# Capture: IRQ numbers, CPU affinity, interrupt counts
```

### IOMMU

```
dmesg | grep -i iommu
ls /sys/kernel/iommu_groups/*/devices/
# Capture: whether A380 is in its own IOMMU group
```

### Minimal render test (only after all above captured)

```
# If Mesa supports the A380:
vulkaninfo --summary 2>&1 | head -30
# Or:
drm_info  # if available
```

Store all output in `../wip-drm-xe-artifacts/rocky-a380-baseline/`.

## 10. Test Strategy Corrections

### Elixir/Zig and upstream friction

Elixir and Zig will create zero friction if kept project-owned and never
proposed for FreeBSD upstream.

For upstream-facing tests, use:

- shell scripts (simplest, most portable)
- Python (already used by some DRM testing infrastructure)
- ATF/kyua (FreeBSD-native)
- existing drm-kmod patterns (if any test infrastructure exists)

The Elixir/Zig split is correct for project-level testing. The important
discipline is: anything intended for FreeBSD upstream review uses FreeBSD
conventions. Project-owned lab tooling uses whatever works best.

### Test mirroring corrections

The proposed list is correct. Two additions:

- **`tests/xe_args_test.c`**: argument parsing and validation. Easy to mirror,
  catches ioctl input validation bugs. Add to Zig mirrors.
- **`tests/xe_dma_buf.c`**: dma-buf export/import. Important for inter-device
  sharing. Add to Zig mirrors after basic BO works.

### Missing test category

**Load/unload stress test**: load `xe.ko`, probe A380, unload `xe.ko`, repeat
50 times. This is the single most important test for catching `devm_*`/`drmm_*`
cleanup bugs, workqueue race conditions, and leaked resources. Write it in
Elixir (orchestration) and run it before any submission tests.

## 11. FreeBSD Upstreaming Concerns

### Anticipated objections

1. **"Why not wait for upstream drm-kmod to add Xe?"** — answer: this is the
   work to enable that. The patches are structured for drm-kmod review.

2. **"The LinuxKPI changes should go through the linuxkpi maintainer."** — yes.
   Phase 0 patches must be reviewed independently of the Xe import. Submit
   them first and let them land before the Xe series.

3. **"This is a lot of new code."** — inevitable for a 73K-line driver. The
   import should be presented as "Linux 6.12 Xe non-display core with minimal
   FreeBSD adaptation" so reviewers know the delta is small.

4. **"What about display?"** — explicitly deferred. The first series is
   render-only. Display is a separate, later series.

5. **"Does it work?"** — the first series should be accompanied by bring-up
   logs from real A380 hardware showing at least PCI probe + firmware load +
   GuC CT. Ideally through `drm_dev_register`.

6. **"What about licensing?"** — all Xe code is GPL-2.0 with MIT headers. It
   belongs in `drm-kmod` which already carries GPL DRM code. LinuxKPI additions
   in `freebsd-src` should be BSD-compatible.

### Patch presentation

For upstream review, present in this order:

1. LinuxKPI fixes (small, independently useful, generic)
2. Xe UAPI and header import (small, mechanically verifiable)
3. Xe core import (large but mostly unchanged from Linux 6.12)
4. FreeBSD adaptation layer (small, where all local changes live)
5. Test results and hardware logs (proves it does something real)

## 12. Concrete Next Actions Before Coding

1. **Capture the Rocky Linux A380 baseline** — run the data checklist from
   section 9 on real hardware. This is the reference for every future
   comparison. Do this first.

2. **Audit `xe_device.c` init/fini ordering** — document every init call from
   `xe_device_probe_early` through `xe_device_probe` and the corresponding
   cleanup path. Identify which cleanup functions exist, which are `devm_*`
   managed, and which are manual.

3. **Audit LinuxKPI `wait_event_timeout` behavior** — write a minimal test
   that exercises timeout, signaled-before-timeout, and interrupted cases.
   Compare against Linux behavior. If they differ, fix LinuxKPI before
   importing Xe.

4. **Verify firmware path mapping** — confirm that FreeBSD's firmware loading
   can find `xe/dg2_guc_*.bin` by the names Linux uses. If paths differ,
   document the mapping.

5. **Check `dma_map_sgtable` and scatter-gather** — verify FreeBSD LinuxKPI
   produces valid bus addresses from `dma_map_sgtable`. If this is broken,
   nothing DMA-related works.

6. **Prototype the xe_freebsd.c module registration** — write the minimal
   FreeBSD kmod wrapper: module_init, module_exit, PCI ID table, module
   dependencies (drmn, ttm, dmabuf, linuxkpi, firmware). Do not write
   a toy probe driver. Write the real module shell.

7. **Import xe_drm.h** — remove it from the drmgeneratepatch exclusion list
   and import it. This unblocks all UAPI work.

8. **Start the non-display import** — phases 1-3 from the patch structure in
   section 8. Do not cherry-pick files. Import by subsystem in the order
   listed.

9. **Try to build** — the first build attempt will produce hundreds of errors.
   Classify them as: missing LinuxKPI header, missing LinuxKPI function, missing
   Kconfig gate, display dependency, or Xe-local issue. This classification IS
   the work — it produces the LinuxKPI gap list for the report-back contract.

10. **Attempt A380 PCI probe** — load the module on real hardware with the
    A380 installed. Even if it panics at MMIO, the PCI probe log is the first
    concrete evidence.

## Answers to the 20 Questions

**1. Biggest architectural or staging mistake?**

The plan does not account for `xe_oa_init` blocking `drm_dev_register`, does
not mention `xe_pcode`, and does not mention `xe_sa`. These are not
architectural mistakes — they are staging gaps that will cause confusion when
the first real build reaches the init sequence. Add them to the milestone
ladder.

**2. Is Linux 6.12 the right baseline?**

Yes. It is LTS, matches FreeBSD's 6.12 DRM lane, and has A380/DG2 support.
Post-6.12 fixes (especially for B580/BMG) should be explicit backports.

**3. Is the patch split correct?**

Yes. Generic LinuxKPI in `freebsd-src-drm-6.12`, Xe-specific in
`drm-kmod-6.12`. The split is conventional and matches AMDGPU/i915 precedent.

**4. Is GuC CT the right first runtime gate?**

Yes. The first test should be `guc_ct_control_toggle` returning 0 via MMIO
send, proving firmware is alive and CT buffers are allocated/pinned. The first
G2H is the response after CT enable.

**5. Is `xe_guc_submit.c` correctly delayed?**

Yes. It depends on `drm_sched`, exec queues, dma_fence, and GuC IDs — all of
which should be proven separately after CT works.

**6. Is userptr/HMM/fault-mode deferral safe?**

Yes. BO-backed explicit VM_BIND does not route through HMM. Verified at source
level.

**7. Is BO-backed VM_BIND viable without HMM?**

Yes. No hidden HMM dependency in the explicit-bind path. `xe_hmm.c` is only
called from userptr VMA paths.

**8. Can non-display Xe reach `drm_dev_register()`?**

Yes. Without `CONFIG_DRM_XE_DISPLAY`, all display init functions are inline
stubs returning 0. The path from GT init → HECI GSC (void) → OA (stub needed)
→ display (stub) → `drm_dev_register` is clean.

**9. Can HECI GSC be staged?**

Yes. `xe_heci_gsc_init` returns void, self-cleans on failure, and no
downstream init depends on its success. Make auxiliary-bus/mei_aux registration
fail with `-ENODEV` and the natural error path runs.

**10. Most likely compile-clean runtime-wrong?**

1. `devm_*`/`drmm_*` cleanup ordering (use-after-free on unload)
2. `wait_event_timeout` returning wrong values (init stalls)
3. `dma_map_sgtable` producing wrong bus addresses (silent DMA corruption)
4. CT buffer cache attributes wrong (GuC reads garbage)

**11. Which risks audit before code?**

`wait_event_timeout` behavior, firmware path mapping, `dma_map_sgtable`
correctness, and `devm_*` action ordering. These four are testable without
Xe code and affect every later step.

**12-14. Covered in section 8 patch structure.**

**15. FreeBSD developer objections?**

Covered in section 11. The biggest real risk is submitting LinuxKPI fixes and
Xe import as one giant series. Split them.

**16. Rocky Linux A/B data?**

Covered in section 9. Capture everything before writing FreeBSD code.

**17. Fast falsification tests correct?**

Yes, with one addition: add a load/unload cycle test between tests 1 and 2.
If the module crashes on unload, cleanup ordering is wrong and everything
downstream is unreliable.

**18. Elixir/Zig mirroring correct?**

Yes, with two additions: `xe_args_test` and `xe_dma_buf` tests. Both are
useful and easy to mirror.

**19. Weak or overconfident assumptions?**

- The assumption that GGTT invalidation "just works" is overconfident. GGTT
  TLB invalidation on FreeBSD may need explicit attention.
- The assumption that `devm_*` ordering is LIFO is correct but needs
  verification — FreeBSD LinuxKPI managed resources may not guarantee strict
  LIFO.
- The assumption that `xe_sa` (suballocator) is transparent is overconfident.
  It is a dependency for CT and ADS that is not in the milestone ladder.

**20. What should be done next before coding?**

Capture the Rocky Linux A380 baseline (section 9, action 1). Everything else
depends on having a Linux reference to compare against.
