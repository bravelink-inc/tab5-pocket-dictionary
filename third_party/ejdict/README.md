# EJDict-hand (英和辞書データ)

- Source: https://github.com/kujirahand/EJDict (`src/*.txt`, fetched 2026-09-08)
- License: CC0 1.0 Universal (Public Domain) — see `LICENSE`
- Format: `headword<TAB>definition` (UTF-8). Lines with several spellings use `a, b, c<TAB>definition`.

`tools/build_dict.py` converts these files into `data/ejdict.pdc`, which is flashed to the `dict` partition.
