"""make_dict.py - runs inside Pyodide on the 「辞書を作る」 page (web/make-dict.html).

The reader downloads the official data files and gives them to the page; this script runs the
same tools/*.py as the book's commands, entirely inside the browser, and leaves the SD-card
files in /work/dict. Nothing is uploaded anywhere.
"""
import gzip, io, os, runpy, sys, zipfile

sys.path.insert(0, "/tools")
SRC, TSV, OUT = "/work/src", "/work/tsv", "/work/dict"
INPUTS = {"jmdict": "JMdict_e.gz", "kanjidic": "kanjidic2.xml.gz", "wnok": "wnjpn-ok.tab.gz",
          "wndef": "wnjpn-def.tab.gz", "ngsl": "NGSL_12_stats.csv"}
DICTS = [("JMdict 和英", "和英", "10_jmdict_waei", "jmdict_waei"),
         ("JMdict 英和（逆引き）", "英和J", "20_jmdict_eiwa", "jmdict_eiwa"),
         ("KANJIDIC2 漢字辞典", "漢字", "30_kanjidic2", "kanjidic2"),
         ("日本語 WordNet（語義・類語）", "類語", "40_wnjpn_kokugo", "wnjpn_kokugo")]


def put_input(kind, data):
    """Store one downloaded file under the name the tools expect."""
    for d in (SRC, TSV, OUT):
        os.makedirs(d, exist_ok=True)
    data = bytes(data)
    name = INPUTS[kind]
    if name.endswith(".gz") and data[:2] != b"\x1f\x8b":   # a browser may have unpacked it
        data = gzip.compress(data, compresslevel=1)
    with open(os.path.join(SRC, name), "wb") as f:
        f.write(data)


def run(script, *args):
    sys.argv = [script, *args]
    try:
        runpy.run_path(f"/tools/{script}", run_name="__main__")
    except SystemExit as e:
        if e.code not in (0, None):
            raise RuntimeError(f"{script} failed ({e.code})")


def src(kind):
    return os.path.join(SRC, INPUTS[kind])


# (message shown on the page, function) - one call per step so the page can show progress
STEPS = [
    ("JMdict を和英・英和に変換", lambda: run("convert_jmdict.py", src("jmdict"), "-o", TSV)),
    ("KANJIDIC2 を漢字辞典に変換", lambda: run("convert_kanjidic.py", src("kanjidic"), "-o", f"{TSV}/kanjidic2.tsv")),
    ("日本語 WordNet を語義・類語辞書に変換", lambda: run("convert_wnjpn.py", src("wnok"), src("wndef"),
                                              "--readings", f"{TSV}/jmdict_readings.tsv", "-o", f"{TSV}/wnjpn_kokugo.tsv")),
] + [
    (f"{title} を .pdc にする", (lambda t=title, g=tag, o=out, s=tsv: run(
        "build_dict.py", "--title", t, "--tag", g, "--headword-sep", "|", "-o", f"{OUT}/{o}.pdc", f"{TSV}/{s}.tsv")))
    for title, tag, out, tsv in DICTS
] + [
    ("テストのデータを作る", lambda: run("make_quiz_data.py", "--ngsl", src("ngsl"), "--kanjidic", src("kanjidic"), "-o", OUT)),
    ("出典とライセンスを書く", lambda: run("make_notices.py", "--jmdict", src("jmdict"), "--kanjidic", src("kanjidic"), "-o", OUT)),
]


def step_names():
    return [s[0] for s in STEPS]


def run_step(i):
    STEPS[i][1]()


def outputs():
    return sorted(os.listdir(OUT))


def make_zip():
    """A ZIP of the dict folder, made here in the browser (for browsers that cannot write to the card)."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
        for n in outputs():
            z.write(os.path.join(OUT, n), "dict/" + n)
    return buf.getvalue()
