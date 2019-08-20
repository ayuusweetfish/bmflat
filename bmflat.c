#include "bmflat.h"

#include <ctype.h>
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

static inline int parse_player_num(const char *s, int line)
{
    if (s[0] >= '1' && s[0] <= '3' && s[1] == '\0') {
        return s[0] - '0';
    } else {
        emit_log(line, "Unrecognized player mode: %s", s);
        return -1;
    }
}

int bm_load(struct bm_chart *chart, const char *_source)
{
    char *source = strdup(_source);

    memset(chart, -1, sizeof(struct bm_chart));

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
        int line_len = end - ptr;

        if (line_len >= 6 && isdigit(s[0]) && isdigit(s[1]) && isdigit(s[2]) &&
            isdigit(s[3]) && isdigit(s[4]) && s[5] == ':')
        {
            // Track data
        } else {
            // Command
            int arg = 0;
            while (arg < line_len && !isspace(s[arg])) arg++;
            s[arg++] = '\0';
            while (arg < line_len && isspace(s[arg])) arg++;
            if (strcmp(s, "PLAYER") == 0)
                chart->meta.player_num = parse_player_num(s + arg, line);
        }
    }

    if (chart->meta.player_num == -1) chart->meta.player_num = 1;
    if (chart->meta.play_level == -1) chart->meta.play_level = 3;
    if (chart->meta.judge_rank == -1) chart->meta.judge_rank = 3;
    if (chart->meta.gauge_total == -1) chart->meta.gauge_total = 160;

    free(source);
    return log_ptr;
}
