import argparse
import csv
from pathlib import Path


def read_image(directory, row):
    path = directory / row["file"].replace("\\", "/").rsplit("/", 1)[-1]
    data = path.read_bytes()
    if len(data) != int(row["bytes"]):
        raise ValueError(f"{path.name}: payload size differs from the manifest")
    return path.name, data


def compare(directory, addresses):
    outputs = {}
    counts = {}
    failures = 0
    for manifest in sorted(directory.glob("*_before.tsv")):
        sequence = int(manifest.name.split("_", 1)[0])
        with manifest.open(encoding="utf-8", newline="") as stream:
            references = list(csv.DictReader(stream, delimiter="\t"))
        for row in references:
            if row["kind"] != "reference":
                continue
            address = int(row["address"], 16)
            if addresses and address not in addresses:
                continue
            key = (address, row["width"], row["height"], row["guest_format"], row["vk_format"], row["tile_mode"], row["mip"])
            if key not in outputs:
                continue
            prior_sequence, prior_name, expected = outputs[key]
            name, current = read_image(directory, row)
            counts[address] = counts.get(address, 0) + 1
            if expected == current:
                print(f"PASS 0x{address:x}: output seq {prior_sequence} -> reference seq {sequence}")
                continue
            failures += 1
            first = next((i for i, pair in enumerate(zip(expected, current)) if pair[0] != pair[1]), min(len(expected), len(current)))
            different = sum(a != b for a, b in zip(expected, current)) + abs(len(expected) - len(current))
            print(f"FAIL 0x{address:x}: {prior_name} -> {name}: {different} differing bytes; first payload offset 0x{first:x}")
        after = manifest.with_name(manifest.name.replace("_before.tsv", "_after.tsv"))
        with after.open(encoding="utf-8", newline="") as stream:
            for row in csv.DictReader(stream, delimiter="\t"):
                if row["kind"] != "output":
                    continue
                address = int(row["address"], 16)
                if addresses and address not in addresses:
                    continue
                key = (address, row["width"], row["height"], row["guest_format"], row["vk_format"], row["tile_mode"], row["mip"])
                name, payload = read_image(directory, row)
                outputs[key] = (sequence, name, payload)
    missing = addresses - counts.keys()
    total = sum(counts.values())
    print(f"Compared {total} reference/output pairs at {len(counts)} addresses; {failures} mismatched pairs")
    if missing or total == 0:
        print("FAIL: no consecutive-frame comparison for " + (", ".join(f"0x{address:x}" for address in sorted(missing)) or "any address"))
        return 2
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description="Require sampled references to equal the preceding dispatch output at the same guest address.")
    parser.add_argument("directory", type=Path)
    parser.add_argument("--address", action="append", default=[], type=lambda value: int(value, 16))
    args = parser.parse_args()
    return compare(args.directory, set(args.address))


if __name__ == "__main__":
    raise SystemExit(main())
