"""
Comprehensive Unit Tests for Reverse-Engineered Chord Recognition Engine
========================================================================
"""

import unittest
from chord_recognizer import ChordRecognizer


class TestChordRecognizer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rec = ChordRecognizer()

    def test_single_notes(self):
        res = self.rec.identify([60])  # C4
        self.assertEqual(res.display_name, "C")
        self.assertEqual(res.inversion, 0)
        self.assertFalse(res.is_slash_chord)

        res = self.rec.identify([66])  # F#4
        self.assertEqual(res.display_name, "F#")

    def test_power_chord(self):
        res = self.rec.identify([60, 67])  # C4, G4
        self.assertEqual(res.display_name, "C5")

    def test_major_triads_and_inversions(self):
        # C Major Root Position (C4, E4, G4)
        c_root = self.rec.identify([60, 64, 67])
        self.assertEqual(c_root.display_name, "C")
        self.assertEqual(c_root.root_name, "C")
        self.assertEqual(c_root.bass_name, "C")
        self.assertEqual(c_root.inversion, 0)
        self.assertFalse(c_root.is_slash_chord)

        # C Major 1st Inversion C/E (E3, G3, C4)
        c_inv1 = self.rec.identify([52, 55, 60])
        self.assertEqual(c_inv1.display_name, "C/E")
        self.assertEqual(c_inv1.root_name, "C")
        self.assertEqual(c_inv1.bass_name, "E")
        self.assertTrue(c_inv1.is_slash_chord)

        # C Major 2nd Inversion C/G (G3, C4, E4)
        c_inv2 = self.rec.identify([55, 60, 64])
        self.assertEqual(c_inv2.display_name, "C/G")
        self.assertEqual(c_inv2.root_name, "C")
        self.assertEqual(c_inv2.bass_name, "G")
        self.assertTrue(c_inv2.is_slash_chord)

    def test_minor_triads(self):
        # A Minor (A3, C4, E4)
        am = self.rec.identify([57, 60, 64])
        self.assertEqual(am.display_name, "Am")
        self.assertEqual(am.root_name, "A")

        # D Minor 1st Inversion Dm/F (F3, A3, D4)
        dm_f = self.rec.identify([53, 57, 62])
        self.assertEqual(dm_f.display_name, "Dm/F")
        self.assertEqual(dm_f.root_name, "D")
        self.assertEqual(dm_f.bass_name, "F")

    def test_seventh_chords(self):
        # G Dominant 7th (G3, B3, D4, F4)
        g7 = self.rec.identify([55, 59, 62, 65])
        self.assertEqual(g7.display_name, "G7")

        # G7 1st Inversion (B3, D4, F4, G4)
        g7_b = self.rec.identify([59, 62, 65, 67])
        self.assertEqual(g7_b.display_name, "G7/B")
        self.assertEqual(g7_b.root_name, "G")
        self.assertEqual(g7_b.bass_name, "B")

        # C Major 7th (C4, E4, G4, B4)
        cmaj7 = self.rec.identify([60, 64, 67, 71])
        self.assertEqual(cmaj7.display_name, "Cmaj7")

        # B Half-Diminished Bm7b5 (B3, D4, F4, A4)
        bm7b5 = self.rec.identify([59, 62, 65, 69])
        self.assertEqual(bm7b5.display_name, "Bm7b5")

        # C Diminished 7th (C4, Eb4, F#4, A4)
        cdim7 = self.rec.identify([60, 63, 66, 69])
        self.assertEqual(cdim7.display_name, "Cdim7")

    def test_suspended_and_added_tone_chords(self):
        # Dsus4 (D4, G4, A4)
        dsus4 = self.rec.identify([62, 67, 69])
        self.assertEqual(dsus4.display_name, "Dsus4")

        # Asus2 (A3, B3, E4)
        asus2 = self.rec.identify([57, 59, 64])
        self.assertEqual(asus2.display_name, "Asus2")

        # Cadd9 (C4, D4, E4, G4)
        cadd9 = self.rec.identify([60, 62, 64, 67])
        self.assertEqual(cadd9.display_name, "Cadd9")

    def test_altered_and_extended_chords(self):
        # E7#9 with 5th: E3, G#3, B3, D4, G4 (MIDI 52, 56, 59, 62, 67)
        e7s9 = self.rec.identify([52, 56, 59, 62, 67])
        self.assertEqual(e7s9.display_name, "E7#9")

        # E7#9 (Hendrix Voicing without 5th): E3, G#3, D4, G4 (MIDI 52, 56, 62, 67)
        e7s9_no5 = self.rec.identify([52, 56, 62, 67])
        self.assertEqual(e7s9_no5.display_name, "E7#9(no5)")

        # C9: C4, E4, G4, Bb4, D5 (MIDI 60, 64, 67, 70, 74)
        c9 = self.rec.identify([60, 64, 67, 70, 74])
        self.assertEqual(c9.display_name, "C9")

        # C13: C4, E4, G4, Bb4, D5, A5 (MIDI 60, 64, 67, 70, 74, 81)
        c13 = self.rec.identify([60, 64, 67, 70, 74, 81])
        self.assertEqual(c13.display_name, "C13")

    def test_polychord_decomposition(self):
        # D Major triad over C Major triad: C3, E3, G3 (C) + D4, F#4, A4 (D)
        polychord = self.rec.identify([48, 52, 55, 62, 66, 69])
        self.assertEqual(polychord.display_name, "D | C")

    def test_octave_doubling(self):
        # C Major with doubled root and fifth: C3, G3, C4, E4, G4, C5
        doubled = self.rec.identify([48, 55, 60, 64, 67, 72])
        self.assertEqual(doubled.display_name, "C")
        self.assertEqual(doubled.root_name, "C")
        self.assertEqual(doubled.bass_name, "C")
        self.assertFalse(doubled.is_slash_chord)

    def test_omit_five_jazz_chords(self):
        # Cmaj7 without 5th: C4, E4, B4
        cmaj7_no5 = self.rec.identify([60, 64, 71])
        self.assertEqual(cmaj7_no5.display_name, "Cmaj7(no5)")

        # G7 without 5th: G3, B3, F4
        g7_no5 = self.rec.identify([55, 59, 65])
        self.assertEqual(g7_no5.display_name, "G7(no5)")

    def test_helper_note_name_input(self):
        res = self.rec.identify_from_note_names(["E3", "G3", "C4"])
        self.assertEqual(res.display_name, "C/E")


if __name__ == "__main__":
    unittest.main()
