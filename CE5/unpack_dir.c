// Unpacks a stream produced by pack_dir.

// Note this is a proof of principle - error checking is poor!

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <assert.h>

int main(int argc, char **argv) {
    char *dir, *prefix;

    if (argc < 3) {
	fprintf(stderr, "Usage: pack_dir directory prefix\n");
	return 1;
    }

    dir = argv[1];
    prefix = argv[2];
    mkdir(dir, 0777);

    int c;
    while ((c = getchar()) != EOF) {
	char *fmt, fn[1024];
	unsigned char token, type;

	// Format, token id/type
	switch(c) {
	case 0: fmt = "r0";  break;
	case 1: fmt = "r0R"; break;
	case 2: fmt = "r1";  break;
	case 3: fmt = "r1R"; break;
	case 4: fmt = "cat"; break;
	case 5: fmt = "rle"; break;
	case 6: fmt = "ix4"; break;
	case 7: fmt = "r0x4";break;
	default:
	    fprintf(stderr, "Unrecognised format code: %d\n", c);
	    exit(1);
	}
	token = getchar();
	type = getchar();

	sprintf(fn, "%s/%s.%d_%d.%s", dir, prefix, token, type, fmt);
	FILE *fp = fopen(fn, "wb");
	if (!fp) {
	    perror(fn);
	    exit(1);
	}

	// Size
	long sz = 0;
	int shift = 0;
	do {
	    c = getchar();
	    sz |= (c & 0x7f) << shift;
	    shift += 7;
	} while (c & 0x80);

	//fprintf(stderr, "Processing %s, size %ld\n", fn, sz);

	// Data
	char *buf = malloc(sz);
	assert(buf);
	if (fread(buf, 1, sz, stdin) != sz)
	    abort();

	fwrite(buf, 1, sz, fp);
	free(buf);

	fclose(fp);
    }

    return 0;
}
