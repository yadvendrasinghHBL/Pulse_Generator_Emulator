#!/usr/bin/env python3
"""Claude Code PreToolUse hook: block shell commands that break the AGENTS.md safety rules.

Reads the hook JSON on stdin. Exit 2 + a reason on stderr blocks the command (Claude sees the
reason); exit 0 lets it through. Rules:
  - STM32_Programmer_CLI: no option-byte writes (-ob other than `-ob displ`), no RDP change,
    no read-unprotect (-rdu, causes a mass erase), no mass erase (-e all / --erase all)
  - the FT231X port only with DTR off: no stty or shell redirection on /dev/ttyUSB*/ttyACM*,
    picocom only with --lower-dtr (DTR on holds the rev A MCU in reset, HARDWARE.md finding 3)
  - no force push
These need an explicit request from the user; the user can still run them with `! <command>`.
"""
import json
import re
import sys

RULES = [
    (r"STM32_Programmer_CLI.*\s(-ob|--optionbytes)\s+(?!displ\b)",
     "option-byte write: only on the user's explicit request in this conversation (AGENTS.md Safety); "
     "reading with `-ob displ` is allowed"),
    (r"STM32_Programmer_CLI.*\bRDP\s*=", "RDP level change: RDP 2 bricks debug access for good"),
    (r"STM32_Programmer_CLI.*\s(-rdu|--readunprotect)\b", "read-unprotect does a mass erase"),
    (r"STM32_Programmer_CLI.*\s(-e|--erase)\s+(all|\[?\s*0\s*\.\.)",
     "mass erase also wipes the settings page (0x0803F800)"),
    (r"\bstty\b.*/dev/tty(USB|ACM)", "stty opens the port with DTR on (HARDWARE.md finding 3); use tools/con_send.sh"),
    (r"[<>]\s*/dev/tty(USB|ACM)", "shell redirection opens the port with DTR on; use tools/con_send.sh"),
    (r"\bcat\s+/dev/tty(USB|ACM)", "cat opens the port with DTR on; use tools/con_send.sh"),
    (r"\bgit\s+push\b.*\s(-f|--force|--force-with-lease)\b", "force push rewrites published history"),
]


def main():
    try:
        cmd = json.load(sys.stdin).get("tool_input", {}).get("command", "")
    except (ValueError, AttributeError):
        return 0
    for pattern, why in RULES:
        if re.search(pattern, cmd):
            print(f"Blocked by .claude/hooks/guard_bash.py: {why}.", file=sys.stderr)
            return 2
    # picocom run as a command (start of the line or after ; & | ( ), not just mentioned in text
    if re.search(r"(^|[;&|(]\s*)(sudo\s+)?picocom\b", cmd) and "--lower-dtr" not in cmd:
        print("Blocked by .claude/hooks/guard_bash.py: picocom needs --lower-dtr "
              "(DTR on holds the MCU in reset, HARDWARE.md finding 3).", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
