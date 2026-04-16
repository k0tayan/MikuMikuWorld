# Mac 移植 決定事項

Phase 1 調査後に確定した意思決定の記録。Phase 2 以降の実装指針となる。

## 基本方針

- Windows 互換性は捨てる。Mac 専用アプリとして再出発
- プラットフォーム抽象レイヤ（PAL）は設計しない。Win32 コードは削除し、Cocoa / POSIX を直接呼ぶ
- ビルドシステムは CMake 一本化。`.vcxproj` / `.sln` は削除
- 配布形態は `.app` バンドル

## 機能スコープ（Q1〜Q6）

### Q1 タイトルバーのダークモード

OS 設定に追従（Cocoa 既定動作）。`DwmSetWindowAttribute` 相当の実装はしない。
→ `UI.cpp` の `setDarkMode` / `isSystemDarkMode` は削除。

### Q2 リサイズ/移動ドラッグ中の連続描画

許容。Mac でリサイズドラッグ中にタイムライン内容が静止しても良い。
→ `main.cpp` の `WM_TIMER` / `WM_ENTERSIZEMOVE` 相当は実装しない。

### Q3 ファイルダイアログの filterIndex

廃止。呼び出し側で拡張子を事前確定してからダイアログを開く。
→ `File.cpp` / `File.h` の `filterIndex` メンバ削除。保存ダイアログは単一フィルタのみ。

### Q4 キーボードショートカットの修飾キー

Mac では Ctrl → Cmd(Super) にリマップ。ImGui の `io.ConfigMacOSXBehaviors = true` を有効化し、`InputBinding` 層でも `ImGuiMod_Ctrl` を `ImGuiMod_Super` に読み替える変換を追加。
表示も `⌘S` 形式。

### Q5 メッセージボックスのアイコン

Question → Information にフォールバック。`NSAlert` に合わせて使用アイコンを絞る。
`Abort / Retry / Ignore` ボタン組み合わせは現状未使用のため対応不要。

### Q6 フルスクリーン

`glfwSetWindowMonitor` ベースを維持（モニタ乗っ取り型）。
Mac ネイティブフルスクリーン（緑ボタン / Mission Control 分離）は将来タスク。

## 必須書き換え（選択肢なし）

### パスセパレータ正規化

`res\\...` 形式のハードコード 11 箇所を `/` に統一:

- `Application.cpp:321-347`
- `OpenGlLoader.cpp:87`
- `ScorePreview.cpp:72,444,445`
- `AudioManager.cpp:88`
- `ScoreEditorTimeline.cpp:2346`

### データ書き込み先の分離

`.app` バンドル内は読み取り専用が前提のため、書き込みデータは `~/Library/Application Support/MikuMikuWorld/` 配下へ:

- `app_config.json`（設定）
- `imgui.ini`（ImGui レイアウト）
- `library/`（プリセット）
- オートセーブ

`appDir` を 2 つに分離:
- リソース読み取り基点: `.app/Contents/Resources/`
- データ書き込み基点: `~/Library/Application Support/MikuMikuWorld/`

## 改訂版フェーズ計画

### Phase A: 地ならし（0.5日）

1. CLAUDE.md を Mac 前提に書き換え
2. 以下を削除:
   - `MikuMikuWorld.sln`
   - `MikuMikuWorld/MikuMikuWorld.vcxproj`
   - `MikuMikuWorld/MikuMikuWorld.vcxproj.filters`
   - `MikuMikuWorld/MikuMikuWorld.rc`
   - `MikuMikuWorld/resource.h`
   - `MikuMikuWorld/mmw_icon.ico`
   - `Depends/GLFW/lib/`
   - `Depends/zlib/lib/`
3. 削除ファイルへの参照が残っていないか grep 確認

### Phase B: CMake 化（1日、Mac ビルド成功まで）

1. `CMakeLists.txt`（ルート） + `MikuMikuWorld/CMakeLists.txt`
2. GLFW / zlib は `FetchContent_Declare` で取得（Homebrew 依存を避ける）
3. `.app` バンドル設定（`MACOSX_BUNDLE`）、リソースを `.app/Contents/Resources/` へ
4. 検証: `cmake --build` で Win32 シンボル未定義エラーまで到達

### Phase C: Win32 削除・Cocoa 置換（3〜5日）

inventory.md の項目を順次消化。抽象化なし、直接書き換え。

ファイル単位の作業:
- `main.cpp`: `wndProc` / `WM_*` / `CommandLineToArgvW` を全削除。標準 `int main(int argc, char** argv)` + `glfwSetDropCallback`
- `Application.cpp` / `Application.h`: `getVersion` を CMake マクロ `APP_VERSION_STRING` に置換、HWND / SetTimer / SetWindowLongPtrW 全削除
- `IO.cpp`: `MessageBoxExW` → `Platform/Cocoa/MessageBox.mm`（NSAlert）。`mbToWideStr` / `wideStringToMb` を削除（UTF-8 std::string 統一）
- `File.cpp` / `File.h`: `OPENFILENAMEW` → `Platform/Cocoa/FileDialog.mm`（NSOpenPanel/NSSavePanel）。wide 版 `open()` オーバーロードと `openFilenameW` メンバ削除、`filterIndex` 削除
- `UI.cpp`: Dwmapi / RegGetValueW / `isSystemDarkMode` / `setDarkMode` を全削除（Q1）
- `Utilities.cpp`: `GetUserDefaultLocaleName` → `Platform/Cocoa/Locale.mm`（NSLocale）
- `ScoreEditor.cpp`: `ShellExecuteW` → `system("open ...")`（.mm 不要）
- `BinaryReader.cpp` / `BinaryWriter.cpp` / `Rendering/Shader.cpp` / `ApplicationConfiguration.cpp` / `ResourceManager.cpp` / `NotesPreset.cpp`: wide 経由を削除し UTF-8 `std::string` 直接
- `Audio/AudioManager.cpp` / `Audio/Sound.cpp`: `ma_sound_init_from_file_w` → `ma_sound_init_from_file`
- 全体: `res\\...` 11 箇所を `/` に
- `InputBinding`: Ctrl → Super 変換層追加（Q4）

検証: Mac でアプリが起動し、空譜面が表示される。

### Phase D: Mac ネイティブ統合（1〜2日）

- 書き込みパスを `~/Library/Application Support/MikuMikuWorld/` に移動
- `appDir` をリソース / データの 2 基点に分離
- `Info.plist`:
  - `CFBundleVersion` / `CFBundleShortVersionString`
  - `CFBundleIdentifier`（例: `com.crash5band.MikuMikuWorld`）
  - `NSHighResolutionCapable = YES`
- `mmw_icon.icns` を既存 png から生成、Info.plist で指定
- `io.ConfigMacOSXBehaviors = true`

検証: ファイル開閉、保存、`.mmws` / `.sus` / Sonolus ラウンドトリップ、オーディオ再生、プレビュー描画すべて Mac で動く。

### Phase E: 仕上げ（0.5日）

- README 更新（CMake + Xcode のビルド手順）
- CLAUDE.md の「ビルド」「プラットフォーム前提」「全体アーキテクチャ」セクション最終化
- GitHub Actions で macOS runner ビルド追加（任意）

## 当初比で削除された作業

- PAL インターフェース設計 → 不要
- `#ifdef _WIN32` 分岐の網羅 → 不要
- Windows 回帰テスト → 不要
- `.vcxproj` 並存維持 → 不要

## 環境前提

- 開発マシン: macOS（Darwin 25.2.0）
- Windows ビルド検証は行わない（Windows 互換は放棄）
- 最小 macOS サポート: 10.14（OpenGL 3.3 が動作する範囲）
