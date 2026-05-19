# udud

**URL Deduplicate Data** — a fast, single-pass URL structural
de-duplicator written from scratch in C.

`udud` reads URLs on stdin and writes a de-duplicated set on stdout. It
collapses URLs that are *structurally* the same endpoint (same path
shape, same parameter set, templated IDs/UUIDs/hashes/slugs) down to one
real, first-seen representative — while staying **non-destructive**: it
keeps `.js`/`.html` sources, source-disclosure findings
(`.bak`/`.sql`/`.zip`/`.phps`), open-redirect / SSRF / LFI parameters,
matrix-param auth endpoints (`;jsessionid=`), and other genuine attack
surface that aggressive dedupers throw away. Smallest output is *not*
the goal — losing zero real surface is.

It is strictly streaming: one pass, emit per line, no cross-line
buffering, so memory stays constant (~a few MB) regardless of input
size and it processes 100M+ line streams in seconds.

## Build

```sh
cc -O3 -march=native -flto -Wall -Wno-misleading-indentation -o udud udud.c
```

`runstat.c` is an optional helper (`fork`+`wait4`+`getrusage`) used for
wall/CPU/peak-RSS benchmarking:

```sh
cc -O2 -o runstat runstat.c
```

## Usage

```sh
cat urls.txt | udud > deduped.txt
```

Clean structural de-duplication is the default — no flags required.

```
usage: udud [-x keep-invalid][-a keep-assets][-s case-sensitive][-k][-p][-W][-r][-V]
```

`-x` is the fully-raw escape hatch (structural dedup only, no cleaning).
`-V` prints `udud: <in> -> <out>  (peak RSS <n> KB)` to stderr.

## License

GNU Affero General Public License v3.0 — see [LICENSE](LICENSE).
