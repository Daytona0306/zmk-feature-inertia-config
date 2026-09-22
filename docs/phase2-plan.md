# Phase2 計画書 (hook + store) — inertia 版

対象: `Daytona0306/zmk-feature-inertia-config` + mjm `zmk-input-processor-scroll-inertia` 最小hook。
型流用: `Daytona0306/zmk-feature-holdtap-config` (RAMシャドウ+ダブルバッファ+irq_lock commit+weak hook)。

## 1. 本体改変の最小性

変更は mjm の `src/input_processor_scroll_inertia.c` のみ、約40行。
内容: weak `dya_inertia_resolve()` + effective解決 + tick/event/stop_detect の
cfg 取得直後に差し替え。`CONFIG_ZMK_INERTIA_RUNTIME=n` では `#else` 旧式のみで旧動作と同一。
layer cb / span cap 等の safety 系は live のまま (sub-tick 対策を損なわない)。
他インスタンスは resolve false で DT のまま。

## 2. 段階分け

### Phase2a (hook + RAMのみ、custom-settings不要) — 本コミット
- 含む: `include/dya_inertia_runtime.h`、RAMシャドウ+ダブルバッファ+irq_lock commit、
  DT既定seed (scroll_inertia_free 実値)、get/set RAM、mjm patch、Kconfig 10項目。
- 対象10項目: friction / limit / decay-fast/slow/tail / fast / slow / start / move / stop。
  gain/blend/scale/tick 等は DT 固定のまま (もとの機能そのまま)。
- 除外: `ZMK_CUSTOM_SETTING_DEFINE_WITH_CONSTRAINTS`、イベント購読、永続化、RPC。
- 終了条件: `CONFIG_ZMK_INERTIA_RUNTIME=y` + `CONFIG_ZMK_CUSTOM_SETTINGS=n` でも
  既存 torabo ビルドが通り、未書込み時は DT と同一挙動。

### Phase2b (永続化)
- Phase2a + DEFINE×10 (INT32+RANGE) + イベント購読 + 明示Saveのみ永続化。
- 要 `CONFIG_ZMK_CUSTOM_SETTINGS=y`。毎tick PERSIST禁止。

## 3. ロールバック

- `CONFIG_ZMK_INERTIA_RUNTIME=n`: モジュールCMakeで除外、本体patchは旧式のみ、DYAログなし。
- DT既定フォールバック: 未書込み時の有効値はDT実値。`settings_reset` でDT既定に戻る。

## 4. テスト計画 (もとの機能そのまま確認)

| # | 項目 | 手順 | 期待 |
|---|---|---|---|
| T1 | DT seed | 起動ログ確認 | fric=35 lim=900 df=992 ds=980 dt=975 fast=250 slow=60 start=40 move=60 stop=1 |
| T2 | 未書込み同一 | 変更なしで弾き | 従来の慣性と同一 |
| T3 | live反映 | frictionのみ35→100 | 次tickから短く止まる、当該coastも即変化 (Phase2a live仕様) |
| T4 | 範囲外 | decay-fast 1200書込み | -ERANGE拒否、旧値保持 |
| T5 | 他PC波及なし | 他inertiaノード追加 | DTまま |
| T6 | ロールバック+peripheral | nでビルド | 旧動作同一、peripheral混入なし |

運用整合: Studioで変えた値はリポジトリに残らない。確定値はDT/Kconfig既定へ書き戻す。
