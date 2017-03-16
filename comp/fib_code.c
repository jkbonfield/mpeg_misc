// As rle2 but without a guard.
// Instead we do one pass to indicate which symbols to RLE and
// always output length for that symbol.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

typedef struct {
    int count;
    int fib;
    uint8_t c;
} fib_code;

int fib_code_sort(const void *vp1, const void *vp2) {
    fib_code *f1 = (fib_code *)vp1;
    fib_code *f2 = (fib_code *)vp2;
    return f2->count - f1->count;
}

uint8_t *encode(uint8_t *data, int64_t len, int64_t *out_len) {
    // calc lengths
    int a, b, n;
    int lhist[256][20] = {0}, run = 0;
    int F[256] = {0}, nsym;
    int64_t i, j;
    uint8_t last = -1;
    uint8_t *out = malloc(len);

    int fib[20] = {1,1,2,3,5,8,13,21,34,55,89,144,233,377,610,987,1597,2584,4181,6765};
//    for (a = 1, b = 1, n = 0; n < 20; n++)
//	fib[n] = a, a = b, b += fib[n];

    // Aggregate code lengths
    for (i = 0; i < len; i++) {
	F[data[i]]=1;
	if (data[i] == last) {
	    run++;
	} else {
	    //fprintf(stderr, "run=%d:", run);
	    while (run >= 2) {
		n = 2;
		while (n < 20 && run >= fib[n])
		    n++;
		run -= fib[n-1];
		lhist[last][n-1]+=fib[n-1]+40; // favour small codes over long ones for O(1)
	    }
	    //fprintf(stderr, "\n");
	    run = 1;
	    last = data[i];
	}
    }

    // Compute number of symbols
    for (nsym = i = 0; i < 128; i++)
	if (F[i]) nsym++;
    while (i < 256) {
	if (F[i++]) {
	    // We only work on 7-bit data
	    memcpy(out, data, len);
	    *out_len = len;
	    return out;

	    //free(out);
	    //return NULL;
	}
    }
	    

    // Filter code lengths, todo
    int code = 0;
    fib_code fc[256*20] = {0};
    for (i = 0; i < 256; i++) {
	for (n = 2; n < 20; n++) {
	    if (!lhist[i][n])
		continue;
	    //fc[code].count = lhist[i][n]*fib[n];
	    fc[code].count = lhist[i][n];
	    fc[code].fib = n;
	    fc[code].c = i;
	    code++;
	    lhist[i][n] = 0;
	}
    }
    qsort(fc, code, sizeof(*fc), fib_code_sort);

    //fprintf(stderr, "nsym=%d\n", nsym);
    for (i = 0; i < code && i < 128; i++) {
	if (fc[i].count < 1.5*nsym) // cost of larger O1 freq table vs saving
	    break;
	lhist[fc[i].c][fc[i].fib] = 128 + i;
	//fprintf(stderr, "code %d %d = %d\n", fc[i].c, fc[i].fib, fc[i].count, i);
    }

    // Replace by code lengths
    j = 0;
    out[j++] = code > 128 ? 128 : code;
    for (i = 0; i < code && i < 128; i++) {
	out[j++] = fc[i].c;
	out[j++] = fc[i].fib;
    }
    run = 0; last = -1;
    for (i = 0; i < len; i++) {
	if (data[i] == last) {
	    run++;
	} else {
	    while (run >= 2) {
		int N = 0;
		n = 1;
		while (n < 20 && run >= fib[n]) {
		    if (lhist[last][n])
			N=n;
		    n++;
		}
		//fprintf(stderr, "run %c len %d, %d,%d\n", last, run, N, fib[N]);
		if (!N)
		    break;
		run -= fib[N];
		out[j++] = lhist[last][N];
	    }
	    while (run--)
		out[j++] = last;
	    run = 1;
	    last = data[i];
	}
    }
    while (run--)
	out[j++] = last;

    *out_len = j;
    return out;
}

uint8_t *decode(uint8_t *data, int64_t len, int64_t *out_len) {
    int64_t i, j;
    uint8_t *out = malloc(len);
    *out_len = len;

    int fib[20] = {1,1,2,3,5,8,13,21,34,55,89,144,233,377,610,987,1597,2584,4181,6765};
    int a, b, n;

    fib_code fc[256];

    i = 0;
    n = data[i++];
    for (j = 0; j < n; j++) {
	fc[j+128].c = data[i++];
	fc[j+128].fib = fib[data[i++]];
    }

    j = 0;
    while (i < len) {
	while (j + fib[19] > *out_len) {
	    *out_len *= 2;
	    out = realloc(out, *out_len);
	}
	if (data[i] & 0x80) {
	    memset(&out[j], fc[data[i]].c, fc[data[i]].fib);
	    j += fc[data[i]].fib;
	} else {
	    out[j++] = data[i];
	}
	i++;
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
