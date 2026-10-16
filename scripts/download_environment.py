#!/usr/bin/env python3
"""Download the selected CC0 environment sources; validate size and MD5 from Poly Haven."""

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / ".cache" / "environment"
MANIFEST = ROOT / "assets" / "environment-manifest.json"
USER_AGENT = "RoninAssetResearch/1.0"
MAX_BYTES = 300_000_000
SELECTION = {
    "monastery_stone_floor": ("2k", {"Diffuse": "jpg", "nor_dx": "jpg", "arm": "jpg"}),
    "japanese_cedar_planks": ("2k", {"Diffuse": "jpg", "nor_dx": "jpg", "arm": "jpg"}),
    "rock_moss_set_01": ("2k", {"fbx": "fbx", "Diffuse": "jpg", "nor_dx": "jpg", "arm": "jpg"}),
    "fir_sapling": ("1k", {"fbx": "fbx", "branches_diff": "jpg", "branches_nor_dx": "jpg", "branches_arm": "jpg", "twigs_diff": "jpg", "twigs_nor_dx": "jpg", "twigs_arm": "jpg", "twigs_alpha": "png"}),
    "overcast_soil_puresky": ("2k", {"hdri": "hdr"}),
}


def request(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": USER_AGENT}), timeout=120)


def digest(path):
    checksum = hashlib.md5()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def download(record):
    destination = ROOT / record["local_path"]
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and destination.stat().st_size == record["size_bytes"] and digest(destination) == record["md5"]:
        print(f"Verified {destination.name}", flush=True)
        return
    temporary = destination.with_suffix(destination.suffix + ".partial")
    with request(record["url"]) as response, temporary.open("wb") as output:
        while block := response.read(1024 * 1024):
            output.write(block)
            if output.tell() > record["size_bytes"]:
                raise RuntimeError(f"Unexpected download size: {destination.name}")
    if temporary.stat().st_size != record["size_bytes"] or digest(temporary) != record["md5"]:
        raise RuntimeError(f"Asset integrity failed: {destination.name}")
    temporary.replace(destination)
    print(f"Downloaded and verified {destination.name}", flush=True)


def main():
    entries = []
    for slug, (resolution, formats) in SELECTION.items():
        with request(f"https://api.polyhaven.com/files/{slug}") as response:
            metadata = json.load(response)
        files = []
        for role, extension in formats.items():
            selected = metadata[role][resolution][extension]
            basename = Path(urllib.parse.urlparse(selected["url"]).path).name
            files.append({"role": role, "format": extension, "resolution": resolution,
                          "url": selected["url"], "size_bytes": selected["size"], "md5": selected["md5"],
                          "local_path": str((CACHE / slug / basename).relative_to(ROOT))})
        entries.append({"id": slug, "source": f"https://polyhaven.com/a/{slug}",
                        "metadata_source": f"https://api.polyhaven.com/files/{slug}", "files": files})
    records = [record for entry in entries for record in entry["files"]]
    total = sum(record["size_bytes"] for record in records)
    if total > MAX_BYTES:
        raise RuntimeError(f"Selected source downloads exceed the {MAX_BYTES}-byte budget: {total}")
    print(f"Selected {len(records)} files, {total:,} bytes", flush=True)
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(download, records))
    result = {"provider": "Poly Haven", "license": "CC0-1.0", "license_url": "https://polyhaven.com/license",
              "retrieved_at_utc": datetime.now(timezone.utc).isoformat(), "download_bytes": total,
              "verification": "Each source file size and MD5 match the public provider metadata.",
              "assets": entries}
    if MANIFEST.is_file():
        previous = json.loads(MANIFEST.read_text())
        if "unreal_import" in previous:
            result["unreal_import"] = previous["unreal_import"]
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Saved {MANIFEST.relative_to(ROOT)}", flush=True)


if __name__ == "__main__":
    main()
