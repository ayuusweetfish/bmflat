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
    printf("Initial Tempo: %d\n", chart.meta.init_tempo);
    printf("Play Level: %d\n", chart.meta.play_level);
    printf("Judge Rank: %d\n", chart.meta.judge_rank);
    printf("Gauge Total: %d\n", chart.meta.gauge_total);
    printf("Difficulty: %d\n", chart.meta.difficulty);
    printf("Loading Background: %s\n", chart.meta.stage_file);
    printf("Banner: %s\n", chart.meta.banner);
    printf("Play Background: %s\n", chart.meta.back_bmp);
    putchar('\n');

    const char *base36 = "-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" + 1;
    for (int i = 0; i < 1296; i++)
        if (chart.tables.wav[i] != NULL)
            printf("Wave %c%c: %s\n",
                base36[i / 36], base36[i % 36], chart.tables.wav[i]);
    for (int i = 0; i < 1296; i++)
        if (chart.tables.bmp[i] != NULL)
            printf("Bitmap %c%C: %s\n",
                base36[i / 36], base36[i % 36], chart.tables.bmp[i]);
    putchar('\n');

    printf("Time Signature\n");
    for (int i = 0; i < BM_BARS_COUNT; i++)
        if (chart.tracks.time_sig[i] != 0)
            printf("Bar %03d: %d\n", i, (int)chart.tracks.time_sig[i]);

    printf("Tempo\n");
    for (int j = 0; j < chart.tracks.tempo.note_count; j++) {
        struct bm_note n = chart.tracks.tempo.notes[j];
        printf("%03d %03d %d\n",
            n.bar, (int)(n.beat * 1000 + 0.5f), n.value);
    }

    printf("Ext-tempo\n");
    for (int j = 0; j < chart.tracks.ex_tempo.note_count; j++) {
        struct bm_note n = chart.tracks.ex_tempo.notes[j];
        printf("%03d %03d %c%c (%g)\n",
            n.bar, (int)(n.beat * 1000 + 0.5f),
            base36[n.value / 36], base36[n.value % 36],
            chart.tables.tempo[n.value]);
    }

    printf("Stop\n");
    for (int j = 0; j < chart.tracks.stop.note_count; j++) {
        struct bm_note n = chart.tracks.stop.notes[j];
        printf("%03d %03d %c%c (%d)\n",
            n.bar, (int)(n.beat * 1000 + 0.5f),
            base36[n.value / 36], base36[n.value % 36],
            chart.tables.stop[n.value]);
    }

    printf("BGA\n");
    for (int j = 0; j < chart.tracks.bga_base.note_count; j++) {
        struct bm_note n = chart.tracks.bga_base.notes[j];
        printf("%03d %03d %c%c (%s)\n",
            n.bar, (int)(n.beat * 1000 + 0.5f),
            base36[n.value / 36], base36[n.value % 36],
            chart.tables.bmp[n.value]);
    }

    printf("BGA Layer\n");
    for (int j = 0; j < chart.tracks.bga_layer.note_count; j++) {
        struct bm_note n = chart.tracks.bga_layer.notes[j];
        printf("%03d %03d %c%c (%s)\n",
            n.bar, (int)(n.beat * 1000 + 0.5f),
            base36[n.value / 36], base36[n.value % 36],
            chart.tables.bmp[n.value]);
    }

    printf("BGA Poor\n");
    for (int j = 0; j < chart.tracks.bga_poor.note_count; j++) {
        struct bm_note n = chart.tracks.bga_poor.notes[j];
        printf("%03d %03d %c%c (%s)\n",
            n.bar, (int)(n.beat * 1000 + 0.5f),
            base36[n.value / 36], base36[n.value % 36],
            chart.tables.bmp[n.value]);
    }

    for (int i = 0; i < chart.tracks.background_count; i++)
        if (chart.tracks.background[i].note_count > 0) {
            printf("Background Track %d\n", i);
            for (int j = 0; j < chart.tracks.background[i].note_count; j++) {
                struct bm_note n = chart.tracks.background[i].notes[j];
                printf("%03d %03d %c%c (%s)\n",
                    n.bar, (int)(n.beat * 1000 + 0.5f),
                    base36[n.value / 36], base36[n.value % 36],
                    chart.tables.wav[n.value]);
            }
        }

    for (int i = 0; i < 60; i++) if (chart.tracks.object[i].note_count > 0) {
        printf("Track %d\n", i + 10);
        for (int j = 0; j < chart.tracks.object[i].note_count; j++) {
            struct bm_note n = chart.tracks.object[i].notes[j];
            printf("%03d %03d %c%c (%s%s)\n",
                n.bar, (int)(n.beat * 1000 + 0.5f),
                base36[n.value / 36], base36[n.value % 36],
                n.value == -1 ? "release" : chart.tables.wav[n.value],
                n.hold ? " hold" : "");
        }
    }

    struct bm_seq seq;
    bm_to_seq(&chart, &seq);

    int bar = 0;

    for (int i = 0; i < seq.event_count; i++) {
        struct bm_event ev = seq.events[i];
        printf("%6d.%02d: ", ev.pos / 48, ev.pos % 48);
        switch (ev.type) {
        case BM_BARLINE:
            printf("------ #%03d %d/4\n", bar++, ev.value);
            break;
        case BM_TEMPO_CHANGE:
            printf("BPM %.2f\n", ev.value_f);
            break;
        case BM_BGA_BASE_CHANGE:
            printf("BGA %d (%s)\n", ev.value, chart.tables.bmp[ev.value]);
            break;
        case BM_BGA_LAYER_CHANGE:
            printf("BGA Layer %d (%s)\n", ev.value, chart.tables.bmp[ev.value]);
            break;
        case BM_BGA_POOR_CHANGE:
            printf("BGA Poor %d (%s)\n", ev.value, chart.tables.bmp[ev.value]);
            break;
        case BM_STOP:
            printf("Stop %d\n", ev.value);
            break;
        case BM_NOTE:
            printf("Note on track %3d | %4d (%s)\n",
                ev.track, ev.value, chart.tables.wav[ev.value]);
            break;
        case BM_NOTE_LONG:
            printf("Long on track %3d | %4d (%s) -- duration %d\n",
                ev.track, ev.value, chart.tables.wav[ev.value], ev.value_a);
            break;
        case BM_NOTE_OFF:
            break;
        default:
            puts("> <");
        }
    }

    return 0;
}
