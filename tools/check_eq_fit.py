import argparse
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from tide import eq_band, eq_fit, eq_files, eq_text


def run(tool, *arguments):
    result = subprocess.run([str(tool), *map(str, arguments)], capture_output=True, text=True)
    return result.returncode, result.stdout.splitlines()


def check_curve(tool, preset, curve, problems):
    _, lines = run(tool, "curve", preset)
    words = lines[0].split()
    if int(words[1]) != eq_files.curve_key(curve) or int(words[3]) != curve.target():
        problems.append(f"{preset.name}: key or target differs: {lines[0]}")
    bands = [tuple(int(value) for value in line.split()[2:]) for line in lines[1:]]
    mine = [(band.frequency_hz, band.gain_tenths, band.q_hundredths, band.filter)
            for band in curve.bands]
    if bands != mine:
        problems.append(f"{preset.name}: bands differ")


def check_designs(tool, preset, curve, problems):
    _, lines = run(tool, "design", preset)
    for line in lines:
        words = [int(value) for value in line.split()[1:]]
        rate, index = words[0], words[1]
        stage = eq_band.design(curve.bands[index], rate)
        mine = [stage.g, stage.a1, stage.m1, stage.m2, int(stage.fine)]
        if mine != words[2:]:
            problems.append(f"{preset.name}: design of band {index} at {rate} Hz: {mine} vs {words[2:]}")


def check_fits(tool, preset, curve, work, problems):
    _, lines = run(tool, "fit", preset)
    player = {int(words[1]): (int(words[2]), int(words[3]))
              for words in (line.split() for line in lines)}
    started = time.monotonic()
    fits = eq_fit.fit_all(curve)
    seconds = time.monotonic() - started
    found = {fit.rate: fit.worst_hundredths for fit in fits}
    for rate, (today, theirs) in sorted(player.items()):
        grid = eq_fit.prepare(curve, rate)
        mine_today = eq_fit.today_hundredths(curve, rate, grid)
        if mine_today != today:
            problems.append(f"{preset.name}: usual error at {rate} Hz: {mine_today} vs {today}")
        if theirs != 0xFFFF and found.get(rate, 0xFFFF) > theirs:
            problems.append(f"{preset.name}: fit at {rate} Hz: {found.get(rate)} worse than {theirs}")
        print(f"  {rate:>6} Hz  usual {today / 100:5.2f}  player "
              f"{'  -  ' if theirs == 0xFFFF else f'{theirs / 100:5.2f}'}  tide "
              f"{'  -  ' if rate not in found else f'{found[rate] / 100:5.2f}'} dB")
    print(f"  {seconds:.1f} s")
    path = work / f"{preset.stem}.BIN"
    path.write_bytes(eq_files.fit_file(eq_files.curve_key(curve), curve.target(), fits))
    code, lines = run(tool, "check", preset, path)
    if code != 0 or not lines or not lines[-1].startswith("ok"):
        problems.append(f"{preset.name}: the player rejects Tide's file: {lines[-1:]}")


def main():
    parser = argparse.ArgumentParser(description="Check Tide's equaliser fit against the player's")
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--presets", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    arguments = parser.parse_args()
    arguments.work.mkdir(parents=True, exist_ok=True)
    problems = []
    for preset in sorted(arguments.presets.glob("*.txt")):
        print(preset.stem)
        curve = eq_text.parse(preset.read_bytes())
        check_curve(arguments.tool, preset, curve, problems)
        check_designs(arguments.tool, preset, curve, problems)
        check_fits(arguments.tool, preset, curve, arguments.work, problems)
    for problem in problems:
        print(problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
