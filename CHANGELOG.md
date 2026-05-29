# Changelog

User-visible release log. For the verification matrix behind each entry
(which inputs and flag combinations were checked), follow the
corresponding tag in git history.

## v24.0 - help and version flags (2.0.0)
Add a curl/wget-style help and version surface.

BREAKING: `-V` now prints the version, not verbose stats. Verbose stats
moved to `-v` (or `--verbose`). This aligns xcull with curl and wget,
where `-V` is version. Scripts using `xcull -V` for stats must switch to
`xcull -v`.

`xcull --help` (or `-h`) prints the option list to stdout and exits 0;
`xcull --help all` (or `-h all`) appends usage examples and the exit-code
table. `xcull -V` / `--version` prints a wget-style block: version,
platform, compiled-in capabilities, the actual build and link flags,
default policy, and license. A bare invocation on a terminal now prints a
one-line "pipe URLs in" hint instead of silently blocking on the TTY, and
an unknown flag points at `--help`.

Dedup output is byte-identical to v23 for every flag and input; this
release only changes the CLI surface.

## v23.0 - GraphQL queries
Keep distinct GraphQL `?query={...}` values. Previously the literal
braces hit the byte-rejection gate and the whole URL was dropped as
garbage. v23 detects the `query=` key with a brace value and emits the
full operation, so two requests `?query={me{id}}` and `?query={users{role}}`
remain distinct attack surface. Scope is tight: only this exact shape is
affected; everything else is byte-identical to v22.

## v22.0 - source maps
Stop dropping `.map` URLs. Source maps are the opposite of render noise:
they disclose unminified source, internal routes and often secrets, and
are a standing recon finding. Output is a strict superset of v21 for
`.map` lines, zero other deltas.

## v21.0 - memory overhaul
Default path peak RSS drops 38% on a 1.1M-line wayback input (40.5 -> 25.3 MB);
on a 781k input 22.5 -> 14.0 MB. Output byte-identical to v20 across every
flag and input. Four changes: front-coded line storage, per-line record
shrunk from 20 B to one bit, varint headers, and a 16-bit Aho-Corasick
goto table.

## v20.0 - bare-endpoint fold
Default mode now folds a bare path against a decorated sibling. So
`/v1/auth/session` is dropped once `/v1/auth/session;jsessionid=...` is
seen on the same input. The richer line still reaches the scanner, so
no endpoint class is lost. `-k` and `-x` are unchanged.

## v19.0 - subset-merge O(n²) cut
The default keyset merge no longer rescans the antichain per insert.
Records are bucketed per (path, cardinality) and equal-size buckets are
skipped, so a single hot path with K key-sets drops from O(K²) to O(K).
Output byte-identical to v18.9.

## v18.9 - memory overhaul (dedup table)
Peak RSS on a 1.1M-line input drops 61 -> 34 MB (-45%). Dedup set
is now keyed on a 128-bit signature digest (two FNV-1a lanes,
splitmix64-finalised) instead of the raw signature bytes. Slot size
halved, Rec halved. Output identical in practice (birthday-collision
probability ~3.6e-27 on a 10^6 input, the same trust model git uses for
object identity).

## v18.7 - corrupted content-hash captures
Drop leaves whose content-hash suffix has a digit immediately followed
by a letter (e.g. `_P1cIt` glued onto the real `_P1` by a scraper). A
real selector ends at the digit run. Tight scope, two-line delta on the
test corpus, zero other movement.

## v18.6 - opaque content-hash ids
Fold leaves of the form `<LABEL><opaque-hex-id>[<sep><suffix>]` to one
witness per template (label + suffix). Content-addressed handles like
`TIP14995B514_P1` are not enumerable surface, so collapsing 1361 of them
to 301 witnesses costs nothing while making the output legible.

## v18.5 - media noise
Render-noise drop now covers the modern audio/video extensions the v18.4
list missed: `m4p`, `m4v`, `aac`, `m4b`, `wma`, `aiff`, `opus`, `mid`,
`mp3`, `flac`, `mkv` and friends. These are static media, dropped by
default, kept under `-a`. 1852 such lines removed on a real 35k input,
zero non-media collateral.

## v18.4 - subset-only query merge
A query URL is dropped iff its key set is a proper subset of another's
on the same templated path. Two URLs with disjoint key sets both
survive. So `/page?role=admin` and `/page?debug=true` both reach the
scanner; only the proper-subset case collapses. Fixes a v18.3 regression
that silently dropped distinct parameters.

## v18.x - perf-only point releases
v18.2 and v18.1 are perf-only releases, byte-identical output to v18 on
every flag and input.

## v18 - default id-preserve
Object ids are kept by default and folded only under `-F`. The previous
default collapsed numeric and UUID ids, which deleted IDOR/BOLA surface.
Inverted: distinct ids now survive, opt in to folding for route
discovery.

## v17 - mixed alphanumeric id fold
Adds the alphanumeric id class to templating: `U-61723A`, `INV0012345`,
`P1234ab` etc. Folded under `-F`, kept by default.

## v16 - content-section title-slug fold
Title-slug fold extended to nested content sections: `/blog/<slug>` and
`/news/2024/01/<slug>` both collapse to one witness per section.

## v15 - speed overhaul
Reorders the per-line work to keep the L1 footprint small across an
780k-line input. Throughput rises enough to hold the Pareto frontier on
wall time as well as memory and false-merge rate.

## v14 - public benchmark, de-identified
Cuts the de-identified Q1 benchmark and ships it in the xcull-benchmark
repo. Every URL is reproducible from a fixed seed; no real target
identifies survives the cipher.

## v13 - Q1-grade re-benchmark
Re-runs at N=10 per cell with a fixed wall clock, canonicalization
checks, and 95% CIs. Establishes the trial protocol that all later
versions use.

## v12 - per-line pentester audit
A real per-line audit of the gau and wayback outputs. Every removed line
classified by hand to confirm it was redundancy, not surface.

## v11 - diff against uddup and urless
Reads the uddup and urless sources to learn why their outputs collapse
JSESSIONID and title-slug groups, and replays those decisions on the
xcull side with the conservative defaults.

## v10 - head-to-head
First multi-tool benchmark vs uro / urldedupe / urless / uddup on the
testinvicti corpus.

## v9 - path/query audit
Path-level and query-level garbage classes: 10 distinct shapes
classified, each gated under `-x`.

## v8 - host audit
Host-level junk: glued TLDs, embedded-domain probes, label-walk bugs in
the resolver.

## v7 - pentester audit fixes
First wave of pentester-audit findings folded back into the code.

## v5 - sanity gate
The pre-dedup garbage drop becomes default-on and can be disabled with
`-x`. Sets the keep-bias / cleanup balance every later version inherits.

## v2 - public fork point
First version published with the structural-dedup core. Subsequent
versions are all proven against the same vulnweb / gau / wayback
inputs.
