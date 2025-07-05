from gtts import gTTS
from zipfile import ZipFile
import io
import os

# Create output directory
os.makedirs("time_audio_files", exist_ok=True)

# Generate list of times
times = []
for hour in range(24):
    for minute in [0, 15, 30, 45]:
        am_pm = "AM" if hour < 12 else "PM"
        hour12 = hour % 12
        hour12 = 12 if hour12 == 0 else hour12
        times.append(f"{hour12} {minute:02d} {am_pm}")

# Generate MP3 files
for time_text in times:
    tts = gTTS(text=time_text, lang='en')
    tts.save(f"time_audio_files/{time_text}.mp3")
    print(f"Created: {time_text}.mp3")

# # Zip all files
# with ZipFile("time_audio_files.zip", 'w') as zipf:
#     for file in os.listdir("time_audio_files"):
#         zipf.write(os.path.join("time_audio_files", file), arcname=file)

print("All files zipped into time_audio_files.zip")
