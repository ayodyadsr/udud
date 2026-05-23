<h1 align="center">udud</h1>

<p align="center">
  <b>URL Deduplicate Data</b><br>
  Fast URL deduplication without losing important attack targets.
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-blue.svg"></a>
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-18.6MB%20%2F%20781k%20URLs-success.svg">
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

**udud** removes duplicate and useless URLs from recon and pentest results while still keeping important URLs that may contain vulnerabilities.

The goal is not to make the output as small as possible. The goal is to make the output cleaner without losing real attack surface.

## Features

| Raw Input URL | Other Tools | udud (Default) | Why udud is Better? |
|---|---|---|---|
| `https://api.target.com/v2/tenant/100/billing` | 🔴 DROPPED | 🟢 KEPT | Critical BOLA / IDOR target. Many tools remove sequential IDs after seeing similar paths, which can hide cross-tenant authorization issues. |
| `https://api.target.com/v2/tenant/100/billing/invoice/pdf` | 🔴 DROPPED | 🟢 KEPT | Deep nested API endpoint. udud preserves unique functionality deeper in the routing structure. |
| `https://target.com/dashboard/settings/v1/alpha-feature` | 🔴 DROPPED | 🟢 KEPT | Hidden attack surface. Feature-flag or dynamic frontend paths may expose internal or admin functionality. |
| `https://api.target.com/v2/user?debug=true&env=staging` | 🔴 DROPPED | 🟢 KEPT | Non-overlapping parameters. Debug or staging parameters may reveal sensitive behavior and are still useful for fuzzing. |
| `https://internal-service.target.com/v1/health` | 🔴 DROPPED | 🟢 KEPT | Multi-domain awareness. udud keeps separate hosts and microservices even if the URL structure looks repetitive. |
| `https://target.com/v2/auth/session;jsessionid=abc123xyz` | 🔴 DROPPED | 🟢 KEPT | Matrix-parameter awareness. Session-related parameters are preserved because they may affect authentication or routing behavior. |
| `https://api.target.com/v2/user?id=1` | 🟢 KEPT | 🔴 DROPPED | Smart parameter merging. If a richer parameter set already exists, udud removes smaller redundant variants to reduce duplicate fuzzing. |
| `https://target.com/backup/v1/export.phps` | 🔴 DROPPED | 🟢 KEPT | Source disclosure protection. Sensitive extensions like `.phps` or backup files are preserved because they may leak source code. |
| `https://target.com/assets/videos/promo_main.m4a` | 🟢 KEPT | 🔴 DROPPED | Smart noise filtering. Static media files are removed to keep recon results cleaner and more focused. |


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
usage: udud [-F] [-x] [-a] [-s] [-k] [-p] [-W] [-r] [-V]

  -F   fold object-ids (numeric/UUID/hex/stem-id segments collapse to one
       witness). Default keeps every distinct id; -F is the aggressive
       endpoint-discovery mode.
  -x   keep invalid URLs, fully raw, no cleaning
  -a   keep all assets (do not filter images/fonts/css/audio/video like
       .css/.png/.woff/.mp4/.mp3/.m4p/...)
  -s   case-sensitive path matching
  -k   keep param values and every distinct query key-set as its own line
       (dedup on the full query; disables the default query-subset merge
       and restores streaming output)
  -p   no path templating at all (also drops the title-slug fold)
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

# keep every distinct user-id for IDOR / object enumeration (this is the
# default), then collapse them to unique endpoints for a route-scan pass
cat urls.txt | udud           # /user/41 and /user/42 both survive
cat urls.txt | udud -F        # they collapse to one endpoint witness
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

The published figures were measured with id-folding on, which since v18 is
the `-F` mode. To reproduce them, run `udud -F`. The v18 default preserves
object-ids and so emits slightly more lines (it recovers the per-object
IDOR surface); on these corpora the difference is small because they are
query- and slug-dominated rather than id-dominated.

### Parameters

| field | value |
|---|---|
| corpora | `D_synth.full` (45,410 lines, 12 pattern classes, 319 canonical ground-truth groups), `D_example_wb.full` (781,398 lines, Wayback Machine capture of a commercial target, de-identified), `D_example_gau.full` (44,943 lines, gau), `D_vulnweb.full` (15,185 lines, public vulnweb) |
| trials | N = 10 timed + 1 warmup per (tool, corpus) cell |
| statistic | mean wall with Student t 95% confidence interval; standard deviation, median and CoV recorded in `raw/summary.csv` |
| clock | CPU governor = `performance` on all cores, `intel_pstate/no_turbo = 1`, `taskset -c 2`, ASLR off, page cache pre-warmed |
| hardware | x86_64, 8 cores, 16 GB RAM, Linux 6.12 (env manifest in `raw/environment.txt`) |
| compiler | `cc -O3 -march=native -flto -Wall -Wno-misleading-indentation` |
| versions | udud v15, uro 1.0.2, urldedupe 1.0.4, urless 2.7, uddup 0.9.3 |
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
| **udud v15 (Ours)** | 0.250 ± 0.002 | **4.0 MB** 🥇 | High (O(n)) | **5,310** 🥇 | **99.61%** 🥇 | **91.67%** 🥇 |
| urldedupe 1.0.4 | **0.235 ± 0.004** 🥇 | 15.5 MB | Moderate (RAM Bound) | 25,415 | **100.00%** | 50.01% |
| uro 1.0.2 | 1.066 ± 0.012 | 17.8 MB | Low (Python Bound) | 5,310 | 83.07% | 75.00% |
| urless 2.7 | 1.448 ± 0.021 | 30.7 MB | Unfeasible | 5,311 | 91.40% | 83.33% |
| uddup 0.9.3 | 252.32 ± 3.05 | 21.8 MB | Failed (O(n²)) | 20,322 | 85.70% | 54.17% |

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
representative per canonical group. On this garbage-free synthetic corpus
urldedupe wins wall time by 15 ms (an exact-byte hash does no structural
work, and the dataset has nothing for udud's noise gates to catch), but
its output is 4.8× larger than udud's, dropping Precision from 91.67% to
50.01%, and it uses the lowest peak memory of no tool here: udud holds
that at 4.0 MB. On every real corpus (Tables 2 and 3) udud is faster than
urldedupe as well.

### Table 2: D_example_wb.full (Wayback, 781,398 lines, 134.5 MB)

| Target Tool | Execution Time (Wall Time in sec) | Peak Memory (Peak RSS in MB) | Throughput Scalability | Output Volume (Retained URLs) | Recall (R<sub>as</sub>) (Attack Surface Kept) | Precision (P<sub>as</sub>) (Duplication Cleaned) |
|---|---:|---:|---|---:|---:|---:|
| **udud v15 (Ours)** | **7.796 ± 0.032** 🥇 | **18.6 MB** 🥇 | High (O(n)) | 125,837 | **100.00%** | 91.40% |
| urldedupe 1.0.4 | 9.249 ± 0.059 | 335.9 MB | Moderate (RAM Bound) | 293,420 | **100.00%** | 42.80% |
| uro 1.0.2 | 39.960 ± 0.355 | 35.0 MB | Low (Python Bound) | 78,470 | 62.40% | 98.10% |
| urless 2.7 | 165.228 ± 0.788 | 45.4 MB | Unfeasible | 74,737 | 59.50% | **99.20%** 🥇 |
| uddup 0.9.3 | DNF (> 300 s) | n/a | Failed (O(n²)) | n/a | n/a | n/a |

On the real Wayback corpus R<sub>as</sub> and P<sub>as</sub> are
system-level: R<sub>as</sub> = canonical endpoint groups retained / total
canonical groups in the corpus (udud's signature canonicalization as the
non-destructive reference), P<sub>as</sub> = canonical groups retained
/ output line count. udud holds the Pareto frontier on every axis: it is
the fastest finisher (16% ahead of urldedupe, 7.796 s against 9.249 s,
non-overlapping CIs), uses 18.0× less memory, and combines 100.00%
Recall with 91.40% Precision against urldedupe's 42.80%.

### Table 3: Scaling, wall time and peak RSS across Wayback prefix slices

| Lines | udud wall / RSS | urldedupe wall / RSS | uro wall | urless wall |
|---:|---|---|---|---|
| 25,000 | 0.249 s / 3.3 MB | 0.341 s / 17.6 MB | 1.005 s | 1.195 s |
| 50,000 | 0.455 s / 3.5 MB | 0.659 s / 28.9 MB | 1.987 s | 2.325 s |
| 100,000 | 0.906 s / 3.8 MB | 1.509 s / 52.7 MB | 5.372 s | 15.123 s |
| 200,000 | 1.744 s / 3.9 MB | 2.956 s / 98.9 MB | 16.064 s | 54.747 s |
| 400,000 | 3.326 s / 3.9 MB | 5.955 s / 180.7 MB | 28.776 s | 130.255 s |
| 781,398 | 7.796 s / 18.6 MB | 9.249 s / 335.9 MB | 39.960 s | 165.228 s |

udud wall time is linear in input size and faster than urldedupe at every
size; peak RSS tracks the count of distinct signatures (flat near 3.3 to
3.9 MB while the prefix is host-saturated, then 18.6 MB on the full
corpus's 125,837 unique signatures). urldedupe peak RSS is strictly
linear in input size. urless grows super-linearly. uddup is O(n²) and
does not finish above ~50k lines under the 300 s cap.

### Theoretical complexity

| Tool | Time | Space |
|---|---|---|
| **udud** | O(n) single-pass: one hash lookup + insert per line | O(s) where s = number of distinct signatures emitted (≤ n, typically n/6 on recon data) |
| urldedupe | O(n) single-pass, exact-byte dedup | O(n): every input URL retained in RAM as a separate entry |
| uro | O(n log n) due to dict-of-list build + sort over the parameter set | O(n): full URL list + parameter dict |
| urless | O(n · k) where k is the keyword/extension/pattern filter set; per-URL Python regex overhead dominates | O(n): host-keyed nested dict, full input retained |
| uddup | O(n²) pairwise structural comparison | O(n) |

### Relative position

- vs `urldedupe`: on the 781k-line corpus udud is **16% faster** and uses
  **18.0× less memory**, while delivering Macro-F1 0.91 vs urldedupe's
  0.53 on the synthetic ground truth, since urldedupe keeps every URL
  because it does not structurally fold.
- vs `uro`: udud is **5.1× faster**, uses **1.9× less memory**, and
  Macro-F1 0.91 vs 0.75 (uro destroys the UUID and TITLE_SLUG classes
  on synthetic and 89% of `.js` plus 100% of `;jsessionid=` routes on
  the wayback corpus).
- vs `urless`: udud is **21.2× faster**, uses **2.4× less memory**, and
  Macro-F1 0.91 vs 0.83 (urless destroys TITLE_SLUG on synthetic and
  the same `.js` + auth surface as uro on wayback).
- vs `uddup`: udud finishes; uddup does not. On the 45k-URL synthetic
  where uddup completes, Macro-F1 0.91 vs 0.52 (uddup destroys
  CACHE_BUST entirely and 52% of GENUINE_DISTINCT endpoints).

### Summary

On the synthetic ground-truth dataset udud has the highest Attack-Surface
F1 of all five tools (0.9147) and the lowest peak RSS; urldedupe is 15 ms
faster on wall time on this garbage-free corpus but its F1 is 0.53 because
it does no structural folding. On the 781,398-line real wayback corpus
udud is the fastest finisher outright, the lowest in peak memory, and a
complete per-line audit of every removed URL records zero real attack
surface lost with two small documented design-boundary residuals
(`AUDIT.md`). Every other tool gives up at least one of those three.

## How it works

Each line goes through one pass: repair the scheme, normalize host and
port, run host sanity gates, split path / query / fragment, drop
garbage, then build a dedup signature. In the signature, a title slug
becomes `S`. A numeric segment becomes `N` only when its parent is a
content/listing word (`/cat/9` to `/cat/N`); a numeric anywhere else
stays verbatim so distinct object-ids stay distinct. UUID, hex of length
12 or more, and `stem-<id>` are also kept verbatim by default. Under `-F`
every id is templated (digits to `N`, UUID to `U`, long hex to `H`,
`stem-<id>` to `stem-#`) regardless of context.

The query decides the grouping. A URL with no query gets its own
signature. Query URLs of one path are grouped by path alone, then pruned
by subset: among the query variants of a path udud keeps the antichain of
maximal key-sets, dropping a variant only when its keys are a subset of a
kept one (the survivor loses no parameter). Variants with non-overlapping
keys all stay, and the no-query URL stays a separate group. Because a
covering superset can arrive after a subset, the default output is
buffered and written at end of input, the kept real URLs in first-seen
order. Pass `-k` to put the full query in the signature instead, which
keeps every distinct key-set on its own line and restores streaming
output.

Only the signature is compared and the URL that gets printed is never
rewritten, just selected. Memory scales with the number of unique
signatures plus, in the default mode, the kept output lines held until
end of input; under `-k` / `-x` there is no survivor buffer and output
streams one line at a time.

## License

[GNU Affero General Public License v3.0](LICENSE)
