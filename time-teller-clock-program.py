import os
import time
import random
import json
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


MONTHLY_SONGS="Monthly Songs"
SCHEDULE_SONGS = "Schedule Songs"

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
    "quotes": os.path.join(BASE_DIR, "Quotes"),
    "custom_song": os.path.join(BASE_DIR, "Custom_songs"),
    "happy_songs": os.path.join(BASE_DIR, "happy_songs"),
}
test_song = "Testsong.mp3"

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

# monthly_songs = {
#     "January": True, "February": True, "March": True, "April": True,
#     "May": True, "June": True, "July": True, "August": True,
#     "September": True, "October": True, "November": True, "December": True
# }

def load_settings(default_settings):
    """Load settings from JSON file if it exists, else return defaults."""
    if os.path.exists(SETTINGS_FILE):
        try:
            with open(SETTINGS_FILE, "r") as f:
                data = json.load(f)
                # Merge saved data with defaults (to handle future updates)
                for key, value in default_settings.items():
                    if key not in data:
                        if key == SCHEDULE_SONGS:
                            data[key] = schedule_status
                        else:
                            data[key] = value
                return data
        except Exception as e:
            print("⚠️ Error reading settings:", e)
            return default_settings
    else:
        return default_settings


def save_settings(settings):
    """Save current settings to JSON file."""
    try:
        with open(SETTINGS_FILE, "w") as f:
            json.dump(settings, f, indent=4)
        print("✅ Settings saved successfully.")
    except Exception as e:
        print("⚠️ Error saving settings:", e)

settings = {
     MONTHLY_SONGS: {
        "January": True, "February": True, "March": True, "April": True,
        "May": True, "June": True, "July": True, "August": True,
        "September": True, "October": True, "November": True, "December": True
    },
    SCHEDULE_SONGS: schedule_status,
    "Sunday Songs": False,
    "Speaker Output": True,
    "Morning Volume": 5,
    "Evening Volume": 5,
    "Happy Song": True
}

print("Loading settings...")
settings = load_settings(settings)



# menu_items = list(settings.keys())
# menu_items.insert(0, "Date & Time")  # add RTC menu first

menu_items = ["Date & Time"] + list(settings.keys())

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
        volume_level = settings["Morning Volume"]
        period = "Morning"
    else:
        volume_level = settings["Evening Volume"]
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
                        new_day = (day % max_day) + value
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
                        new_month = (month % 12) + value
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
                        dt = dt.replace(year=year + value)
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
# Monthly Submenu
# --------------------------
def monthly_display(months, index):
    current_month = months[index]
    status = "ON" if settings[MONTHLY_SONGS][current_month] else "OFF"
    lcd.clear()
    lcd.write_string("> " + current_month[:14])
    lcd.crlf()
    lcd.write_string("Status: " + status)

def monthly_menu():
    months = list(settings[MONTHLY_SONGS].keys())
    index = 0
    monthly_display(months, index)
    time.sleep(0.5)
    while True:
        key = read_keypad()
        if key==RIGHT:
            index = (index + 1) % len(months)
            monthly_display(months, index)
            time.sleep(0.3)
        elif key==LEFT:
            index = (index - 1) % len(months)
            monthly_display(months, index)
            time.sleep(0.3)
        elif key==SET:
            current_month = months[index]
            settings[MONTHLY_SONGS][current_month] = not settings[MONTHLY_SONGS][current_month]
            monthly_display(months, index)
            time.sleep(0.3)
        elif key==BACK:
            lcd.clear()
            lcd.write_string("Back to Main")
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
    item = menu_items[index]
    lcd.clear()
    lcd.write_string("> " + item)

def settings_menu():
    global editing
    
    lcd.clear()
    lcd.write_string("Settings Menu")
    time.sleep(1)
    print("Entering Settings Menu...")

    index = 0
    lcd.cursor_mode = "hide"
    editing = True
    settings_display(index)

    now = get_RTC_time()
    settings_display(index)
    lcd.crlf()
    lcd.write_string(now.strftime("%H:%M %d-%m"))

    item = menu_items[index]
    while editing:
        # Display value or status

        # Button Controls
        key = read_keypad()
        if key==RIGHT:
            index = (index + 1) % len(menu_items)
            item = menu_items[index]
            if item == "Date & Time":
                now = get_RTC_time()
                settings_display(index)
                lcd.crlf()
                lcd.write_string(now.strftime("%H:%M %d-%m"))
            elif item == MONTHLY_SONGS:
                settings_display(index)
                lcd.crlf()
                lcd.write_string("Tap Set to Open")
            elif item == SCHEDULE_SONGS:
                settings_display(index)
                lcd.crlf()
                lcd.write_string("Tap Set to Open")
            else:
                val = settings[item]
                settings_display(index)
                lcd.crlf()
                if isinstance(val, bool):
                    lcd.write_string("Status: " + ("ON" if val else "OFF"))
                else:
                    lcd.write_string("Value: " + str(val))

            time.sleep(0.3)

        elif key==LEFT:
            index = (index - 1) % len(menu_items)
            item = menu_items[index]
            if item == "Date & Time":
                now = get_RTC_time()
                settings_display(index)
                lcd.crlf()
                lcd.write_string(now.strftime("%H:%M %d-%m"))
            elif item == MONTHLY_SONGS:
                settings_display(index)
                lcd.crlf()
                lcd.write_string("Tap Set to Open")
            elif item == SCHEDULE_SONGS:
                settings_display(index)
                lcd.crlf()
                lcd.write_string("Tap Set to Open")
            else:
                val = settings[item]
                settings_display(index)
                lcd.crlf()
                if isinstance(val, bool):
                    lcd.write_string("Status: " + ("ON" if val else "OFF"))
                else:
                    lcd.write_string("Value: " + str(val))

            time.sleep(0.3)

        elif key==SET:
            # Perform action
            if item == "Date & Time":
                edit_time()
                now = get_RTC_time()
                settings_display(index)
                lcd.crlf()
                lcd.write_string(now.strftime("%H:%M %d-%m"))
                # editing = False
            elif item == MONTHLY_SONGS:
                monthly_menu()
                settings_display(index)
                lcd.crlf()
                lcd.write_string("Tap Set to Open")
            elif item == SCHEDULE_SONGS:
                scheduled_songs_menu()
                settings_display(index)
                lcd.crlf()
                lcd.write_string("Tap Set to Open")
            elif isinstance(settings[item], bool):
                settings[item] = not settings[item]
                val = settings[item]
                settings_display(index)
                lcd.crlf()
                lcd.write_string("Status: " + ("ON" if val else "OFF"))
            else:
                settings[item] = (settings[item] + 1) % 11  # Example: cycle 0-10
                val = settings[item]
                settings_display(index)
                lcd.crlf()
                lcd.write_string("Value: " + str(val))
            # settings_display(index)
            time.sleep(0.3)

        elif key==BACK:
            save_settings(settings)
            set_speaker_output()
            lcd.clear()
            lcd.write_string("Settings Saved")
            time.sleep(1)
            editing = False

        time.sleep(0.1)

def set_speaker_output():
    if settings["Speaker Output"]:
        GPIO.output(output_pin_speaker, GPIO.HIGH)
        print("Speaker Output Enabled")
    else:
        GPIO.output(output_pin_speaker, GPIO.LOW)
        print("Speaker Output Disabled")

#Play speech programs
def read_input_and_play_song():
    if not GPIO.input(input_pin_test_sound):
        play_exact_file(FOLDERS['custom_song'],test_song)

def play_audio(file_path):
    if os.path.exists(file_path):
        
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

def play_random_from(folder):
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

def time_teller(now,custom_song=None, custom_folder=None):
    print(f"[{now.strftime('%Y-%m-%d %H:%M:%S')}] Playing time teller audio...")

    play_random_from(FOLDERS['rythem'])
    play_random_from(FOLDERS['wishing'])
    play_exact_file(FOLDERS['time'], get_time_filename(now))
    play_exact_file(FOLDERS['date'], get_date_filename(now))
    play_exact_file(FOLDERS['month'], get_month_filename(now))
    play_exact_file(FOLDERS['day'], get_day_filename(now))
    play_random_from(FOLDERS['quotes'])
    if custom_song:
        lcd_display_song(custom_song)
        play_exact_file(FOLDERS['custom_song'],custom_song)
    if custom_folder:
        lcd_display_song(custom_folder)
        play_random_from(os.join.path(FOLDERS['custom_song'],custom_folder))
    else:
        play_random_from(FOLDERS['happy_songs'])

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
                            True
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
                if song_name:
                    if song_name==True:
                        time_teller(now)
                    elif song_name.endswith('.mp3'):
                        time_teller(now,custom_song=song_name)
                    else:
                        time_teller(now,custom_folder=song_name)
                elif now.minute % 15 == 0:
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

