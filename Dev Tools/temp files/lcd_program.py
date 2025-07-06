import time
from datetime import datetime
from RPLCD.i2c import CharLCD

# Change '0x27' to your address from i2cdetect
lcd = CharLCD('PCF8574', 0x27, port=1,
              cols=16, rows=2, dotsize=8,
              charmap='A00', auto_linebreaks=True)

try:
    while True:
        now = datetime.now()
        lcd.clear()
        lcd.write_string(now.strftime("%d-%b-%Y"))
        lcd.cursor_pos = (1, 0)  # second line
        lcd.write_string(now.strftime("%I:%M:%S %p"))
        time.sleep(1)
except KeyboardInterrupt:
    lcd.clear()
    print("Stopped")
