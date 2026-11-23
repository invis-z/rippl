import argparse
import json
import math
import shutil
import sys
import tempfile
from pathlib import Path

import cover_export
import library_index
from tide import covers as tide_covers
from tide import records, rippl_format, store

STREAM_LIBRARY = 1
STREAM_RECORDS = 2
REGION_LIBRARY = 0
VALUE_RECORDS = 0

COVER_SIZE = 240

ALBUMS = (
    ("Paper Lanterns", "Slow Tide", 2019, "Indie", "rings", ((24, 60, 92), (240, 196, 120)),
     ("Harbour Lights", "Slow Tide", "Kite String", "Seven Bridges", "Low Sun", "Salt and Ember",
      "Quiet Engines")),
    ("Paper Lanterns", "Glass Harbour", 2022, "Indie", "stripes", ((20, 84, 88), (236, 232, 208)),
     ("First Ferry", "Glass Harbour", "Weathervane", "Nine Lamps", "Tidewrack", "Home by Morning")),
    ("Northern Static", "Signal Fires", 2016, "Electronic", "grid", ((36, 20, 72), (96, 232, 208)),
     ("Carrier Wave", "Signal Fires", "Blue Shift", "Halogen", "Night Relay", "Zero Crossing",
      "Afterimage", "Last Transmission")),
    ("The Copper Owls", "Lowlands", 2011, "Folk", "hills", ((72, 44, 24), (232, 180, 96)),
     ("Marsh Road", "Lowlands", "Barley Moon", "The Miller's Reel", "Stone Fences", "Come Evening")),
    ("Juno Vale", "Afterglow Motel", 2021, "Pop", "sun", ((200, 56, 96), (252, 208, 96)),
     ("Vacancy", "Afterglow Motel", "Neon Pool", "Postcards", "Room Eleven", "Check Out Time",
      "Long Way Round")),
    ("Halden Trio", "Blue Hours", 1998, "Jazz", "diagonal", ((16, 28, 64), (120, 168, 232)),
     ("Blue Hours", "Third Street", "Slow Brush", "A Walk at Two", "Small Rooms", "Closing Set")),
    ("Vast Meridian", "Orbits", 2024, "Ambient", "rings", ((12, 16, 28), (168, 144, 232)),
     ("Perihelion", "Slow Orbit", "Dust Lanes", "Far Side", "Aphelion")),
    ("Saltwater Radio", "Tape Season", 2008, "Rock", "stripes", ((40, 40, 44), (232, 88, 56)),
     ("Rewind", "Tape Season", "Dial Tone Summer", "Static on the Pier", "Boardwalk", "Hiss",
      "Side B", "Fade Out")),
    ("Marisol and the Overnight Cartographers", "Everything We Meant to Say Before the Last Train",
     2023, "Indie", None, None,
     ("The Long Way Home Through Every Town We Never Stopped In", "Platform Nine", "Window Seat")),
)
LYRICS_TRACK = ("Juno Vale", "Afterglow Motel", 2)
LYRICS = (
    (0, "Neon in the window, nobody home"),
    (9, "Pool lights hum a song of their own"),
    (18, "Keys on a ring with a plastic tag"),
    (27, "Everything I own in a paper bag"),
    (38, "Stay till the sign says vacancy"),
    (47, "Stay till the morning comes for me"),
    (58, "Afterglow, afterglow"),
    (66, "Leave the light on low"),
    (76, "Ice machine rattles down the hall"),
    (85, "Your name in pencil on the wall"),
    (96, "Afterglow, afterglow"),
    (104, "Leave the light on low"),
)


def seconds_of(album, track):
    return 160 + (album * 53 + track * 37) % 210


def mixed(first, second, share):
    return tuple(round(a + (b - a) * share) for a, b in zip(first, second))


def share_at(pattern, x, y):
    across, down = x / (COVER_SIZE - 1), y / (COVER_SIZE - 1)
    if pattern == "rings":
        return 0.5 + 0.5 * math.cos(math.hypot(across - 0.5, down - 0.62) * 26)
    if pattern == "stripes":
        return 1.0 if int((across + down * 0.6) * 9) % 2 else down * 0.35
    if pattern == "grid":
        return 1.0 if x % 30 < 3 or y % 30 < 3 else down * 0.5
    if pattern == "hills":
        return 1.0 if down > 0.55 + 0.12 * math.sin(across * 9) else down * 0.5
    if pattern == "sun":
        return 1.0 if math.hypot(across - 0.5, down - 0.7) < 0.3 else down * 0.6
    return (across + down) / 2


def cover_image(pattern, colors):
    from PIL import Image
    image = Image.new("RGB", (COVER_SIZE, COVER_SIZE))
    image.putdata([mixed(colors[0], colors[1], share_at(pattern, x, y))
                   for y in range(COVER_SIZE) for x in range(COVER_SIZE)])
    return image


def track_path(artist, album, index, title):
    return f"{artist}/{album}/{index:02d} {title}.flac"


def lyrics_file():
    lines = [f"[{seconds // 60:02d}:{seconds % 60:02d}.00]{text}" for seconds, text in LYRICS]
    return "".join(f"{line}\n" for line in lines).encode("utf-8")


def lyrics_card_path():
    artist, album, index = LYRICS_TRACK
    titles = next(entry[6] for entry in ALBUMS if entry[0] == artist and entry[1] == album)
    return "/" + track_path(artist, album, index, titles[index - 1]).rsplit(".", 1)[0] + ".lrc"


def tracks_and_covers():
    tracks, covers = [], {}
    for number, (artist, album, year, genre, pattern, colors, titles) in enumerate(ALBUMS):
        if pattern:
            covers[(artist, album)] = cover_image(pattern, colors)
        for index, title in enumerate(titles, 1):
            tracks.append(library_index.TrackInfo(
                path=track_path(artist, album, index, title), title=title, artist=artist,
                album_artist=artist, album=album, genre=genre, year=year, disc=1, number=index,
                duration_ms=seconds_of(number, index) * 1000))
    return tracks, covers


def cover_table(model, images):
    result = tide_covers.CoverResult()
    sources = cover_export.album_sources(model)
    rows = bytearray()
    previous = None
    for key, album in rippl_format.ranked_keys(model):
        if previous is not None and key == previous[0]:
            rows += tide_covers.ROW.pack(*previous[1])
            continue
        source = sources[album]
        image = images.get((source.artist, source.title))
        row = (tide_covers.NO_COVER, 0)
        if image is not None:
            row = (tide_covers.append_record(result, cover_export.cover_record(image)), 0)
            result.covers += 1
        rows += tide_covers.ROW.pack(*row)
        previous = (key, row)
    result.table = bytes(rows)
    return result


def write_store(folder, model, images):
    card_store = store.Store(folder, False)
    old = tide_covers.OldCovers(card_store)
    library = rippl_format.encode_library(model)
    card_store.write(STREAM_LIBRARY, store.POOL_LIBRARY, library)
    card_store.write(STREAM_RECORDS, store.POOL_LIBRARY, records.encode_records([]))
    card_store.set_region(REGION_LIBRARY, STREAM_LIBRARY, 0, len(library))
    card_store.set_value(VALUE_RECORDS, STREAM_RECORDS)
    tide_covers.stage(card_store, old, cover_table(model, images))
    card_store.commit()


def build(output):
    tracks, images = tracks_and_covers()
    output.mkdir(parents=True, exist_ok=True)
    model = library_index.build_library(tracks, [])
    files = []
    with tempfile.TemporaryDirectory() as temporary:
        folder = Path(temporary)
        write_store(folder, model, images)
        for path in sorted(folder.rglob("*")):
            if path.is_file():
                name = path.relative_to(folder).as_posix()
                copy = "store_" + name.replace("/", "_")
                shutil.copyfile(path, output / copy)
                files.append({"path": "/.rippl/" + name, "file": copy})
    (output / "lyrics.lrc").write_bytes(lyrics_file())
    files.append({"path": lyrics_card_path(), "file": "lyrics.lrc"})
    (output / "demo_files.json").write_text(json.dumps(files, indent=1), encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Write the invented library the theme editor shows")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args(argv)
    try:
        build(arguments.output)
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
