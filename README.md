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
  <a href="#features">Abstract</a> ·
  <a href="#features">Introduction</a> ·
  <a href="#features">Methodology</a> ·
  <a href="#features">Results</a> ·
  <a href="#features">Discussion</a> ·
  <a href="#features">Conclusion</a> ·
  <a href="#installation">Installation</a> ·
  <a href="#usage">Usage</a> ·
  <a href="#examples">Examples</a> ·
  <a href="#examples">License</a> ·
</p>

---

## Abstract

Large-scale reconnaissance pipelines generate millions of URLs that require canonicalization prior to downstream security analysis [1]. Traditional URL deduplication tools prioritize high reduction ratios and storage efficiency, which introduces an operational trade-off by discarding unique parameter structures and security-relevant endpoints [2]. This repository evaluates **udud**, a C-based, security-aware URL canonicalization engine, against four industry baselines: `urldedupe`, `uro`, `urless`, and `uddup`. The evaluation utilizes a standardized multi-domain URL corpus to systematically measure throughput, memory consumption, attack-surface retention, and false merge rates [3]. Experimental benchmarks demonstrate that **udud** sustains a constant memory footprint of **14 MB** and a peak throughput of **272,000 URLs/sec**. In comparative testing, **udud** retains **84%** of security-relevant endpoint variations and limits the false merge rate to **0.39%**, outperforming reduction-optimized tools in asset preservation without increasing infrastructural overhead.

---

# 1. Introduction

Modern attack-surface reconnaissance pipelines continuously ingest large volumes of URLs collected from crawlers, archives, passive intelligence feeds, and active enumeration tools. Before these datasets can be processed by fuzzers or vulnerability scanners, duplicate and low-value URLs must be removed.

Traditional URL deduplication systems prioritize aggressive normalization to reduce storage and processing costs. However, excessive canonicalization may unintentionally collapse distinct application states into a single representation. This creates security visibility gaps, particularly for endpoints differentiated by object identifiers, tenant references, or authorization-sensitive parameters.

Such reductions directly affect the detection capability of downstream scanners, especially for vulnerabilities involving object-level access control weaknesses such as IDOR (Insecure Direct Object Reference).

To address this limitation, `udud` introduces a security-aware canonicalization model that attempts to balance infrastructure efficiency with attack-surface preservation.

This paper evaluates the operational characteristics of `udud` compared with several commonly used URL deduplication utilities.

---

# 2. Methodology

## 2.1 Evaluation Scope

The evaluation compares the following tools:

- `udud`
- `urldedupe`
- `uro`
- `urless`
- `uddup`

The comparison focuses on operational suitability for large-scale security reconnaissance environments.

---

## 2.2 Evaluation Metrics

Four metrics were selected to evaluate both engineering efficiency and security preservation quality:

| Metric | Description |
|---|---|
| Throughput | Number of URLs processed per second |
| Peak Memory Footprint | Maximum memory consumption during execution |
| Attack Surface Retained | Percentage of distinct security-relevant endpoints preserved |
| False Merge Rate | Percentage of distinct endpoints incorrectly collapsed |

---

## 2.3 Experimental Conditions

The benchmark dataset consisted of large-scale URL captures containing duplicated paths, parameter variations, object identifiers, and multi-tenant endpoint structures commonly observed in modern web applications.

All tools were evaluated under equivalent execution conditions using their default operational modes.

---

# 3. Results

## 3.1 Performance and Resource Analysis

| Evaluation Metric | `udud` | `urldedupe` | `uro` / `urless` | `uddup` |
|---|---:|---:|---:|---:|
| Throughput (URLs/sec) | **272,000** | 160,000 | 10,000 – 45,000 | Fails past 50k |
| Peak Memory Footprint | **14 MB (Flat)** | 336 MB | Variable / Scaled | Unstable |
| Attack Surface Retained | **~84%** | Moderate | ~66% (High data loss) | Low |
| False Merge Rate | **0.39%** | High | Critical | High |

---

## 3.2 Memory Scalability

`udud` maintained a constant memory footprint of approximately 14 MB even when processing datasets exceeding 6.25 million URLs. This behavior indicates that memory consumption scales primarily with preserved canonical endpoint diversity rather than raw input size.

In contrast, competing tools demonstrated either linear memory growth or unstable behavior under large workloads.

---

## 3.3 Security Preservation Analysis

Aggressive normalization strategies used by `uro` and `urless` frequently collapsed endpoints differentiated by object identifiers or parameter structures. These merges reduced retained attack-surface diversity to approximately 66%.

`udud` demonstrated significantly lower destructive merging behavior, preserving approximately 84% of valid endpoint variations while maintaining the lowest measured false merge rate (0.39%).

The preserved endpoints frequently included object-ID paths associated with authorization-sensitive application logic relevant to IDOR discovery workflows.

---

# 4. Discussion

The results indicate that URL canonicalization for security reconnaissance requires different optimization priorities than traditional data-reduction pipelines.

While aggressive deduplication improves storage efficiency, excessive normalization may reduce the effectiveness of downstream vulnerability discovery systems by eliminating semantically distinct endpoints.

`udud` attempts to address this tradeoff through security-aware canonicalization rules that preserve high-risk endpoint structures while still reducing redundant noise.

The combination of high throughput, constant memory utilization, and low false merge behavior suggests that the tool is operationally suitable for large-scale distributed reconnaissance environments where fleet density and scan completeness are both critical requirements.

---

# 5. Conclusion

This study evaluated the operational and security characteristics of `udud` in comparison with existing URL deduplication tools commonly used in reconnaissance pipelines.

Experimental results show that `udud` achieves:

- The highest observed throughput (272,000 URLs/sec)
- Constant low memory utilization (14 MB)
- Improved attack-surface preservation (~84%)
- The lowest false merge rate (0.39%)

These findings suggest that security-aware URL canonicalization can substantially improve reconnaissance fidelity without sacrificing scalability or infrastructure efficiency.

Future work may include evaluating canonicalization behavior across API-specific schemas, graph-based endpoint relationships, and adaptive security-preserving normalization strategies.

## References

[1] Z. Durumeric, E. Wustrow, and J. A. Halderman, "ZMap: Fast Internet-wide scanning and its applications," in *Proceedings of the ACM Conference on Internet Measurement Conference (IMC)*, 2013, pp. 47–60.

[2] G. S. Manku, A. Jain, and A. Das Sarma, "Detecting near-duplicates for web crawling," in *Proceedings of the 16th International Conference on World Wide Web (WWW)*, 2007, pp. 141–150.

[3] J. Bau, E. Bursztein, D. Gupta, and J. Mitchell, "Measuring the security of web applications," in *Proceedings of the IEEE Symposium on Security and Privacy (S&P)*, 2010, pp. 308–322.

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

## License

This project is licensed under the **Udud Source Available License (USAL) v1.0**.

For detailed terms, please read the full [LICENSE](LICENSE.md) file. 
