# Repository Hygiene

Date: 2026-04-18

## Purpose

This WIP directory is intended to become a normal Git repository that can be
committed and pushed to a remote.

Keep this repo small, reviewable, and source-oriented.

## What Belongs In This Repo

Track files that are appropriate for Git review:

- design documents
- source code
- small scripts
- small test definitions
- small structured metadata
- reproducible instructions
- manually curated result summaries

The rule is:

> If a file is useful to review in a commit diff, it probably belongs here.

## What Does Not Belong In This Repo

Do not track large or generated artifacts here.

Keep these outside the repo:

- build directories
- object files
- kernel modules
- downloaded RPMs or packages
- ISO or VM images
- bhyve disk images
- vmcores and crash dumps
- full hardware log captures
- long trace captures
- generated benchmark output
- large binary firmware/package snapshots

The rule is:

> If a file is large, generated, machine-local, or not useful in a code review
> diff, put it in the parent directory instead.

## Parent Artifact Area

Use a parent-directory artifact area for untracked material.

Recommended default:

- `../wip-drm-xe-artifacts/`

Suggested layout:

- `../wip-drm-xe-artifacts/logs/freebsd-xe/native/<date>/`
- `../wip-drm-xe-artifacts/logs/linux-rocky10/native/<date>/`
- `../wip-drm-xe-artifacts/logs/linux-rocky10/bhyve-passthrough/<date>/`
- `../wip-drm-xe-artifacts/packages/`
- `../wip-drm-xe-artifacts/vm/`
- `../wip-drm-xe-artifacts/build/`
- `../wip-drm-xe-artifacts/traces/`
- `../wip-drm-xe-artifacts/crash/`

If another parent-directory path is used, document it in the relevant test
run summary.

## Result Summaries

Large raw logs should stay outside the repo, but small summaries can be
tracked.

Good tracked summary files include:

- short "works / does not work" matrices
- manually reduced failure notes
- exact commands used for a test run
- links or relative references to untracked artifact directories
- small excerpts required to explain a bug

Do not paste massive `dmesg`, trace, or benchmark logs into tracked Markdown.

## Gitignore Policy

The repo's `.gitignore` excludes common local artifacts, including:

- `logs/`
- `artifacts/`
- `build/`
- `obj/`
- `tmp/`
- `*.rpm`
- `*.iso`
- `*.img`
- `*.qcow2`
- `*.core`
- `*.vmcore`
- `*.ko`
- Zig and Elixir build directories

If a future generated artifact appears in `git status`, either move it to the
parent artifact area or add a narrow ignore rule.

Do not use broad ignore rules that might accidentally hide source files.

## Final Rule

This repo is for reviewable project source and planning material.
Large build artifacts, hardware logs, VM images, package downloads, crash
dumps, and generated traces belong in the parent directory, not in Git.
