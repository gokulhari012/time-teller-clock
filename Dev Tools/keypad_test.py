
import RPi.GPIO as GPIO
import time

# --- Pin definitions ---
ROWS = [4, 17]
COLS = [27, 22]

# Button map layout
KEYS = [
    ['1', '2'],
    ['3', '4']
]

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

try:
    print("Press any button on the 2x2 keypad...")
    while True:
        key = read_keypad()
        if key:
            print(f"Button {key} pressed!")
            time.sleep(0.3)  # debounce
        time.sleep(0.05)
except KeyboardInterrupt:
    GPIO.cleanup()
    print("Exiting...")
