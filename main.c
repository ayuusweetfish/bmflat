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
        if (fread(buf, len, 1, f) != 1) { free(buf); buf = NULL; break; }
    } while (0);

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
    int msgs = bm_load(&chart, src);

    printf("%d warning%s\n", msgs, msgs == 1 ? "" : "s");
    for (int i = 0; i < msgs; i++) {
        printf("Line %d: %s\n", bm_logs[i].line, bm_logs[i].message);
    }

    puts("----");
    printf("Genre: %s\n", chart.meta.genre);
    printf("Title: %s\n", chart.meta.title);
    printf("Artist: %s\n", chart.meta.artist);
    printf("Subartist: %s\n", chart.meta.subartist);

    const char *base36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int i = 0; i < 1296; i++)
        if (chart.table.wav[i] != NULL)
            printf("Wave %c%c: %s\n",
                base36[i / 36], base36[i % 36], chart.table.wav[i]);
    for (int i = 0; i < 1296; i++)
        if (chart.table.bmp[i] != NULL)
            printf("Bitmap %c%C: %s\n",
                base36[i / 36], base36[i % 36], chart.table.bmp[i]);

    return 0;
}
