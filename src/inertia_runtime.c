/*
 * DYA inertia scroll runtime — Phase2b: hook + RAM + custom-settings永続化。
 *
 * 対象は scroll_inertia_free のみ (axis=0/layer=5)。
 * 他インスタンスは DT のまま。central 側のみ。
 * dev->name 文字列比較は禁止。devポインタ同一性で判別する。
 *
 * もともとの機能はそのまま:
 * - CONFIG_ZMK_INERTIA_RUNTIME=n → 本ファイル自体ビルド除外、
 *   mjm 側 patch も #else 旧式のみで旧動作と同一。
 * - =y でも未書込み時は DT 実値を seed するため初動は同一。
 * - CONFIG_ZMK_CUSTOM_SETTINGS=n でもビルド可 (Phase2a相当、永続化なし)。
 */

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
#include <cormoran/zmk/custom_settings.h>
#include <zmk/event_manager.h>
#endif

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
    s_staged.enabled = CONFIG_ZMK_INERTIA_ENABLED;
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
    s_staged.enabled = CONFIG_ZMK_INERTIA_ENABLED;
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

#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)

/* ---- Phase2b: custom-settings 登録・購読 ---- */

#define DYA_INERTIA_SUBSYS "dya__inertia"

enum dya_inertia_field {
    DYA_IN_ENABLED = 0,
    DYA_IN_FRICTION = 1,
    DYA_IN_LIMIT = 2,
    DYA_IN_DECAY_FAST = 3,
    DYA_IN_DECAY_SLOW = 4,
    DYA_IN_DECAY_TAIL = 5,
    DYA_IN_FAST = 6,
    DYA_IN_SLOW = 7,
    DYA_IN_START = 8,
    DYA_IN_MOVE = 9,
    DYA_IN_STOP = 10,
    DYA_IN_COUNT = 11,
};

static const char *const s_keys[DYA_IN_COUNT] = {
    "enabled", "friction", "limit", "decay_fast", "decay_slow", "decay_tail",
    "fast",    "slow",     "start", "move",       "stop",
};

ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_enabled, DYA_INERTIA_SUBSYS, "enabled",
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_ENABLED),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(0, 1));

ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_friction, DYA_INERTIA_SUBSYS, "friction",
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_FRICTION),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(0, 1000));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_limit, DYA_INERTIA_SUBSYS, "limit", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_LIMIT),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(1, 4000));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_decay_fast, DYA_INERTIA_SUBSYS, "decay_fast",
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_DECAY_FAST),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(800, 999));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_decay_slow, DYA_INERTIA_SUBSYS, "decay_slow",
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_DECAY_SLOW),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(800, 999));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_decay_tail, DYA_INERTIA_SUBSYS, "decay_tail",
    ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_DECAY_TAIL),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(800, 999));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_fast, DYA_INERTIA_SUBSYS, "fast", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_FAST),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(0, 4000));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_slow, DYA_INERTIA_SUBSYS, "slow", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_SLOW),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(0, 4000));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_start, DYA_INERTIA_SUBSYS, "start", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_START),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(1, 2000));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_move, DYA_INERTIA_SUBSYS, "move", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_MOVE),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(1, 2000));
ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS(
    dya_in_stop, DYA_INERTIA_SUBSYS, "stop", ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
    ZMK_CUSTOM_SETTING_VALUE_INT32(CONFIG_ZMK_INERTIA_STOP),
    ZMK_CUSTOM_SETTING_CONFIDENTIALITY_RPC_PUBLIC, ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,
    ZMK_CUSTOM_SETTING_PERMISSION_SECURE, ZMK_CUSTOM_SETTING_RANGE_INT32(1, 500));

/* set_default用既定値。ポインタ保持のみのため static/BSS常駐必須。 */
static struct zmk_custom_setting_value s_def_vals[DYA_IN_COUNT];

static int32_t dya_inertia_staged_field(int field) {
    switch (field) {
    case DYA_IN_ENABLED:
        return s_staged.enabled;
    case DYA_IN_FRICTION:
        return s_staged.friction;
    case DYA_IN_LIMIT:
        return s_staged.limit;
    case DYA_IN_DECAY_FAST:
        return s_staged.decay_fast;
    case DYA_IN_DECAY_SLOW:
        return s_staged.decay_slow;
    case DYA_IN_DECAY_TAIL:
        return s_staged.decay_tail;
    case DYA_IN_FAST:
        return s_staged.fast;
    case DYA_IN_SLOW:
        return s_staged.slow;
    case DYA_IN_START:
        return s_staged.start;
    case DYA_IN_MOVE:
        return s_staged.move;
    case DYA_IN_STOP:
        return s_staged.stop;
    default:
        return 0;
    }
}

static void dya_inertia_install_defaults(void) {
    for (int field = 0; field < DYA_IN_COUNT; field++) {
        s_def_vals[field].type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32;
        s_def_vals[field].int32_value = dya_inertia_staged_field(field);
        const struct zmk_custom_setting *st =
            zmk_custom_setting_find(DYA_INERTIA_SUBSYS, s_keys[field]);
        if (st == NULL) {
            LOG_WRN("dya_inertia find %s failed", s_keys[field]);
            continue;
        }
        int rc = zmk_custom_setting_set_default(st, &s_def_vals[field]);
        if (rc < 0) {
            LOG_WRN("dya_inertia set_default %s failed: %d", s_keys[field], rc);
        }
    }
}

static bool dya_inertia_lookup_key(const char *key, int *field_out) {
    if (key == NULL) {
        return false;
    }
    for (int field = 0; field < DYA_IN_COUNT; field++) {
        if (strcmp(key, s_keys[field]) == 0) {
            if (field_out != NULL) {
                *field_out = field;
            }
            return true;
        }
    }
    return false;
}

static int dya_inertia_apply_value(int field, int32_t v) {
    if (field < 0 || field >= DYA_IN_COUNT) {
        return -EINVAL;
    }
    switch (field) {
    case DYA_IN_ENABLED:
        if (v < 0 || v > 1) {
            return -ERANGE;
        }
        s_staged.enabled = v;
        break;
    case DYA_IN_FRICTION:
        if (v < 0 || v > 1000) {
            return -ERANGE;
        }
        s_staged.friction = v;
        break;
    case DYA_IN_LIMIT:
        if (v < 1 || v > 4000) {
            return -ERANGE;
        }
        s_staged.limit = v;
        break;
    case DYA_IN_DECAY_FAST:
        if (v < 800 || v > 999) {
            return -ERANGE;
        }
        s_staged.decay_fast = v;
        break;
    case DYA_IN_DECAY_SLOW:
        if (v < 800 || v > 999) {
            return -ERANGE;
        }
        s_staged.decay_slow = v;
        break;
    case DYA_IN_DECAY_TAIL:
        if (v < 800 || v > 999) {
            return -ERANGE;
        }
        s_staged.decay_tail = v;
        break;
    case DYA_IN_FAST:
        if (v < 0 || v > 4000) {
            return -ERANGE;
        }
        s_staged.fast = v;
        break;
    case DYA_IN_SLOW:
        if (v < 0 || v > 4000) {
            return -ERANGE;
        }
        s_staged.slow = v;
        break;
    case DYA_IN_START:
        if (v < 1 || v > 2000) {
            return -ERANGE;
        }
        s_staged.start = v;
        break;
    case DYA_IN_MOVE:
        if (v < 1 || v > 2000) {
            return -ERANGE;
        }
        s_staged.move = v;
        break;
    case DYA_IN_STOP:
        if (v < 1 || v > 500) {
            return -ERANGE;
        }
        s_staged.stop = v;
        break;
    default:
        return -EINVAL;
    }
    dya_inertia_commit_locked();
    return 0;
}

static void dya_inertia_reload_all(void) {
    for (int field = 0; field < DYA_IN_COUNT; field++) {
        struct zmk_custom_setting_value v;
        int rc = zmk_custom_setting_read_by_key(DYA_INERTIA_SUBSYS, s_keys[field], &v);
        if (rc < 0) {
            LOG_WRN("dya_inertia reload %s read failed: %d", s_keys[field], rc);
            continue;
        }
        if (v.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) {
            LOG_WRN("dya_inertia reload %s bad type %d", s_keys[field], (int)v.type);
            continue;
        }
        rc = dya_inertia_apply_value(field, v.int32_value);
        if (rc < 0) {
            LOG_WRN("dya_inertia reload %s out of range %d", s_keys[field],
                    (int)v.int32_value);
        }
    }
}

static int dya_inertia_settings_listener(const zmk_event_t *eh) {
    const struct zmk_custom_setting_changed *ch = as_zmk_custom_setting_changed(eh);
    if (ch != NULL) {
        if (ch->setting == NULL || ch->setting->key == NULL) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        if (ch->setting->custom_subsystem_id == NULL ||
            strcmp(ch->setting->custom_subsystem_id, DYA_INERTIA_SUBSYS) != 0) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        int field = -1;
        if (!dya_inertia_lookup_key(ch->setting->key, &field)) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        struct zmk_custom_setting_value v;
        if (zmk_custom_setting_read(ch->setting, &v) < 0) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        if (v.type != ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        int rc = dya_inertia_apply_value(field, v.int32_value);
        if (rc < 0) {
            LOG_WRN("dya_inertia apply %s=%d rejected: %d", ch->setting->key,
                    (int)v.int32_value, rc);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (as_zmk_custom_settings_initialized(eh) != NULL) {
        dya_inertia_reload_all();
        LOG_INF("dya_inertia initialized: fric=%d lim=%d df=%d ds=%d dt=%d",
                (int)s_live.friction, (int)s_live.limit, (int)s_live.decay_fast,
                (int)s_live.decay_slow, (int)s_live.decay_tail);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dya_inertia_runtime, dya_inertia_settings_listener);
ZMK_SUBSCRIPTION(dya_inertia_runtime, zmk_custom_setting_changed);
ZMK_SUBSCRIPTION(dya_inertia_runtime, zmk_custom_settings_initialized);

#endif /* IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS) */

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

/* set_*: 先にstoreへMEMORY書込みし、成功時のみRAMへcommit。
 * custom-settings無効時はRAMのみ (Phase2a動作)。 */
int dya_inertia_set_enabled(int32_t v) {
    if (v < 0 || v > 1) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_ENABLED], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_ENABLED, v);
#else
    s_staged.enabled = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_friction(int32_t v) {
    if (v < 0 || v > 1000) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_FRICTION], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_FRICTION, v);
#else
    s_staged.friction = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_limit(int32_t v) {
    if (v < 1 || v > 4000) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_LIMIT], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_LIMIT, v);
#else
    s_staged.limit = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_decay_fast(int32_t v) {
    if (v < 800 || v > 999) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_DECAY_FAST], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_DECAY_FAST, v);
#else
    s_staged.decay_fast = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_decay_slow(int32_t v) {
    if (v < 800 || v > 999) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_DECAY_SLOW], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_DECAY_SLOW, v);
#else
    s_staged.decay_slow = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_decay_tail(int32_t v) {
    if (v < 800 || v > 999) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_DECAY_TAIL], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_DECAY_TAIL, v);
#else
    s_staged.decay_tail = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_fast(int32_t v) {
    if (v < 0 || v > 4000) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_FAST], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_FAST, v);
#else
    s_staged.fast = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_slow(int32_t v) {
    if (v < 0 || v > 4000) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_SLOW], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_SLOW, v);
#else
    s_staged.slow = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_start(int32_t v) {
    if (v < 1 || v > 2000) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_START], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_START, v);
#else
    s_staged.start = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_move(int32_t v) {
    if (v < 1 || v > 2000) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_MOVE], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_MOVE, v);
#else
    s_staged.move = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_set_stop(int32_t v) {
    if (v < 1 || v > 500) {
        return -ERANGE;
    }
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    struct zmk_custom_setting_value sv = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_INT32,
                                          .int32_value = v};
    int rc = zmk_custom_setting_write_by_key(DYA_INERTIA_SUBSYS, s_keys[DYA_IN_STOP], &sv,
                                             ZMK_CUSTOM_SETTING_WRITE_MODE_MEMORY);
    if (rc < 0) {
        return rc;
    }
    return dya_inertia_apply_value(DYA_IN_STOP, v);
#else
    s_staged.stop = v;
    dya_inertia_commit_locked();
    return 0;
#endif
}

int dya_inertia_save(void) {
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    uint32_t affected = 0;
    int rc = zmk_custom_settings_save_scope(DYA_INERTIA_SUBSYS, NULL, NULL, &affected);
    LOG_INF("dya_inertia save rc=%d affected=%u", rc, (unsigned int)affected);
    return rc;
#else
    return -ENOSYS;
#endif
}

int dya_inertia_discard(void) {
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    uint32_t affected = 0;
    int rc = zmk_custom_settings_discard_scope(DYA_INERTIA_SUBSYS, NULL, NULL, &affected);
    LOG_INF("dya_inertia discard rc=%d affected=%u", rc, (unsigned int)affected);
    if (rc == 0) {
        dya_inertia_reload_all();
    }
    return rc;
#else
    return -ENOSYS;
#endif
}

bool dya_inertia_resolve(const struct device *dev, int32_t *enabled,
                         int32_t *friction, int32_t *limit, int32_t *decay_fast,
                         int32_t *decay_slow, int32_t *decay_tail,
                         int32_t *fast, int32_t *slow, int32_t *start,
                         int32_t *move, int32_t *stop) {
    if (dev == NULL || dev != s_dev) {
        return false; /* scroll_inertia_free 以外は DT のまま */
    }
    unsigned int key = irq_lock();
    struct dya_inertia_cfg cur = s_live;
    irq_unlock(key);
    if (enabled != NULL) {
        *enabled = cur.enabled;
    }
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
#if IS_ENABLED(CONFIG_ZMK_CUSTOM_SETTINGS)
    dya_inertia_install_defaults();
#endif
    LOG_INF("dya_inertia init: en=%d fric=%d lim=%d df=%d ds=%d dt=%d fast=%d "
            "slow=%d start=%d move=%d stop=%d",
            (int)s_live.enabled, (int)s_live.friction, (int)s_live.limit,
            (int)s_live.decay_fast, (int)s_live.decay_slow,
            (int)s_live.decay_tail, (int)s_live.fast, (int)s_live.slow,
            (int)s_live.start, (int)s_live.move, (int)s_live.stop);
    return 0;
}

SYS_INIT(dya_inertia_runtime_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
