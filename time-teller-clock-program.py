import os
import time
import random
import json
from datetime import datetime
import pygame
import platform

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
}

# Initialize pygame mixer
pygame.mixer.init()
# ============================

def play_audio(file_path):
    if os.path.exists(file_path):
        print(f"🎵 Playing: {file_path}")
        pygame.mixer.music.load(file_path)
        pygame.mixer.music.play()
        while pygame.mixer.music.get_busy():
            time.sleep(0.1)

def play_random_from(folder):
    files = [f for f in os.listdir(folder) if f.endswith('.mp4')]
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

def get_time_filename():
    now = datetime.now()
    if(isWindows()):
        return now.strftime("time-%#I:%M %p.mp3")
    else:
        return now.strftime("time-%-I:%M %p.mp3")

def get_date_filename():
    now = datetime.now()
    if(isWindows()):
        return now.strftime("date-%#d.mp3")
    else:
        return now.strftime("date-%-d.mp3")

def get_month_filename():
    now = datetime.now()
    if(isWindows()):
        return now.strftime("month-%B.mp3")
    else:
        return now.strftime("month-%B.mp3")

def get_day_filename():
    now = datetime.now()
    if(isWindows()):
        return now.strftime("day-%A.mp3")
    else:
        return now.strftime("day-%A.mp3")

def time_teller():
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Playing time teller audio...")
    play_random_from(FOLDERS['rythem'])
    play_exact_file(FOLDERS['time'], get_time_filename())
    play_exact_file(FOLDERS['date'], get_date_filename())
    play_exact_file(FOLDERS['month'], get_month_filename())
    play_exact_file(FOLDERS['day'], get_day_filename())
    play_random_from(FOLDERS['quotes'])

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
            if "All" in entry["days"] or current_day in entry["days"]:
                if "All" in entry["months"] or current_month in entry["months"]:
                    return True
    return False

def wait_for_next_minute():
    while True:
        now = datetime.now()
        if now.second == 0:
            return
        time.sleep(1)

if __name__ == "__main__":
    print("🔔 Time Teller with Scheduler started.")

    schedule_data = load_schedule()

    while True:
        wait_for_next_minute()
        now = datetime.now()

        if should_play_custom(now, schedule_data):
            time_teller()
        elif now.minute % 15 == 0:
            time_teller()

        time.sleep(60)

