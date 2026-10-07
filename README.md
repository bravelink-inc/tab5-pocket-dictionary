# Tab5 ポケット辞書

書籍『M5Stack Tab5 で作る、自分だけの電子辞書』（佐藤 豊 著、株式会社ブレイブリンク）のソースコードです。M5Stack Tab5 と Tab5 Keyboard で動く電子辞書のファームウェアと、辞書データを作る道具が入っています。

## 書籍との対応

書籍の内容は、タグ `v1.0` の時点のものです。書籍の手順どおりに進めるときは、タグを指定して取得してください。

```bash
git clone --branch v1.0 https://github.com/bravelink-inc/tab5-pocket-dictionary.git
```

`main` ブランチは、正誤の修正などで書籍より先に変わることがあります。

## 入っているもの

| 場所 | 内容 |
| --- | --- |
| `firmware/` | 電子辞書のファームウェア（ESP-IDF v5.5、M5Unified、M5GFX） |
| `examples/ch04_hello_tab5/` | 書籍の最初に書き込む、画面に文字を出すだけのサンプル |
| `tools/` | 辞書データの変換、SD カードへの転送、画面の撮影などの道具（Python） |
| `third_party/` | 内蔵の英和辞書、テスト用の単語リストと漢字の学年表、フォント関係のライセンス |
| `case/` | 3D プリンターで作る台（クレードル）の生成プログラムと STL |
| `web/`、`scripts/make_release.sh` | ブラウザから書き込むページと、配布用の一式を作るスクリプト |

## ビルドと書き込み（概要）

詳しい手順は書籍にあります。ESP-IDF v5.5 の環境で次を実行すると、ファームウェアと内蔵の辞書が書き込まれます。

```bash
cd firmware
idf.py set-target esp32p4
idf.py build
idf.py -p <ポート> flash
```

`<ポート>` は macOS なら `/dev/cu.usbmodem1101` のような名前、Windows なら `COM3` のような名前です。

## ライセンス

- このリポジトリのプログラムは MIT ライセンスです（`LICENSE`）
- 辞書データ、単語リスト、フォントなどの第三者のデータは、それぞれのライセンスに従います（`THIRD_PARTY_NOTICES.md`）
- M5Stack 公式の Tab5 と Tab5 Keyboard の 3D データは含みません。ケースを改造するときに参照したい場合は、M5Stack の公式ドキュメント（<https://docs.m5stack.com/>）から入手してください

## 正誤表

書籍の誤りが見つかったときは、ここに載せます。

（現在、正誤はありません）

## お問い合わせ

書籍とこのソースコードについてのお問い合わせは、発行元のお問い合わせフォーム（<https://brave-link.co.jp/#contact>）からお願いします。書籍名と章、使っているタグ（`v1.0` など）を添えてください。

## 改ざんへの備え

- `main` ブランチとタグは保護されていて、著者が確認したコミットだけが入ります。コミットには GitHub の署名が付き、画面に「Verified」と表示されます
- 書籍の奥付に、タグ `v1.0` のコミット ID（中身から計算される指紋）を載せています。取得したフォルダで `git rev-parse HEAD` を実行し、表示される値と一致するか確かめられます
- 似た名前のリポジトリやフォークではなく、必ず <https://github.com/bravelink-inc/tab5-pocket-dictionary> から取得してください
