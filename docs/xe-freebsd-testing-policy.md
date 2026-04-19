# Intel Xe on FreeBSD 6.12: Testing Policy

Date: 2026-04-18

## Purpose

This document defines the testing policy for the Xe-on-FreeBSD effort.

The policy is intentionally simple:

> Existing tests stay in their existing framework and must pass.
> New performance-insensitive project tests should be written in Elixir.
> New performance-sensitive project tests should be written in Zig.

This policy applies to this planning workspace and to future project-owned test
tools around `freebsd-src-drm-6.12`, `drm-kmod-6.12`, Rocky Linux 10.x A/B
testing, and real-hardware bring-up.

## Existing Tests

Existing tests are non-negotiable.

If a tree already has tests, use the framework that tree already uses and make
those tests pass.

Examples:

- FreeBSD `kyua` / ATF tests
- FreeBSD kernel build and module-load checks
- `drm-kmod` build tests
- existing shell, make, or Python test scripts
- upstream Linux KUnit, kselftest, or driver tests when they are relevant and
  practical to run
- Mesa, libdrm, or userspace tests when validating an execution path

Do not rewrite existing tests into Elixir or Zig only to satisfy this policy.
The policy for existing tests is:

- preserve the native test harness
- keep the test's original intent
- fix regressions instead of weakening tests
- document tests that cannot yet run on FreeBSD
- do not delete or skip tests silently

If an imported Linux test cannot run because FreeBSD does not yet provide the
needed semantic, mark that honestly as an unsupported or deferred area.
The deferral should identify the missing FreeBSD, LinuxKPI, DRM, firmware, or
hardware prerequisite.

## New Tests

For new project-owned tests, choose the language by sensitivity to performance,
timing, binary layout, and low-level hardware interaction.

The decision rule is:

- use Elixir when correctness, orchestration, observability, or workflow
  structure matters more than raw speed
- use Zig when runtime overhead, latency, memory layout, binary parsing,
  deterministic execution, or low-level system interaction matters

Do not choose a language because it is fashionable.
Choose it because it makes the test more reliable and maintainable.

## Elixir Tests

Use Elixir for performance-insensitive tests.

Good Elixir test targets:

- test orchestration across FreeBSD and Rocky Linux
- A/B log collection and comparison
- repeated boot, load, unload, attach, detach, and reboot workflows
- parsing ordinary text logs where speed is not a bottleneck
- checking that expected milestones appear in `dmesg`
- classifying failures as LinuxKPI, DRM, firmware, PCI, VM, or Xe-local
- generating structured test reports
- supervising long-running hardware test sessions
- coordinating bhyve test VMs
- validating documentation matrices and known-unsupported lists
- checking package, kernel, firmware, and module-version metadata

Elixir is the default for project-level test harnesses because this effort will
need durable orchestration more than micro-optimized scripting.

Examples of Elixir-owned checks:

- "Did Rocky Linux 10.1 and FreeBSD reach the same A380 firmware milestone?"
- "Did the A380 attach survive 50 load/unload cycles?"
- "Did the B580 Linux reference run expose `xe.ko`, BMG firmware, and a render
  node?"
- "Did a FreeBSD panic occur before or after the Linux GT init milestone?"

## Zig Tests

Use Zig for performance-sensitive tests.

Good Zig test targets:

- low-overhead hardware probes
- deterministic latency or timing checks
- binary trace parsing where throughput matters
- ioctl fuzz or ABI exercisers where C ABI control matters
- minimal DRM userspace probes
- mmap, BO, VM-bind, fence, and sync-object exercisers
- command-submission smoke tests
- stress tools that must avoid VM/runtime noise
- tests that need explicit memory layout or alignment
- tiny standalone utilities that should be easy to run on FreeBSD and Linux

Zig is the default for tests that are close to the driver ABI or performance
path.

Examples of Zig-owned checks:

- "Can this process open the render node and issue the minimal Xe query ioctl?"
- "Can BO allocation, mmap, bind, and fence wait complete within stable bounds?"
- "Does repeated exec submission produce stable latency without runtime noise?"
- "Can a binary devcoredump, trace, or ioctl capture be decoded quickly and
  deterministically?"

## Boundary Cases

When a test has both orchestration and low-level hot paths, split it.

Use Elixir for:

- scheduling
- retries
- host/guest control
- log collection
- report generation
- high-level assertions

Use Zig for:

- the tight measurement loop
- the ioctl exerciser
- the binary parser
- the low-level probe
- the performance-critical worker

The Elixir harness can call Zig binaries and consume structured output.
Prefer line-delimited JSON or another simple stable text format for that
boundary.

## A/B Testing Policy

Rocky Linux 10.x is an operational Linux reference, especially for 6.12-series
`xe` behavior.

For A/B tests:

- collect Linux and FreeBSD artifacts in parallel where practical
- compare milestones before comparing performance
- classify each difference as upstream 6.12, 6.12.y LTS, Rocky/RHEL
  downstream, FreeBSD LinuxKPI, FreeBSD DRM, firmware, hardware, or Xe-port
  local
- keep native same-hardware boot as the cleanest hardware truth
- treat bhyve passthrough as useful but not identical to native boot

Elixir should usually own the A/B harness.
Zig should own any low-level render-node, ioctl, timing, or stress binary used
inside that harness.

## Hardware Bring-Up Test Order

For early A380 and B580 testing, prefer this order:

1. build succeeds
2. module loads
3. PCI match occurs
4. attach starts
5. MMIO BAR access works
6. firmware paths resolve
7. GuC firmware load and ADS setup reach known ready/fail states
8. GuC CT buffer allocation and GGTT pinning work
9. CT H2G/G2H exchange succeeds or fails with useful classification
10. IRQ mode is established without WITNESS violations
11. DRM device registration succeeds
12. render node appears
13. simple query ioctl succeeds
14. BO allocation/free works for system and local memory
15. BO-backed VM_BIND works with fault mode disabled
16. basic submission works and fence signals
17. memory-pressure and eviction smoke tests begin

Do not run performance tests as the primary signal before the lower milestones
are stable.

## Linux Xe Tests To Mirror

Mirror Linux Xe test intent, not necessarily KUnit mechanics.

Recommended project-level mirrors:

- `tests/xe_bo.c`: reimplement BO placement and move checks in Zig
- `tests/xe_migrate.c`: reimplement migration data-integrity checks in Zig
- `tests/xe_pci.c` / `tests/xe_pci_test.c`: port directly if KUnit is
  available; otherwise expose as Elixir probe tests
- `tests/xe_guc_db_mgr_test.c`: reimplement doorbell allocator behavior in Zig
- `tests/xe_guc_id_mgr_test.c`: reimplement GuC ID allocator bounds in Zig
- `tests/xe_wa_test.c`: preserve workaround application checks
- `tests/xe_rtp_test.c`: preserve register table processing checks

Skip for the first A380 milestone:

- `tests/xe_guc_relay_test.c`
- `tests/xe_gt_sriov_pf_service_test.c`
- `tests/xe_lmtt_test.c`

These are relay-specific, SR-IOV-specific, or too KUnit-specific for first
bring-up.

## CI and Review Policy

Every future patch series should say which tests were run.

At minimum, record:

- tree and branch
- kernel or module version
- hardware used
- operating system
- test command
- pass/fail result
- known skipped tests
- logs location

If a test is skipped, explain why.
Acceptable reasons include missing hardware, missing FreeBSD semantic, missing
firmware, unsupported feature stage, or a known upstream limitation.

Unacceptable reasons include convenience, local flakiness without diagnosis, or
because the test exposes a real regression.

## Upstreaming Rule

Tests intended for FreeBSD upstream review should fit FreeBSD's review style.

That means:

- keep existing FreeBSD tests in FreeBSD-native frameworks
- keep generic LinuxKPI or DRM tests separate from Xe-only tests
- do not require Elixir or Zig for a FreeBSD-src patch unless the test is
  clearly project-owned and external to upstream FreeBSD's normal test suite
- use Elixir and Zig mainly for our reproducible bring-up lab, A/B harnesses,
  and driver-facing tools
- prefer shell, Python, ATF, kyua, or existing drm-kmod conventions for tests
  that are meant to be submitted upstream

The language policy is a project testing policy, not a demand that FreeBSD
upstream adopt Elixir or Zig inside base.

## Final Rule

Existing tests must pass in their existing framework.
New tests use Elixir unless performance, low-level ABI control, or deterministic
system interaction makes Zig the better tool.
