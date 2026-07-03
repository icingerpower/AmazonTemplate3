#!/usr/bin/env python3
"""
Backfill aplus/uploadedAt in settings.ini for existing sizing folders.
For each folder that has marketing image elements (id starts with "image_")
with at least one non-txt version, writes:
  [aplus]
  uploadedAt=<latest generated date>
Skips folders matching B0FY6V8VM6-* and B0CKPJ6QCG-*.
Skips folders that already have uploadedAt set.
"""

import json
import os
import re
import sys

SIZING_DIR = "/home/cedric/Dropbox/freelancers/projects/workingDirectory/amazonTemplate3/sizing"
SKIP_PREFIXES = ("B0FY6V8VM6-", "B0CKPJ6QCG-")


def get_latest_image_date(index_json_path: str) -> str | None:
    """Return the latest generated date from image_* elements with non-txt versions."""
    try:
        with open(index_json_path) as f:
            data = json.load(f)
    except Exception as e:
        print(f"  [error] Cannot read {index_json_path}: {e}")
        return None

    latest = None
    for element in data.get("elements", []):
        eid = element.get("id", "")
        if not eid.startswith("image_"):
            continue
        for version in element.get("versions", []):
            desktop = version.get("desktop", "")
            if desktop.endswith(".txt"):
                continue
            generated = version.get("generated", "")
            if generated and (latest is None or generated > latest):
                latest = generated
    return latest


def inject_uploaded_at(settings_path: str, date_str: str) -> bool:
    """
    Insert uploadedAt=<date_str> into [aplus] section.
    Returns True if the file was modified.
    Uses string manipulation to avoid configparser mangling backslash-keys.
    """
    if os.path.exists(settings_path):
        with open(settings_path) as f:
            content = f.read()
    else:
        content = ""

    # Already set — skip
    if re.search(r'^\s*uploadedAt\s*=', content, re.MULTILINE | re.IGNORECASE):
        return False

    # Does [aplus] section exist?
    aplus_match = re.search(r'^\[aplus\]\s*$', content, re.MULTILINE | re.IGNORECASE)
    if aplus_match:
        # Insert the key right after the [aplus] line
        insert_pos = aplus_match.end()
        new_content = content[:insert_pos] + f"\nuploadedAt={date_str}" + content[insert_pos:]
    else:
        # Append a new [aplus] section at the end
        if content and not content.endswith("\n"):
            content += "\n"
        new_content = content + f"\n[aplus]\nuploadedAt={date_str}\n"

    with open(settings_path, "w") as f:
        f.write(new_content)
    return True


def main():
    if not os.path.isdir(SIZING_DIR):
        print(f"ERROR: sizing dir not found: {SIZING_DIR}")
        sys.exit(1)

    folders = sorted(os.listdir(SIZING_DIR))
    processed = skipped_prefix = skipped_no_images = skipped_already = errors = 0

    for folder in folders:
        folder_path = os.path.join(SIZING_DIR, folder)
        if not os.path.isdir(folder_path):
            continue

        if any(folder.startswith(p) for p in SKIP_PREFIXES):
            print(f"  skip (excluded): {folder}")
            skipped_prefix += 1
            continue

        index_json = os.path.join(folder_path, "aplus", "index.json")
        if not os.path.exists(index_json):
            print(f"  skip (no index.json): {folder}")
            skipped_no_images += 1
            continue

        latest_date = get_latest_image_date(index_json)
        if not latest_date:
            print(f"  skip (no image versions): {folder}")
            skipped_no_images += 1
            continue

        settings_path = os.path.join(folder_path, "settings.ini")
        modified = inject_uploaded_at(settings_path, latest_date)
        if modified:
            print(f"  ✓ {folder}  →  uploadedAt={latest_date}")
            processed += 1
        else:
            print(f"  already set: {folder}")
            skipped_already += 1

    print()
    print(f"Done. Written: {processed}  |  Already set: {skipped_already}  "
          f"|  No images: {skipped_no_images}  |  Excluded: {skipped_prefix}")


if __name__ == "__main__":
    main()
