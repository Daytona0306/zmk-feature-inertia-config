/*
 * DYA inertia scroll runtime — Phase2a: hook + RAM のみ。
 * custom-settings 不要 (CONFIG_ZMK_CUSTOM_SETTINGS=n でもビルド可)。
 * Phase2b で DEFINE×10 + イベント購読 + 永続化を追加する。
 *
 * 対象は scroll_inertia_free のみ (axis=0/layer=5)。
 * 他インスタンスは DT のまま。central 側のみ。
 * dev->name 文字列比較は禁止。devポインタ同一性で判別する。
 *
 * もともとの機能はそのまま:
 * - CONFIG_ZMK_INERTIA_RUNTIME=n → 本ファイル自体ビルド除外、
 *   mjm 側 patch も #else 旧式のみで旧動作と同一。
 * - =y でも未書込み時は DT 実値を seed するため初動は同一。
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "dya_inertia_runtime.h"

LOG_MODULE_REGISTER(dya_inertia_runtime, LOG_LEVEL_DBG);

#define DYA_INERTIA_NODE DT_NODELABEL(scroll_inertia_free)

/* RAMシャドウ + ダブルバッファ。liveがresolverの読み出し面。 */
static struct dya_inertia_cfg s_live;
static struct dya_inertia_cfg s_staged;
static const struct device *s_dev = NULL;

static void dya_inertia_commit_locked(void) {
    unsigned int key = irq_lock();
    s_live = s_staged;
    irq_unlock(key);
}

/* DT既定seed。prop無ければKconfig既定。
 * peripheral 等で node 無効でも落ちないよう EXISTS ガード。 */
static void dya_inertia_seed_from_dt(void) {
#if DT_NODE_EXISTS(DYA_INERTIA_NODE)
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, friction)
    s_staged.friction = (int32_t)DT_PROP(DYA_INERTIA_NODE, friction);
#else
    s_staged.friction = CONFIG_ZMK_INERTIA_FRICTION;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, limit)
    s_staged.limit = (int32_t)DT_PROP(DYA_INERTIA_NODE, limit);
#else
    s_staged.limit = CONFIG_ZMK_INERTIA_LIMIT;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, decay_fast)
    s_staged.decay_fast = (int32_t)DT_PROP(DYA_INERTIA_NODE, decay_fast);
#else
    s_staged.decay_fast = CONFIG_ZMK_INERTIA_DECAY_FAST;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, decay_slow)
    s_staged.decay_slow = (int32_t)DT_PROP(DYA_INERTIA_NODE, decay_slow);
#else
    s_staged.decay_slow = CONFIG_ZMK_INERTIA_DECAY_SLOW;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, decay_tail)
    s_staged.decay_tail = (int32_t)DT_PROP(DYA_INERTIA_NODE, decay_tail);
#else
    s_staged.decay_tail = CONFIG_ZMK_INERTIA_DECAY_TAIL;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, fast)
    s_staged.fast = (int32_t)DT_PROP(DYA_INERTIA_NODE, fast);
#else
    s_staged.fast = CONFIG_ZMK_INERTIA_FAST;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, slow)
    s_staged.slow = (int32_t)DT_PROP(DYA_INERTIA_NODE, slow);
#else
    s_staged.slow = CONFIG_ZMK_INERTIA_SLOW;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, start)
    s_staged.start = (int32_t)DT_PROP(DYA_INERTIA_NODE, start);
#else
    s_staged.start = CONFIG_ZMK_INERTIA_START;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, move)
    s_staged.move = (int32_t)DT_PROP(DYA_INERTIA_NODE, move);
#else
    s_staged.move = CONFIG_ZMK_INERTIA_MOVE;
#endif
#if DT_NODE_HAS_PROP(DYA_INERTIA_NODE, stop)
    s_staged.stop = (int32_t)DT_PROP(DYA_INERTIA_NODE, stop);
#else
    s_staged.stop = CONFIG_ZMK_INERTIA_STOP;
#endif
#else
    s_staged.friction = CONFIG_ZMK_INERTIA_FRICTION;
    s_staged.limit = CONFIG_ZMK_INERTIA_LIMIT;
    s_staged.decay_fast = CONFIG_ZMK_INERTIA_DECAY_FAST;
    s_staged.decay_slow = CONFIG_ZMK_INERTIA_DECAY_SLOW;
    s_staged.decay_tail = CONFIG_ZMK_INERTIA_DECAY_TAIL;
    s_staged.fast = CONFIG_ZMK_INERTIA_FAST;
    s_staged.slow = CONFIG_ZMK_INERTIA_SLOW;
    s_staged.start = CONFIG_ZMK_INERTIA_START;
    s_staged.move = CONFIG_ZMK_INERTIA_MOVE;
    s_staged.stop = CONFIG_ZMK_INERTIA_STOP;
#endif
    s_live = s_staged;
}

int dya_inertia_get(struct dya_inertia_cfg *out) {
    if (out == NULL) {
        return -EINVAL;
    }
    unsigned int key = irq_lock();
    *out = s_live;
    irq_unlock(key);
    return 0;
}

const struct device *dya_inertia_dev(void) { return s_dev; }

int dya_inertia_set_friction(int32_t v) {
    if (v < 0 || v > 1000) {
        return -ERANGE;
    }
    s_staged.friction = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_limit(int32_t v) {
    if (v < 1 || v > 4000) {
        return -ERANGE;
    }
    s_staged.limit = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_decay_fast(int32_t v) {
    if (v < 800 || v > 999) {
        return -ERANGE;
    }
    s_staged.decay_fast = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_decay_slow(int32_t v) {
    if (v < 800 || v > 999) {
        return -ERANGE;
    }
    s_staged.decay_slow = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_decay_tail(int32_t v) {
    if (v < 800 || v > 999) {
        return -ERANGE;
    }
    s_staged.decay_tail = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_fast(int32_t v) {
    if (v < 0 || v > 4000) {
        return -ERANGE;
    }
    s_staged.fast = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_slow(int32_t v) {
    if (v < 0 || v > 4000) {
        return -ERANGE;
    }
    s_staged.slow = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_start(int32_t v) {
    if (v < 1 || v > 2000) {
        return -ERANGE;
    }
    s_staged.start = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_move(int32_t v) {
    if (v < 1 || v > 2000) {
        return -ERANGE;
    }
    s_staged.move = v;
    dya_inertia_commit_locked();
    return 0;
}

int dya_inertia_set_stop(int32_t v) {
    if (v < 1 || v > 500) {
        return -ERANGE;
    }
    s_staged.stop = v;
    dya_inertia_commit_locked();
    return 0;
}

bool dya_inertia_resolve(const struct device *dev, int32_t *friction,
                         int32_t *limit, int32_t *decay_fast,
                         int32_t *decay_slow, int32_t *decay_tail,
                         int32_t *fast, int32_t *slow, int32_t *start,
                         int32_t *move, int32_t *stop) {
    if (dev == NULL || dev != s_dev) {
        return false; /* scroll_inertia_free 以外は DT のまま */
    }
    unsigned int key = irq_lock();
    struct dya_inertia_cfg cur = s_live;
    irq_unlock(key);
    if (friction != NULL) {
        *friction = cur.friction;
    }
    if (limit != NULL) {
        *limit = cur.limit;
    }
    if (decay_fast != NULL) {
        *decay_fast = cur.decay_fast;
    }
    if (decay_slow != NULL) {
        *decay_slow = cur.decay_slow;
    }
    if (decay_tail != NULL) {
        *decay_tail = cur.decay_tail;
    }
    if (fast != NULL) {
        *fast = cur.fast;
    }
    if (slow != NULL) {
        *slow = cur.slow;
    }
    if (start != NULL) {
        *start = cur.start;
    }
    if (move != NULL) {
        *move = cur.move;
    }
    if (stop != NULL) {
        *stop = cur.stop;
    }
    return true;
}

static int dya_inertia_runtime_init(void) {
#if DT_NODE_EXISTS(DYA_INERTIA_NODE)
    s_dev = DEVICE_DT_GET(DYA_INERTIA_NODE);
    if (!device_is_ready(s_dev)) {
        LOG_WRN("scroll_inertia_free device not ready");
    }
#else
    s_dev = NULL;
#endif
    dya_inertia_seed_from_dt();
    LOG_INF("dya_inertia init: fric=%d lim=%d df=%d ds=%d dt=%d fast=%d slow=%d "
            "start=%d move=%d stop=%d",
            (int)s_live.friction, (int)s_live.limit, (int)s_live.decay_fast,
            (int)s_live.decay_slow, (int)s_live.decay_tail, (int)s_live.fast,
            (int)s_live.slow, (int)s_live.start, (int)s_live.move,
            (int)s_live.stop);
    return 0;
}

SYS_INIT(dya_inertia_runtime_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
