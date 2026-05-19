<h1 align="center">udud</h1>

<p align="center">
  <b>URL Deduplicate Data</b><br>
  Single-pass URL structural de-duplicator that keeps your attack surface.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-blue.svg"></a>
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-~6.6MB%20constant-success.svg">
  <a href="https://github.com/ayodyadsr/udud-benchmark"><img src="https://img.shields.io/badge/benchmark-results-orange.svg"></a>
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#installation">Installation</a> ·
  <a href="#usage">Usage</a> ·
  <a href="#examples">Examples</a> ·
  <a href="#benchmark">Benchmark</a> ·
  <a href="#how-it-works">How it works</a>
</p>

---

`udud` reads URLs from stdin and writes a deduplicated set to stdout. It
collapses URLs that hit the same endpoint (same path shape, same
parameter set, with IDs, UUIDs, hashes and slugs treated as templated)
down to the first one it saw. It does not delete the kinds of URLs a
pentester still wants to look at.

It does not try to produce the smallest output. It cuts noise without
dropping anything you would actually test.

## Features

- Non-destructive. Keeps `.js` and `.html` sources, source-disclosure
  files (`.bak`, `.sql`, `.zip`, `.phps`), open-redirect / SSRF / LFI
  parameters, and matrix-param auth endpoints like `;jsessionid=`.
  Aggressive dedupers drop these.
- Single-pass. One read of the stream, one line out at a time, no
  cross-line buffer. RAM stays around 6.6 MB on a 119 MB / 640k-line
  input. Throughput is in the hundreds of MB/s.
- Structural. Numeric IDs, UUIDs, hex digests, title slugs and
  `stem-<id>` patterns are folded only inside the dedup signature. The
  line printed is the real first-seen URL, unmodified.
- Clean by default. No flags needed. Scanner junk, payload cache values,
  mangled hosts and crawler spam are dropped without asking. Pass `-x`
  if you want the raw stream with no cleaning.
- Single C file. No runtime, no config, nothing to install but the
  binary.

## Installation

```sh
git clone https://github.com/ayodyadsr/udud
cd udud
cc -O3 -march=native -flto -Wall -Wno-misleading-indentation -o udud udud.c
sudo install -m755 udud /usr/local/bin/udud
```

Optional benchmark helper (fork + wait4 + getrusage for wall, CPU and
peak RSS):

```sh
cc -O2 -o runstat runstat.c
```

## Usage

```sh
cat urls.txt | udud
```

That is the normal case. Clean structural dedup runs by default with no
flags.

```
usage: udud [-x] [-a] [-s] [-k] [-p] [-W] [-r] [-V]

  -x   keep invalid URLs, fully raw, no cleaning
  -a   keep all assets (do not filter .css/.png/.woff/...)
  -s   case-sensitive path matching
  -k   keep param values (dedup on the full query, not just keys)
  -p   no path templating (do not fold numeric/UUID/hash/slug IDs)
  -W   opt out of wayback-noise handling
  -r   opt out of URL canonicalization
  -V   print "udud: <in> -> <out> (peak RSS <n> KB)" to stderr
```

## Examples

```sh
# clean recon surface from an archive feed
gau example.com | udud > surface.txt

# combine multiple sources, dedupe once
cat gau.txt waybackurls.txt katana.txt | udud | tee urls.txt

# feed a param-fuzzing pipeline
gau example.com | udud | qsreplace FUZZ | anew params.txt

# show the reduction (stats on stderr, data still on stdout)
cat urls.txt | udud -V > deduped.txt

# structural dedup only, skip the cleaning
cat urls.txt | udud -x
```

## Benchmark

Full 5-tool comparison and a line-by-line audit are in
[ayodyadsr/udud-benchmark](https://github.com/ayodyadsr/udud-benchmark)
([ANALYSIS.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/results/ANALYSIS.md)).

`apple.com` waybackurls, frozen 640,399-line / 119.6 MB snapshot:

| tool | output | wall | peak RSS | notes |
|---|---:|---:|---:|---|
| udud | 44 653 | 3.7 s | 6.6 MB | keeps 1790 `.html`, 28 `;jsessionid=`, 19 `.woa`, 624 `.js` |
| uro | 39 811 | 20.4 s | 28 MB | drops `.html`, all `;jsessionid=`, redirect endpoints |
| urldedupe | 133 412 | 5.2 s | 343 MB | barely dedupes (3x the lines), 52x the RAM |
| urless | 36 335 | 91.4 s | 39 MB | keyword blacklist removes blog/news, `.html`, `;jsessionid=` |
| uddup | 0 | killed at 300 s | n/a | O(n^2), no output even at a 30-min cap |

udud is the fastest, uses the least RAM, and is the only one that does
not delete real surface. Fewer output lines is not a better result here.

## How it works

Each line goes through one pass: repair the scheme, normalize host and
port, run host sanity gates, split path / query / fragment, drop
garbage, then build a dedup signature. In the signature, path segments
are templated (digits become `N`, UUID becomes `U`, hex of length 12 or
more becomes `H`, slug becomes `S`, `stem-<id>` becomes `stem-#`) and
the query is reduced to its sorted key set. The signature is looked up
in a hash set. If it is new, the original URL bytes are printed
unchanged.

Only the signature is compared. The URL that gets printed is never
rewritten. There is no survivor buffer, so memory scales with the
number of unique signatures, not with the size of the input. That is
the point of the single-pass design.

## License

[GNU Affero General Public License v3.0](LICENSE)
