/* xcull - fast URL structural de-duplicator (C, single-pass, stdin->stdout)
 *
 * Default: signature-keyed dedup with object-id PRESERVE (numeric / UUID /
 * hex IDs survive for IDOR / BOLA enumeration), distinct session tokens
 * survive, query merge is subset-only on the same templated path, bare
 * endpoints fold against decorated siblings, .map URLs are kept, render-
 * noise / scanner-probe / wayback-glue URLs are dropped, paths are
 * canonicalized case-insensitively, hosts are normalized.
 * Opt-outs: -F fold object-ids | -x keep invalid/scan-artefact URLs |
 *           -a keep all assets | -s case-sensitive path | -W keep raw
 *           hosts | -r raw first-seen | -k keep param values + every
 *           distinct key-set | -p no path templating | -v stats.
 *
 * Release notes: see CHANGELOG.md. Build: make (or cc -O3 -march=native
 * -flto -o xcull xcull.c). License: XSAL v1.0 (LICENSE.md); commercial /
 * OEM use: XCOL.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <getopt.h>

#define XCULL_VERSION "2.0.0"
/* build flags are injected by the Makefile; fall back for a bare cc build. */
#ifndef XCULL_CFLAGS
#define XCULL_CFLAGS "-O3 -march=native -flto"
#endif
#ifndef XCULL_LDFLAGS
#define XCULL_LDFLAGS ""
#endif
#if defined(__clang__)
#define XCULL_CC "clang " __clang_version__
#elif defined(__GNUC__)
#define XCULL_CC "gcc " __VERSION__
#else
#define XCULL_CC __VERSION__
#endif

/* ---------- ASCII lowercase LUT (v15: tolower hot-path replacement) ---------- *
 * tolower(3) is locale-aware (TLS lookup + per-call dispatch on glibc). On the
 * synthetic input the per-line path-lowering loop was the single largest hot
 * spot. URLs are ASCII by RFC; a 256-byte branch-free LUT folds A-Z to a-z and
 * passes everything else through, which is what xcull's path/host/scheme
 * lowering already wanted (the surrounding code never touched non-ASCII bytes
 * in those positions because bad_bytes() drops them upstream). */
static unsigned char LO[256];
static void lo_init(void){
    for(int i=0;i<256;i++) LO[i]=(unsigned char)((i>='A'&&i<='Z')?(i|0x20):i);
}

/* ---------- front-coded prev-line (deferred emit buffer) ---------- */
/* LEB128 varint for the front-code header: matchlen and suffixlen are each
 * < 8 KB, so they encode in 1-2 bytes instead of a flat u16 pair, trimming the
 * (resident) front-coded stream by the header bytes saved. */
static inline size_t vput(unsigned char*b,size_t v){ size_t i=0; while(v>=0x80){ b[i++]=(unsigned char)(v|0x80); v>>=7; } b[i++]=(unsigned char)v; return i; }
static inline size_t vget(const unsigned char*b,size_t*out){ size_t v=0,i=0; int sh=0; while(b[i]&0x80){ v|=(size_t)(b[i]&0x7f)<<sh; sh+=7; i++; } v|=(size_t)b[i]<<sh; i++; *out=v; return i; }
static char g_prev[8192]; static size_t g_prevlen=0;
/* keyset bytes live in a SEPARATE arena so the main arena stays a contiguous
 * front-coded entry stream (the emit walk steps it sequentially). */
static char *ksa=NULL; static size_t ksa_len=0,ksa_cap=0;
static size_t ksa_put(const char*p,size_t n){ if(ksa_len+n>ksa_cap){ while(ksa_len+n>ksa_cap) ksa_cap=ksa_cap?ksa_cap*2:(1u<<16); if(!(ksa=realloc(ksa,ksa_cap))){perror("realloc");exit(1);} } size_t o=ksa_len; memcpy(ksa+o,p,n); ksa_len+=n; return o; }
/* per-entry drop bitmap, indexed by arrival order (== emit walk order). */
static unsigned char *dropb=NULL; static size_t dropb_cap=0; static size_t ent_cnt=0;
static void db_ensure(size_t i){ size_t need=(i>>3)+1; if(need>dropb_cap){ size_t nc=dropb_cap?dropb_cap:4096; while(nc<need)nc*=2; dropb=realloc(dropb,nc); if(!dropb){perror("realloc");exit(1);} memset(dropb+dropb_cap,0,nc-dropb_cap); dropb_cap=nc; } }
static inline void db_set(size_t i){ dropb[i>>3]|=(unsigned char)(1u<<(i&7)); }
static inline int  db_get(size_t i){ return (dropb[i>>3]>>(i&7))&1; }
/* query-only records: subset-merge needs the key-set + a link through the
 * (path,card) bucket; aidx maps back to the entry's drop bit. */
typedef struct { unsigned ks_off, ks_len; int gnext; unsigned aidx; } QRec;
static QRec *qr=NULL; static size_t qr_cap=0, qr_cnt=0;
static size_t qr_push(unsigned ks_off,unsigned ks_len,int gnext,unsigned aidx){ if(qr_cnt>=qr_cap){ qr_cap=qr_cap?qr_cap*2:4096; if(!(qr=realloc(qr,qr_cap*sizeof(QRec)))){perror("realloc");exit(1);} } qr[qr_cnt].ks_off=ks_off; qr[qr_cnt].ks_len=ks_len; qr[qr_cnt].gnext=gnext; qr[qr_cnt].aidx=aidx; return qr_cnt++; }

/* ---------- arena ---------- */
static char  *arena = NULL;
static size_t arena_len = 0, arena_cap = 0;
static size_t arena_put(const char *s, size_t n) {
    if (arena_len + n > arena_cap) {
        while (arena_len + n > arena_cap) arena_cap = arena_cap ? arena_cap * 2 : (1u << 20);
        /* offsets are stored as uint32 (Slot/Rec/GSlot), so the arena is
         * capped at 4 GB; recon inputs never approach this (155 MB input
         * -> ~38 MB arena). Fail loud rather than silently truncate. */
        if (arena_cap > 0xFFFFFFFFull) {
            fprintf(stderr,"xcull: dedup arena would exceed 4 GB; "
                           "split the input or use -k streaming mode\n");
            exit(1); }
        if (!(arena = realloc(arena, arena_cap))) { perror("realloc"); exit(1); }
    }
    size_t off = arena_len; memcpy(arena + off, s, n); arena_len += n; return off;
}

/* ---------- 128-bit signature digest (v18.8) ----------
 * The dedup set is keyed on a 128-bit hash of the signature instead of the
 * signature bytes, so the arena no longer has to store a copy of every sig
 * (it now holds only the emitted lines + the small query key-sets, ~halving
 * peak RSS on large inputs). Two FNV-1a lanes with distinct basis+prime are
 * each run through a splitmix64 avalanche finaliser to decorrelate them. The
 * birthday-collision probability for a 10^6-line input is ~3.6e-27, so the
 * output is identical to a byte-exact compare in any run that will ever occur
 * - the same content-addressing trust model git uses for object identity. */
static inline unsigned long long mix64(unsigned long long x){
    x ^= x>>30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x>>27; x *= 0x94d049bb133111ebULL;
    x ^= x>>31; return x; }
static void hash128(const char*p,size_t n,unsigned long long*a,unsigned long long*b){
    unsigned long long h1=0xcbf29ce484222325ULL, h2=0x9e3779b97f4a7c15ULL;
    for(size_t i=0;i<n;i++){ unsigned char c=(unsigned char)p[i];
        h1=(h1^c)*0x100000001b3ULL;
        h2=(h2^c)*0x880355f21e6d1965ULL; }
    *a=mix64(h1); *b=mix64(h2); }

/* ---------- hash-keyed open-addressing set ----------
 * 16 bytes/slot: the 128-bit digest, no arena pointer. A slot is empty iff
 * both halves are zero; a real digest that happens to be (0,0) is nudged to
 * (1,0) so the sentinel stays unambiguous. */
typedef struct { unsigned long long h0,h1; } Slot;
static Slot  *tab = NULL;
static size_t tab_cap = 0, tab_cnt = 0;
static void tab_init(size_t c){ tab_cap=1; while(tab_cap<c) tab_cap<<=1;
    if(!(tab=calloc(tab_cap,sizeof(Slot)))){perror("calloc");exit(1);} }
static void tab_grow(void){ size_t oc=tab_cap; Slot*ot=tab; tab_cap<<=1;
    if(!(tab=calloc(tab_cap,sizeof(Slot)))){perror("calloc");exit(1);}
    for(size_t i=0;i<oc;i++){ if(!(ot[i].h0|ot[i].h1))continue; size_t j=ot[i].h0&(tab_cap-1);
        while(tab[j].h0|tab[j].h1) j=(j+1)&(tab_cap-1); tab[j]=ot[i]; } free(ot); }
static int tab_add(const char *s, size_t n){
    if(tab_cnt*4>=tab_cap*3) tab_grow();
    unsigned long long h0,h1; hash128(s,n,&h0,&h1); if(!(h0|h1)) h0=1;
    size_t j=h0&(tab_cap-1);
    while(tab[j].h0|tab[j].h1){ if(tab[j].h0==h0&&tab[j].h1==h1) return 0;
        j=(j+1)&(tab_cap-1); }
    tab[j].h0=h0; tab[j].h1=h1; tab_cnt++; return 1;
}

/* ---------- deferred-emit records (query-keyset SUBSET merge) ----------
 * Default keyset mode keeps one record per distinct query key-set (the dedup
 * sig already carries the sorted key-set), then drops a record whose key-set
 * is a SUBSET of another record of the SAME path: a covered variant adds no
 * new parameter, so removing it loses no surface, while DISJOINT key-sets
 * (e.g. ?is_prod vs ?is_debug) are both kept. /home?qs collapses into
 * /home?qs&secondQs (subset) but /product?is_prod and /product?is_debug both
 * survive. A no-query URL is its own record, never merged with a query one.
 * The covering superset can appear AFTER the subset and stdout cannot un-emit,
 * so default output is held until EOF and emitted in first-seen order.
 *
 * v21 storage: the emitted lines no longer need an offset/length array. Every
 * first-seen line is appended FRONT-CODED into the arena as a contiguous stream
 * [u16 matchlen-vs-previous-stored-line][u16 suffixlen][suffix bytes], and the
 * EOF emit walks that stream sequentially (reconstructing each line from the
 * shared prefix of the one before it). Drop state is a single bit per entry,
 * indexed by arrival order, in the `dropb` bitmap. Only query lines still need
 * their key-set for the subset test, so those keep a small QRec (key-set offset
 * in `ksa` + a gnext link through the path's (path,card) bucket + the arrival
 * index for the drop bit). Recon dumps are heavily prefix-clustered, so the
 * front-coded arena is roughly a third the size of the verbatim lines, and the
 * per-line off/len array (was 20 B/rec) collapses to one bit. */
/* dedup on the 128-bit sig digest. *is_new tells the caller whether this is
 * the first sighting (and so should store a fresh entry). No sig bytes stored. */
static void tab_group(const char *s, size_t n, int *is_new){
    if(tab_cnt*4>=tab_cap*3) tab_grow();
    unsigned long long h0,h1; hash128(s,n,&h0,&h1); if(!(h0|h1)) h0=1;
    size_t j=h0&(tab_cap-1);
    while(tab[j].h0|tab[j].h1){ if(tab[j].h0==h0&&tab[j].h1==h1){ *is_new=0; return; }
        j=(j+1)&(tab_cap-1); }
    tab[j].h0=h0; tab[j].h1=h1; tab_cnt++; *is_new=1;
}

/* ---------- base-path group table (head of each path's bucket list) ----------
 * keyed on the sig prefix up to and including '?', so every query key-set of
 * one path shares a list; value (head) is the index of the path's first
 * cardinality bucket (see Bucket below), -1 when the path has none yet. */
typedef struct { unsigned long long h0,h1; int head; } GSlot;
static GSlot *gtab=NULL; static size_t gcap=0,gcnt=0;
static void gtab_init(size_t c){ gcap=1; while(gcap<c) gcap<<=1;
    if(!(gtab=calloc(gcap,sizeof(GSlot)))){perror("calloc");exit(1);} }
static void gtab_grow(void){ size_t oc=gcap; GSlot*og=gtab; gcap<<=1;
    if(!(gtab=calloc(gcap,sizeof(GSlot)))){perror("calloc");exit(1);}
    for(size_t i=0;i<oc;i++){ if(!(og[i].h0|og[i].h1))continue; size_t j=og[i].h0&(gcap-1);
        while(gtab[j].h0|gtab[j].h1) j=(j+1)&(gcap-1); gtab[j]=og[i]; } free(og); }
/* return slot index for the base prefix s[0..n), keyed on its 128-bit digest
 * (no base bytes stored). new slots get head=-1. */
static size_t gtab_slot(const char*s,size_t n){
    if(gcnt*4>=gcap*3) gtab_grow();
    unsigned long long h0,h1; hash128(s,n,&h0,&h1); if(!(h0|h1)) h0=1;
    size_t j=h0&(gcap-1);
    while(gtab[j].h0|gtab[j].h1){ if(gtab[j].h0==h0&&gtab[j].h1==h1) return j;
        j=(j+1)&(gcap-1); }
    gtab[j].h0=h0; gtab[j].h1=h1; gtab[j].head=-1; gcnt++; return j; }

/* ---------- base-path bare-fold table (v20) ---------------------------------
 * Drops a bare URL (no ';matrix' and no '?query') when the SAME base path
 * also appears decorated - with a matrix param or a query. The endpoint still
 * survives via the richer line, so the bare line is a redundant duplicate of
 * an already-listed endpoint, not lost surface (path coverage is unchanged).
 * Keyed on the base-path digest: sig[0..base_len) where base_len stops at the
 * first ';' or '?'. A bare URL hashes its whole sig; a decorated one hashes
 * the prefix before its ';' / '?', so the two collide exactly when they share
 * the same endpoint. Deferred-emit (MERGE) only, so a decorated sibling that
 * arrives AFTER the bare line can still un-emit it. bare_rec = the bare line's
 * arrival index (-1 = none yet); decorated = a matrix/query sibling appeared. */
typedef struct { unsigned long long h0,h1; int bare_rec; int decorated; } BSlot;
static BSlot *btab=NULL; static size_t bcap=0,bcnt=0;
static void btab_init(size_t c){ bcap=1; while(bcap<c) bcap<<=1;
    if(!(btab=calloc(bcap,sizeof(BSlot)))){perror("calloc");exit(1);} }
static void btab_grow(void){ size_t oc=bcap; BSlot*ob=btab; bcap<<=1;
    if(!(btab=calloc(bcap,sizeof(BSlot)))){perror("calloc");exit(1);}
    for(size_t i=0;i<oc;i++){ if(!(ob[i].h0|ob[i].h1))continue; size_t j=ob[i].h0&(bcap-1);
        while(btab[j].h0|btab[j].h1) j=(j+1)&(bcap-1); btab[j]=ob[i]; } free(ob); }
static size_t btab_slot(const char*s,size_t n){
    if(bcnt*4>=bcap*3) btab_grow();
    unsigned long long h0,h1; hash128(s,n,&h0,&h1); if(!(h0|h1)) h0=1;
    size_t j=h0&(bcap-1);
    while(btab[j].h0|btab[j].h1){ if(btab[j].h0==h0&&btab[j].h1==h1) return j;
        j=(j+1)&(bcap-1); }
    btab[j].h0=h0; btab[j].h1=h1; btab[j].bare_rec=-1; btab[j].decorated=0; bcnt++; return j; }

/* ---------- per-(path,cardinality) buckets ----------------------------------
 * A path's records are split into buckets, one per distinct key-set size. Two
 * key-sets of EQUAL cardinality can never be a subset of one another (and an
 * identical pair is already collapsed by tab_group), so the subset-merge never
 * compares within a bucket - it only walks buckets of a DIFFERENT size. The
 * pathological case (one path, K distinct same-size key-sets: cache-buster or
 * fuzzer param spam) thus drops from O(K^2) to O(K). Records of one bucket are
 * chained through Rec.gnext; buckets of one path are chained through bnext. */
typedef struct { unsigned card; int rhead; int bnext; } Bucket;
static Bucket *buckets=NULL; static size_t bkt_cap=0, bkt_cnt=0;
static int bkt_new(unsigned card,int rhead,int bnext){
    if(bkt_cnt>=bkt_cap){ bkt_cap=bkt_cap?bkt_cap*2:4096;
        if(!(buckets=realloc(buckets,bkt_cap*sizeof(Bucket)))){perror("realloc");exit(1);} }
    buckets[bkt_cnt].card=card; buckets[bkt_cnt].rhead=rhead; buckets[bkt_cnt].bnext=bnext;
    return (int)bkt_cnt++; }
/* -L N: cap subset comparisons per inserted record (0 = off = strictly exact).
 * Only a safety valve for adversarial multi-cardinality antichains; when it
 * trips, the record is kept un-merged (output may then differ from a full run). */
static long g_merge_cap=0;

/* compare two query keys the same way query_keys() sorted them: memcmp over
 * the shared prefix, then shorter token first (so "a" < "ab"). */
static int key_cmp(const char*a,size_t al,const char*b,size_t bl){
    size_t m=al<bl?al:bl; int r=m?memcmp(a,b,m):0;
    if(r) return r; return al<bl?-1:al>bl?1:0; }
/* given two sorted '&'-joined key-sets, set *sub=1 if A is a subset of B and
 * *sup=1 if B is a subset of A (both 1 only if equal). Tokens are walked in
 * lockstep counting matches; A subset of B iff every A token matched. */
static void ks_relation(const char*a,size_t an,const char*b,size_t bn,int*sub,int*sup){
    size_t ia=0,ib=0; int na=0,nb=0,common=0;
    while(ia<an||ib<bn){
        if(ia>=an){ size_t e=ib; while(e<bn&&b[e]!='&')e++; nb++; ib=e<bn?e+1:e; continue; }
        if(ib>=bn){ size_t e=ia; while(e<an&&a[e]!='&')e++; na++; ia=e<an?e+1:e; continue; }
        size_t ae=ia; while(ae<an&&a[ae]!='&')ae++;
        size_t be=ib; while(be<bn&&b[be]!='&')be++;
        int c=key_cmp(a+ia,ae-ia,b+ib,be-ib);
        if(c==0){ common++; na++; nb++; ia=ae<an?ae+1:ae; ib=be<bn?be+1:be; }
        else if(c<0){ na++; ia=ae<an?ae+1:ae; }
        else { nb++; ib=be<bn?be+1:be; } }
    *sub=(common==na); *sup=(common==nb); }

/* ---------- predicates ---------- */
static int all_digits(const char*s,size_t n){ if(!n)return 0;
    for(size_t i=0;i<n;i++) if(!isdigit((unsigned char)s[i]))return 0; return 1; }
static int is_hex(const char*s,size_t n){
    for(size_t i=0;i<n;i++) if(!isxdigit((unsigned char)s[i]))return 0; return 1; }
static int is_uuid(const char*s,size_t n){ if(n!=36)return 0;
    for(size_t i=0;i<36;i++){ if(i==8||i==13||i==18||i==23){if(s[i]!='-')return 0;}
        else if(!isxdigit((unsigned char)s[i]))return 0;} return 1; }
/* render-noise ONLY: css/img/font/media. Archives & documents (pdf zip
 * doc xls sql bak swf ...) are NEVER auto-dropped - they can be findings.
 * NOT here on purpose: "map" (source maps disclose original source / routes /
 * secrets, a finding - v22). A .map is still a real static file, so it stays
 * in SX/KX: kept, with its ?query treated as scanner noise. */
static const char*NOISE_EXT[]={"css","png","jpg","jpeg","gif","svg","ico","bmp",
 "webp","tif","tiff","woff","woff2","ttf","eot","otf","mp4","mp3","avi","mov",
 "webm","wav","ogg","m4a","m4p","m4b","m4v","aac","wma","aiff","aif","opus",
 "mid","midi","oga","ogv","weba","amr","caf","ac3","mpg","mpeg","m2v","wmv",
 "f4v","f4a","3gp","3g2","vob","asf","flac","mkv",0};
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

/* segment of the form <LABEL><opaque-hex-id>[<sep><suffix>] where LABEL is an
 * uppercase tag whose tail is a NON-hex letter (TIP, IMG, DOC...) and the id is
 * a run of >=8 hex digits carrying at least one hex LETTER (A-F) - i.e. a
 * content-addressed handle like TIP14995B514_P1, not an enumerable counter.
 * Help-tip / asset id schemes mint thousands of distinct <LABEL><hex>_P1
 * articles that all hit ONE render template, so a hunter tests it once. We fold
 * the id to a single marker in the dedup SIGNATURE (label + '#' + suffix) =>
 * every <LABEL><hex>_<suffix> collapses to ONE real first-seen representative,
 * while a DIFFERENT label or suffix stays a distinct signature. Strict so
 * nothing enumerable is lost:
 *   - LABEL: leading [A-Z] run with trailing hex-letters pushed into the id;
 *     what remains must be >=2 chars, so its tail is a NON-hex letter. A pure
 *     uppercase-hex blob trims to len 0 and is REJECTED (those are the -F
 *     is_hex job and stay preserved by default).
 *   - id: [0-9A-Fa-f] run, length >=8, MUST contain >=1 hex letter, so a pure
 *     decimal id (INV00012345) is NOT folded - decimal/IDOR ids stay distinct.
 *   - after the id: end-of-segment OR a '-'/'_' separator; a '.' or trailing
 *     letters reject it (matching the other helpers' never-touch-dotted rule).
 * Default-on (no -F): these ids are high-entropy and never guessable, so folding
 * them costs no attack surface. O(seg)/line, no state. Returns the id END offset
 * (start of suffix) and sets *pre to the LABEL length; 0 on no match. */
static size_t content_hash_id(const char*s,size_t n,size_t*pre){
    if(n<10)return 0;                              /* >=2 label + >=8 id        */
    size_t p=0; while(p<n&&s[p]>='A'&&s[p]<='Z')p++;   /* leading UPPER run     */
    size_t pl=p;                                   /* push trailing hex-letters */
    while(pl&&s[pl-1]>='A'&&s[pl-1]<='F')pl--;     /*   of the label into id    */
    if(pl<2)return 0;                              /* label tail = NON-hex letter */
    size_t e=pl; int hl=0;                         /* id = hex run from pl      */
    for(;e<n;e++){ char c=s[e];
        if(c>='0'&&c<='9')continue;
        if((c>='A'&&c<='F')||(c>='a'&&c<='f')){hl=1;continue;}
        break; }
    if(e-pl<8||!hl)return 0;                       /* >=8 hex, >=1 hex letter   */
    if(e<n&&s[e]!='-'&&s[e]!='_')return 0;         /* end OR '-'/'_' separator  */
    *pre=pl; return e; }

/* CORRUPTED content-hash capture: a content_hash_id() leaf whose suffix carries
 * a digit immediately followed by a letter (TIPF5053A60A_P1cIt - the legit page
 * selector is _P1 and a stray "cIt" got glued on by the scraper, exactly like
 * the _P1http://... embedded-URL artifact the scheme gate already drops). A real
 * page selector is <alpha><digits> and ENDS there, so a digit->letter transition
 * inside the suffix never occurs in a clean one. Scoped to the already-narrow
 * content-hash shape so it cannot touch ordinary endpoints; the clean witness
 * (...._P1) is still present, so the dropped line is a pure duplicate. !X gated
 * via is_garbage(), so -x keeps it raw. O(seg)/line, no state. */
static int content_hash_junk(const char*s,size_t n){
    size_t pre,e=content_hash_id(s,n,&pre);
    if(!e)return 0;
    for(size_t i=e+1;i<n;i++){ char a=s[i-1],b=s[i];
        if(a>='0'&&a<='9'&&((b>='A'&&b<='Z')||(b>='a'&&b<='z')))return 1; }
    return 0; }

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
 * keyword BLACKLIST (it deletes Blogs.aspx too). xcull gets the same
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
static int bad_bytes(const char*s,size_t n,int isq,int ab){
    int semi=0;                                    /* seen ';' matrix intro */
    for(size_t i=0;i<n;i++){ unsigned char ch=s[i];
        if(ch<0x20||ch==0x7f||ch==' ')return 1;
        /* ab (allow_brace) = caller proved this is a GraphQL ?query={...}
         * value, where the braces ARE the operation, not scanner garbage.
         * Everything else still rejects raw braces (template artifacts,
         * mangled captures). The other invalid bytes stay rejected always. */
        if(ch=='{'||ch=='}'){ if(!ab)return 1; }
        else if(strchr("\"<>\\^`|",ch))return 1;
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
/* DFA goto: acg[state][byte]. The whole automaton is ~441 states, so a state
 * index fits in a signed 16-bit cell; storing the goto table as short instead
 * of int halves it (524 KB -> 262 KB) and, being half the footprint, it also
 * stays warmer in cache during the per-line scan. */
static short (*acg)[ACK];
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
 "webm","wav","ogg","m4a","m4p","m4b","m4v","aac","wma","aiff","aif","opus",
 "mid","midi","oga","ogv","weba","amr","caf","ac3","mpg","mpeg","m2v","wmv",
 "f4v","f4a","3gp","3g2","vob","asf","flac","mkv","map","txt","csv","md","ini",
 "log","conf","yaml","yml","pdf","doc","docx","xls","xlsx","ppt","pptx","zip",
 "rar","gz","tar","7z","bz2","sql","bak","old","swp","phps",0};
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
 *     (the input's qko07.info.example.com, q1837.sftkto.example.com,
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
 * NO buffering - distinct from the cross-line prefix-walk class xcull
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

/* GraphQL GET request: a query string carrying a key named "query" whose VALUE
 * begins with '{' (or its percent-encoded form %7b/%7B). On GraphQL the URL path
 * is always the same (/graphql) and the query VALUE is the whole operation, so
 * unlike an ordinary param it must dedupe on the value, not just the key name:
 * ?query={me{id}} and ?query={users{id}} hit different resolvers/objects and are
 * distinct attack surface. Tight on purpose (v23): only a literal "query" key
 * with a brace value fires, so a search box ?query=shoes and unrelated template
 * artifacts like ?affiliate_id={...} stay on the normal key-set merge path and
 * keep being dropped/merged exactly as before.
 *
 * Fast-path: 99.99% of query strings carry no '{' or '%7b' anywhere, so a single
 * SSE memchr for '{' (and a cheap pre-check for '%') bails out before the full
 * key/value walk. The walk itself only fires on queries that already contain a
 * literal '{' (or possibly %7b), keeping the v22 hot path untouched. */
static int is_gql_query(const char*q,size_t n){
    if(!memchr(q,'{',n)){
        /* no literal '{' - could still be %7b/%7B, but those are rare; scan
         * for '%' first (also cheap) and only then check the byte-pair */
        const char*p=memchr(q,'%',n); int has=0;
        while(p){ size_t rem=n-(size_t)(p-q);
            if(rem>=3&&p[1]=='7'&&(p[2]|32)=='b'){ has=1; break; }
            p=memchr(p+1,'%',n-(size_t)(p+1-q)); }
        if(!has) return 0; }
    size_t i=0;
    while(i<n){ size_t ks=i;
        while(i<n&&q[i]!='='&&q[i]!='&')i++;
        if(i<n&&q[i]=='='){                         /* key=value token */
            size_t kl=i-ks, vs=i+1;
            if(kl==5&&!memcmp(q+ks,"query",5)&&vs<n&&
               (q[vs]=='{'||(vs+2<n&&q[vs]=='%'&&q[vs+1]=='7'&&(q[vs+2]|32)=='b')))
                return 1;
            i=vs; }
        while(i<n&&q[i]!='&')i++;                    /* skip to next token */
        if(i<n)i++; }
    return 0; }

/* fast path: cheap O(n) byte scan first, then ONE Aho-Corasick pass over
 * path (BOTH|PATH markers) and query (BOTH markers only). */
static int is_garbage(const char*pp,size_t pn,const char*q,size_t qn){
    /* is_garbage is a pure OR of side-effect-free predicates, so call order
     * never changes the result, only the cost. Cheapest + highest-hit checks
     * run first; the expensive whole-path scans (repeat_seg, repeat_junk) run
     * LAST so a line caught by anything cheaper never pays for them. */
    if(bad_bytes(pp,pn,0,0))return 1;
    if(q&&qn&&q[0]==',')return 1;          /* query cannot start with ',' */
    /* gql is computed lazily, AFTER the cheap path checks have not already
     * rejected the line - keeps the v22 fast path unchanged for lines without
     * graphql braces (the overwhelming majority). */
    if(q&&qn&&bad_bytes(q,qn,1,is_gql_query(q,qn)))return 1;
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
        if(content_hash_junk(seg,sl))return 1;   /* TIP<hex>_P1cIt glue  */
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
                /* v23: a GraphQL "query={...}" value IS the surface (operation
                 * graph) - emit it verbatim, bypassing val_is_payload's >96
                 * char / brace / encoded-ctrl heuristics that would otherwise
                 * blank a real query. Tight: key must be literally "query"
                 * and value must start with '{' / %7b / %7B. The cheap byte
                 * tests run FIRST so the memcmp only fires on the brace shape. */
                int gqlv=vl&&(q[vs]=='{'||(vl>=3&&q[vs]=='%'&&q[vs+1]=='7'&&
                              (q[vs+2]|32)=='b'))&&kl==5&&
                         !memcmp(q+ks,"query",5);
                if(vl&&(gqlv||!val_is_payload(q+vs,vl)))
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

/* curl-style help: -h / --help prints the short form, --help all (or
 * -h all) appends examples and exit codes. Help goes to stdout (exit 0)
 * so it can be piped; the unknown-flag path stays on stderr (exit 2). */
static void print_help(int all){
    fputs(
"Usage: xcull [options] < urls.txt\n"
"\n"
"Dedup a recon URL surface on stdin and write the kept set to stdout.\n"
"Keep-biased and security-aware: distinct object ids and session tokens\n"
"survive, query dedup is keyed on parameter shape, render and scanner\n"
"noise is dropped. Pairs with gau, waybackurls, katana, qsreplace, anew.\n"
"\n"
"Options:\n"
" -F              Fold object ids to one route witness (route discovery)\n"
" -x              Keep invalid URLs raw (disable the garbage gate)\n"
" -a              Keep all assets (.css .png .woff .mp4 and other media)\n"
" -s              Case-sensitive path matching (/Login distinct from /login)\n"
" -L <N>          Cap subset-merge comparisons per record (0 = off, default)\n"
" -k              Keep parameter values and every distinct query key-set\n"
" -p              No path templating (every literal path survives)\n"
" -W              Disable wayback host-glue handling\n"
" -r              Disable URL canonicalization (RFC 3986)\n"
" -v, --verbose   Write \"<in> -> <out>  (peak RSS)\" stats to stderr\n"
" -h, --help      Show this help (use \"--help all\" for examples)\n"
" -V, --version   Show version number and build info, then quit\n",
    stdout);
    if(all){
        fputs(
"\n"
"Examples:\n"
"  gau example.com | xcull > surface.txt\n"
"        Default run: clean one archive feed, every distinct id kept.\n"
"  cat gau.txt wayback.txt katana.txt | xcull | tee urls.txt\n"
"        Merge multiple sources and dedupe once.\n"
"  cat urls.txt | xcull -F\n"
"        -F  fold ids to one route witness (route-scan pass).\n"
"  cat urls.txt | xcull -x\n"
"        -x  keep invalid/raw URLs (forensic; garbage gate off).\n"
"  gau example.com | xcull -a | grep -E '\\.(js|json|map)$'\n"
"        -a  keep assets; hunt secrets in JS and source maps.\n"
"  cat urls.txt | xcull -s\n"
"        -s  case-sensitive paths (/Login distinct from /login).\n"
"  cat fuzzer_dump.txt | xcull -L 100\n"
"        -L  cap subset-merge on adversarial multi-cardinality input.\n"
"  gau example.com | xcull -k | qsreplace FUZZ | anew params.txt\n"
"        -k  keep every value; feed a parameter-fuzzing pipeline.\n"
"  gau docs.example.com | xcull -p\n"
"        -p  no templating; keep every literal path (docs sites).\n"
"  waybackurls example.com | xcull -W\n"
"        -W  keep the archive's raw host glue (forensic).\n"
"  cat urls.txt | xcull -r > raw.txt\n"
"        -r  no canonicalization; ports and %-escapes kept verbatim.\n"
"  cat urls.txt | xcull -v > out.txt\n"
"        -v  show the reduction (stats on stderr, data on stdout).\n"
"\n"
"Exit codes:\n"
"  0   Success.\n"
"  1   I/O error, allocation failure, or out of memory.\n"
"  2   Unknown flag or malformed argument.\n",
        stdout);
    } else {
        fputs(
"\n"
"Use \"xcull --help all\" for examples and exit codes, or \"man xcull\".\n",
        stdout);
    }
    fputs("\nProject: https://github.com/xcull/xcull\n", stdout);
}

/* wget-style --version / -V: name + platform, compiled-in capabilities,
 * the actual build and link flags (injected by the Makefile), the default
 * dedup policy, license, and where to send bug reports. */
static void print_version(void){
    struct utsname u; const char *sys="unknown", *mach="";
    if(uname(&u)==0){ sys=u.sysname; mach=u.machine; }
    printf("xcull %s built on %s %s.\n\n", XCULL_VERSION, sys, mach);
    fputs(
"Capabilities (all compiled in, libc only, no runtime dependencies):\n"
" +idor-preserve  +session-preserve  +query-shape-dedup  +subset-merge\n"
" +garbage-gate   +wayback-clean     +case-fold          +canonical\n"
"\n",
    stdout);
    printf("Build:\n    %s\n    %s\n    built %s\n\n",
        XCULL_CFLAGS, XCULL_CC, __DATE__);
    printf("Link:\n    %s\n\n",
        XCULL_LDFLAGS[0] ? XCULL_LDFLAGS : "(none)");
    fputs(
"Default policy:\n"
"    Distinct object ids and session tokens survive; query dedup is\n"
"    keyed on parameter shape; render and scanner noise is dropped;\n"
"    hosts are normalized and paths canonicalized case-insensitively.\n"
"\n"
"License XSAL v1.0 (source-available, custom; see LICENSE.md).\n"
"Commercial and OEM licensing: XCOL.md.\n"
"This software is provided WITHOUT WARRANTY, to the extent permitted\n"
"by law.\n"
"\n"
"Written by the xcull project.\n"
"Bug reports and questions: https://github.com/xcull/xcull/issues\n",
    stdout);
}

int main(int argc,char**argv){
    int F=1,S=0,K=0,P=0,W=1,C=1,VB=0,X=0,FI=0,c; /* clean defaults: sanity gate +
                                       noise-filter + case-fold + wayback +
                                       canonical (X=0 means gate ENABLED).
                                       FI=0: object-ids PRESERVED (v18) */
    /* --help [all] / -h [all] take an optional positional "all" word that
     * getopt cannot model, so catch that form here; bare -h/--help,
     * -V/--version, -v/--verbose all go through getopt_long below. */
    for(int i=1;i<argc;i++)
        if((!strcmp(argv[i],"-h")||!strcmp(argv[i],"--help"))
           && i+1<argc && !strcmp(argv[i+1],"all")){ print_help(1); return 0; }
    static const struct option lopt[]={
        {"verbose",no_argument,0,'v'},
        {"help",   no_argument,0,'h'},
        {"version",no_argument,0,'V'},
        {0,0,0,0} };
    while((c=getopt_long(argc,argv,"fkpwcWrasxvVhFL:",lopt,NULL))!=-1){
        if(c=='k')K=1; else if(c=='p')P=1;
        else if(c=='a')F=0;                          /* keep ALL assets */
        else if(c=='s')S=1;                          /* case-sensitive path */
        else if(c=='x')X=1;                          /* keep invalid URLs */
        else if(c=='F')FI=1;                         /* fold ids (N/U/H/stem) */
        else if(c=='L')g_merge_cap=strtol(optarg,NULL,10); /* subset-cmp cap (0=off) */
        else if(c=='W')W=0; else if(c=='r')C=0;      /* opt-outs */
        else if(c=='f'||c=='w'||c=='c'){ /* legacy no-ops (already default) */ }
        else if(c=='v')VB=1;                         /* verbose stats */
        else if(c=='h'){ print_help(0); return 0; }
        else if(c=='V'){ print_version(); return 0; }
        else { fprintf(stderr,"Try 'xcull --help' for the full option list.\n");
          return 2; } }
    if(isatty(STDIN_FILENO)){
        fprintf(stderr,
          "xcull: no input on stdin (it is a terminal).\n"
          "Pipe URLs in, e.g.  gau example.com | xcull\n"
          "Run 'xcull --help' for options.\n");
        return 2; }
    lo_init();
    /* v15: 64 KB output stdio buffer so fwrite() amortises syscalls. Input
     * is read with our own block reader below - bypasses getline()'s per-
     * line buffer-growth check and the byte-by-byte stdio scan for '\n'. */
    static char io_out[1<<16];
    setvbuf(stdout, io_out, _IOFBF, sizeof io_out);
    if(X) g_nojunk=0;
    if(!X){ garbage_init(); tld_init(); }
    tab_init(1<<16);
    gtab_init(1<<14);
    btab_init(1<<14);
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
    /* MERGE = default keyset mode: drop a path's query URL when its key set
     * is a subset of another's; disjoint key sets all survive (deferred
     * emit). -k (full query) and -x (raw) keep streaming, untouched. */
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
                size_t stl,ide,cpre=0;
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
                else if(!P&&(ide=content_hash_id(sg,sl,&cpre))){
                    /* <LABEL><opaque-hex>[sep suffix] -> label + '#' + suffix */
                    size_t avail=sizeof sig>o?sizeof sig-1-o:0;
                    size_t n=cpre<avail?cpre:avail;
                    if(S) memcpy(sig+o,sg,n);
                    else for(size_t z=0;z<n;z++){
                        unsigned char c=(unsigned char)sg[z];
                        sig[o+z]=(char)(c|(((unsigned)c-'A'<26u)<<5)); }
                    o+=n; PUTC('#');
                    size_t sfx=sl-ide;             /* suffix kept verbatim     */
                    avail=sizeof sig>o?sizeof sig-1-o:0;
                    n=sfx<avail?sfx:avail;
                    if(S) memcpy(sig+o,sg+ide,n);
                    else for(size_t z=0;z<n;z++){
                        unsigned char c=(unsigned char)sg[ide+z];
                        sig[o+z]=(char)(c|(((unsigned)c-'A'<26u)<<5)); }
                    o+=n; }
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
        /* is_qgroup: this URL carries a meaningful query key-set. qstart marks
         * where the sorted keys begin in sig, so sig[0..qstart) (incl. '?') is
         * the base-path group key and sig[qstart..o) is the key-set. */
        int is_qgroup=0; size_t qstart=0;
        if(qok&&query&&queryn){
            if(K){ PUTC('?'); PUT(query,queryn); }
            else if(is_gql_query(query,queryn)){
                /* v23: GraphQL ?query={...} - dedupe on the WHOLE query VALUE,
                 * not just the key name. The operation IS the surface, so two
                 * distinct queries hit two distinct resolver subgraphs. We
                 * deliberately leave is_qgroup=0 so this stays out of the
                 * keyset-merge antichain and behaves like a normal decorated
                 * line (v20 bare-fold then drops a bare /graphql sibling). */
                PUTC('?'); PUT(query,queryn); }
            else { char qk[4096]; size_t qn=query_keys(query,queryn,qk,sizeof qk);
                   if(qn){ PUTC('?'); qstart=o; PUT(qk,qn);
                           if(MERGE) is_qgroup=1; } } }

        if(MERGE){
            int is_new=0; tab_group(sig,o,&is_new);
            if(!is_new) continue;                 /* this key-set already seen */
            /* build the canonical line, then store it FRONT-CODED into the arena:
             * [u16 matchlen vs the previous stored line][u16 suffixlen][suffix]. */
            char out[8192]; size_t r=0;
            {
                #define O(S,N) do{size_t _n=(N); if(r+_n<sizeof out){memcpy(out+r,(S),_n);r+=_n;}}while(0)
                #define OC(X)  do{ if(r+1<sizeof out) out[r++]=(X);}while(0)
              if(!C){ O(line,L); OC('\n'); }
              else {
                O(schb,schbn); O("://",3); O(H,HN);
                if(port&&!defp){ OC(':'); O(port,portn); }
                OC('/');
                for(int k=0;k<segc;k++){ if(k) OC('/'); O(pp+seg_s[k], seg_l[k]); }
                if(qok&&query&&queryn){ char cq[8192];
                    size_t cn=clean_query(query,queryn,cq,sizeof cq);
                    if(cn){ OC('?'); O(cq,cn); } }
                if(r<sizeof out) out[r++]='\n';
              }
                #undef O
                #undef OC
            }
            size_t ml=0,mm=r<g_prevlen?r:g_prevlen; while(ml<mm&&out[ml]==g_prev[ml])ml++;
            unsigned char hdr[8]; size_t hl=vput(hdr,ml); hl+=vput(hdr+hl,r-ml);
            arena_put((char*)hdr,hl); arena_put(out+ml,r-ml);
            memcpy(g_prev,out,r<sizeof g_prev?r:sizeof g_prev); g_prevlen=r;
            size_t aidx=ent_cnt; db_ensure(aidx); ent_cnt++;
            /* v20 bare-fold: key on the base path (sig up to first ';' or '?').
             * A decorated line (matrix/query) marks the base and un-emits any
             * bare line already kept for it; a bare line is dropped on sight if
             * a decorated sibling was seen first. (drop bit by arrival index.) */
            size_t base_len=0; while(base_len<o&&sig[base_len]!=';'&&sig[base_len]!='?') base_len++;
            int bare=(base_len==o);
            size_t bslot=btab_slot(sig,base_len);
            if(!bare){
                btab[bslot].decorated=1;
                int br=btab[bslot].bare_rec;
                if(br>=0&&!db_get((size_t)br)){ db_set((size_t)br); kept--; }
            }
            if(!is_qgroup){
                if(bare){
                    if(btab[bslot].decorated) db_set(aidx);         /* sibling already present */
                    else kept++;
                    btab[bslot].bare_rec=(int)aidx;
                } else kept++;                                      /* matrix line survives */
                continue;
            }
            /* query record: stash the key-set bytes in ksa (ks_relation needs the
             * real names for the subset test), then drop-or-keep against the path's
             * existing records (an antichain of maximal key-sets). */
            size_t ks_off=ksa_put(sig+qstart,o-qstart); unsigned ks_len=(unsigned)(o-qstart);
            size_t gs=gtab_slot(sig,qstart);
            /* cardinality = number of unique '&'-joined keys (>=1 here) */
            unsigned card=1; for(unsigned z=0;z<ks_len;z++) if(ksa[ks_off+z]=='&') card++;
            int subsumed=0, tb=-1; long cmps=0;
            for(int b=gtab[gs].head; b!=-1; b=buckets[b].bnext){
                if(buckets[b].card==card){ tb=b; continue; } /* same size: incomparable, skip */
                if(subsumed) continue;                       /* covered: just keep finding tb */
                if(g_merge_cap && cmps>=g_merge_cap) continue;
                if(buckets[b].card>card){     /* members larger: S may be a SUBSET of one */
                    for(int w=buckets[b].rhead; w!=-1; w=qr[w].gnext){
                        if(db_get(qr[w].aidx)) continue; cmps++;
                        int sub=0,sup=0;
                        ks_relation(ksa+ks_off,ks_len,ksa+qr[w].ks_off,qr[w].ks_len,&sub,&sup);
                        if(sub){ subsumed=1; break; }                 /* covered by existing */
                        if(g_merge_cap && cmps>=g_merge_cap) break; }
                } else {                      /* members smaller: S may be a SUPERSET, drop them */
                    for(int w=buckets[b].rhead; w!=-1; w=qr[w].gnext){
                        if(db_get(qr[w].aidx)) continue; cmps++;
                        int sub=0,sup=0;
                        ks_relation(ksa+ks_off,ks_len,ksa+qr[w].ks_off,qr[w].ks_len,&sub,&sup);
                        if(sup){ db_set(qr[w].aidx); kept--; }        /* new one covers this  */
                        if(g_merge_cap && cmps>=g_merge_cap) break; }
                }
            }
            int gnext = (tb<0)? -1 : buckets[tb].rhead;
            size_t qidx = qr_push(ks_off,ks_len,gnext,(unsigned)aidx);
            if(tb<0){ tb=bkt_new(card,(int)qidx,gtab[gs].head); gtab[gs].head=tb; }
            else    { buckets[tb].rhead=(int)qidx; }
            if(subsumed) db_set(aidx); else kept++;
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
    /* sequential front-coded emit: step the arena entry stream in arrival order,
     * rebuilding each line from the shared prefix of the previous one, and write
     * only the entries whose drop bit is clear. */
    if(MERGE){ static char rec_line[8192]; size_t pos=0;
        for(size_t i=0;i<ent_cnt;i++){
            size_t mlen,slen,hl;
            hl =vget((unsigned char*)arena+pos,&mlen);
            hl+=vget((unsigned char*)arena+pos+hl,&slen);
            memcpy(rec_line+mlen, arena+pos+hl, slen);   /* prefix already in rec_line */
            if(!db_get(i)) fwrite(rec_line,1,mlen+slen,stdout);
            pos += hl+slen;
        }
    }
    if(VB){ struct rusage ru; getrusage(RUSAGE_SELF,&ru);
        fprintf(stderr,"xcull: %llu -> %llu  (peak RSS %ld KB)\n",total,kept,ru.ru_maxrss); }
    return 0;
}
