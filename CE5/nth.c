#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>

// Reports every Nth in Mth byte.
// Eg 2nd every 4th.

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
    int decode = 0;

    if (argc != 3) {
	fprintf(stderr, "Usage: nth  N size\n");
	fprintf(stderr, "or     nth -d size\n");
	return 1;
    }

    if (strcmp(argv[1], "-d") == 0) {
	// AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD to
	// ABCDABCDABCDABCDABCDABCDABCDABCDABCDABCD
	int m = atoi(argv[2]);
	size_t l, i, j, lm, k;

	unsigned char *dat = load(&l);
	unsigned char *dat2 = malloc(l);
	lm = l/m;
	for (i = k = 0; i < l; i+=m, k++)
	    for (j = 0; j < m; j++)
		dat2[i+j] = dat[k + j*lm];

	write(1, dat2, l);
	free(dat);
	free(dat2);
    } else {
	// ABCDABCDABCDABCDABCDABCDABCDABCDABCDABCD to
	// AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDD
	int n = atoi(argv[1])-1;
	int m = atoi(argv[2]);
	int c;
	size_t l = 0;

	while ((c = getchar()) != EOF) {
	    if (l++ % m == n)
		putchar(c);
	}
    }
    
    return 0;
}
