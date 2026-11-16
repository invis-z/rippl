from dataclasses import dataclass, field

PEAK = 0
LOW_SHELF = 1
HIGH_SHELF = 2
OTHER = 3

MAX_BANDS = 10
MAX_BYTES = 4096
MAX_LINE = 160
WINDOW_BYTES = 3 + MAX_LINE + 1
MAX_TOKENS = 16
MAX_FILTER_ID = 999
MAX_FILTER_LINES = 32
GAIN_MIN_TENTHS = -120
GAIN_MAX_TENTHS = 120
Q_MIN_HUNDREDTHS = 10
Q_MAX_HUNDREDTHS = 1000
PREAMP_MIN_TENTHS = -240
PREAMP_MAX_TENTHS = 120
MAX_RATE = 96000
TARGET_RATE = 48000
TARGET_RATE_MIN = 8000
TARGET_RATE_MAX = 384000
UINT16_MAX = 0xFFFF
BOM = b"\xef\xbb\xbf"

FIELD_FREQUENCY = 1
FIELD_GAIN = 2
FIELD_Q = 4


@dataclass(frozen=True)
class Band:
    frequency_hz: int
    gain_tenths: int
    q_hundredths: int
    filter: int


@dataclass
class Curve:
    preamp_set: bool = False
    preamp_tenths: int = 0
    target_rate: int = 0
    bands: list = field(default_factory=list)

    def target(self):
        return self.target_rate if self.target_rate != 0 else TARGET_RATE


class ParseError(ValueError):
    pass


def _lower(text):
    return bytes(byte + 32 if 65 <= byte <= 90 else byte for byte in text)


def _is(token, word):
    return _lower(token) == word


def _digits(token):
    if not token:
        return None
    value = 0
    for byte in token:
        if not 48 <= byte <= 57:
            return None
        value = value * 10 + byte - 48
        if value > MAX_FILTER_ID:
            return None
    return value


def _scaled(token, places):
    index = 0
    negative = False
    if index < len(token) and token[index] in b"+-":
        negative = token[index] == ord("-")
        index += 1
    scaled = 0
    digits = 0
    while index < len(token) and 48 <= token[index] <= 57:
        scaled = scaled * 10 + token[index] - 48
        if scaled > 1000000:
            return None
        index += 1
        digits += 1
    fraction_digits = 0
    rounding = 0
    if index < len(token) and token[index] == ord("."):
        index += 1
        while index < len(token) and 48 <= token[index] <= 57:
            digit = token[index] - 48
            if fraction_digits < places:
                scaled = scaled * 10 + digit
            elif fraction_digits == places:
                rounding = 1 if digit >= 5 else 0
            fraction_digits += 1
            index += 1
        if fraction_digits == 0:
            return None
    if (digits == 0 and fraction_digits == 0) or index != len(token):
        return None
    for _ in range(fraction_digits, places):
        scaled *= 10
    scaled += rounding
    value = -scaled if negative else scaled
    return ((value + 0x80000000) & 0xFFFFFFFF) - 0x80000000


def _blanks(line, at):
    while at < len(line) and line[at] in b" \t":
        at += 1
    return at


def _after_word(line, at, word):
    if _lower(line[at:at + len(word)]) != word:
        return None
    return at + len(word)


def _rate_comment(line, curve):
    at = _blanks(line, 0)
    if at >= len(line) or line[at] != ord("#"):
        return
    at = _after_word(line, _blanks(line, at + 1), b"sample")
    if at is None or at >= len(line) or line[at] not in b" \t":
        return
    at = _after_word(line, _blanks(line, at), b"rate")
    at = _blanks(line, at) if at is not None else None
    if at is None or at >= len(line) or line[at] != ord(":"):
        return
    at = _blanks(line, at + 1)
    rate = 0
    digits = 0
    while at < len(line) and 48 <= line[at] <= 57 and digits < 7:
        rate = rate * 10 + line[at] - 48
        at += 1
        digits += 1
    at = _blanks(line, at)
    unit = _after_word(line, at, b"hz")
    at = _blanks(line, unit if unit is not None else at)
    if digits == 0 or at != len(line) or rate < TARGET_RATE_MIN or rate > TARGET_RATE_MAX:
        return
    curve.target_rate = rate


def _normalise(line):
    cut = line.find(b"#")
    if cut >= 0:
        line = line[:cut]
    colons = line.count(b":")
    if colons > 1:
        raise ParseError("syntax")
    return line.replace(b":", b" "), colons == 1


def _tokens(line):
    tokens = [token for token in line.replace(b"\t", b" ").split(b" ") if token]
    if len(tokens) > MAX_TOKENS:
        raise ParseError("syntax")
    return tokens


def _kind(token):
    if _is(token, b"pk") or _is(token, b"peq") or _is(token, b"modal"):
        return PEAK
    if _is(token, b"lsc") or _is(token, b"ls"):
        return LOW_SHELF
    if _is(token, b"hsc") or _is(token, b"hs"):
        return HIGH_SHELF
    return OTHER


def _field(token):
    if _is(token, b"fc"):
        return FIELD_FREQUENCY, b"hz"
    if _is(token, b"gain"):
        return FIELD_GAIN, b"db"
    if _is(token, b"q"):
        return FIELD_Q, None
    return None, None


def _apply(found, values, field_id, value, unit):
    if found & field_id:
        raise ParseError("duplicate field")
    places = 0 if field_id == FIELD_FREQUENCY else 1 if field_id == FIELD_GAIN else 2
    parsed = _scaled(value, places)
    if parsed is None:
        raise ParseError("value")
    if field_id == FIELD_FREQUENCY:
        if parsed <= 0 or parsed > UINT16_MAX or parsed * 20 >= MAX_RATE * 9:
            raise ParseError("frequency")
    elif field_id == FIELD_GAIN:
        if parsed < GAIN_MIN_TENTHS or parsed > GAIN_MAX_TENTHS:
            raise ParseError("gain")
    else:
        if unit is not None:
            raise ParseError("syntax")
        if parsed < Q_MIN_HUNDREDTHS or parsed > Q_MAX_HUNDREDTHS:
            raise ParseError("q")
    values[field_id] = parsed
    return found | field_id


def _read_fields(tokens, kind):
    index = 4
    found = 0
    values = {}
    while index < len(tokens):
        field_id, unit_name = _field(tokens[index])
        if field_id is None:
            if index == 4 or _is(tokens[index], b"bw"):
                return None
            raise ParseError("syntax")
        if index + 1 >= len(tokens):
            raise ParseError("syntax")
        unit = None
        if index + 2 < len(tokens) and unit_name is not None and _is(tokens[index + 2], unit_name):
            unit = tokens[index + 2]
        found = _apply(found, values, field_id, tokens[index + 1], unit)
        index += 3 if unit is not None else 2
    if kind != PEAK and found == FIELD_FREQUENCY | FIELD_GAIN:
        return None
    if found != FIELD_FREQUENCY | FIELD_GAIN | FIELD_Q:
        raise ParseError("syntax")
    return values


class _State:
    def __init__(self):
        self.curve = Curve()
        self.preamp_seen = False
        self.ids = []


def _preamp(tokens, state):
    if state.preamp_seen:
        raise ParseError("duplicate preamp")
    if len(tokens) < 2 or len(tokens) > 3:
        raise ParseError("syntax")
    tenths = _scaled(tokens[1], 1)
    if tenths is None:
        raise ParseError("value")
    if len(tokens) == 3 and not _is(tokens[2], b"db"):
        raise ParseError("syntax")
    if tenths < PREAMP_MIN_TENTHS or tenths > PREAMP_MAX_TENTHS:
        raise ParseError("preamp")
    state.curve.preamp_set = True
    state.curve.preamp_tenths = tenths
    state.preamp_seen = True


def _filter(tokens, state):
    if len(state.ids) == MAX_FILTER_LINES:
        raise ParseError("too many filters")
    if len(tokens) < 4:
        raise ParseError("syntax")
    filter_id = _digits(tokens[1])
    if filter_id is None or filter_id == 0:
        raise ParseError("value")
    if filter_id in state.ids:
        raise ParseError("duplicate filter")
    enabled = _is(tokens[2], b"on")
    if not enabled and not _is(tokens[2], b"off"):
        raise ParseError("syntax")
    state.ids.append(filter_id)
    if not enabled:
        return
    kind = _kind(tokens[3])
    values = None if kind == OTHER else _read_fields(tokens, kind)
    if values is None:
        return
    if len(state.curve.bands) == MAX_BANDS:
        raise ParseError("too many filters")
    state.curve.bands.append(Band(values[FIELD_FREQUENCY], values[FIELD_GAIN], values[FIELD_Q], kind))


def _line(line, state):
    line = line.split(b"\0")[0]
    _rate_comment(line, state.curve)
    line, has_colon = _normalise(line)
    tokens = _tokens(line)
    if not tokens:
        return
    if not has_colon:
        raise ParseError("syntax")
    if _is(tokens[0], b"preamp"):
        _preamp(tokens, state)
    elif _is(tokens[0], b"filter"):
        _filter(tokens, state)
    else:
        raise ParseError("unsupported directive")


def parse(data):
    if len(data) > MAX_BYTES:
        raise ParseError("too large")
    if not data:
        raise ParseError("empty")
    state = _State()
    position = 0
    while position < len(data):
        start = len(BOM) if position == 0 and data.startswith(BOM) else 0
        window = data[position:position + WINDOW_BYTES]
        end = window.find(b"\n", start)
        if end < 0:
            end = len(window)
        if end - start > MAX_LINE or (end == len(window) and position + len(window) < len(data)):
            raise ParseError("line too long")
        line = window[start:end]
        if line.endswith(b"\r"):
            line = line[:-1]
        _line(line, state)
        position += end + 1 if end < len(window) else len(window)
    if not state.preamp_seen and not state.ids:
        raise ParseError("empty")
    return state.curve
