"""Copy the Time Teller audio files into the numbered layout the MP3-TF-16P module needs.

The MP3 module can only play a file by folder number and file number (for
example 03/025.mp3), not by name. This script copies the audio folders
(Audio-files-for-esp32-new/Language_1, Language_2, Language_3) into that layout and writes
track-list.txt, which shows which original file became which number.

Each language gets a block of 33 folders: Language_1 = 01-33, Language_2 = 34-66,
Language_3 = 67-99. A missing Language folder is skipped. An Audio-files folder
with the old layout (no Language folders) is copied as Language_1.

Usage:
    python prepare_sd_card.py [<Audio-files folder> [<SD card or output folder>]] [--overwrite]

Example (Windows, SD card is drive E:):
    python prepare_sd_card.py ..\\Audio-files-for-esp32-new E:\\

If the folders are not given on the command line, DEFAULT_SOURCE and
DEFAULT_DEST below are used, so you can just run:
    python prepare_sd_card.py
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

# Used when source / dest are not given on the command line.
# A relative path here is relative to this script's folder, not the current directory.
DEFAULT_SOURCE = r"C:\Users\gokul\Documents\projects\time-teller-clock\time-teller-clock\Audio-files-for-esp32-new"
DEFAULT_DEST = "E:\\"

MAX_FILES_PER_FOLDER = 255
TEST_SONG = "testsong.mp3"  # compared in lowercase
MONTHS = ["january", "february", "march", "april", "may", "june",
          "july", "august", "september", "october", "november", "december"]
DAYS = ["sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"]

# Folder numbers - these must match the FOLDER_* constants in esp32-time-teller-clock.ino
FOLDER_RYTHEM, FOLDER_WISHING, FOLDER_TIME, FOLDER_DATE, FOLDER_MONTH, FOLDER_DAY = 1, 2, 3, 4, 5, 6
FOLDER_CHURCH_NAME, FOLDER_QUOTES, FOLDER_EXTRA_QUOTES = 7, 8, 9
FOLDER_HAPPY_MORNING, FOLDER_HAPPY_EVENING, FOLDER_SUNDAY_SONGS, FOLDER_MONTHLY_SONGS = 10, 11, 12, 13
FOLDER_CUSTOM, FOLDER_TEST, FIRST_CUSTOM_FOLDER = 14, 15, 25
FOLDER_YEAR, FOLDER_DONGLE, FOLDER_BLESSING = 16, 17, 18
FOLDER_HAPPY_EARLY, FOLDER_HAPPY_NIGHT = 19, 20
FOLDER_GOOD_FRIDAY, FOLDER_EASTER, FOLDER_CHRISTMAS, FOLDER_NEW_YEAR = 21, 22, 23, 24
PROMISE_FIRST_TRACK = 101  # monthly_promise goes into the Month folder: January = 101 ... December = 112
LANGUAGE_COUNT, LANGUAGE_FOLDERS = 3, 33   # Language_N uses folders (N-1)*33 + 1 ... N*33
MAX_CUSTOM_FOLDERS = LANGUAGE_FOLDERS - FIRST_CUSTOM_FOLDER + 1  # 9 Custom_songs sub-folders

TIME_RE = re.compile(r"^time-(\d{1,2}) (\d{2}) (AM|PM)\.mp3$", re.IGNORECASE)
DATE_RE = re.compile(r"^date-(\d{1,2})\.mp3$", re.IGNORECASE)
MONTH_RE = re.compile(r"^month-([a-z]+)\.mp3$", re.IGNORECASE)
DAY_RE = re.compile(r"^day-([a-z]+)\.mp3$", re.IGNORECASE)
YEAR_RE = re.compile(r"^year-(\d{4})\.mp3$", re.IGNORECASE)
PROMISE_RE = re.compile(r"^month-([a-z]+)\.mp3$", re.IGNORECASE)


def mp3_files(folder):
    """All .mp3 files in a folder, sorted by name (empty list if the folder is missing)."""
    if not folder.is_dir():
        return []
    files = [f for f in folder.iterdir() if f.is_file() and f.suffix.lower() == ".mp3"]
    return sorted(files, key=lambda f: f.name.lower())


def time_track(name):
    """time-6 15 AM.mp3 -> 26  (12:00 AM = 1, 12:15 AM = 2 ... 11:45 PM = 96)"""
    match = TIME_RE.match(name)
    if not match:
        return None
    hour12, minute, am_pm = int(match.group(1)), int(match.group(2)), match.group(3).upper()
    if not 1 <= hour12 <= 12 or minute not in (0, 15, 30, 45):
        return None
    hour24 = hour12 % 12 + (12 if am_pm == "PM" else 0)
    return hour24 * 4 + minute // 15 + 1


def date_track(name):
    """date-29.mp3 -> 29"""
    match = DATE_RE.match(name)
    if not match or not 1 <= int(match.group(1)) <= 31:
        return None
    return int(match.group(1))


def month_track(name):
    """month-september.mp3 -> 9"""
    match = MONTH_RE.match(name)
    if not match or match.group(1).lower() not in MONTHS:
        return None
    return MONTHS.index(match.group(1).lower()) + 1


def day_track(name):
    """day-sunday.mp3 -> 1  (Sunday = 1 ... Saturday = 7)"""
    match = DAY_RE.match(name)
    if not match or match.group(1).lower() not in DAYS:
        return None
    return DAYS.index(match.group(1).lower()) + 1


def promise_track(name):
    """monthly_promise: month-jan.mp3 or month-january.mp3 -> 101 ... month-dec.mp3 -> 112.
    The month may be shortened to its first 3 or more letters (jan, feb, march, sept...)."""
    match = PROMISE_RE.match(name)
    if not match or len(match.group(1)) < 3:
        return None
    word = match.group(1).lower()
    for number, month in enumerate(MONTHS):
        if month.startswith(word):
            return PROMISE_FIRST_TRACK + number
    return None


def year_track(name):
    """year-2026.mp3 -> 26  (2001 = 1 ... 2255 = 255)"""
    match = YEAR_RE.match(name)
    if not match or not 2001 <= int(match.group(1)) <= 2255:
        return None
    return int(match.group(1)) - 2000


class SdCardBuilder:
    def __init__(self, source, dest):
        self.source = source
        self.dest = dest
        self.sections = []  # (title, [lines])
        self.warnings = []

    def _copy(self, file, folder, track):
        target_dir = self.dest / f"{folder:02d}"
        target_dir.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target_dir / f"{track:03d}.mp3")
        return f"  {folder:02d}/{track:03d}.mp3  <-  {file.relative_to(self.source)}"

    def copy_in_order(self, folder, files, title):
        """Number the files 001, 002, ... in name order (random folders and custom songs)."""
        if len(files) > MAX_FILES_PER_FOLDER:
            self.warnings.append(f"{title}: only the first {MAX_FILES_PER_FOLDER} of {len(files)} files were copied")
            files = files[:MAX_FILES_PER_FOLDER]
        lines = [self._copy(f, folder, i) for i, f in enumerate(files, start=1)]
        if not files:
            self.warnings.append(f"{title}: no .mp3 files, folder {folder:02d} not created")
        self.sections.append((f"{folder:02d}  {title}  ({len(files)} files)", lines))

    def copy_by_name(self, folder, source_dir, track_for_name, expected, title):
        """Number the files from their names (time, date, month, day, year).
        expected=None: any number of files, no missing-file warning (year)."""
        lines, used = [], {}
        for file in mp3_files(source_dir):
            track = track_for_name(file.name)
            if track is None:
                self.warnings.append(f"{title}: file name not recognised, skipped: {file.name}")
            elif track in used:
                self.warnings.append(f"{title}: {file.name} and {used[track]} both map to {track:03d}.mp3")
            else:
                used[track] = file.name
                lines.append((track, self._copy(file, folder, track)))
        if expected is None:
            if not used:
                self.warnings.append(f"{title}: no .mp3 files, folder {folder:02d} not created")
            self.sections.append((f"{folder:02d}  {title}  ({len(used)} files)",
                                  [line for _, line in sorted(lines)]))
            return
        missing = [n for n in range(1, expected + 1) if n not in used]
        if missing:
            self.warnings.append(f"{title}: missing {len(missing)} of {expected} files "
                                 f"(track numbers {', '.join(str(n) for n in missing)})")
        self.sections.append((f"{folder:02d}  {title}  ({len(used)} of {expected} files)",
                              [line for _, line in sorted(lines)]))

    def write_track_list(self):
        text = ["Time Teller SD card - made by prepare_sd_card.py", ""]
        for title, lines in self.sections:
            text.append(title)
            text.extend(lines)
            text.append("")
        if self.warnings:
            text.append("Warnings")
            text.extend(f"  - {w}" for w in self.warnings)
        (self.dest / "track-list.txt").write_text("\n".join(text) + "\n", encoding="utf-8")


def copy_language(builder, src, base, lang):
    """Copy one Language folder into SD folders base+1 ... base+33."""
    builder.copy_in_order(base + FOLDER_RYTHEM, mp3_files(src / "Rythem"), f"{lang}/Rythem (random)")
    builder.copy_in_order(base + FOLDER_WISHING, mp3_files(src / "Wishing"), f"{lang}/Wishing (random)")
    builder.copy_by_name(base + FOLDER_TIME, src / "Time", time_track, 96, f"{lang}/Time")
    builder.copy_by_name(base + FOLDER_DATE, src / "Date", date_track, 31, f"{lang}/Date")
    builder.copy_by_name(base + FOLDER_MONTH, src / "Month", month_track, 12, f"{lang}/Month")
    promises = mp3_files(src / "monthly_promise")
    if promises:
        builder.copy_by_name(base + FOLDER_MONTH, src / "monthly_promise", promise_track, None,
                             f"{lang}/monthly_promise (month-jan.mp3 -> 101 ... month-dec.mp3 -> 112)")
        found = {promise_track(f.name) for f in promises}
        missing = [MONTHS[n][:3] for n in range(12) if PROMISE_FIRST_TRACK + n not in found]
        if missing:
            builder.warnings.append(f"{lang}/monthly_promise: no file for {', '.join(missing)} (nothing plays in those months)")
    else:
        builder.warnings.append(f"{lang}/monthly_promise: no .mp3 files, no monthly promise in this language")
    builder.copy_by_name(base + FOLDER_DAY, src / "Day", day_track, 7, f"{lang}/Day")
    builder.copy_by_name(base + FOLDER_YEAR, src / "Year", year_track, None, f"{lang}/Year (year-2026.mp3 -> 026)")
    dongle = mp3_files(src / "Dongle")
    if len(dongle) > 1:
        builder.warnings.append(f"{lang}/Dongle: {len(dongle)} files, only the first ({dongle[0].name}) is used as the bell")
    builder.copy_in_order(base + FOLDER_DONGLE, dongle[:1], f"{lang}/Dongle (bell, rung once per hour)")
    builder.copy_in_order(base + FOLDER_CHURCH_NAME, mp3_files(src / "church_name"), f"{lang}/church_name (random)")
    builder.copy_in_order(base + FOLDER_BLESSING, mp3_files(src / "Blessing_prayer"), f"{lang}/Blessing_prayer (random)")
    builder.copy_in_order(base + FOLDER_QUOTES, mp3_files(src / "Quotes"), f"{lang}/Quotes (random)")
    builder.copy_in_order(base + FOLDER_EXTRA_QUOTES, mp3_files(src / "extra_quotes"), f"{lang}/extra_quotes (random)")
    builder.copy_in_order(base + FOLDER_HAPPY_EARLY, mp3_files(src / "happy_songs_early_morning"),
                          f"{lang}/happy_songs_early_morning (random, 12 AM - 6 AM)")
    builder.copy_in_order(base + FOLDER_HAPPY_MORNING, mp3_files(src / "happy_songs_morning"),
                          f"{lang}/happy_songs_morning (random, 6 AM - 12 PM)")
    builder.copy_in_order(base + FOLDER_HAPPY_EVENING, mp3_files(src / "happy_songs_evening"),
                          f"{lang}/happy_songs_evening (random, 12 PM - 6 PM)")
    builder.copy_in_order(base + FOLDER_HAPPY_NIGHT, mp3_files(src / "happy_songs_night"),
                          f"{lang}/happy_songs_night (random, 6 PM - 12 AM)")
    builder.copy_in_order(base + FOLDER_SUNDAY_SONGS, mp3_files(src / "sunday_songs"), f"{lang}/sunday_songs (random)")
    builder.copy_in_order(base + FOLDER_MONTHLY_SONGS, mp3_files(src / "monthly_songs"), f"{lang}/monthly_songs (random)")
    builder.copy_in_order(base + FOLDER_GOOD_FRIDAY, mp3_files(src / "good_friday"),
                          f"{lang}/good_friday (random, Palm Sunday - Holy Saturday)")
    builder.copy_in_order(base + FOLDER_EASTER, mp3_files(src / "easter_sunday"),
                          f"{lang}/easter_sunday (random, Easter Sunday)")
    builder.copy_in_order(base + FOLDER_CHRISTMAS, mp3_files(src / "christmas"),
                          f"{lang}/christmas (random, 25-31 December)")
    builder.copy_in_order(base + FOLDER_NEW_YEAR, mp3_files(src / "new_year"),
                          f"{lang}/new_year (random, 1-7 January)")

    custom_dir = src / "Custom_songs"
    custom_files = mp3_files(custom_dir)
    songs = [f for f in custom_files if f.name.lower() != TEST_SONG]
    tests = [f for f in custom_files if f.name.lower() == TEST_SONG]
    builder.copy_in_order(base + FOLDER_CUSTOM, songs, f"{lang}/Custom_songs - use 'One song' with the file number")
    builder.copy_in_order(base + FOLDER_TEST, tests, f"{lang}/Test song")

    subfolders = sorted((d for d in custom_dir.iterdir() if d.is_dir()), key=lambda d: d.name.lower()) \
        if custom_dir.is_dir() else []
    for number, sub in enumerate(subfolders, start=1):
        if number > MAX_CUSTOM_FOLDERS:
            builder.warnings.append(f"{lang}/Custom_songs: more than {MAX_CUSTOM_FOLDERS} sub-folders, "
                                    f"'{sub.name}' and later were skipped")
            break
        builder.copy_in_order(base + FIRST_CUSTOM_FOLDER + number - 1, mp3_files(sub),
                              f"{lang}/Custom_songs/{sub.name} - use 'Random from a sub-folder' {number}")


def main():
    parser = argparse.ArgumentParser(description="Copy Audio-files into the numbered layout for the MP3-TF-16P module.")
    parser.add_argument("source", type=Path, nargs="?",
                        help=f"the audio folder with Language_1/2/3 (default: {DEFAULT_SOURCE})")
    parser.add_argument("dest", type=Path, nargs="?",
                        help=f"the SD card root, or a folder to copy to the card later (default: {DEFAULT_DEST})")
    parser.add_argument("--overwrite", action="store_true",
                        help="delete numbered folders (01-99) already in dest before copying")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    source = args.source.resolve() if args.source else (script_dir / DEFAULT_SOURCE).resolve()
    dest = args.dest.resolve() if args.dest else (script_dir / DEFAULT_DEST).resolve()
    if not source.is_dir():
        sys.exit(f"Audio-files folder not found: {source}")
    dest.mkdir(parents=True, exist_ok=True)

    existing = sorted(p for p in dest.iterdir() if p.is_dir() and re.fullmatch(r"\d{2}", p.name))
    if existing:
        if not args.overwrite:
            sys.exit(f"{dest} already has numbered folders ({', '.join(p.name for p in existing)}).\n"
                     "Use an empty card, or run again with --overwrite to replace them.")
        for folder in existing:
            shutil.rmtree(folder)

    builder = SdCardBuilder(source, dest)
    languages = [source / f"Language_{n}" for n in range(1, LANGUAGE_COUNT + 1)]
    if not any(d.is_dir() for d in languages):
        print("No Language_1/2/3 folders found: copying the folders as Language_1")
        languages = [source] + languages[1:]
    for n, lang_dir in enumerate(languages, start=1):
        if lang_dir.is_dir():
            copy_language(builder, lang_dir, (n - 1) * LANGUAGE_FOLDERS, f"Language_{n}")
        else:
            builder.warnings.append(f"Language_{n}: folder not found, folders "
                                    f"{(n - 1) * LANGUAGE_FOLDERS + 1:02d}-{n * LANGUAGE_FOLDERS:02d} not created")

    builder.write_track_list()
    print((dest / "track-list.txt").read_text(encoding="utf-8"))
    print(f"Done. Files copied to {dest}")


if __name__ == "__main__":
    main()
