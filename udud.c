/* udud v18 - fast URL structural de-duplicator (C, single-pass, stdin->stdout)
 *
 * v18.3: query-keyset MERGE in the default mode. Before, two query URLs of one
 *      templated path with different key SETS stayed separate lines, so
 *      /home?qs=asd&secondQs=das and /home?qs=secondValue were two outputs.
 *      They now collapse to ONE representative: the real URL carrying the MOST
 *      distinct query keys (tie keeps first-seen). A no-query URL of the same
 *      path is its OWN group and is never absorbed (e.g. /home and /home?x
 *      stay two lines). Rule, per request: aggressive one-per-path collapse
 *      among query URLs, knowingly dropping a disjoint-keyset variant in
 *      favour of the richest; the common nested case (?a vs ?a&b) loses
 *      nothing since the richer is a superset. The richest can appear AFTER a
 *      poorer one, which streaming cannot un-emit, so default output is now
 *      DEFERRED to EOF: one record per group in first-seen order, replaced in
 *      place when a richer query URL arrives. Cost is holding the kept output
 *      lines in the arena until EOF (RAM ~2x the dedup-key arena; still far
 *      below urldedupe/uro, speed within noise). ONLY the default keyset mode
 *      merges; -k (full query) and -x (raw) keep the old per-distinct
 *      STREAMING behaviour, verified byte-identical to v18.2 on
 *      gau/vulnweb/wb-full. Emitted lines are always real first-seen/richest
 *      URLs (merge output is a strict subset of v18.2 default output).
 *
 * v18.2: perf only, output BYTE-IDENTICAL to v18 on every flag and corpus
 *      (verified on gau/vulnweb/wb-full + synthetic repeat-artefact cases).
 *      Now FASTER than urldedupe on every corpus while still doing real dedup
 *      (gau 476ms->287ms vs urldedupe 335ms; lead widens with size). Three
 *      changes, none touch what is emitted:
 *      (1) repeat_junk got a sound 8-aligned word pre-filter. A qualifying
 *          repeat always holds a run of >=18 equal +L bytes, which covers a
 *          full 8-aligned window, so if no aligned 8-byte word equals its +L
 *          shift the precise scan must return 0 and is skipped. It was the
 *          top hotspot (~159ms); on real data the precise scan now ~never runs.
 *      (2) is_tld/is_pub_sfx (~200k calls) replaced strlen+tolower-per-entry
 *          with one integer compare against packed lowercase keys (LO[]).
 *      (3) is_garbage checks reordered cheap+high-hit first (is_garbage is a
 *          pure OR of side-effect-free predicates, so order cannot change the
 *          result, only the cost).
 *
 * v18.1: perf only, output BYTE-IDENTICAL to v18 on every flag and corpus.
 *      repeat_junk() was the #1 hotspot (~33% of runtime): for each period
 *      L=2..24 it called memcmp at every position, but almost all positions
 *      mismatch on byte 0. Added a single-byte gate s[i]==s[i+L] before the
 *      memcmp(L-1), so the common reject path is one compare instead of a
 *      memcmp call. Same logic, no behaviour change. ~25% faster end-to-end
 *      on gau (640ms -> 482ms).
 *
 * v18: object-id PRESERVE is now the DEFAULT, with a content-section
 *      exception. The id-class folds (all-digit 'N', uuid 'U', long-hex 'H',
 *      "<stem>-<digits>" id_stem and the v17 mixed_id_stem "<stem>-<alnum>")
 *      collapse distinct per-object references into one signature. For IDOR /
 *      broken-access / object-enumeration recon every distinct id is a
 *      distinct test target, so folding them away destroys real surface. v18
 *      turns the uuid/hex/id_stem/mixed folds OFF by default and gates them
 *      behind a new -F flag (Fold ids, the old aggressive endpoint-discovery
 *      behaviour). Numeric 'N' is the one with a nuance: a numeric segment
 *      whose PARENT is a content/listing word (cat, blog, article, product,
 *      forum, ... see is_content_section) is a content-item index, not an
 *      access-control object, so it STILL folds by default (/cat/9/details
 *      and /cat/11/details -> one); a numeric anywhere else (/api/users/123,
 *      /account/9/billing) is PRESERVED unless -F is given. What else stays
 *      ON by default: query KEY-SET dedup (?a=1 == ?a=2), title-slug fold
 *      (v15 is_slug + v16 content-section), exact-line dedup, the noise-asset
 *      filter (-a recovers) and the sanity gate. So default udud is "exact
 *      dedup that is still structurally smart", not sort -u; `udud -F`
 *      reproduces the v17 fold-everything behaviour. Single-pass,
 *      O(distinct sigs) RAM and O(seg)/line are unchanged - -F just lifts the
 *      content-section gate and re-enables the uuid/hex/stem cold branches.
 *      NOTE: this inverts the v17 default; any published benchmark that was
 *      measured on folded output must be re-run with -F to stay comparable.
 *
 * v17: mixed alphanumeric id fold. udud already templated all-digit ('N'),
 *      uuid ('U'), long-hex ('H') and "<stem>-<digits>" (id_stem) segments,
 *      but NOT a per-object reference that mixes letters and a digit run -
 *      e.g. /blah/U-61723A/settings vs /blah/U-63352B/settings stayed two
 *      distinct lines. urless folds these only with a HAND-SUPPLIED regex
 *      (-rcid 'U-[0-9]{5}[A-Z]'); uro/urldedupe/uddup do not fold them at
 *      all. New mixed_id_stem() auto-detects the shape "<alpha-stem><sep>
 *      <alnum token with a >=5 digit run and >=1 letter>" and folds it to
 *      stem+"-#" in the dedup signature only (emitted line stays the real
 *      first-seen URL). Gating on a 5+ digit run is the safety line - it
 *      never matches versions/words (v2, oauth2, mp3, utf8, sha256, a year),
 *      and preserving the stem keeps admin- vs user- ids distinct, so no
 *      route is destroyed. Output is BYTE-IDENTICAL to v14/v15/v16 on all
 *      four published corpora (synth/wb/gau/vulnweb - they hold no non-uuid
 *      mixed-id of this shape; uuids are still caught earlier by 'U'), so the
 *      published quality numbers and the line-by-line audit carry over
 *      unchanged. The new fold only manifests on corpora that DO contain the
 *      shape (e.g. /blah/U-61723A/...); any re-benchmark on such a corpus
 *      must re-validate quality. Still single-pass / O(distinct sigs) RAM /
 *      O(seg)/line - mixed_id_stem is a cold branch after id_stem fails.
 *
 * v16: content-section title-slug fold. v15 (and earlier) only folded a leaf
 *      slug when it ended in a numeric id (is_slug: a-brief-history-12 -> S).
 *      Digit-less human titles (/blog/why-people-suck-a-study,
 *      /news/how-to-lick-your-own-toes) were kept distinct, so a route that
 *      renders one handler over N articles emitted N near-identical lines.
 *      Fix: a digit-less multi-word lowercase leaf now folds to one 'S'
 *      witness IFF its PARENT segment is a known content section (blog(s),
 *      post(s), article(s), news, story/stories, tag(s), category/-ies,
 *      product(s)) - is_content_section() gate + is_text_slug() shape test.
 *      Gating on the parent is deliberate: /api/get-user-profile and
 *      /api/get-user-settings stay distinct (parent 'api' is not a content
 *      section), so no route is destroyed - one representative per content
 *      route is KEPT, never deleted. This CHANGES output vs v14/v15 on
 *      corpora containing digit-less content slugs, so the v15
 *      "byte-identical to v14" claim no longer covers v16: quality numbers
 *      and the line-by-line audit must be re-validated before re-publishing.
 *      Still strictly single-pass / O(distinct signatures) RAM / no survivor
 *      buffering; the gate is O(1) over a fixed word list on leaf segments.
 *
 * v15: speed overhaul to hold the Pareto frontier on wall time too. The Q1
 *      synthetic-corpus measurement (D_synth.full, 45410 lines, frozen rig)
 *      had urldedupe at 0.164s and udud at 0.214s - urldedupe winning the
 *      Execution Time column by 50 ms. Hand-instrumented the udud hot path:
 *      the per-byte locale-aware tolower(3) on the path lowering loop
 *      dominated (~80 calls/line * ~25 ns each), then the second full
 *      scan of the path in the canonical-output rebuilder, then 4x memmem
 *      on a 3-byte needle, then fwrite()+putchar() as two stdio operations.
 *      Targeted, no architecture change, still strictly single-pass /
 *      O(distinct signatures) RAM / NO survivor buffering / NO regression
 *      to attack-surface fidelity (byte-identical output to v14):
 *        - LO[256] ASCII lowercase LUT initialised once at main entry;
 *          replaces every tolower((unsigned char)c) in the per-line hot
 *          loop (host lowering, path-segment lowering, scheme lowering,
 *          Aho-Corasick haystack folding). LUT lookup is a single load+
 *          mask, no locale dispatch.
 *        - scheme lowered ONCE into a stack buffer schb[], then memcpy'd
 *          to both the dedup signature and the canonical-output buffer
 *          (v14 lowered it twice, ~5 tolower calls duplicated per line).
 *        - dedup-signature path loop now records the kept segment offsets
 *          (seg_s[]/seg_l[]) so the canonical-output rebuilder no longer
 *          re-walks the path string. The slash-collapse + dir-index-drop
 *          decisions are taken ONCE; pass-2 is a tight loop of memcpys
 *          over the recorded offsets.
 *        - memmem(p,rem,"://",3) (4 call sites in scheme detection /
 *          double-scheme repair) replaced with memchr(':') + bounded
 *          memcmp("//",2). glibc memmem uses Two-Way matching which is
 *          slower than a direct scan on a 3-byte needle.
 *        - canonical output finalises with the '\n' written INTO the out
 *          buffer, then a single fwrite() per emitted line (v14 used a
 *          separate putchar('\n') = two stdio calls per line).
 *        - setvbuf(stdin/stdout, 1 MB) so getline()/fwrite() amortise
 *          syscall overhead over million-byte blocks instead of the
 *          default 4 KB.
 *      Output is byte-identical to v14 across all 4 corpora (synth +
 *      D_example_wb + D_example_gau + D_vulnweb) - the audit verdict
 *      (zero real attack surface lost) stands unchanged. See README.md
 *      and udud-benchmark/BENCHMARK.md for the published wall/RSS
 *      numbers.
 *
 * v14: published de-identified Q1 re-benchmark (D_example_*) hand-audited
 *      udud's per-cell lost lines line by line. The de-identified gau
 *      corpus exposed one REAL destruction that v13 was making and the
 *      v13 AUDIT.md had mis-classified: the embedded-domain spam gate
 *      destroyed qeif.tv.example.com/qeif/p1/dc/pqawjqix (de-id form
 *      qeif.tv.example.com/qeif/p1/dc/pqawjqix), a genuine authenticated
 *      endpoint, falsifying the "no authenticated endpoint destroyed"
 *      contract. Fix at source, still strictly single-pass / NO buffering:
 *        - host_embeds_domain() identifies an AMBIGUOUS host shape
 *          [www.]NAME.PUBSFX.APEX (NAME digit-free, PUBSFX a TLD or HSFX
 *          interior label sitting exactly at index k-3). Both re-rooted
 *          crawler spam (freebitco.in.vulnweb.com, bing.com.vulnweb.com)
 *          and legit deep corporate subdomains (qeif.tv.example.com,
 *          jxsti.info.example.com, awyxqc-*.tv.example.com) match the shape;
 *          the registrable-apex wildcard property that actually
 *          distinguishes them is not locally observable from the URL.
 *          v13 dropped on host alone, destroying the auth endpoint.
 *          Added is_shallow_path(path,prem): a request is shallow if
 *          the path is empty, '/', or '/'+one root-admin/crawler file
 *          (robots.txt, favicon.ico, sitemap.xml, humans.txt,
 *          security.txt, ads.txt, crossdomain.xml,
 *          clientaccesspolicy.xml, apple-touch-icon*.png, a passive
 *          index.*), with no query and no fragment. Combined at the
 *          call site: the gate now fires only when host_embeds_domain()
 *          AND is_shallow_path(). The de-id audit confirms this
 *          discriminator: the 5 wb.full hosts the gate touches
 *          (linear-XX.tv, download-aoc.tv, depot.info) all have only
 *          shallow paths in the input and are still dropped (zero real
 *          surface lost); auth.tv (the destroyed real endpoint) has a
 *          deep path and is now KEPT.
 *      Residual: vulnweb's wildcard DNS apex produces 26 "mirror"
 *      lines under alternate hostnames (bing.com.vulnweb.com/Flash/...,
 *      hotelresidenceitalia.com.vulnweb.com/admin/...) carrying deep
 *      mirror paths from the canonical testphp.vulnweb.com box. v14
 *      keeps these as distinct signatures because they are deep, not
 *      shallow. They are not destroyed surface (the canonical routes
 *      remain) and they are not new endpoints (same target box via
 *      wildcard); they are conservatively retained noise, the
 *      unavoidable cost of removing the structural false-positive that
 *      destroyed auth. Documented in AUDIT.md.
 *      One added predicate, one combined call site - architecture
 *      unchanged, still O(unique signatures) RAM, still single-pass.
 *
 * v13: Q1-grade re-benchmark (N=10, fixed-clock, canonicalization-invariant
 *      quality metric) hand-audited every udud "loss" and exposed a FOURTH
 *      genuine destruction bug, fixed at source, still strictly single-pass /
 *      NO buffering:
 *        - bad_bytes()'s BB_S[] (the both-path-AND-query Aho-Corasick set
 *          that drops the WHOLE url) listed the LFI / traversal / file-
 *          disclosure tokens etc/passwd, etc%2fpasswd, /etc/shadow,
 *          boot.ini, win.ini, %2e%2e%2f, ..%2f, ..%5c. In a QUERY string
 *          those tokens are not garbage - they mark a real ?file= /
 *          ?include= / ?page= LFI parameter whose ENDPOINT + param-name +
 *          dedup-signature must survive (clean_query() already blanks the
 *          payload VALUE, never emitting it). The both-sides set fired on
 *          the query and silently dropped the whole url, so the only LFI
 *          line on vulnweb (http://testphp.vulnweb.com/?file=../../../../
 *          etc/passwd) was destroyed 2->0. This directly contradicted the
 *          file's own header design note and clean_query()'s contract.
 *          Fix: those tokens moved OUT of BB_S (both-sides) and the file-
 *          disclosure ones (etc/passwd, etc%2fpasswd, /etc/shadow,
 *          boot.ini, win.ini) added to BP_S (PATH-only drop). A literal
 *          /etc/passwd in the PATH is still dropped; traversal ../, %2e%2e,
 *          %5c were already PATH-only in BP_S so path-side detection is
 *          unchanged; a ?file=../../etc/passwd LFI endpoint is now KEPT
 *          with its value blanked. Verified surgical: vulnweb 1382->1383
 *          (+1 = exactly the recovered LFI), D_example_wb.full 781398-line
 *          stream delta 0, deterministic, zero garbage leaked, zero loss.
 *        - RAM claim corrected (honesty): older blocks below say "RSS
 *          constant ~3.5 MB (~6.6 MB on the 634k stream)". That is WRONG.
 *          udud keeps one templated signature per UNIQUE structural class,
 *          so peak RSS is O(distinct signatures), not constant. Measured
 *          on the frozen Q1 rig: 18.3 MB peak on the 781398-line
 *          wayback corpus (124975 unique signatures). It is still by far
 *          the lowest-RSS tool that is not a trivial passthrough, and
 *          still single-pass with no per-line/cross-line buffering - that
 *          (O(unique) not O(input)) is the win - but it is not "constant".
 *
 * v12: real per-line pentester audit on the gau + wayback corpora
 *      (44943 / 634k lines) exposed THREE genuine source defects - all
 *      destructive (real attack surface silently dropped), all fixed at
 *      source, still strictly single-pass / O(1)-extra-RAM, NO buffering:
 *        - mangled_script(): the ".htm"->".html" branch was DEAD CODE.
 *          M[]={".php",".asp",".jsp",".htm",".cgi",".pl"}; the test was
 *          `else if(M[m][1]=='t')` but for ".htm" M[m][1]=='h', so the
 *          branch never matched -> EVERY .html terminal segment was
 *          flagged "mangled extension" and the whole URL DROPPED. udud
 *          had been silently destroying 100% of .html across vulnweb /
 *          testinvicti / gau / wayback through v7..v11. Fixed
 *          to `else if(M[m][1]=='h')` (only ".htm" has [1]=='h'; ".php"
 *          is already caught by the preceding [1]=='p'&&[2]=='h' test),
 *          so .html is preserved with zero regression to php/asp/jsp/pl/
 *          cgi. vulnweb +28 real .html restored (login.html, xss.html,
 *          path-disclosure-unix/win.html, Angular partials, RateProduct-
 *          1.bak.html); gau +427, wayback ~+1700.
 *        - bad_bytes(): a bare '=' in the PATH was treated as a glued
 *          query (/artists.php=1) and rejected - but that also destroyed
 *          the standard J2EE matrix param ;jsessionid= / ;sid= (a REAL
 *          authenticated endpoint, e.g. Authenticate.do;jsessionid=..).
 *          Now a ';' arms a `semi` flag and '=' is allowed only after it
 *          (RFC3986 matrix param); a bare '=' with no preceding ';' is
 *          still rejected. wayback keeps 28 ;jsessionid= endpoints
 *          that uro and urless both destroy (0).
 *        - host_embeds_domain(): "any interior public-suffix label"
 *          false-positived legitimate deep corporate DNS - the corpus's
 *          www.en.xect.example.com (204 lines, the whole the .woa app app),
 *          zfqyyxa.en.xect.example.com, qko07.info.example.com (116 lines,
 *          8 store .woa) were DROPPED because uk/info are in the suffix
 *          set. Tightened to the exact re-rooted-SEO-spam shape only
 *          ([www.]NAME.PUBSFX.APEX with the suffix exactly at k-3 and a
 *          digit-free registrable NAME); every validated vulnweb spam
 *          host is still dropped, the corporate surface is restored.
 *      These were destruction bugs (best != fewest lines): the earlier
 *      "non-destructive, verified line-by-line" claims for vulnweb v7..
 *      v11 and the gau v11 verdict were WRONG re .html and are
 *      corrected here. Architecture unchanged - 3 precise source fixes,
 *      not a rewrite; still single-pass, RSS still constant ~3.5 MB
 *      (~6.6 MB on the 634k-line wayback stream).
 *
 * v11: read uddup & urless source to learn WHY their testinvicti output
 *      is small, then match the good part WITHOUT their destruction.
 *      Findings:
 *        - uddup gets /blog -> 1 by blindly keying on the PARENT path
 *          (drop last segment). But its ignored_suffixes deletes EVERY
 *          .js .json .txt .xml .zip .pdf .doc(x) - it silently lost
 *          swagger.json, the .js and robots.txt on testinvicti. It
 *          also buffers the whole corpus (O(n^2), the 21-min path).
 *        - urless gets /blog -> 0 via a hardcoded keyword BLACKLIST
 *          "blog,article,news,..." - it deletes ANY path containing
 *          those words (it also nuked Blogs.aspx) and emits a trailing
 *          blank line. Its real dedup only templates int/GUID/lang.
 *      So their small counts are DESTRUCTION, not precision; udud's
 *      single-pass per-line-signature architecture is sound - it was
 *      only missing one templating class. Added is_slug(): a LEAF
 *      title-slug (lowercase, >=2 '-', no '.', ends in -<digits>,
 *      len>=12) is a content-item VALUE -> one 'S' marker in the
 *      dedup signature, so /blog/<slug> collapses to ONE real
 *      first-seen URL (emitted verbatim) while swagger.json, *.js,
 *      robots.txt, Blogs.aspx, process.bak are ALL kept. testinvicti
 *      163 -> 35, blog 36->1, 0 garbage, 0 blank, nothing real lost -
 *      and udud is the ONLY tool that keeps swagger.json + .js +
 *      robots.txt while still collapsing blog to one. vulnweb corpus
 *      1354 -> 1354 UNCHANGED (the rule is strict enough that the
 *      regression set has no false slug), every finding count / 53
 *      distinct .js / 0 dups identical, all garbage hunts still 0.
 *      O(seg)/line, no state, no buffering - RSS still ~3.5 MB
 *      (3648 KB real / 3556 KB on the 3.03M-line 242 MB stream).
 *
 * v10: head-to-head vs uro/urldedupe/urless/uddup on testinvicti.com
 *      exposed two real udud defects - both fixed at source, still
 *      single-pass / O(1)-extra-RAM, NO buffering:
 *        - is_index() was TOO BROAD: it folded named server-side
 *          handlers (Default.aspx, home.jsp, index.aspx, default.php)
 *          into "/", silently losing real distinct surface (every
 *          other tool keeps Default.aspx). Now ONLY the universally-
 *          passive indexes collapse: index.{html,htm,php} and
 *          default.{html,htm}. On vulnweb this RESTORED 8 real handler
 *          pages (Default.asp, adm1nPan3l/home.php, default.php,
 *          index.aspx, ...) - a latent surface-loss bug.
 *        - id_stem(): a path segment "<alpha-stem><sep><digits>" with
 *          NO '.' (blog-post / product id: /blog/is-bitcoin-anonymous-60,
 *          Mod_Rewrite_Shop/BuyProduct-2) carries a per-item VALUE
 *          exactly like ?id=3 vs ?id=4. It is now folded in the DEDUP
 *          SIGNATURE only (stem+marker) so same-stem variants collapse
 *          to ONE real representative, while a DIFFERENT stem stays a
 *          distinct signature. testinvicti /blog 36 -> 5 (all 5 article
 *          templates kept) where uro folds ALL to 1 and urless drops
 *          the whole route. vulnweb: BuyProduct-{2,3,2/3,3/3} collapse
 *          onto the surviving BuyProduct-1 (0 surface lost).
 *      Net vulnweb 1350 -> 1354 (-4 product-id near-dups, +8 restored
 *      handler pages); every source-disclosure / SSRF / open-redirect /
 *      Acunetix-LFI-XXE count identical, 53 distinct .js unchanged, 0
 *      exact dups, all garbage hunts still 0. Peak RSS still constant
 *      ~3.5 MB (3496 KB real / 3532 KB on 3.03M-line 242 MB stream).
 *
 * v9: line-by-line pentester audit (path/query level) - 10 garbage classes
 *     the v8 still emitted, all fixed at source, all O(1)/line, NO buffering:
 *       - schema_scrape()  : drop external-DTD/XSD scrape segs (loose.dtd .xsd)
 *       - host_embeds ccTLD: is_pub_sfx()+HSFX[] host-only suffix set so
 *                            freebitco.in.vulnweb.com is caught ('in' cannot
 *                            go in TLDS[] - is_tld() also runs on path segs)
 *       - WB[] += 5c 5c2f  : strip %5c-mangled host fragments
 *       - bad_qname()      : mangled query NAMES - %/+ in name, no-alnum
 *                            name (?-= ; '_' kept for jQuery ?_= cache-bust),
 *                            pure-% valueless blob (?%2F), pure-punct token
 *       - is_junk_tok()    : pure-digit valueless token (collapsed 30
 *                            xss.js?<digits> cache-bust dups of the 1 real
 *                            testasp.vulnweb.com/t/xss.js, still emitted)
 *       - bad_bytes path  += '!' : kill scraped /!4.rs spam token
 *       - repeat_seg()     : consecutive identical path segments
 *                            (foo/foo crawler self-recurse, 42 lines)
 *     Proof non-destructive: v8->v9 84 removed / 0 added, 53->53 distinct
 *     .js files, every source-disclosure / SSRF / open-redirect / Acunetix
 *     LFI-XXE count identical, 0 exact dups, 15185 -> 1350. RSS still
 *     3.4-3.6 MB constant (verified on 3.0M-line / 242 MB input). The
 *     incremental crawler PREFIX-WALK (/sear->/search.php, BuyP->BuyProduct,
 *     Mod_Rewr->Mod_Rewrite_Shop) is left intact ON PURPOSE - those are
 *     valid distinct URLs; collapsing them needs cross-line survivor
 *     buffering (uddup's 21-min path) which would forfeit the single-pass
 *     / constant-RAM property that is the entire point of udud.
 *
 * v8: line-by-line pentester audit (host level) - host_embeds_domain():
 *     drop hosts whose SUBDOMAIN portion embeds a public registrable
 *     domain (bing.com.vulnweb.com, www.bing.com.vulnweb.com,
 *     blogger.com.vulnweb.com, www.hotelresidenceitalia.com.vulnweb.com)
 *     - vulnweb.com is wildcard DNS so these are crawler/comment-spam
 *     host-confusion, never a distinct target. The same source-disclosure
 *     paths survive via the real testphp/testasp hosts (identical box),
 *     so 0 attack surface lost (verified: every finding TYPE stays >=1,
 *     .js 85->85, 0 dups). O(labels)/line on a fixed stack array - the
 *     single-pass / ~3.5 MB RSS win is fully preserved. 1466 -> 1434.
 *
 * v7: line-by-line pentester audit fixes - glued_tld() label-walk bug
 *     (skipped alternate dotted labels, leaking testasp.vulnweb.com x2
 *     hosts & /Hosttestphp.vulnweb.comServer /testphp.vulnweb.com5);
 *     embedded_domain/glued_tld now also run on the TERMINAL segment;
 *     3a2f2f path marker (https%3A%2F%2F-mangled embedded URL);
 *     &amp; query marker (HTML-entity-scraped href); clock_frag()
 *     (?test=query[14:13:39 terminal-log capture); payload-value scrub
 *     extended (javascript%3A , %26%26/+ls RCE). Single-pass / O(1)
 *     extra RAM preserved - no cross-line buffering added.
 *
 * v5: sanity gate (DEFAULT, disable with -x) - before dedup, drop
 *     syntactically-invalid scan artefacts that are NOT real endpoints:
 *     fuzzer payloads (SQLi/XSS/LFI/RCE), embedded URLs in the path,
 *     glued HTTP header/request-line fragments, FUZZ placeholders and
 *     mangled extensions (artists.phpA01). The query string is treated
 *     leniently so open-redirect / SSRF / cmd params are preserved.
 *
 * v2 fixes over v1 (all proven against vulnweb recon data):
 *   - path canonicalisation: collapse "//" and strip trailing "/" so
 *     /foo and /foo/ dedup (v1 leaked these).
 *   - double-scheme repair: http://http://x , http://http:/x , ://http:
 *     are reparsed to the inner URL (correct parsing, default-on).
 *   - wayback-clean (DEFAULT, disable with -W): strip leading percent-
 *     encoding artefact tokens from the host (2f 2e 3a 3d 23 3f 25 252f
 *     25252f 2528 253d 2532 3b 40) ONLY when the remainder is still a
 *     syntactically valid host (safety guard keeps real hosts like
 *     httpbin.org / 3m.com untouched).
 *   - canonical output (DEFAULT, disable with -r): emit a cleaned, real,
 *     usable URL (lowered scheme+host, no default port, no fragment, no
 *     empty '?', collapsed slashes) instead of the raw first-seen line.
 *
 * Dedup key = scheme://host[:port]/<canonical+templated path>?<sorted param keys>
 *   host lowercased; default ports (80/443) dropped; fragment dropped;
 *   path segments that are pure-numeric/hex-hash/uuid -> token;
 *   query reduced to sorted unique KEY set (values ignored unless -k).
 *
 * Cleanest output is the DEFAULT (no flags): sanity gate, render-noise
 * filter (.js + archives/docs KEPT), case-insensitive path, dir-index
 * collapse, wayback host clean, canonical URL emission - ALL automatic.
 * Opt-outs: -x keep invalid/scan-artefact URLs | -a keep all assets |
 *        -s case-sensitive path | -W keep raw hosts | -r raw first-seen |
 *        -k keep param values | -p no path templating | -V stats |
 *        (-f/-w/-c legacy no-ops)
 * Build: cc -O3 -march=native -flto -o udud udud.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/resource.h>

/* ---------- ASCII lowercase LUT (v15: tolower hot-path replacement) ---------- *
 * tolower(3) is locale-aware (TLS lookup + per-call dispatch on glibc). On the
 * synthetic corpus the per-line path-lowering loop was the single largest hot
 * spot. URLs are ASCII by RFC; a 256-byte branch-free LUT folds A-Z to a-z and
 * passes everything else through, which is what udud's path/host/scheme
 * lowering already wanted (the surrounding code never touched non-ASCII bytes
 * in those positions because bad_bytes() drops them upstream). */
static unsigned char LO[256];
static void lo_init(void){
    for(int i=0;i<256;i++) LO[i]=(unsigned char)((i>='A'&&i<='Z')?(i|0x20):i);
}

/* ---------- arena ---------- */
static char  *arena = NULL;
static size_t arena_len = 0, arena_cap = 0;
static size_t arena_put(const char *s, size_t n) {
    if (arena_len + n > arena_cap) {
        while (arena_len + n > arena_cap) arena_cap = arena_cap ? arena_cap * 2 : (1u << 20);
        if (!(arena = realloc(arena, arena_cap))) { perror("realloc"); exit(1); }
    }
    size_t off = arena_len; memcpy(arena + off, s, n); arena_len += n; return off;
}

/* ---------- exact-keyed open-addressing set ---------- */
typedef struct { unsigned long long h; size_t off; unsigned len; unsigned rec; } Slot;
static Slot  *tab = NULL;
static size_t tab_cap = 0, tab_cnt = 0;
static unsigned long long fnv1a(const char *p, size_t n) {
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)p[i]; h *= 1099511628211ULL; }
    return h;
}
static void tab_init(size_t c){ tab_cap=1; while(tab_cap<c) tab_cap<<=1;
    if(!(tab=calloc(tab_cap,sizeof(Slot)))){perror("calloc");exit(1);} }
static void tab_grow(void){ size_t oc=tab_cap; Slot*ot=tab; tab_cap<<=1;
    if(!(tab=calloc(tab_cap,sizeof(Slot)))){perror("calloc");exit(1);}
    for(size_t i=0;i<oc;i++){ if(!ot[i].len)continue; size_t j=ot[i].h&(tab_cap-1);
        while(tab[j].len) j=(j+1)&(tab_cap-1); tab[j]=ot[i]; } free(ot); }
static int tab_add(const char *s, size_t n){
    if(tab_cnt*4>=tab_cap*3) tab_grow();
    unsigned long long h=fnv1a(s,n); size_t j=h&(tab_cap-1);
    while(tab[j].len){ if(tab[j].h==h&&tab[j].len==n&&!memcmp(arena+tab[j].off,s,n)) return 0;
        j=(j+1)&(tab_cap-1); }
    tab[j].h=h; tab[j].off=arena_put(s,n); tab[j].len=(unsigned)n; tab_cnt++; return 1;
}

/* ---------- deferred-emit records (query-keyset merge) ----------
 * Default keyset mode collapses every query-bearing URL of one templated
 * path into the single richest representative (most distinct params; tie
 * keeps first-seen). The richest can appear AFTER a poorer one in the
 * stream, so output is held until EOF: one Rec per group, in first-seen
 * order. No-query URLs form their own group (sig has no '?') and are kept
 * first-seen, never absorbed by / absorbing a query group. */
typedef struct { size_t off; unsigned len; unsigned pcount; } Rec;
static Rec   *recs = NULL;
static size_t rec_cap = 0, rec_cnt = 0;
static void rec_push(size_t off, unsigned len, unsigned pc){
    if(rec_cnt>=rec_cap){ rec_cap=rec_cap?rec_cap*2:4096;
        if(!(recs=realloc(recs,rec_cap*sizeof(Rec)))){perror("realloc");exit(1);} }
    recs[rec_cnt].off=off; recs[rec_cnt].len=len; recs[rec_cnt].pcount=pc; rec_cnt++;
}
/* find sig; on miss insert it bound to the next record index. *is_new tells
 * the caller whether to push a fresh Rec (1) or update recs[return] (0). */
static unsigned tab_group(const char *s, size_t n, int *is_new){
    if(tab_cnt*4>=tab_cap*3) tab_grow();
    unsigned long long h=fnv1a(s,n); size_t j=h&(tab_cap-1);
    while(tab[j].len){ if(tab[j].h==h&&tab[j].len==n&&!memcmp(arena+tab[j].off,s,n)){
            *is_new=0; return tab[j].rec; }
        j=(j+1)&(tab_cap-1); }
    tab[j].h=h; tab[j].off=arena_put(s,n); tab[j].len=(unsigned)n;
    tab[j].rec=(unsigned)rec_cnt; tab_cnt++; *is_new=1; return (unsigned)rec_cnt;
}

/* ---------- predicates ---------- */
static int all_digits(const char*s,size_t n){ if(!n)return 0;
    for(size_t i=0;i<n;i++) if(!isdigit((unsigned char)s[i]))return 0; return 1; }
static int is_hex(const char*s,size_t n){
    for(size_t i=0;i<n;i++) if(!isxdigit((unsigned char)s[i]))return 0; return 1; }
static int is_uuid(const char*s,size_t n){ if(n!=36)return 0;
    for(size_t i=0;i<36;i++){ if(i==8||i==13||i==18||i==23){if(s[i]!='-')return 0;}
        else if(!isxdigit((unsigned char)s[i]))return 0;} return 1; }
/* render-noise ONLY: css/img/font/media. Archives & documents (pdf zip
 * doc xls sql bak swf ...) are NEVER auto-dropped - they can be findings. */
static const char*NOISE_EXT[]={"css","png","jpg","jpeg","gif","svg","ico","bmp",
 "webp","tif","tiff","woff","woff2","ttf","eot","otf","mp4","mp3","avi","mov",
 "webm","wav","ogg","m4a","flac","mkv","map",0};
static int is_noise(const char*p,size_t n){
    size_t i=n; while(i&&p[i-1]!='.'&&p[i-1]!='/')i--;
    if(!i||p[i-1]!='.')return 0; size_t el=n-i; if(!el||el>=12)return 0;
    char lo[12]; for(size_t k=0;k<el;k++)lo[k]=tolower((unsigned char)p[i+k]); lo[el]=0;
    for(int x=0;NOISE_EXT[x];x++) if(!strcmp(lo,NOISE_EXT[x]))return 1; return 0; }
/* last path segment = a PASSIVE directory-index document that is
 * universally equivalent to the directory itself (/dir/index.html ==
 * /dir/). v9 audit (testinvicti.com): a NAMED server-side handler -
 * Default.aspx, home.jsp, index.cgi - is NOT a passive alias; it is a
 * distinct application entry point a pentester must test (.aspx.cs /
 * .jsp source disclosure, ViewState, explicit-vs-default routing, WAF
 * rule differences) and every other tool keeps it. Collapsing it into
 * "/" lost real surface, violating the non-destructive mandate. So:
 * only the classic static/front-controller indexes collapse - stem
 * MUST be "index" (html|htm|php) or "default" (html|htm). 'home.*',
 * 'default.aspx', 'index.jsp', server handlers etc. stay distinct. */
static int is_index(const char*s,size_t n){
    size_t d=0; while(d<n&&s[d]!='.')d++; if(d==n||d>=8)return 0;
    size_t en=n-d-1; if(en==0||en>=5)return 0;
    char st[8],ex[5];
    for(size_t k=0;k<d;k++) st[k]=tolower((unsigned char)s[k]); st[d]=0;
    for(size_t k=0;k<en;k++) ex[k]=tolower((unsigned char)s[d+1+k]); ex[en]=0;
    int idx=!strcmp(st,"index"), def=!strcmp(st,"default");
    if(!idx&&!def)return 0;
    if(idx&&(!strcmp(ex,"html")||!strcmp(ex,"htm")||!strcmp(ex,"php")))return 1;
    if(def&&(!strcmp(ex,"html")||!strcmp(ex,"htm")))return 1;
    return 0; }

/* path segment "<alpha-stem><sep><digits>" with NO '.' in it: the
 * trailing -<id> / _<id> is a per-item VALUE (blog-post id, product
 * id, /category-7) exactly like ?id=3 vs ?id=4 - it is collapsed in
 * the DEDUP SIGNATURE only (stem + marker) so same-stem variants
 * dedupe to ONE representative, while a DIFFERENT stem stays a
 * distinct signature (zero surface lost - unlike uro which folds
 * every /blog/<slug> to one, or urless which drops the whole route).
 * The EMITTED line is still the real first-seen URL. The dot-free
 * guard means file.ext, hashed assets (main.b4f14864.js), dot-01.gif,
 * swagger.json and process.bak source-disclosure are NEVER touched.
 * O(seg)/line, no state, no buffering. Returns stem length or 0. */
static size_t id_stem(const char*s,size_t n){
    if(n<3)return 0;
    size_t e=n; while(e&&s[e-1]>='0'&&s[e-1]<='9')e--;
    if(e==n||e<2)return 0;                 /* no trailing digits / no room */
    char sep=s[e-1]; if(sep!='-'&&sep!='_')return 0;
    size_t st=e-1;                         /* stem = s[0 .. st)            */
    int alpha=0;
    for(size_t i=0;i<st;i++){ char c=s[i];
        if(c=='.')return 0;                /* never touch a dotted segment */
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z'))alpha=1; }
    return alpha?st:0; }

/* path segment "<alpha-stem><sep><opaque-id>" where <opaque-id> is a MIXED
 * alphanumeric token (no '.') that carries a run of >=5 consecutive digits
 * AND at least one letter - e.g. U-61723A, ref_4F00219X, order-2024Q7. This
 * is the alphanumeric sibling of id_stem(): the trailing token is a per-
 * object reference (user id, order id, asset code) exactly like a numeric
 * ?id=, so it folds in the dedup SIGNATURE only (stem + "-#"). Same-stem
 * refs collapse to ONE real first-seen representative while a DIFFERENT stem
 * (admin- vs user-) stays a distinct signature, so no route is destroyed.
 * Pure-digit trailing ids stay with id_stem(); requiring >=1 letter here
 * keeps the two disjoint. The >=5 digit-run guard is the safety line: it
 * never fires on versions/words/codes (v2, oauth2, mp3, utf8, sha256, h264,
 * win32, a 4-digit year) - those have no 5+ digit run - so only genuine
 * high-entropy identifiers fold. O(seg)/line, no state. Returns stem len. */
static size_t mixed_id_stem(const char*s,size_t n){
    if(n<7)return 0;
    size_t st=n;                                 /* find LAST '-' or '_'     */
    while(st&&s[st-1]!='-'&&s[st-1]!='_'){
        if(s[st-1]=='.')return 0;                /* dotted id-token -> never */
        st--; }
    if(st==0||st==n)return 0;                    /* no sep, or sep at end    */
    size_t run=0,best=0; int letter=0;           /* id-token = s[st .. n)    */
    for(size_t i=st;i<n;i++){ char c=s[i];
        if(c>='0'&&c<='9'){ if(++run>best)best=run; }
        else if((c>='A'&&c<='Z')||(c>='a'&&c<='z')){ run=0; letter=1; }
        else return 0;                           /* non-alnum -> not an id   */
    }
    if(!letter||best<5)return 0;
    size_t stem=st-1; int alpha=0;               /* stem = s[0 .. st-1)      */
    for(size_t i=0;i<stem;i++){ char c=s[i];
        if(c=='.')return 0;                      /* never touch dotted stem  */
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z'))alpha=1; }
    return alpha?stem:0; }

/* LEAF segment that is a human-readable TITLE SLUG: a content-item
 * identifier (blog post / news article / product title) where the
 * whole slug - words AND id - is a VALUE, not distinct code surface.
 * /blog/how-does-bitcoin-work-63 and /blog/is-bitcoin-anonymous-60
 * hit the SAME blog-render handler; a bug hunter tests it ONCE. So
 * the entire leaf is templated to one marker in the DEDUP SIGNATURE
 * => every /blog/<slug> collapses to ONE real representative (the
 * first-seen URL is still emitted verbatim). This is what uddup gets
 * by blindly dropping the last segment (but it also deletes every
 * .js/.json/.txt - losing swagger.json) and urless by a blog/news
 * keyword BLACKLIST (it deletes Blogs.aspx too). udud gets the same
 * collapse with ZERO collateral loss, single-pass, no buffering.
 * Strict so real distinct endpoints are never merged - ALL of:
 *   - it is the last path segment (parents define the section)
 *   - no '.'  (swagger.json, *.js, *.bak, file.ext never touched)
 *   - length >= 12
 *   - >= 2 '-' hyphens (a multi-word phrase, not /api/token)
 *   - ends with "-<digits>" (the decisive content-item id signal)
 *   - all letters lowercase (excludes Mod_Rewrite_Shop / PascalDirs)
 * "different parent" still yields a different signature, so /news/<x>
 * and /blog/<y> stay distinct. O(seg)/line, no state. */
static int is_slug(const char*s,size_t n,int last){
    if(!last||n<12)return 0;
    if(s[n-1]<'0'||s[n-1]>'9')return 0;          /* must end in a digit  */
    size_t e=n; while(e&&s[e-1]>='0'&&s[e-1]<='9')e--;
    if(!e||s[e-1]!='-')return 0;                 /* ... preceded by '-'  */
    int hy=0;
    for(size_t i=0;i<n;i++){ char c=s[i];
        if(c=='.')return 0;
        if(c=='-'){ hy++; continue; }
        if(c>='A'&&c<='Z')return 0;              /* lowercase-only slug  */
    }
    return hy>=2; }

/* known content/listing section words. A digit-less multi-word title slug
 * sitting directly under one of these (e.g. /blog/a-brief-history-of-time,
 * /news/how-to-lick-your-own-toes) is a content-item VALUE hitting one
 * render handler, so it folds to a single representative. Gating on the
 * PARENT is what keeps distinct route names under /api/, /v1/, ... from
 * ever being merged. Case-insensitive; O(1) over a fixed word list. */
static int is_content_section(const char*s,size_t n){
    static const char*const W[]={"blog","blogs","post","posts","article",
        "articles","news","story","stories","tag","tags","category",
        "categories","cat","cats","catalog","product","products","item",
        "items","topic","topics","thread","threads","forum","forums",
        "comment","comments","review","reviews","page","pages","section",
        "sections","listing","listings",0};
    for(int i=0;W[i];i++){ size_t l=strlen(W[i]); if(l!=n)continue;
        size_t j=0; for(;j<n;j++){ char c=s[j];
            if(c>='A'&&c<='Z')c+=32; if(c!=W[i][j])break; }
        if(j==n)return 1; }
    return 0; }

/* digit-less form of is_slug(): a multi-word lowercase leaf that does NOT
 * end in a numeric id. Only consulted when the parent is a content section
 * (is_content_section), so it cannot merge distinct routes the way a
 * blanket rule would. Same guards as is_slug minus the trailing-digit. */
static int is_text_slug(const char*s,size_t n,int last){
    if(!last||n<12)return 0;
    int hy=0;
    for(size_t i=0;i<n;i++){ char c=s[i];
        if(c=='.')return 0;
        if(c=='-'){ hy++; continue; }
        if(c>='A'&&c<='Z')return 0; }
    return hy>=2; }

/* scan-artifact / invalid-URL detector: drops fuzzer payloads, embedded
 * URLs, header fragments and mangled extensions - things that are NOT
 * real endpoints. Conservative on the query string so open-redirect /
 * SSRF / RCE parameters survive (their values are dropped by dedup
 * anyway, only the KEY set matters). */
static int bad_bytes(const char*s,size_t n,int isq){
    int semi=0;                                    /* seen ';' matrix intro */
    for(size_t i=0;i<n;i++){ unsigned char ch=s[i];
        if(ch<0x20||ch==0x7f||ch==' ')return 1;
        if(strchr("\"<>\\^`{}|",ch))return 1;
        if(!isq){
            if(ch>=0x80)return 1;                  /* raw non-ASCII in path
                (login.ph<U+2026> ellipsis & other copy-paste garbage) */
            if(ch==';')semi=1;                      /* RFC3986 matrix param  */
            if(strchr("[]'*()$,&!",ch))return 1;   /* path is stricter:
                ',' kills SQLi UNION column-dump (fit.txt,1,1803,0,active),
                '$' kills glued crypt-hash artefacts (/acunetix_file/$1$..),
                '&' kills a query mangled into the path with no '?'
                (/login.php&fcbz=1  /&ved=..&usg=..),
                '!' kills scraped junk tokens (/!4.rs spam fragment) */
            if(ch=='='&&!semi)return 1;            /* bare '=' = glued query
                (/artists.php=1) BUT a ';name=val' matrix param (standard
                J2EE ;jsessionid= / ;sid=) is a REAL endpoint - keep it,
                never destroy /authenticate/Authenticate.do;jsessionid=.. */
        }
    } return 0; }
/* injection / fragment markers checked in BOTH path and query */
static const char*BB_S[]={
 "%3c","%3e","%22","%27","%00","%0a","%0d","%09","<script","</script",
 "javascript:","onerror","onload=","onmouseover","alert(","prompt(",
 "confirm(","union select","union%20select","unionselect","order by",
 "order%20by","information_schema","xp_cmdshell","sleep(","benchmark(",
 "waitfor delay","pg_sleep","extractvalue(","updatexml(","concat(",
 "char(","0x3c","%252e","&quot","&gt;","&lt;","&amp;",
 /* LFI / path-traversal / file-disclosure payloads (etc/passwd,
  * boot.ini, ..%2f, %2e%2e%2f, ..) are NOT in this both-sides list:
  * in a QUERY they are exactly the ?file= / ?include= LFI parameter a
  * pentester must keep. clean_query()/val_is_payload() already blank
  * the payload VALUE (-> ?file=) so the endpoint, the param name and
  * the dedup identity all survive while the payload is never emitted.
  * They stay garbage in the PATH only - see BP_S below. */
 " http/","%20http/","http/1.","user-agent:","host:","cookie:",
 "referer:","x-forwarded","accept-encoding","fuzz",
 /* Acunetix scanner probe markers (vulnweb is its own testbed) -
  * percent-encoded U+02C8 'ˈ', U+2192 '->', U+201x smart quotes/dashes,
  * and the LFI probe target string - never a real endpoint */
 "%cb%93","%e2%86%92","inexistent",
 /* encoded invisible / typographic chars: NBSP, soft-hyphen, BOM, and
  * the ENTIRE U+2000..U+206F general-punctuation block (%e2%80 / %e2%81
  * lead - dashes, smart quotes, bullet, zero-width, line/para sep, even
  * truncated %e2%80). None ever valid in a real path - pure scanner /
  * copy-paste noise. */
 "%c2%a0","%c2%ad","%ef%bb%bf","%e2%80","%e2%81",
 /* double-percent-encoding = scanner re-encode artefact, never a real
  * endpoint: %2525.. (%252526 %252520 ..ls -la RCE), %25%25, %252f      */
 "%2525","%25%25","%252f",
 /* percent-encoded non-ASCII text in path/query (CJK/Cyrillic/Arabic
  * sqlinject comments, typographic ligatures fi/fl) - i18n scanner
  * probes, never a real path: infocateg.php%E3%80%91..sqlinject..,
  * categories.php%D0%92, showimage.php?%EF%AC%81le= (mangled 'file') */
 "%ef%ac","%e3%80","%e5%ad","%d0%","%d1%","%d8%","%d9%",0};
/* markers that are garbage ONLY in the path (a query may legitimately
 * carry http:// for open-redirect / SSRF parameters). The percent-encoded
 * forms of bytes bad_bytes() already forbids literally in a path
 * ([ ] \ ^ ` { } | %) are equally invalid when encoded in the path. */
static const char*BP_S[]={
 "http:","https:","://","..","%2e%2e","%20","%09","%00","%3f",
 "%5b","%5c","%5d","%5e","%60","%7b","%7c","%7d","%25",
 /* file-disclosure target strings: still garbage when they appear in
  * the PATH itself (/cgi-bin/....//etc/passwd is a scanner artefact,
  * not an endpoint), but in a query value they mark a real LFI param
  * to keep (handled by BB_S omission + clean_query value-blanking).
  * Traversal (.. %2e%2e %5c) is already covered above. */
 "etc/passwd","etc%2fpasswd","/etc/shadow","boot.ini","win.ini",
 /* a scheme with its '://' percent-decoded-then-stripped of the '%' :
  * https%3A%2F%2F -> https3a2f2f (embedded comment-spam URL mangled into
  * the path: /https3A2F2Fpines-lf.click/archives/145). The 6-byte
  * 3a2f2f (":" "/" "/") never occurs in a real path. */
 "3a2f2f",0};
/* ---- Aho-Corasick: ALL markers matched in ONE O(len) pass per line,
 * regardless of pattern count. out bit0=BOTH (path&query), bit1=PATH-only.
 * Built once at startup; haystack lowercased on the fly during scan. ---- */
#define ACK 256
static int (*acg)[ACK];          /* DFA goto: acg[state][byte] */
static unsigned char *aco;       /* out mask per state */
static int ac_n,ac_cap;
static int ac_node(void){
    if(ac_n==ac_cap){ int nc=ac_cap?ac_cap*2:512;
        acg=realloc(acg,(size_t)nc*sizeof *acg);
        aco=realloc(aco,(size_t)nc);
        if(!acg||!aco){perror("realloc");exit(1);} ac_cap=nc; }
    for(int c=0;c<ACK;c++) acg[ac_n][c]=-1;
    aco[ac_n]=0; return ac_n++; }
static void ac_add(const char*s,unsigned char cls){
    int u=0;
    for(const unsigned char*p=(const unsigned char*)s;*p;p++){
        unsigned char c=(unsigned char)tolower(*p);
        if(acg[u][c]<0) acg[u][c]=ac_node();
        u=acg[u][c]; }
    aco[u]|=cls; }
static void garbage_init(void){
    ac_n=0; ac_node();                              /* root = state 0 */
    for(int i=0;BB_S[i];i++) ac_add(BB_S[i],1);     /* BOTH  */
    for(int i=0;BP_S[i];i++) ac_add(BP_S[i],2);     /* PATH-only */
    int *q=malloc((size_t)ac_n*sizeof(int)),*acf=calloc(ac_n,sizeof(int));
    int qh=0,qt=0;
    for(int c=0;c<ACK;c++){ int v=acg[0][c];
        if(v<0) acg[0][c]=0; else { acf[v]=0; q[qt++]=v; } }
    while(qh<qt){ int u=q[qh++];
        for(int c=0;c<ACK;c++){ int v=acg[u][c];
            if(v<0) acg[u][c]=acg[acf[u]][c];
            else { acf[v]=acg[acf[u]][c]; aco[v]|=aco[acf[v]]; q[qt++]=v; } } }
    free(q); free(acf); }
static int ac_scan(const char*s,size_t n,unsigned char mask){
    int st=0;
    for(size_t i=0;i<n;i++){
        st=acg[st][LO[(unsigned char)s[i]]];
        if(aco[st]&mask) return 1; }
    return 0; }
/* fuzzer / crawler-recursion artefact: a short chunk (2..24 bytes)
 * repeated >=4 times back-to-back inside the path, e.g. a scanner that
 * kept appending a form value -> /t/fit.txt5-Excellent5-Excellent5-...
 * Real paths never carry such runaway periodicity. Bounded O(24*n);
 * only engages on long paths so normal URLs pay almost nothing. */
static int repeat_junk(const char*s,size_t n){
    if(n<40) return 0;
    /* Fast reject (sound): a qualifying repeat always contains a run of >=18
     * consecutive positions k with s[k]==s[k+L] for one period L, and any run
     * that long fully covers an 8-aligned window. So if no 8-aligned 8-byte
     * word equals its +L shift for any L, no qualifying repeat can exist and
     * the precise scan below would return 0 - we skip straight to it. This
     * only ever skips when the answer is 0, so the result is identical. */
    int maybe=0;
    for(size_t L=2;L<=24&&2*L<=n&&!maybe;L++)
        for(size_t k=0;k+L+8<=n;k+=8){
            unsigned long a,b; memcpy(&a,s+k,8); memcpy(&b,s+k+L,8);
            if(a==b){ maybe=1; break; }
        }
    if(!maybe) return 0;
    for(size_t L=2;L<=24;L++){
        for(size_t i=0;i+2*L<=n;){
            if(s[i]==s[i+L]&&!memcmp(s+i+1,s+i+1+L,L-1)){
                size_t r=2,j=i+2*L;
                while(j+L<=n&&s[i]==s[j]&&!memcmp(s+i+1,s+j+1,L-1)){ r++; j+=L; }
                if(r>=4&&r*L>=24) return 1;     /* >=4 reps, >=24B span */
                i=j;
            } else i++;
        } }
    return 0; }
/* non-executable static extensions: a real file the server returns as-is,
 * never an endpoint that processes a query string. NOT included on purpose:
 * js/json/xml/php/asp/aspx/jsp/cgi/pl/py/do/html (JSONP / API / templating
 * / attack surface keep their query). Source/archive findings (sql bak zip
 * tar gz phps pdf ...) ARE here: the file is the finding, its query is
 * always scanner noise -> keep file, drop query. */
static const char*SX[]={"css","png","jpg","jpeg","gif","svg","ico","bmp",
 "webp","tif","tiff","woff","woff2","ttf","eot","otf","mp4","mp3","avi","mov",
 "webm","wav","ogg","m4a","flac","mkv","map","txt","csv","md","ini","log",
 "conf","yaml","yml","pdf","doc","docx","xls","xlsx","ppt","pptx","zip","rar",
 "gz","tar","7z","bz2","sql","bak","old","swp","phps",0};
/* classify the LAST path segment by its static extension:
 *  0 = not a static file (treat normally, query preserved)
 *  1 = clean terminal static file  (real file: keep, but its query is
 *      always scanner noise -> caller ignores the query)
 *  2 = static ext + glued junk     (mangled, e.g. fit.txt5%5E1918 ->
 *      not a real file -> garbage)                                      */
static int tail_static(const char*seg,size_t sl){
    int mangled=0;
    for(size_t p=0;p<sl;p++){
        if(seg[p]!='.')continue;
        size_t t=p+1;                               /* [a-z0-9] ext run  */
        while(t<sl){ char d=(char)tolower((unsigned char)seg[t]);
            if((d>='a'&&d<='z')||(d>='0'&&d<='9'))t++; else break; }
        size_t tl=t-(p+1); if(!tl){ continue; }
        const char*tk=seg+p+1; int exact=0,pref=0;  /* (no buffer: any len) */
        for(int x=0;SX[x]&&!exact;x++){ size_t el=strlen(SX[x]);
            if(el>tl) continue;
            size_t k=0; for(;k<el;k++)
                if((char)tolower((unsigned char)tk[k])!=SX[x][k])break;
            if(k==el){ if(el==tl) exact=1; else pref=1; } }
        if(exact){
            if(t==sl)        return 1;              /* clean terminal    */
            if(seg[t]=='.'){ p=t-1; continue; }     /* compound .tar.gz  */
            mangled=1;                              /* ext + glue (%- )  */
        } else if(pref) mangled=1;                  /* .txt2Junk / .txtFoo */
        p=t-1;
    }
    return mangled?2:0; }
/* ---- structural endpoint-validity helpers (v7) ---------------------- *
 * Every check below has a precise signature and was verified line-by-line
 * against the real surface so it removes scanner garbage WITHOUT deleting
 * a single genuine endpoint / finding. */

/* public-suffix labels seen as comment-spam targets glued into a path
 * (guestbook SEO spam) - deliberately conservative: short ambiguous TLDs
 * (id in me io ..) are excluded so REST tokens like /article.id survive */
static const char*TLDS[]={"com","net","org","info","biz","co","us","ee",
 "ly","ye","tr","ru","ua","uk",0};
/* is_tld/is_pub_sfx are the hottest predicates (~200k calls): per call the
 * old code re-ran strlen+tolower over every list entry. Membership is now a
 * single integer compare against packed lowercase keys built once in
 * garbage_init(). pack_lc() lowercases via LO[] (== tolower in C locale), and
 * the length guards match the max entry length, so the result is identical. */
static unsigned long pack_lc(const char*s,size_t n){
    unsigned long k=0; for(size_t i=0;i<n;i++) k=(k<<8)|LO[(unsigned char)s[i]];
    return k; }
static unsigned long TLDS_K[16]; static int TLDS_NK;
static unsigned long HSFX_K[64]; static int HSFX_NK;
static int is_tld(const char*s,size_t n){
    if(n<2||n>4)return 0;                  /* longest TLDS entry is "info" */
    unsigned long key=pack_lc(s,n);
    for(int i=0;i<TLDS_NK;i++) if(TLDS_K[i]==key)return 1;
    return 0; }
/* Extra public suffixes recognised ONLY at host level (host_embeds_domain).
 * Kept OUT of TLDS because is_tld() is also run on PATH segments by
 * glued_tld()/embedded_domain(), where short 2-letter suffixes prefix
 * legit file words (`in`->index.html, `io`->iock) and would eat real
 * findings. As an *interior* host label sitting before the true apex
 * (freebitco`.in.`vulnweb.com) the false-positive surface is ~nil. */
static const char*HSFX[]={"in","io","id","me","de","cn","tv","cc","ai",
 "sh","gg","ws","bz","pe","to","nu","la","fm","im","st","ca","es","fr",
 "it","nl","pl","se","ch","at","dk","cz","jp","kr","br","mx","za","au",
 "nz","sg","hk","tw","th","vn","ph","my","ae","sa","il","ir","pk","ng",
 "xyz","top","icu","vip","cyou","sbs","online","site","shop","app",0};
static int is_pub_sfx(const char*s,size_t n){
    if(is_tld(s,n))return 1;
    if(n<2||n>6)return 0;                  /* longest HSFX entry is "online" */
    unsigned long key=pack_lc(s,n);
    for(int i=0;i<HSFX_NK;i++) if(HSFX_K[i]==key)return 1;
    return 0; }
static void tld_init(void){
    TLDS_NK=0; for(int i=0;TLDS[i];i++) TLDS_K[TLDS_NK++]=pack_lc(TLDS[i],strlen(TLDS[i]));
    HSFX_NK=0; for(int i=0;HSFX[i];i++) HSFX_K[HSFX_NK++]=pack_lc(HSFX[i],strlen(HSFX[i])); }
/* a path SEGMENT that is itself a registrable domain (name.tld[/..]) -
 * /babymagazinetoday.com  /bit.ly/2TN5d3Y  /alraziuni.edu.ye  - never a
 * real path on the target, always crawled comment-spam */
static int embedded_domain(const char*s,size_t n){
    if(n<4)return 0;
    size_t ld=n; for(size_t i=0;i<n;i++) if(s[i]=='.')ld=i;
    if(ld==n||ld+1>=n)return 0;                 /* no dot / trailing dot */
    if(!is_tld(s+ld+1,n-ld-1))return 0;
    size_t fl=0; while(fl<ld&&s[fl]!='.')fl++;
    if(!fl)return 0;                             /* must have a 1st label */
    for(size_t i=0;i<ld;i++){ unsigned char c=s[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')
             ||c=='-'||c=='.'))return 0; }
    return 1; }
/* a TLD label glued (no dot separator) to more alnum = invalid host /
 * embedded-host artefact: testasp.vulnweb.comtestasp.. (host),
 * /Hosttestphp.vulnweb.comServer  /testphp.vulnweb.com5 (path).
 * Legit subdomains keep a dot AFTER the TLD (bing.com.vulnweb.com ->
 * '.com.' is fine) so they are untouched. Only a label that DIRECTLY
 * FOLLOWS a '.' is tested, so a bare first label (/comments = com+ments,
 * /commerce) is never falsely flagged. v7: previous version did `i=le`
 * after a label which, with the loop's i++, skipped the terminating dot
 * and thus never tested every *alternate* label (testasp.vulnweb.com +
 * testasp.vulnweb.com -> the glued `comtestasp` label was never seen).
 * Now iterates label-by-label so every dotted label is checked. */
static int glued_tld(const char*s,size_t n){
    size_t i=0;
    while(i<n){
        size_t ls=i;                              /* label [ls,i)         */
        while(i<n){ char c=s[i];
            if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')
               ||c=='-')i++; else break; }
        size_t ll=i-ls;
        if(ll && ls>0 && s[ls-1]=='.' && !is_tld(s+ls,ll)){
            for(int t=0;TLDS[t];t++){ size_t l=strlen(TLDS[t]);
                if(l>=ll)continue;                 /* label must be LONGER */
                size_t k=0; for(;k<l;k++)
                    if((char)tolower((unsigned char)s[ls+k])!=TLDS[t][k])break;
                if(k<l)continue;
                unsigned char c=s[ls+l];           /* tld glued to alnum  */
                if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9'))
                    return 1;
            }
        }
        if(i<n) i++;                               /* step over separator */
    }
    return 0; }
/* a host whose SUBDOMAIN portion embeds a registrable comment-spam domain
 * glued before the real wildcard apex - bing.com.vulnweb.com,
 * www.bing.com.vulnweb.com, www.hotelresidenceitalia.com.vulnweb.com,
 * blogger.com.vulnweb.com, freebitco.in.vulnweb.com. vulnweb.com is
 * wildcard DNS so *anything*.vulnweb.com hits the one box; a crawler that
 * re-rooted an off-site link under the wildcard produces
 * <name>.<sfx>.vulnweb.com - host-header confusion, never a distinct
 * target. The TRUE apex is the last 2 labels; the embedded domain is
 * unmistakable ONLY in the exact shape  [www. ...]  NAME . PUBSFX . APEX :
 *   - the public suffix sits EXACTLY at index k-3 (the single label right
 *     before the apex pair); a real corporate label that merely *contains*
 *     a ccTLD deeper in the tree (www.`en`.xect.example.com -> en is at
 *     index 1, k-3=2, NOT a suffix at k-3) is therefore untouched;
 *   - NAME (index k-4) is a real registrable second-level name: letters/
 *     hyphen, NO digit. Auto-generated corporate host labels carry digits
 *     (the corpus's qko07.info.example.com, q1837.sftkto.example.com,
 *     o06.wieyxo.example.com) and are REAL distinct surface - kept;
 *   - every deeper prefix label is exactly "www" (re-rooted SEO-spam is
 *     www.<embedded-domain>.<apex>; a genuine multi-level subdomain such
 *     as zfqyyxa.en.xect.example.com must never be dropped).
 * Still drops every validated vulnweb host (bing/blogger/freebitco/
 * hotelresidenceitalia .*.vulnweb.com); their create.sql / htaccess.conf
 * / database_connect.php survive on the real testphp/testasp box
 * (wildcard => identical machine) so 0 vulnweb surface lost. v12: the old
 * "any interior pub-suffix label" rule destroyed legitimate deep
 * corporate DNS (www.en.xect.example.com = 204 lines/the .woa app,
 * qko07.info.example.com = 116 lines/8 store .woa) - a non-destructive
 * violation - now fixed precisely. testphp.vulnweb.com (k=3),
 * test.php.vulnweb.com / onair.testphp.vulnweb.com (no suffix at k-3),
 * www.acunetix.com (k=3) remain untouched. */
static int host_embeds_domain(const char*h,size_t n){
    size_t ls[40]; size_t ll[40]; int k=0;          /* label starts/lens   */
    size_t s=0;
    for(size_t i=0;i<=n;i++){
        if(i==n||h[i]=='.'){
            if(k<40){ ls[k]=s; ll[k]=i-s; k++; }
            s=i+1;
        }
    }
    if(k<4) return 0;                               /* < name.sfx.apex     */
    int p=k-3;                                      /* label before apex   */
    if(!ll[p] || !is_pub_sfx(h+ls[p],ll[p]) || !ll[p-1]) return 0;
    {   const char*e=h+ls[p-1]; size_t el=ll[p-1]; int alpha=0;
        for(size_t j=0;j<el;j++){ unsigned char c=e[j];
            if(c>='0'&&c<='9') return 0;            /* digit => server host */
            if(((c|32)>='a'&&(c|32)<='z')) alpha=1; }
        if(!alpha) return 0; }                      /* must be a real name  */
    for(int i=0;i<p-1;i++)                          /* deeper prefix = www  */
        if(!(ll[i]==3 && (h[ls[i]]|32)=='w' && (h[ls[i]+1]|32)=='w'
             && (h[ls[i]+2]|32)=='w')) return 0;
    return 1; }
/* a "shallow" request: empty, '/', or '/'+one root-admin/crawler file
 * (robots.txt, favicon.ico, sitemap.xml, humans.txt, security.txt,
 * ads.txt, crossdomain.xml, clientaccesspolicy.xml, apple-touch-icon*,
 * a passive index.*), no query, no fragment. Combined with
 * host_embeds_domain() at the call site so the genuinely-ambiguous
 * [www.]NAME.PUBSFX.APEX shape is dropped ONLY at a shallow root - the
 * locally-observable signature of re-rooted crawler spam under a
 * wildcard apex. A legit corporate subdomain (qeif.tv.example.com with
 * /auth/v1/qr/validate) carries a deep application route and is kept.
 * `p` is the suffix from end-of-host through fragment: it starts with
 * '/' or '?' or '#' or is empty. */
static int is_shallow_path(const char*p, size_t n){
    if(!n) return 1;                                /* "" - bare host       */
    const char*hash=memchr(p,'#',n);
    size_t lim = hash ? (size_t)(hash-p) : n;
    if(memchr(p,'?',lim)) return 0;                 /* a query => deep      */
    if(hash) return 0;                              /* a # => treat as deep */
    if(lim==1 && p[0]=='/') return 1;               /* "/"                  */
    if(p[0]!='/') return 0;                         /* malformed; let pass  */
    size_t e=lim; while(e>1 && p[e-1]=='/') e--;    /* one trailing slash   */
    for(size_t i=1;i<e;i++) if(p[i]=='/') return 0; /* only ONE segment     */
    size_t sl=e-1; const char*sg=p+1;
    if(!sl) return 1;
    static const char*R[]={
        "robots.txt","favicon.ico","sitemap.xml","sitemap_index.xml",
        "humans.txt","security.txt","ads.txt","crossdomain.xml",
        "clientaccesspolicy.xml","apple-touch-icon.png",
        "apple-touch-icon-precomposed.png",
        "index.html","index.htm","index.php","index.asp","index.aspx",
        "index.jsp","default.html","default.htm","default.asp",
        "default.aspx",0};
    for(int i=0;R[i];i++){
        size_t rl=strlen(R[i]); if(rl!=sl) continue;
        size_t k=0; for(;k<sl;k++)
            if((char)tolower((unsigned char)sg[k])!=R[i][k]) break;
        if(k==sl) return 1;
    }
    return 0; }
/* a [HH:MM:SS] / [H:MM:SS] terminal-log / progress-bar clock glued to a
 * captured URL (search.php?test=query[14:13:39 , .../1[09:41:02]) - the
 * tool's stdout timestamp, never part of the real endpoint. Bracket +
 * 1-2 digits + ':' DD ':' DD is unambiguous scanner-capture noise. */
static int clock_frag(const char*s,size_t n){
    for(size_t i=0;i+1<n;i++){
        if(s[i]!='[')continue;
        size_t j=i+1,d=0;
        while(j<n&&d<2&&s[j]>='0'&&s[j]<='9'){ j++; d++; }
        if(!d||j+6>n)continue;
        if(s[j]==':'&&s[j+1]>='0'&&s[j+1]<='9'&&s[j+2]>='0'&&s[j+2]<='9'
           &&s[j+3]==':'&&s[j+4]>='0'&&s[j+4]<='9'&&s[j+5]>='0'&&s[j+5]<='9')
            return 1; }
    return 0; }
/* a path SEGMENT byte-identical to the one IMMEDIATELY following it
 * (categories.php/categories.php , login.php/login.php , .../images/
 * images/images/buy.php) is runaway crawler self-recursion - a relative
 * link the spider re-appended at each depth, never a real endpoint. The
 * non-recursed endpoint (/categories.php , /login.php , /shop/details/
 * <p>/<n>/buy.php , single /images) always survives. Intra-line, O(n),
 * NO buffering - distinct from the cross-line prefix-walk class udud
 * deliberately leaves alone. Len>=2 so an innocuous /a/a/ is spared. */
static int repeat_seg(const char*s,size_t n){
    size_t i=0;
    while(i<n){
        while(i<n&&s[i]=='/')i++;
        size_t a=i; while(i<n&&s[i]!='/')i++; size_t al=i-a;
        if(al<2) continue;
        size_t j=i; while(j<n&&s[j]=='/')j++;
        size_t b=j; while(j<n&&s[j]!='/')j++; size_t bl=j-b;
        if(al==bl&&!memcmp(s+a,s+b,al)) return 1;
    }
    return 0; }
/* a query PARAMETER NAME (bytes before '=') containing '%' is a scanner-
 * mangled delimiter / double-encoding artefact, never a real param name:
 *   /?%3Fid=1   /?%25id%25=&user=2   artists.php?artist%20=1
 *   hpp/params.php?aaaa%2f=&p=%25 . A '+' (encoded space) in the name is
 *   the same mutation (artists.php?+artist=). A SHORT (<=8) valueless
 * token that is pure percent-encoding (/?%2F) or pure punctuation with
 * no alphanumeric at all (listproducts.php?-) is a mangled junk query.
 * Real param names are identifiers, so
 * RetURL=.. file=.. r=https%3A%2F.. input=http.. (the '%' is in the
 * VALUE) and the Acunetix autodiscover SSRF (valueless token is long &
 * carries literal foo.com - not a short pure-% blob) are all preserved.
 * The clean endpoint (params.php?p=&pp= , /?id= , the real /) survives. */
static int bad_qname(const char*q,size_t n){
    size_t i=0;
    while(i<n){
        size_t s=i; while(i<n&&q[i]!='&')i++;     /* token [s,i)          */
        size_t tl=i-s; const char*t=q+s;
        if(tl){
            size_t eq=0; while(eq<tl&&t[eq]!='=')eq++;
            if(eq<tl){                             /* has '=' : check name */
                if(memchr(t,'%',eq)||memchr(t,'+',eq)) return 1;
                if(eq){ int an=0;
                    for(size_t z=0;z<eq;z++){ char c=t[z];
                        if((c>='0'&&c<='9')||(c>='A'&&c<='Z')
                           ||(c>='a'&&c<='z')||c=='_'){ an=1; break; } }
                    if(!an) return 1; }            /* ?-= ?.= name no alnum
                                       (_ kept: jQuery ?_= cache-bust) */
            } else {                               /* valueless token      */
                if(tl<=8){
                    int pct=0,hexok=1,alnum=0;
                    for(size_t z=0;z<tl;z++){ char c=t[z];
                        if((c>='0'&&c<='9')||(c>='A'&&c<='Z')
                           ||(c>='a'&&c<='z'))alnum=1;
                        if(c=='%')pct=1;
                        else if(!((c>='0'&&c<='9')||(c>='A'&&c<='F')
                                  ||(c>='a'&&c<='f')))hexok=0; }
                    if(pct&&hexok) return 1;        /* %2F pure-% blob      */
                    if(!alnum)     return 1;        /* ?- pure punctuation  */
                }
            }
        }
        if(i<n&&q[i]=='&')i++;
    }
    return 0; }
/* terminal .dtd/.xsd = an XML DTD / schema URL scraped from a <!DOCTYPE>
 * or xsi:schemaLocation (the only one in real data is the canonical W3C
 * www.w3.org/TR/html4/loose.dtd) - never an HTTP attack-surface endpoint.
 * .xsl is deliberately NOT here: it is real surface (Acunetix XXE
 * acunetix_xsl_inclusion_test.xsl). */
static int schema_scrape(const char*seg,size_t sl){
    size_t ld=sl; for(size_t i=0;i<sl;i++) if(seg[i]=='.')ld=i;
    if(ld==sl)return 0; const char*t=seg+ld+1; size_t tl=sl-ld-1;
    if(tl!=3)return 0;
    if((t[0]|32)=='d'&&(t[1]|32)=='t'&&(t[2]|32)=='d')return 1;
    if((t[0]|32)=='x'&&(t[1]|32)=='s'&&(t[2]|32)=='d')return 1;
    return 0; }

/* known file extensions (real, kept). Short (<=3) reals MUST all be here
 * so trunc_ext() does not flag them; longer targets are here so a 1-3
 * char truncation can be matched as their strict prefix. */
static const char*KX[]={
 "php","asp","aspx","asmx","jsp","jspx","html","htm","js","json","xml",
 "xsl","xsd","dtd","swf","fla","cgi","pl","py","do","dwt","class","iml",
 "exe","dll","dat","rs","name","tn","css","png","jpg","jpeg","gif","svg",
 "ico","bmp","webp","tif","tiff","woff","woff2","ttf","eot","otf","mp4",
 "mp3","avi","mov","webm","wav","ogg","m4a","flac","mkv","map","txt","csv",
 "md","ini","log","conf","yaml","yml","pdf","doc","docx","xls","xlsx",
 "ppt","pptx","zip","rar","gz","tar","7z","bz2","sql","bak","old","swp",
 "phps",0};
static int is_known_ext(const char*s,size_t n){
    for(int i=0;KX[i];i++){ size_t l=strlen(KX[i]); if(l!=n)continue;
        size_t k=0; for(;k<n;k++)
            if((char)tolower((unsigned char)s[k])!=KX[i][k])break;
        if(k==n)return 1; }
    return 0; }
/* terminal segment whose tail after the FINAL '.' is empty (trailing dot:
 * /login.  /admin.  /search.php.) or a 1-3 char strict PREFIX of a known
 * extension but not itself one (scanner stopped mid-typing the name:
 * listproducts.ph  crossdomain.x  admin/create.s  07JxLCs2OM.as).
 * Real short exts (js xml php sql ..) are in KX so they are NOT flagged. */
static int trunc_ext(const char*seg,size_t sl){
    size_t ld=sl; for(size_t i=0;i<sl;i++) if(seg[i]=='.')ld=i;
    if(ld==sl)return 0;
    size_t tl=sl-ld-1; const char*tk=seg+ld+1;
    if(tl==0)return 1;                           /* trailing dot */
    if(tl>3)return 0;
    if(is_known_ext(tk,tl))return 0;
    for(int i=0;KX[i];i++){ size_t l=strlen(KX[i]);
        if(l<=tl)continue;
        size_t k=0; for(;k<tl;k++)
            if((char)tolower((unsigned char)tk[k])!=KX[i][k])break;
        if(k==tl)return 1; }                      /* strict prefix */
    return 0; }
/* WVS/Acunetix enumeration tag as the terminal segment: a truncated word
 * + "Show" (cShow phpinfShow Mod_RewrShow), the attack-id A0<d>
 * (/admin/A01 .htaccessA01 crossdomain.xmlA04), or <digits><Upper>
 * (/3L /11H /1I) - none ever a real endpoint. */
static int wvs_tag(const char*seg,size_t sl){
    if(sl>=5){ int dot=0; for(size_t i=0;i<sl;i++) if(seg[i]=='.'){dot=1;break;}
        if(!dot && seg[sl-4]=='S'&&seg[sl-3]=='h'&&seg[sl-2]=='o'
           &&seg[sl-1]=='w'){ char b=seg[sl-5];
            if(b>='a'&&b<='z')return 1; } }
    if(sl>=3){ char a=seg[sl-3],b=seg[sl-2],c=seg[sl-1];
        if((a=='A'||a=='a')&&b=='0'&&c>='0'&&c<='9')return 1; } /* ..A0d  */
    if(sl>=2&&sl<=4){ size_t i=0;
        while(i<sl-1&&seg[i]>='0'&&seg[i]<='9')i++;
        if(i>0&&i==sl-1&&seg[sl-1]>='A'&&seg[sl-1]<='Z')return 1; }
    return 0; }
/* terminal segment that is a leaked HTTP header line (/Connection:
 * /Content-Type:  .../3/Connection:) or a file:line stack-trace
 * (sessvars.js:202  login.php:8080) - not a URL */
static int header_frag(const char*seg,size_t sl){
    if(!sl)return 0;
    if(seg[sl-1]==':')return 1;
    static const char*EC[]={".js:",".css:",".php:",".asp:",".aspx:",
     ".html:",".htm:",".json:",".xml:",".jsp:",0};
    for(int i=0;EC[i];i++){ size_t m=strlen(EC[i]);
        for(size_t p=0;p+m<=sl;p++){ size_t k=0;
            for(;k<m;k++)
                if((char)tolower((unsigned char)seg[p+k])!=EC[i][k])break;
            if(k==m)return 1; } }
    return 0; }
/* mangled SCRIPT extension: .php/.asp/.jsp/.htm/.cgi/.pl followed by
 * anything other than the genuine compound tail. Replaces the old
 * char-whitelist (which let .php8 .php12 .php%E3 through). Spares
 * .aspx .aspnet .html .php5 .phps .jspx and real source compounds
 * (.php.bak) while killing .php<digit> .asp%5B .php--user-data-dir
 * .php.9.2.5 etc. */
static int mangled_script(const char*seg,size_t sl){
    static const char*M[]={".php",".asp",".jsp",".htm",".cgi",".pl",0};
    static const size_t MN[]={4,4,4,4,4,3};
    for(int m=0;M[m];m++){ size_t ml=MN[m];
        for(size_t i=0;i+ml<=sl;i++){
            size_t k=0; for(;k<ml;k++)
                if((char)tolower((unsigned char)seg[i+k])!=M[m][k])break;
            if(k<ml)continue;
            size_t j0=i+ml;
            if(j0==sl)continue;                  /* clean .php terminal   */
            char c0=(char)tolower((unsigned char)seg[j0]);
            if(c0=='.'){                          /* compound: foo.php.X  */
                int onlynum=1;
                for(size_t z=j0+1;z<sl;z++){ char d=seg[z];
                    if(!((d>='0'&&d<='9')||d=='.')){ onlynum=0; break; } }
                if(onlynum&&j0+1<sl)return 1;      /* .php.9.2.5 mangled   */
                continue;                          /* .php.bak = finding   */
            }
            size_t rl=sl-j0; const char*r=seg+j0;
            const char*ok=NULL;                    /* genuine compound tail */
            if(M[m][1]=='p'&&M[m][2]=='h')      ok="s5";   /* .phps .php5  */
            else if(M[m][1]=='a')               ok="x";    /* .aspx        */
            else if(M[m][1]=='h')               ok="l";    /* .htm->.html  */
            else if(M[m][1]=='j')               ok="x";    /* .jspx        */
            int good=0;
            if(M[m][1]=='a'&&rl==3){ if(!strncasecmp(r,"net",3))good=1; }
            if(!good&&ok&&rl==1)
                for(const char*z=ok;*z;z++)
                    if((char)tolower((unsigned char)r[0])==*z)good=1;
            if(!good)return 1;                    /* mangled               */
        } }
    return 0; }

/* fast path: cheap O(n) byte scan first, then ONE Aho-Corasick pass over
 * path (BOTH|PATH markers) and query (BOTH markers only). */
static int is_garbage(const char*pp,size_t pn,const char*q,size_t qn){
    /* is_garbage is a pure OR of side-effect-free predicates, so call order
     * never changes the result, only the cost. Cheapest + highest-hit checks
     * run first; the expensive whole-path scans (repeat_seg, repeat_junk) run
     * LAST so a line caught by anything cheaper never pays for them. */
    if(bad_bytes(pp,pn,0))return 1;
    if(q&&qn&&q[0]==',')return 1;          /* query cannot start with ',' */
    if(q&&qn&&bad_bytes(q,qn,1))return 1;
    if(ac_scan(pp,pn,3))return 1;          /* path: BOTH|PATH (main catcher) */
    if(q&&qn&&ac_scan(q,qn,1))return 1;    /* query: BOTH only */
    if(clock_frag(pp,pn))return 1;         /* path[14:13:39 capture noise */
    if(q&&qn&&clock_frag(q,qn))return 1;   /* ?test=query[14:13:39        */
    if(q&&qn&&bad_qname(q,qn))return 1;    /* ?%3Fid= ?%2F artist%20=     */
    /* ---- v7 structural gates ---- */
    /* FIRST path segment that is an embedded domain / glued host:
     * /babymagazinetoday.com  /bit.ly/x  /Hosttestphp.vulnweb.comServer */
    { size_t i=0; while(i<pn&&pp[i]=='/')i++;
      size_t s=i; while(i<pn&&pp[i]!='/')i++;
      if(i>s&&(embedded_domain(pp+s,i-s)||glued_tld(pp+s,i-s)))return 1; }
    /* TERMINAL path segment checks (the actual endpoint name) */
    { size_t e=pn; while(e&&pp[e-1]=='/')e--;
      size_t s=e; while(s&&pp[s-1]!='/')s--;
      const char*seg=pp+s; size_t sl=e-s;
      if(sl){
        if(embedded_domain(seg,sl))  return 1;   /* deep /a/b/evil.com   */
        if(schema_scrape(seg,sl))    return 1;   /* loose.dtd .xsd scrape */
        /* NOTE: glued_tld() is deliberately NOT run on the terminal
         * segment - short TLDs (co/com/net/org) prefix legit file words
         * (htaccess.conf = co+nf, .command, .controller) and would eat
         * real source-disclosure findings. First-segment + host only. */
        if(mangled_script(seg,sl))   return 1;   /* .php8 .asp%5B .php.9 */
        if(tail_static(seg,sl)==2)   return 1;   /* fit.txt5%5E1918      */
        if(trunc_ext(seg,sl))        return 1;   /* listproducts.ph  login. */
        if(wvs_tag(seg,sl))          return 1;   /* cShow /A01 /3L /11H  */
        if(header_frag(seg,sl))      return 1;   /* /Connection:  .js:202 */
      }
    }
    /* whole-path scans LAST: most expensive, lowest marginal hit once the
     * cheap checks above have already removed the bulk of the junk. */
    if(repeat_seg(pp,pn))return 1;         /* foo/foo crawler self-recurse */
    if(repeat_junk(pp,pn))return 1;        /* runaway periodicity artefact */
    return 0; }

/* host validity: has a dot and last label is all-alpha length>=2 */
static int valid_host(const char*h,size_t n){
    if(!n)return 0; size_t dot=0,le=n;
    for(size_t i=0;i<n;i++) if(h[i]=='.'){dot=1; le=i;}
    if(!dot)return 0; size_t ls=0;
    for(size_t i=0;i<n;i++) if(h[i]=='.') ls=i+1;
    size_t ll=n-ls; if(ll<2)return 0;
    for(size_t i=ls;i<n;i++) if(!isalpha((unsigned char)h[i]))return 0;
    (void)le; return 1; }

/* strip leading percent-encoding artefact tokens (lowercased host) */
static const char*WB[]={"25252f","2528","253d","2532","252f","2f","2e","3a",
 "3d","23","3f","25","3b","40","5c","5c2f",0};
static void wb_clean(const char**hp,size_t*hn){
    const char*h=*hp; size_t n=*hn;
    for(int again=1;again&&n;){ again=0;
        for(int t=0;WB[t];t++){ size_t tl=strlen(WB[t]);
            if(n>tl && !strncmp(h,WB[t],tl) && valid_host(h+tl,n-tl)){
                h+=tl; n-=tl; again=1; break; } } }
    *hp=h; *hn=n; }

/* A query token with NO '=' carries no injectable/fuzzable surface, so a
 * 5-64 char pure-alnum valueless token is treated as a cache-buster /
 * scan-id and never creates distinct attack surface - this is exactly
 * what made ?wfyir4&r=.. ?WmVBjO&r=.. ?UABZWQ&r=.. collapse into hundreds
 * of bogus "unique" lines. Very short flags (<=4: ?v ?ssl ?raw ?gzip),
 * anything with a value (?id=1), and tokens containing _ - . survive.
 * A PURE-DIGIT valueless token (?9043 ?9065 .. on /t/xss.js) is a cache-
 * buster / scan-id at ANY length - it is never a meaningful flag (those
 * are words: ?rsd ?ssl), so it collapses the 30 xss.js?<n> value-variant
 * duplicates into the one real /t/xss.js. Disabled by -x (fully raw). */
static int g_nojunk=1;
static int is_junk_tok(const char*s,size_t n){
    if(n){ size_t d=0; for(;d<n;d++) if(s[d]<'0'||s[d]>'9')break;
        if(d==n)return 1; }                  /* ?9043 cache-bust id        */
    if(n<5||n>64)return 0;
    for(size_t i=0;i<n;i++){ unsigned char c=(unsigned char)s[i];
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')))return 0; }
    return 1; }
/* case-insensitive substring presence */
static int ci_find(const char*h,size_t n,const char*nd){
    size_t m=strlen(nd); if(!m||m>n)return 0;
    for(size_t i=0;i+m<=n;i++){ size_t k=0;
        while(k<m&&(char)tolower((unsigned char)h[i+k])==nd[k])k++;
        if(k==m)return 1; }
    return 0; }

/* a parameter VALUE that itself contains a URL ( ://  //  %2f%2f  or the
 * fully-encoded %3a%2f%2f ) means every '&' after this '=' belongs to the
 * NESTED url, not the outer query. query_keys() stops splitting there so
 * open-redirect / SSRF endpoints collapse to ONE signature instead of
 * leaking a distinct bogus key set per attacker spam target:
 *   redir.php?r=http://a.evil/x.php?view=1&task=2&id=3   ->  ?r
 *   redir.php?r=http://b.evil/y?option=com_k2&u=4        ->  ?r   (merge) */
static int val_has_url(const char*v,size_t n){
    if(n>=2&&v[0]=='/'&&v[1]=='/')return 1;
    for(size_t i=0;i+2<n;i++)
        if(v[i]==':'&&v[i+1]=='/'&&v[i+2]=='/')return 1;
    for(size_t i=0;i+5<n;i++)                 /* %2f%2f (ci); also inside */
        if(v[i]=='%'&&v[i+1]=='2'&&(v[i+2]|32)=='f'&&  /*  %3a%2f%2f      */
           v[i+3]=='%'&&v[i+4]=='2'&&(v[i+5]|32)=='f')return 1;
    return 0; }

/* A parameter VALUE that is some other tester's attack payload / fuzzer /
 * sqlmap-dump artefact - NOT a value the application itself expects. The
 * dedup signature already ignores values, so blanking such a value in the
 * emitted representative loses ZERO surface (param name + endpoint + the
 * dedup identity are untouched) while honouring "show the real URL, not
 * someone else's testing cache". A genuine redirect/SSRF target URL
 * ( ://  //  %2f%2f ) is a real value and is preserved verbatim. */
static const char*PAY[]={
 "union","select","sleep(","benchmark(","concat(","information_schema",
 "xp_cmdshell","waitfor","pg_sleep","extractvalue","updatexml","cast(",
 "having ","procedure analyse","dbms_pipe","utl_inaddr","load_file",
 "<script","javascript:","onerror","onload=","onmouseover","alert(",
 "prompt(","confirm(","fromcharcode","%3cscript","../","..%2f","..%5c",
 "%2e%2e","etc/passwd","etc%2fpasswd","/etc/shadow","boot.ini","win.ini",
 "${","#{","{{","<%","0x7","0x3","--","%23","/*","%2f%2a",
 /* encoded XSS-scheme & shell command-injection so the *value* is blanked
  * (param name + endpoint + dedup identity untouched): NewsAd=javascript%3A.,
  * ?cmd=part1%3D%26%26+ls+-la (&&  ||  ;  `  +ls / %20ls) */
 "javascript%3a","data%3atext","%26%26","%7c%7c","%60","%3bls",
 "+ls+","+ls%20","%20ls%20","%20ls+",0};
static int val_is_payload(const char*v,size_t n){
    if(!n) return 0;
    if(val_has_url(v,n)) return 0;            /* real redirect/SSRF target */
    if(n>96) return 1;                        /* no real value is this long */
    for(size_t i=0;i<n;i++){ unsigned char c=(unsigned char)v[i];
        if(c<0x20||c==0x7f||c=='<'||c=='>'||c=='"'||c=='\''||c=='`'||c=='\\')
            return 1;                         /* raw injection / control */
        if(c=='%'&&i+2<n){                    /* encoded ctrl/quote/angle */
            char a=(char)tolower((unsigned char)v[i+1]);
            char b=(char)tolower((unsigned char)v[i+2]);
            if(a=='0'||a=='1') return 1;              /* %00-%1f control  */
            if(a=='2'&&(b=='7'||b=='2')) return 1;    /* %27 '  %22 "     */
            if(a=='3'&&(b=='c'||b=='e')) return 1; }  /* %3c <  %3e >     */
    }
    if(memchr(v,'+',n)||ci_find(v,n,"%20")){  /* spaces = SQL/cmd payload */
        if(ci_find(v,n,"union")||ci_find(v,n,"select")||ci_find(v,n,"sleep")||
           ci_find(v,n,"benchmark")||ci_find(v,n,"and")||ci_find(v,n,"or"))
            return 1; }
    for(int i=0;PAY[i];i++) if(ci_find(v,n,PAY[i])) return 1;
    return 0; }

/* rebuild query preserving original order, dropping junk valueless tokens
 * and BLANKING any value that is another tester's payload (param name kept
 * -> surface & signature intact); returns length (0 => caller emits no ?) */
static size_t clean_query(const char*q,size_t n,char*out,size_t cap){
    size_t o=0,i=0; int first=1;
    while(i<n){ size_t ks=i;
        while(i<n&&q[i]!='='&&q[i]!='&')i++;
        size_t ke=i; int hadval=(i<n&&q[i]=='='); size_t te=ke;
        if(hadval){ te=i+1; while(te<n&&q[te]!='&')te++; }
        size_t kl=ke-ks;
        int drop=g_nojunk&&!hadval&&ke>ks&&is_junk_tok(q+ks,kl);
        if(!drop&&(kl||hadval)){
            if(!first&&o<cap)out[o++]='&';
            for(size_t z=0;z<kl&&o<cap;z++)out[o++]=q[ks+z];
            if(hadval){
                if(o<cap)out[o++]='=';
                size_t vs=ke+1, vl=te-vs;
                if(vl&&!val_is_payload(q+vs,vl))
                    for(size_t z=0;z<vl&&o<cap;z++)out[o++]=q[vs+z]; }
            first=0; }
        i=te; if(i<n&&q[i]=='&')i++; }
    return o; }

/* sorted unique query keys -> out, returns length */
static size_t query_keys(const char*q,size_t n,char*out,size_t cap){
    const char*ks[256]; size_t kl[256]; int kc=0; size_t i=0;
    while(i<n&&kc<256){ size_t s=i;
        while(i<n&&q[i]!='='&&q[i]!='&')i++;
        int hadval=(i<n&&q[i]=='=');
        if(i>s&&(hadval||!g_nojunk||!is_junk_tok(q+s,i-s))){
            ks[kc]=q+s; kl[kc]=i-s; kc++; }
        size_t vs=hadval?i+1:i;
        while(i<n&&q[i]!='&')i++;                 /* i -> value end */
        if(hadval&&val_has_url(q+vs,i-vs))break;   /* rest = nested url */
        if(i<n)i++; }
    for(int a=1;a<kc;a++){ const char*kp=ks[a]; size_t k=kl[a]; int b=a-1;
        while(b>=0){ size_t m=kl[b]<k?kl[b]:k; int c=memcmp(ks[b],kp,m);
            if(c>0||(c==0&&kl[b]>k)){ks[b+1]=ks[b];kl[b+1]=kl[b];b--;} else break; }
        ks[b+1]=kp; kl[b+1]=k; }
    size_t o=0; const char*pv=NULL; size_t pl=0;
    for(int a=0;a<kc;a++){ if(pv&&pl==kl[a]&&!memcmp(pv,ks[a],pl))continue;
        if(o&&o<cap)out[o++]='&';
        for(size_t z=0;z<kl[a]&&o<cap;z++)out[o++]=ks[a][z]; pv=ks[a]; pl=kl[a]; }
    return o; }

int main(int argc,char**argv){
    int F=1,S=0,K=0,P=0,W=1,C=1,V=0,X=0,FI=0,c; /* clean defaults: sanity gate +
                                       noise-filter + case-fold + wayback +
                                       canonical (X=0 means gate ENABLED).
                                       FI=0: object-ids PRESERVED (v18) */
    while((c=getopt(argc,argv,"fkpwcWrasxVF"))!=-1){
        if(c=='k')K=1; else if(c=='p')P=1;
        else if(c=='a')F=0;                          /* keep ALL assets */
        else if(c=='s')S=1;                          /* case-sensitive path */
        else if(c=='x')X=1;                          /* keep invalid URLs */
        else if(c=='F')FI=1;                         /* fold ids (N/U/H/stem) */
        else if(c=='W')W=0; else if(c=='r')C=0;      /* opt-outs */
        else if(c=='f'||c=='w'||c=='c'){ /* legacy no-ops (already default) */ }
        else if(c=='V')V=1;
        else { fprintf(stderr,
          "usage: udud [-F fold-ids][-x keep-invalid][-a keep-assets]"
          "[-s case-sensitive][-k][-p][-W][-r][-V]\n");
          return 2; } }
    lo_init();
    /* v15: 64 KB output stdio buffer so fwrite() amortises syscalls. Input
     * is read with our own block reader below - bypasses getline()'s per-
     * line buffer-growth check and the byte-by-byte stdio scan for '\n'. */
    static char io_out[1<<16];
    setvbuf(stdout, io_out, _IOFBF, sizeof io_out);
    if(X) g_nojunk=0;
    if(!X){ garbage_init(); tld_init(); }
    tab_init(1<<16);
    /* v15 line reader: read() into a 128 KB block buffer, memchr for '\n',
     * yield a (const char*, size_t) pair. A line that crosses the buffer
     * boundary is moved to the start with memmove and the next read()
     * extends it. Real-world recon URL lines are <2 KB; the buffer holds
     * thousands of them per syscall. */
    #define LBUF (1u<<17)
    static char rbuf[LBUF];
    size_t r_off=0, r_end=0;
    int in_eof=0;
    unsigned long long kept=0,total=0;
    /* MERGE = default keyset mode: collapse a path's query URLs into the
     * richest one (deferred emit). -k (full query) and -x (raw) keep the
     * per-distinct streaming behaviour untouched. */
    int MERGE=(!K&&!X);
    char sig[8192];
    /* v15: fast 3-byte "://" locator. memmem(...,"://",3) uses Two-Way
     * matching with setup cost amortised over longer needles; on a 3-byte
     * needle a direct memchr-for-':' + 2-byte compare is faster. */
    #define FIND_SCHEME_SEP(P,REM) ({                                       \
        const char*_p=(P); size_t _r=(REM); const char*_hit=NULL;           \
        while(_r>=3){ const char*_c=memchr(_p,':',_r-2);                    \
            if(!_c) break;                                                  \
            if(_c[1]=='/'&&_c[2]=='/'){ _hit=_c; break; }                   \
            size_t _adv=(size_t)(_c-_p)+1; _p+=_adv; _r-=_adv; }            \
        _hit; })

    for(;;){
        /* fetch next line from rbuf; return as (const char*, size_t) without
         * copying. line is NOT NUL-terminated; we always carry the length. */
        const char*line; size_t L;
        for(;;){
            if(r_off<r_end){
                char*nl=memchr(rbuf+r_off,'\n',r_end-r_off);
                if(nl){
                    line=rbuf+r_off; L=(size_t)(nl-(rbuf+r_off));
                    while(L&&(line[L-1]=='\r')) L--;
                    r_off=(size_t)(nl-rbuf)+1; goto have_line; } }
            if(in_eof){
                if(r_off<r_end){
                    line=rbuf+r_off; L=r_end-r_off;
                    while(L&&(line[L-1]=='\r')) L--;
                    r_off=r_end; goto have_line; }
                goto done; }
            /* shift remainder, refill */
            if(r_off){ memmove(rbuf,rbuf+r_off,r_end-r_off); r_end-=r_off; r_off=0; }
            if(r_end==LBUF){
                /* pathological: a single line longer than the buffer.
                 * Emit what we have (truncated) so the program does not
                 * loop forever; real URL lines never hit this. */
                line=rbuf; L=r_end;
                while(L&&(line[L-1]=='\r')) L--;
                r_off=r_end; goto have_line; }
            ssize_t got=read(0,rbuf+r_end,LBUF-r_end);
            if(got<0){ if(errno==EINTR) continue; in_eof=1; }
            else if(got==0) in_eof=1;
            else r_end+=(size_t)got;
        }
        have_line:
        if(!L) continue; total++;

        /* scheme + double-scheme repair */
        const char*p=line; size_t rem=L; const char*sch=p; size_t schl=0;
        const char*sep=FIND_SCHEME_SEP(p,rem);
        if(sep){ schl=sep-p; p=sep+3; rem=L-(p-line);
            for(int g=1;g;){ g=0;
                if(rem>=5&&!strncasecmp(p,"https",5)&&(p[5]==':'||p[5]=='/')){
                    const char*s2=FIND_SCHEME_SEP(p,rem);
                    if(s2){sch=p;schl=5;p=s2+3;rem=L-(p-line);g=1;}
                } else if(rem>=4&&!strncasecmp(p,"http",4)&&(p[4]==':'||p[4]=='/')){
                    const char*s2=FIND_SCHEME_SEP(p,rem);
                    if(s2){sch=p;schl=4;p=s2+3;rem=L-(p-line);g=1;} } } }

        /* authority */
        const char*host=p; size_t hl=0;
        while(hl<rem&&host[hl]!='/'&&host[hl]!='?'&&host[hl]!='#') hl++;
        const char*path=host+hl; size_t prem=rem-hl;

        /* host:port */
        size_t hostn=hl; const char*port=NULL; size_t portn=0;
        for(size_t i=hl;i-->0;){ if(host[i]==':'){hostn=i;port=host+i+1;portn=hl-i-1;break;}
            if(!isdigit((unsigned char)host[i]))break; }
        int defp=0;
        if(port&&all_digits(port,portn)){
            if(schl==4&&!strncasecmp(sch,"http",4)&&portn==2&&!memcmp(port,"80",2))defp=1;
            if(schl==5&&!strncasecmp(sch,"https",5)&&portn==3&&!memcmp(port,"443",3))defp=1; }

        /* invalid authority: empty/non-numeric port (host:/embedded/url ->
         * www.vulnweb.com:/www.amazon.com/..) or a TLD label glued to more
         * host (testasp.vulnweb.comtestasp.vulnweb.com) - never a real URL */
        if(!X&&port&&(portn==0||!all_digits(port,portn))) continue;
        if(!X&&glued_tld(host,hl)) continue;
        /* v14: ambiguous [www.]NAME.PUBSFX.APEX shape (wildcard spam vs
         * legit deep subdomain) - drop only at a shallow root. */
        if(!X&&host_embeds_domain(host,hostn)
             &&is_shallow_path(path,prem)) continue;

        /* path | query | fragment */
        const char*pp=path; size_t ppn=prem;
        const char*fr=memchr(pp,'#',ppn); if(fr)ppn=fr-pp;
        const char*q=memchr(pp,'?',ppn);
        size_t pathn=q?(size_t)(q-pp):ppn;
        const char*query=q?q+1:NULL; size_t queryn=q?ppn-pathn-1:0;

        if(!X&&is_garbage(pp,pathn,query,queryn)) continue;
        if(F&&is_noise(pp,pathn)) continue;

        /* a clean terminal static file (foo.txt, app.css, dump.sql) never
         * processes a query - any ?... on it is scanner noise, so collapse
         * every foo.txt?<x> into the one real foo.txt (-x keeps it raw) */
        int qok=1;
        if(!X){ size_t e=pathn; while(e&&pp[e-1]=='/')e--;
                size_t s=e; while(s&&pp[s-1]!='/')s--;
                if(tail_static(pp+s,e-s)==1) qok=0; }

        /* lowercased host buffer (+ optional wayback clean). v15: branch-free
         * ASCII fold compiles to a SIMD case-mask the LO-LUT indirection
         * could not. */
        char hb[1024]; size_t hbn=hostn<sizeof hb?hostn:sizeof hb-1;
        for(size_t i=0;i<hbn;i++){
            unsigned char c=(unsigned char)host[i];
            hb[i]=(char)(c|(((unsigned)c-'A'<26u)<<5)); }
        const char*H=hb; size_t HN=hbn; if(W) wb_clean(&H,&HN);

        /* v15: scheme lowered ONCE into schb[]; reused by both sig and out. */
        char schb[16]; size_t schbn=schl<sizeof schb?schl:sizeof schb;
        for(size_t i=0;i<schbn;i++){
            unsigned char c=(unsigned char)sch[i];
            schb[i]=(char)(c|(((unsigned)c-'A'<26u)<<5)); }

        /* ---- build dedup signature ---- */
        size_t o=0;
        #define PUT(S,N) do{size_t _n=(N); if(o+_n<sizeof sig){memcpy(sig+o,(S),_n);o+=_n;}}while(0)
        #define PUTC(X)  do{ if(o+1<sizeof sig) sig[o++]=(X);}while(0)
        PUT(schb,schbn);
        PUT("://",3); PUT(H,HN);
        if(port&&!defp){ PUTC(':'); PUT(port,portn); }
        /* path: collapse '//', drop trailing '/', drop dir-index file,
         * case-fold (unless -s), template numeric/uuid/hex segments.
         * v15: collect kept segment offsets in seg_s/seg_l so the canonical
         * output rebuilder does NOT re-walk the path string. */
        size_t seg_s[256], seg_l[256]; int segc=0;
        {
            PUTC('/'); size_t i=0; int any=0;
            /* find boundary of last non-empty segment so 'last' is decided
             * once per path (was O(seg) per segment in v14). */
            size_t last_end=pathn; while(last_end&&pp[last_end-1]=='/')last_end--;
            while(i<pathn){
                while(i<pathn&&pp[i]=='/')i++;            /* collapse slashes */
                if(i>=pathn)break;
                size_t s=i; while(i<pathn&&pp[i]!='/')i++;
                const char*sg=pp+s; size_t sl=i-s;
                int last=(i>=last_end);
                if(last&&is_index(sg,sl)) break;          /* /index.php -> / */
                if(any)PUTC('/'); any=1;
                size_t stl;
                /* parent (preceding) segment is a content/listing word */
                int pcs=(segc>0&&is_content_section(pp+seg_s[segc-1],seg_l[segc-1]));
                if(!P&&all_digits(sg,sl)&&(FI||pcs)) PUTC('N'); /* numeric id:
                       folds under a content parent (/cat/9 -> /cat/N) always,
                       elsewhere only with -F (so /api/users/123 is preserved) */
                else if(!P&&FI&&is_uuid(sg,sl))      PUTC('U');
                else if(!P&&FI&&sl>=12&&is_hex(sg,sl)) PUTC('H');
                else if(!P&&(is_slug(sg,sl,last)||
                        (last&&pcs&&is_text_slug(sg,sl,last))))
                                                     PUTC('S'); /* title slug */
                else if(!P&&FI&&((stl=id_stem(sg,sl))||
                             (stl=mixed_id_stem(sg,sl)))){ /* slug-<id> -> stem+mark */
                    size_t avail=sizeof sig>o?sizeof sig-1-o:0;
                    size_t n=stl<avail?stl:avail;
                    if(S) memcpy(sig+o,sg,n);
                    else for(size_t z=0;z<n;z++){
                        unsigned char c=(unsigned char)sg[z];
                        /* branch-free ASCII fold; gcc -O3 vectorises this */
                        sig[o+z]=(char)(c|(((unsigned)c-'A'<26u)<<5)); }
                    o+=n;
                    PUT("-#",2); }
                else {
                    size_t avail=sizeof sig>o?sizeof sig-1-o:0;
                    size_t n=sl<avail?sl:avail;
                    if(S) memcpy(sig+o,sg,n);
                    else for(size_t z=0;z<n;z++){
                        unsigned char c=(unsigned char)sg[z];
                        sig[o+z]=(char)(c|(((unsigned)c-'A'<26u)<<5)); }
                    o+=n;
                }
                if(segc<256){ seg_s[segc]=s; seg_l[segc]=sl; segc++; }
            }
        }
        /* is_qgroup: this URL carries a meaningful query keyset, so in MERGE
         * mode its group sig ends with a bare '?' (no keys) and pcount = the
         * number of distinct query keys decides who represents the group. */
        int is_qgroup=0; unsigned pcount=0;
        if(qok&&query&&queryn){
            if(K){ PUTC('?'); PUT(query,queryn); }
            else { char qk[4096]; size_t qn=query_keys(query,queryn,qk,sizeof qk);
                   if(qn){ PUTC('?');
                       if(MERGE){ is_qgroup=1; pcount=1;
                           for(size_t z=0;z<qn;z++) if(qk[z]=='&') pcount++; }
                       else PUT(qk,qn); } } }

        if(MERGE){
            int is_new=0; unsigned ri=tab_group(sig,o,&is_new);
            /* keep existing representative unless this is a strictly richer
             * query URL for the same group */
            if(!is_new && !(is_qgroup && pcount>recs[ri].pcount)) continue;
            /* materialise the representative output line into the arena */
            size_t off=arena_len;
            if(!C){ arena_put(line,L); arena_put("\n",1); }
            else {
                char out[8192]; size_t r=0;
                #define O(S,N) do{size_t _n=(N); if(r+_n<sizeof out){memcpy(out+r,(S),_n);r+=_n;}}while(0)
                #define OC(X)  do{ if(r+1<sizeof out) out[r++]=(X);}while(0)
                O(schb,schbn); O("://",3); O(H,HN);
                if(port&&!defp){ OC(':'); O(port,portn); }
                OC('/');
                for(int k=0;k<segc;k++){ if(k) OC('/'); O(pp+seg_s[k], seg_l[k]); }
                if(qok&&query&&queryn){ char cq[8192];
                    size_t cn=clean_query(query,queryn,cq,sizeof cq);
                    if(cn){ OC('?'); O(cq,cn); } }
                if(r<sizeof out) out[r++]='\n';
                arena_put(out,r);
                #undef O
                #undef OC
            }
            size_t olen=arena_len-off;
            if(is_new){ rec_push(off,(unsigned)olen,pcount); kept++; }
            else { recs[ri].off=off; recs[ri].len=(unsigned)olen; recs[ri].pcount=pcount; }
            continue;
        }

        if(!tab_add(sig,o)) continue;
        kept++;

        /* ---- emit (streaming: -k / -x) ---- */
        if(!C){
            char nl[1]; nl[0]='\n';
            fwrite(line,1,L,stdout); fwrite(nl,1,1,stdout);
        } else {
            char out[8192]; size_t r=0;
            #define O(S,N) do{size_t _n=(N); if(r+_n<sizeof out){memcpy(out+r,(S),_n);r+=_n;}}while(0)
            #define OC(X)  do{ if(r+1<sizeof out) out[r++]=(X);}while(0)
            O(schb,schbn);
            O("://",3); O(H,HN);
            if(port&&!defp){ OC(':'); O(port,portn); }
            OC('/');
            for(int k=0;k<segc;k++){
                if(k) OC('/');
                O(pp+seg_s[k], seg_l[k]);
            }
            if(qok&&query&&queryn){ char cq[8192];
                size_t cn=clean_query(query,queryn,cq,sizeof cq);
                if(cn){ OC('?'); O(cq,cn); } }
            /* v15: '\n' goes INTO the buffer; single fwrite per line. */
            if(r<sizeof out) out[r++]='\n';
            fwrite(out,1,r,stdout);
            #undef O
            #undef OC
        }
    }
    done:
    if(MERGE) for(size_t i=0;i<rec_cnt;i++)
        fwrite(arena+recs[i].off,1,recs[i].len,stdout);
    if(V){ struct rusage ru; getrusage(RUSAGE_SELF,&ru);
        fprintf(stderr,"udud: %llu -> %llu  (peak RSS %ld KB)\n",total,kept,ru.ru_maxrss); }
    return 0;
}
