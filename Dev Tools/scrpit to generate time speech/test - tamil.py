from gtts import gTTS

text = "ஞாயிறு"
text += " கிலலம்"

tts = gTTS(text, lang='ta')
tts.save("day-sunday.mp3")
