# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## プロジェクト概要

MikuMikuWorld はプロセカ（Project Sekai）向けの macOS 専用の譜面エディタ兼プレビュアー。`.sus`（Sliding Universal Score）、独自バイナリ形式の `.mmws`、Sonolus のレベルデータの読み書きに対応する。

元は Windows 専用として開発されていたが、本リポジトリは macOS への移植版。Windows 互換性は維持していない。

## ビルド

macOS + AppleClang + CMake でのみビルド可能。最小サポート macOS は 10.14（OpenGL 3.3 Core Profile が動作する範囲）。

- CMake 3.21 以降、C++17
- 依存ライブラリ（GLFW、zlib）は CMake `FetchContent` でソース取得しインツリービルド。Homebrew やシステム依存は不要
- ヘッダーオンリー依存は `Depends/` 配下: `DirectXMath-master`、`glad`、`json`、`miniaudio`、`stb_image`、`stb_vorbis`
- Cocoa API を叩くため Objective-C++ (`.mm`) ファイルを含む。リンクに `-framework Cocoa`、`-framework OpenGL`、`-framework IOKit`、`-framework CoreFoundation`、`-framework AppKit` が必要（CMake が自動付与）

ビルド手順:

```bash
cmake -S . -B build -G "Xcode"            # Xcode プロジェクト生成
cmake --build build --config Release      # ビルド
open build/Release/MikuMikuWorld.app      # 起動
```

または:

```bash
cmake -S . -B build
cmake --build build
open build/MikuMikuWorld.app
```

出力は `.app` バンドル形式。`MikuMikuWorld/res/` はビルド時に `.app/Contents/Resources/` へコピーされる（CMake の `add_custom_command(POST_BUILD)` 経由）。この `res/` が無いとアプリは起動しない。

バージョン文字列は CMake 側で `APP_VERSION_STRING` マクロとしてコンパイラに渡す（`CMakeLists.txt` の `project(... VERSION x.y.z)` から派生）。

## プラットフォーム前提

macOS の Cocoa / AppKit / Foundation を前提とする:

- ウィンドウ管理は GLFW のクロスプラットフォーム API のみを使用。`HWND` / `WndProc` 相当の低レベルフックは存在しない
- ドラッグ＆ドロップは `glfwSetDropCallback` 経由（UTF-8 パス受信）
- ネイティブダイアログ / メッセージボックス / システムロケール / ダークモード判定は `Platform/Cocoa/*.mm` が担当
  - `MessageBox.mm`: `NSAlert`
  - `FileDialog.mm`: `NSOpenPanel` / `NSSavePanel`
  - `Locale.mm`: `NSLocale`
- タイトルバーのダークモードは OS 設定に追従（Cocoa 既定挙動、上書きしない）
- OS シェル連携（URL / フォルダを開く）は `system("open ...")` で処理。Objective-C++ 不要
- ファイルシステム経路は UTF-8 の `std::string` に統一。wide-string (`std::wstring`) 経由のパス処理は存在しない

キーボードショートカット:

- ImGui の `io.ConfigMacOSXBehaviors = true` を有効化
- `InputBinding` 層で `ImGuiMod_Ctrl` を `ImGuiMod_Super`（Cmd キー）として扱う。設定ファイル上の Ctrl バインディングは起動時に Cmd に読み替えられる
- 表示も `⌘S` 形式

## データ書き込み先

`.app` バンドル内は読み取り専用が前提（Gatekeeper / 署名の制約）。書き込みデータは `~/Library/Application Support/MikuMikuWorld/` に置く:

- `app_config.json`（設定）
- `imgui.ini`（ImGui レイアウト）
- `library/`（プリセット）
- オートセーブ

一方、リソース（シェーダ、テクスチャ、フォント、翻訳）は `.app/Contents/Resources/` から読み取る。`Application::getAppDir()` 相当は「リソース読み取り基点」と「データ書き込み基点」の 2 つに分離されている。

## 全体アーキテクチャ

エントリポイント: `main.cpp` が標準 `int main(int argc, char** argv)` でグローバルの `Application app` を生成し、`initialize(dir)` を呼び、CLI 引数のファイルを `appendOpenFile` でキューに入れ、`run()` を実行する。未捕捉の例外時には保存を促し、設定を書き出してから終了する。

所有関係:

```
Application
 ├── GLFWwindow（glad 経由の OpenGL 3.3 Core Profile コンテキスト）
 ├── ImGuiManager            // ImGui + GLFW + OpenGL3 バックエンド、フォント、テーマ
 └── ScoreEditor
      └── ScoreContext       // 現在開いているドキュメント
           ├── Score         // 純粋なデータ
           ├── HistoryManager
           ├── Audio::AudioManager（miniaudio / CoreAudio バックエンド）
           ├── Engine::DrawData（プレビュー）
           ├── selectedNotes、pasteData
           └── WaveformMipChain（L/R）
```

主要サブシステム（ディレクトリではなくファイル単位で整理。多くは `MikuMikuWorld/` 直下に並置されている）:

- データモデル — `Score.h`、`Note.h`、`Tempo.h`、`NoteTypes.h`。`Score` は単なるデータ保持。ノート（`std::map<int, Note>`）、ホールド、テンポ変化、拍子、ハイスピ変化、スキル、フィーバーを保持する。ID は整数で `nextSkillID` 等はグローバル。編集するたびに新しい `Score` スナップショットを生成し `HistoryManager` に push する。
- エディタ UI — `ScoreEditor.{h,cpp}` が全体を取りまとめる。`ScoreEditorTimeline.{h,cpp}` がメインのタイムラインキャンバス（大きいファイルで入力／当たり判定／描画を担う）。`ScoreEditorWindows.{h,cpp}` はダイアログとサイドパネル群。`UI.{h,cpp}` は ImGui の共通パターンのラッパー。
- プレビュー／再生 — `ScorePreview.{h,cpp}` ＋ `PreviewEngine.{h,cpp}` ＋ `PreviewData.{h,cpp}` が譜面をゲーム内表示に近い形で描画する。`EffectView` と `Particle` がヒットエフェクトを担当。
- シリアライズ — 抽象クラス `ScoreSerializer` と具象実装: `NativeScoreSerializer`（`.mmws`、`BinaryReader` / `BinaryWriter` 経由）、`SusSerializer` / `SusParser` / `SusExporter`（`.sus` テキスト）、`SonolusSerializer`（Sonolus のレベル／JSON）。`ScoreSerializeController` ＋ `ScoreSerializeWindow` が非同期の保存／読み込み UI を駆動する。ディスパッチキーは `SerializeFormat` 列挙値。
- レンダリング — `Rendering/` が OpenGL 3.3 Core をラップする: `Shader`、`Texture`、`Framebuffer`、`Renderer`、`Camera`、`Quad`、`Sprite`、`VertexBuffer`。シェーダとテクスチャは `res/` 配下（実行時は `.app/Contents/Resources/`）。
- オーディオ — `Audio/AudioManager` が miniaudio エンジンと `Sound` を保有する。`Waveform.h` はタイムラインに波形を描くためのミップチェーン。OGG は `stb_vorbis` でデコードする。macOS では CoreAudio バックエンドが自動選択される。
- 設定 / 入力 — `ApplicationConfiguration` が `app_config.json`（テーマ、アクセントカラー、最近使ったファイル、ショートカット）を永続化する。`InputBinding` がカスタマイズ可能なキーボードショートカットを実装する。ドラッグ＆ドロップは `main.cpp` の `glfwSetDropCallback` → `Application::appendOpenFile` で配線されている。
- ローカライズ — `Localization` / `Language` が `res/i18n/` から翻訳を読み込む。`DefaultLanguage.h` が英語のベースライン。システムロケール取得は `Platform/Cocoa/Locale.mm`。

## リソース

`MikuMikuWorld/res/`（`editor`、`effect`、`fonts`、`i18n`、`notes`、`shaders`、`sound`）は実行時に必須の資源。シェーダ、テクスチャ、フォント、翻訳を追加する際はここに配置すれば、ポストビルドで `.app/Contents/Resources/` へコピーされる。

パスセパレータは全コードベースを通じて `/` を使用する（`\\` のハードコードはしない）。

## このリポジトリで観察されるコード規約

- プロジェクトのコードはすべて `namespace MikuMikuWorld`（エイリアス `mmw`）配下。オーディオは `namespace MikuMikuWorld::Audio`、プレビュー描画データは `namespace MikuMikuWorld::Engine`、プラットフォーム固有コードは `namespace MikuMikuWorld::Platform`。
- 致命的でないエラーは例外ではなく `IO::messageBox(...)` で通知する。`main.cpp` のトップレベル例外ハンドラは最後の保存機会を提供するためのもの。
- インデントはタブ。ヘッダは `#pragma once`。インクルードはリポジトリ内ヘッダを二重引用符、サードパーティ／システムヘッダを山括弧で記述する。
- `IMGUI_DEFINE_MATH_OPERATORS` は ImGui ヘッダより前に定義する。これらを include する新しい翻訳単位を追加する際はこの順序を守ること。
- Cocoa API を呼ぶ翻訳単位は `.mm` 拡張子にし、`Platform/Cocoa/` 配下に置く。C++ 側からは `extern "C"` 不要のプレーンな関数インターフェースで呼び出す。
- 文字列は UTF-8 `std::string` に統一。`std::wstring` を新規に導入しない。
