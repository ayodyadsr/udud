# Contributing to xcull

Patches, bug reports, and corpus contributions are welcome.

## Reporting bugs

Open a GitHub issue at https://github.com/xcull/xcull/issues with:

- the exact command line you ran,
- a minimal input that reproduces the problem (the smallest set of URLs
  that triggers it - five lines beats five thousand),
- the actual output,
- the output you expected,
- `xcull -v` output if relevant (it prints peak RSS and in/out counts),
  and `xcull -V` (the version/build block) so the build is on record.

Single-line repros land fastest. If the bug is "this URL got dropped",
the issue body is one line of input plus the expected one-line output.

## Submitting patches

xcull is a single-file C program with no runtime dependencies. The bar
for changes is:

1. `make` builds clean on gcc and clang, with no new warnings.
2. `make test` passes. Every behavior change adds a golden case under
   `tests/cases/<name>/` with `in.txt`, `flags`, and `expected.txt`.
3. Output stays deterministic. xcull guarantees byte-identical output
   for a given input and flag set across runs.
4. Memory stays bounded. Per-line state grows in front-coded storage,
   not in unbounded per-URL allocations.

If a change folds previously-distinct URLs together, the PR description
needs to argue why the merged URLs were redundant for an attacker -
that is the only axis on which xcull is allowed to lose information.
See [`CHANGELOG.md`](CHANGELOG.md) for examples of how prior merges
were justified.

## Style

- C99, no C++ comments inside multi-line blocks.
- Indent with 4 spaces. No tabs.
- Functions stay short enough to read on one screen.
- No new dependencies. Standard libc only.

## Branching

Work off `main`. Open a PR against `main`. CI runs on every push and
PR; if the matrix is red, the PR is not ready.

## Releases

Tags of the form `v<major>.<minor>` trigger the release workflow,
which builds linux-x86_64 and linux-arm64 static tarballs and uploads
them to the GitHub release with sha256 sidecars. Maintainers cut the
tag from a clean `main` after CI is green.

## Security issues

Do not open public issues for security bugs in xcull itself (parser
crashes on hostile input, memory corruption, etc.). Email the address
on the repo profile instead and a maintainer will respond.

## Commercial and OEM use

xcull is source-available under [XSAL v1.0](LICENSE.md). Embedding xcull
in a commercial product, hosted service, or appliance requires a
separate license - see [XCOL.md](XCOL.md). Contributors retain copyright
on their patches; by opening a PR you license the contribution under the
same XSAL v1.0 terms.
