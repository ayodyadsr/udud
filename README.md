<h1 align="center">xcull</h1>

<h4 align="center">Security-aware URL deduplicator for recon pipelines.</h4>

<p align="center">
  Collapses noisy recon URLs into clean attack surface while preserving exploitable patterns.
</p>

<p align="center">
  <a href="https://github.com/xcull/xcull/actions/workflows/ci.yml"><img src="https://github.com/xcull/xcull/actions/workflows/ci.yml/badge.svg" alt="ci"></a>
  <a href="https://github.com/xcull/xcull/releases"><img src="https://img.shields.io/github/release/xcull/xcull" alt="release"></a>
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-22.6MB%20%2F%20780k%20URLs-success.svg">
  <a href="https://github.com/xcull/xcull-benchmark"><img src="https://img.shields.io/badge/benchmark-results-orange.svg"></a>
  <a href="https://github.com/xcull/xcull/issues"><img src="https://img.shields.io/badge/contributions-welcome-brightgreen.svg" alt="contributions welcome"></a>
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#installation">Installation</a> ·
  <a href="#usage">Usage</a> ·
  <a href="#running-xcull">Running xcull</a> ·
  <a href="#examples">Examples</a> ·
  <a href="#flag-use-cases">Flag use cases</a> ·
  <a href="#output">Output</a> ·
  <a href="#pipeline-integration">Pipelines</a> ·
  <a href="#benchmark">Benchmark</a> ·
  <a href="#reporting-issues">Issues</a> ·
  <a href="#sponsor">Sponsor</a> ·
  <a href="#license">License</a>
</p>

<p align="center">
  <img src="demo/xcull.gif" alt="xcull demo">
</p>

---

## Features

- Single static C binary with no runtime dependencies.
- Streaming dedup with constant 22 MB peak RSS on 780,200 URLs.
- Preserves every distinct object identifier (numeric, UUID, hex) for IDOR and BOLA enumeration.
- Preserves every distinct session token for session-bound authorization testing.
- Deduplicates on query parameter shape, not raw query string.
- Path templating folds repetitive slug variations without merging distinct endpoints.
- Filters binary assets, wayback noise, and scanner-probe URLs by default.

## Installation

### Prebuilt binary

Download the latest release for your platform from
[github.com/xcull/xcull/releases](https://github.com/xcull/xcull/releases),
then:

```sh
tar xzf xcull-*-linux-x86_64.tar.gz
sudo install -m755 xcull /usr/local/bin/xcull
```

Releases include `linux-x86_64` and `linux-arm64`. Each archive ships
with a `.sha256` next to it.

<details>
  <summary>Verify the checksum</summary>

```sh
curl -LO https://github.com/xcull/xcull/releases/latest/download/xcull-v1.0.1-linux-x86_64.tar.gz
curl -LO https://github.com/xcull/xcull/releases/latest/download/xcull-v1.0.1-linux-x86_64.tar.gz.sha256
sha256sum -c xcull-v1.0.1-linux-x86_64.tar.gz.sha256
```

</details>

### From source

```sh
git clone https://github.com/xcull/xcull
cd xcull
make
sudo make install
```

<details>
  <summary>Build options (PREFIX, DESTDIR, CC, CFLAGS)</summary>

`make install` honors `PREFIX` (default `/usr/local`) and `DESTDIR` for
packaging. The man page and bash + zsh completion files install
alongside the binary, so `man xcull` works after install and tab-completion
picks up the flags. `make test` runs the golden-output regression suite;
`make benchmark` additionally builds `runstat`, the fork + wait4 +
getrusage harness used by the benchmark.

```sh
# install to a custom prefix
make install PREFIX=$HOME/.local

# package into a staged DESTDIR (for distro packagers)
make install DESTDIR=/tmp/staging PREFIX=/usr

# build with clang
make CC=clang

# build with extra warnings
make CFLAGS="-O3 -march=native -flto -Wall -Wextra"
```

</details>

<details>
  <summary>Uninstall</summary>

```sh
sudo make uninstall
```

Removes the binary, man page, and shell completion files installed by
`make install`. Honors the same `PREFIX` and `DESTDIR` variables.

</details>

## Usage

xcull reads URLs from stdin and writes the deduplicated set to stdout.
The normal case is a one-line pipe with no flags.

```sh
cat urls.txt | xcull
```

The full flag list (run `man xcull` after install for the long form):

```
usage: xcull [-F fold-ids][-x keep-invalid][-a keep-assets]
             [-s case-sensitive][-L N subset-cmp cap]
             [-k][-p][-W][-r][-V]

  -F     fold object-ids (numeric/UUID/hex/stem-id segments collapse
         to one witness). Default keeps every distinct id; -F is the
         aggressive endpoint-discovery mode.
  -x     keep invalid URLs, fully raw, no cleaning.
  -a     keep all assets (do not filter images/fonts/css/audio/video
         like .css/.png/.woff/.mp4/.mp3/.m4p/...).
  -s     case-sensitive path matching.
  -L N   cap subset-merge comparisons per inserted record (0 = off,
         the default). Safety valve for adversarial multi-cardinality
         antichains; when it trips, the record is kept un-merged so
         output may differ from a full run.
  -k     keep param values and every distinct query key-set as its own
         line (dedup on the full query; disables the default
         query-subset merge and restores streaming output).
  -p     no path templating at all (also drops the title-slug fold).
  -W     opt out of wayback-noise handling.
  -r     opt out of URL canonicalization.
  -V     print "xcull: <in> -> <out> (peak RSS <n> KB)" to stderr.
```

## Running xcull

xcull always reads URLs from stdin, one per line, and writes the cleaned
set to stdout. There is no `-l` or `-u` flag; input shape is controlled
by the shell. Empty lines and leading or trailing whitespace are
tolerated.

### Piped input (stdin)

```sh
gau example.com | xcull
```

This is the canonical use: a recon feed pipes its output straight into
xcull, which streams the deduped set onward.

### File input

```sh
xcull < urls.txt
```

Equivalent to `cat urls.txt | xcull`. Use whichever your pipeline reads
more naturally.

### Multiple sources

```sh
cat gau.txt waybackurls.txt katana.txt | xcull > urls.txt
```

xcull dedupes the union, so combining multiple recon sources costs no
more than running it once. Order does not matter for the default mode:
xcull defers emission so a later record can retroactively un-emit an
earlier one when their templated path is the same.

### Pre-sorted input

xcull never assumes input is sorted; output order is deterministic with
respect to first-seen arrival under a given flag set. Two runs over the
same input produce byte-identical output.

## Examples

A few common shapes; per-flag use cases follow in the next section.

```sh
# clean recon surface from an archive feed
gau example.com | xcull > surface.txt
```

```sh
# combine multiple sources, dedupe once
cat gau.txt waybackurls.txt katana.txt | xcull | tee urls.txt
```

```sh
# feed a param-fuzzing pipeline
gau example.com | xcull | qsreplace FUZZ | anew params.txt
```

```sh
# show the reduction (stats on stderr, data still on stdout)
cat urls.txt | xcull -V > deduped.txt
```

## Flag use cases

Each flag has a narrow purpose. The default mode is the answer 90% of
the time; reach for a flag only when its specific use case applies.

### Default (no flags)

**When:** the normal recon pass. Stream an archive feed or crawler
output through xcull and pipe the cleaned set to whatever consumes URLs
next.

```sh
gau example.com | xcull > urls.txt
```

In default mode every distinct object id (`/user/41`, `/user/42`),
session token (`;jsessionid=...`), and GraphQL operation
(`?query={me{id}}`) survives, query URLs merge only by subset
relation, render-noise assets are dropped, and wayback / scanner-probe
junk is filtered.

### `-F` fold object ids (route discovery)

**When:** you want one witness per endpoint pattern, not per object.
Useful for a route-scan pass where the concrete IDs are noise.

```sh
gau example.com | xcull -F > routes.txt
```

Input:

```
https://example.com/user/41
https://example.com/user/42
https://example.com/user/43
https://example.com/file/550e8400-e29b-41d4-a716-446655440000
https://example.com/file/6ba7b810-9dad-11d1-80b4-00c04fd430c8
```

Default output keeps all five (so IDOR / BOLA enumeration sees every
id). `-F` collapses each id class to one witness, leaving two lines
(one `/user/<N>`, one `/file/<UUID>`). Standard recon pattern: run
default first for the IDOR pass, then re-run with `-F` for route
coverage.

### `-x` keep invalid URLs (forensic / debug)

**When:** you suspect the default cleaner is dropping something it
should keep, or you want to audit the raw structural dedup without any
sanity gate.

```sh
cat dirty.txt | xcull -x > raw_dedup.txt
```

`-x` disables the garbage gate (glued TLDs, embedded-domain probes,
malformed bytes, scanner artifacts) but keeps the structural dedup
itself. Use it side-by-side with a default run to see which lines were
classified as junk.

```sh
diff <(cat dirty.txt | xcull) <(cat dirty.txt | xcull -x) | less
```

### `-a` keep all assets (source maps, JS, secrets-in-static)

**When:** you are hunting for secrets in JS bundles, source maps, or
static config files that the default render-noise filter would drop.

```sh
gau example.com | xcull -a | grep -E '\.(js|map|json|env)$'
```

Default drops `.css .png .woff .mp4 .mp3 .m4p .svg .ico` and the rest
of the static-render extensions. `-a` keeps them. Note: `.map` URLs
are kept under the default too (source maps disclose unminified source
and are a standing recon finding), so reach for `-a` mainly for the
other static classes.

### `-s` case-sensitive path matching

**When:** the target runs on a case-sensitive backend (Java/JSP, some
Python/Node frameworks) where `/Admin` and `/admin` are distinct
routes.

```sh
cat urls.txt | xcull -s
```

Default folds path case, so `/Login` and `/login` collide into one
witness. `-s` keeps them separate. On Apache/IIS/PHP targets the
default is what you want.

### `-L N` cap subset-merge comparisons (adversarial input)

**When:** the input has a single endpoint accumulating thousands of
distinct query key-sets (a fuzzer dump, a telemetry log, a
cache-buster spam). The default subset-merge is O(K) per insert into a
bucket; an adversarial multi-cardinality antichain can still cost real
time.

```sh
cat fuzzer_dump.txt | xcull -L 100 -V
```

`-L N` caps comparisons per inserted record at `N`. When the cap
trips, the record is kept un-merged (output may differ from a full
run; this is a safety valve, not a quality knob). `0` (default) means
no cap.

### `-k` keep param values and every distinct key-set (value mining)

**When:** you are mining parameter values, not endpoints. You want to
see every concrete value of `?role=`, `?env=`, `?debug=`, and you
don't want xcull's query-subset merge to collapse anything.

```sh
gau example.com | xcull -k | grep -oE '\?[a-z_]+=[^&]+' | sort -u
```

```sh
# look for admin / debug / staging param values
gau example.com | xcull -k | grep -iE '\?(role|env|debug|admin)='
```

Default merges `/page?id=1` and `/page?id=2` into one witness because
the key-set is identical. `-k` keeps both, including the values. As a
side effect, `-k` also restores streaming output (no deferred emit),
so it is the lowest-RSS mode.

### `-p` no path templating (literal paths)

**When:** the target's path segments are meaningful, not templated.
Docs sites, content portals, knowledge bases where `/docs/install`
and `/docs/config` are distinct content, not slugs of the same route.

```sh
gau docs.example.com | xcull -p > docs_paths.txt
```

Default folds title-slug groups (`/blog/<slug>` collapses to one
witness). `-p` disables every path template, including the slug fold.
Use it when you would rather hand off the full path inventory to a
doc-aware scanner.

### `-W` opt out of wayback-noise handling

**When:** the input is from a live crawl (katana, hakrawler, hand
recon) and not from wayback, so the wayback-noise heuristics are
overhead with nothing to match. Or, the input IS wayback but you want
to keep scanner-artifact URLs (e.g., to study prior attacker activity
recorded by the archive).

```sh
katana -u https://example.com -silent | xcull -W
```

```sh
# keep wayback's scanner probes (for threat-intel work)
waybackurls example.com | xcull -W | grep -E '(\.\./|/etc/passwd|<script>)'
```

### `-r` opt out of URL canonicalization

**When:** you need byte-exact comparison with another tool, the input
is already RFC 3986 normalized, or you are debugging an encoding issue
xcull's canonicaliser might be hiding.

```sh
cat urls.txt | xcull -r | diff - <(cat urls.txt | xcull)
```

Default applies percent-decode for safe bytes, normalises default
ports, lowercases hosts, and resolves `.` / `..` segments. `-r`
disables all of that and emits the first-seen line verbatim.

### `-V` verbose stats (CI, monitoring)

**When:** you want a one-line summary of the run (input lines, output
lines, peak RSS) for logs, CI, or a quick sanity check. Stdout is
unchanged, so `-V` is safe to leave on in production pipelines.

```sh
gau example.com | xcull -V > urls.txt 2>> xcull.log
```

```sh
# CI assertion: dedup ratio should be at least 5x
in=$(wc -l < urls.txt)
out=$(xcull -V < urls.txt 2>&1 > deduped.txt | awk '{print $4}')
test $(( in / out )) -ge 5 || { echo "dedup too weak"; exit 1; }
```

## Output

xcull writes cleaned URLs to stdout, one per line, in deterministic
first-seen order. Stderr is reserved for diagnostics.

### stdout

The deduped URL set. Pipe it to a file, a tool that reads from stdin
(`qsreplace`, `nuclei`, `ffuf`, `httpx`, `gf`), or `tee` for both.

```sh
cat urls.txt | xcull > deduped.txt
```

### stderr (`-V`)

With `-V`, xcull prints a single summary line to stderr after the run:

```
xcull: 782143 -> 55920 (peak RSS 22612 KB)
```

The numbers are: input lines read, output lines emitted, peak resident
set size in kilobytes. stdout is unaffected, so `-V` is safe to add to
production pipelines.

```sh
cat urls.txt | xcull -V > deduped.txt
# stderr: xcull: 782143 -> 55920 (peak RSS 22612 KB)
```

### Exit codes

| Code | Meaning |
|------|---------|
| 0    | Success. Output written. |
| 1    | I/O error on stdin or stdout. |
| 2    | Unknown flag or malformed argument. |

## Pipeline integration

xcull is a stdin-stdout filter. Anything that emits URLs upstream can
feed it; anything that consumes URLs downstream can read from it.

### With archive feeds

```sh
# gau (https://github.com/lc/gau)
gau example.com | xcull > urls.txt

# waybackurls (https://github.com/tomnomnom/waybackurls)
waybackurls example.com | xcull > urls.txt

# both, deduped once
( gau example.com; waybackurls example.com ) | xcull > urls.txt
```

### With active crawlers

```sh
# katana (https://github.com/projectdiscovery/katana)
katana -u https://example.com -silent | xcull > urls.txt

# hakrawler (https://github.com/hakluke/hakrawler)
echo https://example.com | hakrawler | xcull > urls.txt
```

### Feeding parameter fuzzers

```sh
# qsreplace + ffuf-style param fuzz
gau example.com | xcull | qsreplace FUZZ | sort -u > params.txt

# only URLs with query strings
cat urls.txt | xcull | grep '?' > queried.txt
```

### Feeding vulnerability scanners

```sh
# nuclei (https://github.com/projectdiscovery/nuclei)
cat urls.txt | xcull | nuclei -t exposures/

# httpx liveness check before scanning
cat urls.txt | xcull | httpx -silent | nuclei
```

### Composing with anew

```sh
# keep only URLs not seen before
gau example.com | xcull | anew urls.txt
```

## Benchmark

Reproducible head-to-head against `urldedupe`, `uro`, `urless`, and
`uddup` on a single labeled 780,200-URL input (`D_unified.full`, 55,920
ground-truth canonical endpoint groups). Peak RSS, throughput,
completion time, false merge rate, per-class PRF, and every CSV behind
every claim live in a separate repo:

**[github.com/xcull/xcull-benchmark](https://github.com/xcull/xcull-benchmark)**

A 99-row side-by-side demo (the kind of differences a recon engineer
notices at a glance: object IDs, session tokens, slug folding, query
keyset merges) is at
**[xcull-benchmark/COMPARISON.md](https://github.com/xcull/xcull-benchmark/blob/main/COMPARISON.md)**.

## Reporting issues

To keep the issue tracker focused on actionable items, please:

- **Bug reports** go through the
  [issue template](https://github.com/xcull/xcull/issues/new?template=bug_report.yml).
  Include a minimal reproducer: command line, the smallest input that
  triggers the problem, expected output, actual output. Five-line
  repros land fastest.
- **Feature requests** go through the
  [feature template](https://github.com/xcull/xcull/issues/new?template=feature_request.yml).
  If the proposal merges URLs that survive today, argue why the merged
  URLs do not represent distinct attack surface.
- **Security vulnerabilities** must not be filed as public issues. Use
  GitHub private vulnerability reporting at
  [github.com/xcull/xcull/security/advisories/new](https://github.com/xcull/xcull/security/advisories/new).
  See [SECURITY.md](SECURITY.md) for scope.
- **Usage questions** and pipeline integration help are welcome under
  [Discussions](https://github.com/xcull/xcull/discussions).

See [CONTRIBUTING.md](CONTRIBUTING.md) for the patch bar and release
flow.

## Sponsor

<div align="center">

<a href="https://github.com/sponsors/xcull"><img src="https://img.shields.io/badge/Sponsor%20xcull-EA4AAA?style=for-the-badge&logo=github-sponsors&logoColor=white" alt="Sponsor xcull"></a>

If you would like to support this project, you can become a sponsor at
**[github.com/sponsors/xcull](https://github.com/sponsors/xcull)**.

</div>

## License

Source-available under the **Xcull Source Available License (XSAL) v1.0**.
Free for personal use, bug-bounty research, and non-commercial security
work. See the full [LICENSE](LICENSE.md) for terms.

### Commercial and OEM licensing

Embedding xcull in a commercial product, hosted service, SaaS platform,
or appliance requires a separate license. Pricing tiers and contact
details are in [XCOL.md](XCOL.md).

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for the release log.
