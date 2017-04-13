//cc -g tokenise_name3.c codec_orig.c rANS_static4x16pr.c -lm

/*
 * This is a non-learning mix of techniques, choosing the optimal method.
 *
 * We support cat (do nothing), rle, rans order 0 and 1.  All of these
 * can be on byte oriented data or 8-bit portions of 32-bit data.
 */

// TODO: X4 could store length once instead of 4 times.

// TODO: RLE+rans0 and RLE+rans1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>

#include "rANS_static4x16.h"

typedef enum {
    CAT, RLE, RANS0, RANS1, PACK0, PACK1, RLE0, RLE1, PACK_RLE0, PACK_RLE1, X4
} codec_t;

int compress(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len, int no_X4);
int uncompress(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len);

//#define DEBUG

//-----------------------------------------------------------------------------
// Simple variable sized unsigned integers
int i7put(uint8_t *buf, uint64_t val) {
    uint8_t *b = buf;
    do {
	*b++ = (val & 0x7f) | ((val >= 0x80) << 7);
	val >>= 7;
    } while (val);

    return b-buf;
}

int i7get(uint8_t *buf, uint64_t *val) {
    uint64_t v = 0;
    uint8_t *b = buf;
    int s = 0;
    uint8_t c;

    do {
	c = *b++;
	v |= (c & 0x7f) << s;
	s += 7;
    } while (c & 0x80);

    *val = v;
    return b - buf;
}

//-----------------------------------------------------------------------------
// Cat: the nul codec, ideal for small data

int cat_encode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    uint8_t *o = out;
    *o++ = CAT;
    o += i7put(o, in_len);
    memcpy(o, in, in_len);

    *out_len = o+in_len - out;
    return 0;
}

// Returns number of bytes read from 'in' on success,
//        -1 on failure.
int64_t cat_decode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    uint8_t *i = in;
    uint64_t ulen;
    assert(*i == CAT);
    i++;
    i += i7get(i, &ulen);
    assert(ulen <= *out_len);

    memcpy(out, i, ulen);
    *out_len = ulen;

    return (i-in) + ulen;
}

//-----------------------------------------------------------------------------
// Run length encoding.
#ifndef GUARD
#  define GUARD 233
#endif

#ifndef RUN_LEN
#  define RUN_LEN 4
#endif

int rle_encode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    uint64_t i, k = 0;
    int last = -1;
    int run_len = 0;

    out[k++] = RLE;
    //out[k++] = GUARD;
    k += i7put(&out[k], in_len);
    
    for (i = 0; i < in_len; i++) {
	if (in[i] == last) {
	    run_len++;
	} else {
	    if (++run_len >= RUN_LEN) {
		k -= run_len * (1 + (last==GUARD));
		out[k++] = GUARD;
		out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		out[k++] = last;
	    }
	    run_len = 0;
	}

	if (in[i] == GUARD) {
	    out[k++] = GUARD;
	    out[k++] = 0;
	} else {
	    out[k++] = in[i];
	}
	last = in[i];
    }

    // Trailing run
    if (++run_len >= RUN_LEN) {
	k -= run_len * (1 + (last==GUARD));
	out[k++] = GUARD;
	out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	out[k++] = last;
    }

    *out_len = k;
    return 0;
}

// Returns number of bytes read from 'in' on success,
//        -1 on failure.
int64_t rle_decode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    uint64_t ulen;
    uint64_t i, j;

    assert(in[0] == RLE);
    i = 1+i7get(&in[1], &ulen);
    assert(ulen <= *out_len);

    for (j = 0; i < in_len && j < ulen; i++) {
	if (in[i] == GUARD) {
	    if (in[++i] == 0) {
		assert(j+1 <= ulen);
	        out[j++] = GUARD;
	    } else {
		uint32_t run_len = 0;
		unsigned char c, s = 0;
		do {
		    c = in[i++];
		    run_len |= (c & 0x7f) << s;
		    s += 7;
		} while (c & 0x80);
		assert(j+run_len <= ulen);
		memset(&out[j], in[i], run_len);
		j += run_len;
	    }
	} else {
	    assert(j+1 <= ulen);
	    out[j++] = in[i];
	}
    }

    *out_len = ulen;

    return i; // number of compressed bytes consumed.
}

//-----------------------------------------------------------------------------
// rANS codec
int rans_encode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len, int method) {
    unsigned int olen = *out_len-5;
    *out = RANS0;

    if (rans_compress_to_4x16(in, in_len, out+5, &olen, method) == NULL)
	return -1;
    *(uint32_t *)(out+1) = olen;

    *out_len = olen+5;
    return 0;
}

// Returns number of bytes read from 'in' on success,
//        -1 on failure.
int64_t rans_decode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len, int method) {
    unsigned int olen = *out_len;
    assert(*in == RANS0);

    if (rans_uncompress_to_4x16(in+5, in_len, out, &olen, method) == NULL)
	return -1;

    *out_len = olen;
    return 5 + *(uint32_t *)(in+1);
}

int rans0_encode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    unsigned int olen = *out_len-5;
    *out = RANS0;

    if (rans_compress_to_4x16(in, in_len, out+5, &olen, 0) == NULL)
	return -1;
    *(uint32_t *)(out+1) = olen;

    *out_len = olen+5;
    return 0;
}

// Returns number of bytes read from 'in' on success,
//        -1 on failure.
int64_t rans0_decode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    unsigned int olen = *out_len;
    assert(*in == RANS0);

    if (rans_uncompress_to_4x16(in+5, in_len, out, &olen, 0) == NULL)
	return -1;

    *out_len = olen;
    return 5 + *(uint32_t *)(in+1);
}

int rans1_encode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    unsigned int olen = *out_len-5;
    *out = RANS1;

    if (rans_compress_to_4x16(in, in_len, out+5, &olen, 1) == NULL)
	return -1;
    *(uint32_t *)(out+1) = olen;

    *out_len = olen+5;
    return 0;
}

// Returns number of bytes read from 'in' on success,
//        -1 on failure.
int64_t rans1_decode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    unsigned int olen = *out_len;
    assert(*in == RANS1);

    if (rans_uncompress_to_4x16(in+5, in_len, out, &olen, 1) == NULL)
	return -1;

    *out_len = olen;
    return 5 + *(uint32_t *)(in+1);
}

//-----------------------------------------------------------------------------
// X4: splitting 32-bit data into 4 8-bit data streams and encoding
// separately.
int x4_encode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    uint64_t j, i4[4];
    uint64_t len4 = (in_len+3)&~3, olen4, olen4_space;
    uint8_t *in4 = malloc(len4);
    uint8_t *out4;
    if (!in4) return -1;

    // Split the 4 interleave signals into their respective arrays
    len4 /= 4;
    for (j = 0; j < 4; j++)
	i4[j] = j*len4;

    for (j = 0; i4[0] < len4; i4[0]++, i4[1]++, i4[2]++, i4[3]++, j+=4) {
	in4[i4[0]] = in[j+0];
	in4[i4[1]] = in[j+1];
	in4[i4[2]] = in[j+2];
	in4[i4[3]] = in[j+3];
    }

    // Encode each signal using the best method per portion.
    out4 = out;
    *out4++ = X4;
    out4 += i7put(out4, in_len);
    olen4 = out4-out;

    for (j = 0; j < 4; j++) {
	olen4_space = *out_len - olen4;
	if (compress(in4 + j*len4, len4, out4, &olen4_space, 1) < 0) return -1;
	olen4 += olen4_space;
	out4  += olen4_space;
    }

    *out_len = olen4;
    free(in4);

    return 0;
}

int x4_decode(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    uint8_t *i = in, *o, *o_orig;
    uint64_t ulen, olen, i4[4], j;
    assert(*i == X4);
    i++;
    i += i7get(i, &ulen);

    o = o_orig = malloc(ulen);
    if (!o)
	return -1;

    in_len -= (i-in);

    // Uncompress
    uint64_t len4 = (ulen+3)&~3;
    len4 /= 4;
    for (j = 0; j < 4; j++) {
	i4[j] = j*len4;
	olen = *out_len - (o-o_orig);
	int64_t clen = uncompress(i, in_len, o, &olen);
	if (clen < 0) return -1;
	i += clen;
	o += olen;
    }

    // Reorder
    j = 0;
    while (j < ulen) {
	out[j++] = o_orig[i4[0]++];
	out[j++] = o_orig[i4[1]++];
	out[j++] = o_orig[i4[2]++];
	out[j++] = o_orig[i4[3]++];
    }

    *out_len = j;
    return i-in;
}

//-----------------------------------------------------------------------------
#define BS 1024*1024
static unsigned char *load(char *fn, uint64_t *lenp) {
    unsigned char *data = NULL;
    uint64_t dsize = 0;
    uint64_t dcurr = 0;
    signed int len;
    int fd = fn ? open(fn, O_RDONLY) : 0;
    if (fd < 0) {
	perror(fn);
	return NULL;
    }

    do {
	if (dsize - dcurr < BS) {
	    dsize = dsize ? dsize * 2 : BS;
	    data = realloc(data, dsize);
	}

	len = read(fd, data + dcurr, BS);
	if (len > 0)
	    dcurr += len;
    } while (len > 0);

    if (fn)
	close(fd);

    if (len == -1) {
	perror("read");
	return NULL;
    }

    *lenp = dcurr;
    return data;
}

int compress(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len, int no_X4) {
    uint64_t best_sz = UINT64_MAX;
    codec_t best = CAT;
    uint64_t olen = *out_len;

    *out_len = olen;
    if (cat_encode(in, in_len, out, out_len) < 0) return -1;
#ifdef DEBUG
    fprintf(stderr, "CAT   -> %ld\n", (long)*out_len);
#endif
    if (best_sz > *out_len) {
	best_sz = *out_len;
	best = CAT;
    }

    *out_len = olen;
    if (rle_encode(in, in_len, out, out_len) < 0) return -1;
#ifdef DEBUG
    fprintf(stderr, "RLE   -> %ld\n", (long)*out_len);
#endif
    if (best_sz > *out_len) {
	best_sz = *out_len;
	best = RLE;
    }

    int rmethods[] = {0,1,128,129,64,65,192,193}, m;
    //for (m = 0; m < 2; m++) {
    for (m = 0; m < 8; m++) {
	*out_len = olen;
	if (rans_encode(in, in_len, out, out_len, rmethods[m]) < 0) return -1;
#ifdef DEBUG
	fprintf(stderr, "RANS0+%d -> %ld\n", m, (long)*out_len);
#endif
	if (best_sz > *out_len) {
	    best_sz = *out_len;
	    best = RANS0+m;
	}
    }

//    *out_len = olen;
//    if (rans0_encode(in, in_len, out, out_len) < 0) return -1;
//#ifdef DEBUG
//    fprintf(stderr, "RANS0 -> %ld\n", (long)*out_len);
//#endif
//    if (best_sz > *out_len) {
//	best_sz = *out_len;
//	best = RANS0;
//    }
//
//    if (in_len >= 4) {
//	*out_len = olen;
//	if (rans1_encode(in, in_len, out, out_len) < 0) return -1;
//#ifdef DEBUG
//	fprintf(stderr, "RANS1 -> %ld\n", (long)*out_len);
//#endif
//	if (best_sz > *out_len) {
//	    best_sz = *out_len;
//	    best = RANS1;
//	}
//    }

    if (!no_X4 && in_len%4 == 0 && in_len >= 32) {
#ifdef DEBUG
	fprintf(stderr, "\n");
#endif
	*out_len = olen;
	if (x4_encode(in, in_len, out, out_len) < 0) return -1;
#ifdef DEBUG
	fprintf(stderr, "X4    -> %ld\n", (long)*out_len);
#endif
	if (best_sz > *out_len) {
	    best_sz = *out_len;
	    best = X4;
	}
    }

#ifdef DEBUG
    fprintf(stderr, "Best method = %d, %ld -> %ld\n", best, (long)in_len, (long)best_sz);
#endif

    switch (best) {
    case CAT:
	if (cat_encode(in, in_len, out, out_len) < 0) return -1;
	break;

    case RLE:
	if (rle_encode(in, in_len, out, out_len) < 0) return -1;
	break;

    case RANS0:
    case RANS1:
    case PACK0:
    case PACK1:
    case RLE0:
    case RLE1:
    case PACK_RLE0:
    case PACK_RLE1:
	*out_len = olen;
	int rmethods[] = {0,1,128,129,64,65,192,193}, m;
	if (rans_encode(in, in_len, out, out_len, rmethods[best-RANS0]) < 0) return -1;
	break;

//    case RANS0:
//	*out_len = olen;
//	if (rans0_encode(in, in_len, out, out_len) < 0) return -1;
//	break;
//
//    case RANS1:
//	*out_len = olen;
//	if (rans1_encode(in, in_len, out, out_len) < 0) return -1;
//	// last method and already in this format.

    default:
	break;
    }

    return 0;
}

uint64_t uncompressed_size(uint8_t *in, uint64_t in_len) {
    uint64_t ulen;

    switch(*in) {
    case CAT:
    case RLE:
    case X4:
	i7get(in+1, &ulen);
	break;

    case RANS0:
    case RANS1:
    case PACK0:
    case PACK1:
    case RLE0:
    case RLE1:
    case PACK_RLE0:
    case PACK_RLE1: {
	uint32_t ulen32 = *(uint32_t *)(in+6);
	ulen = ulen32;
	break;
    }

//    case RANS0:
//    case RANS1: {
//	//uint32_t ulen32 = *(uint32_t *)(in+5);
//	uint32_t ulen32 = *(uint32_t *)(in+6);
//	ulen = ulen32;
//	break;
//    }

    default:
	return -1;
    }

    return ulen;
}

int uncompress(uint8_t *in, uint64_t in_len, uint8_t *out, uint64_t *out_len) {
    switch (*in) {
    case CAT:
	return cat_decode(in, in_len, out, out_len);

    case RLE:
	return rle_decode(in, in_len, out, out_len);

    case RANS0:
    case RANS1:
    case PACK0:
    case PACK1:
    case RLE0:
    case RLE1:
    case PACK_RLE0:
    case PACK_RLE1: {
	int rmethods[] = {0,1,128,129,64,65,192,193}, m;
	return rans_decode(in, in_len, out, out_len, rmethods[*in - RANS0]);
    }

//    case RANS0:
//	return rans0_decode(in, in_len, out, out_len);
//
//    case RANS1:
//	return rans1_decode(in, in_len, out, out_len);

    case X4:
	return x4_decode(in, in_len, out, out_len);

    default:
	return -1;
    }
}

#ifdef TEST_MAIN_
int main(int argc, char **argv) {
    uint8_t *in, *out;
    uint64_t in_len, out_len;

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
	// Unpack a serialised list of compressed blocks to separate filenames.
	in = load(NULL, &in_len);

	if (*in == 255) {
	    // single file mode
	    out_len = uncompressed_size(in+1, in_len-1);
	    out = malloc(out_len);
	    assert(out);

	    if (uncompress(in+1, in_len-1, out, &out_len) < 0)
		abort();

	    if (out_len != write(1, out, out_len))
		abort();

	    free(out);
	    free(in);
	    return 0;
	}
	
	char *prefix = argc > 2 ? argv[2] : "_uncomp";

	uint8_t *in2 = in;
	int tnum = -1;
	while (in_len) {
	    uint8_t ttype = *in2++;
	    uint64_t clen;

	    // fixme: realloc this
	    out_len = uncompressed_size(in2, in_len);
	    out = malloc(out_len);
	    assert(out);

	    //fprintf(stderr, "uncomp %d -> %d -> ", (int)in_len, (int)out_len);

	    if ((clen = uncompress(in2, in_len, out, &out_len)) < 0)
		abort();

	    //fprintf(stderr, "%d\n", (int)out_len);

	    if (ttype == 0)
		tnum++;
	    char fn[1024];
	    sprintf(fn, "%s.%03d_%02d", prefix, tnum, ttype);
	    int fd = open(fn, O_WRONLY|O_TRUNC|O_CREAT, 0666);
	    if (fd < 0) {
		perror(fn);
		abort();
	    }

	    if (out_len != write(fd, out, out_len))
		abort();

	    close(fd);

	    free(out);

	    in2 += clen;
	    in_len -= clen+1;
	}

	free(in);
    } else {
	// Encode all filenames and stream packed data to stdout
	int i, last_tnum = -1;
	if (argc == 1) {
	    // stdin, just for a quick single file test
	    in = load(NULL, &in_len);

	    out_len = 1.5 * rans_compress_bound_4x16(in_len, 1); // guesswork
	    out = malloc(out_len);
	    assert(out);

	    if (compress(in, in_len, out, &out_len, 0) < 0)
		abort();

	    uint8_t single = 255; // marker for single file format.
	    write(1, &single, 1);

	    if (out_len != write(1, out, out_len))
		abort();

	    free(in);
	    free(out);
	    return 0;
	}

	for (i = 1; i < argc; i++) {
	    // parse filename
	    size_t l = strlen(argv[i]);
	    int tnum, ttype;
	    if (l <= 7 || sscanf(argv[i]+l-7, ".%03d_%02d", &tnum, &ttype) != 2) {
		fprintf(stderr, "Filename must be prefix.%%03d_%%02d syntax\n");
		abort();
	    }

	    if (ttype == 0) {
		assert(tnum == last_tnum+1);
		last_tnum = tnum;
	    }
	    
	    in = load(argv[i], &in_len);

	    out_len = 1.5 * rans_compress_bound_4x16(in_len, 1); // guesswork
	    out = malloc(out_len);
	    assert(out);

	    uint8_t ttype8 = ttype;
	    write(1, &ttype8, 1);

	    if (compress(in, in_len, out, &out_len, 0) < 0)
		abort();

	    if (out_len != write(1, out, out_len))
		abort();

	    free(in);
	    free(out);
	}
    }

    return 0;
}
#endif
