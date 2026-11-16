#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 GiuxLab
"""Compile simple GNU gettext PO files into standard MO catalogs, without installing gettext.
Supports UTF-8 msgid/msgstr, multiline strings, comments, and fuzzy markers.
Plural forms and msgctxt deliberately fail until implemented rather than being silently corrupted.
"""
import ast
import struct
import sys
from pathlib import Path

def parse_po(path):
    data = {}
    msgid = None
    msgstr = None
    field = None
    fuzzy = False
    def commit():
        if msgid is not None and msgstr is not None and not fuzzy:
            if msgid in data:
                raise ValueError(f"duplicate msgid {msgid!r}")
            data[msgid] = msgstr
    for raw in path.read_text(encoding="utf-8").splitlines() + [""]:
        line = raw.strip()
        if not line:
            commit()
            msgid = None
            msgstr = None
            field = None
            fuzzy = False
            continue
        if line.startswith("#,") and "fuzzy" in line:
            fuzzy = True
            continue
        if line.startswith("#"):
            continue
        if line.startswith("msgctxt") or line.startswith("msgid_plural") or line.startswith("msgstr["):
            raise ValueError("Context/plural translations require a full gettext compiler")
        if line.startswith("msgid "):
            field = "id"
            msgid = ast.literal_eval(line[6:])
        elif line.startswith("msgstr "):
            field = "str"
            msgstr = ast.literal_eval(line[7:])
        elif line.startswith('"'):
            value = ast.literal_eval(line)
            if field == "id":
                msgid += value
            elif field == "str":
                msgstr += value
            else:
                raise ValueError(f"orphaned PO continuation: {line}")
        else:
            raise ValueError(f"unexpected PO line: {line}")
    return data

def build_mo(entries):
    keys = sorted(entries)
    originals = [k.encode("utf-8") for k in keys]
    translated = [entries[k].encode("utf-8") for k in keys]
    count = len(keys)
    original_table = 28
    translated_table = original_table + count * 8
    strings_offset = translated_table + count * 8
    ids_blob = bytearray()
    msg_blob = bytearray()
    ids_table = []
    msg_table = []
    for k in originals:
        ids_table.append((len(k), strings_offset + len(ids_blob)))
        ids_blob.extend(k + b"\x00")
    for v in translated:
        msg_table.append((len(v), strings_offset + len(ids_blob) + len(msg_blob)))
        msg_blob.extend(v + b"\x00")
    header = struct.pack("<7I", 0x950412DE, 0, count, original_table,
                         translated_table, 0, 0)
    original_index = b"".join(struct.pack("<2I", *item) for item in ids_table)
    translation_index = b"".join(struct.pack("<2I", *item) for item in msg_table)
    return header + original_index + translation_index + ids_blob + msg_blob

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: compile_po.py input.po output.mo")
    source, output = map(Path, sys.argv[1:])
    translations = parse_po(source)
    if not translations:
        raise SystemExit("No translations found")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(build_mo(translations))
    print(f"{source}: {len(translations)} messages => {output}")
