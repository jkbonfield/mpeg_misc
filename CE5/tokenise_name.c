/*
100,000 names from ~/scratch/data/SRR608881.fastq.gz

raw:         4712441
fqzcomp -n1:  218738
fqzcomp -n2:  235817
this raw:    3390476
this | gzip:  217578
this | bzip2: 212000
this | rans0: 238074  // r4x16b, 246890 with r32x16b
mix of above: 199355  // min gzip, bzip2 & rans0 per desc.
this | rans*: 201734  // best of rans0 or rans1 per desc.
cmix          194565  // took 6897.19 seconds!

*/

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
// - Varous integer encoding methods are available.
//   Fixed 32-bit ints (this example code).
//   Variable sized encoding (eg zig-zag method).
//   4 discrete channels per byte in the int (fqzcomp did this).
//
// - Check this works with variable number of tokens per line.
//   Specifically using the previous token type as context will break.
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

typedef struct {
    char last_name[1024]; // Last name
    int last_name_len;    // Length of last name

    int last_ntok;
    int last_token_type[MAX_TOKENS];
    int last_token_int[MAX_TOKENS];  // decoded integer
    int last_token_str[MAX_TOKENS];  // offset into name
    int last_token_delta[MAX_TOKENS]; // boolean

    // For finding entire line dups
    khash_t(s2i) *name_s;
    khash_t(i2s) *name_i;
    int counter;
} name_context;

enum name_type {N_TYPE = 0, N_ALPHA/*, N_ALPHA_LEN*/, N_CHAR, N_ZERO,
		N_DIGITS, N_D1, N_D2, N_D3, N_DDELTA, N_MATCH, N_DUP, N_END};

char *types[]={"TYPE", "ALPHA", "CHAR", "ZERO",
	       "DIGITS", "", "", "", "DDELTA", "MATCH", "DUP", "END"};

typedef struct {
    uint8_t *buf;
    size_t buf_a, buf_l; // alloc and used length.
} descriptor;

static descriptor desc[MAX_DESCRIPTORS];

//-----------------------------------------------------------------------------
// Fast unsigned integer printing code.
// Returns number of bytes written.
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

static int decode_token_type(name_context *ctx, int ntok) {
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

#define encode_token_int encode_token_int4
#define decode_token_int decode_token_int4



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
    int i, j, k, kret;
    name[len]=0; // keep hash table happy, ugh.
    khiter_t kh = kh_put(s2i, ctx->name_s, strdup(name), &kret);

    //fprintf(stderr, "NAME: %.*s, \tkh=%3d, kret=%d value=%4d\n", len, name, kh, kret, kret?-666:kh_value(ctx->name_s, kh));

    if (kret == 0) {
	int dist = ctx->counter - kh_value(ctx->name_s, kh);
	return encode_token_dup(ctx, dist);
    }

    kh_value(ctx->name_s, kh) = ctx->counter++;

    int ntok = 0;
    for (i = j = 0, k = 0; i < len; i++, j++, k++) {
	/* Determine data type of this segment */
	if (isalpha(name[i])) {
	    int s = i+1;

	    // FIXME: try which of these is best.  alnum is good sometimes.
	    while (s < len && isalpha(name[s]))
	    //while (s < len && isalnum(name[s]))
		s++;

	    // Single byte strings are better encoded as chars.
	    if (s-i == 1) goto n_char;

	    if (ntok < ctx->last_ntok && ctx->last_token_type[ntok] == N_ALPHA) {
		if (s-i == ctx->last_token_int[ntok] &&
		    memcmp(&name[i], 
			   &ctx->last_name[ctx->last_token_str[ntok]],
			   s-i) == 0) {
		    //fprintf(stderr, "Tok %d (alpha-mat, %.*s)\n", N_MATCH, s-i, &name[i]);
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		} else {
		    //fprintf(stderr, "Tok %d (alpha, %.*s / %.*s)\n", N_ALPHA,
		    //	    s-i, &ctx->last_name[ctx->last_token_str[ntok]], s-i, &name[i]);
		    if (encode_token_alpha(ctx, ntok, &name[i], s-i) < 0) return -1;
		}
	    } else {
		//fprintf(stderr, "Tok %d (new alpha, %.*s)\n", N_ALPHA, s-i, &name[i]);
		if (encode_token_alpha(ctx, ntok, &name[i], s-i) < 0) return -1;
	    }

	    ctx->last_token_int[ntok] = s-i;
	    ctx->last_token_str[ntok] = i;
	    ctx->last_token_type[ntok] = N_ALPHA;

	    i = s-1;
	} else if (name[i] == '0') {
	    int s = i, v;
	    while (s < len && name[s] == '0' && s-i < 255)
		s++;
	    v = s-i;

	    if (ntok < ctx->last_ntok && ctx->last_token_type[ntok] == N_ZERO) {
		if (ctx->last_token_int[ntok] == v) {
		    //fprintf(stderr, "Tok %d (zero-mat, len %d)\n", N_MATCH, v);
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		} else {
		    //fprintf(stderr, "Tok %d (zero, len %d / %d)\n", N_ZERO, ctx->last_token_int[ntok], v);
		    if (encode_token_int1(ctx, ntok, N_ZERO, v) < 0) return -1;
		}
	    } else {
		//fprintf(stderr, "Tok %d (new zero, len %d)\n", N_ZERO, v);
		if (encode_token_int1(ctx, ntok, N_ZERO, v) < 0) return -1;
	    }

	    ctx->last_token_int[ntok] = v;
	    ctx->last_token_type[ntok] = N_ZERO;

	    i = s-1;
	} else if (isdigit(name[i])) {
	    int s = i;
	    uint32_t v = 0;
	    int d = 0;
	    while (s < len && isdigit(name[s]) && v < (1<<27)) {
		v = v*10 + name[s] - '0';
		s++;
	    }
	    
	    // If we previously emitted a zero token at this point, then possibly
	    // we have a fixed sized numeric field that sometimes has leading zeros
	    // and sometimes not.  In this case we emit an N_ZERO token of 0 length
	    // to ensure token count is constant and keep remaining tokens in frame.
	    // Ie  :090: :111: will be CHAR ZERO DIG CHAR in all cases.
	    //
	    // However we only do this when the previous ZERO does indeed precede
	    // a DIGIT as with :0: :1: :2: the :0: *is* the number and isn't actually
	    // a leading zero.
	    if (ntok+1 < ctx->last_ntok && ctx->last_token_type[ntok] == N_ZERO &&
		ctx->last_token_type[ntok+1] == N_DIGITS) {
		if (ctx->last_token_int[ntok] == 0) {
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		} else {
		    if (encode_token_int1(ctx, ntok, N_ZERO, 0) < 0) return -1;
		}
		ctx->last_token_int[ntok] = 0;
		ntok++;
	    }


	    // TODO: optimise choice over whether to switch from DIGITS to DELTA
	    // regularly vs all DIGITS, also MATCH vs DELTA 0.
	    if (ntok < ctx->last_ntok && ctx->last_token_type[ntok] == N_DIGITS) {
		d = v - ctx->last_token_int[ntok];
		ctx->last_token_str[ntok]++;
		if (d == 0 /* && !ctx->last_token_delta[ntok]*/) {
		    //fprintf(stderr, "Tok %d (dig-mat, %d)\n", N_MATCH, v);
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		    //ctx->last_token_delta[ntok] = 0;
		} else if (d < 256 && d >= 0) {
		    //fprintf(stderr, "Tok %d (dig-delta, %d / %d)\n", N_DDELTA, ctx->last_token_int[ntok], v);
		    if (encode_token_int1(ctx, ntok, N_DDELTA, d) < 0) return -1;
		    //ctx->last_token_delta[ntok] = 1;
		} else {
		    //fprintf(stderr, "Tok %d (dig, %d / %d)\n", N_DIGITS, ctx->last_token_int[ntok], v);
		    if (encode_token_int(ctx, ntok, N_DIGITS, v) < 0) return -1;
		    //ctx->last_token_delta[ntok] = 0;
		}
	    } else {
		//fprintf(stderr, "Tok %d (new dig, %d)\n", N_DIGITS, v);
		if (encode_token_int(ctx, ntok, N_DIGITS, v) < 0) return -1;
		//ctx->last_token_delta[ntok] = 0;
	    }

	    ctx->last_token_int[ntok] = v;
	    ctx->last_token_type[ntok] = N_DIGITS;

	    i = s-1;
	} else {
	n_char:
	    if (ntok < ctx->last_ntok && ctx->last_token_type[ntok] == N_CHAR) {
		if (name[i] == ctx->last_token_int[ntok]) {
		    //fprintf(stderr, "Tok %d (chr-mat, %c)\n", N_MATCH, name[i]);
		    if (encode_token_match(ctx, ntok) < 0) return -1;
		} else {
		    //fprintf(stderr, "Tok %d (chr, %c / %c)\n", N_CHAR, ctx->last_token_int[ntok], name[i]);
		    if (encode_token_char(ctx, ntok, name[i]) < 0) return -1;
		}
	    } else {
		//fprintf(stderr, "Tok %d (new chr, %c)\n", N_CHAR, name[i]);
		if (encode_token_char(ctx, ntok, name[i]) < 0) return -1;
	    }

	    ctx->last_token_int[ntok] = name[i];
	    ctx->last_token_type[ntok] = N_CHAR;
	}

	ntok++;
    }

    //fprintf(stderr, "Tok %d (end)\n", N_END);
    if (encode_token_end(ctx, ntok) < 0) return -1;

    //printf("Encoded %.*s with %d tokens\n", len, name, ntok);
    
    memcpy(ctx->last_name, name, len);
    ctx->last_name_len = len;
    ctx->last_ntok = ntok;

    return 0;
}


//-----------------------------------------------------------------------------
// Name decoder

// FIXME: should know the maximum name length for safety.
int decode_name(name_context *ctx, char *name) {
    int ntok, len = 0, len2;
    khiter_t kh;

    *name = 0;

    for (ntok = 0; ntok < MAX_TOKENS; ntok++) {
	uint32_t v;
	enum name_type tok;
	tok = decode_token_type(ctx, ntok);
	//printf("token type %s / %s, %d\n", types[tok], types[ctx->last_token_type[ntok]], ctx->last_token_int[ntok]);

	if (ntok == 0 && tok == N_DUP) {
	    // Duplicate
	    decode_token_int(ctx, 0, N_DUP, &v);
	    kh = kh_get(i2s, ctx->name_i, ctx->counter - v);
	    assert(kh != kh_end(ctx->name_i));
	    strcpy(name, kh_value(ctx->name_i, kh));
	    return strlen(name);
	    //return sprintf(name, "Xdup of %d => '%s'" ,ctx->counter - v, kh_value(ctx->name_i, kh));
	}

	switch (tok) {
	case N_CHAR:
	    decode_token_char(ctx, ntok, &name[len]);
	    ctx->last_token_type[ntok] = N_CHAR;
	    ctx->last_token_int[ntok] = name[len++];
	    break;

	case N_ALPHA:
	    len2 = decode_token_alpha(ctx, ntok, &name[len]);
	    ctx->last_token_type[ntok] = N_ALPHA;
	    ctx->last_token_str[ntok] = len;
	    ctx->last_token_int[ntok] = len2;
	    len += len2;
	    break;

	case N_DIGITS:
	    decode_token_int(ctx, ntok, N_DIGITS, &v);
	    len += append_uint32(&name[len], v);
	    ctx->last_token_type[ntok] = N_DIGITS;
	    ctx->last_token_int[ntok] = v;
	    break;

	case N_DDELTA:
	    decode_token_int1(ctx, ntok, N_DDELTA, &v);
	    v += ctx->last_token_int[ntok];
	    len += append_uint32(&name[len], v);
	    ctx->last_token_int[ntok] = v;
	    break;

	case N_ZERO:
	    decode_token_int1(ctx, ntok, N_ZERO, &v);
	    ctx->last_token_type[ntok] = N_ZERO;
	    ctx->last_token_int[ntok] = v;
	    while (v--)
		name[len++] = '0';
	    break;

	case N_MATCH:
	    switch (ctx->last_token_type[ntok]) {
	    case N_CHAR:
		name[len++] = ctx->last_token_int[ntok];
		break;

	    case N_ALPHA:
		memcpy(&name[len],
		       &ctx->last_name[ctx->last_token_str[ntok]],
		       ctx->last_token_int[ntok]);
		ctx->last_token_str[ntok] = len;
		len += ctx->last_token_int[ntok];
		break;

	    case N_DIGITS:
		len += append_uint32(&name[len], ctx->last_token_int[ntok]);
		break;

	    case N_ZERO:
		v = ctx->last_token_int[ntok];
		while (v--)
		    name[len++] = '0';
		break;

	    default:
		abort();
	    }
	    break;

	case N_END:
	    name[len++] = 0;
	    ctx->last_token_type[ntok] = N_END;
	    // FIXME: avoid using memcpy, just keep pointer into buffer?
	    memcpy(ctx->last_name, name, len);
	    
	    int ret;
	    kh = kh_put(i2s, ctx->name_i, ctx->counter++, &ret);
	    kh_value(ctx->name_i, kh) = strdup(name);
	    
	    return len;

	default:
	    return 0; // eof we hope!
	}
    }

    return -1;
}

static int decode(int argc, char **argv) {
    FILE *fp;
    char *prefix = "stdin";
    char line[8192];
    int i;

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
    name_context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name_i = kh_init(i2s);

    while ((ret = decode_name(&ctx, line)) > 0)
	puts(line);

    if (ret < 0)
	return -1;

    return 0;
}

static int encode(int argc, char **argv) {
    FILE *fp;
    char line[8192], *prefix = "stdin";
    name_context ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.name_s = kh_init(s2i);

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

    while (fgets(line, 8192, fp))
	// hack: -1 for \n
	if (encode_name(&ctx, line, strlen(line)-1) < 0)
	    return 1;

    if (fclose(fp) < 0) {
	perror("closing file");
	return 1;
    }

    // Write out descriptors
    int i;
    for (i = 0; i < MAX_DESCRIPTORS; i++) {
	if (!desc[i].buf_l) continue;

	printf("Des %d/%d: size %d\n", i>>4, i&15, (int)desc[i].buf_l);
	char fn[1024]; // fixme
	sprintf(fn, "%s.%d_%d", prefix, i>>4, i&15);
	fp = fopen(fn, "w");
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
    }

    // purge hash table
    khiter_t k;
    for (k = kh_begin(ctx.name_s); k != kh_end(ctx.name_s); ++k)
	if (kh_exist(ctx.name_s, k))
	    free((char*)kh_key(ctx.name_s, k));
    
    return 0;
}

int main(int argc, char **argv) {

    if (argc > 1 && strcmp(argv[1], "-d") == 0)
	return decode(argc-1, argv+1);
    else
	return encode(argc, argv);
}


/*
Example encoding with debug:

@ seq3d[mpeg/CE5]; ./a.out _names4 2>&1 | head -46
NAME: @SRR608881.1 FCD0F0WABXX:7:1101:1439:2199/1
Tok 2 (new chr, @)
Tok 1 (new alpha, SRR)
Tok 4 (new dig, 608881)
Tok 2 (new chr, .)
Tok 4 (new dig, 1)
Tok 2 (new chr,  )
Tok 1 (new alpha, FCD)
Tok 3 (new zero, len 1)
Tok 1 (new alpha, F)
Tok 3 (new zero, len 1)
Tok 1 (new alpha, WABXX)
Tok 2 (new chr, :)
Tok 4 (new dig, 7)
Tok 2 (new chr, :)
Tok 4 (new dig, 1101)
Tok 2 (new chr, :)
Tok 4 (new dig, 1439)
Tok 2 (new chr, :)
Tok 4 (new dig, 2199)
Tok 2 (new chr, /)
Tok 4 (new dig, 1)
Tok 13 (end)
NAME: @SRR608881.2 FCD0F0WABXX:7:1101:1458:2211/1
Tok 12 (chr-mat, @)
Tok 12 (alpha-mat, SRR)
Tok 12 (dig-mat, 608881)
Tok 12 (chr-mat, .)
Tok 8 (dig-delta, 1 / 2)
Tok 12 (chr-mat,  )
Tok 12 (alpha-mat, FCD)
Tok 12 (zero-mat, len 1)
Tok 12 (alpha-mat, F)
Tok 12 (zero-mat, len 1)
Tok 12 (alpha-mat, WABXX)
Tok 12 (chr-mat, :)
Tok 12 (dig-mat, 7)
Tok 12 (chr-mat, :)
Tok 12 (dig-mat, 1101)
Tok 12 (chr-mat, :)
Tok 8 (dig-delta, 1439 / 1458)
Tok 12 (chr-mat, :)
Tok 8 (dig-delta, 2199 / 2211)
Tok 12 (chr-mat, /)
Tok 12 (dig-mat, 1)
Tok 13 (end)
*/
