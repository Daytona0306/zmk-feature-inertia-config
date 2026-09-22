/*
 * DYA inertia runtime — Studio custom subsystem stub登録。
 *
 * Phase3 で本物ハンドラに置き換えるまでの stub。
 */

#include <stdbool.h>

#include <zmk/studio/custom.h>

static struct zmk_rpc_custom_subsystem_meta dya_inertia_rpc_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

static bool dya_inertia_rpc_handler(const zmk_custom_CallRequest *req, pb_callback_t *res) {
    ARG_UNUSED(req);
    ARG_UNUSED(res);
    return false;
}

ZMK_RPC_CUSTOM_SUBSYSTEM(dya__inertia, &dya_inertia_rpc_meta, dya_inertia_rpc_handler);
