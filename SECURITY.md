# Security Policy

## Supported versions

Only the most recent minor release receives security fixes. Older tags
remain available but will not be patched.

## Reporting a vulnerability

Do not open a public GitHub issue for security bugs in xcull (parser
crashes on hostile input, out-of-bounds reads or writes, memory
corruption, integer overflows, etc.).

Use GitHub's private vulnerability reporting:

1. Go to https://github.com/xcull/xcull/security/advisories/new
2. File the report with a minimal reproducer (input bytes + flags +
   observed crash/asan output).
3. A maintainer will acknowledge within 3 working days and coordinate
   a fix and disclosure timeline with you.

## Scope

xcull reads untrusted URL lists from stdin. The threat model assumes a
hostile input. In scope:

- Crashes (SIGSEGV, SIGBUS, abort) on any input the binary accepts.
- Out-of-bounds reads or writes (ASan / UBSan / MSan findings).
- Pathological CPU or memory behavior triggered by ~1 KB of input.

Out of scope:

- Behavior under malformed command-line flags (operator error, not a
  vuln).
- Resource use that scales with input size as documented (xcull is
  streaming but the deferred-emit path retains kept lines).
