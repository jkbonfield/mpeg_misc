// cc -I. -g -O3 tokenise_name3.c codec_orig.c rANS_static4x16pr.c pooled_alloc.c -lm

// As per tokenise_name2 but has the entropy encoder built in already,
// so we just have a single encode and decode binary.  (WIP; mainly TODO)

// TODO
//
// - Is it better when encoding 1, 2, 3, 3, 4, 5, 5, 6, 7, 9, 9, 10 to encode
//   this as a mixture of MATCH and DELTA ops, or as entirely as DELTA ops
//   with some delta values being zero?  I suspect the latter, but it is
//   not implemented here.  See "last_token_delta" comments in code.
//
// - Consider variable size string implementations.
//   Pascal style strings (length + str),
//   C style strings (nul terminated),
//   Or split descriptors: length descriptor and string contents descriptor.
//
// - Is this one descriptor or many descriptors?
//   A) Lots of different models but feeding one bit-buffer emitted to
//      by the entropy encoder => one descriptor (fqzcomp).
//   B) Lots of different models each feeding their own bit-buffers
//      => many descriptors.
//
// - multiple integer types depending on size; 1, 2, 4 byte long.
//
// - Consider token choice for isalnum instead of isalpha.  Sometimes better.
//
// - Consider token synchronisation (eg on matching chr symbols?) incase of
//   variable number.  Eg consider foo:0999, foo:1000, foo:1001 (the leading
//   zero adds an extra token).
//
// - Optimisation of tokens.  Eg:
//     HS25_09827:2:2102:11274:80442#49
//     HS25_09827:2:2109:12941:31311#49
//
//   We'll have tokens for HS 25 _ 09827 : 2 : that are entirely <MATCH>
//   after the initial token.  These 7 tokens could be one ALPHA instead
//   of 7 distinct tokens, with 1 MATCH instead of 7.  This is both a speed
//   improvement for decoding as well as a space saving (fewer descriptors
//   and associated overhead).


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pooled_alloc.h>

// FIXME
#define MAX_TOKENS 128
#define MAX_DESCRIPTORS (MAX_TOKENS<<4)

// Number of names per block
#define MAX_NAMES 1000000

enum name_type {N_ERR = -1, N_TYPE = 0, N_ALPHA, N_CHAR, N_DZLEN, N_DIGITS0, N_DUP, N_DIFF, 
		N_DIGITS, N_D1, N_D2, N_D3, N_DDELTA, N_DDELTA0, N_MATCH, N_END};

char *types[]={"TYPE", "ALPHA", "CHAR", "DZLEN", "DIG0", "DUP", "DIFF",
	       "DIGITS", "", "", "", "DDELTA", "DDELTA0", "MATCH", "END"};

typedef struct trie trie_t;

typedef struct {
    char *last_name;
    int last_ntok;
    enum name_type last_token_type[MAX_TOKENS];
    int last_token_int[MAX_TOKENS];
    int last_token_str[MAX_TOKENS];
    //int last_token_delta[MAX_TOKENS];
} last_context;

typedef struct {
    last_context *lc;

    // For finding entire line dups
    int counter;

    // Trie used in encoder only
    trie_t *t_head;
    pool_alloc_t *pool;
} name_context;

name_context *create_context(int max_names) {
    name_context *ctx = malloc(sizeof(*ctx) + max_names*sizeof(*ctx->lc));
    if (!ctx) return NULL;

    ctx->counter = 0;
    ctx->t_head = NULL;

    ctx->lc = (last_context *)(((char *)ctx) + sizeof(*ctx));
    ctx->pool = NULL;

    return ctx;
}

void free_trie(trie_t *t);
void free_context(name_context *ctx) {
    if (!ctx)
	return;

//    if (ctx->t_head)
//	free_trie(ctx->t_head);
    if (ctx->pool)
	pool_destroy(ctx->pool);

    free(ctx);
}

typedef struct {
    uint8_t *buf;
    size_t buf_a, buf_l; // alloc and used length.
    int tnum, ttype;
    int dup_from;
} descriptor;

static descriptor desc[MAX_DESCRIPTORS];

//-----------------------------------------------------------------------------
// Fast unsigned integer printing code.
// Returns number of bytes written.
static int append_uint32_fixed(char *cp, uint32_t i, uint8_t l) {
    switch (l) {
    case 9:*cp++ = i / 100000000 + '0', i %= 100000000;
    case 8:*cp++ = i / 10000000  + '0', i %= 10000000;
    case 7:*cp++ = i / 1000000   + '0', i %= 1000000;
    case 6:*cp++ = i / 100000    + '0', i %= 100000;
    case 5:*cp++ = i / 10000     + '0', i %= 10000;
    case 4:*cp++ = i / 1000      + '0', i %= 1000;
    case 3:*cp++ = i / 100       + '0', i %= 100;
    case 2:*cp++ = i / 10        + '0', i %= 10;
    case 1:*cp++ = i             + '0';
    case 0:break;
    }
    return l;
}

static int append_uint32_var(char *cp, uint32_t i) {
    char *op = cp;
    uint32_t j;

    //if (i < 10)         goto b0;
    if (i < 100)        goto b1;
    //if (i < 1000)       goto b2;
    if (i < 10000)      goto b3;
    //if (i < 100000)     goto b4;
    if (i < 1000000)    goto b5;
    //if (i < 10000000)   goto b6;
    if (i < 100000000)  goto b7;

    if ((j = i / 1000000000)) {*cp++ = j + '0'; i -= j*1000000000; goto x8;}
    if ((j = i / 100000000))  {*cp++ = j + '0'; i -= j*100000000;  goto x7;}
 b7:if ((j = i / 10000000))   {*cp++ = j + '0'; i -= j*10000000;   goto x6;}
    if ((j = i / 1000000))    {*cp++ = j + '0', i -= j*1000000;    goto x5;}
 b5:if ((j = i / 100000))     {*cp++ = j + '0', i -= j*100000;     goto x4;}
    if ((j = i / 10000))      {*cp++ = j + '0', i -= j*10000;      goto x3;}
 b3:if ((j = i / 1000))       {*cp++ = j + '0', i -= j*1000;       goto x2;}
    if ((j = i / 100))        {*cp++ = j + '0', i -= j*100;        goto x1;}
 b1:if ((j = i / 10))         {*cp++ = j + '0', i -= j*10;         goto x0;}
    if (i)                     *cp++ = i + '0';
    return cp-op;

 x8:*cp++ = i / 100000000 + '0', i %= 100000000;
 x7:*cp++ = i / 10000000  + '0', i %= 10000000;
 x6:*cp++ = i / 1000000   + '0', i %= 1000000;
 x5:*cp++ = i / 100000    + '0', i %= 100000;
 x4:*cp++ = i / 10000     + '0', i %= 10000;
 x3:*cp++ = i / 1000      + '0', i %= 1000;
 x2:*cp++ = i / 100       + '0', i %= 100;
 x1:*cp++ = i / 10        + '0', i %= 10;
 x0:*cp++ = i             + '0';

    return cp-op;
}

//-----------------------------------------------------------------------------
// Example descriptor encoding and IO.
//
// Here we just append to a buffer so we can dump out the results.
// These could then be passed through a static entropy encoder that
// encodes the entire buffer.
//
// Alternatively an adaptive entropy encoder could be place inline
// here to encode as it goes using additional knowledge from the
// supplied context.

// Ensure room for sz more bytes.
static int descriptor_grow(descriptor *fd, uint32_t sz) {
    while (fd->buf_l + sz > fd->buf_a) {
	size_t buf_a = fd->buf_a ? fd->buf_a*2 : 65536;
	unsigned char *buf = realloc(fd->buf, buf_a);
	if (!buf)
	    return -1;
	fd->buf = buf;
	fd->buf_a = buf_a;
    }

    return 0;
}

static int encode_token_type(name_context *ctx, int ntok,
			     enum name_type type) {
    int id = ntok<<4;

    if (descriptor_grow(&desc[id], 1) < 0) return -1;

    desc[id].buf[desc[id].buf_l++] = type;

    return 0;
}

static int encode_token_match(name_context *ctx, int ntok) {
    return encode_token_type(ctx, ntok, N_MATCH);
}

static int encode_token_end(name_context *ctx, int ntok) {
    return encode_token_type(ctx, ntok, N_END);
}

static enum name_type decode_token_type(name_context *ctx, int ntok) {
    int id = ntok<<4;
    if (desc[id].buf_l >= desc[id].buf_a) return -1;
    return desc[id].buf[desc[id].buf_l++];
}

// int stored as 32-bit quantities
static int encode_token_int(name_context *ctx, int ntok,
			    enum name_type type, uint32_t val) {
    int id = (ntok<<4) | type;

    if (encode_token_type(ctx, ntok, type) < 0) return -1;
    if (descriptor_grow(&desc[id], 4) < 0)	return -1;

    // Assumes little endian and unalign access OK.
    *(uint32_t *)(desc[id].buf + desc[id].buf_l) = val;
    desc[id].buf_l += 4;

    return 0;
}

// Return 0 on success, -1 on failure;
static int decode_token_int(name_context *ctx, int ntok,
			    enum name_type type, uint32_t *val) {
    int id = (ntok<<4) | type;
    // FIXME: add checks

    // Assumes little endian and unalign access OK.
    *val = *(uint32_t *)(desc[id].buf + desc[id].buf_l);
    desc[id].buf_l += 4;

    return 0;
}

// 8 bit integer quantity
static int encode_token_int1(name_context *ctx, int ntok,
			     enum name_type type, uint32_t val) {
    int id = (ntok<<4) | type;

    if (encode_token_type(ctx, ntok, type) < 0) return -1;
    if (descriptor_grow(&desc[id], 1) < 0)	return -1;

    desc[id].buf[desc[id].buf_l++] = val;

    return 0;
}

static int encode_token_int1_(name_context *ctx, int ntok,
			      enum name_type type, uint32_t val) {
    int id = (ntok<<4) | type;

    if (descriptor_grow(&desc[id], 1) < 0)	return -1;

    desc[id].buf[desc[id].buf_l++] = val;

    return 0;
}

// Return 0 on success, -1 on failure;
static int decode_token_int1(name_context *ctx, int ntok,
			     enum name_type type, uint32_t *val) {
    int id = (ntok<<4) | type;
    // FIXME: add checks

    *val = desc[id].buf[desc[id].buf_l++];

    return 0;
}

// Int stored in 4 data series as 4x8 bit quantities
static int encode_token_int4(name_context *ctx, int ntok,
			    enum name_type type, uint32_t val) {
    int id = (ntok<<4) | type;

    if (encode_token_type(ctx, ntok, type) < 0) return -1;
    if (descriptor_grow(&desc[id  ], 1) < 0)	return -1;
    if (descriptor_grow(&desc[id+1], 1) < 0)	return -1;
    if (descriptor_grow(&desc[id+2], 1) < 0)	return -1;
    if (descriptor_grow(&desc[id+3], 1) < 0)	return -1;

    desc[id  ].buf[desc[id  ].buf_l++] = val>>0;
    desc[id+1].buf[desc[id+1].buf_l++] = val>>8;
    desc[id+2].buf[desc[id+2].buf_l++] = val>>16;
    desc[id+3].buf[desc[id+3].buf_l++] = val>>24; 

    return 0;
}

// Return 0 on success, -1 on failure;
static int decode_token_int4(name_context *ctx, int ntok,
			     enum name_type type, uint32_t *val) {
    int id = (ntok<<4) | type;
    // FIXME: add checks

    *val = 
	(desc[id  ].buf[desc[id  ].buf_l++] << 0 ) |
	(desc[id+1].buf[desc[id+1].buf_l++] << 8 ) |
	(desc[id+2].buf[desc[id+2].buf_l++] << 16) |
	(desc[id+3].buf[desc[id+3].buf_l++] << 24);

    return 0;
}

// 7 bits at a time with variable size.
static int encode_token_int7(name_context *ctx, int ntok,
			     enum name_type type, uint32_t val) {
    int id = (ntok<<4) | type;

    if (encode_token_type(ctx, ntok, type) < 0) return -1;
    if (descriptor_grow(&desc[id], 5) < 0)	return -1;

    do {
	desc[id].buf[desc[id].buf_l++] = (val & 0x7f) | ((val >= 0x80)<<7);
	val >>= 7;
    } while (val);

    return 0;
}

// Return 0 on success, -1 on failure;
static int decode_token_int7(name_context *ctx, int ntok,
			     enum name_type type, uint32_t *val) {
    int id = (ntok<<4) | type;
    uint32_t v = 0, s = 0;
    uint8_t c;

    // FIXME: add checks
    do {
	c = desc[id].buf[desc[id].buf_l++];
	v |= (c & 0x7f) << s;
	s += 7;
    } while (c & 0x80);

    *val = v;
    return 0;
}

//#define encode_token_int encode_token_int7
//#define decode_token_int decode_token_int7

//#define encode_token_int encode_token_int4
//#define decode_token_int decode_token_int4



// Basic C-string style for now.
//
// Maybe XOR with previous string as context?
// This permits partial match to be encoded efficiently.
static int encode_token_alpha(name_context *ctx, int ntok,
			    char *str, int len) {
    int id = (ntok<<4) | N_ALPHA;

    if (encode_token_type(ctx, ntok, N_ALPHA) < 0)  return -1;
    if (descriptor_grow(&desc[id], len+1) < 0) return -1;
    memcpy(&desc[id].buf[desc[id].buf_l], str, len);
    desc[id].buf[desc[id].buf_l+len] = 0;
    desc[id].buf_l += len+1;

    return 0;
}

//// Strings using a separate length model
//static int encode_token_alpha_len(name_context *ctx, int ntok,
//				  char *str, int len) {
//    assert(len < 256); // FIXME
//    int id = (ntok<<4) | N_ALPHA;
//
//    if (encode_token_type(ctx, ntok, N_ALPHA) < 0)  return -1;
//    if (descriptor_grow(&desc[id],   len) < 0) return -1;
//    if (descriptor_grow(&desc[id+1], 1) < 0) return -1;
//    memcpy(&desc[id].buf[desc[id].buf_l], str, len);
//    desc[id].buf[desc[id].buf_l+len] = 0;
//    desc[id].buf_l += len;
//    desc[id+1].buf[desc[id+1].buf_l++] = len;
//
//    return 0;
//}
//#define encode_token_alpha encode_token_alpha_len

// FIXME: need limit on string length for security
// Return length on success, -1 on failure;
static int decode_token_alpha(name_context *ctx, int ntok, char *str) {
    int id = (ntok<<4) | N_ALPHA;
    char c;
    int len = 0;
    do {
	// FIXME: add checks
	c = desc[id].buf[desc[id].buf_l++];
	str[len++] = c;
    } while(c);

    return len-1;
}

static int encode_token_char(name_context *ctx, int ntok, char c) {
    int id = (ntok<<4) | N_CHAR;

    if (encode_token_type(ctx, ntok, N_CHAR) < 0) return -1;
    if (descriptor_grow(&desc[id], 1) < 0)    return -1;
    desc[id].buf[desc[id].buf_l++] = c;

    return 0;
}

// FIXME: need limit on string length for security
// Return length on success, -1 on failure;
static int decode_token_char(name_context *ctx, int ntok, char *str) {
    int id = (ntok<<4) | N_CHAR;

    // FIXME: add checks
    *str = desc[id].buf[desc[id].buf_l++];

    return 1;
}


// A duplicated name
static int encode_token_dup(name_context *ctx, uint32_t val) {
    return encode_token_int(ctx, 0, N_DUP, val);
}

// Which read to delta against
static int encode_token_diff(name_context *ctx, uint32_t val) {
    return encode_token_int(ctx, 0, N_DIFF, val);
}


//-----------------------------------------------------------------------------
// Trie implementation for tracking common name prefixes.
typedef struct trie {
    char c;
    int count;
    //struct trie *next[128];
    struct trie *next, *sibling;
    int n; // Nth line
} trie_t;

//static trie_t *t_head = NULL;

void free_trie(trie_t *t) {
    trie_t *x, *n;
    for (x = t->next; x; x = n) {
	n = x->sibling;
	free_trie(x);
    }
    free(t);
}

int build_trie(name_context *ctx, char *data, size_t len, int n) {
    int nlines = 0;
    size_t i;
    trie_t *t;

    if (!ctx->t_head)
	ctx->t_head = calloc(1, sizeof(*ctx->t_head));

    // Build our trie, also counting input lines
    for (nlines = i = 0; i < len; i++, nlines++) {
	t = ctx->t_head;
	t->count++;
	while (i < len && data[i] > '\n') {
	    unsigned char c = data[i++];
	    if (c & 0x80)
		//fprintf(stderr, "8-bit ASCII is unsupported\n");
		abort();
	    c &= 127;


	    trie_t *x = t->next, *l = NULL;
	    while (x && x->c != c) {
		l = x; x = x->sibling;
	    }
	    if (!x) {
		if (!ctx->pool)
		    ctx->pool = pool_create(sizeof(trie_t));
		x = (trie_t *)pool_alloc(ctx->pool);
		memset(x, 0, sizeof(*x));
		if (!l)
		    x = t->next    = x;
		else
		    x = l->sibling = x;
		x->n = n;
		x->c = c;
	    }
	    t = x;
	    t->c = c;
	    t->count++;
	}
    }

    return 0;
}

#if 0
void dump_trie(trie_t *t, int depth) {
    if (depth == 0) {
	printf("graph x_%p {\n    splines = ortho\n    ranksep=2\n", t);
	printf("    p_%p [label=\"\"];\n", t);
	dump_trie(t, 1);
	printf("}\n");
    } else {
	int j, k, count;//, cj;
	char label[100], *cp;
	trie_t *tp = t;

//    patricia:
//	for (count = j = 0; j < 128; j++)
//	    if (t->next[j])
//		count++, cj=j;
//
//	if (count == 1) {
//	    t = t->next[cj];
//	    *cp++ = cj;
//	    goto patricia;
//	}

	trie_t *x;
	for (x = t->next; x; x = x->sibling) {
	    printf("    p_%p [label=\"%c\"];\n", x, x->c);
	    printf("    p_%p -- p_%p [label=\"%d\", penwidth=\"%f\"];\n", tp, x, x->count, MAX((log(x->count)-3)*2,1));
	    //if (depth <= 11)
		dump_trie(x, depth+1);
	}

#if 0	    
	for (j = 0; j < 128; j++) {
	    trie_t *tn;

	    if (!t->next[j])
		continue;

	    cp = label;
	    tn = t->next[j];
	    *cp++ = j;
//	patricia:

	    for (count = k = 0; k < 128; k++)
		if (tn->next[k])
		    count++;//, cj=k;

//	    if (count == 1) {
//		tn = tn->next[cj];
//		*cp++ = cj;
//		goto patricia;
//	    }
	    *cp++ = 0;

	    printf("    p_%p [label=\"%s\"];\n", tn, label);
	    printf("    p_%p -- p_%p [label=\"%d\", penwidth=\"%f\"];\n", tp, tn, tn->count, MAX((log(tn->count)-3)*2,1));
	    if (depth <= 11)
		dump_trie(tn, depth+1);
	}
#endif
    }
}
#endif

int search_trie(name_context *ctx, char *data, size_t len, int n, int *exact, int *is_fixed, int *fixed_len) {
    int nlines = 0;
    size_t i, j = -1;
    trie_t *t;
    int from = -1, p3 = -1;

    // Horrid hack for the encoder only.
    // We optimise per known name format here.
    int prefix_len;
    char *d = *data == '@' ? data+1 : data;
    int l   = *data == '@' ? len-1  : len;
    int f = (*data == '>') ? 1 : 0;
    if (l > 70 && d[f+0] == 'm' && d[7] == '_' && d[f+14] == '_' && d[f+61] == '/') {
	prefix_len = 60; // PacBio
	*is_fixed = 0;
    } else if (l == 17 && d[f+5] == ':' && d[f+11] == ':') {
	prefix_len = 7;  // IonTorrent
	*fixed_len = 7;
	*is_fixed = 1;
    } else if (l > 37 && d[f+8] == '-' && d[f+13] == '-' && d[f+18] == '-' && d[f+23] == '-' &&
	       ((d[f+0] >= '0' && d[f+0] <='9') || (d[f+0] >= 'a' && d[f+0] <= 'f')) &&
	       ((d[f+35] >= '0' && d[f+35] <='9') || (d[f+35] >= 'a' && d[f+35] <= 'f'))) {
	// ONT: f33d30d5-6eb8-4115-8f46-154c2620a5da_Basecall_1D_template...
	prefix_len = 37;
	*fixed_len = 37;
	*is_fixed = 1;
    } else {
	// Anything else we give up on the trie method, but we still want to search
	// for exact matches;
	prefix_len = INT_MAX;
	*is_fixed = 0;
    }
    //prefix_len = INT_MAX;

    if (!ctx->t_head)
	ctx->t_head = calloc(1, sizeof(*ctx->t_head));

    // Find an item in the trie
    for (nlines = i = 0; i < len; i++, nlines++) {
	t = ctx->t_head;
	while (i < len && data[i] > '\n') {
	    unsigned char c = data[i++];
	    if (c & 0x80)
		//fprintf(stderr, "8-bit ASCII is unsupported\n");
		abort();
	    c &= 127;

	    trie_t *x = t->next;
	    while (x && x->c != c)
		x = x->sibling;
	    t = x;

//	    t = t->next[c];

	    from = t->n;
	    if (i == prefix_len) p3 = t->n;
	    //if (t->count >= .0035*ctx->t_head->count && t->n != n) p3 = t->n; // pacbio
	    //if (i == 60) p3 = t->n; // pacbio
	    //if (i == 7) p3 = t->n; // iontorrent
	    t->n = n;
	}
    }

    //printf("Looked for %d, found %d, prefix %d\n", n, from, p3);

    *exact = (n != from);
    return *exact ? from : p3;
}


//-----------------------------------------------------------------------------
// Name encoder

/*
 * Tokenises a read name using ctx as context as the previous
 * tokenisation.
 *
 * Parsed elements are then emitted for encoding by calling the
 * encode_token() function with the context, token number (Nth token
 * in line), token type and token value.
 *
 * Returns 0 on success;
 *        -1 on failure.
 */
static int encode_name(name_context *ctx, char *name, int len) {
    int i, is_fixed, fixed_len;

    int exact;
    int cnum = ctx->counter++;
    int pnum = search_trie(ctx, name, len, cnum, &exact, &is_fixed, &fixed_len);
    if (pnum < 0) pnum = cnum ? cnum-1 : 0;
    //pnum = pnum & (MAX_NAMES-1);
    //cnum = cnum & (MAX_NAMES-1);
    //if (pnum == cnum) {pnum = cnum ? cnum-1 : 0;}
#ifdef ENC_DEBUG
    fprintf(stderr, "%d: pnum=%d (%d), exact=%d\n%s\n%s\n",
	    ctx->counter, pnum, cnum-pnum, exact, ctx->lc[pnum].last_name, name);
#endif

    // Return DUP or DIFF switch, plus the distance.
    if (exact && len == strlen(ctx->lc[pnum].last_name)) {
	encode_token_dup(ctx, cnum-pnum);
	ctx->lc[cnum].last_name = name;
	ctx->lc[cnum].last_ntok = ctx->lc[pnum].last_ntok;
	// FIXME: optimise this
	int nc = ctx->lc[cnum].last_ntok ? ctx->lc[cnum].last_ntok : MAX_TOKENS;
	memcpy(ctx->lc[cnum].last_token_type, ctx->lc[pnum].last_token_type, nc * sizeof(int));
	memcpy(ctx->lc[cnum].last_token_int , ctx->lc[pnum].last_token_int , nc * sizeof(int));
	memcpy(ctx->lc[cnum].last_token_str , ctx->lc[pnum].last_token_str , nc * sizeof(int));
	return 0;
    }

    encode_token_diff(ctx, cnum-pnum);

    int ntok = 1;
    i = 0;
    if (is_fixed) {
	if (pnum < cnum && ntok < ctx->lc[pnum].last_ntok && ctx->lc[pnum].last_token_type[ntok] == N_ALPHA) {
	    if (ctx->lc[pnum].last_token_int[ntok] == fixed_len && memcmp(name, ctx->lc[pnum].last_name, fixed_len) == 0) {
		encode_token_match(ctx, ntok);
	    } else {
		encode_token_alpha(ctx, ntok, name, fixed_len);
	    }
	} else {
	    encode_token_alpha(ctx, ntok, name, fixed_len);
	}
	ctx->lc[cnum].last_token_int[ntok] = fixed_len;
	ctx->lc[cnum].last_token_str[ntok] = 0;
	ctx->lc[cnum].last_token_type[ntok++] = N_ALPHA;
	i = fixed_len;
    }

    for (; i < len; i++) {
	/* Determine data type of this segment */
	if (isalpha(name[i])) {
	    int s = i+1;

	    // FIXME: try which of these is best.  alnum is good sometimes.
	    //putchar(name[i]);
	    while (s < len && isalpha(name[s]))
	    //while (s < len && isalnum(name[s]))
		//putchar(name[s]),
		s++;

	    // Single byte strings are better encoded as chars.
	    if (s-i == 1) goto n_char;

	    if (pnum < cnum && ntok < ctx->lc[pnum].last_ntok && ctx->lc[pnum].last_token_type[ntok] == N_ALPHA) {
		if (s-i == ctx->lc[pnum].last_token_int[ntok] &&
		    memcmp(&name[i], 
			   &ctx->lc[pnum].last_name[ctx->lc[pnum].last_token_str[ntok]],
			   s-i) == 0) {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (alpha-mat, %.*s)\n", N_MATCH, s-i, &name[i]);
#endif
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		} else {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (alpha, %.*s / %.*s)\n", N_ALPHA,
		    	    s-i, &ctx->lc[pnum].last_name[ctx->lc[pnum].last_token_str[ntok]], s-i, &name[i]);
#endif
		    // same token/length, but mismatches
		    if (encode_token_alpha(ctx, ntok, &name[i], s-i) < 0) return -1;
		}
	    } else {
#ifdef ENC_DEBUG
		fprintf(stderr, "Tok %d (new alpha, %.*s)\n", N_ALPHA, s-i, &name[i]);
#endif
		if (encode_token_alpha(ctx, ntok, &name[i], s-i) < 0) return -1;
	    }

	    ctx->lc[cnum].last_token_int[ntok] = s-i;
	    ctx->lc[cnum].last_token_str[ntok] = i;
	    ctx->lc[cnum].last_token_type[ntok] = N_ALPHA;

	    i = s-1;
	} else if (name[i] == '0') digits0: {
	    // Digits starting with zero; encode length + value
	    uint32_t s = i;
	    uint32_t v = 0;
	    int d = 0;

	    while (s < len && isdigit(name[s]) && s-i < 8) {
		v = v*10 + name[s] - '0';
		//putchar(name[s]);
		s++;
	    }

	    // TODO: optimise choice over whether to switch from DIGITS to DELTA
	    // regularly vs all DIGITS, also MATCH vs DELTA 0.
	    if (pnum < cnum && ntok < ctx->lc[pnum].last_ntok && ctx->lc[pnum].last_token_type[ntok] == N_DIGITS0) {
		d = v - ctx->lc[pnum].last_token_int[ntok];
		if (d == 0 && ctx->lc[pnum].last_token_str[ntok] == s-i) {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (dig-mat, %d)\n", N_MATCH, v);
#endif
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		    //ctx->lc[pnum].last_token_delta[ntok]=0;
		} else if (d < 256 && d >= 0 && ctx->lc[pnum].last_token_str[ntok] == s-i) {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (dig-delta, %d / %d)\n", N_DDELTA, ctx->lc[pnum].last_token_int[ntok], v);
#endif
		    //if (encode_token_int1_(ctx, ntok, N_DZLEN, s-i) < 0) return -1;
		    if (encode_token_int1(ctx, ntok, N_DDELTA0, d) < 0) return -1;
		    //ctx->lc[pnum].last_token_delta[ntok]=1;
		} else {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (dig, %d / %d)\n", N_DIGITS, ctx->lc[pnum].last_token_int[ntok], v);
#endif
		    if (encode_token_int1_(ctx, ntok, N_DZLEN, s-i) < 0) return -1;
		    if (encode_token_int(ctx, ntok, N_DIGITS0, v) < 0) return -1;
		    //ctx->lc[pnum].last_token_delta[ntok]=0;
		}
	    } else {
#ifdef ENC_DEBUG
		fprintf(stderr, "Tok %d (new dig, %d)\n", N_DIGITS, v);
#endif
		if (encode_token_int1_(ctx, ntok, N_DZLEN, s-i) < 0) return -1;
		if (encode_token_int(ctx, ntok, N_DIGITS0, v) < 0) return -1;
		//ctx->lc[pnum].last_token_delta[ntok]=0;
	    }

	    ctx->lc[cnum].last_token_str[ntok] = s-i; // length
	    ctx->lc[cnum].last_token_int[ntok] = v;
	    ctx->lc[cnum].last_token_type[ntok] = N_DIGITS0;

	    i = s-1;
	} else if (isdigit(name[i])) {
	    // digits starting 1-9; encode value
	    uint32_t s = i;
	    uint32_t v = 0;
	    int d = 0;

	    while (s < len && isdigit(name[s]) && s-i < 8) {
		v = v*10 + name[s] - '0';
		//putchar(name[s]);
		s++;
	    }

	    // If the last token was DIGITS0 and we are the same length, then encode
	    // using that method instead as it seems likely the entire column is fixed
	    // width, sometimes with leading zeros.
	    if (pnum < cnum && ntok < ctx->lc[pnum].last_ntok &&
		ctx->lc[pnum].last_token_type[ntok] == N_DIGITS0 &&
		ctx->lc[pnum].last_token_str[ntok] == s-i)
		goto digits0;
	    
	    // TODO: optimise choice over whether to switch from DIGITS to DELTA
	    // regularly vs all DIGITS, also MATCH vs DELTA 0.
	    if (pnum < cnum && ntok < ctx->lc[pnum].last_ntok && ctx->lc[pnum].last_token_type[ntok] == N_DIGITS) {
		d = v - ctx->lc[pnum].last_token_int[ntok];
		if (d == 0) {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (dig-mat, %d)\n", N_MATCH, v);
#endif
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		    //ctx->lc[pnum].last_token_delta[ntok]=0;
		} else if (d < 256 && d >= 0) {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (dig-delta, %d / %d)\n", N_DDELTA, ctx->lc[pnum].last_token_int[ntok], v);
#endif
		    if (encode_token_int1(ctx, ntok, N_DDELTA, d) < 0) return -1;
		    //ctx->lc[pnum].last_token_delta[ntok]=1;
		} else {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (dig, %d / %d)\n", N_DIGITS, ctx->lc[pnum].last_token_int[ntok], v);
#endif
		    if (encode_token_int(ctx, ntok, N_DIGITS, v) < 0) return -1;
		    //ctx->lc[pnum].last_token_delta[ntok]=0;
		}
	    } else {
#ifdef ENC_DEBUG
		fprintf(stderr, "Tok %d (new dig, %d)\n", N_DIGITS, v);
#endif
		if (encode_token_int(ctx, ntok, N_DIGITS, v) < 0) return -1;
		//ctx->lc[pnum].last_token_delta[ntok]=0;
	    }

	    ctx->lc[cnum].last_token_int[ntok] = v;
	    ctx->lc[cnum].last_token_type[ntok] = N_DIGITS;

	    i = s-1;
	} else {
	n_char:
	    //if (!isalpha(name[i])) putchar(name[i]);
	    if (pnum < cnum && ntok < ctx->lc[pnum].last_ntok && ctx->lc[pnum].last_token_type[ntok] == N_CHAR) {
		if (name[i] == ctx->lc[pnum].last_token_int[ntok]) {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (chr-mat, %c)\n", N_MATCH, name[i]);
#endif
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		} else {
#ifdef ENC_DEBUG
		    fprintf(stderr, "Tok %d (chr, %c / %c)\n", N_CHAR, ctx->lc[pnum].last_token_int[ntok], name[i]);
#endif
		    if (encode_token_char(ctx, ntok, name[i]) < 0) return -1;
		}
	    } else {
#ifdef ENC_DEBUG
		fprintf(stderr, "Tok %d (new chr, %c)\n", N_CHAR, name[i]);
#endif
		if (encode_token_char(ctx, ntok, name[i]) < 0) return -1;
	    }

	    ctx->lc[cnum].last_token_int[ntok] = name[i];
	    ctx->lc[cnum].last_token_type[ntok] = N_CHAR;
	}

	ntok++;
	//putchar(' ');
    }

#ifdef ENC_DEBUG
    fprintf(stderr, "Tok %d (end)\n", N_END);
#endif
    if (encode_token_end(ctx, ntok) < 0) return -1;

    //printf("Encoded %.*s with %d tokens\n", len, name, ntok);
    
    ctx->lc[cnum].last_name = name;
    ctx->lc[cnum].last_ntok = ntok;

    return 0;
}

//-----------------------------------------------------------------------------
// Name decoder

// FIXME: should know the maximum name length for safety.
static int decode_name(name_context *ctx, char *name) {
    int t0 = decode_token_type(ctx, 0);
    uint32_t dist;
    int pnum, cnum = ctx->counter++;

    if (t0 < 0)
	return 0;

    decode_token_int(ctx, 0, t0, &dist);
    if ((pnum = cnum - dist) < 0) pnum = 0;

    //fprintf(stderr, "t0=%d, dist=%d, pnum=%d, cnum=%d\n", t0, dist, pnum, cnum);

    if (t0 == N_DUP) {
	strcpy(name, ctx->lc[pnum].last_name);
	// FIXME: optimise this
	ctx->lc[cnum].last_name = name;
	ctx->lc[cnum].last_ntok = ctx->lc[pnum].last_ntok;
	int nc = ctx->lc[cnum].last_ntok ? ctx->lc[cnum].last_ntok : MAX_TOKENS;
	memcpy(ctx->lc[cnum].last_token_type, ctx->lc[pnum].last_token_type, nc * sizeof(int));
	memcpy(ctx->lc[cnum].last_token_int , ctx->lc[pnum].last_token_int , nc * sizeof(int));
	memcpy(ctx->lc[cnum].last_token_str , ctx->lc[pnum].last_token_str , nc * sizeof(int));

	return strlen(name);
    }

    *name = 0;
    int ntok, len = 0, len2;

    for (ntok = 1; ntok < MAX_TOKENS; ntok++) {
	uint32_t v, vl;
	enum name_type tok;
	tok = decode_token_type(ctx, ntok);
	//fprintf(stderr, "Tok %d = %d\n", ntok, tok);

	switch (tok) {
	case N_CHAR:
	    decode_token_char(ctx, ntok, &name[len]);
	    //fprintf(stderr, "Tok %d CHAR %c\n", ntok, name[len]);
	    ctx->lc[cnum].last_token_type[ntok] = N_CHAR;
	    ctx->lc[cnum].last_token_int [ntok] = name[len++];
	    break;

	case N_ALPHA:
	    len2 = decode_token_alpha(ctx, ntok, &name[len]);
	    //fprintf(stderr, "Tok %d ALPHA %.*s\n", ntok, len2, &name[len]);
	    ctx->lc[cnum].last_token_type[ntok] = N_ALPHA;
	    ctx->lc[cnum].last_token_str [ntok] = len;
	    ctx->lc[cnum].last_token_int [ntok] = len2;
	    len += len2;
	    break;

	case N_DIGITS0: // [0-9]*
	    decode_token_int1(ctx, ntok, N_DZLEN, &vl);
	    decode_token_int(ctx, ntok, N_DIGITS0, &v);
	    len += append_uint32_fixed(&name[len], v, vl);
	    //fprintf(stderr, "Tok %d DIGITS0 %0*d\n", ntok, vl, v);
	    ctx->lc[cnum].last_token_type[ntok] = N_DIGITS0;
	    ctx->lc[cnum].last_token_int [ntok] = v;
	    ctx->lc[cnum].last_token_str [ntok] = vl;
	    break;

	case N_DDELTA0:
	    decode_token_int1(ctx, ntok, N_DDELTA0, &v);
	    v += ctx->lc[pnum].last_token_int[ntok];
	    len += append_uint32_fixed(&name[len], v, ctx->lc[pnum].last_token_str[ntok]);
	    //fprintf(stderr, "Tok %d DELTA0 %0*d\n", ntok, ctx->lc[pnum].last_token_str[ntok], v);
	    ctx->lc[cnum].last_token_type[ntok] = N_DIGITS0;
	    ctx->lc[cnum].last_token_int [ntok] = v;
	    ctx->lc[cnum].last_token_str [ntok] = ctx->lc[pnum].last_token_str[ntok];
	    break;

	case N_DIGITS: // [1-9][0-9]*
	    decode_token_int(ctx, ntok, N_DIGITS, &v);
	    len += append_uint32_var(&name[len], v);
	    //fprintf(stderr, "Tok %d DIGITS %d\n", ntok, v);
	    ctx->lc[cnum].last_token_type[ntok] = N_DIGITS;
	    ctx->lc[cnum].last_token_int [ntok] = v;
	    break;

	case N_DDELTA:
	    decode_token_int1(ctx, ntok, N_DDELTA, &v);
	    v += ctx->lc[pnum].last_token_int[ntok];
	    len += append_uint32_var(&name[len], v);
	    //fprintf(stderr, "Tok %d DELTA %d\n", ntok, v);
	    ctx->lc[cnum].last_token_type[ntok] = N_DIGITS;
	    ctx->lc[cnum].last_token_int [ntok] = v;
	    break;

	case N_MATCH:
	    switch (ctx->lc[pnum].last_token_type[ntok]) {
	    case N_CHAR:
		name[len++] = ctx->lc[pnum].last_token_int[ntok];
		//fprintf(stderr, "Tok %d MATCH CHAR %c\n", ntok, ctx->lc[pnum].last_token_int[ntok]);
		ctx->lc[cnum].last_token_type[ntok] = N_CHAR;
		ctx->lc[cnum].last_token_int [ntok] = ctx->lc[pnum].last_token_int[ntok];
		break;

	    case N_ALPHA:
		memcpy(&name[len],
		       &ctx->lc[pnum].last_name[ctx->lc[pnum].last_token_str[ntok]],
		       ctx->lc[pnum].last_token_int[ntok]);
		//fprintf(stderr, "Tok %d MATCH ALPHA %.*s\n", ntok, ctx->lc[pnum].last_token_int[ntok], &name[len]);
		ctx->lc[cnum].last_token_type[ntok] = N_ALPHA;
		ctx->lc[cnum].last_token_str [ntok] = len;
		ctx->lc[cnum].last_token_int [ntok] = ctx->lc[pnum].last_token_int[ntok];
		len += ctx->lc[pnum].last_token_int[ntok];
		break;

	    case N_DIGITS:
		len += append_uint32_var(&name[len], ctx->lc[pnum].last_token_int[ntok]);
		//fprintf(stderr, "Tok %d MATCH DIGITS %d\n", ntok, ctx->lc[pnum].last_token_int[ntok]);
		ctx->lc[cnum].last_token_type[ntok] = N_DIGITS;
		ctx->lc[cnum].last_token_int [ntok] = ctx->lc[pnum].last_token_int[ntok];
		break;

	    case N_DIGITS0:
		len += append_uint32_fixed(&name[len], ctx->lc[pnum].last_token_int[ntok], ctx->lc[pnum].last_token_str[ntok]);
		//fprintf(stderr, "Tok %d MATCH DIGITS %0*d\n", ntok, ctx->lc[pnum].last_token_str[ntok], ctx->lc[pnum].last_token_int[ntok]);
		ctx->lc[cnum].last_token_type[ntok] = N_DIGITS0;
		ctx->lc[cnum].last_token_int [ntok] = ctx->lc[pnum].last_token_int[ntok];
		ctx->lc[cnum].last_token_str [ntok] = ctx->lc[pnum].last_token_str[ntok];
		break;

	    default:
		abort();
	    }
	    break;

	case N_END:
	    name[len++] = 0;
	    ctx->lc[cnum].last_token_type[ntok] = N_END;
	    // FIXME: avoid using memcpy, just keep pointer into buffer?
	    ctx->lc[cnum].last_name = name;
	    ctx->lc[cnum].last_ntok = ntok;
	    
	    return len;

	default:
	    return 0; // eof we hope!
	}
    }


    return -1;
}

// Large enough for whole file for now.
#define BLK_SIZE 1*1024*1024
static char blk[BLK_SIZE*2]; // temporary fix for decoder, which needs more space

static int decode(int argc, char **argv) {
    uint32_t sz;
    while (fread(&sz, 1, 4, stdin) == 4) {
	uint8_t *in = malloc(sz);
	if (!in)
	    return -1;

	if (fread(in, 1, sz, stdin) != sz)
	    return -1;

	name_context *ctx;
	char *line;
	int i, c, o = 0;

	// Unpack descriptors
	int tnum = -1;
	while (o < sz) {
	    uint8_t ttype = in[o++];
	    if (ttype == 255) {
		uint16_t j = *(uint16_t *)&in[o];
		o += 2;
		ttype = in[o++];
		if (ttype == 0)
		    tnum++;
		i = (tnum<<4) | ttype;

		desc[i].buf_l = 0;
		desc[i].buf_a = desc[j].buf_a;
		desc[i].buf = malloc(desc[i].buf_a);
		memcpy(desc[i].buf, desc[j].buf, desc[i].buf_a);
		//fprintf(stderr, "Copy ttype %d, i=%d,j=%d, size %d\n", ttype, i, j, (int)desc[i].buf_a);
		continue;
	    }

	    if (ttype == 0)
		tnum++;

	    //fprintf(stderr, "Read %02x\n", c);

	    // Load compressed block
	    uint8_t ctype[10];
	    int nb;
	    uint64_t clen, ulen = uncompressed_size(&in[o], sz-o);
	    if (ulen < 0)
		return -1;
	    i = (tnum<<4) | ttype;

	    desc[i].buf_l = 0;
	    desc[i].buf = malloc(ulen);

	    desc[i].buf_a = ulen;
	    clen = uncompress(&in[o], sz-o, desc[i].buf, &desc[i].buf_a);
	    assert(desc[i].buf_a == ulen);

//	    fprintf(stderr, "%d: Decode tnum %d type %d clen %d ulen %d via %d\n",
//		    o, tnum, ttype, (int)clen, (int)desc[i].buf_a, desc[i].buf[0]);

	    o += clen;

	    // Encode tnum 0 type 0 ulen 100000 clen 12530 via 2
	    // Encode tnum 0 type 6 ulen 196800 clen 43928 via 3
	    // Encode tnum 0 type 7 ulen 203200 clen 17531 via 3
	    // Encode tnum 1 type 0 ulen 50800 clen 10 via 1
	    // Encode tnum 1 type 1 ulen 3 clen 5 via 0
	    // Encode tnum 2 type 0 ulen 50800 clen 10 via 1
	    // 	
	}

	int ret;
	ctx = create_context(MAX_NAMES);

	line = blk;
	while ((ret = decode_name(ctx, line)) > 0) {
	    puts(line);
	    line += ret+1;
	}

	free_context(ctx);

	for (i = 0; i < MAX_DESCRIPTORS; i++) {
	    if (desc[i].buf) {
		free(desc[i].buf);
		desc[i].buf = 0;
	    }
	}

	if (ret < 0)
	    return -1;

	free(in);
    }

    return 0;
}

static int encode(int argc, char **argv) {
    FILE *fp;
    char *prefix = "stdin";
    int len, i, j;
    name_context *ctx;

    if (argc > 1) {
	fp = fopen(argv[1], "r");
	if (!fp) {
	    perror(argv[1]);
	    return 1;
	}
	prefix = argv[1];
    } else {
	fp = stdin;
    }

    int blk_offset = 0;
    int blk_num = 0;
    for (;;) {
	int last_start = 0;

	memset(&desc[0], 0, MAX_DESCRIPTORS * sizeof(desc[0]));

	ctx = create_context(MAX_NAMES);

	len = fread(blk+blk_offset, 1, BLK_SIZE-blk_offset, fp);
	if (len <= 0)
	    break;

	// Construct trie
	int ctr = 0;
	len += blk_offset;
	for (i = j = 0; i < len; j=++i) {
	    while (i < len && blk[i] != '\n')
		i++;
	    if (blk[i] != '\n')
		break;

	    //blk[i] = '\0';
	    last_start = i+1;
	    build_trie(ctx, &blk[j], i-j, ctr++);
	}

	//fprintf(stderr, "Processed %d of %d in block, line %d\n", last_start, len, ctr);

	// Encode name
	for (i = j = 0; i < len; j=++i) {
	    while (i < len && blk[i] != '\n')
		i++;
	    if (blk[i] != '\n')
		break;

	    blk[i] = '\0';
	    if (encode_name(ctx, &blk[j], i-j) < 0)
		return 1;
	}

	//dump_trie(t_head, 0);

	// Serialise descriptors
	int last_tnum = -1;
	uint32_t tot_size = 0;
	int ndesc = 0;
	for (i = 0; i < MAX_DESCRIPTORS; i++) {
	    if (!desc[i].buf_l) continue;

	    ndesc++;

	    int tnum = i>>4;
	    int ttype = i&15;

	    if (ttype == 0) {
		assert(tnum == last_tnum+1);
		last_tnum = tnum;
	    }

	    uint64_t out_len = 1.5 * rans_compress_bound_4x16(desc[i].buf_l, 1); // guesswork
	    uint8_t *out = malloc(out_len);
	    assert(out);

	    //uint8_t ttype8 = ttype;
	    //write(1, &ttype8, 1);

	    if (compress(desc[i].buf, desc[i].buf_l, out, &out_len, 0) < 0)
		abort();

	    free(desc[i].buf);
	    desc[i].buf = out;
	    desc[i].buf_l = out_len;
	    desc[i].tnum = tnum;
	    desc[i].ttype = ttype;

	    // Find dups
	    int j;
	    for (j = 0; j < i; j++) {
		if (!desc[j].buf)
		    continue;
		if (desc[i].buf_l != desc[j].buf_l)
		    continue;
		if (memcmp(desc[i].buf, desc[j].buf, desc[i].buf_l) == 0)
		    break;
	    }
	    if (j < i) {
		//fprintf(stderr, "Dup %d %d size %d\n", i, j, (int)desc[i].buf_l);
		desc[i].dup_from = j;
		tot_size += 4; // flag, dup_from, ttype
		//fprintf(stderr, "Desc %d %d/%d => DUP %d\n", i, tnum, ttype, j);
	    } else {
		desc[i].dup_from = 0;
		tot_size += out_len + 1; // ttype
		//fprintf(stderr, "Desc %d %d/%d => %d\n", i, tnum, ttype, (int)desc[i].buf_l);
	    }
	    
//	    fprintf(stderr, "Encode tnum %d type %d ulen %d clen %d via %d\n",
//		    tnum, ttype, (int)desc[i].buf_l, (int)out_len, *out);

	    //if (out_len != write(1, out, out_len))
	    //    abort();

	    //free(out);
	    //free(desc[i].buf);
	}
	//fprintf(stderr, "Serialised %d descriptors\n", ndesc);

	// Write
	write(1, &tot_size, 4);
	for (i = 0; i < MAX_DESCRIPTORS; i++) {
	    if (!desc[i].buf_l) continue;
	    uint8_t ttype8 = desc[i].ttype;
	    if (desc[i].dup_from) {
		uint8_t x = 255;
		write(1, &x, 1);
		uint16_t y = desc[i].dup_from;
		write(1, &y, 2);
		write(1, &ttype8, 1);
	    } else {
		write(1, &ttype8, 1);
		write(1, desc[i].buf, desc[i].buf_l);
	    }
	}

	for (i = 0; i < MAX_DESCRIPTORS; i++) {
	    if (!desc[i].buf_l) continue;
	    free(desc[i].buf);
	}

	free_context(ctx);

	if (len > last_start)
	    memmove(blk, &blk[last_start], len - last_start);
	blk_offset = len - last_start;
	blk_num++;
    }

    if (fclose(fp) < 0) {
	perror("closing file");
	return 1;
    }

    return 0;
}

int main(int argc, char **argv) {

    if (argc > 1 && strcmp(argv[1], "-d") == 0)
	return decode(argc-1, argv+1);
    else
	return encode(argc, argv);
}

