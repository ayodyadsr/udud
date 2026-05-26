<h1 align="center">udud</h1>

<p align="center">
  <b>URL Deduplicate Data</b><br>
  Reduce noisy recon URLs into actionable attack surface while preserving exploitable patterns.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C-00599C.svg">
  <img src="https://img.shields.io/badge/dependencies-none-success.svg">
  <img src="https://img.shields.io/badge/memory-14MB%20%2F%20781k%20URLs-success.svg">
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

**udud** is a security-aware URL canonicalization engine written in C, built for
the deduplication stage of a recon pipeline. It reduces the raw URLs harvested
for an asset into the working set scanners actually process, with high
throughput, low memory, and a strong bias toward keeping real attack surface.

The goal is not to make the output as small as possible. The goal is to make the
output cleaner without losing real attack surface.

On a real 781,398-URL recon capture, udud leads on the two things that set fleet
capacity, throughput and memory, in the same run:

- **272k URLs/sec**, the fastest measured: 1.7x urldedupe, 6x uro, 26x urless,
  and it finishes where uddup never does (it gives up past ~50k URLs)
- **14 MB peak memory**, the lowest measured: 24x lighter than urldedupe (336 MB),
  so you run many assets in parallel on commodity hardware
- **flat 14 MB and a constant rate to 6.25M URLs**, because memory tracks the
  distinct endpoints kept, not the input size
- **lowest false merge rate of any real deduplicator** (0.39% on known ground
  truth): it keeps more real endpoints than the aggressive folders (uro and
  urless fold away roughly a third of the endpoint classes; udud keeps ~84% on
  this capture), including the object-ID endpoints where IDOR/BOLA bugs live

| Raw Input URL | uro | urless | urldedupe | uddup | udud |
|---|---|---|---|---|---|
| `http://example.com/page.php?id=1` | 🟢 | 🟢 | 🟢 | 🟢 | 🔴 |
| `http://example.com/page.php?id=2` | 🔴 | 🔴 | 🔴 | 🟢 | 🔴 |
| `http://example.com/page.php?id=3&page=2` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://example.com/cat/9/details.html` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://example.com/cat/11/details.html` | 🔴 | 🔴 | 🟢 | 🟢 | 🔴 |
| `http://example.com/blog/why-people-suck-a-study` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `http://example.com/blog/how-to-lick-your-own-toes` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://example.com/banner.jpg` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://example.com/assets/background.jpg` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `https://target.com/blah/U-61723A/settings` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://target.com/blah/U-63352B/settings` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://target.com/blah/U-61351A/profile` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://target.com/blah/U-61723A/settings` | 🔴 | 🔴 | 🔴 | 🔴 | 🔴 |
| `https://target.com/blah/U-64135C/profile` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://google.com` | 🟢 | 🟢 | 🟢 | 🟢 | 🔴 |
| `https://google.com/home?qs=value` | 🟢 | 🟢 | 🟢 | 🟢 | 🔴 |
| `https://google.com/home?qs=secondValue` | 🔴 | 🔴 | 🔴 | 🟢 | 🔴 |
| `https://google.com/home?qs=newValue&secondQs=anotherValue` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://google.com/home?qs=asd&secondQs=das` | 🔴 | 🔴 | 🔴 | 🟢 | 🔴 |
| `https://site.com/api/users/123` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://site.com/api/users/222` | 🔴 | 🔴 | 🟢 | 🔴 | 🟢 |
| `https://site.com/api/users/412/profile` | 🔴 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://site.com/users/photos/photo.jpg` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `https://site.com/users/photos/myPhoto.jpg` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `https://site.com/users/photos/photo.png` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `https://www.example.com/product/123` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `https://www.example.com/product/456` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `https://www.example.com/product/123?is_prod=false` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://www.example.com/product/222?is_debug=true` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `https://www.example.com/` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `https://www.example.com/privacy-policy` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `https://www.example.com/product/1` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `https://www.example2.com/product/2` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `https://www.example3.com/product/4` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/user/1001/profile` | 🟢 | 🔴 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/user/1002/profile` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/user/1002/profile?view=private` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/user/1002/profile?role=admin` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/user/1002/profile;jsessionid=deadbeef` | 🔴 | 🔴 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/user/1002/profile.json` | 🔴 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/user/1002/profile.bak` | 🔴 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/user/1002/profile.old` | 🔴 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/user/1002/export` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/user/1002/export.csv` | 🔴 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/user/1002/export?format=xml` | 🔴 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/user/1002/reset-password` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/user/1002/reset-password?token=test` | 🔴 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/admin/users/1002` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/admin/users/1002/permissions` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/admin/users/1002/permissions?debug=true` | 🔴 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/admin/users/1002/roles` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/admin/users/1002/roles?impersonate=true` | 🔴 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/org/55/project/77/member/88` | 🟢 | 🔴 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/org/55/project/77/member/89` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/org/55/project/77/member/89?include=secrets` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/org/55/project/77/member/89/billing` | 🔴 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/org/55/project/77/member/89/invoices/pdf` | 🔴 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/org/55/project/77/member/89/activity` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/org/55/project/77/member/89/activity?from=2025-01-01` | 🔴 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/org/55/project/77/member/89/activity?debug=1` | 🔴 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/payment/transfer` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/payment/transfer?currency=usd` | 🟢 | 🟢 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/payment/transfer?currency=usd&debug=true` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/payment/transfer/preview` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/payment/transfer/commit` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/payment/transfer/commit?race=test` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/payment/withdraw` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/payment/withdraw/confirm` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/payment/withdraw/confirm?step=2` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/auth/session` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/auth/session;jsessionid=AAAA1111` | 🔴 | 🔴 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/auth/session;jsessionid=BBBB2222` | 🔴 | 🔴 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v1/auth/session?redirect=/admin` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/v1/auth/token/refresh` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/auth/token/refresh?device=mobile` | 🟢 | 🟢 | 🟢 | 🔴 | 🔴 |
| `http://api.example.com/v1/auth/token/refresh?device=mobile&debug=true` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://example.com/internal/debug` | 🔴 | 🔴 | 🟢 | 🔴 | 🔴 |
| `http://example.com/internal/debug?env=staging` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://example.com/internal/debug?env=production` | 🔴 | 🔴 | 🔴 | 🔴 | 🔴 |
| `http://example.com/internal/health` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://example.com/internal/metrics` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://example.com/internal/prometheus` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://example.com/backup/config.zip` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://example.com/backup/config.tar.gz` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://example.com/backup/.env` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://example.com/backup/.git/config` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://example.com/backup/database.sql` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://example.com/backup/export.phps` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://cdn.example.com/assets/app.js.map` | 🟢 | 🟢 | 🟢 | 🟢 | 🔴 |
| `http://cdn.example.com/assets/admin.js.map` | 🟢 | 🟢 | 🟢 | 🔴 | 🔴 |
| `http://cdn.example.com/assets/mobile.apk` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://cdn.example.com/assets/mobile.ipa` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/graphql` | 🔴 | 🔴 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/graphql?query={me{id,email}}` | 🟢 | 🟢 | 🟢 | 🟢 | 🔴 |
| `http://api.example.com/graphql?query={users{id,role}}` | 🔴 | 🔴 | 🔴 | 🟢 | 🔴 |
| `http://api.example.com/graphiql` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/swagger.json` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/openapi.json` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/v2/swagger.yaml` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |

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

A full, reproducible benchmark (methodology, every per-trial timing, the raw
tool outputs, and the de-identified corpora) lives in
[ayodyadsr/udud-benchmark](https://github.com/ayodyadsr/udud-benchmark)
([BENCHMARK.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/BENCHMARK.md),
[ANONYMIZATION.md](https://github.com/ayodyadsr/udud-benchmark/blob/main/ANONYMIZATION.md)).
All figures below are udud's **shipping default** (no flags), against each tool's
documented invocation, on the same machine and the same inputs.

The benchmark answers the question a recon workflow cares about: how many URLs
per second can one worker clear, at what memory cost, and of the distinct
endpoints a target exposes, how many survive to actually get scanned.

### Large real target: Wayback capture, 781,398 URLs

| Tool | Throughput | Peak memory | Endpoint classes kept | Scales? |
|---|---:|---:|---:|:--:|
| **udud (default)** | **272k URLs/sec** | **13.6 MB** | **84%** (best real deduplicator) | yes |
| urldedupe | 159k URLs/sec | 336 MB | 100% (near-passthrough, 2.3x output) | memory-bound |
| uro | 45k URLs/sec | 35 MB | 63% (folds away ~37% of classes) | slow |
| urless | 10k URLs/sec | 45 MB | 67% (folds away ~33% of classes) | too slow |
| uddup | did not finish | n/a | n/a | no |

**How to read it.** udud is first on throughput and first on peak memory in the
same run. "Endpoint classes kept" is the security view: the fraction of the
distinct kinds of endpoint that survive, counting every class equally. udud and
urldedupe are the only two that do not throw surface away, but urldedupe gets
there by barely deduplicating (2.3x the lines, 24x the memory), while uro and
urless produce a tidy short list by folding away a third of the endpoint classes,
which is exactly what a scanner then never tests. udud keeps the most surface of
any real deduplicator while also being the fastest and the lightest.

### The security metric: false merge rate on known ground truth

On the controlled corpus the correct grouping is known exactly, so a merge that
destroys a distinct endpoint class can be counted. False merge rate is the
fraction of classes a tool wrongly collapses (lower is better, since each wrong
merge removes an endpoint from every later scan):

| Tool | False merge rate |
|---|---:|
| **udud** | **0.39%** |
| urldedupe | 0% (near-passthrough) |
| urless | 8.6% |
| uddup | 14.3% |
| uro | 16.9% |

udud has the lowest false merge rate of any tool that actually reduces the input.
urldedupe's 0% is the passthrough artifact: it keeps ~80 redundant lines per
class, so it cannot mis-merge and has not deduplicated either.

### Smaller targets confirm the pattern

| Corpus | udud: kept / time / memory | for comparison |
|---|---|---|
| gau, 44,943 URLs | 97% / 0.16 s / 3.5 MB | uro and urless keep 75%; urldedupe matches coverage at 8x the output and 6x the memory |
| vulnweb, 15,185 URLs | 95% / 0.02 s / 3.4 MB | uro keeps 86% at 10x the time; uddup keeps 58% |
| controlled known-answer, 45,410 URLs | 99.6% / 0.10 s / 3.9 MB | urless 91%, uro 83%, by folding away whole classes |

### Memory stays bounded as targets grow

udud's memory tracks the number of distinct endpoints it keeps, not the raw input
size, so it stays flat as inputs scale and rises only with genuinely new surface
(3 to 4 MB across 25k to 400k-URL slices, 13.6 MB on the full 781k corpus).
Replicating the corpus up to 6.25M URLs keeps peak memory flat at 13.8 MB and the
rate constant at ~270k URLs/sec. urldedupe's memory grows with input and reaches
336 MB on the same corpus; uddup's cost grows with the square of the input and it
stops finishing past ~50k URLs. udud does not fall over on big targets.

### The one honest trade-off

udud is deliberately **keep-biased**: faced with an ambiguous URL, an object ID,
a session token, or an opaque hash, it keeps it rather than fold it away, because
that is exactly where IDOR and broken-object-level-authorization bugs hide. The
cost is a larger output than the most aggressive folders. The trade is
intentional: a few redundant lines a scanner absorbs in seconds, in exchange for
never silently dropping a testable endpoint. Teams that want a smaller list can
fold object IDs with `-F`; the numbers above are the default, which optimizes for
not losing surface. The full per-class data, including the classes where the
keep-bias lowers a shape-only precision score, is published unedited in the
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

The query decides the grouping. Query URLs of one path are grouped by
path alone, then pruned by subset: among the query variants of a path
udud keeps the antichain of maximal key-sets, dropping a variant only
when its keys are a subset of a kept one (the survivor loses no
parameter). Variants with non-overlapping keys all stay.

A bare URL, one with no matrix token and no query, is dropped when the
same base path also appears decorated, either with a `;matrix` token or a
`?query`. The bare line carries no parameter the decorated sibling lacks,
so the endpoint still reaches the scanner through the richer line and the
bare line is a pure duplicate of the route. So `/v1/auth/session` is
folded once `/v1/auth/session;jsessionid=...` or
`/v1/auth/session?redirect=/admin` is present. The base key is the
signature up to the first `;` or `?`, so a decorated line removes a bare
line already kept for it and a bare line is dropped on sight once any
decorated sibling has been seen. The surviving decorated line shares the
same templated base, so no endpoint class is lost.

Because a covering superset (or a decorated sibling) can arrive after a
subset (or a bare line), the default output is buffered and written at end
of input, the kept real URLs in first-seen order. Pass `-k` to put the
full query in the signature instead, which keeps every distinct key-set on
its own line, disables the bare-fold, and restores streaming output.

Only the signature is compared and the URL that gets printed is never
rewritten, just selected. Memory scales with the number of unique
signatures plus, in the default mode, the kept output lines held until
end of input; under `-k` / `-x` there is no survivor buffer and output
streams one line at a time.

## License

This project is licensed under the **Udud Source Available License (USAL) v1.0**.

For detailed terms, please read the full [LICENSE](LICENSE.md) file. 
