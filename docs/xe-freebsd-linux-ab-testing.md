# Intel Xe on FreeBSD 6.12: Linux A/B Testing Strategy

Date: 2026-04-18

## Purpose

This memo defines the Linux-side reference environment for Xe bring-up.

The goal is not to make Linux the target.
The target remains stock FreeBSD first.
The goal is to use a Linux 6.12-class system as an operational oracle while
porting the same driver generation into FreeBSD's DRM/LinuxKPI stack.

Test implementation follows the project testing policy:

- existing tests stay in their native frameworks and must pass
- new performance-insensitive A/B harnesses should be written in Elixir
- new performance-sensitive or low-level probes should be written in Zig

See:

- [xe-freebsd-testing-policy.md](xe-freebsd-testing-policy.md)

## Why Rocky Linux 10.x Matters

Rocky Linux 10.x is useful for this project because it gives us a maintained
enterprise Linux environment on the same kernel generation as the pinned DRM
baseline.

Published baseline facts:

- Rocky Linux 10.0 shipped with kernel `6.12.0-55.14.1`.
- Rocky Linux 10.1 shipped with kernel `6.12.0-124.8.1`.
- RHEL 10.0 is documented as distributed with kernel version `6.12.0`.
- RHEL 10.1 is listed with kernel `6.12.0-124.8.1.el10_1`.

Sources:

- Rocky Linux release notes:
  `https://docs.rockylinux.org/release_notes/`
- Red Hat Enterprise Linux 10.0 release notes:
  `https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/10/html-single/10.0_release_notes/index`
- Red Hat Enterprise Linux release-date and shipped-kernel table:
  `https://access.redhat.com/articles/red-hat-enterprise-linux-release-dates`

This reinforces the 6.12 pin:

- `../nx/linux-6.12` remains the source-semantic baseline.
- Rocky/RHEL 10.x becomes the operational Linux reference.
- FreeBSD's 6.12 DRM lane remains the integration target.

The important distinction is that Rocky/RHEL 10 kernels are not guaranteed to
be byte-for-byte upstream Linux 6.12.
They are enterprise kernels with their own patch streams.
That is acceptable for testing, but any behavior difference must be classified:

- upstream Linux 6.12 behavior
- 6.12.y LTS behavior
- Rocky/RHEL downstream behavior
- FreeBSD port behavior

Do not import Rocky/RHEL-specific behavior blindly.
Use it to diagnose and then trace the real source of the behavior.

## Test Topologies

### 1. Native side-by-side boot

This is the cleanest A/B comparison.

Use the same physical machine and A380, booting either:

- FreeBSD with the developing Xe port
- Rocky Linux 10.x with its 6.12-class kernel and Xe driver

Advantages:

- closest hardware equivalence
- no hypervisor layer
- best signal for PCI, BAR, firmware, interrupts, reset, VRAM, and GT init

Disadvantages:

- slower iteration because changing sides requires rebooting
- FreeBSD crash/debug loops may interrupt the Linux reference workflow

Use this as the authoritative comparison for hardware behavior.

### 2. Rocky Linux 10.x under bhyve with A380 passthrough

This is useful for faster reference testing when the FreeBSD host can reserve
the A380 for passthrough.

The FreeBSD Foundation kernel-development workflow describes PCI passthrough
as exporting a PCIe device to a VM so the guest gets direct access to the
device. The device is claimed by the host `ppt` driver and appears inside the
VM behind the VM's PCIe root complex.

Source:

- FreeBSD Foundation, "FreeBSD Kernel Development Workflow":
  `https://freebsdfoundation.org/our-work/journal/browser-based-edition/development-workflow-and-ci/freebsd-kernel-development-workflow/`

Advantages:

- FreeBSD remains the host and log collection environment
- Rocky Linux can provide a quick Linux-side reference without replacing the
  whole host OS
- VM reboot and log capture can be faster than full native reboot cycles

Constraints:

- the A380 cannot be simultaneously owned by host Xe and the Rocky guest
- passthrough requires IOMMU and correct `ppt` ownership on the host
- device reset, power management, MSI/MSI-X, BAR placement, and firmware paths
  may differ from native boot
- a bhyve-only failure is not automatically a Xe/Linux driver failure

Use bhyve passthrough as a strong secondary reference, not as the final
hardware truth.

### 3. Linux guest without A380 passthrough

This is not useful for Xe hardware bring-up.

It can test userspace packaging, log collection scripts, or build tooling, but
it cannot validate:

- PCI probe
- firmware loading on the real GPU
- MMIO or GT init
- IRQ behavior
- VRAM aperture handling
- render-node creation for the A380

## Hardware Stance

Phase 1 baseline machine layout:

- Alder Lake iGPU stays as the stable host/display GPU
- Arc A380 is the Xe test device

For native FreeBSD testing:

- Alder Lake remains on `i915`
- A380 is claimed by the developing `xe` driver

For Rocky native testing:

- boot Rocky Linux 10.x on the same machine
- verify the A380 is claimed by Linux `xe`
- capture Linux logs as the reference

For bhyve Linux testing:

- FreeBSD host keeps the Alder Lake iGPU for console/display
- A380 is reserved for passthrough with `ppt`
- Rocky Linux guest claims the A380 with Linux `xe`

Do not treat the bhyve topology as simultaneous A/B ownership.
It is fast switching under a FreeBSD host, not shared ownership of the same
GPU.

## Phase 1.5 Hardware Stance

After the first A380 milestone is stable, the roadmap should add a Phase 1.5
lab move:

- transition to a lab system that actually provides two Xe-managed devices
- keep the external Arc A380 installed
- use that machine as the first dual-Xe host topology

For this phase, "stable" should mean at least:

- A380 probe, firmware load, CT, and `drm_dev_register()` work
- BO allocation, GGTT pinning, and basic VRAM access work
- at least one BO-backed VM_BIND succeeds
- at least one GuC submission completes and a fence signals
- module load/unload does not leak or panic across 50 cycles

Hardware warning:

- a normal Alder Lake iGPU remains `i915` and does not count as dual-Xe
- if the planned Phase 1.5 machine is really `i915 + Xe`, it is still a Phase
  1-style topology
- if no internal Xe-managed platform is available, use another two-Xe topology
  such as `A380 + B580` or `A380 + A380`

The Phase 1.5 purpose is:

- validate internal-plus-external Xe coexistence
- validate per-device attach, firmware, CT, GT, and render-node behavior
- catch topology, PCI, IOMMU, and teardown bugs that do not appear on the
  simpler Phase 1 host layout
- keep cross-device sharing and display questions out of scope

The Phase 1.5 purpose is not:

- automatic display enablement
- cross-device BO sharing or PRIME/dma-buf
- cross-device VM sharing
- replacing the Linux 6.12 baseline
- skipping the A380-first milestone

For native FreeBSD Phase 1.5 testing:

- the internal Xe-managed device and the external A380 should both be visible
  to the FreeBSD host
- logs should be captured per device as well as per boot
- any attach or unload test on one device should note the state of the other
- display should remain disabled on both devices

For native Rocky Linux Phase 1.5 testing:

- boot the same dual-Xe machine into Rocky Linux 10.x
- verify both Xe devices bind to Linux `xe`
- capture enumeration order, firmware versions, and render-node ordering for
  both devices

For bhyve during Phase 1.5:

- keep bhyve as a secondary diagnostic tool, not the primary multi-Xe truth
- if one device is reserved for passthrough, record which device remains with
  the host and which moves to the guest
- do not confuse guest-visible partial topology with native dual-Xe behavior

## Linux Reference Capture

For every meaningful Linux A380 run, capture at least:

- `uname -a`
- `/etc/os-release`
- `rpm -q kernel-core kernel-modules linux-firmware`
- `modinfo xe`
- `lspci -nn -vv -s <A380_BDF>`
- `dmesg -T` filtered for `xe`, `drm`, `guc`, `huc`, `gsc`, `dg2`, `a380`,
  `firmware`, `msi`, and `irq`
- `/dev/dri` node listing
- `/sys/module/xe/parameters/*`
- `/sys/kernel/debug/dri/*` where debugfs is available
- `drm_info` output where available
- `vainfo`, `vulkaninfo`, or a minimal render test when the driver reaches
  usable execution

For Phase 1.5 dual-Xe runs, also capture:

- `lspci -nn -vv` for both the internal Xe-managed device and the external
  A380
- `/dev/dri` node ownership and numbering for both devices
- per-device `sysfs` and `debugfs` data where Linux exposes it
- a note showing which device owns display responsibilities, if any
- attach and unload logs that show whether one device's teardown perturbs the
  other
- evidence that both devices reported correct BAR sizes and distinct VRAM sizes
- evidence that each device completed a separate GuC/CT init sequence

Name the dual-Xe failure modes explicitly in the capture summary:

- GuC ID namespace collision between devices
- second-device firmware-load or firmware-cache confusion
- GGTT address-space isolation failure
- concurrent firmware loads for the same blob name
- DRM minor or render-node allocation failure on the second device
- LinuxKPI or core-DRM global-state races during dual probe
- IRQ or MSI/MSI-X registration collision
- TTM device or memory-pool isolation failure
- memory-pressure amplification when both devices allocate BOs
- unload or attach failure on one device freeing shared state used by the other

Store logs with enough metadata to reproduce the comparison:

- date
- BIOS version
- PCI slot and BDF
- native vs bhyve
- kernel package version
- firmware package version
- Mesa/userspace versions if execution paths are being compared

Recommended local layout:

- `../wip-drm-xe-artifacts/logs/linux-rocky10/native/<date>/`
- `../wip-drm-xe-artifacts/logs/linux-rocky10/bhyve-passthrough/<date>/`
- `../wip-drm-xe-artifacts/logs/freebsd-xe/native/<date>/`

Raw logs and large captures should stay outside this Git-tracked WIP repo.
Keep only small summaries or curated excerpts here.
See [repo-hygiene.md](repo-hygiene.md).

## FreeBSD Comparison Points

The first useful comparisons are not performance results.
They are initialization facts.

Compare Linux and FreeBSD for:

- PCI IDs and detected platform name
- BAR sizes and VRAM aperture
- DMA mask and busdma setup
- firmware file names and versions
- GuC, HuC, and GSC init ordering
- MMIO and GT init milestones
- MSI/MSI-X or interrupt mode
- display ownership and whether the A380 is secondary-only
- DRM minor and render-node creation
- reset behavior after attach failure
- panic, fault, or timeout locations

For Phase 1.5, add:

- multi-device enumeration order
- which Xe device receives which DRM minor and render node
- whether internal-device attach changes external-device behavior
- whether unload, reset, or reprobe on one device perturbs the other

The immediate purpose is to answer:

> Is FreeBSD failing before, at, or after the same milestone Linux reaches on
> the same hardware?

## Backport Discipline

Rocky/RHEL 10.x may expose useful fixes that are not in the local
`../nx/linux-6.12` tree.

When that happens:

1. identify whether the fix is upstream 6.12.y, later upstream mainline, or
   Rocky/RHEL downstream-only
2. find the upstream commit if one exists
3. document why the FreeBSD port needs it
4. carry it as a named backport against the pinned 6.12 base
5. avoid silently replacing the baseline with a newer Linux snapshot

This preserves the project's core rule:

> Linux 6.12 is the semantic and integration baseline; newer behavior enters
> only as explicit, justified backports.

## Success Criteria

The A/B setup is successful when we can repeatedly produce paired artifacts:

- Linux Rocky/RHEL 10.x logs showing expected A380 Xe behavior
- FreeBSD Xe logs from the same hardware and same bring-up milestone
- a short classification of each difference as upstream, LTS, downstream,
  FreeBSD LinuxKPI, FreeBSD DRM, or Xe-port local

This gives FreeBSD developers a concrete comparison set instead of only a
statement that "Linux works."

The preferred implementation split is an Elixir A/B harness calling Zig helper
binaries for render-node, ioctl, timing, and stress probes.
