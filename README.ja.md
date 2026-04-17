[English](./README.md) / **日本語**

# MikuMikuWorld
プロセカ用の譜面エディタ・譜面ビューアー。

## 必要な環境：
- macOS 10.14 以降
- OpenGL 3.3 対応の GPU

> 本フォークは macOS 専用です。Cocoa 移植に伴い Windows サポートは終了しました。

## ソースからビルド：
```bash
cmake -S . -B build
cmake --build build
open build/MikuMikuWorld.app
```
CMake 3.21+ と Xcode コマンドラインツールが必要です。GLFW と zlib は `FetchContent` で自動取得されます。

ユーザーデータ（設定・レイアウト・プリセット・オートセーブ）は `~/Library/Application Support/MikuMikuWorld/` に保存されます。

## オフライン動画レンダラー
譜面ファイルと音源から、ゲームプレイ風の動画を生成できます。`ffmpeg` が `PATH` 上に必要です。

```bash
MikuMikuWorld.app/Contents/MacOS/MikuMikuWorld --render \
  --score chart.sus \
  --audio bgm.mp3 \
  --out out.mp4 \
  --fps 60 --width 1920 --height 1080
```

対応譜面形式は `.mmws` / `.sus` / `.json` / `.json.gz`（Sonolus レベルデータ）。

ノート・ホールド・ヒットエフェクト・背景に加え、タップ／フリック／トレース／tick／ホールド本体ループの SE も出力動画に含まれます。SE は `res/sound/0N/` から内部で合成し、ffmpeg の `amix` で BGM と混合します。

主なオプション（すべて任意）：

| フラグ | 意味 |
| --- | --- |
| `--note-speed N` | ノート速度の上書き（1..12） |
| `--stage-cover N` | ステージカバーの上書き |
| `--bg-brightness N` | 背景の明るさ |
| `--background path` | 背景画像の差し替え |
| `--no-background` | 背景を無効化 |
| `--notes-skin 0\|1` | ノーツスキンの上書き |
| `--effects-profile N` | ヒットエフェクトプロファイルの上書き |
| `--mirror` | 譜面を左右反転 |
| `--audio-offset SEC` | BGM のオフセット調整 |
| `--se-profile 0\|1` | SE プロファイルの選択 |
| `--se-volume X` | SE 音量倍率（デフォルト 1.0） |
| `--no-se` | SE を無効化 |
| `--tail SEC` | 譜面終端の延長秒数（デフォルト 2.0） |
| `--ffmpeg path` | ffmpeg バイナリパスの指定 |

全オプションは `--render --help` で確認できます。

## 機能一覧：
- Sliding Universal Score（\*.sus）ファイルの入出力。
- BPM・拍子・ハイスピーの調整。
- 最大1920分音符までのカスタム分割。
- ノーツプリセットの作成・使用。
- カスタマイズ可能なキーボードショートカット。


## スクリーンショット：
![ミクワ](./docs/screenshot.png)
