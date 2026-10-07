# ケース（3D プリント）

M5Stack Tab5 + Tab5 Keyboard を机に置くためのクレードル（台）です。書籍の第 16 章で説明しています。

| ファイル | 内容 |
| --- | --- |
| `tray.py` | 形状を生成するスクリプト（寸法は冒頭の `PARAMS`） |
| `stl/tray.3mf`、`stl/tray.stl` | バッテリー付きの Tab5 用（133.6 × 128.6 × 23.7 mm） |
| `stl/tray_nobattery.3mf`、`stl/tray_nobattery.stl` | バッテリーなしの Tab5 用（133.6 × 128.6 × 9.0 mm） |
| `ref/` | M5Stack 公式の Tab5 / Tab5 Keyboard の STL（参照用） |

作り直すとき（macOS / Windows 共通。Windows は `python3` を `python` に）:

```bash
cd case
python3 -m venv .venv
.venv/bin/pip install manifold3d numpy matplotlib      # Windows: .venv\Scripts\pip.exe install ...
.venv/bin/python tray.py                               # -> out/tray.3mf, out/tray.stl, out/tray_preview.png
```

Homebrew の OpenSCAD は 2026 年 9 月に配布停止になったため、Python だけで作れる形にしています。

## 寸法の出どころ

- バッテリーのくぼみの位置と向きは、`ref/Tab5.stl` のバッテリー受け（38.2 × 70.4 mm）から取っています。画面を上、キーボードを手前にしたとき、本体の中心から右へ 36.3 mm、奥へ 4.7 mm です。NP-F550 の長い辺（70 mm）は奥行き方向を向きます
- キーボードが本体から手前に出る長さ 43.0 mm は `ref/Tab5_Keyboard.stl` の値です。ショップの表記（59.4 mm）とは違うので、実物で測って `kb_d` を直してください
- 底は平らな 1 枚板で、四隅に 10 mm のゴム足を貼るくぼみがあります。バッテリーが片側に寄っていても机の上でがたつきません

Bambu Studio では、3MF は「ファイル → プロジェクトを開く」でも開けます。STL は「ファイル → インポート」（⌘I / Ctrl+I）か、ウィンドウへのドラッグで読み込みます。
