# Time Teller Clock — ESP32-C3 version

This folder has the same church time-teller clock as the Raspberry Pi program, rebuilt for an **ESP32-C3** with an **MP3-TF-16P** MP3 module (a DFPlayer Mini compatible board with an SD card slot).

| File | What it is |
|------|------------|
| [esp32-time-teller-clock.ino](esp32-time-teller-clock.ino) | The Arduino program |
| [prepare_sd_card.py](prepare_sd_card.py) | Copies `Audio-files/` onto the SD card in the numbered layout the MP3 module needs |

The announcements, schedules, LCD menu and buttons work the same way as the Pi version. The main [README](../README.md) describes them.

---

## The main difference: files are played by number

The MP3-TF-16P reads the SD card by itself. The ESP32 can only send it commands such as "play file 26 in folder 03" and ask "how many files are in folder 07?". This has two consequences:

- **The ESP32 cannot read file names or write to the SD card.** The card must use numbered folders (`01`–`99`) and numbered files (`001.mp3`–`255.mp3`). `prepare_sd_card.py` builds this layout for you.
- **Settings and schedules can't be stored on the SD card.** Settings are saved in the ESP32's own flash memory. Schedules are written in the sketch, in place of `schedule.json`.

### Other changes from the Pi version

| | Raspberry Pi | ESP32-C3 |
|---|---|---|
| Audio | pygame plays files by name | MP3 module plays numbered files |
| Schedules | `schedule.json` | `SCHEDULES` list in the sketch |
| Settings | `settings.json` | ESP32 flash, kept across power cuts |
| Buttons | 2×2 matrix (4 buttons on 4 pins) | 4 separate buttons, each on its own pin |
| Status LEDs | "Started" and "Failed" | One LED: steady = running, blinking = problem |
| Speaker relay | Switches on and off around every clip | Stays on for the whole announcement (no clicks between clips) |
| LCD clock | Updates once a minute | Updates every second |
| Start-up time | About 70 s | A few seconds (plus Wi-Fi sync, if set) |
| Internet time | Copied when the internet is reachable | Copied only after a real NTP answer |
| Morning/evening volume | Uses the system clock | Uses the RTC |

Everything else behaves the same: the announcement order, the song rules (monthly, Sunday and happy songs), Teller Times, Sunday Silence and the LCD menu. The main README describes them under [How an announcement works](../README.md#how-an-announcement-works) and [Using the clock](../README.md#using-the-clock-front-panel).

> **Updating from the first ESP32 version:** the SD card folder numbers have changed. Build the card again with `prepare_sd_card.py --overwrite`. The new menu settings start at their defaults, which match the old behavior: an announcement and a happy song every 15 minutes.

---

## Parts

| Part | Notes |
|------|-------|
| ESP32-C3 board | One whose USB socket connects straight to the chip, such as the ESP32-C3 SuperMini or Seeed XIAO ESP32-C3. Avoid boards with a separate USB-to-serial chip (for example ESP32-C3-DevKitM-1): it is wired to GPIO 20/21, which this project uses for the TEST button and LED. |
| MP3-TF-16P module + micro-SD card | FAT32, 32 GB or smaller |
| 16×2 I²C LCD (PCF8574 backpack) | Address `0x27` (sometimes `0x3F`) |
| PCF8563 RTC module with coin cell | The same RTC as the Pi version |
| 5 push buttons | LEFT, RIGHT, BACK, SET, TEST |
| Relay module *(optional)* | Switches the amplifier or speaker power |
| 1 LED + 330 Ω resistor | Status LED |
| 1 kΩ resistor | Between the ESP32 TX pin and the MP3 module RX pin |
| 100–470 µF capacitor | Across the MP3 module's VCC and GND; reduces hum and resets |
| Amplifier + speakers, or a small 3 W speaker | See [Audio output](#audio-output) |
| 5 V power supply | Powers the ESP32 board, the MP3 module and the relay |

---

## Wiring

All the pin numbers are set at the top of the sketch, so you can change them.

| Connection | ESP32-C3 pin | Notes |
|------------|--------------|-------|
| LCD + RTC SDA | GPIO 8 | Shared I²C bus |
| LCD + RTC SCL | GPIO 9 | Shared I²C bus |
| MP3 module **TX** | GPIO 5 (ESP32 RX) | |
| MP3 module **RX** | GPIO 6 (ESP32 TX) | **Through a 1 kΩ resistor** |
| MP3 module **BUSY** | GPIO 7 | LOW while playing; tells the ESP32 when a clip ends |
| LEFT button | GPIO 0 | Connect each button between its pin and GND |
| RIGHT button | GPIO 1 | |
| BACK button | GPIO 3 | |
| SET button | GPIO 4 | |
| TEST button | GPIO 20 | Plays the test song |
| Relay IN | GPIO 10 | HIGH = on. Set `RELAY_ON_LEVEL` to `LOW` for active-low relay boards. |
| Status LED (+ 330 Ω) | GPIO 21 | Other end to GND |
| MP3 module VCC | 5 V | |
| RTC VCC | 3.3 V | |
| All GNDs | GND | Everything must share ground |

GPIO 2 is left free on purpose. It is a boot "strapping" pin, like GPIO 8 and GPIO 9. GPIO 8 and 9 are safe for I²C because the bus pull-up resistors hold them HIGH, which is what the chip needs at boot.

> **The LCD and 5 V.** ESP32-C3 pins are **not 5 V tolerant**. Most PCF8574 LCD backpacks run at 5 V and pull SDA/SCL up to 5 V. Either put an I²C level shifter between the backpack and the ESP32, or use a 3.3 V LCD.

### Audio output

The MP3-TF-16P has two kinds of output:

- **SPK_1 / SPK_2**: a built-in 3 W amplifier. Connect one small speaker directly.
- **DAC_R / DAC_L + GND**: line-level output. Connect these to the AUX input of the church amplifier. This replaces the Pi's audio jack.

---

## Preparing the SD card

1. Format the micro-SD card as **FAT32**.
2. Put your audio into the Pi project's `Audio-files/` folders as usual (see the main README).
3. Run the script from this folder. For example, if the SD card is drive `E:`:
   ```bash
   python prepare_sd_card.py ../Audio-files E:\
   ```
   If you run it with no folders given, it uses `DEFAULT_SOURCE` and `DEFAULT_DEST` from the top of the script. The script needs Python 3 and no extra packages. You can also copy to an empty folder first and move it to the card later.
4. Read the printed list, which is also saved on the card as `track-list.txt`. It shows the number each file received, and it warns about missing or wrongly named files.

To rebuild a card that already has numbered folders, add `--overwrite`. This deletes the folders `01`–`99` on the card before copying.

### SD card layout

| Folder | Contents | How the clock picks a file |
|--------|----------|----------------------------|
| `01` | Rythem | Random |
| `02` | Wishing | Random |
| `03` | Time: `001` = 12:00 AM, `002` = 12:15 AM … `026` = 6:15 AM … `096` = 11:45 PM | hour × 4 + minute ÷ 15 + 1 |
| `04` | Date: `001`–`031` | Day of the month |
| `05` | Month: `001` = January … `012` = December | Month number |
| `06` | Day: `001` = Sunday … `007` = Saturday | Weekday |
| `07` | church_name | Random (if *Church Name* is ON) |
| `08` | Quotes | Random |
| `09` | extra_quotes | Random (if *Extra Quotes* is ON) |
| `10` | happy_songs_morning | Random, for songs from 12 AM to 12 PM |
| `11` | happy_songs_evening | Random, for songs from 12 PM to 12 AM |
| `12` | sunday_songs | Random |
| `13` | monthly_songs | Random |
| `14` | `Custom_songs/*.mp3` in name order, except the test song | Number given in a schedule |
| `15` | `001` = `Custom_songs/Testsong.mp3` | Test button |
| `20`–`99` | One folder per `Custom_songs/` sub-folder, in name order | Random, for a folder schedule |

Folders 16–19 are unused, so new fixed folders can be added later without renumbering the custom folders.

At startup the clock asks the module how many files are in each random folder. Any folder with no files is listed on the LCD (`No files in N:` followed by the folder numbers) and then skipped when playing. If you add songs later, restart the clock.

If you use a Mac to copy the files, it adds hidden `._` files that the module counts as tracks. Run `dot_clean /Volumes/<card>` before ejecting.

---

## Arduino IDE setup

1. Install the **esp32** board package by Espressif: *Boards Manager* → search "esp32".
2. Install these libraries from the *Library Manager*:
   - **LiquidCrystal I2C** by Frank de Brabander
   - **RTClib** by Adafruit (it also installs *Adafruit BusIO*)
   - **DFRobotDFPlayerMini** by DFRobot
3. Choose your board, for example *ESP32C3 Dev Module* or *Nologo ESP32C3 Super Mini*.
4. Make sure **Tools → USB CDC On Boot** is **Enabled**. This sends the program's `Serial` messages over the USB cable to the Serial Monitor. When it is Disabled, those messages go out on GPIO 20 and 21 instead, which are the TEST button and LED pins. The *Super Mini* and *XIAO* board choices already default to Enabled; *ESP32C3 Dev Module* defaults to Disabled.
5. Open `esp32-time-teller-clock.ino`, edit the CONFIG section, and upload.
6. Open the Serial Monitor at 115200 baud to watch the startup messages.

I compiled the sketch with esp32 core 3.3.2 for both board types listed above. It uses about 77% of the flash.

---

## Configuration

Everything you're likely to change is in the **CONFIG** section at the top of the sketch.

### Wi-Fi time sync (optional)

```cpp
const char* WIFI_SSID     = "ChurchWiFi";
const char* WIFI_PASSWORD = "password";
const char* TIME_ZONE     = "IST-5:30";   // India
```

At startup, the clock connects to Wi-Fi, gets the time from the internet, writes it to the RTC, and turns Wi-Fi off again. Leave `WIFI_SSID` empty to rely only on the RTC and the *Date & Time* menu.

### Schedules

The `SCHEDULES` list replaces `schedule.json`. These are the three entries from the Pi version:

```cpp
const Schedule SCHEDULES[] = {
  // name               hh  mm  days               months                 song         no  enabled
  { "mon_tue_mornings",  6,  5, DAY_MON | DAY_TUE, MONTH_JUN | MONTH_JUL, SONG_TRACK,   1, true },
  { "sunday1",          15, 30, DAY_SUN,           ALL_MONTHS,            SONG_FOLDER, 20, true },
  { "sunday",           15, 30, DAY_SUN,           ALL_MONTHS,            SONG_NONE,    0, true },
};
```

| `schedule.json` | Sketch |
|-----------------|--------|
| `"time": "06:05"` | `6, 5` (24-hour) |
| `"days": ["monday", "tuesday"]` | `DAY_MON \| DAY_TUE`, or `ALL_DAYS` |
| `"months": ["june", "july"]` | `MONTH_JUN \| MONTH_JUL`, or `ALL_MONTHS` |
| `"custom_song": "GokulHari.mp3"` | `SONG_TRACK, 1`. The number comes from folder `14` in `track-list.txt`. |
| `"custom_song": "new_folder"` | `SONG_FOLDER, 20`. The folder number comes from `track-list.txt`. |
| no `custom_song` | `SONG_NONE, 0` (standard announcement, with the song set for that time in the menu) |
| `"enabled": true` | `true` (only the first-boot value; after that it is changed from the LCD) |

**Add new schedules at the end of the list.** The ON/OFF choices made on the LCD are saved by position, so inserting an entry in the middle shifts them.

### MP3 module options

- **`MP3_USE_ACK`**: some MP3-TF-16P clones don't reply to commands. If the LCD shows *MP3 module / not found*, set this to `false`.
- **`MANUAL_FOLDER_COUNTS`**: if startup lists a folder under *No files in* even though it has files, your module can't report how many files a folder holds. Enter the counts yourself, for example `{ FOLDER_RYTHEM, 25 },`.

### Other options

- **`LCD_ADDRESS`**: the LCD's I²C address, usually `0x27`, sometimes `0x3F`.
- **`RELAY_ON_LEVEL`**: set to `LOW` for an active-low relay board.
- **`DAILY_RESTART`**: restarts the clock at 03:55 every day, like the Pi's cron reboot. The restart is skipped if audio is playing or a menu is open.

---

## Startup screens and the status LED

| LCD | LED | Meaning |
|-----|-----|---------|
| `Time Teller` / `Starting...` | Off | Starting |
| `RTC not found` / `Check wiring` | Blinking | The RTC isn't answering on I²C. The clock stops here. |
| `MP3 module` / `not found` | Blinking afterwards | The MP3 module didn't answer. The clock keeps running without sound. See `MP3_USE_ACK`. |
| `No files in 3:` / `02 07 09` | — | These folders are empty or missing, so they are skipped. This is normal for features you don't use; otherwise see `MANUAL_FOLDER_COUNTS`. |
| `Clock was reset` / `Set date & time` | — | The RTC lost power and no internet time was available. Set the time from the menu. |
| Date and time | Steady | Running normally |

---

## Troubleshooting

| Problem | Check |
|---------|-------|
| No sound at all | Check the SD card is FAT32 and has the numbered folders. Check the TX/RX wires aren't swapped and the 1 kΩ resistor is in place. Check the volume setting isn't 0 and the relay wiring. Watch the Serial Monitor for `Playing: 03/026.mp3` lines. |
| Some clips are skipped | Serial shows `Clip did not start (missing file?)`. Compare the card with `track-list.txt`. |
| Clips cut off or overlap | Check the BUSY wire to GPIO 7. |
| Hum or clicks | Add the capacitor across the MP3 module's power. Keep the audio ground away from the relay wiring. |
| LCD blank or boxes | Check the I²C address (`LCD_ADDRESS`), the contrast potentiometer and the level shifter. |
| Wi-Fi never connects (SuperMini boards) | Some ESP32-C3 SuperMini boards have a weak antenna. Add `WiFi.setTxPower(WIFI_POWER_8_5dBm);` after `WiFi.begin(...)` in `syncTimeFromInternet()`. |
| New songs aren't played | Restart the clock so it counts the files again. |
