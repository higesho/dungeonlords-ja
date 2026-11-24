# ダンジョンロード 日本語化 dinput8.dll のソース

Steam 版 Dungeon Lords Steam Edition の文字表示を日本語にする `dinput8.dll` と、その組み立てに使う道具です。

- `src/` … dinput8.dll 本体（C）。Visual Studio 2022 の x86 ツールで `src\build.bat` を実行すると `build\dinput8.dll` ができます。`src\build.bat test` は試験用の版（`build\test\dinput8.dll`、配布しません）を作ります。
- `tools/` … Python の道具。`extract_all.py`・`extract_exe.py` で原作の文章を集め、`sh tools/rebuild.sh`（merge → make_data → make_font → build.bat）でまとめて組み立てます。`make_zip.py` は配布用の zip を作ります。`win.py`・`newgame.sh`・`loadgame.sh` は試験用の起動・操作の道具で、試験用の版を使います。

`dlfmt.py`・`dlx.py` の中のゲームの場所は、各自の環境に合わせて書き換えてください。

必要なもの: Python 3（Pillow、fontTools、pefile、capstone）、Noto Serif JP、訳の表（このリポジトリには入っていません）。
原作のファイル（DLSteamEdition.exe、D6STRING.DAT、QUESTTEXT.PAK など）は各自で用意してください。
