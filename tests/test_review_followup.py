"""Regression tests for dictionary restrictions and failed storage/transfer.

Run with the standard library only: python3 -m unittest discover -s tests -v
"""
import base64
import gzip
import hashlib
import importlib.util
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_dict import normalize_key, parse_line
from make_quiz_data import ejdict_words


class DictionaryConversion(unittest.TestCase):
    def test_compact_spelling_aliases_and_numbers(self):
        for head in ("organize,organise", "organize, organise"):
            self.assertEqual(parse_line(head + "\tmeaning")[1], ["organize", "organise"])
        self.assertEqual(parse_line("1,000\tmeaning")[1], ["1,000"])
        with tempfile.TemporaryDirectory() as temp:
            Path(temp, "test.txt").write_text("Organize,organise\tmeaning\n1,000\tmeaning\n")
            self.assertEqual(ejdict_words(temp), {"organize", "organise", "1,000"})
        self.assertEqual(normalize_key(" ＣＯＬＯＵＲ "), "colour")

    def test_jmdict_reading_writing_and_sense_restrictions(self):
        xml = '''<JMdict><entry><ent_seq>1</ent_seq><k_ele><keb>上手</keb></k_ele>
        <r_ele><reb>じょうず</reb></r_ele><r_ele><reb>うわて</reb></r_ele>
        <sense><stagr>じょうず</stagr><pos>adj-na</pos><gloss>skillful</gloss></sense>
        <sense><stagr>うわて</stagr><pos>n</pos><gloss>upper hand</gloss></sense></entry>
        <entry><ent_seq>2</ent_seq><k_ele><keb>甲</keb></k_ele><k_ele><keb>乙</keb></k_ele>
        <r_ele><reb>こう</reb><re_restr>甲</re_restr></r_ele>
        <r_ele><reb>おつ</reb><re_restr>乙</re_restr></r_ele>
        <r_ele><reb>かな</reb><re_nokanji/></r_ele>
        <sense><stagk>甲</stagk><pos>n</pos><gloss>first form</gloss></sense>
        <sense><stagk>乙</stagk><gloss>second form</gloss></sense>
        <sense><stagr>かな</stagr><gloss>kana only</gloss></sense></entry></JMdict>'''
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp, "JMdict_e.gz"); source.write_bytes(gzip.compress(xml.encode()))
            subprocess.run([sys.executable, str(ROOT / "tools/convert_jmdict.py"),
                            str(source), "-o", temp], check=True, capture_output=True)
            rows = Path(temp, "jmdict_waei.tsv").read_text().splitlines()
            jouzu = next(r for r in rows if r.startswith("jouzu"))
            uwate = next(r for r in rows if r.startswith("uwate"))
            self.assertIn("skillful", jouzu); self.assertNotIn("upper hand", jouzu)
            self.assertIn("upper hand", uwate); self.assertNotIn("skillful", uwate)
            kou = next(r for r in rows if r.startswith("kou\t"))
            self.assertIn("甲【こう】", kou); self.assertNotIn("乙", kou)
            self.assertNotIn("second form", kou)
            kana = next(r for r in rows if r.startswith("kana\t"))
            self.assertIn("かな|", kana); self.assertNotIn("甲", kana)
            reverse = dict(r.split("\t", 1) for r in Path(temp, "jmdict_eiwa.tsv").read_text().splitlines())
            self.assertIn("じょうず", reverse["skillful"]); self.assertNotIn("うわて", reverse["skillful"])
            self.assertIn("乙【おつ】 (n)", reverse["second form"])
            readings = Path(temp, "jmdict_readings.tsv").read_text()
            self.assertIn("上手\tじょうず\n", readings); self.assertIn("上手\tうわて\n", readings)
            self.assertNotIn("甲\tかな", readings); self.assertNotIn("乙\tこう", readings)

    def test_wordnet_keeps_all_valid_readings(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp)
            (p / "ok.gz").write_bytes(gzip.compress("00000001-n\t上手\n".encode()))
            (p / "def.gz").write_bytes(gzip.compress("00000001-n\tja\tdef\t説明\n".encode()))
            (p / "readings.tsv").write_text("上手\tじょうず\n上手\tうわて\n")
            # This word has authoritative readings, so automatic guessing is unused.
            fake = types.SimpleNamespace(kakasi=lambda: types.SimpleNamespace())
            with patch.dict(sys.modules, {"pykakasi": fake}), patch.object(sys, "argv", [
                "convert_wnjpn.py", str(p / "ok.gz"), str(p / "def.gz"),
                "--readings", str(p / "readings.tsv"), "-o", str(p / "out.tsv")]):
                runpy.run_path(str(ROOT / "tools/convert_wnjpn.py"), run_name="__main__")
            result = (p / "out.tsv").read_text()
            self.assertIn("jouzu", result); self.assertIn("uwate", result)
            self.assertIn("上手【じょうず・うわて】", result)


class Storage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(); cls.dir = Path(cls.temp.name)
        cls.binary = cls.dir / "wordbook-test"
        subprocess.run(["c++", "-std=c++17", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-include", "cstdlib",
                        '-DWORDBOOK_SD_PATH=std::getenv("PD_WB_PATH")',
                        "-I" + str(ROOT / "tests/host"), "-I" + str(ROOT / "firmware/main"),
                        str(ROOT / "firmware/main/wordbook.cpp"),
                        str(ROOT / "firmware/main/file_replace.cpp"),
                        str(ROOT / "firmware/main/settings.cpp"),
                        str(ROOT / "tests/host/wordbook_test.cpp"), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def test_storage_failures_migration_limits_and_recovery(self):
        for mode in ("empty", "legacy", "failure", "short-write", "nvs", "replace", "oversize", "init"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temp:
                path = str(Path(temp) / "wordbook.txt")
                subprocess.run([str(self.binary), path, mode], check=True, env={
                    **os.environ, "PD_WB_PATH": path, "ASAN_OPTIONS": "detect_leaks=0"})


class Transfer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with patch.dict(sys.modules, {"serial": types.SimpleNamespace()}):
            spec = importlib.util.spec_from_file_location("sdput_test", ROOT / "tools/sdput.py")
            cls.tool = importlib.util.module_from_spec(spec); spec.loader.exec_module(cls.tool)

    class Port:
        def __init__(self, bad=False, old=False):
            self.queue = []; self.writes = []; self.total = 0; self.bad = bad; self.old = old
        def reset_input_buffer(self): self.queue.clear()
        def flush(self): pass
        def readline(self): return self.queue.pop(0) if self.queue else b""
        def write(self, data):
            self.writes.append(data)
            if data.startswith(b"\x01PROTO"):
                self.queue.append(b"ERR unknown command\n" if self.old else b"PDICT-PUT 2 SHA256\n")
            elif data.startswith(b"\x01PUT"):
                _, size, self.hash = data.decode().strip().rsplit(" ", 2)
                self.size = int(size); self.queue.append(b"READY\n")
                if self.size == 0: self.queue.append(f"DONE 0 {self.hash}\n".encode())
            else:
                self.total += len(base64.b64decode(data))
                self.queue.append(f"OK {self.total}\n".encode())
                if self.total == self.size:
                    self.queue.append(f"DONE {self.total} {'0'*64 if self.bad else self.hash}\n".encode())

    def test_verified_transfer_empty_and_mismatched_hash(self):
        with tempfile.TemporaryDirectory() as temp:
            p = Path(temp, "test.pdc"); p.write_bytes(b"abc" * 4096)
            port = self.Port(); self.tool.put(port, str(p), "/sdcard/dict/test.pdc")
            self.assertIn(hashlib.sha256(p.read_bytes()).hexdigest().encode(), port.writes[1])
            with self.assertRaisesRegex(RuntimeError, "SHA-256"):
                self.tool.put(self.Port(bad=True), str(p), "/sdcard/dict/test.pdc")
            p.write_bytes(b""); self.tool.put(self.Port(), str(p), "/sdcard/dict/test.pdc")

    def test_old_firmware_rejected_before_put(self):
        port = self.Port(old=True)
        with self.assertRaises(RuntimeError): self.tool.put(port, "unused", "/sdcard/dict/test.pdc")
        self.assertFalse(any(b"PUT " in w for w in port.writes))

    def test_log_noise_does_not_reset_timeout(self):
        port = types.SimpleNamespace(readline=lambda: b"log noise\n")
        clock = iter(i / 10 for i in range(100))
        with patch.object(self.tool.time, "monotonic", side_effect=lambda: next(clock)):
            with self.assertRaisesRegex(RuntimeError, "timeout"):
                self.tool.expect(port, "DONE", timeout=0.5)

    def test_firmware_put_parser_rejects_unsafe_paths_and_sizes(self):
        source = (ROOT / "firmware/main/debug_console.cpp").read_text()
        body = source[source.index("bool parsePut("):source.index("// [book:15-cmd-put]")]
        harness = '#include <string>\n#include <cstdlib>\n#include <cerrno>\n#include <cassert>\n'
        checks = r'''
int main() {
    std::string path, hash, sha(64, 'a'); unsigned long size;
    auto valid = [&](const std::string& p, const std::string& n) {
        return parsePut((p + " " + n + " " + sha).c_str(), path, size, hash);
    };
    assert(valid("/sdcard/dict/日本語 file.pdc", "3") && size == 3 && hash == sha);
    assert(valid("/sdcard/dict/empty.pdc", "0"));
    assert(!valid("/sdcard/dict/../wordbook.txt", "3"));
    assert(!valid("/sdcard/dict/file.pdict-tmp", "3"));
    assert(!valid("/sdcard/dict/tab\tname.pdc", "3"));
    assert(!valid("/sdcard//file.pdc", "3"));
    assert(!valid("/other/file.pdc", "3"));
    assert(!valid("/sdcard/dict/file.pdc", "-1"));
    assert(!valid("/sdcard/dict/file.pdc", "268435457"));
    assert(!valid("/sdcard/dict/file.pdc", "999999999999999999999999"));
    assert(!parsePut("/sdcard/dict/file.pdc 3", path, size, hash));
}
'''
        with tempfile.TemporaryDirectory() as temp:
            cpp, binary = Path(temp, 'put.cpp'), Path(temp, 'put')
            cpp.write_text(harness + body + checks)
            subprocess.run(['c++', '-std=c++17', str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


class QuizPool(unittest.TestCase):
    def test_only_resolvable_english_keys_enable_wordbook_quiz(self):
        source = (ROOT / 'firmware/main/ui.cpp').read_text()
        body = source[source.index('std::vector<std::string> g_wordbookPool;'):source.index('bool buildPool()')]
        harness = r'''
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
struct Entry { std::string word, quizKey; };
namespace wordbook { std::vector<Entry> data; const auto& entries() { return data; } }
int* quizDict() { return nullptr; }
bool lookupExact(int*, const std::string& key, std::string&, std::string& def) {
    def = "definition"; return key == "organise" || key == "apple" || key == "beta" || key == "color";
}
'''
        checks = r'''
int main() {
    wordbook::data = {{"心", ""}, {"東京", ""}, {"日本語", ""}, {"国語", ""}};
    refreshWordbookPool(); assert(g_wordbookPool.empty());
    wordbook::data = {{"organize,organise", "organise"}, {"organise", "organise"}, {"missing", "missing"}};
    refreshWordbookPool(); assert(g_wordbookPool.size() == 1 && g_wordbookPool[0] == "organise");
    wordbook::data.insert(wordbook::data.end(), {{"apple", "apple"}, {"beta", "beta"}, {"color,colour", "color"}});
    refreshWordbookPool(); assert(g_wordbookPool.size() == 4);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            cpp, binary = Path(temp, 'pool.cpp'), Path(temp, 'pool')
            cpp.write_text(harness + body + checks)
            subprocess.run(['c++', '-std=c++17', str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
