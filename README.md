<h1 align="center">xcull</h1>

<p align="center">
  <b>Security-aware URL deduplicator for recon pipelines.</b><br>
  Collapses noisy recon URLs into clean attack surface while preserving exploitable patterns.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-22.6MB%20%2F%20780k%20URLs-success.svg">
  <a href="https://github.com/xcull/xcull-benchmark"><img src="https://img.shields.io/badge/benchmark-results-orange.svg"></a>
</p>

<p align="center">
  <a href="#features">Features</a> ·
  <a href="#installation">Installation</a> ·
  <a href="#usage">Usage</a> ·
  <a href="#examples">Examples</a> ·
  <a href="#benchmark">Benchmark</a> ·
  <a href="#sponsor">Sponsor</a> ·
  <a href="#license">License</a>
</p>

---

## Features

Every claim below is measured on the public 780,200-URL labeled benchmark
in [xcull-benchmark](https://github.com/xcull/xcull-benchmark).

- **IDOR/BOLA surface stays alive.** Every distinct numeric id, UUID, and hex object-id survives the dedup by default. `/api/user/1001` and `/api/user/1002` both reach your fuzzer. `uro` merges them into one witness and silently deletes the enumerable surface before you see it.
- **Session tokens are not noise.** Every distinct `;jsessionid=...`, `;sid=...`, and matrix-parameter value is kept as its own line. On the 780k-URL set, `uro` deletes 100% of JSESSIONID groups and 100% of UUID groups. `urless` deletes 100% of JSESSIONID groups. xcull keeps them all.
- **Title-slug pages are real endpoints.** `/blog/why-people-suck` and `/blog/how-to-pick-locks` stay distinct unless you opt in to folding with `-F`. The competing tools collapse all 260 title-slug groups in the benchmark to zero. Article-level IDOR and authz bugs hide here.
- **Dedup keys on the query shape, not the raw query string.** `/page?role=admin` and `/page?debug=true` survive because the parameter set is different. `/page?id=1` and `/page?id=2` collapse because only the value changed. You keep auth-bypass shapes and lose only the per-value duplicates.
- **Wayback and scanner-probe noise is filtered by default.** Old SQLi/XSS sweeps captured by archive crawlers do not reappear in your fuzzing queue. Pass `-W` to keep the raw archive.
- **22 MB peak RSS on 780k URLs.** Constant. `urldedupe` needs 194 MB for the same input. `uddup` is O(n²) and does not finish past 50k URLs. xcull runs on the smallest VPS you have, or inside a CI job, without tuning.
- **One C binary, no Python, no pip, no runtime.** Streaming stdin to stdout. Drops into `gau | xcull | qsreplace | anew` with zero glue.

## Installation

```sh
git clone https://github.com/xcull/xcull
cd xcull
cc -O3 -march=native -flto -Wall -Wno-misleading-indentation -o xcull xcull.c
sudo install -m755 xcull /usr/local/bin/xcull
```

Optional benchmark helper (fork + wait4 + getrusage for wall, CPU and
peak RSS):

```sh
cc -O2 -o runstat runstat.c
```

## Usage

```sh
cat urls.txt | xcull
```

That is the normal case. Clean structural dedup runs by default with no
flags.

```
usage: xcull [-F] [-x] [-a] [-s] [-k] [-p] [-W] [-r] [-V]

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
  -V   print "xcull: <in> -> <out> (peak RSS <n> KB)" to stderr
```

## Examples

```sh
# clean recon surface from an archive feed
gau example.com | xcull > surface.txt

# combine multiple sources, dedupe once
cat gau.txt waybackurls.txt katana.txt | xcull | tee urls.txt

# feed a param-fuzzing pipeline
gau example.com | xcull | qsreplace FUZZ | anew params.txt

# show the reduction (stats on stderr, data still on stdout)
cat urls.txt | xcull -V > deduped.txt

# structural dedup only, skip the cleaning
cat urls.txt | xcull -x

# keep every distinct user-id for IDOR / object enumeration (this is the
# default), then collapse them to unique endpoints for a route-scan pass
cat urls.txt | xcull           # /user/41 and /user/42 both survive
cat urls.txt | xcull -F        # they collapse to one endpoint witness
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

## Sponsor

<div align="center">

<a href="https://github.com/sponsors/xcull"><img src="https://img.shields.io/badge/Sponsor%20xcull-❤️-EA4AAA?style=for-the-badge&logo=github-sponsors&logoColor=white" alt="Sponsor xcull"></a>

If you would like to support this project, you can become a sponsor here:

**[github.com/sponsors/xcull](https://github.com/sponsors/xcull)**

</div>

## License

This project is licensed under the **Xcull Source Available License (XSAL) v1.0**.

For detailed terms, please read the full [LICENSE](LICENSE.md) file.
