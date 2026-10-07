# 章ごとのサンプル

書籍の各章で作る段階のプロジェクトです。それぞれ独立した ESP-IDF プロジェクトとしてビルドできます。

| ディレクトリ | 章 | 内容 |
| --- | --- | --- |
| `ch04_hello_tab5` | 第 4 章 | M5Unified で画面に日本語を出し、タッチ位置に丸を描く最小構成 |

macOS:
```bash
cd examples/ch04_hello_tab5
. ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

Windows（ESP-IDF 5.5 PowerShell）:
```powershell
cd examples\ch04_hello_tab5
idf.py set-target esp32p4
idf.py build
idf.py -p COM3 flash monitor
```
