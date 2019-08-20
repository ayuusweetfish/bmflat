#ifndef _BMFLAT_H_
#define _BMFLAT_H_

#ifdef __cplusplus
extern "C" {
#endif

struct bm_chart {
};

extern char *bm_errmsg;

int bm_load(struct bm_chart *chart, const char *source);

#ifdef __cplusplus
}
#endif

#endif
