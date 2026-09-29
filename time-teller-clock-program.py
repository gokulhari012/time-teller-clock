import os
import time
import random
import json
import copy
from datetime import datetime
import pygame
import platform
import socket
import board
import busio
from adafruit_pcf8563.pcf8563 import PCF8563
import RPi.GPIO as GPIO
from RPLCD.i2c import CharLCD
import threading
from calendar import monthrange


#GPio config
GPIO.setmode(GPIO.BCM)
# Example pins
output_pin_started = 14  # GPIO17 (pin 11 on header)
output_pin_failed = 15  # GPIO17 (pin 11 on header)
output_pin_speaker = 23  # need to solder the pin
input_pin_test_sound = 18   # GPIO27 (pin 13 on header)

# Setup pins
GPIO.setup(output_pin_started, GPIO.OUT)
GPIO.setup(output_pin_failed, GPIO.OUT)
GPIO.setup(output_pin_speaker, GPIO.OUT)
GPIO.setup(input_pin_test_sound, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.output(output_pin_started, GPIO.LOW)
GPIO.output(output_pin_failed, GPIO.LOW)

GPIO.output(output_pin_speaker, GPIO.LOW)

# --- Pin definitions ---
ROWS = [4, 17]
COLS = [27, 22]

# Button map layout
# KEYS = [
#     ['1', '2'],
#     ['3', '4']
# ]
LEFT = "LEFT"
RIGHT = "RIGHT"
SET = "SET"
BACK = "BACK"

KEYS = [
    [LEFT, RIGHT],
    [BACK, SET]
]

editing = False
speaker_output_required = True

BASE_DIR = "/home/pi/time-teller-clock/Audio-files"  # Change this
SCHEDULE_FILE = os.path.join(BASE_DIR, "schedule.json")

SETTINGS_FILE = os.path.join(BASE_DIR, "../settings.json")

# ========== CONFIG ==========
FOLDERS = {
    "rythem": os.path.join(BASE_DIR, "Rythem"),
    "wishing": os.path.join(BASE_DIR, "Wishing"),
    "time": os.path.join(BASE_DIR, "Time"),
    "date": os.path.join(BASE_DIR, "Date"),
    "month": os.path.join(BASE_DIR, "Month"),
    "day": os.path.join(BASE_DIR, "Day"),
    "church_name": os.path.join(BASE_DIR, "church_name"),
    "quotes": os.path.join(BASE_DIR, "Quotes"),
    "extra_quotes": os.path.join(BASE_DIR, "extra_quotes"),
    "custom_song": os.path.join(BASE_DIR, "Custom_songs"),
    "happy_songs_morning": os.path.join(BASE_DIR, "happy_songs_morning"),  # 12 AM to 12 PM
    "happy_songs_evening": os.path.join(BASE_DIR, "happy_songs_evening"),  # 12 PM to 12 AM
    "sunday_songs": os.path.join(BASE_DIR, "sunday_songs"),
    "monthly_songs": os.path.join(BASE_DIR, "monthly_songs"),
}
test_song = "Testsong.mp3"

# Quarter-hour times "00:00", "00:15" ... "23:45". The time teller, happy song and
# Sunday song times are chosen from these (the Time folder has a file for each one).
SLOT_TIMES = [f"{hour:02d}:{minute:02d}" for hour in range(24) for minute in (0, 15, 30, 45)]
MONTH_NAMES = ["January", "February", "March", "April", "May", "June",
               "July", "August", "September", "October", "November", "December"]

def save_schedules(data):
    with open(SCHEDULE_FILE, "w") as f:
        json.dump(data, f, indent=4)

def load_schedule():
    if not os.path.exists(SCHEDULE_FILE):
        return []
    with open(SCHEDULE_FILE, 'r') as f:
        return json.load(f)

print("Loading schedule...")
schedules = load_schedule()
schedule_status = {item["schedule_name"]: item["enabled"] for item in schedules}

DEFAULT_SETTINGS = {
    "teller_times": list(SLOT_TIMES),      # when the time teller runs (default: every 15 minutes)
    "happy_song_times": list(SLOT_TIMES),  # announcements followed by a happy song
    "sunday_song_times": [],           # announcements followed by a Sunday song (Sundays only)
    "sunday_silence": {"enabled": False, "start": "09:00", "end": "11:00"},  # no announcements
    "monthly_songs": {month: False for month in MONTH_NAMES},  # months that play monthly songs
    "church_name": True,
    "extra_quotes": True,
    "speaker_output": True,
    "morning_volume": 5,
    "evening_volume": 5,
}

# settings.json keys from the previous version that still mean the same thing
OLD_SETTING_KEYS = {"Speaker Output": "speaker_output",
                    "Morning Volume": "morning_volume",
                    "Evening Volume": "evening_volume"}

def load_settings():
    """Load settings.json on top of the defaults. Keys the program doesn't use are dropped."""
    loaded = copy.deepcopy(DEFAULT_SETTINGS)
    if not os.path.exists(SETTINGS_FILE):
        return loaded
    try:
        with open(SETTINGS_FILE, "r") as f:
            data = json.load(f)
    except Exception as e:
        print("⚠️ Error reading settings:", e)
        return loaded
    for old_key, new_key in OLD_SETTING_KEYS.items():
        if old_key in data and new_key not in data:
            data[new_key] = data[old_key]
    for key, value in loaded.items():
        if key not in data:
            continue
        if isinstance(value, dict):
            value.update(data[key])  # keep defaults for any missing entries
        else:
            loaded[key] = data[key]
    return loaded


def save_settings(settings):
    """Save current settings to JSON file."""
    try:
        with open(SETTINGS_FILE, "w") as f:
            json.dump(settings, f, indent=4)
        print("✅ Settings saved successfully.")
    except Exception as e:
        print("⚠️ Error saving settings:", e)

print("Loading settings...")
settings = load_settings()

MENU_ITEMS = ["Date & Time", "Teller Times", "Happy Songs", "Sunday Songs", "Sunday Silence",
              "Monthly Songs", "Church Name", "Extra Quotes", "Schedule Songs",
              "Speaker Output", "Morning Volume", "Evening Volume"]
# Menu items that open a list of quarter-hour times, and their settings key
TIME_LIST_ITEMS = {"Teller Times": "teller_times",
                   "Happy Songs": "happy_song_times",
                   "Sunday Songs": "sunday_song_times"}
# Menu items that SET turns ON/OFF, or steps 0-10
ON_OFF_ITEMS = {"Church Name": "church_name", "Extra Quotes": "extra_quotes",
                "Speaker Output": "speaker_output"}
VOLUME_ITEMS = {"Morning Volume": "morning_volume", "Evening Volume": "evening_volume"}

# os.environ["SDL_AUDIODRIVER"] = "alsa"
#os.environ["SDL_AUDIODRIVER"] = "pulseaudio"

time.sleep(60)
# Initialize pygame mixer
pygame.mixer.init()
# ============================


def auto_set_volume(settings):
    """Automatically set volume based on current time."""
    now = datetime.now()
    hour = now.hour

    # Morning: 5 AM to 5 PM
    # Evening: 5 PM to 5 AM
    if 5 <= hour < 17:
        volume_level = settings["morning_volume"]
        period = "Morning"
    else:
        volume_level = settings["evening_volume"]
        period = "Evening"

    # Convert 0-10 range to pygame 0.0-1.0 scale
    vol = max(0, min(volume_level, 10)) / 10.0
    pygame.mixer.music.set_volume(vol)
    print(f"🔊 Auto volume set to {vol*100:.0f}% ({period})")

#LCD init
# Change '0x27' to your address from i2cdetect
lcd = CharLCD('PCF8574', 0x27, port=1,
              cols=16, rows=2, dotsize=8,
              charmap='A00', auto_linebreaks=True)

def lcd_display(now):
    lcd.clear()
    lcd.write_string(now.strftime("%d-%b-%Y"))
    lcd.cursor_pos = (1, 0)  # second line
    lcd.write_string(now.strftime("%I:%M:%S %p"))
    print("Time:", now.strftime("%I:%M:%S %p"))
    print("Date:", now.strftime("%d-%b-%Y"))

def lcd_display_song(song_name):
    lcd.clear()
    lcd.write_string("playing...")
    lcd.cursor_pos = (1, 0)  # second line
    lcd.write_string(song_name)

#RTC functions
rtc = None
# Create I2C bus
def init_RTC():
    global rtc
    i2c = busio.I2C(board.SCL, board.SDA)
    
    print("Check I2C.")
    # Wait for I2C to be ready
    
    while not i2c.try_lock():
        pass
    print("I2C is locked and ready.")
    
    i2c.unlock()
    # Create RTC object
    rtc = PCF8563(i2c)
    print("RTC initiated.")


def update_RTC_time():
    # Set RTC time to system time
    rtc.datetime = datetime.now().timetuple()
    print("RTC time set.")

def get_RTC_time():
    now = rtc.datetime
    #print("RTC Time:", now)
    now = datetime(*now[:6]) 
    # print("Time:", now.strftime("%I:%M:%S %p"))
    # print("Date:", now.strftime("%d-%b-%Y"))
    return now

def set_RTC_time(dt):
    rtc.datetime = dt.timetuple()
    print("RTC time updated.")

#wifi check
def is_connected():
    try:
        # Try to connect to an Internet host on port 53 (DNS)
        socket.create_connection(("8.8.8.8", 53), timeout=3)
        return True
    except OSError:
        return False

def keypad_init():
    GPIO.setmode(GPIO.BCM)
    # Setup rows as outputs
    for row in ROWS:
        GPIO.setup(row, GPIO.OUT)
        GPIO.output(row, GPIO.HIGH)

    # Setup cols as inputs with pull-ups
    for col in COLS:
        GPIO.setup(col, GPIO.IN, pull_up_down=GPIO.PUD_UP)

def read_keypad():
    for i, row in enumerate(ROWS):
        GPIO.output(row, GPIO.LOW)
        for j, col in enumerate(COLS):
            if GPIO.input(col) == GPIO.LOW:  # Button pressed
                GPIO.output(row, GPIO.HIGH)
                return KEYS[i][j]
        GPIO.output(row, GPIO.HIGH)
    return None

def keypad_loop():
    try:
        print("Press any button on the 2x2 keypad...")
        while True:
            key = read_keypad()
                # if SET button long press → enter edit mode
            if key==SET:
                print(f"Button {key} pressed!")
                t0 = time.time()
                while key==SET:
                    time.sleep(0.1)
                    key = read_keypad()
                if time.time() - t0 > 1:
                    settings_menu()
                    lcd.cursor_mode = "hide"
                    now = get_RTC_time()
                    lcd_display(now)
            elif key:
                print(f"Button {key} pressed!")
                time.sleep(0.3)  # debounce

            time.sleep(0.05)
    except KeyboardInterrupt:
        GPIO.cleanup()
        print("Exiting...")

# -------------------
# LCD Helper
# -------------------
def show_time(dt, pos):
    lcd.clear()
    lcd.write_string(dt.strftime("%d-%m-%Y"))
    lcd.crlf()
    lcd.write_string(dt.strftime("%H:%M:%S"))

    # underline current position (for editing)
    if pos < 3:
        line, col = 0, [0, 3, 6][pos]
    else:
        line, col = 1, [0, 3, 6][pos - 3]
    lcd.cursor_pos = (line, col)
    lcd.cursor_mode = "blink"

# -------------------
# Time setting menu
# -------------------
def edit_time():
    dt = get_RTC_time()
    fields = ['day', 'month', 'year', 'hour', 'minute', 'second']
    pos = 0
    editing = True
    show_time(dt, pos)
    time.sleep(0.5)
    while editing:
        key = read_keypad()
        if key==RIGHT:  # move right
            pos = (pos + 1) % len(fields)
            show_time(dt, pos)
            time.sleep(0.3)

        elif key==LEFT:  # move left
            pos = (pos - 1) % len(fields)
            show_time(dt, pos)
            time.sleep(0.3)

        elif key==SET:  # increment current field
            year, month, day = dt.year, dt.month, dt.day
            show_time(dt, pos)
            if fields[pos] == 'day':
                # Get max days in current month
                print("Adjusting day...")
                time.sleep(0.5)
                inside_adjustment=True
                value = 0
                previous_value = 0
                while inside_adjustment:
                    key = read_keypad()
                    if key==RIGHT:  # move right
                        value+=1
                        time.sleep(0.3)
                    elif key==LEFT:  # move left
                        value-=1
                        time.sleep(0.3)
                    elif key==SET: # adjust day
                        inside_adjustment=False
                        time.sleep(0.3)
                    elif key==BACK:  # exit adjustment
                        inside_adjustment=False
                        time.sleep(0.3)
                    if value != previous_value:
                        previous_value = value
                        max_day = monthrange(year, month)[1]
                        new_day = (day - 1 + value) % max_day + 1  # wrap within 1..max_day
                        dt = dt.replace(day=new_day)
                        show_time(dt, pos)
                    time.sleep(0.1)

            elif fields[pos] == 'month':
                print("Adjusting month...")
                time.sleep(0.5)
                inside_adjustment=True
                value = 0
                previous_value = 0
                while inside_adjustment:
                    key = read_keypad()
                    if key==RIGHT:  # move right
                        value+=1
                        time.sleep(0.3)
                    elif key==LEFT:  # move left
                        value-=1
                        time.sleep(0.3)
                    elif key==SET: # adjust day
                        inside_adjustment=False
                        time.sleep(0.3)
                    elif key==BACK:  # exit adjustment
                        inside_adjustment=False
                        time.sleep(0.3)
                    if value != previous_value:
                        previous_value = value
                        new_month = (month - 1 + value) % 12 + 1  # wrap within 1..12
                        # Adjust day if current day > new month's max days
                        max_day = monthrange(year, new_month)[1]
                        new_day = min(day, max_day)
                        dt = dt.replace(month=new_month, day=new_day)
                        show_time(dt, pos)
                    time.sleep(0.1)
                

            elif fields[pos] == 'year':
                print("Adjusting year...")
                time.sleep(0.5)
                inside_adjustment=True
                value = 0
                previous_value = 0
                while inside_adjustment:
                    key = read_keypad()
                    if key==RIGHT:  # move right
                        value+=1
                        time.sleep(0.3)
                    elif key==LEFT:  # move left
                        value-=1
                        time.sleep(0.3)
                    elif key==SET: # adjust day
                        inside_adjustment=False
                        time.sleep(0.3)
                    elif key==BACK:  # exit adjustment
                        inside_adjustment=False
                        time.sleep(0.3)
                    if value != previous_value:
                        previous_value = value
                        # Adjust day if 29-Feb lands on a non-leap year
                        max_day = monthrange(year + value, month)[1]
                        dt = dt.replace(year=year + value, day=min(day, max_day))
                        show_time(dt, pos)
                    time.sleep(0.1)

            elif fields[pos] == 'hour':
                print("Adjusting hour...")
                time.sleep(0.5)
                inside_adjustment=True
                while inside_adjustment:
                    value = 0
                    key = read_keypad()
                    if key==RIGHT:  # move right
                        value+=1
                        time.sleep(0.3)
                    elif key==LEFT:  # move left
                        value-=1
                        time.sleep(0.3)
                    elif key==SET: # adjust day
                        inside_adjustment=False
                        time.sleep(0.3)
                    elif key==BACK:  # exit adjustment
                        inside_adjustment=False
                        time.sleep(0.3)
                    if value != 0:
                        dt = dt.replace(hour=(dt.hour + value) % 24)
                        show_time(dt, pos)
                    time.sleep(0.1)

            elif fields[pos] == 'minute':
                # Get max days in current month
                print("Adjusting minute...")
                time.sleep(0.5)
                inside_adjustment=True
                while inside_adjustment:
                    value = 0
                    key = read_keypad()
                    if key==RIGHT:  # move right
                        value+=1
                        time.sleep(0.3)
                    elif key==LEFT:  # move left
                        value-=1
                        time.sleep(0.3)
                    elif key==SET: # adjust day
                        inside_adjustment=False
                        time.sleep(0.3)
                    elif key==BACK:  # exit adjustment
                        inside_adjustment=False
                        time.sleep(0.3)
                    if value != 0:
                        dt = dt.replace(minute=(dt.minute + value) % 60)
                        show_time(dt, pos)
                    time.sleep(0.1)

            elif fields[pos] == 'second':
                # Get max days in current month
                print("Adjusting day...")
                time.sleep(0.5)
                inside_adjustment=True
                while inside_adjustment:
                    value = 0
                    key = read_keypad()
                    if key==RIGHT:  # move right
                        value+=1
                        time.sleep(0.3)
                    elif key==LEFT:  # move left
                        value-=1
                        time.sleep(0.3)
                    elif key==SET: # adjust day
                        inside_adjustment=False
                        time.sleep(0.3)
                    elif key==BACK:  # exit adjustment
                        inside_adjustment=False
                        time.sleep(0.3)
                    if value != 0:
                        dt = dt.replace(second=(dt.second + value) % 60)
                        show_time(dt, pos)
                    time.sleep(0.1)

            time.sleep(0.3)

        elif key==BACK:  # save & exit
            set_RTC_time(dt)
            lcd.clear()
            lcd.write_string("RTC Updated!")
            lcd.crlf()
            lcd.write_string("Back to Main")
            time.sleep(1)
            editing = False

        time.sleep(0.1)

# --------------------------
# Menu helpers
# --------------------------
def lcd_show(line1, line2=""):
    lcd.clear()
    lcd.write_string(line1[:16])
    if line2:
        lcd.crlf()
        lcd.write_string(line2[:16])

_held_key = None
_held_since = 0.0

def key_held_seconds(key):
    """Seconds the same key has been held down. Call it on every loop with the key just read."""
    global _held_key, _held_since
    now = time.time()
    if key != _held_key:
        _held_key, _held_since = key, now
    return now - _held_since

def time_label(hhmm):
    """'21:15' -> '09:15 PM'"""
    return datetime.strptime(hhmm, "%H:%M").strftime("%I:%M %p")

def slot_index(hhmm):
    """'09:15' -> 37, its position in SLOT_TIMES"""
    hour, minute = map(int, hhmm.split(":"))
    return (hour * 60 + minute) // 15

# --------------------------
# Time list submenu (Teller Times, Happy Songs, Sunday Songs)
# --------------------------
BULK_ACTIONS = ["All ON", "All OFF", "Every hour"]

def apply_bulk_action(setting_key, action):
    if action == "All ON":
        settings[setting_key] = list(SLOT_TIMES)
    elif action == "All OFF":
        settings[setting_key] = []
    else:  # Every hour: only the :00 times
        settings[setting_key] = [t for t in SLOT_TIMES if t.endswith(":00")]

def time_list_display(setting_key, index, confirm):
    if index < len(BULK_ACTIONS):
        lcd_show("> " + BULK_ACTIONS[index], "Set again = OK" if confirm else "Tap Set to apply")
        return
    slot = SLOT_TIMES[index - len(BULK_ACTIONS)]
    on = slot in settings[setting_key]
    if on and setting_key != "teller_times" and slot not in settings["teller_times"]:
        status = "ON (Teller OFF)"  # a song only plays when the time teller runs
    else:
        status = "Status: " + ("ON" if on else "OFF")
    lcd_show("> " + time_label(slot), status)

def time_list_menu(setting_key):
    """Bulk actions, then the 96 quarter-hour times. SET turns the shown time ON/OFF."""
    entries = len(BULK_ACTIONS) + len(SLOT_TIMES)
    now = get_RTC_time()
    index = len(BULK_ACTIONS) + slot_index(now.strftime("%H:%M"))  # start at the current time
    confirm = False
    time_list_display(setting_key, index, confirm)
    time.sleep(0.5)
    while True:
        key = read_keypad()
        held = key_held_seconds(key)
        if key==RIGHT or key==LEFT:
            step = 4 if held > 2 else 1  # after 2 s, move an hour at a time
            index = (index + (step if key == RIGHT else -step)) % entries
            confirm = False
            time_list_display(setting_key, index, confirm)
            time.sleep(0.3)
        elif key==SET:
            if index < len(BULK_ACTIONS):
                if confirm:  # second SET: apply
                    apply_bulk_action(setting_key, BULK_ACTIONS[index])
                    lcd_show("Done")
                    time.sleep(1)
                confirm = not confirm
            else:
                slot = SLOT_TIMES[index - len(BULK_ACTIONS)]
                if slot in settings[setting_key]:
                    settings[setting_key].remove(slot)
                else:
                    settings[setting_key] = sorted(settings[setting_key] + [slot])
            time_list_display(setting_key, index, confirm)
            time.sleep(0.3)
        elif key==BACK:
            save_settings(settings)
            lcd_show("Setting Saved", "Back to Main")
            time.sleep(1)
            return

        time.sleep(0.1)

# --------------------------
# Sunday Silence submenu
# --------------------------
def silence_display(index, adjusting):
    silence = settings["sunday_silence"]
    if index == 0:
        lcd_show("> Silence", "Status: " + ("ON" if silence["enabled"] else "OFF"))
    else:
        field = "start" if index == 1 else "end"
        title = "> Start time" if index == 1 else "> End time"
        lcd_show(title, time_label(silence[field]) + ("  +/-" if adjusting else ""))

def sunday_silence_menu():
    """ON/OFF, start and end of the Sunday time with no announcements."""
    silence = settings["sunday_silence"]
    index = 0  # 0 ON/OFF, 1 start time, 2 end time
    adjusting = False
    silence_display(index, adjusting)
    time.sleep(0.5)
    while True:
        key = read_keypad()
        held = key_held_seconds(key)
        if key==RIGHT or key==LEFT:
            direction = 1 if key == RIGHT else -1
            if adjusting:
                field = "start" if index == 1 else "end"
                step = 4 if held > 2 else 1  # after 2 s, move an hour at a time
                silence[field] = SLOT_TIMES[(slot_index(silence[field]) + direction * step) % len(SLOT_TIMES)]
            else:
                index = (index + direction) % 3
            silence_display(index, adjusting)
            time.sleep(0.3)
        elif key==SET:
            if index == 0:
                silence["enabled"] = not silence["enabled"]
            else:
                adjusting = not adjusting
            silence_display(index, adjusting)
            time.sleep(0.3)
        elif key==BACK:
            if adjusting:
                adjusting = False
                silence_display(index, adjusting)
                time.sleep(0.3)
            else:
                save_settings(settings)
                lcd_show("Setting Saved", "Back to Main")
                time.sleep(1)
                return

        time.sleep(0.1)

# --------------------------
# Monthly Submenu
# --------------------------
def monthly_display(index):
    month = MONTH_NAMES[index]
    status = "ON" if settings["monthly_songs"][month] else "OFF"
    lcd_show("> " + month, "Status: " + status)

def monthly_menu():
    """Months set ON play a song from monthly_songs in place of the happy or Sunday song."""
    index = get_RTC_time().month - 1  # start at the current month
    monthly_display(index)
    time.sleep(0.5)
    while True:
        key = read_keypad()
        if key==RIGHT:
            index = (index + 1) % 12
            monthly_display(index)
            time.sleep(0.3)
        elif key==LEFT:
            index = (index - 1) % 12
            monthly_display(index)
            time.sleep(0.3)
        elif key==SET:
            month = MONTH_NAMES[index]
            settings["monthly_songs"][month] = not settings["monthly_songs"][month]
            monthly_display(index)
            time.sleep(0.3)
        elif key==BACK:
            save_settings(settings)
            lcd_show("Setting Saved", "Back to Main")
            time.sleep(1)
            return

        time.sleep(0.1)


def scheduled_display(names, index):
    current_name = names[index]
    status = "ON" if schedule_status[current_name] else "OFF"
    lcd.clear()
    lcd.write_string("> " + current_name[:14])  # Fit in 16 chars
    lcd.crlf()
    lcd.write_string("Status: " + status)

def scheduled_songs_menu():
    if not schedules:
        lcd.clear()
        lcd.write_string("No schedules")
        time.sleep(1)
        return

    names = [s["schedule_name"] for s in schedules]
    index = 0
    scheduled_display(names, index)
    time.sleep(0.5)

    while True:
        key = read_keypad()
        if key==RIGHT:
            index = (index + 1) % len(names)
            scheduled_display(names, index)
            time.sleep(0.3)
        elif key==LEFT:
            index = (index - 1) % len(names)
            scheduled_display(names, index)
            time.sleep(0.3)
        elif key==SET:
            current_name = names[index]
            schedule_status[current_name] = not schedule_status[current_name]
            schedules[index]["enabled"] = schedule_status[current_name]
            scheduled_display(names, index)
            time.sleep(0.3)
        elif key==BACK:
            save_schedules(schedules)
            lcd.clear()
            lcd.write_string("Setting Saved")
            lcd.crlf()
            lcd.write_string("Back to Main")
            time.sleep(1)
            return

        time.sleep(0.1)

# --------------------------
# Settings Menu Function
# --------------------------
def settings_display(index):
    item = MENU_ITEMS[index]
    if item == "Date & Time":
        value = get_RTC_time().strftime("%H:%M %d-%m")
    elif item in ON_OFF_ITEMS:
        value = "Status: " + ("ON" if settings[ON_OFF_ITEMS[item]] else "OFF")
    elif item in VOLUME_ITEMS:
        value = "Value: " + str(settings[VOLUME_ITEMS[item]])
    else:
        value = "Tap Set to Open"
    lcd_show("> " + item, value)

def settings_menu():
    global editing

    lcd_show("Settings Menu")
    time.sleep(1)
    print("Entering Settings Menu...")

    index = 0
    lcd.cursor_mode = "hide"
    editing = True
    settings_display(index)

    while editing:
        key = read_keypad()
        if key==RIGHT or key==LEFT:
            index = (index + (1 if key == RIGHT else -1)) % len(MENU_ITEMS)
            settings_display(index)
            time.sleep(0.3)

        elif key==SET:
            item = MENU_ITEMS[index]
            if item == "Date & Time":
                edit_time()
                lcd.cursor_mode = "hide"
            elif item in TIME_LIST_ITEMS:
                time_list_menu(TIME_LIST_ITEMS[item])
            elif item == "Sunday Silence":
                sunday_silence_menu()
            elif item == "Monthly Songs":
                monthly_menu()
            elif item == "Schedule Songs":
                scheduled_songs_menu()
            elif item in ON_OFF_ITEMS:
                settings[ON_OFF_ITEMS[item]] = not settings[ON_OFF_ITEMS[item]]
            elif item in VOLUME_ITEMS:
                settings[VOLUME_ITEMS[item]] = (settings[VOLUME_ITEMS[item]] + 1) % 11  # cycle 0-10
            settings_display(index)
            time.sleep(0.3)

        elif key==BACK:
            save_settings(settings)
            set_speaker_output()
            lcd_show("Settings Saved")
            time.sleep(1)
            editing = False

        time.sleep(0.1)

def set_speaker_output():
    global speaker_output_required
    if settings["speaker_output"]:
        speaker_output_required = True
        #GPIO.output(output_pin_speaker, GPIO.HIGH)
        print("Speaker Output Enabled")
    else:
        speaker_output_required = False
        #GPIO.output(output_pin_speaker, GPIO.LOW)
        print("Speaker Output Disabled")

#Play speech programs
def read_input_and_play_song():
    if not GPIO.input(input_pin_test_sound):
        play_exact_file(FOLDERS['custom_song'],test_song)

def play_audio(file_path):
    if os.path.exists(file_path):
        if speaker_output_required:
            GPIO.output(output_pin_speaker, GPIO.HIGH)
        auto_set_volume(settings)  # Automatically adjust volume

        print(f"🎵 Playing: {file_path}")
        pygame.mixer.music.load(file_path)
        pygame.mixer.music.play()
        # pygame.mixer.music.play(loops=0, start=0.0, fade_ms=1000)
        # pygame.mixer.music.play(fade_ms=2000)  # 2-second fade-in
        while pygame.mixer.music.get_busy():
            time.sleep(0.1)
        # # Apply fade-out before stopping
        # fade_out_ms = 2000  # 2 seconds fade-out
        # pygame.mixer.music.fadeout(fade_out_ms)
        # time.sleep(fade_out_ms / 1000)
        if speaker_output_required:
            GPIO.output(output_pin_speaker, GPIO.LOW)

def play_random_from(folder):
    if not os.path.isdir(folder):
        print(f"⚠️ Folder not found, skipping: {folder}")
        return
    files = [f for f in os.listdir(folder) if f.endswith('.mp3')]
    if files:
        filepath = os.path.join(folder, random.choice(files))
        play_audio(filepath)

def play_exact_file(folder, filename):
    filepath = os.path.join(folder, filename)
    if os.path.exists(filepath):
        play_audio(filepath)

def isWindows():
    if platform.system() == 'Windows':
        return True
    else:
        return False

def get_time_filename(now):
    name = ""
    if(isWindows()):
        name = now.strftime("time-%#I %M %p.mp3")
    else:
        name = now.strftime("time-%-I %M %p.mp3")
    #print(name)
    return name

def get_date_filename(now):
    if(isWindows()):
        return now.strftime("date-%#d.mp3")
    else:
        return now.strftime("date-%-d.mp3")

def get_month_filename(now):
    month_file = "month-" + now.strftime("%B").lower() + ".mp3"
    return month_file

def get_day_filename(now):
    day_file = "day-" + now.strftime("%A").lower() + ".mp3"
    return day_file

def has_mp3(folder):
    return os.path.isdir(folder) and any(f.endswith('.mp3') for f in os.listdir(folder))

def choose_song_folder(now):
    """Folder of the song after the announcement: monthly song, else Sunday song, else happy song.
    Songs are set per quarter-hour. A folder with no .mp3 files is skipped and the next one is used."""
    slot = now.strftime("%H:%M")
    happy = slot in settings["happy_song_times"]
    sunday = now.weekday() == 6 and slot in settings["sunday_song_times"]
    monthly = (happy or sunday) and settings["monthly_songs"].get(now.strftime("%B"), False)
    candidates = []
    if monthly:
        candidates.append(FOLDERS['monthly_songs'])
    if sunday:
        candidates.append(FOLDERS['sunday_songs'])
    if happy:
        candidates.append(FOLDERS['happy_songs_morning'] if now.hour < 12 else FOLDERS['happy_songs_evening'])
    for folder in candidates:
        if has_mp3(folder):
            return folder
    return None

def is_teller_time(now):
    return now.strftime("%H:%M") in settings["teller_times"]

def in_sunday_silence(now):
    """True on Sunday between the Sunday Silence start and end times (end not included)."""
    silence = settings["sunday_silence"]
    if not silence["enabled"] or now.weekday() != 6:
        return False
    minute_of_day = now.hour * 60 + now.minute
    start = slot_index(silence["start"]) * 15
    end = slot_index(silence["end"]) * 15
    if start <= end:
        return start <= minute_of_day < end
    return minute_of_day >= start or minute_of_day < end  # a time that crosses midnight

def time_teller(now,custom_song=None, custom_folder=None):
    print(f"[{now.strftime('%Y-%m-%d %H:%M:%S')}] Playing time teller audio...")

    play_random_from(FOLDERS['rythem'])
    play_random_from(FOLDERS['wishing'])
    play_exact_file(FOLDERS['time'], get_time_filename(now))
    play_exact_file(FOLDERS['date'], get_date_filename(now))
    play_exact_file(FOLDERS['month'], get_month_filename(now))
    play_exact_file(FOLDERS['day'], get_day_filename(now))
    if settings["church_name"]:
        play_random_from(FOLDERS['church_name'])
    play_random_from(FOLDERS['quotes'])
    if settings["extra_quotes"]:
        play_random_from(FOLDERS['extra_quotes'])

    song_folder = choose_song_folder(now)
    if song_folder:
        play_random_from(song_folder)

    if custom_song:
        lcd_display_song(custom_song)
        play_exact_file(FOLDERS['custom_song'],custom_song)
    elif custom_folder:
        lcd_display_song(custom_folder)
        play_random_from(os.path.join(FOLDERS['custom_song'],custom_folder))

def should_play_custom(now, schedule):
    current_time = now.strftime("%H:%M")
    current_day = now.strftime("%A")
    current_month = now.strftime("%B")

    for entry in schedule:
        if entry["enabled"]:
            if entry["time"] == current_time:
                if "all" in entry["days"] or current_day.lower() in entry["days"]:
                    if "all" in entry["months"] or current_month.lower() in entry["months"]:
                        if "custom_song" in entry:
                            return entry["custom_song"]
                        else:
                            return True
    return False

def wait_for_next_minute():
    while True:
        now = get_RTC_time()
        if now.second == 0:
            return
        time.sleep(1)
        read_input_and_play_song()

if __name__ == "__main__":
    try:
        print("Speaker Output Setting...")
        set_speaker_output()
        print("Initing RTC...")
        init_RTC()
        if rtc:
            keypad_init()
            print("🔔 Time Teller Keypad initiated...")
            threading.Thread(target=keypad_loop).start()
            print("🔔 Time Teller with Scheduler started.")
            GPIO.output(output_pin_started, GPIO.HIGH)
            if is_connected():
                print("Wi-Fi is connected!")
                update_RTC_time()
            else:
                print("Wi-Fi is NOT connected.")
            
            #init display
            now = get_RTC_time()
            lcd_display(now)
            

            while True:
                wait_for_next_minute()  # Comment for development
                now = get_RTC_time()
                if not editing:
                    lcd_display(now)
                song_name = should_play_custom(now, schedules)
                if in_sunday_silence(now):
                    if song_name or is_teller_time(now):
                        print("Sunday Silence time, nothing played")
                elif song_name:
                    if song_name==True:
                        time_teller(now)
                    elif song_name.endswith('.mp3'):
                        time_teller(now,custom_song=song_name)
                    else:
                        time_teller(now,custom_folder=song_name)
                elif is_teller_time(now):
                # else: # development
                    time_teller(now)
                time.sleep(1)
        else:
            print("Init RTC failed")
            GPIO.output(output_pin_failed, GPIO.HIGH)

    except KeyboardInterrupt:
        lcd.clear()
        GPIO.output(output_pin_started, GPIO.LOW)
        GPIO.output(output_pin_failed, GPIO.LOW)
        GPIO.cleanup()
        print("Program Stopped")

