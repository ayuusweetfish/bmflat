#include "bmflat.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

struct bm_log *bm_logs = NULL;
static int log_cap, log_ptr;

static void reset_logs()
{
    if (bm_logs) free(bm_logs);
    bm_logs = NULL;
    log_cap = log_ptr = 0;
}

static void ensure_log_cap()
{
    if (log_cap <= log_ptr) {
        log_cap = (log_cap == 0 ? 8 : (log_cap << 1));
        bm_logs = (struct bm_log *)
            realloc(bm_logs, log_cap * sizeof(struct bm_log));
    }
}

#define emit_log(_line, ...) do { \
    ensure_log_cap(); \
    bm_logs[log_ptr].line = _line; \
    snprintf(bm_logs[log_ptr].message, BM_MSG_LEN, __VA_ARGS__); \
    log_ptr++; \
} while (0)

static inline int is_space_or_linebreak(char ch)
{
    return ch == '\r' || ch == '\n' || ch == '\0';
}

static inline int isbase36(char ch)
{
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z');
}

static inline int base36(char c1, char c2)
{
    // Assumes isbase36(c1) and isbase36(c2) are true
    return
        (c1 <= '9' ? c1 - '0' : c1 - 'A' + 10) * 36 +
        (c2 <= '9' ? c2 - '0' : c2 - 'A' + 10);
}

static inline void add_note(struct bm_track *track, short bar, float beat, short value)
{
    if (track->note_cap <= track->note_count) {
        track->note_cap = (track->note_cap == 0 ? 8 : (track->note_cap << 1));
        track->notes = (struct bm_note *)
            realloc(track->notes, track->note_cap * sizeof(struct bm_note));
    }
    track->notes[track->note_count].bar = bar;
    track->notes[track->note_count].beat = beat;
    track->notes[track->note_count++].value = value;
}

static inline void parse_track(int line, char *s, struct bm_track *track, short bar)
{
    int count = 0;
    for (char *p = s; *p != '\0'; p++) count += (!isspace(*p));
    count /= 2;

    for (int p = 0, q, i = 0; s[p] != '\0'; p = q + 1) {
        while (isspace(s[p]) && s[p] != '\0') p++;
        if (s[p] == '\0') break;
        q = p + 1;
        while (isspace(s[q]) && s[q] != '\0') q++;
        if (s[q] == '\0') {
            emit_log(line, "Extraneous trailing character %c, ignoring", s[p]);
            break;
        }
        if (!isbase36(s[p]) || !isbase36(s[q])) {
            emit_log(line, "Invalid base-36 index %c%c at column %d, ignoring",
                s[p], s[q], p + 8);
            continue;
        }
        int value = base36(s[p], s[q]);
        if (value != 0) add_note(track, bar, (float)i / count, value);
        i++;
    }
}

int bm_load(struct bm_chart *chart, const char *_source)
{
    char *source = strdup(_source);

    chart->meta.player_num = -1;
    chart->meta.genre = NULL;
    chart->meta.title = NULL;
    chart->meta.artist = NULL;
    chart->meta.subartist = NULL;
    chart->meta.play_level = -1;
    chart->meta.judge_rank = -1;
    chart->meta.gauge_total = -1;
    memset(&chart->tables.wav, 0, sizeof chart->tables.wav);
    memset(&chart->tables.bmp, 0, sizeof chart->tables.bmp);
    for (int i = 0; i < BM_INDEX_MAX; i++) chart->tables.tempo[i] = -1;
    memset(&chart->tables.stop, -1, sizeof chart->tables.stop);
    memset(&chart->tracks, 0, sizeof chart->tracks);

    reset_logs();
    int len = strlen(source);
    int ptr = 0, next = 0, line = 1;

    for (; ptr != len; ptr = ++next, line++) {
        // Advance to the next line break
        while (!is_space_or_linebreak(source[next])) next++;
        if (source[next] == '\r' && next + 1 < len && source[next + 1] == '\n') next++;

        // Trim at both ends
        while (ptr < next && isspace(source[ptr])) ptr++;
        int end = next;
        while (end >= ptr && isspace(source[end])) end--;
        source[++end] = '\0';

        // Comment
        if (source[ptr] != '#') continue;

        // Skip the # character
        char *s = source + ptr + 1;
        int line_len = end - ptr - 1;

        if (line_len >= 6 && isdigit(s[0]) && isdigit(s[1]) && isdigit(s[2]) &&
            isdigit(s[3]) && isdigit(s[4]) && s[5] == ':')
        {
            // Track data
            int bar = s[0] * 100 + s[1] * 10 + s[2] - '0' * 111;
            int track = s[3] * 10 + s[4] - '0' * 11;
            if (track == 2) {
                // Time signature
            } else if (track == 3) {
                // Tempo change
                parse_track(line, s + 6, &chart->tracks.tempo, bar);
                for (int i = 0; i < chart->tracks.tempo.note_count; i++) {
                    int x = chart->tracks.tempo.notes[i].value;
                    chart->tracks.tempo.notes[i].value = (x / 36) * 16 + (x % 36);
                }
            } else if (track == 4) {
                // BGA
                parse_track(line, s + 6, &chart->tracks.bga_base, bar);
            } else if (track == 6) {
                // BGA poor
                parse_track(line, s + 6, &chart->tracks.bga_poor, bar);
            } else if (track == 7) {
                // BGA layer
                parse_track(line, s + 6, &chart->tracks.bga_layer, bar);
            } else if (track == 8) {
                // Extended tempo change
                parse_track(line, s + 6, &chart->tracks.ex_tempo, bar);
            } else if (track == 9) {
                // Stop
                parse_track(line, s + 6, &chart->tracks.stop, bar);
            } else if (track >= 10 && track <= 59) {
                // Fixed
                parse_track(line, s + 6, &chart->tracks.fixed[track - 10], bar);
            } else {
                emit_log(line, "Unknown track %c%c, ignoring", s[3], s[4]);
            }
        } else {
            // Command
            int arg = 0;
            while (arg < line_len && !isspace(s[arg])) arg++;
            s[arg++] = '\0';
            while (arg < line_len && isspace(s[arg])) arg++;

            if (arg >= line_len) {
                emit_log(line, "Command requires non-empty arguments, ignoring");
                continue;
            }

            #define checked_parse_int(_var, _min, _max, ...) do { \
                errno = 0; \
                long x = strtol(s + arg, NULL, 10); \
                if (errno != EINVAL && x >= (_min) && x <= (_max)) { \
                    if ((_var) != -1) emit_log(line, __VA_ARGS__); \
                    (_var) = x; \
                } else { \
                    emit_log(line, "Invalid integral value, should be " \
                        "between %d and %d (inclusive)", (_min) ,(_max)); \
                } \
            } while (0)

            #define checked_parse_float(_var, _min, _max, ...) do { \
                errno = 0; \
                float x = strtof(s + arg, NULL); \
                if (errno != EINVAL && x >= (_min) && x <= (_max)) { \
                    if ((_var) != -1) emit_log(line, __VA_ARGS__); \
                    (_var) = x; \
                } else { \
                    emit_log(line, "Invalid integral value, should be " \
                        "between %g and %g (inclusive)", (_min) ,(_max)); \
                } \
            } while (0)

            #define checked_strdup(_var, ...) do { \
                char *x = strdup(s + arg); \
                /* TODO: Handle cases of memory exhaustion? */ \
                if (x != NULL) { \
                    if ((_var) != NULL) { free(_var); emit_log(line, __VA_ARGS__); } \
                    (_var) = x; \
                } \
            } while (0)

            if (strcmp(s, "PLAYER") == 0) {
                checked_parse_int(chart->meta.player_num,
                    1, 3,
                    "Multiple PLAYER commands, overwritten");
            } else if (strcmp(s, "GENRE") == 0) {
                checked_strdup(chart->meta.genre,
                    "Multiple GENRE commands, overwritten");
            } else if (strcmp(s, "TITLE") == 0) {
                checked_strdup(chart->meta.title,
                    "Multiple TITLE commands, overwritten");
            } else if (strcmp(s, "ARTIST") == 0) {
                checked_strdup(chart->meta.artist,
                    "Multiple ARTIST commands, overwritten");
            } else if (strcmp(s, "SUBARTIST") == 0) {
                checked_strdup(chart->meta.subartist,
                    "Multiple SUBARTIST commands, overwritten");
            } else if (strcmp(s, "PLAYLEVEL") == 0) {
                checked_parse_int(chart->meta.play_level,
                    1, 999,
                    "Multiple PLAYLEVEL commands, overwritten");
            } else if (strcmp(s, "RANK") == 0) {
                checked_parse_int(chart->meta.judge_rank,
                    0, 3,
                    "Multiple RANK commands, overwritten");
            } else if (strcmp(s, "TOTAL") == 0) {
                checked_parse_int(chart->meta.gauge_total,
                    1, 999,
                    "Multiple TOTAL commands, overwritten");
            } else if (memcmp(s, "WAV", 3) == 0 && isbase36(s[3]) && isbase36(s[4])) {
                int index = base36(s[3], s[4]);
                checked_strdup(chart->tables.wav[index],
                    "Wave %c%c specified multiple times, overwritten", s[3], s[4]);
            } else if (memcmp(s, "BMP", 3) == 0 && isbase36(s[3]) && isbase36(s[4])) {
                int index = base36(s[3], s[4]);
                checked_strdup(chart->tables.bmp[index],
                    "Bitmap %c%c specified multiple times, overwritten", s[3], s[4]);
            } else if (memcmp(s, "BPM", 3) == 0 && isbase36(s[3]) && isbase36(s[4])) {
                int index = base36(s[3], s[4]);
                checked_parse_float(chart->tables.tempo[index],
                    1, 999,
                    "Tempo %c%c specified multiple times, overwritten", s[3], s[4]);
            } else if (memcmp(s, "STOP", 4) == 0 && isbase36(s[4]) && isbase36(s[5])) {
                int index = base36(s[4], s[5]);
                checked_parse_int(chart->tables.stop[index],
                    0, 32767,
                    "Stop %c%c specified multiple times, overwritten", s[4], s[5]);
            } else {
                emit_log(line, "Unrecognized command %s, ignoring", s);
            }
        }
    }

    if (chart->meta.player_num == -1) chart->meta.player_num = 1;
    if (chart->meta.genre == NULL) chart->meta.genre = "(unknown)";
    if (chart->meta.title == NULL) chart->meta.title = "(unknown)";
    if (chart->meta.artist == NULL) chart->meta.artist = "(unknown)";
    if (chart->meta.subartist == NULL) chart->meta.subartist = "(unknown)";
    if (chart->meta.play_level == -1) chart->meta.play_level = 3;
    if (chart->meta.judge_rank == -1) chart->meta.judge_rank = 3;
    if (chart->meta.gauge_total == -1) chart->meta.gauge_total = 160;

    free(source);
    return log_ptr;
}
