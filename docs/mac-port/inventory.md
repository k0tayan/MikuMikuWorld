# Mac 移植 依存棚卸し (Phase 1)

本ドキュメントは Phase 1（調査のみ、コード変更なし）の成果物。`main` ブランチ時点のコードベースに対し、Windows 固有依存と Mac 移植時の代替方針を列挙する。分類は以下の 3 種。

- `GLFW代替可`: GLFW の既存のクロスプラットフォーム API で置換できる
- `PAL化必要`: 機能自体は Mac にも必要。プラットフォーム抽象レイヤ (Platform Abstraction Layer) を設計して Win32 実装と Cocoa 実装を分岐させる
- `不要化可能`: Windows 固有のハックで、Mac では無条件に不要、または別手段で代替可能

（ImGui 同梱コード `MikuMikuWorld/ImGui/*` は既に `#ifdef _WIN32` で分岐済みのため対象外。glad / stb / json も対象外。）

## 1. エントリポイント・コマンドライン

| 参照箇所 | Win32 API / 機能 | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `MikuMikuWorld/main.cpp:4` | `#include "Windows.h"` | PAL化必要 | `#ifdef _WIN32` で囲う |
| `MikuMikuWorld/main.cpp:9-13` | `main()` の代わりに `int main()`+`CommandLineToArgvW(GetCommandLineW())` で wide 引数取得 | 不要化可能 | Mac の `argv` は UTF-8。標準 `int main(int argc, char** argv)` に統一し、Windows のみ wide 変換経路を通す |
| `MikuMikuWorld/main.cpp:22,29,98` | `IO::wideStringToMb(args[i])` 呼び出し | 不要化可能 | 同上 |

## 2. ウィンドウ・WndProc フック

`main.cpp:63-118` の `wndProc` と `Application.cpp:381-398` の設置処理。いずれも HWND を取得して GLFW の WndProc を差し替える Windows 固有実装。

| 参照箇所 | 目的 | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `Application.h:10-11`, `Application.cpp:383,391-392` | `glfwGetWin32Window` + `SetWindowLongPtrW` で WndProc フック | PAL化必要 | Mac では不要。下記メッセージのうち必要なものは GLFW コールバックで受ける |
| `main.cpp:67-77` (WM_TIMER) + `Application.cpp:395-396` (SetTimer) | リサイズ/移動ドラッグ中も描画継続させるためのポンプ | 不要化可能 (Macでは) | Mac の Cocoa ではリサイズ中もメインループが通常動作するか、modal loop は GLFW が処理。MVP では省略可。問題が出たら別タスク |
| `main.cpp:79-85` (WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE) | 同上のためのフラグ更新 | 不要化可能 | 同上 |
| `main.cpp:87-104` (WM_DROPFILES) + `Application.cpp:398` (DragAcceptFiles) + `DragQueryFileW` | ドラッグ＆ドロップ受信 | GLFW代替可 | `glfwSetDropCallback(window, cb)` で `const char* paths[]` (UTF-8) が届く。両 OS で同じコード |
| `main.cpp:105-110` (WM_SETTINGCHANGE → ImmersiveColorSet) | OS のライトモード/ダークモード切替を検知 | PAL化必要 | Mac: `NSDistributedNotificationCenter` の `AppleInterfaceThemeChangedNotification` を Objective-C++ 側で購読、または起動時のみ読むなら不要 |
| `main.cpp:114,117` (`CallWindowProcW`/`DefWindowProcW`) | 残りメッセージを既存 WndProc に委譲 | 不要化可能 | 上記を GLFW コールバック化すれば WndProc 自体が不要 |
| `Application.h:22-37` `WindowState` のうち `windowHandle` (void*), `windowTimerId` (UINT_PTR), `windowDragging` | WndProc と共有する Win32 状態 | PAL化必要 | `windowHandle` は PAL 経由でネイティブハンドルを扱う場面（MessageBox の親指定など）でのみ必要 |

## 3. タイトルバー / ダークモード

| 参照箇所 | Win32 API | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `UI.cpp:14-18` | `GLFW_EXPOSE_NATIVE_WIN32` / `<Windows.h>` / `<dwmapi.h>` | PAL化必要 | `#ifdef _WIN32` で囲う |
| `UI.cpp:420-440` `isSystemDarkMode()` | `RegGetValueW` でレジストリから `AppsUseLightTheme` を読む | PAL化必要 | Mac: `[NSApp effectiveAppearance].name == NSAppearanceNameDarkAqua`（.mm） |
| `UI.cpp:442-451` `setDarkMode()` | `DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ...)` でタイトルバーを黒く | PAL化必要 | Mac: `[window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]]`。Cocoa 自動追従でもよい（nice-to-have） |

## 4. メッセージボックス / 通知

| 参照箇所 | Win32 API | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `IO.cpp:1-45` `messageBox()` | `MessageBoxExW` | PAL化必要 | Mac: `NSAlert`（.mm）。ボタン enum / icon enum マッピングをそろえる |

## 5. ネイティブファイルダイアログ

| 参照箇所 | Win32 API | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `File.cpp:237-312` `FileDialog::showFileDialog` | `OPENFILENAMEW` + `GetOpenFileNameW` / `GetSaveFileNameW` | PAL化必要 | Mac: `NSOpenPanel` / `NSSavePanel`（.mm）。`allowedContentTypes` でフィルタ指定。`filterIndex` の概念が無いので最初の 1 件のみ有効、ラジオで切替 UI を自前で作るか割り切る |

## 6. シェル操作（URL / フォルダを開く）

| 参照箇所 | Win32 API | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `ScoreEditor.cpp:549` プリセットフォルダを開く | `ShellExecuteW(0,0,path,0,0,SW_SHOW)` | PAL化必要 | Mac: `[[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:path]]` または `system("open \"path\"")` |
| `ScoreEditor.cpp:700` Wiki URL を開く | `ShellExecuteW(0,0,L"https://...",0,0,SW_SHOW)` | PAL化必要 | 同上（`NSURL URLWithString:`） |

## 7. アプリケーション バージョン取得

| 参照箇所 | Win32 API | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `Application.cpp:63-98` `getVersion()` | `GetFileVersionInfoSizeW` / `GetFileVersionInfoW` / `VerQueryValue` で `MikuMikuWorld.exe` の VS_FIXEDFILEINFO を読む | PAL化必要 | Mac: `NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"]`。`.app` 化するなら Info.plist、そうでないならビルド時マクロ（`APP_VERSION_STRING`）で渡すのが簡便 |

## 8. ロケール取得

| 参照箇所 | Win32 API | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `Utilities.cpp:22-32` `getSystemLocale()` | `GetUserDefaultLocaleName` | PAL化必要 | Mac: `[[NSLocale preferredLanguages] firstObject]` で `"ja-JP"` 形式が返る。最初の `-` 前だけ取る現在のロジックをそのまま適用可能 |

## 9. Unicode パス対応（wide string）

この `mbToWideStr` / `wideStringToMb` 経由のロジックは「MSVC の `std::fstream` / `std::filesystem::path` が Windows で UTF-8 を受け付けない」ことへの対処。Mac では不要。

| 参照箇所 | 目的 | 分類 | Mac 代替 |
| --- | --- | --- | --- |
| `IO.h:51-52`, `IO.cpp:146-162` `wideStringToMb`/`mbToWideStr` | `WideCharToMultiByte` / `MultiByteToWideChar` | 不要化可能 (Mac) | Windows 専用にし、`#ifdef _WIN32` で実装を分ける。Mac 実装は `return str;`（std::string パススルー）。**または** 各呼び出し側で `#ifdef _WIN32` を使う |
| `File.cpp:55-65` 2 種類の `open()` / `openFilenameW` メンバ | wide パス受付 | PAL化必要 | Mac: std::string のみで十分。条件コンパイルで wide オーバーロードを除外 |
| `File.cpp:26-30`, `File.h`, `BinaryReader.cpp:9`, `BinaryWriter.cpp:9` | wide パスで fstream を open | 同上 | 同上 |
| `ApplicationConfiguration.cpp:23,257`, `ResourceManager.cpp:382`, `NotesPreset.cpp:53,110,124,139,195,211,258,289,306`, `ScoreEditor.cpp:705-747`, `Rendering/Shader.cpp:29` | `std::filesystem::exists(wPath)` / `directory_iterator(wPath)` / `fs::path{wFilename}` | 不要化可能 (Mac) | PAL 化した path 変換ユーティリティ経由にする（Win では wide、Mac では string） |
| `Audio/AudioManager.cpp:107`, `Audio/Sound.cpp:54,157,160` | `ma_sound_init_from_file_w(wstr)` | PAL化必要 | miniaudio は `ma_sound_init_from_file` (UTF-8) もサポートする。Mac では UTF-8 版、Windows では `_w` 版を使う `#ifdef` 分岐、もしくはユーティリティ関数で統一 |
| `Application.cpp:66` | `lstrcpyW` | 不要化可能 | getVersion 自体を PAL 化するので付随して除去 |

## 10. ビルド設定（MSBuild / `.vcxproj`）

| 参照箇所 | 内容 | 分類 | Mac 対応 |
| --- | --- | --- | --- |
| `MikuMikuWorld/MikuMikuWorld.vcxproj:33,39,46,52` | `PlatformToolset=v143` (MSVC 2022 固定) | PAL化必要 | CMake 化。Mac は AppleClang + libc++ |
| `vcxproj:41,47,54` | `CharacterSet=Unicode` (`UNICODE`/`_UNICODE` 定義) | 不要化可能 | Mac では意味を持たない。CMake で Windows のみ定義 |
| `vcxproj:91,105` | プリプロセッサ定義 `WIN32;_DEBUG;_CONSOLE` など | PAL化必要 | CMake で分岐 |
| `vcxproj:122,144` `AdditionalIncludeDirectories` | `../Depends/DirectXMath-master;glad;GLFW;stb_image;miniaudio;json;stb_vorbis;zlib/include` | PAL化必要 | CMake の `target_include_directories` に移植。Mac では GLFW / zlib を Homebrew or FetchContent で取得 |
| `vcxproj:128,156` `AdditionalDependencies` | `glfw3.lib;zlibstatic.lib;opengl32.lib;Version.lib;Dwmapi.lib` | PAL化必要 | Mac: `glfw`, `z`（Homebrew or システム）, `-framework OpenGL`, `-framework Cocoa`, `-framework IOKit`, `-framework CoreFoundation`, `-framework AppKit`。`Version.lib` / `Dwmapi.lib` は不要 |
| `vcxproj:131-133, 161-163` | ポストビルドで `xcopy res → $(OutDir)` | PAL化必要 | CMake の `add_custom_command(POST_BUILD)` + `cmake -E copy_directory` |
| `vcxproj:151,157` | `SubSystem=Windows` / `EntryPointSymbol=mainCRTStartup` (Release) | 不要化可能 | Mac では不要 |
| `vcxproj:318` `MikuMikuWorld.rc`, `vcxproj:315` `mmw_icon.ico` | Win32 リソース | 不要化可能 | Mac では `.icns` を Info.plist 経由で指定 |
| `Depends/GLFW/lib/glfw3.lib`, `Depends/zlib/lib/*.lib` | Windows 用プリビルド lib のみ | PAL化必要 | Mac: Homebrew (`brew install glfw`) または CMake `FetchContent` でソースビルド |

`MikuMikuWorld.vcxproj.filters` も並存維持が望ましい（メンテナが VS IDE を使い続ける前提）。

## 11. ヘッダーオンリー依存の Mac 互換性

| 依存 | Mac 対応 | 備考 |
| --- | --- | --- |
| `Depends/DirectXMath-master` | 可（Microsoft 公式が非 Windows 対応） | `<sal.h>` が無いので `<XMsal.h>` の代替または `Extensions/` の sal シムが必要な可能性。SSE2/NEON 前提で AppleClang でビルド可 |
| `Depends/glad` | 可 | 完全にクロスプラットフォーム |
| `Depends/GLFW` (ソースも lib も内包) | 可（ソースビルドまたは Homebrew） | 付属 lib は Windows のみ |
| `Depends/zlib` | 可 | Mac は Homebrew の `zlib` or システム付属 |
| `Depends/miniaudio/miniaudio.h` | 可（CoreAudio バックエンド内蔵） | `HWND` 参照は DirectSound バックエンド内の `#ifdef MA_WIN32` 領域のみ。Mac では自動的に CoreAudio 経路 |
| `Depends/stb_image`, `Depends/stb_vorbis` | 可 | 完全にクロスプラットフォーム |
| `Depends/json` (nlohmann::json) | 可 | 完全にクロスプラットフォーム |
| `MikuMikuWorld/ImGui/*` | 可（同梱は Dear ImGui 本体） | 既に `#ifdef _WIN32` で分岐済み |

OpenGL 3.3 Core Profile は macOS 10.9 以降で動作するが Apple は 10.14 で deprecation を通知済み（macOS 14 / Sequoia 時点でも動作はする）。将来 Metal / MoltenVK 移行の可能性はあるが、本 Phase のスコープ外。

## 12. .mm (Objective-C++) が必要な範囲

以下は C/C++ だけでは Cocoa API を叩けないため、`Platform/Cocoa/*.mm` で実装する。

- `MessageBox`（NSAlert）
- `FileDialog`（NSOpenPanel / NSSavePanel）
- `IsSystemDarkMode` / `SetWindowDarkMode`（NSAppearance）
- `OpenShellPath` / `OpenShellUrl`（NSWorkspace）- `system("open ...")` で済ますなら C++ のみでも可
- `GetSystemLocale`（NSLocale）- `setlocale` / `std::locale` を使うなら C++ のみでも可
- `GetAppVersion`（NSBundle）- CMake でマクロ定義するなら不要

## 13. PAL インターフェース案

調査結果から導出される最小限の抽象化は以下。実装 (Phase 3) で再検討する。

```cpp
namespace MikuMikuWorld::Platform {
    struct NativeWindow { void* handle; };                      // HWND on Win, NSWindow* on Mac
    NativeWindow getNativeWindow(GLFWwindow*);

    MessageBoxResult showMessageBox(const NativeWindow& parent, ...);   // IO.cpp:13 の置換
    FileDialogResult showOpenDialog(const NativeWindow& parent, ...);   // File.cpp:237 の置換
    FileDialogResult showSaveDialog(const NativeWindow& parent, ...);

    bool   isSystemDarkMode();                                           // UI.cpp:420
    void   setWindowDarkTitlebar(const NativeWindow&, bool);             // UI.cpp:442
    void   subscribeThemeChange(std::function<void()>);                  // main.cpp:105 の置換

    void   openPathInShell(const std::string& path);                     // ScoreEditor.cpp:549
    void   openUrlInBrowser(const std::string& url);                     // ScoreEditor.cpp:700

    std::string getSystemLocale();                                       // Utilities.cpp:22
    std::string getAppVersion();                                         // Application.cpp:63

    // wide パス橋渡し。Mac では no-op (std::string パススルー)
    // Win ではファイル I/O 呼び出し前に wide に変換する内部ユーティリティ
}
```

## 14. 規模感・リスクまとめ

- `#include <Windows.h>` を直接使う翻訳単位: **7** 個（`main.cpp`, `Application.h/.cpp`, `ScoreEditor.cpp`, `Utilities.cpp`, `IO.cpp`, `File.cpp`, `UI.cpp`）。どれもアプリ入口～シェル連携で、PAL 化の恩恵が大きい
- Mac で追加が必要な `.mm` ファイル: 最大で上記 PAL の関数ごとに 1 ファイル、合計 3〜6 本程度
- クロスプラットフォーム化の最大リスクは
  1. OpenGL 3.3 の将来 deprecated（短期では実害なし）
  2. Retina / DPI スケーリング差分（ImGui のフォント・UI スケール調整が別途必要）
  3. `UNICODE` マクロに依存した `TEXT("...")` / `VerQueryValue` マクロ展開（`VerQueryValueW` 直接指定に直せば解消）
- CLAUDE.md の方針「アプリ全体が Windows 依存である前提で扱うこと」と矛盾する → Phase 0 の合意事項として書き換え必須

## 次のアクション

Phase 2（CMake 化）に進む前に、メンテナ確認したい項目:

1. `MikuMikuWorld.vcxproj` は残すか、CMake 一本化か
2. Mac で諦めてよい機能（MVP スコープ）
   - タイトルバーのダークモード追従
   - リサイズドラッグ中の連続描画（`WM_ENTERSIZEMOVE` 相当）
   - ネイティブファイルダイアログの `filterIndex` 概念
3. Mac 配布形態（`.app` バンドル化するか、生の実行ファイルで良いか）
4. CLAUDE.md の「Windows 依存前提」記述の書き換えに合意
