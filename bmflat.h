#ifndef _BMFLAT_H_
#define _BMFLAT_H_

#ifdef __cplusplus
extern "C" {
#endif

struct bm_metadata {
    int player_num;
    char *genre;
    char *title;
    char *artist;
    char *subartist;
    float init_tempo;
    int play_level;
    int judge_rank;
    int gauge_total;
    int difficulty;
    char *stage_file;
    char *banner;
    char *back_bmp;
};

struct bm_tables {
#define BM_INDEX_MAX    1296
    char *wav[BM_INDEX_MAX];
    char *bmp[BM_INDEX_MAX];
    float tempo[BM_INDEX_MAX];
    short stop[BM_INDEX_MAX];
};

struct bm_note {
    float beat; // In fractions of the bar
    short bar:15;
    short hold:1;
    short value;
};

struct bm_track {
    int note_count, note_cap;
    struct bm_note *notes;
};

struct bm_tracks {
#define BM_BARS_COUNT   1000
    unsigned char time_sig[BM_BARS_COUNT];

#define BM_BGM_TRACKS   64
    int background_count;
    struct bm_track background[BM_BGM_TRACKS];
    struct bm_track object[60];

    struct bm_track tempo;
    struct bm_track bga_base;
    struct bm_track bga_layer;
    struct bm_track bga_poor;
    struct bm_track ex_tempo;
    struct bm_track stop;
};

struct bm_chart {
    struct bm_metadata meta;
    struct bm_tables tables;
    struct bm_tracks tracks;
};

enum bm_event_type {
    BM_BARLINE = 0,         // value = index, value_a = time signature
    BM_TEMPO_CHANGE,        // value_f = BPM
    BM_BGA_BASE_CHANGE,     // value = index
    BM_BGA_LAYER_CHANGE,    // value = index
    BM_BGA_POOR_CHANGE,     // value = index
    BM_STOP,                // value = duration
    BM_NOTE,                // value = index
    BM_NOTE_LONG,           // value = index, value_a = duration
    BM_NOTE_OFF,            // value = index, value_a = duration
};

struct bm_event {
    int pos;    // beat * 48 + fraction in 48ths of a beat (192ths of a whole note)
    enum bm_event_type type:8;
    signed char track;  // non-positive for backgrounds; 11 - 59 for objects
    union {
        struct {
            short value;
            short value_a;
        };
        float value_f;
    };
};

struct bm_seq {
    int event_count;
    struct bm_event *events;

    int long_note_count;
    struct bm_event *long_notes;
};

#define BM_MSG_LEN  128

struct bm_log {
    int line;
    char message[BM_MSG_LEN];
};

extern struct bm_log *bm_logs;

int bm_load(struct bm_chart *chart, const char *source);
void bm_to_seq(struct bm_chart *chart, struct bm_seq *seq);

void bm_close_chart(struct bm_chart *chart);
void bm_close_seq(struct bm_seq *seq);

#ifdef __cplusplus
}
#endif

#endif
