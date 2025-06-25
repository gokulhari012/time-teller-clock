import os
import time
import random
from datetime import datetime
from playsound import playsound

# Folder paths
BASE_DIR = "your_project_directory"  # Change this
FOLDERS = {
    "rythem": os.path.join(BASE_DIR, "Rythem"),
    "time": os.path.join(BASE_DIR, "Time"),
    "date": os.path.join(BASE_DIR, "Date"),
    "day": os.path.join(BASE_DIR, "Day"),
    "quotes": os.path.join(BASE_DIR, "Quotes"),
}

def play_random_from(folder):
    files = [f for f in os.listdir(folder) if f.endswith('.mp4')]
    if files:
        filepath = os.path.join(folder, random.choice(files))
        playsound(filepath)

def play_exact_file(folder, filename):
    filepath = os.path.join(folder, filename)
    if os.path.exists(filepath):
        playsound(filepath)

def get_time_filename():
    now = datetime.now()
    return now.strftime("%-I:%M %p voice speech.mp4")  # eg: "6:00 AM voice speech.mp4"

def get_date_filename():
    now = datetime.now()
    return now.strftime("%-d %B.mp4")  # eg: "25 June.mp4"

def get_day_filename():
    now = datetime.now()
    return now.strftime("%A.mp4")  # eg: "Tuesday.mp4"

def time_teller():
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Playing time teller audio...")

    # Play rhythm
    play_random_from(FOLDERS['rythem'])

    # Play time
    play_exact_file(FOLDERS['time'], get_time_filename())

    # Play date
    play_exact_file(FOLDERS['date'], get_date_filename())

    # Play day
    play_exact_file(FOLDERS['day'], get_day_filename())

    # Play random quote
    play_random_from(FOLDERS['quotes'])

def wait_for_next_quarter():
    while True:
        now = datetime.now()
        if now.minute % 15 == 0 and now.second == 0:
            return
        time.sleep(1)

if __name__ == "__main__":
    print("🔔 Time Teller started. Will play every 15 minutes.")
    while True:
        wait_for_next_quarter()
        time_teller()
        time.sleep(60)  # Wait 1 minute to avoid replaying the same minute
