#include <stdio.h>
#include <stdlib.h>

#include "bmflat.h"

char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return NULL;

    char *buf = NULL;

    do {
        if (fseek(f, 0, SEEK_END) != 0) break;
        long len = ftell(f);
        if (fseek(f, 0, SEEK_SET) != 0) break;
        if ((buf = (char *)malloc(len)) == NULL) break;
        if (fread(buf, len, 1, f) != 1) { buf = NULL; break; }
    } while (0);

    if (buf) free(buf);
    fclose(f);
    return buf;
}

int main()
{
    char *src = read_file("sample.bms");
    if (src == NULL) {
        puts("> <");
        return 1;
    }

    struct bm_chart chart;
    int line = bm_load(&chart, src);
    if (line != 0) {
        printf("Line %d: %s\n", line, bm_errmsg);
        return 2;
    }

    puts("(^^)");

    return 0;
}
