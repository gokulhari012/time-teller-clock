import time
import board
import busio
from adafruit_pcf8563.pcf8563 import PCF8563
import datetime

# Create I2C bus
i2c = busio.I2C(board.SCL, board.SDA)

print("Check I2C.")
# Wait for I2C to be ready
while not i2c.try_lock():
    pass

print("I2C is locked and ready.")

i2c.unlock()

# Create RTC object
rtc = PCF8563(i2c)

# Set RTC time to system time
#rtc.datetime = datetime.datetime.now().timetuple()
print("RTC time set.")

# Loop print time
while True:
    now = rtc.datetime
    print("RTC Time:", now)
    now = datetime.datetime(*now[:6]) 
    print("Time:", now.strftime("%I:%M:%S %p"))
    print("Date:", now.strftime("%d-%b-%Y"))
    time.sleep(1)
