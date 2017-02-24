#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <inttypes.h>

/*
 * Shannon showed that for storage in base 'b' with alphabet symbols 'a' having
 * a probability of ocurring in any context of 'Pa' we should encode
 * symbol 'a' to have a storage width of -logb(Pa).
 *
 * Eg. b = 26, P(e) = .22. => width .4647277.
 *
 * We use this to calculate the entropy of a signal by summing over all letters
 * in the signal. In this case, our storage has base 256.
 */
#define EBASE 65536
double entropy16(unsigned short *data, int len) {
    double E[EBASE];
    double P[EBASE];
    double e = 0;
    int i;
    
    for (i = 0; i < EBASE; i++)
        P[i] = 0;

    for (i = 0; i < len; i++)
        P[data[i]]++;

    for (i = 0; i < EBASE; i++) {
        if (P[i]) {
            P[i] /= len;
            E[i] = -(log(P[i])/log(EBASE));
        } else {
            E[i] = 0;
        }
    }

    for (e = i = 0; i < len; i++)
        e += E[data[i]];

    return e * log(EBASE)/log(256);
}

#define EBASE2 256
double entropy8(unsigned char *data, int len) {
    int F[EBASE2];
    double e = 0;
    int i;
    
    for (i = 0; i < EBASE2; i++)
        F[i] = 0;

    for (i = 0; i < len; i++)
        F[data[i]]++;

    for (i = 0; i < EBASE2; i++) {
        if (F[i]) {
	    e += -log((double)F[i]/len) * F[i];
        }
    }

    return e / log(EBASE2);
}

double entropy8o1(unsigned char *data, int len) {
    int F[256][256] = {0}, T[256] = {0};
    double e = 0;
    int i, j;
    
    for (j = i = 0; i < len; i++) {
        F[j][data[i]]++;
        T[j]++;
        j=data[i];
    }

    for (j = 0; j < 256; j++) {
        for (i = 0; i < 256; i++) {
            if (F[j][i]) {
                e += -log((double)F[j][i]/T[j]) * F[j][i];
            }
        }
    }

    return e / log(256);
}

#define BSIZE 1024*1024
static unsigned char buf[BSIZE];

int main(int argc, char **argv) {
    int len;
    uint64_t tlen = 0;
    double e8_0 = 0, e16_0 = 0, e8_1 = 0;

    while ((len = fread(buf, 1, BSIZE, stdin)) > 0) {
	e16_0 += entropy16((unsigned short *)buf, len/2);
	e8_0  += entropy8(buf, len);
	e8_1  += entropy8o1(buf, len);

	tlen += len;
    }

    printf("len = %14"PRId64" bytes, entropy16  = %12.1f, %7.5f bits per byte\n",
	   tlen, e16_0, 8 * e16_0/tlen);

    printf("len = %14"PRId64" bytes, entropy8   = %12.1f, %7.5f bits per byte\n",
	   tlen, e8_0, 8 * e8_0/tlen);

    printf("len = %14"PRId64" bytes, entropy8o1 = %12.1f, %7.5f bits per byte\n",
	   tlen, e8_1, 8 * e8_1/tlen);

    return 0;
}
