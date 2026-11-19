"""Unit tests that need no game files: run with python -I -m unittest."""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import addrlib  # noqa: E402
import classify  # noqa: E402
import commonlib_scan  # noqa: E402
import rtti  # noqa: E402


def _db(pairs):
    """Encode (id, offset) pairs in format 2 using only explicit fields."""
    body = struct.pack("<i", 2) + struct.pack("<4i", 1, 6, 1170, 0)
    body += struct.pack("<i", 0) + struct.pack("<ii", 8, len(pairs))
    for ident, off in pairs:
        body += bytes([0x00]) + struct.pack("<QQ", ident, off)
    return body


class AddrLib(unittest.TestCase):
    def test_explicit_entries(self):
        with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as f:
            f.write(_db([(5, 0x1000), (9, 0x2468)]))
        try:
            ver, ps, m = addrlib.load(f.name)
        finally:
            os.unlink(f.name)
        self.assertEqual(ver, (1, 6, 1170, 0))
        self.assertEqual(ps, 8)
        self.assertEqual(m, {5: 0x1000, 9: 0x2468})

    def test_delta_entries(self):
        # id = prev + 1 (lo 1); offset = prev/8 + 1, scaled by 8 (hi 8|1)
        body = bytearray(_db([(5, 0x1000)]))
        struct.pack_into("<i", body, 28, 2)
        body += bytes([0x91])
        with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as f:
            f.write(bytes(body))
        try:
            _v, _p, m = addrlib.load(f.name)
        finally:
            os.unlink(f.name)
        self.assertEqual(m[6], 0x1008)

    def test_rejects_other_formats(self):
        with tempfile.NamedTemporaryFile(delete=False, suffix=".bin") as f:
            f.write(struct.pack("<i", 1) + b"\0" * 32)
        try:
            with self.assertRaises(ValueError):
                addrlib.load(f.name)
        finally:
            os.unlink(f.name)


class Names(unittest.TestCase):
    def test_demangle(self):
        self.assertEqual(rtti.demangle_type(".?AVInner@Outer@RE@@"), "RE::Outer::Inner")
        self.assertEqual(rtti.demangle_type(".?AV?$SettingT@VINISettingCollection@@@@"),
                         "SettingT<INISettingCollection>")
        raw = ".?AV?$NativeFunction5@VActor@@_NPEAV1@@BSScript@@"
        self.assertEqual(rtti.demangle_type(raw), raw)

    def test_virtual_regex(self):
        m = commonlib_scan.VIRT_RE.match("\t\tvirtual ~NiRefObject();  // 00")
        self.assertEqual((m.group(2), m.group(3), m.group(4)), ("~", "NiRefObject", "00"))
        m = commonlib_scan.VIRT_RE.match("\t\tvoid OnVisible(NiCullingProcess& a) override;  // 34")
        self.assertEqual((m.group(3), m.group(4)), ("OnVisible", "34"))
        self.assertIsNone(commonlib_scan.VIRT_RE.match("\t\tfloat fov;  // 08"))

    def test_classify(self):
        self.assertEqual(classify.subsystem("BSShadowDirectionalLight"), "lighting_shadows")
        self.assertEqual(classify.subsystem("BSScript::Internal::VirtualMachine"), "papyrus")
        self.assertEqual(classify.subsystem("BSWaterShader"), "water")
        self.assertIsNone(classify.subsystem("CombatPathingGoalPolicyExitWater"))
        self.assertEqual(classify.setting_subsystem("fShadowDistance", "Display"),
                         "lighting_shadows")


if __name__ == "__main__":
    unittest.main()
