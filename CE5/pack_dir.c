// Given a directory and filename prefix this packs all files matching
// dir/prefix.*_*.fmt into a single stream.

// Note this is a proof of principle - error checking is poor!

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

void output(char *dir, struct dirent *ent, size_t p_len) {
    char fname[1024], fmt_s[1024];
    FILE *fp;
    char *dat;
    struct stat sb;
    int fmt, token, type;

    sprintf(fname, "%s/%s", dir, ent->d_name);

    if (sscanf(ent->d_name + p_len, ".%d_%d.%s\n", &token, &type, fmt_s) != 3) {
	fprintf(stderr, "Unrecognised filename format: %s\n", ent->d_name);
	exit(1);
    }

    if      (strcmp(fmt_s, "r0" ) == 0) fmt=0;
    else if (strcmp(fmt_s, "r0R") == 0) fmt=1;
    else if (strcmp(fmt_s, "r1" ) == 0) fmt=2;
    else if (strcmp(fmt_s, "r1R") == 0) fmt=3;
    else if (strcmp(fmt_s, "cat") == 0) fmt=4;
    else {
	fprintf(stderr, "Unrecognised format\n");
	exit(1);
    }
	     
    //fprintf(stderr, "Processing %s, fmt=%s,%d, token=%d, type %d\n", fname, fmt_s, fmt, token, type);

    if (stat(fname, &sb) < 0) {
	perror(fname);
	exit(1);
    }

    dat = malloc(sb.st_size);
    fp = fopen(fname, "rb");
    if (!dat || !fp ||
	sb.st_size != fread(dat, 1, sb.st_size, fp)) {
	perror(fname);
	exit(1);
    }
    fclose(fp);

    // Format, token id/type, size, data
    fputc(fmt, stdout);
    fputc(token, stdout);
    fputc(type, stdout);

    size_t sz = sb.st_size;
    do {
	fputc((sz & 0x7f) | ((sz >= 0x80) << 7), stdout);
	sz >>= 7;
    } while (sz);

    fwrite(dat, 1, sb.st_size, stdout);

    free(dat);
}

int main(int argc, char **argv) {
    if (argc < 3) {
	fprintf(stderr, "Usage: pack_dir directory prefix\n");
	return 1;
    }

    DIR *d = opendir(argv[1]);
    if (!d) {
	perror(argv[1]);
	return 1;
    }

    struct dirent *ent;
    size_t p_len = strlen(argv[2]);
    while ((ent = readdir(d))) {
	if (strncmp(ent->d_name, argv[2], p_len) == 0)
	    output(argv[1], ent, p_len);
    }

    closedir(d);
    return 0;
}
