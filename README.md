# zmk-feature-inertia-config

DYA Studio 向け慣性スクロール実行時調整モジュール。
mjm `scroll_inertia_free` (axis=0/layer=5) の friction / limit / decay系等を
再フラッシュなしで変える。型は `zmk-feature-holdtap-config` 流用
(RAMシャドウ+ダブルバッファ+irq_lock commit+weak hook)。

もともとの機能はそのまま:
- `CONFIG_ZMK_INERTIA_RUNTIME=n` → mjm 素の DT 動作と同一。
- `=y` でも未書込み時は DT 実値を seed するため初動は同一。

## 段階

- Phase1: 土台 (本雛形)。Kconfig 登録のみでビルド確認
- Phase2a: hook + RAMのみ (本コミット、custom-settings 不要)
- Phase2b: 永続化 (custom-settings INT32+RANGE×10)
- Phase3: RPC (Studio custom RPC)

## 使い方 (torabo-tsuki 側、Phase2b以降)

`config/west.yml` に追加:

```yaml
    - name: zmk-feature-inertia-config
      remote: Daytona0306
      revision: <SHA>  # track: main
```

`snippets/split-central/split-central.conf` に追加 (central側のみ):

```ini
CONFIG_ZMK_INERTIA_RUNTIME=y
```

mjm 側 patch (`docs/mjm-hook.patch`) は Daytona fork 側で適用。
cormoran 追随は patch のみ更新で維持。
