# Rocky Linux 10.1 as an Intel Xe Reference

Date: 2026-04-18

## Purpose

This note records why Rocky Linux 10.1 is useful as a Linux-side A/B reference
for the FreeBSD Xe port.

The key point is:

> Rocky Linux 10.1 can exercise Intel Arc B580 through the Linux `xe` driver on
> the same 6.12 kernel generation we are using as the FreeBSD port baseline.

This does not make Rocky the source baseline.
The source-semantic baseline remains `../nx/linux-6.12`.
Rocky Linux 10.1 is an operational reference for real hardware behavior.

## Confirmed Baseline

Rocky Linux 10.1 ships a 6.12-series kernel:

- Rocky Linux 10.1 kernel version: `6.12.0-124.8.1`
- RHEL 10.1 shipped kernel version: `6.12.0-124.8.1.el10_1`

Sources:

- Rocky Linux release notes:
  `https://docs.rockylinux.org/release_notes/`
- Red Hat Enterprise Linux release-date and shipped-kernel table:
  `https://access.redhat.com/articles/red-hat-enterprise-linux-release-dates`

## Intel Arc B580 Means Xe

Intel's hardware table identifies Arc B580 as:

- PCI ID: `E20B`
- Product: `Intel Arc B580 Graphics`
- Architecture: `Xe2`
- Codename: `Battlemage`
- Upstream kernel support: `6.11*`

Source:

- Intel GPU hardware table:
  `https://dgpu-docs.intel.com/devices/hardware-table.html`

The asterisk in Intel's table means force-probe may be required for the first
upstream support version. For our purposes, the important fact is that B580 is
in the `xe` driver generation, not the legacy `i915` path.

## Local Linux 6.12 Source Check

The local Linux 6.12 tree confirms B580 is in the Xe PCI ID set.

In:

- `../nx/linux-6.12/include/drm/intel/xe_pciids.h`

`XE_BMG_IDS` includes:

```c
#define XE_BMG_IDS(MACRO__, ...) \
	MACRO__(0xE202, ## __VA_ARGS__), \
	MACRO__(0xE20B, ## __VA_ARGS__), \
	MACRO__(0xE20C, ## __VA_ARGS__), \
	MACRO__(0xE20D, ## __VA_ARGS__), \
	MACRO__(0xE212, ## __VA_ARGS__)
```

In:

- `../nx/linux-6.12/drivers/gpu/drm/xe/xe_pci.c`

Linux 6.12 defines a Battlemage descriptor:

```c
static const struct xe_device_desc bmg_desc = {
	DGFX_FEATURES,
	PLATFORM(BATTLEMAGE),
	.has_display = true,
	.has_heci_cscfi = 1,
};
```

and wires BMG IDs into the Xe PCI table:

```c
XE_BMG_IDS(INTEL_VGA_DEVICE, &bmg_desc),
```

This is the source-level confirmation that B580-class Battlemage devices are
handled by `drivers/gpu/drm/xe/`.

## Rocky Linux 10.1 Package Check

The Rocky Linux 10.1 kernel module package contains the Xe kernel module:

```text
./lib/modules/6.12.0-124.8.1.el10_1.x86_64/kernel/drivers/gpu/drm/xe/xe.ko.xz
```

Extracting the module shows a PCI alias for the B580 device ID:

```text
alias=pci:v00008086d0000E20Bsv*sd*bc03sc*i*
```

That verifies Rocky's packaged 10.1 kernel can match Intel vendor `8086`,
device `E20B`, display-class hardware with `xe.ko`.

## Firmware Check

Rocky Linux 10.1 also carries the Battlemage Xe firmware files in the
`linux-firmware` package:

```text
./usr/lib/firmware/xe/bmg_guc_70.bin.xz
./usr/lib/firmware/xe/bmg_huc.bin.xz
```

That matters because a useful B580 reference requires more than PCI matching.
The Linux-side reference should be able to exercise at least:

- PCI probe
- firmware discovery
- GuC firmware load
- HuC firmware availability
- DRM device creation
- GT initialization

## What This Confirms

For this project, Rocky Linux 10.1 + Arc B580 is a valid Linux Xe reference for:

- driver binding through `xe.ko`
- Battlemage PCI matching
- BMG firmware availability
- early kernel bring-up comparison
- DRM node and render-node comparison
- GuC/HuC log comparison
- native same-hardware A/B testing against FreeBSD

This is directly useful because the FreeBSD port is pinned to Linux 6.12
semantics while Rocky 10.1 provides a maintained 6.12-series Linux environment.

## What This Does Not Prove

This does not prove that every B580 workload is fully supported on Rocky Linux
10.1 out of the box.

Full user-visible support can also depend on:

- Mesa version
- Vulkan userspace
- VA-API / media-driver stack
- oneAPI or Level Zero stack
- firmware package version
- compositor and desktop session
- downstream Rocky/RHEL kernel patches

So the right language is:

> Rocky Linux 10.1 confirms a real 6.12-series `xe` kernel-driver reference for
> B580. It does not by itself guarantee every graphics, media, compute, or
> desktop workload is perfect.

## How To Use It In This Project

Use Rocky Linux 10.1 as the Linux-side operational oracle for B580 and, by
extension, for validating that our FreeBSD Xe port behaves like the Linux Xe
driver generation we are targeting.

Preferred A/B topologies:

- native FreeBSD vs native Rocky Linux 10.1 on the same machine
- FreeBSD host with Alder Lake iGPU as display, A380 or B580 passed through to
  Rocky Linux 10.1 under bhyve when passthrough is practical

For each B580 Linux run, capture:

- `uname -a`
- `/etc/os-release`
- `rpm -q kernel-core kernel-modules linux-firmware`
- `modinfo xe`
- `lspci -nn -vv -s <B580_BDF>`
- `dmesg -T` filtered for `xe`, `drm`, `guc`, `huc`, `gsc`, `bmg`,
  `battlemage`, `firmware`, `msi`, and `irq`
- `/dev/dri` listing
- `/sys/module/xe/parameters/*`
- `drm_info` where available
- `vulkaninfo`, `vainfo`, or a minimal render test only after basic driver
  bring-up is proven

Store raw logs, downloaded RPMs, package extracts, and large captures outside
this Git-tracked repo, preferably under:

- `../wip-drm-xe-artifacts/`

Keep only small summaries or curated excerpts in this repo.
See:

- `docs/repo-hygiene.md`

## Backport Discipline

Rocky/RHEL 10.1 may include downstream fixes or 6.12.y fixes that are not
present in the local `../nx/linux-6.12` tree.

If Rocky succeeds where upstream 6.12 reference behavior appears insufficient:

1. identify whether the fix is upstream 6.12.y, later upstream mainline, or
   Rocky/RHEL downstream-only
2. find the upstream commit if one exists
3. document why the FreeBSD port needs it
4. carry it as an explicit backport
5. do not silently move the whole FreeBSD port baseline beyond Linux 6.12

## Summary

Rocky Linux 10.1 is a strong A/B test partner for this project because it
ships a 6.12-series kernel, packages `xe.ko`, matches Arc B580 PCI ID `E20B`,
and carries the BMG GuC/HuC firmware files.

For B580 specifically, the kernel path is definitely `xe`, not `i915`.
