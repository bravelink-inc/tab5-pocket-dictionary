"""Host regression checks. Run: python3 -m unittest discover -s tests -v

Uses the real PDC reader with memory/log shims, and the actual nextQuestion body
with deterministic draws. No device, extra Python package or dictionary download.
"""
import gzip
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RequiredFixes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.temp.name)
        cls.reader = cls.dir / "reader"
        subprocess.run(["c++", "-std=c++17", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-g", "-I" + str(ROOT / "tests/host"),
                        "-I" + str(ROOT / "firmware/main"),
                        str(ROOT / "firmware/main/dictionary.cpp"),
                        str(ROOT / "tests/host/dictionary_reader_test.cpp"),
                        "-o", str(cls.reader)], check=True)
        tsv = cls.dir / "words.tsv"
        tsv.write_text("alpha\tfirst definition\nbeta, beta alias\tsecond definition\n", encoding="utf-8")
        image = cls.dir / "valid.pdc"
        subprocess.run([sys.executable, str(ROOT / "tools/build_dict.py"),
                        str(tsv), "-o", str(image), "--title", "Test", "--tag", "T"], check=True)
        cls.good = image.read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def check_image(self, data, accepted=False, record=True):
        path = self.dir / "case.pdc"
        path.write_bytes(data)
        subprocess.run([str(self.reader), str(path), "valid" if accepted else "invalid",
                        "record" if record else "bad-record"], check=True,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})

    def test_valid_memory_resident_and_lazy(self):
        self.check_image(self.good, True)

    def test_truncation_header_and_indices(self):
        for length in (0, 63, len(self.good) - 1):
            with self.subTest(length=length): self.check_image(self.good[:length])
        for field in (4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44):
            bad = bytearray(self.good)
            struct.pack_into("<I", bad, field, 0xffffffff)
            with self.subTest(field=field): self.check_image(bad)

    def test_corrupt_key_and_definition_offsets(self):
        keys_off, keys_size, kidx, dref = struct.unpack_from("<4I", self.good, 16)
        for location, value in [(kidx, keys_size), (kidx, 1), (dref, 0xffffffff), (dref, 1)]:
            bad = bytearray(self.good)
            struct.pack_into("<I", bad, location, value)
            with self.subTest(location=location, value=value): self.check_image(bad)
        bad = bytearray(self.good)
        bad[keys_off:keys_off + keys_size] = b"x" * keys_size
        self.check_image(bad)
        bad = bytearray(self.good)
        bad[keys_off] = ord("z")   # keys are no longer sorted
        self.check_image(bad)

    def test_corrupt_record_is_rejected_in_all_read_paths(self):
        defs = struct.unpack_from("<I", self.good, 32)[0]
        for value in (0, 1, 0xffffffff, 512 * 1024):
            bad = bytearray(self.good)
            struct.pack_into("<I", bad, defs, value)
            with self.subTest(length=value): self.check_image(bad, True, False)
        bad = bytearray(self.good)
        length = struct.unpack_from("<I", bad, defs)[0]
        bad[defs + 4:defs + 4 + length] = b"x" * length
        self.check_image(bad, True, False)

    def test_quiz_unique_answers_and_exhaustion(self):
        ui = (ROOT / "firmware/main/ui.cpp").read_text()
        body = ui.split("// [book:13-next-question]\n", 1)[1].split("// [/book:13-next-question]", 1)[0]
        harness = r'''
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
enum class QuizKind { Word, Kanji };
struct Question { std::string word, def, options[4]; int correct = 0; };
struct State { QuizKind kind = QuizKind::Word; bool jaToEn = false; Question q;
    std::vector<std::string> asked; int answered = -1; } Z;
std::vector<std::string> draws;
size_t at = 0;
bool nextKanjiQuestion() { return false; }
unsigned rnd(unsigned) { return 0; }
std::string firstSense(const std::string& s) { return s; }
bool pickWord(std::string& w, std::string& d, const std::string&, bool = false) {
    if (at == draws.size()) return false;
    w = "word" + std::to_string(at); d = draws[at++]; return true;
}
'''
        checks = r'''
int main() {
    for (bool reverse : {false, true}) {
        Z = {}; Z.jaToEn = reverse;
        draws = {"right", "one", "two", "three"}; at = 0;
        assert(nextQuestion() && at == 4 && Z.asked.size() == 1);
        for (int i = 0; i < 4; ++i) for (int j = 0; j < i; ++j)
            assert(Z.q.options[i] != Z.q.options[j]);
    }
    Z = {}; draws.assign(11, "right"); at = 0;
    assert(!nextQuestion() && Z.asked.empty());
    Z = {}; draws.assign(10, "right");
    draws.insert(draws.end(), {"one", "two", "three"}); at = 0;
    assert(nextQuestion() && at == 13);
    assert(Z.q.options[0] == "right" && Z.q.options[1] == "one");
}
'''
        src, binary = self.dir / "quiz.cpp", self.dir / "quiz"
        src.write_text(harness + body + checks)
        subprocess.run(["c++", "-std=c++17", str(src), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_kanji_uses_former_jlpt(self):
        # This fixture does not need the external romaji dependency.
        (self.dir / "romaji.py").write_text("def romaji_keys(s): return ['shin']\ndef to_hiragana(s): return s\n")
        xml = self.dir / "kanji.xml.gz"
        xml.write_bytes(gzip.compress(b'<kanjidic2><character><literal>test</literal><misc>'
            b'<jlpt>3</jlpt></misc><reading_meaning><rmgroup><meaning>heart</meaning>'
            b'</rmgroup></reading_meaning></character></kanjidic2>'))
        out = self.dir / "kanji.tsv"
        command = "import runpy,sys; sys.argv=sys.argv[1:]; sys.path.insert(0,sys.argv.pop(1)); runpy.run_path(sys.argv[0],run_name='__main__')"
        subprocess.run([sys.executable, "-c", command, str(ROOT / "tools/convert_kanjidic.py"),
                        str(self.dir), str(xml), "-o", str(out)], check=True)
        text = out.read_text()
        self.assertIn("旧JLPT 3級（2009年以前）", text)
        self.assertNotIn("JLPT N3", text)
