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


#GPio config
GPIO.setmode(GPIO.BCM)
# Example pins
output_pin_started = 14  # GPIO17 (pin 11 on header)
output_pin_failed = 15  # GPIO17 (pin 11 on header)
input_pin_test_sound = 18   # GPIO27 (pin 13 on header)

# Setup pins
GPIO.setup(output_pin_started, GPIO.OUT)
GPIO.setup(output_pin_failed, GPIO.OUT)
GPIO.setup(input_pin_test_sound, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.output(output_pin_started, GPIO.LOW)
GPIO.output(output_pin_failed, GPIO.LOW)


# ========== CONFIG ==========
BASE_DIR = "Audio-files"  # Change this
SCHEDULE_FILE = os.path.join(BASE_DIR, "schedule.json")
FOLDERS = {
    "rythem": os.path.join(BASE_DIR, "Rythem"),
    "time": os.path.join(BASE_DIR, "Time"),
    "date": os.path.join(BASE_DIR, "Date"),
    "month": os.path.join(BASE_DIR, "Month"),
    "day": os.path.join(BASE_DIR, "Day"),
    "quotes": os.path.join(BASE_DIR, "Quotes"),
    "custom_song": os.path.join(BASE_DIR, "Custom_songs"),
}
test_song = "Testsong.mp3"

# Initialize pygame mixer
pygame.mixer.init()
# ============================

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

#wifi check
def is_connected():
    try:
        # Try to connect to an Internet host on port 53 (DNS)
        socket.create_connection(("8.8.8.8", 53), timeout=3)
        return True
    except OSError:
        return False

#Play speech programs
def read_input_and_play_song():
    if not GPIO.input(input_pin_test_sound):
        play_exact_file(FOLDERS['custom_song'],test_song)

def play_audio(file_path):
    if os.path.exists(file_path):
        print(f"🎵 Playing: {file_path}")
        pygame.mixer.music.load(file_path)
        pygame.mixer.music.play()
        while pygame.mixer.music.get_busy():
            time.sleep(0.1)

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

def time_teller(now,custom_song=None):
    print(f"[{now.strftime('%Y-%m-%d %H:%M:%S')}] Playing time teller audio...")
    play_random_from(FOLDERS['rythem'])
    play_exact_file(FOLDERS['time'], get_time_filename(now))
    play_exact_file(FOLDERS['date'], get_date_filename(now))
    play_exact_file(FOLDERS['month'], get_month_filename(now))
    play_exact_file(FOLDERS['day'], get_day_filename(now))
    play_random_from(FOLDERS['quotes'])
    if custom_song:
        play_exact_file(FOLDERS['custom_song'],custom_song)

def load_schedule():
    if not os.path.exists(SCHEDULE_FILE):
        return []
    with open(SCHEDULE_FILE, 'r') as f:
        return json.load(f)

def should_play_custom(now, schedule):
    current_time = now.strftime("%H:%M")
    current_day = now.strftime("%A")
    current_month = now.strftime("%B")

    for entry in schedule:
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
        print("Initing RTC...")
        init_RTC()
        if rtc:
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
            
            schedule_data = load_schedule()

            while True:
                wait_for_next_minute()
                now = get_RTC_time()
                lcd_display(now)
                song_name = should_play_custom(now, schedule_data)
                if song_name:
                    if song_name==True:
                        time_teller(now)
                    else:
                        time_teller(now,custom_song=song_name)
                elif now.minute % 15 == 0:
                    time_teller(now)
                time.sleep(1)
        else:
            print("Init RTC failed")
            GPIO.output(output_pin_failed, GPIO.HIGH)

    except KeyboardInterrupt:
        lcd.clear()
        GPIO.output(output_pin_started, GPIO.LOW)
        GPIO.output(output_pin_failed, GPIO.LOW)
        print("Program Stopped")
