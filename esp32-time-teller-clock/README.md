# Time Teller Clock — ESP32-C3 version

This folder has the same church time-teller clock as the Raspberry Pi program, rebuilt for an **ESP32-C3** with an **MP3-TF-16P** MP3 module (a DFPlayer Mini compatible board with an SD card slot).

| File | What it is |
|------|------------|
| [esp32-time-teller-clock.ino](esp32-time-teller-clock.ino) | The Arduino program |
| [web_settings.ino](web_settings.ino) | The phone settings page: Wi-Fi hotspot and web server (opens as a second tab in the Arduino IDE) |
| [web_page.h](web_page.h) | The settings page itself (HTML, CSS and JavaScript in one file) |
| [prepare_sd_card.py](prepare_sd_card.py) | Copies `Audio-files-for-esp32-new/Language_1` … `Language_3` onto the SD card in the numbered layout the MP3 module needs |

The announcements, schedules, LCD menu and buttons work the same way as the Pi version. The main [README](../README.md) describes them. On top of that, every setting can be changed from a phone. See [Setting up from a phone](#setting-up-from-a-phone).

---

## The main difference: files are played by number

The MP3-TF-16P reads the SD card by itself. The ESP32 can only send it commands such as "play file 26 in folder 03" and ask "how many files are in folder 07?". This has two consequences:

- **The ESP32 cannot read file names or write to the SD card.** The card must use numbered folders (`01`–`99`) and numbered files (`001.mp3`–`255.mp3`). `prepare_sd_card.py` builds this layout for you.
- **Settings and schedules can't be stored on the SD card.** They are saved in the ESP32's own flash memory instead, and they are kept after a power cut.

### Other changes from the Pi version

| | Raspberry Pi | ESP32-C3 |
|---|---|---|
| Audio | pygame plays files by name | MP3 module plays numbered files |
| Changing settings | LCD and keypad | LCD and buttons, **or a phone** over the clock's own Wi-Fi |
| Schedules | `schedule.json` | Added, edited and deleted on the phone; saved in ESP32 flash |
| Settings | `settings.json` | ESP32 flash, kept across power cuts |
| Buttons | 2×2 matrix (4 buttons on 4 pins) | Resistor keys (4 buttons on 2 wires), 4 separate buttons, or the same 2×2 matrix. See [Choosing the key type](#choosing-the-key-type) |
| Status LEDs | "Started" and "Failed" | One LED: steady = running, blinking = problem |
| Speaker relay | Switches on and off around every clip | Stays on for the whole announcement (no clicks between clips) |
| LCD clock | Updates once a minute | Updates every second |
| Start-up time | About 70 s | A few seconds (plus Wi-Fi sync, if set) |
| Internet time | Copied when the internet is reachable | Copied only after a real NTP answer |
| Morning/evening volume | Uses the system clock; switches at 5 AM and 5 PM | Uses the RTC; the switch times can be changed on the phone |
| Announcement order | Rhythm, wishing, time, date, month, weekday, church name, quote, extra quote, song | Adds a bell (dongle) before the time, the year after the weekday, and a monthly promise and a blessing prayer after the church name (see [Playback order](#playback-order)) |

Everything else behaves the same: the song rules (monthly, Sunday and happy songs), Sunday Silence and the LCD menu. On top of the Pi version, the ESP32 has [three Programs of times, specific minutes](#programs-and-specific-minutes), [three languages](#languages), four happy song periods, Blessing times, the [monthly promise](#monthly-promise), [season based songs](#season-based-songs), the [MP3 Player Relay](#mp3-player-relay) and [admin and user logins](#logins-admin-and-user). The main README describes them under [How an announcement works](../README.md#how-an-announcement-works) and [Using the clock](../README.md#using-the-clock-front-panel).

> **Updating from the first ESP32 version:** the SD card folder numbers have changed. Build the card again with `prepare_sd_card.py --overwrite`. The new menu settings start at their defaults, which match the old behavior: an announcement and a happy song every 15 minutes.

### Playback order

Every announcement plays these clips one after another. The order is set in `playAnnouncement()` in [esp32-time-teller-clock.ino](esp32-time-teller-clock.ino). All clips come from the [selected language](#languages); the folder numbers below are for Language_1 (add 33 for Language_2, 66 for Language_3).

| # | Step | SD folder | How the file is picked |
|---|------|-----------|------------------------|
| 1 | Rhythm / intro music | `01` | Random |
| 2 | Greeting ("wishing") | `02` | Random |
| 3 | Dongle (bell) | `17` | `001`, rung once per hour on a 12-hour clock: 5 AM or 5 PM rings 5 times, 12 rings 12 times |
| 4 | Spoken time (quarter-hours only) | `03` | hour × 4 + minute ÷ 15 + 1 |
| 5 | Spoken date | `04` | Day of the month |
| 6 | Spoken month | `05` | Month number |
| 7 | Spoken weekday | `06` | `001` = Sunday … `007` = Saturday |
| 8 | Spoken year | `16` | Year − 2000, e.g. `026` = 2026 |
| 9 | Church name (if *Church Name* is ON) | `07` | Random |
| 10 | Monthly promise (at the *Promise* times) | `05` | This month's file: `101` = January … `112` = December |
| 11 | Blessing prayer (at the *Blessing* times) | `18` | Random |
| 12 | Quote | `08` | Random |
| 13 | Extra quote (if *Extra Quotes* is ON) | `09` | Random |
| 14 | Song for this time: season, monthly, Sunday or happy song | `21`–`24`, `13`, `12`, or `19`/`10`/`11`/`20` | Random, or none (see [season based songs](#season-based-songs), [Which song plays](../README.md#which-song-plays) and [happy song periods](#happy-song-periods)) |
| 15 | Custom song (schedules with a song only) | `14`, or `25`–`33` | The schedule's file number, or random from its sub-folder |

A step whose folder or file is missing on the SD card is skipped. A random folder or the bell folder that is empty at startup is skipped at once. A missing numbered file (time, date, month, day, year) is skipped when the MP3 module reports it, or after 2 seconds.

An announcement starts at a quarter-hour that is ON in *Teller* in the active Program, at one of that Program's specific minutes with *Teller* ON, or at a time where a schedule matches the hour, minute, weekday and month. Nothing plays during Sunday Silence, or while the previous announcement is still playing.

### Programs and specific minutes

The clock has three sets of times: **Program 1**, **Program 2** and **Program 3**. It uses one of them at a time, chosen under **Times → The clock uses** on the phone page, or with *Program* in the LCD menu. Each Program has:

- five quarter-hour lists: **Teller** (when announcements play), **Happy songs**, **Sunday songs**, **Blessing** (when the blessing prayer plays) and **Promise** (when the [monthly promise](#monthly-promise) plays);
- up to 20 **specific minutes**, e.g. 6:05 AM, each with its own *Teller*, *Happy song*, *Sunday song*, *Blessing* and *Monthly promise* switches. At a minute that is not :00, :15, :30 or :45 there is no spoken time, because the Time folder has clips for quarter-hours only.

The LCD menus *Teller Times*, *Happy Songs*, *Sunday Songs*, *Blessing Times* and *Promise Times* change the Program in use. Specific minutes can only be changed on the phone. Schedules don't belong to a Program: they run whichever Program is used.

After updating from a version without Programs, Program 1 keeps your old times, with the blessing prayer at every time (as before). Programs 2 and 3 start as copies of Program 1.

### Happy song periods

| Time | Folder | SD folder (Language_1) |
|------|--------|------------------------|
| 12:00 AM – 5:59 AM | `happy_songs_early_morning` | `19` |
| 6:00 AM – 11:59 AM | `happy_songs_morning` | `10` |
| 12:00 PM – 5:59 PM | `happy_songs_evening` | `11` |
| 6:00 PM – 11:59 PM | `happy_songs_night` | `20` |

If the folder for that time has no songs, no happy song plays.

### Languages

`Audio-files-for-esp32-new/` has one folder per language: `Language_1`, `Language_2` and `Language_3`. Each holds the full set of folders (`Rythem`, `Time`, `Date`, …, `Custom_songs`). Choose the language under **Language** on the phone page, or with *Language* in the LCD menu. Every clip of the announcement, the songs, the custom songs and the test song then come from that language. After a change, the clock counts that language's files again (on the phone at once, from the LCD when you are back on the home screen).

### Monthly promise

Each language folder has a `monthly_promise` folder with one file per month: `month-jan.mp3`, `month-feb.mp3` … `month-dec.mp3`. The month can be written short or in full (`month-sept.mp3` and `month-september.mp3` both work), but needs at least 3 letters.

At the times that are ON in the **Promise** tab (or LCD menu *Promise Times*), this month's file plays right after the church name. In January that is `month-jan.mp3`. All Promise times start OFF, and a month with no file is skipped.

`prepare_sd_card.py` puts these files in the **Month folder** (`05` for Language_1) as `101.mp3` = January … `112.mp3` = December, next to the spoken month names (`001`–`012`). They don't need a folder of their own, and all 99 folder numbers are already taken. After adding or changing a promise file, build the SD card again.

### Season based songs

In these seasons the season's song plays instead of the happy, Sunday or monthly song, at the times one of those would play. Each season is turned ON or OFF under **Season based songs** on the phone page (admin only). All four are ON at first.

| Season | Folder | SD folder (Language_1) | Days |
|--------|--------|------------------------|------|
| Good Friday week | `good_friday` | `21` | Palm Sunday to Holy Saturday (the 7 days before Easter) |
| Easter | `easter_sunday` | `22` | Easter Sunday only |
| Christmas week | `christmas` | `23` | 25 to 31 December |
| New Year week | `new_year` | `24` | 1 to 7 January |

- The clock works out Easter itself (the Western date used by most churches in India; for 2026 it is 5 April). The page shows this year's Good Friday and Easter dates.
- The Christmas and New Year days are `CHRISTMAS_FIRST_DAY` … `NEW_YEAR_LAST_DAY` in the sketch's CONFIG section.
- If a season's folder has no songs, the normal song for that time plays.

### MP3 Player Relay

A second relay, on GPIO 1 (`PIN_MP3_RELAY`), for an outside MP3 player or music system. Turn **MP3 Player Relay** ON on the phone page or in the LCD menu, and the relay is ON. When the time teller starts playing, the relay goes OFF; half a second later the announcement starts. When the announcement (or the test song) ends, the relay goes back ON. With the feature OFF, the relay stays OFF.

---

## Setting up from a phone

The clock makes its own Wi-Fi network, so it doesn't need the church Wi-Fi or the internet. Any phone, tablet or laptop with a browser can open the settings page.

### Connecting

1. On the phone, open **Wi-Fi** and join **`TimeTeller`**. The Wi-Fi password is **`12345678`**.
2. Open the browser (Chrome or Safari) and go to **`http://192.168.4.1`**.
3. Tap **Admin** or **User** and enter that password: **`12345678`** for Admin, **`user1234`** for User. The phone remembers the login, so you only type it once. **Log out** is at the top of the page.

If you forget these details, open **Phone Settings** in the LCD menu. It shows the address, and pressing SET shows the Wi-Fi name and password for 5 seconds.

**Android:** the phone may warn that *TimeTeller has no internet access*. Choose **Stay connected** (or "Keep Wi-Fi connection"). If the page still won't open, turn off mobile data while you use it, so the phone doesn't send the request over mobile data instead.

**Android app:** install [TimeTeller.apk](../android-app/TimeTeller.apk) from [android-app/](../android-app/README.md). It shows these steps, checks that the clock answers, and opens the page inside the app. It also reaches the clock with mobile data still on.

**Or add the page to the home screen** (also works on iPhone): once the page is open, in Chrome use menu **⋮** → **Add to Home screen**, in Safari **Share** → **Add to Home Screen**. The icon then opens the settings page directly. The phone still has to be on the TimeTeller Wi-Fi.

### What the page has

| Section | What you can do |
|---------|-----------------|
| **Play** | **Test song**, **Announce now** (plays the announcement for the current time; the spoken time only exists at :00, :15, :30 and :45), and **Stop** (stops anything that is playing). Also shows what is playing. |
| **Date & time** | Set the clock to the phone's time with one tap, or type a date and time. The clock's current time is shown at the top of the page. |
| **Times** | **The clock uses** picks the active Program; the **Program 1/2/3** buttons pick the one to edit (✓ = in use). Five tabs: **Teller** (when announcements play), **Happy songs**, **Sunday songs**, **Blessing** and **Promise** (monthly promise). Each shows all 96 quarter-hours as boxes to tap. Tap an hour's label to switch its four boxes together. The shortcuts are **All ON**, **All OFF**, **Every hour** and **Same as Teller**. A dashed box means a song, blessing or promise is set but Teller is OFF at that time. Below the boxes, **Specific minutes** adds times at any minute. See [Programs and specific minutes](#programs-and-specific-minutes). |
| **Language** *(admin)* | Language 1, 2 or 3: which `Language_N` folder the audio comes from |
| **Sunday Silence** | ON/OFF, start and end time |
| **Monthly Songs** | Tap the months that play monthly songs |
| **Announcement** | Church name ON/OFF, extra quotes ON/OFF |
| **MP3 Player Relay** | ON/OFF. See [MP3 Player Relay](#mp3-player-relay). |
| **Season based songs** *(admin)* | Good Friday week, Easter, Christmas week and New Year week, each ON/OFF. See [Season based songs](#season-based-songs). |
| **Startup screen** | **Show mobile number** ON/OFF, the **Mobile number** (default `9043172726`), and the **Name** shown after it (default `KAIROSE TECH`; leave it empty to skip it). Up to 16 characters each. Phone-only; shown at the next restart. |
| **Sound** *(admin)* | Speaker output (relay), morning and evening volume (0–10), and the times the morning and evening volumes start. The start times are phone-only. |
| **Schedules** *(admin)* | Add, edit, delete, and switch ON/OFF up to 20 schedules. Each has a name, time, days, months, and an optional custom song (one song from `Custom_songs`, or a random song from a `Custom_songs` sub-folder 1–9), taken from the selected language. |
| **SD card** *(admin)* | Lists each song folder with its number of files. Empty folders are shown in orange. **Rescan SD card** counts them again after you copy new songs, with no restart needed. |
| **Clock health** *(admin)* | Free memory (and the lowest it has been since the clock started), how long the clock has been running, and why it last started. See [If the Wi-Fi drops](#if-the-wi-fi-drops). |
| **Login passwords** *(admin)* | New admin and user passwords. See [Logins](#logins-admin-and-user). |

### Saving

- Changes to settings and schedules stay on the phone until you tap **Save** in the bar at the bottom. **Undo** throws them away.
- **Play**, **Stop**, **Set time** and **Rescan** act immediately.
- The LCD menu and the phone change the same settings. If someone changes something on the LCD while the page is open, reload the page before saving, because a save sends all the settings.

### Logins: admin and user

The page has two logins:

| Login | First password | Sees |
|-------|----------------|------|
| **Admin** | `12345678` | Everything |
| **User** | `user1234` | Everything except Sound, Language, Season based songs, Schedules, SD card, Clock health and Login passwords |

The clock checks the login on every request, not just the page: a user login can't save those settings, the schedules or the passwords, and isn't sent the schedules, SD card folders or clock health.

- The admin changes both passwords under **Login passwords** on the page (4–16 characters, no spaces). They are saved in the clock and kept after a restart.
- If a password is forgotten, open **Web Passwords** in the LCD menu and press SET: it shows both for 5 seconds. Anyone at the clock can already change every setting from the LCD, so this gives nothing away.
- The first passwords are `DEFAULT_ADMIN_PASSWORD` and `DEFAULT_USER_PASSWORD` in the CONFIG section. Changing them there only matters before the first start; after that, the saved passwords are used.

The Wi-Fi password (`HOTSPOT_PASSWORD`) is set in the CONFIG section and needs at least 8 characters. To change it, edit the sketch and upload it again.

---

## Parts

| Part | Notes |
|------|-------|
| ESP32-C3 board | One whose USB socket connects straight to the chip, such as the ESP32-C3 SuperMini or Seeed XIAO ESP32-C3. Avoid boards with a separate USB-to-serial chip (for example ESP32-C3-DevKitM-1): it is wired to GPIO 20/21, which this project uses for the TEST button and LED. |
| MP3-TF-16P module + micro-SD card | FAT32, 32 GB or smaller |
| 16×2 I²C LCD (PCF8574 backpack) | Address `0x27` (sometimes `0x3F`) |
| PCF8563 RTC module with coin cell | The same RTC as the Pi version |
| 5 push buttons | LEFT, RIGHT, BACK, SET, TEST. For resistor keys (the default): 4 resistors, 1.8k, 4.3k, 8.2k and 15k, plus a 10k pull-up. See [Choosing the key type](#choosing-the-key-type) |
| Relay module *(optional)* | Switches the amplifier or speaker power |
| Second relay module *(optional)* | The [MP3 Player Relay](#mp3-player-relay) for an outside MP3 player |
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
| Resistor keys (LEFT, RIGHT, BACK, SET) | GPIO 0 | See [Using resistor keys](#using-resistor-keys). For the other key types, see [Choosing the key type](#choosing-the-key-type). |
| TEST button | GPIO 20 | Between the pin and GND. Plays the test song |
| Relay IN | GPIO 10 | HIGH = on. Set `RELAY_ON_LEVEL` to `LOW` for active-low relay boards. |
| MP3 Player Relay IN | GPIO 1 | HIGH = on. Set `MP3_RELAY_ON_LEVEL` to `LOW` for active-low relay boards. |
| Status LED (+ 330 Ω) | GPIO 21 | Other end to GND |
| MP3 module VCC | 5 V | |
| RTC VCC | 3.3 V | |
| All GNDs | GND | Everything must share ground |

GPIO 2 is left free on purpose. It is a boot "strapping" pin, like GPIO 8 and GPIO 9. GPIO 8 and 9 are safe for I²C because the bus pull-up resistors hold them HIGH, which is what the chip needs at boot.

> **The LCD and 5 V.** ESP32-C3 pins are **not 5 V tolerant**. Most PCF8574 LCD backpacks run at 5 V and pull SDA/SCL up to 5 V. Either put an I²C level shifter between the backpack and the ESP32, or use a 3.3 V LCD.

### Choosing the key type

The LEFT, RIGHT, BACK and SET buttons can be wired three ways. Set **exactly one** of these to `true` in the sketch's CONFIG section (the sketch won't compile otherwise):

| Setting | Wiring | Pins |
|---------|--------|------|
| `resistor_based_key` *(default)* | 4 buttons on 2 wires, each through its own resistor | GPIO 0 (`PIN_KEYS_ANALOG`) and GND |
| `matrix_keys` | 2×2 matrix keypad, like the Pi version | GPIO 0, 1, 3, 4 |
| `normal_button` | 4 separate buttons, each between its pin and GND | LEFT GPIO 0, RIGHT GPIO 1, BACK GPIO 3, SET GPIO 4 |

The TEST button is a separate button on GPIO 20 with all three. With `matrix_keys` or `normal_button`, GPIO 1 is a key pin, so move the MP3 Player Relay (`PIN_MP3_RELAY`) to another free pin: the build stops with a message until you do.

### Using resistor keys

Each button connects GPIO 0 to GND through its own resistor, and a **10 kΩ resistor goes from GPIO 0 to 3.3 V** on the ESP32 side. Pressing a button then puts its own voltage on the pin:

| Button | Resistor to GND | Voltage | Setting |
|--------|-----------------|---------|---------|
| LEFT | 1.8 kΩ | 0.5 V | `RESISTOR_KEYS` |
| RIGHT | 4.3 kΩ | 1.0 V | |
| BACK | 8.2 kΩ | 1.5 V | |
| SET | 15 kΩ | 2.0 V | |
| *(none pressed)* | | 3.3 V | |

The steps are 0.5 V rather than 1 V because the ESP32-C3 reads voltages only up to about 2.5 V; 1 V steps would leave room for just two buttons. A reading within 0.2 V (`RESISTOR_KEY_TOLERANCE_MV`) of a button's voltage is that button.

**With other resistors**, or a ready-made resistor keypad: open the Serial Monitor and press each button. It prints `Key LEFT 512 mV` for a recognised button, or `Key 1240 mV: matches no key` for a voltage it doesn't know. Write the voltages you see into `RESISTOR_KEYS`. Pressing two buttons at once gives a different voltage, so press one at a time.

### Using a 2×2 matrix keypad

Set `matrix_keys = true` (and the other two `false`) and wire the keypad's four lines to these pins:

| Keypad line | ESP32-C3 pin | Setting |
|-------------|--------------|---------|
| L1 (row 1) | GPIO 0 | `PIN_MATRIX_L1` |
| L2 (row 2) | GPIO 1 | `PIN_MATRIX_L2` |
| R1 (column 1) | GPIO 3 | `PIN_MATRIX_R1` |
| R2 (column 2) | GPIO 4 | `PIN_MATRIX_R2` |

The keys are laid out like the Pi's keypad: **L1×R1 = LEFT**, **L1×R2 = RIGHT**, **L2×R1 = BACK**, **L2×R2 = SET**. The TEST button stays a separate button on GPIO 20.

The sketch drives one row LOW at a time and reads the columns, which have pull-ups. The other row is left unconnected rather than driven HIGH, so pressing two keys at once can't short two pins together. Don't use GPIO 18/19 (the USB connection) or GPIO 21 (status LED) for the keypad; GPIO 22 doesn't exist on the ESP32-C3.

### Audio output

The MP3-TF-16P has two kinds of output:

- **SPK_1 / SPK_2**: a built-in 3 W amplifier. Connect one small speaker directly.
- **DAC_R / DAC_L + GND**: line-level output. Connect these to the AUX input of the church amplifier. This replaces the Pi's audio jack.

---

## Preparing the SD card

1. Format the micro-SD card as **FAT32**.
2. Put your audio into `Audio-files-for-esp32-new/Language_1`, `Language_2` and `Language_3`. Each language folder has the same sub-folders (`Rythem`, `Wishing`, `Time`, …, and the season folders `good_friday`, `easter_sunday`, `christmas`, `new_year`). A missing language or folder is skipped. A folder in the old layout, with no `Language_` folders, is copied as Language 1.
3. Run the script from this folder. For example, if the SD card is drive `E:`:
   ```bash
   python prepare_sd_card.py ../Audio-files-for-esp32-new E:\
   ```
   If you run it with no folders given, it uses `DEFAULT_SOURCE` and `DEFAULT_DEST` from the top of the script. The script needs Python 3 and no extra packages. You can also copy to an empty folder first and move it to the card later.
4. Read the printed list, which is also saved on the card as `track-list.txt`. It shows the number each file received, and it warns about missing or wrongly named files.

To rebuild a card that already has numbered folders, add `--overwrite`. This deletes the folders `01`–`99` on the card before copying.

### SD card layout

Each language has a block of 33 folders: **Language_1 = `01`–`33`, Language_2 = `34`–`66`, Language_3 = `67`–`99`**. The table shows Language_1. For Language_2 add 33 (for example Rythem = `34`, Time = `36`), and for Language_3 add 66 (Rythem = `67`).

| Folder | Contents | How the clock picks a file |
|--------|----------|----------------------------|
| `01` | Rythem | Random |
| `02` | Wishing | Random |
| `03` | Time: `001` = 12:00 AM, `002` = 12:15 AM … `026` = 6:15 AM … `096` = 11:45 PM | hour × 4 + minute ÷ 15 + 1 |
| `04` | Date: `001`–`031` | Day of the month |
| `05` | Month: `001` = January … `012` = December. Also `monthly_promise`: `101` = `month-jan.mp3` … `112` = `month-dec.mp3` | Month number; 100 + month number for the monthly promise |
| `06` | Day: `001` = Sunday … `007` = Saturday | Weekday |
| `07` | church_name | Random (if *Church Name* is ON) |
| `08` | Quotes | Random |
| `09` | extra_quotes | Random (if *Extra Quotes* is ON) |
| `10` | happy_songs_morning | Random, for songs from 6 AM to 12 PM |
| `11` | happy_songs_evening | Random, for songs from 12 PM to 6 PM |
| `12` | sunday_songs | Random |
| `13` | monthly_songs | Random |
| `14` | `Custom_songs/*.mp3` in name order, except the test song | Number given in a schedule |
| `15` | `001` = `Custom_songs/Testsong.mp3` | Test button |
| `16` | Year: `Year/year-2026.mp3` → `026` (2001–2255) | Year − 2000 |
| `17` | Dongle: `001` = the first file in `Dongle/` (one bell sound) | Rung once per hour (12-hour clock) |
| `18` | Blessing_prayer | Random, at the *Blessing* times |
| `19` | happy_songs_early_morning | Random, for songs from 12 AM to 6 AM |
| `20` | happy_songs_night | Random, for songs from 6 PM to 12 AM |
| `21` | good_friday | Random, in Good Friday week |
| `22` | easter_sunday | Random, on Easter Sunday |
| `23` | christmas | Random, in Christmas week |
| `24` | new_year | Random, in New Year week |
| `25`–`33` | `Custom_songs/` sub-folders 1–9, in name order | Random, for a sub-folder schedule |

Each language can have up to 9 `Custom_songs` sub-folders; the script warns about any more. (Folders 21–24 used to be sub-folders 1–4; a schedule that used sub-folder 10–13 loses its custom song at the next start, and the Serial Monitor says so.) A schedule's sub-folder number (1–9) and song number mean the same position in every language, so give the folders and files the same names in each language.

At startup the clock asks the module how many files are in each random folder. Any folder with no files is listed on the LCD (`No files in N:` followed by the folder numbers) and then skipped when playing. If you add songs later, tap **Rescan SD card** on the phone page, or restart the clock.

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
5. Open `esp32-time-teller-clock.ino`. `web_settings.ino` and `web_page.h` open as extra tabs; keep all three files in the same folder. Edit the CONFIG section, then upload.
6. Open the Serial Monitor at 115200 baud to watch the startup messages. The last one shows the settings page address.

I compiled the sketch with esp32 core 3.3.2 for both board types listed above. It uses about 85% of the flash. The web server doesn't need any extra library.

**Memory.** The ESP32-C3's 400 KB of RAM is plenty. The Arduino IDE reports 38 KB (11%) used for fixed data, and almost all of that belongs to the Wi-Fi and the ESP32's system software; the sketch's own variables take under 2 KB. The rest is free memory for Wi-Fi and the phone page. Each phone request builds its answer in one block of at most about 2.5 KB and frees it afterwards. **Clock health** on the phone page shows the free memory, and the Serial Monitor prints it once an hour.

---

## Configuration

Everything you're likely to change is in the **CONFIG** section at the top of the sketch. The settings on the LCD and phone page don't need a new upload.

### Hotspot and passwords

```cpp
const char* HOTSPOT_NAME     = "TimeTeller";
const char* HOTSPOT_PASSWORD = "12345678";  // Wi-Fi password of the hotspot
const char* DEFAULT_ADMIN_PASSWORD = "12345678";  // first admin login of the page
const char* DEFAULT_USER_PASSWORD  = "user1234";  // first user login of the page
```

The page passwords can then be changed on the page. See [Logins](#logins-admin-and-user).

### Wi-Fi signal

```cpp
const wifi_power_t WIFI_TX_POWER = WIFI_POWER_8_5dBm;
const uint8_t HOTSPOT_CHANNEL = 1;
```

Many ESP32-C3 boards, the SuperMini especially, lose the Wi-Fi at full power (20 dBm): phones drop off the hotspot or can't join it. So the clock transmits at 8.5 dBm, which is enough for a phone in the same room or hall. If you need more range and the Wi-Fi stays stable, try `WIFI_POWER_11dBm`, `WIFI_POWER_13dBm` or `WIFI_POWER_15dBm` (`WIFI_POWER_19_5dBm` is full power). The internet time uses the same power.

`HOTSPOT_CHANNEL` is the hotspot's Wi-Fi channel: use 1, 6 or 11. If phones still drop, other Wi-Fi nearby may be on the same channel, so try another one.

### Internet time (optional)

```cpp
const char* WIFI_SSID     = "ChurchWiFi";
const char* WIFI_PASSWORD = "password";
const char* TIME_ZONE     = "IST-5:30";   // India
```

If the church has Wi-Fi with internet, the clock can join it at startup, copy the internet time into the RTC, and then switch to its own hotspot. Leave `WIFI_SSID` empty (the default) to skip this and set the time from the phone or the LCD instead.

### Schedules

Schedules are managed on the phone page. On first boot the clock starts with the three entries from the Pi's `schedule.json`, which are listed in `DEFAULT_SCHEDULES` in the sketch. To match a `schedule.json` entry on the phone:

| `schedule.json` | Phone page |
|-----------------|------------|
| `"time": "06:05"` | Time 6:05 AM |
| `"days"`, `"months"` | Tap the days and months (or **Every day** / **All**) |
| `"custom_song": "GokulHari.mp3"` | Custom song: **One song (Custom_songs)**, number **1** (from folder `14` in `track-list.txt`) |
| `"custom_song": "new_folder"` | Custom song: **Random from a sub-folder**, number **1** (the sub-folder number in `track-list.txt`) |

Schedules saved by an older version with a folder number 20–99 are changed to the sub-folder number (20 → 1, 21 → 2, …) at startup.
| no `custom_song` | Custom song: **None**. The announcement still ends with the song set for that time. |
| `"enabled"` | The ON/OFF switch (also in the LCD *Schedule Songs* menu) |

### MP3 module options

- **`MP3_USE_ACK`**: some MP3-TF-16P clones don't reply to commands. If the LCD shows *MP3 module / not found*, set this to `false`.
- **`MANUAL_FOLDER_COUNTS`**: if startup lists a folder under *No files in* even though it has files, your module can't report how many files a folder holds. Enter the counts yourself, for example `{ FOLDER_RYTHEM, 25 },`.

### Other options

- **`LCD_ADDRESS`**: the LCD's I²C address, usually `0x27`, sometimes `0x3F`.
- **`LCD_BACKLIGHT_TIMEOUT_MS`**: the LCD backlight turns off after 5 minutes without a button press. The next press turns it back on, and that press does nothing else (so a setting can't change while the screen is dark). TEST still plays the test song straight away. The clock keeps working with the backlight off. If a menu is open when the backlight goes off, its changes are saved (as BACK does) and the display goes back to the home screen; only an unfinished *Date & Time* edit is dropped, so the clock isn't set to a time from minutes ago. Set to `0` to keep the backlight always on.
- **`RELAY_ON_LEVEL`**: set to `LOW` for an active-low relay board.
- **`DAILY_RESTART`**: restarts the clock at 03:55 every day, like the Pi's cron reboot. The restart is skipped if audio is playing or a menu is open.

---

## Startup screens and the status LED

| LCD | LED | Meaning |
|-----|-----|---------|
| `Mobile number` / `9043172726` | Off | Shown for 2 s at power-up, if *Show mobile number* is ON on the phone page |
| `KAIROSE TECH` | Off | The name, shown for 2 s after the number (skipped if the name is empty) |
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
| The phone can't find the `TimeTeller` Wi-Fi | Watch the Serial Monitor at startup. `Hotspot did not start` means a password in CONFIG is shorter than 8 characters. Otherwise stand closer to the clock, or raise `WIFI_TX_POWER` (see [Wi-Fi signal](#wi-fi-signal)). |
| The phone keeps dropping off the Wi-Fi | See [If the Wi-Fi drops](#if-the-wi-fi-drops) below. |
| The settings page doesn't open | Type `http://192.168.4.1`, starting with `http://`, not `https://`. On Android, choose *Stay connected* when the phone says there is no internet, or turn off mobile data. |
| The page keeps asking for the password | Check **Admin** or **User** is the right one for the password. The LCD menu *Web Passwords* shows both. |
| The buttons do nothing, or the wrong button reacts | Check the key type in CONFIG matches the wiring ([Choosing the key type](#choosing-the-key-type)). For resistor keys, check the 10 kΩ pull-up and watch the `Key … mV` lines in the Serial Monitor. |
| Internet time never works | Check `WIFI_SSID` and `WIFI_PASSWORD`, and that the clock is in range of the church Wi-Fi. If the board is near the edge of its range, raise `WIFI_TX_POWER` a step. |
| New songs aren't played | Tap **Rescan SD card** on the phone page, or restart the clock. |

### If the Wi-Fi drops

First open **Clock health** at the bottom of the phone page.

- **The clock restarted by itself.** *Running for* starts again from 0 (the daily restart at 3:55 AM shows as *restart*), and *Last start* says why:
  - *low power*: the 5 V supply dips, often when the amplifier or relay switches on. Use a 2 A 5 V supply (the MP3 module's speaker amplifier alone can draw most of 1 A), short power wires, and the 100–470 µF capacitor.
  - *crash* or *watchdog*: a program fault. Note what the clock was doing at the time.
- **The clock kept running, but the phone left the hotspot.** Connect the clock to a computer and watch the Serial Monitor while it happens:
  - `Phone left the hotspot, reason 3` or `reason 8`: the phone left by itself, usually because the hotspot has no internet. Choose *Stay connected* when the phone asks, turn off mobile data, or turn off *Switch to mobile data* in the phone's Wi-Fi settings (on Samsung phones it is under *Intelligent Wi-Fi*).
  - Any other reason number: the clock lost the phone. If that happens even next to the clock, try another `HOTSPOT_CHANNEL` and check the 5 V supply. If it only happens farther away, raise `WIFI_TX_POWER` one step.
- **Free memory** should stay about the same from hour to hour. Every phone request frees the memory it used, so free memory that keeps falling would point to a fault.
