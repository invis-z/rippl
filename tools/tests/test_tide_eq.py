import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

from tide import eq_band, eq_files, eq_fit, eq_text

PRESETS = TOOLS_DIRECTORY / "tests" / "eq_presets"
SIMPLE = b"Preamp: -3 dB\nFilter 1: ON PK Fc 9000 Hz Gain 4 dB Q 1.4\n"
OTHER = b"Preamp: -2 dB\nFilter 1: ON HS Fc 7000 Hz Gain 3 dB Q 0.7\n"


def quick_fits(curve):
    return [eq_fit.Fit(rate=44100, target_rate=curve.target(), worst_hundredths=3,
                       stages=[eq_band.design(curve.bands[0], 44100)])]


class ParseTest(unittest.TestCase):
    def test_a_preset_reads_into_tenths_and_hundredths(self):
        curve = eq_text.parse(b"\xef\xbb\xbfPreamp: -1.45 dB\r\n"
                              b"Filter 1: ON PK Fc 105 Hz Gain 6.55 dB Q 0.707\n"
                              b"Filter 2: OFF PK Fc 30 Hz Gain 1 dB Q 1\n"
                              b"Filter 3: ON LP Fc 18000 Hz Q 0.7\n")
        self.assertEqual(-15, curve.preamp_tenths)
        self.assertEqual([eq_text.Band(105, 66, 71, eq_text.PEAK)], curve.bands)

    def test_a_sample_rate_comment_sets_the_target(self):
        curve = eq_text.parse(b"# Sample rate: 44100 Hz\nPreamp: 0 dB\n")
        self.assertEqual(44100, curve.target())
        self.assertEqual(48000, eq_text.parse(b"# Sample rate: 7000\nPreamp: 0 dB\n").target())

    def test_errors_are_refused_as_on_the_player(self):
        for text in (b"", b"Preamp: 13 dB\n", b"Filter 1: ON PK Fc 100 Hz Gain 13 dB Q 1\n",
                     b"Preamp: 0 dB\nPreamp: 0 dB\n", b"Bass: 3 dB\n", b"x" * 161 + b"\n"):
            with self.subTest(text=text[:20]), self.assertRaises(eq_text.ParseError):
                eq_text.parse(text)


class FileTest(unittest.TestCase):
    def test_a_fit_file_fills_a_slot_and_reads_back(self):
        curve = eq_text.parse(SIMPLE)
        data = eq_files.fit_file(eq_files.curve_key(curve), curve.target(), quick_fits(curve))
        self.assertEqual(eq_files.SLOT_BYTES, len(data))
        self.assertEqual((eq_files.curve_key(curve), 48000, 1), eq_files.read_header(data))
        damaged = bytearray(data)
        damaged[20] ^= 1
        self.assertIsNone(eq_files.read_header(bytes(damaged)))

    def test_slot_names_have_two_digits(self):
        self.assertEqual("E07.BIN", eq_files.slot_name(7))
        self.assertEqual(42, eq_files.slot_of("e42.bin"))
        self.assertIsNone(eq_files.slot_of("E7.BIN"))


class RefreshTest(unittest.TestCase):
    def card(self, folder, presets):
        card = Path(folder)
        (card / "EQ").mkdir()
        for name, text in presets.items():
            (card / "EQ" / name).write_bytes(text)
        return card, card / ".rippl"

    def test_presets_without_fits_are_fitted_and_gone_ones_deleted(self):
        with tempfile.TemporaryDirectory() as folder:
            card, rippl = self.card(folder, {"A.txt": SIMPLE, "B.txt": OTHER})
            report = eq_files.refresh(card, rippl, fit_curve=quick_fits)
            self.assertEqual((2, 2, 0, 0), (report.presets, report.fitted, report.kept,
                                            report.deleted))
            self.assertEqual({"E00.BIN", "E01.BIN"}, set(os.listdir(rippl / "equaliser")))
            (card / "EQ" / "A.txt").unlink()
            (rippl / "equaliser" / "E05.BIN").write_bytes(b"damaged")
            report = eq_files.refresh(card, rippl, fit_curve=quick_fits)
            self.assertEqual((1, 0, 1, 2), (report.presets, report.fitted, report.kept,
                                            report.deleted))
            self.assertEqual(1, len(os.listdir(rippl / "equaliser")))

    def test_the_sync_maps_each_fit_and_keeps_the_choice(self):
        with tempfile.TemporaryDirectory() as folder:
            card, rippl = self.card(folder, {"A.txt": SIMPLE, "B.txt": OTHER})
            rippl.mkdir()
            choice = bytes(range(75))
            (rippl / "equaliser.bin").write_bytes(choice + bytes(4096 - 75))
            eq_files.refresh(card, rippl, fit_curve=quick_fits)
            data = (rippl / "equaliser.bin").read_bytes()
            self.assertEqual(4096, len(data))
            self.assertEqual(choice, data[:75])
            keys = struct.unpack_from("<100I", data, 75)
            self.assertEqual(eq_files.fnv(data[75:475]), struct.unpack_from("<I", data, 475)[0])
            for slot in (0, 1):
                header = eq_files.read_header((rippl / "equaliser" / f"E0{slot}.BIN").read_bytes())
                self.assertEqual(header[0], keys[slot])
            self.assertTrue(all(key == 0 for key in keys[2:]))

    def test_a_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            card, rippl = self.card(folder, {"A.txt": SIMPLE})
            report = eq_files.refresh(card, rippl, dry_run=True, fit_curve=quick_fits)
            self.assertEqual(1, report.fitted)
            self.assertFalse((rippl / "equaliser").exists())
            self.assertFalse((rippl / "equaliser.bin").exists())


class FitTest(unittest.TestCase):
    def test_a_fit_beats_the_usual_filters_at_every_rate_it_keeps(self):
        curve = eq_text.parse((PRESETS / "Treble 20k.txt").read_bytes())
        found, usual = eq_fit.fit(curve, 44100)
        self.assertIsNotNone(found)
        self.assertLess(found.worst_hundredths, usual)
        self.assertTrue(all(eq_fit.usable(stage) for stage in found.stages))


if __name__ == "__main__":
    unittest.main()
