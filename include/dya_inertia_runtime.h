#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

/* scroll_inertia_free 単一スロット。devポインタ同一性で解決する。
 * dev->name 文字列比較は禁止 (holdtap と同一制約)。
 * 他インスタンスは DT のまま (波及なし)。
 */
struct dya_inertia_cfg {
    int32_t enabled;    /* 0=off 1=on */
    int32_t friction;   /* 0..1000 permille */
    int32_t limit;      /* 1..4000 */
    int32_t decay_fast; /* 800..999 */
    int32_t decay_slow; /* 800..999 */
    int32_t decay_tail; /* 800..999 */
    int32_t fast;       /* 0..4000 */
    int32_t slow;       /* 0..4000 */
    int32_t start;      /* 1..2000 */
    int32_t move;       /* 1..2000 */
    int32_t stop;       /* 1..500 */
};

int dya_inertia_get(struct dya_inertia_cfg *out);
const struct device *dya_inertia_dev(void);

/* 明示保存・破棄 (MEMORY運用の永続化口)。custom-settings無効時は -ENOSYS。 */
int dya_inertia_save(void);
int dya_inertia_discard(void);
int dya_inertia_set_enabled(int32_t v);
int dya_inertia_set_friction(int32_t v);
int dya_inertia_set_limit(int32_t v);
int dya_inertia_set_decay_fast(int32_t v);
int dya_inertia_set_decay_slow(int32_t v);
int dya_inertia_set_decay_tail(int32_t v);
int dya_inertia_set_fast(int32_t v);
int dya_inertia_set_slow(int32_t v);
int dya_inertia_set_start(int32_t v);
int dya_inertia_set_move(int32_t v);
int dya_inertia_set_stop(int32_t v);

/* mjm input_processor_scroll_inertia.c から呼ばれる。
 * true で 10値を値コピー。false で dev->config に fallback
 * (素の DT 動作 = もともとの機能そのまま)。
 */
bool dya_inertia_resolve(const struct device *dev, int32_t *enabled,
                         int32_t *friction,
                         int32_t *limit, int32_t *decay_fast,
                         int32_t *decay_slow, int32_t *decay_tail,
                         int32_t *fast, int32_t *slow, int32_t *start,
                         int32_t *move, int32_t *stop);
