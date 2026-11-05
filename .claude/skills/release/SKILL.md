---
name: release
description: Cut a firmware release of the Pulse Generator Emulator - bump FW_VERSION, update CHANGELOG/PLAN/docs, run the definition-of-done builds, commit, tag v<version>, and push so GitHub Actions creates the GitHub release. Use when the user asks to release, tag or publish a new version.
disable-model-invocation: true
argument-hint: <new version, e.g. 0.2.0>
---

# Release v$ARGUMENTS

Follow AGENTS.md. Stop and ask if anything below fails or is unclear.

1. `git status` must be clean apart from the release changes; `git log origin/main..` shows what ships.
2. Set `FW_VERSION` in `Core/Inc/version.h` to `$ARGUMENTS`.
3. CHANGELOG.md: move the `[Unreleased]` items under `## [$ARGUMENTS] - <today>` (user-visible changes only)
   and add the compare link at the bottom. Leave an empty `[Unreleased]`.
4. Docs that show the version: `docs/USER_MANUAL.md` title and examples. Find leftovers with
   `grep -rn "<old version>" --include=*.md .`
5. `tools/build_all.sh` must end with `OK 2 builds ... 0 warnings`.
6. If the user says a board is connected: `tools/build_and_flash.sh`, then `tools/pulse_test.py`
   (see the `bench` skill) and paste the real output. Otherwise say the release is build-tested only.
7. PLAN.md: status column, plus a log line with what was and was not verified on hardware.
8. Show the user the diff summary and ask before each of: commit,
   `git tag -a v$ARGUMENTS -m "FW $ARGUMENTS: <summary>"`, `git push origin main v$ARGUMENTS`.
9. After the push, the `build` workflow creates the GitHub release (the tag must equal `v` + FW_VERSION).
