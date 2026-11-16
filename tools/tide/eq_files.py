import os
import struct
from dataclasses import dataclass

from tide import eq_fit, eq_text

FNV_START = 2166136261
FNV_PRIME = 16777619
WORD_MASK = 0xFFFFFFFF
MAGIC = b"EQFT"
VERSION = 1
FIT_METHOD = 2
HEADER_BYTES = 16
STAGE_BYTES = 17
ENTRY_BYTES = 8 + STAGE_BYTES * eq_text.MAX_BANDS
CHECKSUM_BYTES = 4
SLOT_BYTES = 4096
SLOTS = 100
PRESETS = 100
NAME_CAPACITY = 64
FINE_FLAG = 0x80
EQ_FOLDER = "EQ"
FITS_FOLDER = "equaliser"
STORE_FILE = "equaliser.bin"
MAP_OFFSET = 75
MAP_BYTES = SLOTS * 4 + 4


def fnv(data, start=FNV_START):
    value = start
    for byte in data:
        value = ((value ^ byte) * FNV_PRIME) & WORD_MASK
    return value


def curve_key(curve):
    key = fnv(struct.pack("<II", FIT_METHOD, curve.target()))
    for index, band in enumerate(curve.bands[:eq_text.MAX_BANDS]):
        key = fnv(struct.pack("<HHHBB", band.frequency_hz & 0xFFFF, band.gain_tenths & 0xFFFF,
                              band.q_hundredths & 0xFFFF, band.filter, index), key)
    return key


def fittable(curve):
    target = curve.target()
    return any(band.gain_tenths != 0 and band.frequency_hz > 0 and band.q_hundredths > 0
               and band.filter <= eq_text.HIGH_SHELF and band.frequency_hz * 20 < target * 9
               for band in curve.bands[:eq_text.MAX_BANDS])


def _entry(fit):
    data = bytearray(ENTRY_BYTES)
    struct.pack_into("<IHB", data, 0, fit.rate, fit.worst_hundredths & 0xFFFF, len(fit.stages))
    for index, stage in enumerate(fit.stages[:eq_text.MAX_BANDS]):
        struct.pack_into("<IIIIB", data, 8 + index * STAGE_BYTES, stage.g & WORD_MASK,
                         stage.a1 & WORD_MASK, stage.m1 & WORD_MASK, stage.m2 & WORD_MASK,
                         stage.band | (FINE_FLAG if stage.fine else 0))
    return bytes(data)


def fit_file(key, target_rate, fits):
    header = bytearray(HEADER_BYTES)
    header[0:4] = MAGIC
    header[4] = VERSION
    header[5] = len(fits)
    struct.pack_into("<II", header, 8, key, target_rate)
    body = bytes(header) + b"".join(_entry(fit) for fit in fits)
    data = body + struct.pack("<I", fnv(body))
    return data + bytes(SLOT_BYTES - len(data))


def read_header(data):
    if len(data) < HEADER_BYTES + CHECKSUM_BYTES or data[0:4] != MAGIC or data[4] != VERSION:
        return None
    entries = data[5]
    if entries > len(eq_fit.RATES):
        return None
    size = HEADER_BYTES + entries * ENTRY_BYTES
    if len(data) < size + CHECKSUM_BYTES:
        return None
    if struct.unpack_from("<I", data, size)[0] != fnv(data[:size]):
        return None
    key, target = struct.unpack_from("<II", data, 8)
    return key, target, entries


def slot_name(slot):
    return f"E{slot:02d}.BIN"


def slot_of(name):
    upper = name.upper()
    if len(upper) == 7 and upper[0] == "E" and upper[1:3].isdigit() and upper[3:] == ".BIN":
        return int(upper[1:3])
    return None


@dataclass
class Preset:
    name: str
    curve: eq_text.Curve
    key: int


def card_name_bytes(name):
    return os.fsencode(name) if isinstance(name, str) else name


def presets(card):
    folder = card / EQ_FOLDER
    found = []
    try:
        entries = sorted(os.scandir(folder), key=lambda entry: entry.name)
    except OSError:
        return found
    for entry in entries:
        name = entry.name
        encoded = name.encode("utf-8", "surrogateescape")
        if (name.startswith(".") or entry.is_dir(follow_symlinks=False) or len(encoded) <= 4
                or encoded[-4:].lower() != b".txt" or len(encoded) + 1 > NAME_CAPACITY):
            continue
        if len(found) == PRESETS:
            break
        try:
            with open(entry.path, "rb") as handle:
                data = handle.read(eq_text.MAX_BYTES + 1)
            curve = eq_text.parse(data)
        except (OSError, eq_text.ParseError):
            continue
        if fittable(curve):
            found.append(Preset(name, curve, curve_key(curve)))
    return found


def slots(folder):
    held = {}
    try:
        entries = list(os.scandir(folder))
    except OSError:
        return held
    for entry in entries:
        slot = slot_of(entry.name)
        if slot is None or not entry.is_file(follow_symlinks=False):
            continue
        try:
            with open(entry.path, "rb") as handle:
                data = handle.read(SLOT_BYTES)
        except OSError:
            continue
        held[slot] = (entry.path, read_header(data))
    return held


def key_map(keys):
    data = bytearray(SLOTS * 4)
    for slot, key in keys.items():
        struct.pack_into("<I", data, 4 * slot, key)
    return bytes(data) + struct.pack("<I", fnv(data))


def write_map(rippl, keys):
    path = rippl / STORE_FILE
    try:
        data = bytearray(path.read_bytes())
    except FileNotFoundError:
        data = bytearray()
    end = MAP_OFFSET + MAP_BYTES
    data.extend(bytes(max(0, end - len(data))))
    mapped = key_map(keys)
    if bytes(data[MAP_OFFSET:end]) == mapped:
        return
    data[MAP_OFFSET:end] = mapped
    rippl.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())


@dataclass
class Report:
    presets: int = 0
    fitted: int = 0
    kept: int = 0
    deleted: int = 0


def refresh(card, rippl, dry_run=False, fit_curve=eq_fit.fit_all, progress=None):
    report = Report()
    wanted = presets(card)
    report.presets = len(wanted)
    folder = rippl / FITS_FOLDER
    held = slots(folder)
    keys = {preset.key: preset for preset in wanted}
    used = set()
    mapped = {}
    for slot, (path, header) in sorted(held.items()):
        if header is not None and header[0] in keys and header[0] not in used:
            used.add(header[0])
            mapped[slot] = header[0]
            report.kept += 1
            continue
        report.deleted += 1
        if not dry_run:
            os.remove(path)
    taken = {slot for slot, (_, header) in held.items() if header is not None and header[0] in used}
    missing = [preset for preset in wanted if preset.key not in used]
    free = (slot for slot in range(SLOTS) if slot not in taken)
    for number, preset in enumerate(missing, start=1):
        slot = next(free, None)
        if slot is None:
            break
        if progress is not None:
            progress(number, len(missing), preset.name)
        fits = fit_curve(preset.curve)
        data = fit_file(preset.key, preset.curve.target(), fits)
        report.fitted += 1
        if dry_run:
            continue
        folder.mkdir(parents=True, exist_ok=True)
        with open(folder / slot_name(slot), "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        mapped[slot] = preset.key
    if not dry_run:
        write_map(rippl, mapped)
    return report
