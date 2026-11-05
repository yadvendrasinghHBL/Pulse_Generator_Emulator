@AGENTS.md

## Claude Code specifics

- The arm toolchain is not on PATH. Always build through the CMake presets or `tools/build_all.sh`.
  Do not call `arm-none-eabi-gcc` by name.
- Flashing and serial tests touch real hardware. Run them only when the user says a board is
  connected, and report the actual console output.
- For bench sessions, run `tools/pulse_test.py` and paste its output into the conversation.
  Do not write your own serial scripts unless that tool cannot do what you need.
- `.claude/settings.json` runs `.claude/hooks/guard_bash.py` before every shell command. It blocks option-byte,
  RDP and mass-erase commands, DTR-on port access and force pushes. If it blocks something the user asked for,
  say so and let the user run it with `! <command>`; do not work around the hook.
- Skills: `/release <version>` (release procedure, user-invoked) and `bench` (on-target checks).
