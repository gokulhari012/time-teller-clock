/*
 * Time Teller Clock - ESP32-C3 version
 * ------------------------------------
 * Port of time-teller-clock-program.py (Raspberry Pi) to an ESP32-C3 with an
 * MP3-TF-16P (DFPlayer Mini compatible) MP3 module.
 *
 * At the times chosen in the "Teller Times" menu, and at the times listed in
 * SCHEDULES below, it plays:
 *   rhythm -> wishing -> time -> date -> month -> day -> church name -> quote
 *   -> extra quote -> song
 * The song is a monthly, Sunday or happy song as set in the LCD menu; a
 * schedule's custom song plays after it.
 *
 * The MP3 module can only play files by NUMBER (for example 03/025.mp3), so the
 * SD card must use the numbered layout made by prepare_sd_card.py.
 * See README.md in this folder for wiring, SD card layout and setup.
 *
 * Libraries (install from the Arduino Library Manager):
 *   - LiquidCrystal I2C     by Frank de Brabander
 *   - RTClib                by Adafruit (also installs Adafruit BusIO)
 *   - DFRobotDFPlayerMini   by DFRobot
 *
 * Board: "ESP32C3 Dev Module" (or your ESP32-C3 board)
 *        Tools -> USB CDC On Boot -> Enabled   (Serial Monitor over USB)
 */

#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <DFRobotDFPlayerMini.h>

// ============================================================
// CONFIG
// ============================================================

// ---- Wi-Fi time sync (optional) ----
// At startup the clock copies internet time into the RTC, like the Pi version.
// Leave WIFI_SSID empty to skip this and use only the RTC.
const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";
const char* TIME_ZONE     = "IST-5:30";  // POSIX TZ string. India = "IST-5:30"
const uint32_t WIFI_TIMEOUT_MS = 15000;
const uint32_t NTP_TIMEOUT_MS  = 10000;

// ---- Pins (ESP32-C3) ----
const uint8_t PIN_I2C_SDA    = 8;   // LCD + RTC
const uint8_t PIN_I2C_SCL    = 9;   // LCD + RTC
const uint8_t PIN_MP3_RX     = 5;   // ESP32 RX <- MP3 module TX
const uint8_t PIN_MP3_TX     = 6;   // ESP32 TX -> MP3 module RX (through a 1k resistor)
const uint8_t PIN_MP3_BUSY   = 7;   // MP3 module BUSY (LOW while playing)
const uint8_t PIN_BTN_LEFT   = 0;   // buttons connect the pin to GND
const uint8_t PIN_BTN_RIGHT  = 1;
const uint8_t PIN_BTN_BACK   = 3;
const uint8_t PIN_BTN_SET    = 4;
const uint8_t PIN_BTN_TEST   = 20;  // plays the test song
const uint8_t PIN_RELAY      = 10;  // speaker / amplifier power relay
const uint8_t PIN_LED_STATUS = 21;  // ON = running, blinking = hardware problem

const uint8_t RELAY_ON_LEVEL = HIGH;  // set to LOW for an active-low relay module

// ---- LCD ----
const uint8_t LCD_ADDRESS = 0x27;  // from an I2C scanner; usually 0x27 or 0x3F

// ---- MP3 module ----
// Some MP3-TF-16P clones don't send acknowledgements. If the LCD shows
// "MP3 module / not found" at startup, set this to false.
const bool MP3_USE_ACK = true;

// ---- Daily restart ----
// Restart every day at 03:55, like the Pi's cron reboot (also re-syncs Wi-Fi time).
const bool DAILY_RESTART = true;
const uint8_t DAILY_RESTART_HOUR   = 3;
const uint8_t DAILY_RESTART_MINUTE = 55;

// ---- SD card folders (made by prepare_sd_card.py) ----
const uint8_t FOLDER_RYTHEM        = 1;   // random file
const uint8_t FOLDER_WISHING       = 2;   // random file
const uint8_t FOLDER_TIME          = 3;   // 001-096 = 12:00 AM, 12:15 AM ... 11:45 PM
const uint8_t FOLDER_DATE          = 4;   // 001-031
const uint8_t FOLDER_MONTH         = 5;   // 001-012 = January ... December
const uint8_t FOLDER_DAY           = 6;   // 001-007 = Sunday ... Saturday
const uint8_t FOLDER_CHURCH_NAME   = 7;   // random file
const uint8_t FOLDER_QUOTES        = 8;   // random file
const uint8_t FOLDER_EXTRA_QUOTES  = 9;   // random file
const uint8_t FOLDER_HAPPY_MORNING = 10;  // random file, for 12 AM to 12 PM
const uint8_t FOLDER_HAPPY_EVENING = 11;  // random file, for 12 PM to 12 AM
const uint8_t FOLDER_SUNDAY_SONGS  = 12;  // random file
const uint8_t FOLDER_MONTHLY_SONGS = 13;  // random file
const uint8_t FOLDER_CUSTOM        = 14;  // Custom_songs/*.mp3 (SONG_TRACK schedules)
const uint8_t FOLDER_TEST          = 15;  // 001.mp3 = Testsong.mp3
// Folders 20-99 = Custom_songs sub-folders (SONG_FOLDER schedules)

// The clock asks the MP3 module how many files each random folder holds.
// If your module can't answer (startup lists a folder under "No files in"
// although it has files), write the counts here and they are used instead.
const uint8_t MANUAL_FOLDER_COUNTS[][2] = {
  // { FOLDER_RYTHEM, 25 },
  { 0, 0 }  // end of list - keep this line
};

// ---- Custom schedules (replaces schedule.json) ----
enum SongType : uint8_t { SONG_NONE, SONG_TRACK, SONG_FOLDER };

struct Schedule {
  const char* name;       // shown in the Schedule Songs menu (14 characters fit)
  uint8_t hour;           // 0-23
  uint8_t minute;         // 0-59
  uint8_t days;           // DAY_* flags joined with |, or ALL_DAYS
  uint16_t months;        // MONTH_* flags joined with |, or ALL_MONTHS
  SongType songType;      // SONG_NONE = announcement + the song set for that time in the menu
  uint8_t songNumber;     // SONG_TRACK: file number in folder 14. SONG_FOLDER: folder number 20-99
  bool enabledByDefault;  // first-boot value; after that it is changed from the LCD
};

constexpr uint8_t DAY_SUN = 1 << 0, DAY_MON = 1 << 1, DAY_TUE = 1 << 2, DAY_WED = 1 << 3,
                  DAY_THU = 1 << 4, DAY_FRI = 1 << 5, DAY_SAT = 1 << 6, ALL_DAYS = 0x7F;
constexpr uint16_t MONTH_JAN = 1 << 0, MONTH_FEB = 1 << 1, MONTH_MAR = 1 << 2, MONTH_APR = 1 << 3,
                   MONTH_MAY = 1 << 4, MONTH_JUN = 1 << 5, MONTH_JUL = 1 << 6, MONTH_AUG = 1 << 7,
                   MONTH_SEP = 1 << 8, MONTH_OCT = 1 << 9, MONTH_NOV = 1 << 10, MONTH_DEC = 1 << 11,
                   ALL_MONTHS = 0x0FFF;

// The first enabled schedule that matches the current minute is played, even at
// times not chosen in Teller Times (but not during Sunday Silence).
// Add new schedules at the END of the list: the ON/OFF choices made on the LCD
// are saved by position.
const Schedule SCHEDULES[] = {
  // name               hh  mm  days               months                 song         no  enabled
  { "mon_tue_mornings",  6,  5, DAY_MON | DAY_TUE, MONTH_JUN | MONTH_JUL, SONG_TRACK,   1, true },  // Custom_songs/GokulHari.mp3 -> 14/001.mp3
  { "sunday1",          15, 30, DAY_SUN,           ALL_MONTHS,            SONG_FOLDER, 20, true },  // Custom_songs/new_folder/   -> 20/
  { "sunday",           15, 30, DAY_SUN,           ALL_MONTHS,            SONG_NONE,    0, true },
};
const uint8_t SCHEDULE_COUNT = sizeof(SCHEDULES) / sizeof(SCHEDULES[0]);
static_assert(sizeof(SCHEDULES) / sizeof(SCHEDULES[0]) <= 32, "At most 32 schedules are supported");

// ---- Timing ----
const uint32_t DEBOUNCE_MS           = 30;
const uint32_t LONG_PRESS_MS         = 1000;  // hold SET this long to open the menu
const uint32_t REPEAT_DELAY_MS       = 500;   // LEFT/RIGHT start repeating after this
const uint32_t REPEAT_RATE_MS        = 200;
const uint32_t FAST_SCROLL_MS        = 2000;  // after this, time lists move an hour per step
const uint32_t MESSAGE_MS            = 1000;  // "Settings Saved" etc.
const uint32_t RELAY_ON_DELAY_MS     = 500;   // let the amplifier power up
const uint32_t CLIP_START_TIMEOUT_MS = 2000;  // BUSY never went LOW = file missing
const uint32_t CLIP_END_MS           = 300;   // BUSY HIGH this long = clip finished
const uint32_t CLIP_GAP_MS           = 100;   // pause between clips

// ============================================================
// TYPES AND GLOBALS
// ============================================================

// Times are chosen in quarter-hours, because the Time folder has one clip per quarter-hour
const uint8_t SLOT_COUNT = 96;  // slot 0 = 12:00 AM, 36 = 9:00 AM, 95 = 11:45 PM

struct SlotSet {
  uint8_t bits[SLOT_COUNT / 8];  // one ON/OFF bit per quarter-hour
};

enum BulkAction : uint8_t { BULK_ALL_ON, BULK_ALL_OFF, BULK_EVERY_HOUR, BULK_COUNT };
const char* const BULK_NAMES[BULK_COUNT] = { "All ON", "All OFF", "Every hour" };

enum InputEvent : uint8_t { EV_NONE, EV_LEFT, EV_RIGHT, EV_BACK, EV_SET, EV_SET_LONG, EV_TEST };

struct Button {
  uint8_t pin;
  InputEvent event;
  bool autoRepeat;
  bool raw = false;
  bool pressed = false;
  bool longSent = false;
  uint32_t rawChangedAt = 0;
  uint32_t pressedAt = 0;
  uint32_t nextRepeatAt = 0;
};

Button buttons[] = {
  { PIN_BTN_LEFT,  EV_LEFT,  true  },
  { PIN_BTN_RIGHT, EV_RIGHT, true  },
  { PIN_BTN_BACK,  EV_BACK,  false },
  { PIN_BTN_SET,   EV_SET,   false },
  { PIN_BTN_TEST,  EV_TEST,  false },
};

enum Screen : uint8_t {
  SCREEN_HOME, SCREEN_MENU, SCREEN_TIME_LIST, SCREEN_SILENCE, SCREEN_MONTHLY,
  SCREEN_SCHEDULES, SCREEN_EDIT_TIME, SCREEN_MESSAGE
};

enum MenuItem : uint8_t {
  MENU_DATE_TIME, MENU_TELLER_TIMES, MENU_HAPPY_SONGS, MENU_SUNDAY_SONGS, MENU_SUNDAY_SILENCE,
  MENU_MONTHLY, MENU_CHURCH_NAME, MENU_EXTRA_QUOTES, MENU_SCHEDULES, MENU_SPEAKER,
  MENU_MORNING_VOLUME, MENU_EVENING_VOLUME, MENU_COUNT
};

const char* const MENU_NAMES[MENU_COUNT] = {
  "Date & Time", "Teller Times", "Happy Songs", "Sunday Songs", "Sunday Silence",
  "Monthly Songs", "Church Name", "Extra Quotes", "Schedule Songs", "Speaker Output",
  "Morning Volume", "Evening Volume"
};
const char* const MONTH_NAMES[12] = {
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November", "December"
};
const char* const MONTH_SHORT[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

struct Settings {
  SlotSet tellerTimes;    // when the time teller runs
  SlotSet happyTimes;     // announcements followed by a happy song
  SlotSet sundayTimes;    // announcements followed by a Sunday song (Sundays only)
  bool silenceOn;         // Sunday Silence: nothing plays on Sunday from silenceStart to silenceEnd
  uint8_t silenceStart;   // quarter-hour slot
  uint8_t silenceEnd;     // quarter-hour slot (not included)
  uint16_t monthlySongs;  // months that play monthly songs, bit 0 = January
  bool churchName;        // play church_name before the quote
  bool extraQuotes;       // play extra_quotes after the quote
  uint32_t scheduleOn;    // one bit per entry in SCHEDULES
  bool speakerOutput;
  uint8_t morningVolume;  // 0-10, used 05:00-16:59
  uint8_t eveningVolume;  // 0-10, used 17:00-04:59
};

struct Clip {
  uint8_t folder;
  uint8_t track;
  const char* label;  // shown as "playing..." on the LCD, or nullptr
};

enum PlayerState : uint8_t { PLAYER_IDLE, PLAYER_POWER_ON, PLAYER_STARTING, PLAYER_PLAYING, PLAYER_GAP };

enum StatusLed : uint8_t { STATUS_OFF, STATUS_ON, STATUS_BLINK };

struct EditDateTime { int day, month, year, hour, minute, second; };

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);
RTC_PCF8563 rtc;
DFRobotDFPlayerMini mp3;
Preferences prefs;
Settings settings;

// Clock
DateTime rtcNow;
int lastMinute = -1;
uint32_t lastRtcReadMs = 0;

// Buttons
uint32_t lastEventHoldMs = 0;  // how long the button of the last event has been held

// Screens and menus
Screen screen = SCREEN_HOME;
uint8_t menuIndex = 0;
uint8_t subIndex = 0;          // month or schedule shown in a sub-menu
Screen messageNext = SCREEN_HOME;
uint32_t messageUntil = 0;
EditDateTime editDt;
uint8_t editField = 0;         // 0 day, 1 month, 2 year, 3 hour, 4 minute, 5 second
bool editAdjusting = false;
SlotSet* timeList = nullptr;   // list shown in SCREEN_TIME_LIST
bool timeListIsSong = false;   // Happy/Sunday Songs: warn when the teller is off at that time
uint8_t timeListIndex = 0;     // 0-2 = bulk actions, 3-98 = quarter-hours
bool timeListConfirm = false;  // a bulk action is waiting for a second SET
uint8_t silenceIndex = 0;      // 0 ON/OFF, 1 start time, 2 end time
bool silenceAdjusting = false;

// Player
const uint8_t MAX_CLIPS = 12;
Clip playlist[MAX_CLIPS];
uint8_t playlistLen = 0;
uint8_t playlistPos = 0;
PlayerState playerState = PLAYER_IDLE;
uint32_t playerTimer = 0;
const char* playingLabel = nullptr;
uint8_t folderCounts[100] = { 0 };

StatusLed statusLed = STATUS_OFF;

// ============================================================
// TIME SLOTS
// ============================================================

bool slotOn(const SlotSet& set, uint8_t slot) {
  return set.bits[slot / 8] & (1 << (slot % 8));
}

void setSlot(SlotSet& set, uint8_t slot, bool on) {
  if (on) {
    set.bits[slot / 8] |= (1 << (slot % 8));
  } else {
    set.bits[slot / 8] &= ~(1 << (slot % 8));
  }
}

void applyBulkAction(SlotSet& set, BulkAction action) {
  for (uint8_t slot = 0; slot < SLOT_COUNT; slot++) {
    setSlot(set, slot, action == BULK_ALL_ON || (action == BULK_EVERY_HOUR && slot % 4 == 0));
  }
}

uint8_t slotOf(const DateTime& t) {
  return t.hour() * 4 + t.minute() / 15;
}

// "09:15 AM" for a quarter-hour slot
void formatSlot(char* buf, size_t size, uint8_t slot) {
  int hour = slot / 4;
  int hour12 = hour % 12 == 0 ? 12 : hour % 12;
  snprintf(buf, size, "%02d:%02d %s", hour12, (slot % 4) * 15, hour < 12 ? "AM" : "PM");
}

// ============================================================
// SETTINGS (saved in ESP32 flash)
// ============================================================

void loadSlots(const char* key, SlotSet& set, BulkAction firstBoot) {
  if (prefs.isKey(key) && prefs.getBytesLength(key) == sizeof(SlotSet)) {
    prefs.getBytes(key, &set, sizeof(SlotSet));
  } else {
    applyBulkAction(set, firstBoot);
  }
}

void loadSettings() {
  uint32_t defaultSchedules = 0;
  for (uint8_t i = 0; i < SCHEDULE_COUNT; i++) {
    if (SCHEDULES[i].enabledByDefault) defaultSchedules |= 1UL << i;
  }

  prefs.begin("timeteller", false);
  loadSlots("tellerSlots", settings.tellerTimes, BULK_ALL_ON);  // every 15 minutes, as before
  loadSlots("happySlots", settings.happyTimes, BULK_ALL_ON);
  loadSlots("sundaySlots", settings.sundayTimes, BULK_ALL_OFF);
  settings.silenceOn     = prefs.getBool("silenceOn", false);
  settings.silenceStart  = prefs.getUChar("silenceFrom", 36) % SLOT_COUNT;  // 9:00 AM
  settings.silenceEnd    = prefs.getUChar("silenceTo", 44) % SLOT_COUNT;    // 11:00 AM
  settings.monthlySongs  = prefs.getUShort("monthSongs", 0);
  settings.churchName    = prefs.getBool("churchName", true);
  settings.extraQuotes   = prefs.getBool("extraQuotes", true);
  settings.speakerOutput = prefs.getBool("speaker", true);
  settings.morningVolume = prefs.getUChar("volMorning", 5);
  settings.eveningVolume = prefs.getUChar("volEvening", 5);

  // Schedules added since the last save get their enabledByDefault value
  uint8_t savedCount = prefs.getUChar("schedCount", 0);
  uint32_t saved = prefs.getULong("schedules", 0);
  uint32_t savedMask = savedCount >= 32 ? 0xFFFFFFFFUL : ((1UL << savedCount) - 1);
  settings.scheduleOn = (saved & savedMask) | (defaultSchedules & ~savedMask);
  prefs.end();
}

void saveSettings() {
  prefs.begin("timeteller", false);
  prefs.putBytes("tellerSlots", &settings.tellerTimes, sizeof(SlotSet));
  prefs.putBytes("happySlots", &settings.happyTimes, sizeof(SlotSet));
  prefs.putBytes("sundaySlots", &settings.sundayTimes, sizeof(SlotSet));
  prefs.putBool("silenceOn", settings.silenceOn);
  prefs.putUChar("silenceFrom", settings.silenceStart);
  prefs.putUChar("silenceTo", settings.silenceEnd);
  prefs.putUShort("monthSongs", settings.monthlySongs);
  prefs.putBool("churchName", settings.churchName);
  prefs.putBool("extraQuotes", settings.extraQuotes);
  prefs.putBool("speaker", settings.speakerOutput);
  prefs.putUChar("volMorning", settings.morningVolume);
  prefs.putUChar("volEvening", settings.eveningVolume);
  prefs.putULong("schedules", settings.scheduleOn);
  prefs.putUChar("schedCount", SCHEDULE_COUNT);
  prefs.end();
  Serial.println("Settings saved.");
}

bool isScheduleOn(uint8_t index) {
  return settings.scheduleOn & (1UL << index);
}

// ============================================================
// HARDWARE HELPERS
// ============================================================

void setRelay(bool on) {
  digitalWrite(PIN_RELAY, on ? RELAY_ON_LEVEL : !RELAY_ON_LEVEL);
}

void updateStatusLed() {
  bool on = statusLed == STATUS_ON || (statusLed == STATUS_BLINK && (millis() / 500) % 2);
  digitalWrite(PIN_LED_STATUS, on ? HIGH : LOW);
}

// Print a full LCD row, padded with spaces so old text is overwritten without clear()
void lcdPrintLine(uint8_t row, const char* text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16.16s", text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

// Returns one event per call: a press, an auto-repeat, or a SET long-press
InputEvent readButtons() {
  uint32_t ms = millis();
  for (Button& b : buttons) {
    bool reading = digitalRead(b.pin) == LOW;
    if (reading != b.raw) {
      b.raw = reading;
      b.rawChangedAt = ms;
    }
    if (b.raw != b.pressed && ms - b.rawChangedAt >= DEBOUNCE_MS) {
      b.pressed = b.raw;
      if (b.pressed) {
        b.pressedAt = ms;
        b.longSent = false;
        b.nextRepeatAt = ms + REPEAT_DELAY_MS;
        lastEventHoldMs = 0;
        return b.event;
      }
    }
    if (b.pressed) {
      if (b.event == EV_SET && !b.longSent && ms - b.pressedAt >= LONG_PRESS_MS) {
        b.longSent = true;
        return EV_SET_LONG;
      }
      if (b.autoRepeat && (int32_t)(ms - b.nextRepeatAt) >= 0) {
        b.nextRepeatAt = ms + REPEAT_RATE_MS;
        lastEventHoldMs = ms - b.pressedAt;
        return b.event;
      }
    }
  }
  return EV_NONE;
}

// Time lists move 15 minutes per step, or an hour once LEFT/RIGHT has been held a while
uint8_t scrollStep() {
  return lastEventHoldMs >= FAST_SCROLL_MS ? 4 : 1;
}

// ============================================================
// WI-FI TIME SYNC
// ============================================================

bool syncTimeFromInternet() {
  if (strlen(WIFI_SSID) == 0) return false;

  lcdPrintLine(1, "Wi-Fi sync...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(250);
  }

  bool synced = false;
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi is connected!");
    configTzTime(TIME_ZONE, "pool.ntp.org", "time.google.com");
    struct tm t;
    // getLocalTime only succeeds once NTP has really set the clock
    if (getLocalTime(&t, NTP_TIMEOUT_MS)) {
      rtc.adjust(DateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec));
      synced = true;
      Serial.println("RTC time set from internet.");
    } else {
      Serial.println("NTP time not received.");
    }
  } else {
    Serial.println("Wi-Fi is NOT connected.");
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return synced;
}

// ============================================================
// AUDIO PLAYER
// ============================================================

int queryFolderCount(uint8_t folder) {
  for (const auto& entry : MANUAL_FOLDER_COUNTS) {
    if (entry[0] == folder) return entry[1];
  }
  int count = -1;
  for (uint8_t attempt = 0; attempt < 2 && count < 0; attempt++) {
    count = mp3.readFileCountsInFolder(folder);
    delay(100);
  }
  return count;
}

void loadFolderCounts() {
  uint8_t folders[20];
  uint8_t folderTotal = 0;
  const uint8_t randomFolders[] = {
    FOLDER_RYTHEM, FOLDER_WISHING, FOLDER_CHURCH_NAME, FOLDER_QUOTES, FOLDER_EXTRA_QUOTES,
    FOLDER_HAPPY_MORNING, FOLDER_HAPPY_EVENING, FOLDER_SUNDAY_SONGS, FOLDER_MONTHLY_SONGS
  };
  for (uint8_t folder : randomFolders) folders[folderTotal++] = folder;
  for (const Schedule& s : SCHEDULES) {
    bool listed = false;
    for (uint8_t i = 0; i < folderTotal; i++) listed |= folders[i] == s.songNumber;
    if (s.songType == SONG_FOLDER && s.songNumber > 0 && s.songNumber < 100 && !listed && folderTotal < 20) {
      folders[folderTotal++] = s.songNumber;
    }
  }

  char emptyList[17] = "";
  uint8_t emptyTotal = 0;
  for (uint8_t i = 0; i < folderTotal; i++) {
    uint8_t folder = folders[i];
    int count = queryFolderCount(folder);
    Serial.printf("Folder %02d: %d files\n", folder, count);
    folderCounts[folder] = count < 0 ? 0 : (count > 255 ? 255 : count);
    if (folderCounts[folder] == 0) {
      emptyTotal++;
      size_t used = strlen(emptyList);
      if (used + 3 <= 16) snprintf(emptyList + used, sizeof(emptyList) - used, "%02d ", folder);
    }
  }
  if (emptyTotal > 0) {
    // Empty folders are skipped when playing; this just makes it visible
    char line0[17];
    snprintf(line0, sizeof(line0), "No files in %d:", emptyTotal);
    lcdPrintLine(0, line0);
    lcdPrintLine(1, emptyList);
    delay(2000);
  }
}

bool isPlayerIdle() {
  return playerState == PLAYER_IDLE;
}

uint8_t currentVolumeSetting() {
  // Morning: 5 AM to 5 PM, Evening: 5 PM to 5 AM
  uint8_t hour = rtcNow.hour();
  return (hour >= 5 && hour < 17) ? settings.morningVolume : settings.eveningVolume;
}

void playlistClear() {
  playlistLen = 0;
  playlistPos = 0;
}

void playlistAdd(uint8_t folder, int track, const char* label) {
  if (track < 1 || track > 255 || playlistLen >= MAX_CLIPS) return;
  playlist[playlistLen++] = { folder, (uint8_t)track, label };
}

void playlistAddRandom(uint8_t folder, const char* label) {
  uint8_t count = folderCounts[folder];
  if (count == 0) {
    Serial.printf("Folder %02d is empty or missing, skipping\n", folder);
    return;
  }
  playlistAdd(folder, random(1, count + 1), label);
}

void playCurrentClip() {
  const Clip& clip = playlist[playlistPos];
  if (clip.label) {
    playingLabel = clip.label;
    if (screen == SCREEN_HOME) drawHome();
  }
  Serial.printf("Playing: %02d/%03d.mp3\n", clip.folder, clip.track);
  mp3.playFolder(clip.folder, clip.track);
  playerState = PLAYER_STARTING;
  playerTimer = millis();
}

void finishPlaylist() {
  setRelay(false);
  playerState = PLAYER_IDLE;
  playlistClear();
  if (playingLabel) {
    playingLabel = nullptr;
    if (screen == SCREEN_HOME) drawHome();
  }
}

void nextClip() {
  playlistPos++;
  if (playlistPos >= playlistLen) {
    finishPlaylist();
    return;
  }
  playerState = PLAYER_GAP;
  playerTimer = millis();
}

void startPlaylist() {
  if (playlistLen == 0) return;
  uint8_t volume = currentVolumeSetting();
  mp3.volume(volume * 3);  // settings are 0-10, the module uses 0-30
  Serial.printf("Volume set to %d/10\n", volume);
  playlistPos = 0;
  if (settings.speakerOutput) {
    setRelay(true);
    playerState = PLAYER_POWER_ON;
    playerTimer = millis();
  } else {
    playCurrentClip();
  }
}

void updatePlayer() {
  // Read messages from the module; skip a clip at once if its file is missing
  if (mp3.available() && mp3.readType() == DFPlayerError) {
    uint16_t error = mp3.read();
    Serial.printf("MP3 module error %d\n", error);
    if (playerState == PLAYER_STARTING && (error == FileMismatch || error == FileIndexOut)) {
      nextClip();
    }
  }

  bool busy = digitalRead(PIN_MP3_BUSY) == LOW;
  uint32_t elapsed = millis() - playerTimer;
  switch (playerState) {
    case PLAYER_IDLE:
      break;
    case PLAYER_POWER_ON:
      if (elapsed >= RELAY_ON_DELAY_MS) playCurrentClip();
      break;
    case PLAYER_STARTING:
      if (busy) {
        playerState = PLAYER_PLAYING;
        playerTimer = millis();
      } else if (elapsed >= CLIP_START_TIMEOUT_MS) {
        Serial.println("Clip did not start (missing file?), skipping");
        nextClip();
      }
      break;
    case PLAYER_PLAYING:
      if (busy) {
        playerTimer = millis();
      } else if (elapsed >= CLIP_END_MS) {
        nextClip();
      }
      break;
    case PLAYER_GAP:
      if (elapsed >= CLIP_GAP_MS) playCurrentClip();
      break;
  }
}

// ============================================================
// TIME TELLER
// ============================================================

// Folder of the song after the announcement: monthly song, else Sunday song, else
// happy song. Songs are set per quarter-hour. A folder with no files is skipped and
// the next one is used. Returns 0 for no song.
uint8_t chooseSongFolder(const DateTime& t) {
  if (t.minute() % 15 != 0) return 0;
  uint8_t slot = slotOf(t);
  bool happy = slotOn(settings.happyTimes, slot);
  bool sunday = t.dayOfTheWeek() == 0 && slotOn(settings.sundayTimes, slot);
  bool monthly = (happy || sunday) && (settings.monthlySongs & (1 << (t.month() - 1)));
  uint8_t happyFolder = t.hour() < 12 ? FOLDER_HAPPY_MORNING : FOLDER_HAPPY_EVENING;

  if (monthly && folderCounts[FOLDER_MONTHLY_SONGS] > 0) return FOLDER_MONTHLY_SONGS;
  if (sunday && folderCounts[FOLDER_SUNDAY_SONGS] > 0) return FOLDER_SUNDAY_SONGS;
  if (happy && folderCounts[happyFolder] > 0) return happyFolder;
  return 0;
}

void playAnnouncement(const DateTime& t, int scheduleIndex) {
  Serial.printf("[%04d-%02d-%02d %02d:%02d:%02d] Playing time teller audio...\n",
                t.year(), t.month(), t.day(), t.hour(), t.minute(), t.second());
  playlistClear();
  playlistAddRandom(FOLDER_RYTHEM, nullptr);
  playlistAddRandom(FOLDER_WISHING, nullptr);
  if (t.minute() % 15 == 0) {  // time clips exist only for quarter-hours
    playlistAdd(FOLDER_TIME, slotOf(t) + 1, nullptr);
  }
  playlistAdd(FOLDER_DATE, t.day(), nullptr);
  playlistAdd(FOLDER_MONTH, t.month(), nullptr);
  playlistAdd(FOLDER_DAY, t.dayOfTheWeek() + 1, nullptr);  // RTClib: 0 = Sunday
  if (settings.churchName) playlistAddRandom(FOLDER_CHURCH_NAME, nullptr);
  playlistAddRandom(FOLDER_QUOTES, nullptr);
  if (settings.extraQuotes) playlistAddRandom(FOLDER_EXTRA_QUOTES, nullptr);

  // The song set for this time (monthly, Sunday or happy), then the schedule's custom song
  uint8_t songFolder = chooseSongFolder(t);
  if (songFolder) {
    Serial.printf("Song from folder %02d\n", songFolder);
    playlistAddRandom(songFolder, nullptr);
  }
  const Schedule* s = scheduleIndex >= 0 ? &SCHEDULES[scheduleIndex] : nullptr;
  if (s && s->songType == SONG_TRACK) {
    playlistAdd(FOLDER_CUSTOM, s->songNumber, s->name);
  } else if (s && s->songType == SONG_FOLDER) {
    playlistAddRandom(s->songNumber, s->name);
  }
  startPlaylist();
}

void playTestSong() {
  if (!isPlayerIdle()) return;
  Serial.println("Test button pressed");
  playlistClear();
  playlistAdd(FOLDER_TEST, 1, "Test song");
  startPlaylist();
}

int findSchedule(const DateTime& t) {
  for (uint8_t i = 0; i < SCHEDULE_COUNT; i++) {
    const Schedule& s = SCHEDULES[i];
    if (!isScheduleOn(i)) continue;
    if (s.hour != t.hour() || s.minute != t.minute()) continue;
    if (!(s.days & (1 << t.dayOfTheWeek()))) continue;
    if (!(s.months & (1 << (t.month() - 1)))) continue;
    return i;
  }
  return -1;
}

// True on Sunday from the Sunday Silence start time up to (not including) the end time
bool inSundaySilence(const DateTime& t) {
  if (!settings.silenceOn || t.dayOfTheWeek() != 0) return false;
  int minuteOfDay = t.hour() * 60 + t.minute();
  int start = settings.silenceStart * 15;
  int end = settings.silenceEnd * 15;
  if (start <= end) return minuteOfDay >= start && minuteOfDay < end;
  return minuteOfDay >= start || minuteOfDay < end;  // a time that crosses midnight
}

void onNewMinute(const DateTime& t) {
  if (DAILY_RESTART && t.hour() == DAILY_RESTART_HOUR && t.minute() == DAILY_RESTART_MINUTE &&
      isPlayerIdle() && screen == SCREEN_HOME) {
    Serial.println("Daily restart");
    delay(100);
    ESP.restart();
  }

  int scheduleIndex = findSchedule(t);
  bool tellerTime = t.minute() % 15 == 0 && slotOn(settings.tellerTimes, slotOf(t));
  if (scheduleIndex < 0 && !tellerTime) return;
  if (inSundaySilence(t)) {
    Serial.println("Sunday Silence time, nothing played");
    return;
  }
  if (!isPlayerIdle()) {
    Serial.println("Still playing, skipping this announcement");
    return;
  }
  playAnnouncement(t, scheduleIndex);
}

void updateClock() {
  if (millis() - lastRtcReadMs < 250) return;
  lastRtcReadMs = millis();

  DateTime t = rtc.now();
  if (!t.isValid()) return;
  bool secondChanged = t.second() != rtcNow.second() || t.minute() != rtcNow.minute();
  rtcNow = t;

  if (secondChanged && screen == SCREEN_HOME) drawHome();
  if (t.minute() != lastMinute) {
    lastMinute = t.minute();
    onNewMinute(t);
  }
}

// ============================================================
// SCREENS
// ============================================================

void drawHome() {
  if (playingLabel) {
    lcdPrintLine(0, "playing...");
    lcdPrintLine(1, playingLabel);
    return;
  }
  char line0[17], line1[17];
  snprintf(line0, sizeof(line0), "%02d-%s-%04d", rtcNow.day(), MONTH_SHORT[rtcNow.month() - 1], rtcNow.year());
  snprintf(line1, sizeof(line1), "%02d:%02d:%02d %s", rtcNow.twelveHour(), rtcNow.minute(), rtcNow.second(),
           rtcNow.isPM() ? "PM" : "AM");
  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

void drawMenu() {
  char line0[17], line1[17];
  snprintf(line0, sizeof(line0), "> %s", MENU_NAMES[menuIndex]);
  switch (menuIndex) {
    case MENU_DATE_TIME:
      snprintf(line1, sizeof(line1), "%02d:%02d %02d-%02d", rtcNow.hour(), rtcNow.minute(), rtcNow.day(), rtcNow.month());
      break;
    case MENU_CHURCH_NAME:
      snprintf(line1, sizeof(line1), "Status: %s", settings.churchName ? "ON" : "OFF");
      break;
    case MENU_EXTRA_QUOTES:
      snprintf(line1, sizeof(line1), "Status: %s", settings.extraQuotes ? "ON" : "OFF");
      break;
    case MENU_SPEAKER:
      snprintf(line1, sizeof(line1), "Status: %s", settings.speakerOutput ? "ON" : "OFF");
      break;
    case MENU_MORNING_VOLUME:
      snprintf(line1, sizeof(line1), "Value: %d", settings.morningVolume);
      break;
    case MENU_EVENING_VOLUME:
      snprintf(line1, sizeof(line1), "Value: %d", settings.eveningVolume);
      break;
    default:  // items that open a sub-menu
      snprintf(line1, sizeof(line1), "Tap Set to Open");
      break;
  }
  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

void drawTimeList() {
  char line0[17], line1[17];
  if (timeListIndex < BULK_COUNT) {
    snprintf(line0, sizeof(line0), "> %s", BULK_NAMES[timeListIndex]);
    snprintf(line1, sizeof(line1), "%s", timeListConfirm ? "Set again = OK" : "Tap Set to apply");
  } else {
    uint8_t slot = timeListIndex - BULK_COUNT;
    char label[12];
    formatSlot(label, sizeof(label), slot);
    snprintf(line0, sizeof(line0), "> %s", label);
    bool on = slotOn(*timeList, slot);
    if (on && timeListIsSong && !slotOn(settings.tellerTimes, slot)) {
      snprintf(line1, sizeof(line1), "ON (Teller OFF)");  // a song only plays when the teller runs
    } else {
      snprintf(line1, sizeof(line1), "Status: %s", on ? "ON" : "OFF");
    }
  }
  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

void drawSilence() {
  char line1[17], label[12];
  if (silenceIndex == 0) {
    lcdPrintLine(0, "> Silence");
    snprintf(line1, sizeof(line1), "Status: %s", settings.silenceOn ? "ON" : "OFF");
  } else {
    lcdPrintLine(0, silenceIndex == 1 ? "> Start time" : "> End time");
    formatSlot(label, sizeof(label), silenceIndex == 1 ? settings.silenceStart : settings.silenceEnd);
    snprintf(line1, sizeof(line1), "%s%s", label, silenceAdjusting ? "  +/-" : "");
  }
  lcdPrintLine(1, line1);
}

void drawMonthly() {
  char line0[17], line1[17];
  snprintf(line0, sizeof(line0), "> %s", MONTH_NAMES[subIndex]);
  snprintf(line1, sizeof(line1), "Status: %s", (settings.monthlySongs & (1 << subIndex)) ? "ON" : "OFF");
  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

void drawSchedules() {
  char line0[17], line1[17];
  snprintf(line0, sizeof(line0), "> %.14s", SCHEDULES[subIndex].name);
  snprintf(line1, sizeof(line1), "Status: %s", isScheduleOn(subIndex) ? "ON" : "OFF");
  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

void drawEditTime() {
  char line0[17], line1[17];
  snprintf(line0, sizeof(line0), "%02d-%02d-%04d%s", editDt.day, editDt.month, editDt.year, editAdjusting ? "  +/-" : "");
  snprintf(line1, sizeof(line1), "%02d:%02d:%02d", editDt.hour, editDt.minute, editDt.second);
  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);

  // Blink the cursor on the selected field
  const uint8_t columns[3] = { 0, 3, 6 };
  lcd.setCursor(columns[editField % 3], editField < 3 ? 0 : 1);
  lcd.blink();
}

void enterScreen(Screen next) {
  screen = next;
  lcd.noBlink();
  switch (screen) {
    case SCREEN_HOME:      drawHome(); break;
    case SCREEN_MENU:      drawMenu(); break;
    case SCREEN_TIME_LIST: drawTimeList(); break;
    case SCREEN_SILENCE:   drawSilence(); break;
    case SCREEN_MONTHLY:   drawMonthly(); break;
    case SCREEN_SCHEDULES: drawSchedules(); break;
    case SCREEN_EDIT_TIME: drawEditTime(); break;
    case SCREEN_MESSAGE:   break;
  }
}

// Show a two-line message for MESSAGE_MS, then go to the next screen
void showMessage(const char* line0, const char* line1, Screen next) {
  lcd.noBlink();
  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
  screen = SCREEN_MESSAGE;
  messageNext = next;
  messageUntil = millis() + MESSAGE_MS;
}

void updateMessage() {
  if (screen == SCREEN_MESSAGE && (int32_t)(millis() - messageUntil) >= 0) {
    enterScreen(messageNext);
  }
}

// ============================================================
// DATE & TIME EDITOR
// ============================================================

// Wrap value into low..high. C++ % can return a negative number, so add span first.
int wrapValue(int value, int low, int high) {
  int span = high - low + 1;
  return low + ((value - low) % span + span) % span;
}

int daysInMonth(int year, int month) {
  static const uint8_t DAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return (month == 2 && leap) ? 29 : DAYS[month - 1];
}

void adjustEditField(int delta) {
  switch (editField) {
    case 0: editDt.day    = wrapValue(editDt.day + delta, 1, daysInMonth(editDt.year, editDt.month)); break;
    case 1: editDt.month  = wrapValue(editDt.month + delta, 1, 12); break;
    case 2: editDt.year   = wrapValue(editDt.year + delta, 2000, 2099); break;  // PCF8563 range
    case 3: editDt.hour   = wrapValue(editDt.hour + delta, 0, 23); break;
    case 4: editDt.minute = wrapValue(editDt.minute + delta, 0, 59); break;
    case 5: editDt.second = wrapValue(editDt.second + delta, 0, 59); break;
  }
  // Keep the day valid after a month or year change (31 -> 30 in April, 29-Feb in a non-leap year)
  int maxDay = daysInMonth(editDt.year, editDt.month);
  if (editDt.day > maxDay) editDt.day = maxDay;
}

void startTimeEditor() {
  editDt = { rtcNow.day(), rtcNow.month(), rtcNow.year(), rtcNow.hour(), rtcNow.minute(), rtcNow.second() };
  editField = 0;
  editAdjusting = false;
  enterScreen(SCREEN_EDIT_TIME);
}

void saveEditedTime() {
  rtc.adjust(DateTime(editDt.year, editDt.month, editDt.day, editDt.hour, editDt.minute, editDt.second));
  rtcNow = rtc.now();
  lastMinute = rtcNow.minute();  // don't announce just because the minute was edited
  Serial.println("RTC time updated.");
  showMessage("RTC Updated!", "Back to Main", SCREEN_MENU);
}

// ============================================================
// BUTTON HANDLING
// ============================================================

void openTimeList(SlotSet* list, bool isSongList) {
  timeList = list;
  timeListIsSong = isSongList;
  timeListIndex = BULK_COUNT + slotOf(rtcNow);  // start at the current time
  timeListConfirm = false;
  enterScreen(SCREEN_TIME_LIST);
}

void handleMenuEvent(InputEvent ev) {
  switch (ev) {
    case EV_RIGHT:
      menuIndex = (menuIndex + 1) % MENU_COUNT;
      drawMenu();
      break;
    case EV_LEFT:
      menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
      drawMenu();
      break;
    case EV_SET:
      switch (menuIndex) {
        case MENU_DATE_TIME:    startTimeEditor(); return;
        case MENU_TELLER_TIMES: openTimeList(&settings.tellerTimes, false); return;
        case MENU_HAPPY_SONGS:  openTimeList(&settings.happyTimes, true); return;
        case MENU_SUNDAY_SONGS: openTimeList(&settings.sundayTimes, true); return;
        case MENU_SUNDAY_SILENCE:
          silenceIndex = 0;
          silenceAdjusting = false;
          enterScreen(SCREEN_SILENCE);
          return;
        case MENU_MONTHLY:
          subIndex = rtcNow.month() - 1;  // start at the current month
          enterScreen(SCREEN_MONTHLY);
          return;
        case MENU_SCHEDULES:
          if (SCHEDULE_COUNT == 0) {
            showMessage("No schedules", "", SCREEN_MENU);
          } else {
            subIndex = 0;
            enterScreen(SCREEN_SCHEDULES);
          }
          return;
        case MENU_CHURCH_NAME:    settings.churchName = !settings.churchName; break;
        case MENU_EXTRA_QUOTES:   settings.extraQuotes = !settings.extraQuotes; break;
        case MENU_SPEAKER:        settings.speakerOutput = !settings.speakerOutput; break;
        case MENU_MORNING_VOLUME: settings.morningVolume = (settings.morningVolume + 1) % 11; break;  // cycle 0-10
        case MENU_EVENING_VOLUME: settings.eveningVolume = (settings.eveningVolume + 1) % 11; break;
      }
      drawMenu();
      break;
    case EV_BACK:
      saveSettings();
      Serial.println(settings.speakerOutput ? "Speaker Output Enabled" : "Speaker Output Disabled");
      if (!settings.speakerOutput) setRelay(false);
      showMessage("Settings Saved", "", SCREEN_HOME);
      break;
    default:
      break;
  }
}

void handleTimeListEvent(InputEvent ev) {
  const uint8_t entries = BULK_COUNT + SLOT_COUNT;
  switch (ev) {
    case EV_RIGHT:
      timeListIndex = (timeListIndex + scrollStep()) % entries;
      timeListConfirm = false;
      break;
    case EV_LEFT:
      timeListIndex = (timeListIndex + entries - scrollStep()) % entries;
      timeListConfirm = false;
      break;
    case EV_SET:
      if (timeListIndex < BULK_COUNT) {
        if (!timeListConfirm) {
          timeListConfirm = true;  // ask for a second SET
          break;
        }
        applyBulkAction(*timeList, (BulkAction)timeListIndex);
        timeListConfirm = false;
        showMessage("Done", "", SCREEN_TIME_LIST);
        return;
      } else {
        uint8_t slot = timeListIndex - BULK_COUNT;
        setSlot(*timeList, slot, !slotOn(*timeList, slot));
      }
      break;
    case EV_BACK:
      saveSettings();
      showMessage("Setting Saved", "Back to Main", SCREEN_MENU);
      return;
    default:
      return;
  }
  drawTimeList();
}

void handleSilenceEvent(InputEvent ev) {
  switch (ev) {
    case EV_RIGHT:
    case EV_LEFT: {
      int direction = ev == EV_RIGHT ? 1 : -1;
      if (silenceAdjusting) {
        uint8_t& slot = silenceIndex == 1 ? settings.silenceStart : settings.silenceEnd;
        slot = wrapValue(slot + direction * scrollStep(), 0, SLOT_COUNT - 1);
      } else {
        silenceIndex = wrapValue(silenceIndex + direction, 0, 2);
      }
      break;
    }
    case EV_SET:
      if (silenceIndex == 0) {
        settings.silenceOn = !settings.silenceOn;
      } else {
        silenceAdjusting = !silenceAdjusting;
      }
      break;
    case EV_BACK:
      if (silenceAdjusting) {
        silenceAdjusting = false;
        break;
      }
      saveSettings();
      showMessage("Setting Saved", "Back to Main", SCREEN_MENU);
      return;
    default:
      return;
  }
  drawSilence();
}

void handleMonthlyEvent(InputEvent ev) {
  switch (ev) {
    case EV_RIGHT: subIndex = (subIndex + 1) % 12; break;
    case EV_LEFT:  subIndex = (subIndex + 11) % 12; break;
    case EV_SET:   settings.monthlySongs ^= (1 << subIndex); break;
    case EV_BACK:
      saveSettings();
      showMessage("Setting Saved", "Back to Main", SCREEN_MENU);
      return;
    default:
      return;
  }
  drawMonthly();
}

void handleSchedulesEvent(InputEvent ev) {
  switch (ev) {
    case EV_RIGHT: subIndex = (subIndex + 1) % SCHEDULE_COUNT; break;
    case EV_LEFT:  subIndex = (subIndex + SCHEDULE_COUNT - 1) % SCHEDULE_COUNT; break;
    case EV_SET:   settings.scheduleOn ^= (1UL << subIndex); break;
    case EV_BACK:
      saveSettings();
      showMessage("Setting Saved", "Back to Main", SCREEN_MENU);
      return;
    default:
      return;
  }
  drawSchedules();
}

void handleEditEvent(InputEvent ev) {
  if (!editAdjusting) {
    // Choosing a field
    switch (ev) {
      case EV_RIGHT: editField = (editField + 1) % 6; break;
      case EV_LEFT:  editField = (editField + 5) % 6; break;
      case EV_SET:   editAdjusting = true; break;
      case EV_BACK:  saveEditedTime(); return;
      default:       return;
    }
  } else {
    // Changing the selected field
    switch (ev) {
      case EV_RIGHT: adjustEditField(+1); break;
      case EV_LEFT:  adjustEditField(-1); break;
      case EV_SET:
      case EV_BACK:  editAdjusting = false; break;
      default:       return;
    }
  }
  drawEditTime();
}

void handleEvent(InputEvent ev) {
  if (ev == EV_TEST) {
    playTestSong();
    return;
  }
  switch (screen) {
    case SCREEN_HOME:
      if (ev == EV_SET_LONG) {
        Serial.println("Entering Settings Menu...");
        menuIndex = 0;
        showMessage("Settings Menu", "", SCREEN_MENU);
      }
      break;
    case SCREEN_MENU:      handleMenuEvent(ev); break;
    case SCREEN_TIME_LIST: handleTimeListEvent(ev); break;
    case SCREEN_SILENCE:   handleSilenceEvent(ev); break;
    case SCREEN_MONTHLY:   handleMonthlyEvent(ev); break;
    case SCREEN_SCHEDULES: handleSchedulesEvent(ev); break;
    case SCREEN_EDIT_TIME: handleEditEvent(ev); break;
    case SCREEN_MESSAGE:   break;  // buttons are ignored while a message is shown
  }
}

// ============================================================
// SETUP AND LOOP
// ============================================================

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);
  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);
  pinMode(PIN_MP3_BUSY, INPUT_PULLUP);
  for (Button& b : buttons) pinMode(b.pin, INPUT_PULLUP);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcdPrintLine(0, "Time Teller");
  lcdPrintLine(1, "Starting...");

  Serial.println("Loading settings...");
  loadSettings();

  Serial.println("Initing RTC...");
  if (!rtc.begin(&Wire)) {
    Serial.println("Init RTC failed");
    lcdPrintLine(0, "RTC not found");
    lcdPrintLine(1, "Check wiring");
    statusLed = STATUS_BLINK;
    while (true) {
      updateStatusLed();
      delay(50);
    }
  }
  bool rtcLostPower = rtc.lostPower();
  rtc.start();
  if (!rtc.now().isValid()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // start from the upload time
    rtcLostPower = true;
  }

  Serial.println("Starting MP3 module...");
  lcdPrintLine(1, "MP3 module...");
  Serial1.begin(9600, SERIAL_8N1, PIN_MP3_RX, PIN_MP3_TX);
  delay(1000);  // the module needs about a second to read the SD card after power-up
  bool mp3Ok = false;
  for (uint8_t attempt = 0; attempt < 3 && !mp3Ok; attempt++) {
    mp3Ok = mp3.begin(Serial1, MP3_USE_ACK, true);
  }
  if (mp3Ok) {
    mp3.setTimeOut(500);
    lcdPrintLine(1, "Reading SD card");
    loadFolderCounts();
  } else {
    Serial.println("MP3 module not found");
    lcdPrintLine(0, "MP3 module");
    lcdPrintLine(1, "not found");
    delay(3000);
  }

  if (!syncTimeFromInternet() && rtcLostPower) {
    lcdPrintLine(0, "Clock was reset");
    lcdPrintLine(1, "Set date & time");
    delay(3000);
  }

  rtcNow = rtc.now();
  lastMinute = rtcNow.minute();  // wait for the next minute, like the Pi version
  statusLed = mp3Ok ? STATUS_ON : STATUS_BLINK;
  Serial.println("Time Teller with Scheduler started.");
  enterScreen(SCREEN_HOME);
}

void loop() {
  updateClock();
  InputEvent ev = readButtons();
  if (ev != EV_NONE) handleEvent(ev);
  updatePlayer();
  updateMessage();
  updateStatusLed();
  delay(5);
}
