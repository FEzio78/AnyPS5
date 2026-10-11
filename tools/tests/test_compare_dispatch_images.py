import contextlib
import csv
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare_dispatch_images import compare


class DispatchConsistencyTests(unittest.TestCase):
    def manifest(self, directory, sequence, stage, rows):
        fields = "kind index address width height guest_format vk_format tile_mode mip bytes file".split()
        with (directory / f"{sequence:06d}_cs_123_0001_{stage}.tsv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fields, delimiter="\t")
            writer.writeheader()
            for index, (kind, address, data) in enumerate(rows):
                name = f"{sequence:06d}_{stage}_{kind}_{index}.raw"
                (directory / name).write_bytes(data)
                writer.writerow(dict(zip(fields, [kind, index, address, 2, 1, 5, 13, 2, 0, len(data), "C:\\old\\capture\\" + name])))

    def result(self, reference, addresses):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            self.manifest(directory, 1, "before", [])
            self.manifest(directory, 1, "after", [("output", "123000", b"\x01\x02")])
            self.manifest(directory, 2, "before", [("reference", "123000", reference)])
            self.manifest(directory, 2, "after", [("output", "123000", reference)])
            with contextlib.redirect_stdout(io.StringIO()):
                return compare(directory, addresses)

    def test_reference_matches_previous_output(self):
        self.assertEqual(self.result(b"\x01\x02", {0x123000}), 0)

    def test_current_output_cannot_hide_stale_reference(self):
        self.assertEqual(self.result(b"\x01\x00", {0x123000}), 1)

    def test_missing_required_address_fails(self):
        self.assertEqual(self.result(b"\x01\x02", {0x456000}), 2)


if __name__ == "__main__":
    unittest.main()
