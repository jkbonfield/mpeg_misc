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

#include "khash.h"
KHASH_MAP_INIT_STR(s2i, int)
KHASH_MAP_INIT_INT(i2s, char *)

// FIXME
#define MAX_TOKENS 64
#define MAX_DESCRIPTORS (MAX_TOKENS<<4)

// Number of names per block
#define MAX_NAMES 100000

enum name_type {N_ERR = -1, N_TYPE = 0, N_ALPHA, N_CHAR, N_DZLEN, N_DIGITS0, N_Z1, N_DUP, N_DIFF, 
		N_DIGITS, N_D1, N_D2, N_D3, N_DDELTA, N_DDELTA0, N_MATCH, N_END};

char *types[]={"TYPE", "ALPHA", "CHAR", "DZLEN", "DIG0", "DUP", "DIFF",
	       "DIGITS", "", "", "", "DDELTA", "DDELTA0", "MATCH", "END"};

typedef struct trie trie_t;

typedef struct {
    char *last_name[MAX_NAMES];

    int last_ntok[MAX_NAMES];
    enum name_type last_token_type[MAX_NAMES][MAX_TOKENS];
    int last_token_int  [MAX_NAMES][MAX_TOKENS]; // decoded integer
    int last_token_str  [MAX_NAMES][MAX_TOKENS]; // offset into name
    int last_token_delta[MAX_NAMES][MAX_TOKENS]; // boolean

    // For finding entire line dups
    khash_t(s2i) *name_s;
    khash_t(i2s) *name_i;
    int counter;

    // Trie used in encoder only
    trie_t *t_head;
} name_context;


typedef struct {
    uint8_t *buf;
    size_t buf_a, buf_l; // alloc and used length.
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

static int append_uint32(char *cp, uint32_t i) {
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
    struct trie *next[128];
    int n; // Nth line
} trie_t;

//static trie_t *t_head = NULL;

void free_trie(trie_t *t) {
    int j;
    for (j = 0; j < 128; j++) {
	if (t->next[j])
	    free_trie(t->next[j]);
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
	    if (!t->next[c])  {
		//t->next[c] = (trie_t *)pool_calloc(t_pool, sizeof(*t));
		t->next[c] = calloc(1, sizeof(*t));
		t->next[c]->n = n;
	    }
	    t = t->next[c];
	    t->c = c;
	    t->count++;
	}
    }

    return 0;
}

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

#define MAX(a,b) ((a)>(b)?(a):(b))

	    printf("    p_%p [label=\"%s\"];\n", tn, label);
	    printf("    p_%p -- p_%p [label=\"%d\", penwidth=\"%f\"];\n", tp, tn, tn->count, MAX((log(tn->count)-3)*2,1));
	    if (depth <= 11)
		dump_trie(tn, depth+1);
	}
    }
}

int search_trie(name_context *ctx, char *data, size_t len, int n, int *exact, int *is_iontorrent) {
    int nlines = 0;
    size_t i, j = -1;
    trie_t *t;
    int from = -1, p3 = -1;

    // Horrid hack for the encoder only.
    // We optimise per known name format here.
    int prefix_len;
    char *d = *data == '@' ? data+1 : data;
    int l   = *data == '@' ? len-1  : len;
    if (l > 70 && d[0] == 'm' && d[7] == '_' && d[14] == '_' && d[61] == '/') {
	prefix_len = 60; // PacBio
	*is_iontorrent = 0;
    } else if (l == 17 && d[5] == ':' && d[11] == ':') {
	prefix_len = 7;  // IonTorrent
	*is_iontorrent = 1;
    } else {
	// Anything else we give up on the trie method, but we still want to search
	// for exact matches;
	prefix_len = INT_MAX;
	*is_iontorrent = 0;
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
	    assert(t->next[c]);
	    if (!t->next[c])  {
		//t->next[c] = (trie_t *)pool_calloc(t_pool, sizeof(*t));
		t->next[c] = calloc(1, sizeof(*t));
		if (j == -1)
		    j = i;
	    }
	    t = t->next[c];
	    from = t->n;
	    if (i == prefix_len) p3 = t->n;
	    //if (t->count >= .0035*t_head->count && t->n != n) p3 = t->n; // pacbio
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
int encode_name(name_context *ctx, char *name, int len) {
    int i, is_iontorrent;

    int exact;
    int cnum = ctx->counter++;
    int pnum = search_trie(ctx, name, len, cnum, &exact, &is_iontorrent);
    if (pnum < 0) pnum = cnum ? cnum-1 : 0;
//    printf("%d: pnum=%d (%d), exact=%d\n%s\n%s\n",
//	   ctx->counter, pnum, cnum-pnum, exact, ctx->last_name[pnum], name);

    // Return DUP or DIFF switch, plus the distance.
    if (exact) {
	encode_token_dup(ctx, cnum-pnum);
	ctx->last_name[cnum] = name;
	ctx->last_ntok[cnum] = ctx->last_ntok[pnum];
	// FIXME: optimise this
	memcpy(ctx->last_token_type[cnum], ctx->last_token_type[pnum], MAX_TOKENS * sizeof(int));
	memcpy(ctx->last_token_int [cnum], ctx->last_token_int [pnum], MAX_TOKENS * sizeof(int));
	memcpy(ctx->last_token_str [cnum], ctx->last_token_str [pnum], MAX_TOKENS * sizeof(int));
	return 0;
    }

    encode_token_diff(ctx, cnum-pnum);

    int ntok = 1;
    i = 0;
#define IT_LEN 7
    if (is_iontorrent) {
	if (ntok < ctx->last_ntok[pnum] && ctx->last_token_type[pnum][ntok] == N_ALPHA) {
	    if (ctx->last_token_int[pnum][ntok] == IT_LEN && memcmp(name, ctx->last_name[pnum], IT_LEN) == 0) {
		encode_token_match(ctx, ntok);
	    } else {
		encode_token_alpha(ctx, ntok, name, IT_LEN);
	    }
	} else {
	    encode_token_alpha(ctx, ntok, name, IT_LEN);
	}
	ctx->last_token_int[cnum][ntok] = IT_LEN;
	ctx->last_token_str[cnum][ntok] = 0;
	ctx->last_token_type[cnum][ntok++] = N_ALPHA;
	i = IT_LEN;
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

	    if (ntok < ctx->last_ntok[pnum] && ctx->last_token_type[pnum][ntok] == N_ALPHA) {
		if (s-i == ctx->last_token_int[pnum][ntok] &&
		    memcmp(&name[i], 
			   &ctx->last_name[pnum][ctx->last_token_str[pnum][ntok]],
			   s-i) == 0) {
		    //fprintf(stderr, "Tok %d (alpha-mat, %.*s)\n", N_MATCH, s-i, &name[i]);
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		} else {
		    //fprintf(stderr, "Tok %d (alpha, %.*s / %.*s)\n", N_ALPHA,
		    //	    s-i, &ctx->last_name[pnum][ctx->last_token_str[pnum][ntok]], s-i, &name[i]);
		    // same token/length, but mismatches
		    if (encode_token_alpha(ctx, ntok, &name[i], s-i) < 0) return -1;
		}
	    } else {
		//fprintf(stderr, "Tok %d (new alpha, %.*s)\n", N_ALPHA, s-i, &name[i]);
		if (encode_token_alpha(ctx, ntok, &name[i], s-i) < 0) return -1;
	    }

	    ctx->last_token_int[cnum][ntok] = s-i;
	    ctx->last_token_str[cnum][ntok] = i;
	    ctx->last_token_type[cnum][ntok] = N_ALPHA;

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
	    if (ntok < ctx->last_ntok[pnum] && ctx->last_token_type[pnum][ntok] == N_DIGITS0) {
		d = v - ctx->last_token_int[pnum][ntok];
		if (d == 0 && ctx->last_token_str[pnum][ntok] == s-i) {
		    //fprintf(stderr, "Tok %d (dig-mat, %d)\n", N_MATCH, v);
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		    //ctx->last_token_delta[pnum][ntok]=0;
		} else if (d < 256 && d >= 0 && ctx->last_token_str[pnum][ntok] == s-i) {
		    //fprintf(stderr, "Tok %d (dig-delta, %d / %d)\n", N_DDELTA, ctx->last_token_int[pnum][ntok], v);
		    //if (encode_token_int1_(ctx, ntok, N_DZLEN, s-i) < 0) return -1;
		    if (encode_token_int1(ctx, ntok, N_DDELTA0, d) < 0) return -1;
		    //ctx->last_token_delta[pnum][ntok]=1;
		} else {
		    //fprintf(stderr, "Tok %d (dig, %d / %d)\n", N_DIGITS, ctx->last_token_int[pnum][ntok], v);
		    if (encode_token_int1_(ctx, ntok, N_DZLEN, s-i) < 0) return -1;
		    if (encode_token_int(ctx, ntok, N_DIGITS0, v) < 0) return -1;
		    //ctx->last_token_delta[pnum][ntok]=0;
		}
	    } else {
		//fprintf(stderr, "Tok %d (new dig, %d)\n", N_DIGITS, v);
		if (encode_token_int1_(ctx, ntok, N_DZLEN, s-i) < 0) return -1;
		if (encode_token_int(ctx, ntok, N_DIGITS0, v) < 0) return -1;
		//ctx->last_token_delta[pnum][ntok]=0;
	    }

	    ctx->last_token_str[cnum][ntok] = s-i; // length
	    ctx->last_token_int[cnum][ntok] = v;
	    ctx->last_token_type[cnum][ntok] = N_DIGITS0;

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
	    if (ntok < ctx->last_ntok[pnum] &&
		ctx->last_token_type[pnum][ntok] == N_DIGITS0 &&
		ctx->last_token_str[pnum][ntok] == s-i)
		goto digits0;
	    
	    // TODO: optimise choice over whether to switch from DIGITS to DELTA
	    // regularly vs all DIGITS, also MATCH vs DELTA 0.
	    if (ntok < ctx->last_ntok[pnum] && ctx->last_token_type[pnum][ntok] == N_DIGITS) {
		d = v - ctx->last_token_int[pnum][ntok];
		if (d == 0) {
		    //fprintf(stderr, "Tok %d (dig-mat, %d)\n", N_MATCH, v);
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		    //ctx->last_token_delta[pnum][ntok]=0;
		} else if (d < 256 && d >= 0) {
		    //fprintf(stderr, "Tok %d (dig-delta, %d / %d)\n", N_DDELTA, ctx->last_token_int[pnum][ntok], v);
		    if (encode_token_int1(ctx, ntok, N_DDELTA, d) < 0) return -1;
		    //ctx->last_token_delta[pnum][ntok]=1;
		} else {
		    //fprintf(stderr, "Tok %d (dig, %d / %d)\n", N_DIGITS, ctx->last_token_int[pnum][ntok], v);
		    if (encode_token_int(ctx, ntok, N_DIGITS, v) < 0) return -1;
		    //ctx->last_token_delta[pnum][ntok]=0;
		}
	    } else {
		//fprintf(stderr, "Tok %d (new dig, %d)\n", N_DIGITS, v);
		if (encode_token_int(ctx, ntok, N_DIGITS, v) < 0) return -1;
		//ctx->last_token_delta[pnum][ntok]=0;
	    }

	    ctx->last_token_int[cnum][ntok] = v;
	    ctx->last_token_type[cnum][ntok] = N_DIGITS;

	    i = s-1;
	} else {
	n_char:
	    //if (!isalpha(name[i])) putchar(name[i]);
	    if (ntok < ctx->last_ntok[pnum] && ctx->last_token_type[pnum][ntok] == N_CHAR) {
		if (name[i] == ctx->last_token_int[pnum][ntok]) {
		    //fprintf(stderr, "Tok %d (chr-mat, %c)\n", N_MATCH, name[i]);
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		} else {
		    //fprintf(stderr, "Tok %d (chr, %c / %c)\n", N_CHAR, ctx->last_token_int[pnum][ntok], name[i]);
		    if (encode_token_char(ctx, ntok, name[i]) < 0) return -1;
		}
	    } else {
		//fprintf(stderr, "Tok %d (new chr, %c)\n", N_CHAR, name[i]);
		if (encode_token_char(ctx, ntok, name[i]) < 0) return -1;
	    }

	    ctx->last_token_int[cnum][ntok] = name[i];
	    ctx->last_token_type[cnum][ntok] = N_CHAR;
	}

	ntok++;
	//putchar(' ');
    }

    //fprintf(stderr, "Tok %d (end)\n", N_END);
    if (encode_token_end(ctx, ntok) < 0) return -1;

    //printf("Encoded %.*s with %d tokens\n", len, name, ntok);
    
    ctx->last_name[cnum] = name;
    ctx->last_ntok[cnum] = ntok;

    return 0;
}


//-----------------------------------------------------------------------------
// Name decoder

// FIXME: should know the maximum name length for safety.
int decode_name(name_context *ctx, char *name) {
    int t0 = decode_token_type(ctx, 0);
    uint32_t dist;
    int pnum, cnum = ctx->counter++;

    if (t0 < 0)
	return 0;

    decode_token_int(ctx, 0, t0, &dist);
    if ((pnum = cnum - dist) < 0) pnum = 0;

    //printf("t0=%d, dist=%d, pnum=%d, cnum=%d\n", t0, dist, pnum, cnum);

    if (t0 == N_DUP) {
	strcpy(name, ctx->last_name[pnum]);
	// FIXME: optimise this
	ctx->last_name[cnum] = name;
	ctx->last_ntok[cnum] = ctx->last_ntok[pnum];
	memcpy(ctx->last_token_type[cnum], ctx->last_token_type[pnum], MAX_TOKENS * sizeof(int));
	memcpy(ctx->last_token_int [cnum], ctx->last_token_int [pnum], MAX_TOKENS * sizeof(int));
	memcpy(ctx->last_token_str [cnum], ctx->last_token_str [pnum], MAX_TOKENS * sizeof(int));

	return strlen(name);
    }

    *name = 0;
    int ntok, len = 0, len2;

    for (ntok = 1; ntok < MAX_TOKENS; ntok++) {
	uint32_t v, vl;
	enum name_type tok;
	tok = decode_token_type(ctx, ntok);
	//printf("Tok %d = %d\n", ntok, tok);

	switch (tok) {
	case N_CHAR:
	    decode_token_char(ctx, ntok, &name[len]);
	    ctx->last_token_type[cnum][ntok] = N_CHAR;
	    ctx->last_token_int [cnum][ntok] = name[len++];
	    break;

	case N_ALPHA:
	    len2 = decode_token_alpha(ctx, ntok, &name[len]);
	    ctx->last_token_type[cnum][ntok] = N_ALPHA;
	    ctx->last_token_str [cnum][ntok] = len;
	    ctx->last_token_int [cnum][ntok] = len2;
	    len += len2;
	    break;

	case N_DIGITS0: // [0-9]*
	    decode_token_int1(ctx, ntok, N_DZLEN, &vl);
	    decode_token_int(ctx, ntok, N_DIGITS0, &v);
	    len += append_uint32_fixed(&name[len], v, vl);
	    ctx->last_token_type[cnum][ntok] = N_DIGITS0;
	    ctx->last_token_int [cnum][ntok] = v;
	    ctx->last_token_str [cnum][ntok] = vl;
	    break;

	case N_DDELTA0:
	    decode_token_int1(ctx, ntok, N_DDELTA0, &v);
	    v += ctx->last_token_int[pnum][ntok];
	    len += append_uint32_fixed(&name[len], v, ctx->last_token_str[pnum][ntok]);
	    ctx->last_token_type[cnum][ntok] = N_DIGITS0;
	    ctx->last_token_int [cnum][ntok] = v;
	    ctx->last_token_str [cnum][ntok] = ctx->last_token_str[pnum][ntok];
	    break;

	case N_DIGITS: // [1-9][0-9]*
	    decode_token_int(ctx, ntok, N_DIGITS, &v);
	    len += append_uint32(&name[len], v);
	    ctx->last_token_type[cnum][ntok] = N_DIGITS;
	    ctx->last_token_int [cnum][ntok] = v;
	    break;

	case N_DDELTA:
	    decode_token_int1(ctx, ntok, N_DDELTA, &v);
	    v += ctx->last_token_int[pnum][ntok];
	    len += append_uint32(&name[len], v);
	    ctx->last_token_type[cnum][ntok] = N_DIGITS;
	    ctx->last_token_int [cnum][ntok] = v;
	    break;

	case N_MATCH:
	    switch (ctx->last_token_type[pnum][ntok]) {
	    case N_CHAR:
		name[len++] = ctx->last_token_int[pnum][ntok];
		ctx->last_token_type[cnum][ntok] = N_CHAR;
		ctx->last_token_int [cnum][ntok] = ctx->last_token_int[pnum][ntok];
		break;

	    case N_ALPHA:
		memcpy(&name[len],
		       &ctx->last_name[pnum][ctx->last_token_str[pnum][ntok]],
		       ctx->last_token_int[pnum][ntok]);
		ctx->last_token_type[cnum][ntok] = N_ALPHA;
		ctx->last_token_str [cnum][ntok] = len;
		ctx->last_token_int [cnum][ntok] = ctx->last_token_int[pnum][ntok];
		len += ctx->last_token_int[pnum][ntok];
		break;

	    case N_DIGITS:
		len += append_uint32(&name[len], ctx->last_token_int[pnum][ntok]);
		ctx->last_token_type[cnum][ntok] = N_DIGITS;
		ctx->last_token_int [cnum][ntok] = ctx->last_token_int[pnum][ntok];
		break;

	    case N_DIGITS0:
		len += append_uint32_fixed(&name[len], ctx->last_token_int[pnum][ntok], ctx->last_token_str[pnum][ntok]);
		ctx->last_token_type[cnum][ntok] = N_DIGITS0;
		ctx->last_token_int [cnum][ntok] = ctx->last_token_int[pnum][ntok];
		ctx->last_token_str [cnum][ntok] = ctx->last_token_str[pnum][ntok];
		break;

	    default:
		abort();
	    }
	    break;

	case N_END:
	    name[len++] = 0;
	    ctx->last_token_type[cnum][ntok] = N_END;
	    // FIXME: avoid using memcpy, just keep pointer into buffer?
	    ctx->last_name[cnum] = name;
	    
	    return len;

	default:
	    return 0; // eof we hope!
	}
    }


    return -1;
}

// Large enough for whole file for now.
#define BLK_SIZE 10*1024*1024
static char blk[BLK_SIZE*2]; // temporary fix for decoder, which needs more space

static int decode(int argc, char **argv) {
    FILE *fp;
    char *prefix = "stdin";
    char *line;
    int i;
    name_context *ctx;

    if (argc > 1)
	prefix = argv[1];

    // Load descriptors
    for (i = 0; i < MAX_DESCRIPTORS; i++) {
	struct stat sb;
	char fn[1024]; // fixme
	sprintf(fn, "%s.%d_%d", prefix, i>>4, i&15);
	if (stat(fn, &sb) < 0)
	    continue;

	if (!(fp = fopen(fn, "r"))) {
	    perror(fn);
	    continue;
	}

	desc[i].buf_l = 0;
	desc[i].buf_a = sb.st_size;
	desc[i].buf = malloc(sb.st_size);
	assert(desc[i].buf);

	if (desc[i].buf_a != fread(desc[i].buf, 1, desc[i].buf_a, fp))
	    return -1;
	fclose(fp);
    }

    int ret;
    ctx = calloc(1, sizeof(*ctx));
    ctx->name_i = kh_init(i2s);

    line = blk;
    while ((ret = decode_name(ctx, line)) > 0) {
	puts(line);
	line += ret+1;
    }

    kh_destroy(i2s, ctx->name_i);
    free(ctx);

    for (i = 0; i < MAX_DESCRIPTORS; i++)
	if (desc[i].buf)
	    free(desc[i].buf);

    if (ret < 0)
	return -1;

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

    // FIXME: loop here
    int blk_offset = 0;
    int blk_num = 0;
    for (;;) {
	int last_start = 0;

	memset(&desc[0], 0, MAX_DESCRIPTORS * sizeof(desc[0]));

	ctx = calloc(1, sizeof(*ctx));
	ctx->name_s = kh_init(s2i);

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

	fprintf(stderr, "Processed %d of %d in block\n", last_start, len);

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

	// Write out descriptors
	for (i = 0; i < MAX_DESCRIPTORS; i++) {
	    if (!desc[i].buf_l) continue;

	    //printf("Des %d/%d: size %d\n", i>>4, i&15, (int)desc[i].buf_l);
	    char fn[1024]; // fixme
	    sprintf(fn, "%s.blk_%06d.%d_%d", prefix, blk_num, i>>4, i&15);
	    FILE *fp = fopen(fn, "w");
	    if (!fp) {
		perror(fn);
		return 1;
	    }
	    if (fwrite(desc[i].buf, 1, desc[i].buf_l, fp) != desc[i].buf_l) {
		perror(fn);
		return 1;
	    }
	    if (fclose(fp) < 0) {
		perror(fn);
		return 1;
	    }

	    free(desc[i].buf);
	}

	kh_destroy(s2i, ctx->name_s);
	free_trie(ctx->t_head);
	free(ctx);

	if (len > last_start) {
	    memmove(blk, &blk[last_start], len - last_start);
	    blk_offset = len - last_start;
	}
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

