## Proposed change

<!--
Describe what this PR changes and why. Link the related issue if any.
If the change is a new dedup rule or behavior change, argue why merged
URLs were redundant for an attacker (the only axis xcull is allowed to
lose information on).
-->

## Verification

<!--
Paste the commands you ran and their outcome. Minimum:
  - make
  - make test
A new dedup rule also needs a golden case under tests/cases/.
-->

```
make
make test
```

## Checklist

- [ ] Builds clean on gcc and clang (no new warnings).
- [ ] `make test` passes.
- [ ] Behavior changes have a golden case under `tests/cases/<name>/`.
- [ ] Output is byte-identical for unchanged inputs, OR the PR
      description explains which input class the output is allowed to
      change for.
- [ ] No new runtime dependencies (libc only).
- [ ] CHANGELOG.md updated if user-visible.
