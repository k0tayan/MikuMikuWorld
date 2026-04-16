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

## 機能一覧：
- Sliding Universal Score（\*.sus）ファイルの入出力。
- BPM・拍子・ハイスピーの調整。
- 最大1920分音符までのカスタム分割。
- ノーツプリセットの作成・使用。
- カスタマイズ可能なキーボードショートカット。


## スクリーンショット：
![ミクワ](./docs/screenshot.png)
