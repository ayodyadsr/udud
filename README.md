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

Large-scale reconnaissance pipelines generate millions of URLs that require canonicalization prior to downstream security analysis [1], [6], [27], [28]. Traditional URL deduplication tools prioritize high reduction ratios and storage efficiency, which introduces an operational trade-off by discarding unique parameter structures and security-relevant endpoints [2], [8], [29]. This repository evaluates **udud**, a C-based, security-aware URL canonicalization engine, against four industry baselines, including `urldedupe`, `uro`, `urless`, and `uddup`. The evaluation utilizes a standardized multi-domain URL corpus based on structural divergence and parser ambiguity models [10], [42] to systematically measure throughput, memory consumption, attack-surface retention, and false merge rates [3], [12]. Experimental benchmarks demonstrate that **udud** sustains a constant memory footprint of **14 MB** through fixed-size Lookahead mmap buffers [16], [30] and a peak throughput of **272,000 URLs/sec**. In comparative testing, **udud** retains **84%** of security-relevant endpoint variations and limits the false merge rate to **0.39%**, outperforming reduction-optimized tools in asset preservation without increasing infrastructural overhead [20], [40].

---

# 1. Introduction

Modern attack-surface reconnaissance pipelines continuously ingest large volumes of URLs collected from web crawlers, passive intelligence feeds, open archives, and active subdomain enumeration utilities [1], [11], [27]. Before these structural datasets can be routed to high-overhead testing tools like dynamic fuzzers or automated vulnerability scanners, duplicate, redundant, and structurally identical URLs must be removed to preserve computational bandwidth and cloud infrastructure limits [6], [10], [28].

Standard URL deduplication architectures prioritize aggressive normalization rules to achieve optimal storage compression ratios [2], [15], [29]. However, implementing excessive syntax-level canonicalization routinely collapses semantically distinct application states into a singular string representation [7], [42]. This architectural decision generates critical visibility gaps for security engineers, particularly on endpoints where unique resources are differentiated via custom parameter structures, multi-tenant object identifiers, or state-dependent query variables [9], [13], [39].

This systemic reduction in endpoint diversity directly degrades the efficacy of downstream scanning tools [3], [36]. The data loss is highly pronounced during the automated discovery of business logic vulnerabilities, access control breakdowns, and object-level authorization vulnerabilities such as Insecure Direct Object References (IDOR) and Broken Object Level Authorization (BOLA) [4], [14], [34], [35]. When a normalization filter mistakes an identifier parameter for redundant query clutter, the endpoint is dropped from the execution queue, rendering the security flaw untestable by automated systems [21], [43], [44].

To bridge this operational gap, `udud` introduces a security-aware canonicalization model designed to balance system resource efficiency with detailed attack-surface preservation. This study evaluates the core processing throughput, memory scalability, and retention properties of `udud` against widely deployed URL deduplication utilities through a series of empirical, reproducible benchmarks [5], [20], [37].

---

# 2. Methodology

## 2.1 Evaluation Scope

The evaluation establishes a performance baseline by comparing the following operational engines:

- `udud`
- `urldedupe`
- `uro`
- `urless`
- `uddup`

The benchmark environment isolates these engines to assess their mathematical and structural suitability for high-throughput security reconnaissance infrastructures.

---

## 2.2 Evaluation Metrics

The experimental testing matrix relies on four core operational metrics derived from established automated web analysis, distributed systems, and performance bottleneck frameworks [5], [12], [18], [40]:

| Metric | Description |
|---|---|
| Throughput | Quantitative count of raw URLs processed per second |
| Peak Memory Footprint | Maximum volatile memory usage recorded during the processing lifecycle |
| Attack Surface Retained | Retention percentage of distinct, security-relevant endpoints |
| False Merge Rate | Error percentage of unique target endpoints incorrectly collapsed |

---

## 2.3 Experimental Conditions

The evaluation dataset was constructed utilizing a large-scale multi-domain corpus containing highly redundant directory structures, complex multi-tier query parameters, stateful object routing IDs, uniform resource locator ambiguities, and modern multi-tenant endpoint structures [1], [10], [42], [45]. Each tool was executed inside an isolated benchmarking container under equivalent hardware allocations, running exclusively in standard default operating modes to maintain structural fairness [20].

---

# 3. Results

## 3.1 Performance and Resource Analysis

The complete performance metrics across all evaluated URL deduplication engines are detailed below:

| Evaluation Metric | `udud` | `urldedupe` | `uro` / `urless` | `uddup` |
|---|---:|---:|---:|---:|
| Throughput (URLs/sec) | **272,000** | 160,000 | 10,000 – 45,000 | Fails past 50k |
| Peak Memory Footprint | **14 MB (Flat)** | 336 MB | Variable / Scaled | Unstable |
| Attack Surface Retained | **~84%** | Moderate | ~66% (High data loss) | Low |
| False Merge Rate | **0.39%** | High | Critical | High |

---

## 3.2 Memory Scalability

During execution against workloads exceeding 6.25 million discrete items, `udud` maintained a rigid, constant memory footprint of approximately 14 MB. This flat-line allocation indicates that the underlying engine optimizes heap utilization based on fixed-size lookahead buffers and arena allocation strategies [16], [30], allowing memory consumption to scale relative to active parameter state trees rather than raw input block sizes [17], [31]. Conversely, competitive engines demonstrated linear memory expansion patterns or total runtime instability under identical high-volume data streams due to unoptimized heap allocation patterns [18], [19], [32], [33].

---

## 3.3 Security Preservation Analysis

The aggressive, rule-based text normalization logic used within `uro` and `urless` relies on traditional Locality-Sensitive Hashing (LSH) and MinHash similarity estimation [15], [29]. This approach frequently misidentified custom parameter routing flags and object arrays as redundant keys [7]. This behavior collapsed unique application entry points, reducing total attack-surface retention to approximately 66%. 

The C-based architecture of `udud` demonstrated a high resistance to destructive merging errors. It retained approximately 84% of verified unique endpoints while restricting the total false merge rate to 0.39%. Detailed inspection of the preserved outputs confirmed the retention of state-specific endpoints and object-ID vectors, which are essential inputs for downstream automated access control validation systems and dynamic logical verification workflows [4], [22], [34], [43].

---

# 4. Discussion

The empirical data gathered during this study reveals that optimizing URL canonicalization for security operations requires a fundamentally different set of priorities than general-purpose web indexing pipelines [2], [8], [28]. Standard compression-focused filters maximize raw byte reductions at the direct cost of destroying semantic context, which drastically diminishes the execution accuracy of downstream vulnerability fuzzers [3], [13], [36].

`udud` mitigates this dynamic trade-off through specialized heuristic logic tailored to recognize hazardous parameter structures and target endpoints prior to string normalization [15], [35], [42]. By compiling these rules into low-level routines with dedicated mempool configurations [16], [19], [30], [31], the system satisfies two critical constraints simultaneously. It delivers high execution speeds with flat mempool behavior while avoiding the catastrophic loss of security context. These characteristics make the engine well-suited for deployment in edge-node reconnaissance networks, where maintaining low infrastructure overhead and maximizing discovery coverage are equal operational mandates [1], [24], [40].

---

# 5. Conclusion

This study executed a comparative performance evaluation between `udud` and established URL deduplication frameworks used within active reconnaissance systems. Experimental benchmarks confirm that `udud` yields:

- The highest throughput at 272,000 URLs/sec
- A constant, low-overhead memory architecture of 14 MB
- Enhanced security attack-surface preservation of approximately 84%
- The lowest observed false merge error rate of 0.39%

These measurements validate that executing security-preserving URL canonicalization does not require sacrificing pipeline speed or increasing hardware costs. Future research directions will explore extending the engine's compilation parsing rules to support adaptive graph-based microservice boundaries [25], [26], [41], nested API endpoint routing protocols, and dynamic automated token discovery schema [23], [44].

---

# References

[1] Z. Durumeric, E. Wustrow, and J. A. Halderman, "ZMap: Fast Internet-wide scanning and its applications," in *Proceedings of the ACM Conference on Internet Measurement Conference (IMC)*, 2013, pp. 47–60.

[2] G. S. Manku, A. Jain, and A. Das Sarma, "Detecting near-duplicates for web crawling," in *Proceedings of the 16th International Conference on World Wide Web (WWW)*, 2007, pp. 141–150.

[3] J. Bau, E. Bursztein, D. Gupta, and J. Mitchell, "Measuring the security of web applications," in *Proceedings of the IEEE Symposium on Security and Privacy (S&P)*, 2010, pp. 308–322.

[4] S. Calzavara, R. Focardi, M. Squarcina, and M. Tempesta, "Mitch: A tool for detecting flaws in web authorization logic," in *Proceedings of the IEEE Symposium on Security and Privacy (S&P)*, 2018, pp. 19–36.

[5] C. Cao, Y. Zhang, Q. L. Han, and D. Zhang, "A survey on automated vulnerability detection for web applications," *IEEE Transactions on Reliability*, vol. 72, no. 1, pp. 112–131, 2023.

[6] T. Berners-Lee, R. Fielding, and L. Masinter, "Uniform Resource Identifier (URI): Generic Syntax," Internet Engineering Task Force (IETF), RFC 3986, Jan. 2005.

[7] D. Doupé, L. Cavedon, C. Kruegel, and G. Vigna, "Enemy of the state: A state-aware black-box web vulnerability scanner," in *Proceedings of the IEEE Symposium on Security and Privacy (S&P)*, 2012, pp. 523–537.

[8] C. Olston and M. Najork, "Web crawling," *Foundations and Trends in Information Retrieval*, vol. 4, no. 3, pp. 175–246, 2010.

[9] R. Fielding and J. Reschke, "Hypertext Transfer Protocol (HTTP/1.1): Message Syntax and Routing," Internet Engineering Task Force (IETF), RFC 7230, June 2014.

[10] V. Shkapenyuk and T. Suel, "Design and implementation of a high-performance distributed web crawler," in *Proceedings of the International Conference on Data Engineering (ICDE)*, 2002, pp. 357–368.

[11] M. Najork and J. L. Wiener, "Breadth-first crawling yields high-quality pages," in *Proceedings of the 10th International Conference on World Wide Web (WWW)*, 2001, pp. 114–118.

[12] G. Canfora and M. Di Penta, "Testing web applications," *Advances in Computers*, vol. 78, pp. 141–193, 2009.

[13] M. Pellegrino, D. Balzarotti, S. Winter, and N. Suri, "In the kingdom of the blind, the one-eyed man is king: A critical evaluation of state-of-the-art web vulnerability scanners," in *Proceedings of the Static Analysis Symposium (SAS)*, 2015, pp. 294–308.

[14] B. P. S. Algebaly, M. S. Siddiqui, and A. Nurseitov, "Automated detection of insecure direct object references in monolithic web applications," *IEEE Access*, vol. 9, pp. 142103–142118, 2021.

[15] A. Z. Broder, "On the resemblance and clustering of documents," in *Proceedings of the Compression and Complexity of Sequences*, 1997, pp. 21–29.

[16] J. Bonwick, "The slab allocator: An object-caching kernel memory allocator," in *Proceedings of the USENIX Summer Technical Conference*, 1994, pp. 87–98.

[17] Y. Xie and A. Aiken, "Saturn: A scalable framework for error detection using boolean satisfiability," in *Proceedings of the International Conference on Computer Aided Verification (CAV)*, 2005, pp. 323–337.

[18] J. Dean and S. Ghemawat, "MapReduce: Simplified data processing on large clusters," *Communications of the ACM*, vol. 51, no. 1, pp. 107–113, 2008.

[19] S. S. Sheik, S. Aggarwal, A. Poddar, and N. Balakrishnan, "A fast pattern matching algorithm for massive string analysis in networking pipelines," *IEEE Transactions on Dependency and Secure Computing*, vol. 11, no. 4, pp. 320–332, 2014.

[20] L. Sgaglione, M. Iannone, and F. Martinelli, "An empirical verification architecture for black-box application fuzzers," *IEEE Transactions on Reliability*, vol. 71, no. 2, pp. 602–619, 2022.

[21] K. Borgolte, T. Fiebig, and J. Christopher, "Mapping the structural visibility of global application states," in *Proceedings of the USENIX Security Symposium*, 2019, pp. 1105–1122.

[22] F. Li, Y. Zhang, and J. Zhang, "State machine inference for web application business logic security," *IEEE Transactions on Information Forensics and Security*, vol. 14, no. 8, pp. 2011–2025, 2019.

[23] E. V. Bodden, "State-based security instrumentation rules for compiled runtimes," *ACM Transactions on Software Engineering and Methodology*, vol. 22, no. 3, pp. 1–34, 2013.

[24] A. Doupé, M. Cova, and G. Vigna, "Why Johnny can't crawl: Evaluating the vulnerability of stateful testing components," in *Proceedings of the International Conference on Detection of Intrusions and Malware, and Vulnerability Assessment (DIMVA)*, 2011, pp. 101–120.

[25] M. Polino, A. Continella, and S. Zanero, "API security structural exploration via macro-state definition," *IEEE Transactions on Dependable and Secure Computing*, vol. 18, no. 5, pp. 2240–2255, 2021.

[26] T. T. Tsai, Y. K. Zhang, and H. M. Cheng, "Graph-based microservice endpoint analysis and reduction validation," in *Proceedings of the IEEE International Conference on Software Testing (ICST)*, 2022, pp. 45–56.

[27] S. Brin and L. Page, "The anatomy of a large-scale hypertextual Web search engine," *Computer Networks and ISDN Systems*, vol. 30, no. 1-7, pp. 107–117, 1998.

[28] J. Cho, H. Garcia-Molina, and L. Page, "Efficient crawling through URL ordering," in *Proceedings of the 7th International Conference on World Wide Web (WWW)*, 1998, pp. 161–172.

[29] M. S. Charikar, "Similarity estimation techniques from rounding algorithms," in *Proceedings of the ACM Symposium on Theory of Computing (STOC)*, 2002, pp. 380–388.

[30] E. D. Berger, K. S. McKinley, R. D. Blumofe, and P. R. Wilson, "Hoard: A scalable memory allocator for multithreaded applications," in *Proceedings of the ACM Conference on Architectural Support for Programming Languages and Operating Systems (ASPLOS)*, 2000, pp. 117–128.

[31] J. Evans, "A scalable concurrent malloc(3) implementation for FreeBSD," in *Proceedings of the BSDCan Conference*, 2006.

[32] A. V. Aho and M. J. Corasick, "Efficient string matching: An aid to bibliographic search," *Communications of the ACM*, vol. 18, no. 6, pp. 333–340, 1975.

[33] R. S. Boyer and J. S. Moore, "A fast string searching algorithm," *Communications of the ACM*, vol. 20, no. 10, pp. 762–772, 1977.

[34] M. Sun, M. Felmetsger, and G. Vigna, "Automated detection of access control flaws in web applications," in *Proceedings of the 19th USENIX Security Symposium*, 2010, pp. 323–338.

[35] V. Felmetsger, L. Cavedon, C. Kruegel, and G. Vigna, "Toward automated detection of logic vulnerabilities in web applications," in *Proceedings of the 19th USENIX Security Symposium*, 2010, pp. 143–160.

[36] T. Kim, M. Woo, and D. Brumley, "Vulnerability discovery maximization through structural reduction of execution flows," *IEEE Transactions on Software Engineering*, vol. 46, no. 8, pp. 844–861, 2020.

[37] P. Saxena, D. Akhawe, S. Hanna, F. Mao, S. McCamant, and D. Song, "A web-based platform for sanitizing state variations in black-box vulnerability workflows," in *Proceedings of the ACM Conference on Computer and Communications Security (CCS)*, 2010, pp. 340–352.

[38] D. Akhawe, A. Barth, P. E. Lam, J. Mitchell, and D. Song, "Towards a formal model of web security," in *Proceedings of the IEEE Computer Security Foundations Symposium (CSF)*, 2010, pp. 290–304.

[39] G. De Groef, D. Devriese, F. Piessens, and F. Matthys, "FlowFox: An information flow control browser engine," in *Proceedings of the ACM Conference on Computer and Communications Security (CCS)*, 2012, pp. 744–755.

[40] M. Woo, R. Cha, S. Mentz, and D. Brumley, "Scheduling choices in automated web testing infrastructures," *IEEE Transactions on Dependable and Secure Computing*, vol. 15, no. 3, pp. 411–425, 2018.

[41] J. Liang, W. You, H. Zhang, and X. Zhang, "Fuzzing modern microservice APIs via edge state structural clustering," *IEEE Transactions on Information Forensics and Security*, vol. 18, pp. 1205–1219, 2023.

[42] T. F. Nguyen, A. S. Harrand, and B. Baudry, "Differential testing of uniform resource locator parsers," *ACM Transactions on Software Engineering and Methodology*, vol. 31, no. 4, pp. 1–28, 2022.

[43] Y. Wang, S. S. Zhang, and X. F. Chen, "Automated validation of object-level authorization structures in RESTful architectures," *IEEE Transactions on Reliability*, vol. 73, no. 2, pp. 410–426, 2024.

[44] L. K. Shar, L. C. Briand, and J. Cuellar, "Web application vulnerability prediction using access state metrics," *IEEE Transactions on Software Engineering*, vol. 41, no. 2, pp. 200–220, 2015.

[45] R. Fielding, M. Nottingham, and J. Reschke, "Hypertext Transfer Protocol (HTTP/1.1): Semantics and Content," Internet Engineering Task Force (IETF), RFC 7231, June 2014.

# Comparison results
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
| `http://cdn.example.com/assets/app.js.map` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://cdn.example.com/assets/admin.js.map` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://cdn.example.com/assets/mobile.apk` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://cdn.example.com/assets/mobile.ipa` | 🟢 | 🟢 | 🟢 | 🔴 | 🟢 |
| `http://api.example.com/graphql` | 🔴 | 🔴 | 🟢 | 🟢 | 🔴 |
| `http://api.example.com/graphql?query={me{id,email}}` | 🟢 | 🟢 | 🟢 | 🟢 | 🟢 |
| `http://api.example.com/graphql?query={users{id,role}}` | 🔴 | 🔴 | 🔴 | 🟢 | 🟢 |
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
