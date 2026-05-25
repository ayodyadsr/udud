<h1 align="center">udud</h1>

<p align="center">
  <b>URL Deduplicate Data</b><br>
  Reduce noisy recon URLs into actionable attack surface while preserving exploitable patterns.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-20MB%20%2F%20781k%20URLs-success.svg">
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

**udud** udud is a streaming-first URL deduplication tool written in C, focused on preserving real attack surface while maintaining extremely high throughput and low runtime overhead.

The goal is not to make the output as small as possible. The goal is to make the output cleaner without losing real attack surface.

On a real 781,398-URL recon capture, udud is the only deduplicator that keeps
the attack surface intact *and* stays cheap enough to run at fleet scale:

- **3.2× faster** than urldedupe, **14× faster** than uro, **59× faster** than
  urless, and it finishes where uddup never does (>15 min, gives up)
- **17× less memory** than urldedupe (20 MB vs 344 MB) — so you can run many
  targets in parallel on commodity hardware instead of one memory-hungry job
- **keeps more real endpoints** than the aggressive folders (uro and urless
  delete roughly a third of the endpoint classes; udud keeps ~84%, including the
  object-ID endpoints where IDOR/BOLA bugs live)

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

A full, reproducible benchmark — methodology, every per-trial timing, the raw
tool outputs, and the de-identified corpora — lives in
[ayodyadsr/udud-benchmark](https://github.com/ayodyadsr/udud-benchmark)
([BENCHMARK.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/BENCHMARK.md),
[ANONYMIZATION.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/ANONYMIZATION.md)).
All figures below are udud's **shipping default** (no flags), against each tool's
documented invocation, on the same machine and the same inputs.

The benchmark answers the question that matters to a security program: *of the
distinct endpoints a target exposes, how many survive deduplication to actually
get scanned — and what does it cost to find out?*

### Large real target — Wayback capture, 781,398 URLs

| Tool | Endpoint classes kept | Processing time | Memory | Scales? |
|---|---:|---:|---:|:--:|
| **udud (default)** | **84%** (best real deduplicator) | **2.9 s** | **20 MB** | yes |
| urldedupe | 100% — but near-passthrough (2.2× the output) | 9.4 s | 344 MB | memory-bound |
| uro | 63% (deletes ~37% of endpoint classes) | 39.8 s | 36 MB | slow |
| urless | 67% (deletes ~33% of endpoint classes) | 172 s | 46 MB | too slow |
| uddup | — | did not finish (>15 min) | — | no |

**How to read it.** "Endpoint classes kept" is the security view — the fraction
of the distinct *kinds* of endpoint in the corpus that survive, counting every
class equally (so a rare-but-critical endpoint type weighs the same as a common
one). udud and urldedupe are the only two that don't throw surface away — but
urldedupe gets there by barely deduplicating (2.2× the lines, 17× the memory),
while uro and urless produce a tidy short list by deleting a third of the
endpoint classes, which is exactly what a scanner then never tests. udud keeps
the most surface of any real deduplicator while also being the fastest and the
lightest.

### Smaller targets confirm the pattern

| Corpus | udud: kept / time / memory | for comparison |
|---|---|---|
| gau, 44,943 URLs | 97% / 0.14 s / 4.2 MB | uro and urless keep 75%; urldedupe matches coverage at 8× the output and 5× the memory |
| vulnweb, 15,185 URLs | 95% / 0.03 s / 3.4 MB | uro keeps 86% at 15× the time; uddup keeps 58% |
| controlled known-answer, 45,410 URLs | 99.6% / 0.08 s / 4.7 MB | urless 91%, uro 83% — they reach a tidy output by deleting whole classes |

On the controlled corpus — the only one where the correct answer is known
exactly — udud retains 99.6% of all endpoint classes, the highest of any tool
that actually deduplicates.

### Memory stays bounded as targets grow

udud's memory tracks the number of *distinct endpoints it keeps*, not the raw
input size, so it stays flat as inputs scale and rises only with genuinely new
surface (3–4 MB across 25k–400k-URL slices, 20 MB on the full 781k corpus).
urldedupe's memory grows with input and reaches 344 MB on the same corpus;
uddup's cost grows with the square of the input and it stops finishing past
~50k URLs. udud has processed multi-million-URL inputs in seconds in stress
testing — it does not fall over on big targets.

### The one honest trade-off

udud is deliberately **keep-biased**: faced with an ambiguous URL — an object
ID, a session token, an opaque hash — it keeps it rather than folding it away,
because that is exactly where IDOR / broken-object-level-authorization bugs
hide. The cost is a larger output than the most aggressive folders. The trade is
intentional: a few redundant lines a scanner absorbs in seconds, in exchange for
never silently dropping a testable endpoint. Teams that want a smaller list can
fold object IDs with `-F`; the numbers above are the default, which optimizes for
not losing surface. The full per-class data — including the classes where the
keep-bias lowers a shape-only precision score — is published unedited in the
benchmark repo's `raw/`.

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

This project is licensed under the **Udud Source Available License (USAL) v1.0**.

For detailed terms, please read the full [LICENSE](LICENSE.md) file. 
