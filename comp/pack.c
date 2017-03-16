// Pack 2, 4 or 8 values into a byte.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

uint8_t *encode(uint8_t *data, int64_t len, int64_t *out_len) {
    int p[256] = {0}, n;
    int64_t i, j;
    uint8_t *out = malloc(len+1);
    if (!out)
	return NULL;

    // count syms
    for (i = 0; i < len; i++)
	p[data[i]]=1;
    
    for (i = n = 0; i < 256; i++) {
	if (p[i]) {
	    p[i] = n++; // p[i] is now the code number
	    out[n] = i;
	}
    }
    j = n+1;
    out[j++] = 0;

    fprintf(stderr, "n=%d\n", n);
    // 1 value per byte
    if (n > 16 || len < j + len/2) {
	out[0] = 1;
	memcpy(out+1, data, len);
	*out_len = len+1;
	return out;
    }

    // Encode original length
    int64_t len_copy = len;
    do {
	out[j++] = (len_copy & 0x7f) | ((len_copy >= 0x80) << 7);
	len_copy >>= 7;
    } while (len_copy);

    // FIXME: partial bytes (we overrun atm)

    // 2 values per byte
    if (n > 4) {
	out[0] = 2;
	for (i = 0; i < len; i+=2)
	    out[j++] = (p[data[i]]<<4) | (p[data[i+1]]<<0);
	*out_len = j;
	return out;
    }

    // 4 values per byte
    if (n > 2) {
	out[0] = 4;
	for (i = 0; i < len; i+=4)
	    out[j++] = (p[data[i]]<<6) | (p[data[i+1]]<<4) | (p[data[i+2]]<<2) | (p[data[i+3]]<<0);
	*out_len = j;
	return out;
    }

    // 8 values per byte
    if (n > 1) {
	out[0] = 8;
	for (i = 0; i < len; i+=8)
	    out[j++] = (p[data[i+0]]<<7) | (p[data[i+1]]<<6) | (p[data[i+2]]<<5) | (p[data[i+3]]<<4)
		     | (p[data[i+4]]<<3) | (p[data[i+5]]<<2) | (p[data[i+6]]<<1) | (p[data[i+7]]<<0);
	*out_len = j;
	return out;
    }

    // infinite values as only 1 type present.
    out[0] = 0;
    *out_len = j;
    return out;
}

uint8_t *decode(uint8_t *data, int64_t len, int64_t *out_len_p) {
    int p[256];
    uint8_t *out, c = 0;
    int64_t i, j, out_len;

    if (data[0] == 1) {
	// raw data
	if (!(out = malloc(len-1))) return NULL;
	memcpy(out, data+1, len-1);
	*out_len_p = len-1;
	return out;
    }

    // Decode translation table
    j = 1;
    do {
	p[c++] = data[j++];
    } while (data[j] != 0);
    j++;

    // Decode original length
    out_len = 0;
    int shift = 0;
    do {
	c = data[j++];
	out_len |= (c & 0x7f) << shift;
	shift += 7;
    } while (c & 0x80);

    fprintf(stderr, "orig len = %d\n", (int)out_len);

    if (!(out = malloc(out_len+7))) return NULL;
    
    switch(data[0]) {
    case 8:
	for (i = 0; i < out_len; i+=8) {
	    c = data[j++];
	    out[i+0] = p[(c>>7)&1];
	    out[i+1] = p[(c>>6)&1];
	    out[i+2] = p[(c>>5)&1];
	    out[i+3] = p[(c>>4)&1];
	    out[i+4] = p[(c>>3)&1];
	    out[i+5] = p[(c>>2)&1];
	    out[i+6] = p[(c>>1)&1];
	    out[i+7] = p[(c>>0)&1];
	}
	break;

    case 4:
	for (i = 0; i < out_len; i+=4) {
	    c = data[j++];
	    out[i+0] = p[(c>>6)&3];
	    out[i+1] = p[(c>>4)&3];
	    out[i+2] = p[(c>>2)&3];
	    out[i+3] = p[(c>>0)&3];
	}
	break;

    case 2:
	for (i = 0; i < out_len; i+=8) {
	    c = data[j++];
	    out[i+0] = p[(c>>4)&15];
	    out[i+1] = p[(c>>0)&15];
	}
	break;

    case 0:
	memset(out, p[0], out_len);
	break;

    default:
	free(out);
	return NULL;
    }

    *out_len_p = out_len;
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
