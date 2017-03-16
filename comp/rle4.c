// As rle2 but without a guard.
// Instead we do one pass to indicate which symbols to RLE and
// always output length for that symbol.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

unsigned char *encode(unsigned char *data, uint64_t len, uint64_t *out_len) {
    uint64_t i, j, k = 0;
    int last = -1;
    unsigned char *out = malloc(len*2);

    int run_len = 0;
    
    // Two pass:
    int64_t saved[256] = {0};
    for (i = 0; i < len; i++) {
	if (data[i] == last) {
	    saved[data[i]]+=run_len;
	    run_len++;
	} else {
	    saved[data[i]]--;
	    run_len = 0;
	    last = data[i];
	}
    }

    for (i =j = 0; i < 256; i++)
	if (saved[i] > 0)
	    j++;
    out[k++] = j;
    for (i = 0; i < 256; i++) {
	if (saved[i] > 0) {
	    //fprintf(stderr, "Saved '%c' = %d\n", (uint8_t)i, (int)saved[i]);
	    out[k++] = i;
	}
    }

    last = -1;
    run_len = 0;
    for (i = 0; i < len; i++) {
	if (data[i] == last) {
	    run_len++;
	} else {
	    if (last >= 0 && saved[last] > 0) {
		k -= run_len;
		//fprintf(stderr, "%d: run=%d, k=%d, last=%c, saved=%d\n", i, run_len, k, last, saved[last]);
		out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
		if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	    }
	    run_len = 0;
	    last = data[i];
	}

	out[k++] = data[i];
	last = data[i];
    }

    // Trailing run
    if (saved[last] > 0) {
	k -= run_len;
	out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
	if (run_len) out[k++] = (run_len & 0x7f) + (run_len>=128 ?128 : 0), run_len>>=7;
    }

    *out_len = k;
    return out;
}

// I know - no error checking! It's just a test tool. :)
unsigned char *grow(unsigned char *b, uint64_t *alloc, uint64_t used, uint64_t growth) {
    while (used + growth > *alloc) {
	*alloc *= 2;
	b = realloc(b, *alloc);
    }
    return b;
}

unsigned char *decode(unsigned char *in, uint64_t len, uint64_t *out_len) {
    uint64_t o_len = len;
    unsigned char *out = malloc(o_len), b;
    uint64_t i, j;

    int saved[256] = {0};
    for (i = 0, j = in[i++]; j; j--)
	saved[in[i++]]=1;

    j = 0;
    while (i < len) {
	b = in[i++];
	if (saved[b]) {
	    uint32_t run_len = 0;
	    unsigned char c, s = 0;
	    do {
		c = in[i++];
		run_len |= (c & 0x7f) << s;
		s += 7;
	    } while (c & 0x80);
	    run_len++;
	    //fprintf(stderr, "run_len=%d %x\n", run_len, run_len);
	    out = grow(out, &o_len, j, run_len);
	    memset(&out[j], b, run_len);
	    j += run_len;
	} else {
	    out = grow(out, &o_len, j, 1);
	    out[j++] = b;
	}
    }

    *out_len = j;
    return out;
}

#define BS 1024*1024
static unsigned char *load(uint64_t *lenp) {
    unsigned char *data = NULL;
    uint64_t dsize = 0;
    uint64_t dcurr = 0;
    signed int len;

    do {
	if (dsize - dcurr < BS) {
	    dsize = dsize ? dsize * 2 : BS;
	    data = realloc(data, dsize);
	}

	len = read(0, data + dcurr, BS);
	if (len > 0)
	    dcurr += len;
    } while (len > 0);

    if (len == -1) {
	perror("read");
    }

    *lenp = dcurr;
    return data;
}


int main(int argc, char **argv) {
    uint64_t len, out_len;
    unsigned char *in  = load(&len), *out;

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
	out = decode(in, len, &out_len);
    } else {
	out = encode(in, len, &out_len);
    }

    write(1, out, out_len);

    free(in);
    free(out);

    return 0;
}
