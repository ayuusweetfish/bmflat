#include "bmflat.h"

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

int bm_load(struct bm_chart *chart, const char *source)
{
    reset_logs();

    memset(chart, 0, sizeof(struct bm_chart));

    const char *s = "lorem ipsum dolor sit amet";
    emit_log(0, "heya");
    emit_log(1, "%s %s %s %s", s, s, s, s);
    emit_log(2, "%d + %d = %d", 222, 22, 222 + 22);

    return log_ptr;
}
