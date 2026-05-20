<h1 align="center">udud</h1>

<p align="center">
  <b>URL Deduplicate Data</b><br>
  Single-pass URL structural de-duplicator that keeps your attack surface.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-blue.svg"></a>
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-18.4MB%20%2F%20781k%20URLs-success.svg">
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
  cross-line buffer. Memory is the set of unique signatures seen so far,
  not constant: it measures 18 MB on a 133 MB / 781k-line input, and
  stays in single digits on smaller inputs. It reads input at about
  15 MB/s.
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

The full report, the statistics harness, every per-trial timing, and a
per-line audit of every removed URL are in
[ayodyadsr/udud-benchmark](https://github.com/ayodyadsr/udud-benchmark)
([BENCHMARK.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/BENCHMARK.md),
[AUDIT.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/AUDIT.md),
[ANONYMIZATION.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/ANONYMIZATION.md)).
The numbers below are quoted verbatim from that report. The cell at the
top is the largest corpus; the benchmark repo carries the prefix slices
(25k, 50k, 100k, 200k, 400k, full), the gau corpus, and the public
vulnweb corpus as well.

### Parameters

| field | value |
|---|---|
| corpora | `D_synth.full` (45,410 lines, 12 pattern classes, 319 canonical ground-truth groups), `D_example_wb.full` (781,398 lines, Wayback Machine capture of a commercial target, de-identified), `D_example_gau.full` (44,943 lines, gau), `D_vulnweb.full` (15,185 lines, public vulnweb) |
| trials | N = 10 timed + 1 warmup per (tool, corpus) cell |
| statistic | mean wall with Student t 95% confidence interval; standard deviation, median and CoV recorded in `raw/summary.csv` |
| clock | CPU governor = `performance` on all cores, `intel_pstate/no_turbo = 1`, `taskset -c 2`, ASLR off, page cache pre-warmed |
| hardware | x86_64, 8 cores, 16 GB RAM, Linux 6.12 (env manifest in `raw/environment.txt`) |
| compiler | `cc -O3 -march=native -flto -Wall -Wno-misleading-indentation` |
| versions | udud v14, uro 1.0.2, urldedupe 1.0.4, urless 2.7, uddup 0.9.3 |
| determinism | output sha256 stable across all 10 trials of every cell, recorded in `raw/trials.csv` |
| time-out | 300 s per trial, otherwise marked DNF |
| quality framework | Attack-Surface Precision / Recall / F1, computed on (a) a synthetic ground-truth dataset where every URL is labelled with its canonical (class, group) and (b) the wayback corpus under an RFC 3986 canonicalization shared between truth and every tool |

### Quality framework

Each tool is graded on two metric groups: Computational Efficiency
(wall time, peak memory, scalability class) and Attack Surface
Fidelity (Accuracy). Per tool:

| Metric | Definition |
|---|---|
| Execution Time (Wall Time in sec) | mean of N=10 timed runs with Student-t 95% CI |
| Peak Memory (Peak RSS in MB) | max `ru_maxrss` across trials |
| Throughput Scalability | theoretical complexity class and observed asymptote |
| Output Volume (Retained URLs) | output line count |
| Recall (R<sub>as</sub>) (Attack Surface Kept) | canonical endpoint groups retained / total canonical groups in the corpus |
| Precision (P<sub>as</sub>) (Duplication Cleaned) | canonical endpoint groups retained / output line count |

A correct deduplicator scores high on BOTH R<sub>as</sub> (it did not
destroy real surface) AND P<sub>as</sub> (it did not bloat the output
with duplicates). A passthrough scores high on R<sub>as</sub> only; an
over-aggressive filter scores high on P<sub>as</sub> only. F1 = 2·P·R /
(P+R) is the harmonic mean and is reported alongside.

### Table 1: D_synth.full (synthetic ground truth, 45,410 URLs, 12 classes, 319 canonical groups)

| Target Tool | Execution Time (Wall Time in sec) | Peak Memory (Peak RSS in MB) | Throughput Scalability | Output Volume (Retained URLs) | Recall (R<sub>as</sub>) (Attack Surface Kept) | Precision (P<sub>as</sub>) (Duplication Cleaned) |
|---|---:|---:|---|---:|---:|---:|
| **udud v14 (Ours)** | 0.214 | **12.3 MB** 🥇 | High (O(n)) | **5,310** 🥇 | **99.61%** 🥇 | **91.67%** 🥇 |
| urldedupe 1.0.4 | **0.164** 🥇 | 15.5 MB | Moderate (RAM Bound) | 25,415 | **100.00%** | 50.01% |
| uro 1.0.2 | 0.565 | 17.7 MB | Low (Python Bound) | 5,310 | 83.07% | 75.00% |
| urless 2.7 | 0.715 | 30.6 MB | Unfeasible | 5,311 | 91.40% | 83.33% |
| uddup 0.9.3 | 139.11 | 21.8 MB | Failed (O(n²)) | 20,322 | 85.70% | 54.17% |

Recall and Precision on the synthetic dataset are class-uniform macro
averages over the 12 pattern classes (NUMERIC_ID, UUID, HEX_HASH,
TITLE_SLUG, CACHE_BUST, JSESSIONID, OPEN_REDIRECT, LFI_PARAM,
PARAM_ORDER, TRAILING_SLASH, GENUINE_DISTINCT × 190, SRCDISC × 20), so
losing the LFI class and losing the cache-bust class count equally.
The corresponding Macro-F1 = 2·P·R/(P+R): **udud 0.9147**, urless 0.8320,
uro 0.7487, urldedupe 0.5279, uddup 0.5175. udud wins F1 by 8.3 points
because it is the only tool that simultaneously preserves the LFI /
open-redirect / JSESSIONID parameter surface AND folds value-variant
classes (numeric ID, UUID, hex hash, title slug, cache bust) into one
representative per canonical group. urldedupe wins wall time by 50 ms
(exact-byte hash with no structural folding) but its output is 4.8×
larger than udud's, dropping Precision from 91.67% to 50.01%.

### Table 2: D_example_wb.full (Wayback, 781,398 lines, 134.5 MB)

| Target Tool | Execution Time (Wall Time in sec) | Peak Memory (Peak RSS in MB) | Throughput Scalability | Output Volume (Retained URLs) | Recall (R<sub>as</sub>) (Attack Surface Kept) | Precision (P<sub>as</sub>) (Duplication Cleaned) |
|---|---:|---:|---|---:|---:|---:|
| **udud v14 (Ours)** | **9.364 ± 0.296** 🥇 | **18.4 MB** 🥇 | High (O(n)) | 125,837 | **100.00%** | 91.40% |
| urldedupe 1.0.4 | 9.412 ± 0.062 | 335.9 MB | Moderate (RAM Bound) | 293,420 | **100.00%** | 42.80% |
| uro 1.0.2 | 39.763 ± 0.184 | 35.1 MB | Low (Python Bound) | 78,470 | 62.40% | 98.10% |
| urless 2.7 | 172.161 ± 1.024 | 45.3 MB | Unfeasible | 74,737 | 59.50% | **99.20%** 🥇 |
| uddup 0.9.3 | DNF (> 300 s) | n/a | Failed (O(n²)) | n/a | n/a | n/a |

On the real Wayback corpus R<sub>as</sub> and P<sub>as</sub> are
system-level: R<sub>as</sub> = canonical endpoint groups retained / total
canonical groups in the corpus (udud's signature canonicalization as the
non-destructive reference), P<sub>as</sub> = canonical groups retained
/ output line count. udud holds the Pareto frontier: matches urldedupe
on wall time within the CI, uses 18.3× less memory, and combines 100.00%
Recall with 91.40% Precision against urldedupe's 42.80%.

### Table 3: Scaling — wall time and peak RSS across Wayback prefix slices

| Lines | udud wall / RSS | urldedupe wall / RSS | uro wall | urless wall |
|---:|---|---|---|---|
| 25,000 | 0.288 s / 3.1 MB | 0.351 s / 17.6 MB | 1.005 s | 1.198 s |
| 50,000 | 0.533 s / 3.4 MB | 0.666 s / 28.9 MB | 2.008 s | 2.358 s |
| 100,000 | 1.026 s / 3.6 MB | 1.430 s / 52.7 MB | 5.408 s | 15.495 s |
| 200,000 | 1.922 s / 3.6 MB | 2.973 s / 99.0 MB | 16.129 s | 57.325 s |
| 400,000 | 3.695 s / 3.6 MB | 5.970 s / 180.7 MB | 28.515 s | 134.006 s |
| 781,398 | 9.364 s / 18.4 MB | 9.412 s / 335.9 MB | 39.763 s | 172.161 s |

udud wall time is linear in input size; peak RSS tracks the count of
distinct signatures (flat at 3.6 MB while the prefix is host-saturated,
then 18.4 MB on the full corpus's 125,837 unique signatures). urldedupe
peak RSS is strictly linear in input size. urless grows super-linearly.
uddup is O(n²) and does not finish above ~50k lines under the 300 s cap.

### Theoretical complexity

| Tool | Time | Space |
|---|---|---|
| **udud** | O(n) single-pass: one hash lookup + insert per line | O(s) where s = number of distinct signatures emitted (≤ n, typically n/6 on recon data) |
| urldedupe | O(n) single-pass, exact-byte dedup | O(n) — every input URL retained in RAM as a separate entry |
| uro | O(n log n) due to dict-of-list build + sort over the parameter set | O(n) — full URL list + parameter dict |
| urless | O(n · k) where k is the keyword/extension/pattern filter set; per-URL Python regex overhead dominates | O(n) — host-keyed nested dict, full input retained |
| uddup | O(n²) pairwise structural comparison | O(n) |

### Relative position

- vs `urldedupe`: udud matches its wall time within the confidence
  interval and uses **18.3× less memory**, while delivering Macro-F1
  0.91 vs urldedupe's 0.53 on the synthetic ground truth — urldedupe
  keeps every URL because it does not structurally fold.
- vs `uro`: udud is **4.2× faster**, uses **1.9× less memory**, and
  Macro-F1 0.91 vs 0.75 (uro destroys the UUID and TITLE_SLUG classes
  on synthetic and 89% of `.js` plus 100% of `;jsessionid=` routes on
  the wayback corpus).
- vs `urless`: udud is **18.4× faster**, uses **2.5× less memory**, and
  Macro-F1 0.91 vs 0.83 (urless destroys TITLE_SLUG on synthetic and
  the same `.js` + auth surface as uro on wayback).
- vs `uddup`: udud finishes; uddup does not. On the 45k-URL synthetic
  where uddup completes, Macro-F1 0.91 vs 0.52 (uddup destroys
  CACHE_BUST entirely and 52% of GENUINE_DISTINCT endpoints).

### Summary

On the synthetic ground-truth dataset udud has the highest Attack-Surface
F1 of all five tools (0.9147) and the lowest peak RSS; urldedupe is 50 ms
faster on wall time but its F1 is 0.53 because it does no structural
folding. On the 781,398-line real wayback corpus udud is the fastest
finisher that does real structural folding, the lowest in peak memory,
and a complete per-line audit of every removed URL records zero real
attack surface lost with two small documented design-boundary residuals
(`AUDIT.md`). Every other tool gives up at least one of those three.

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
