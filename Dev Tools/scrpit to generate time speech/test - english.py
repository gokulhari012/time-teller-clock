from gtts import gTTS

text = "sunday"

tts = gTTS(text, lang='en')
tts.save("day-sunday.mp3")
