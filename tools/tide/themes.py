import argparse
import re
import shutil
import struct
import sys
from pathlib import Path

FOLDER = "Themes"
SUFFIX = ".rippltheme"
MAGIC = b"RPLT"
HEADER = struct.Struct("<4sHHII")
NAME_LIMIT = 23
SHOWN_LIMIT = 8
SIZE_LIMIT = 4 * 1024 * 1024
NAME = re.compile(r"^[^/\\:*?\"<>|]{1,23}$")


class ThemeError(Exception):
    pass


def theme_problem(data):
    if len(data) < HEADER.size:
        return "shorter than its header"
    if len(data) > SIZE_LIMIT:
        return f"{len(data):,} bytes, too big for a theme"
    magic, _version, header_size, checked, _checksum = HEADER.unpack_from(data)
    if magic != MAGIC:
        return "not a Rippl theme (.rippltheme)"
    if header_size > len(data) or checked > len(data) or checked < header_size:
        return "its size fields don't match the file"
    return None


def themes_on(card):
    folder = card / FOLDER
    if not folder.is_dir():
        return []
    return sorted(path for path in folder.iterdir()
                  if path.is_file() and path.suffix.lower() == SUFFIX and not path.name.startswith("."))


def install(card, files, dry_run=False):
    folder = card / FOLDER
    for name in files:
        source = Path(name)
        if source.suffix.lower() != SUFFIX:
            raise ThemeError(f"{source.name}: a theme file ends in {SUFFIX}")
        stem = source.stem
        if not NAME.match(stem) or len(stem.encode("utf-8")) > NAME_LIMIT:
            raise ThemeError(f"{source.name}: the player shows names of 1 to {NAME_LIMIT} bytes")
        data = source.read_bytes()
        problem = theme_problem(data)
        if problem:
            raise ThemeError(f"{source.name}: {problem}")
        print(f"  {stem:<24} {FOLDER}/{source.name}, {len(data):,} bytes")
        if not dry_run:
            folder.mkdir(exist_ok=True)
            shutil.copyfile(source, folder / source.name)


def remove(card, names, dry_run=False):
    for name in names:
        path = card / FOLDER / (Path(name).stem + SUFFIX)
        if path.is_file():
            print(f"  removing {FOLDER}/{path.name}")
            if not dry_run:
                path.unlink()
        else:
            print(f"  {FOLDER}/{path.name} isn't there")


def parse_arguments(argv):
    parser = argparse.ArgumentParser(
        prog="tide themes",
        description="Put theme files on the card, in its Themes folder, for Settings → Theme.")
    parser.add_argument("card", help="the card's root folder, for example E:\\ or /media/card")
    parser.add_argument("action", nargs="?", choices=("list", "install", "remove"),
                        default="list")
    parser.add_argument("names", nargs="*",
                        help="theme files to install, or theme names to remove")
    parser.add_argument("--dry-run", action="store_true", help="show what would change")
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(sys.argv[1:] if argv is None else argv)
    card = Path(arguments.card)
    try:
        if not card.is_dir():
            raise ThemeError(f"{card} isn't a folder or drive. Give the card's drive.")
        if arguments.action == "install":
            if not arguments.names:
                raise ThemeError("Name the theme files to install, for example: "
                                 "tide themes E: install turntable.rippltheme")
            install(card, arguments.names, dry_run=arguments.dry_run)
        elif arguments.action == "remove":
            remove(card, arguments.names, dry_run=arguments.dry_run)
        present = themes_on(card)
        print(f"Themes on {card}:")
        for path in present:
            print(f"  {path.stem}")
        if len(present) > SHOWN_LIMIT:
            print(f"Settings → Theme shows the first {SHOWN_LIMIT} only; remove some to see the rest.")
        print("Choose one in Settings → Display → Theme.")
    except (ThemeError, OSError) as error:
        print(f"tide themes: {error}", file=sys.stderr)
        return 2
    return 0
