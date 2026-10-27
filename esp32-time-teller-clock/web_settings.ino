/*
 * Phone settings page
 * -------------------
 * The clock makes its own Wi-Fi hotspot (HOTSPOT_NAME / HOTSPOT_PASSWORD).
 * Connect a phone to it and open http://192.168.4.1 in the browser.
 * The page itself is in web_page.h; this file answers its requests:
 *
 *   GET  /               the settings page
 *   GET  /api/state      settings, schedules and status, as JSON
 *   GET  /api/status     clock time, what is playing, SD card folders, free memory
 *   POST /api/settings   save the menu settings
 *   POST /api/schedules  replace the schedule list
 *   POST /api/time       set the clock (t = YYYY-MM-DDTHH:MM:SS)
 *   POST /api/test       play the test song
 *   POST /api/announce   play the announcement for the current time
 *   POST /api/stop       stop playing
 *   POST /api/rescan     count the files on the SD card again
 *
 * Every /api/ request needs WEB_PASSWORD (HTTP Basic auth, user "admin").
 * The handlers run inside loop(), so they never overlap the clock or the LCD.
 */

#include "web_page.h"

const char* WEB_USER = "admin";  // the settings page always logs in with this name

WebServer server(80);
bool webStarted = false;

// ============================================================
// HELPERS
// ============================================================

void sendJson(const String& json) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void sendError(int code, const String& message) {
  server.send(code, "application/json", "{\"error\":\"" + message + "\"}");
}

// Checks the password. Without it, answers 401 without a WWW-Authenticate header,
// so the phone shows the page's own password box instead of a browser pop-up.
bool authorized() {
  if (server.authenticate(WEB_USER, WEB_PASSWORD)) return true;
  sendError(401, "Wrong password");
  return false;
}

// Reads a whole-number form field. If it is missing or outside low..high,
// answers 400 and returns false.
bool readNumber(const char* name, long low, long high, long& value) {
  String text = server.arg(name);
  char* end = nullptr;
  value = strtol(text.c_str(), &end, 10);
  if (text.length() > 0 && *end == '\0' && value >= low && value <= high) return true;
  sendError(400, String("Bad value for ") + name);
  return false;
}

// 96 ON/OFF quarter-hours as 24 hex digits, two per byte of SlotSet::bits
void slotsToHex(const SlotSet& set, char* out) {
  for (uint8_t i = 0; i < sizeof(set.bits); i++) sprintf(out + i * 2, "%02x", set.bits[i]);
}

bool readSlots(const char* name, SlotSet& set) {
  String hex = server.arg(name);
  bool ok = hex.length() == sizeof(set.bits) * 2;
  for (uint8_t i = 0; ok && i < sizeof(set.bits); i++) {
    char pair[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
    ok = isxdigit((unsigned char)pair[0]) && isxdigit((unsigned char)pair[1]);
    set.bits[i] = strtol(pair, nullptr, 16);
  }
  if (!ok) sendError(400, String("Bad value for ") + name);
  return ok;
}

// Keeps only characters the LCD can show and that are safe inside JSON
void cleanName(const String& text, char* out, size_t size) {
  size_t used = 0;
  for (size_t i = 0; i < text.length() && used < size - 1; i++) {
    char c = text[i];
    if (c >= 32 && c <= 126 && c != '"' && c != '\\') out[used++] = c;
  }
  out[used] = '\0';
}

// After a change from the phone, redraw the LCD if it is showing a menu
void redrawLcdMenu() {
  if (screen == SCREEN_SCHEDULES) {
    if (scheduleCount == 0) {
      enterScreen(SCREEN_MENU);
      return;
    }
    if (subIndex >= scheduleCount) subIndex = 0;
  }
  if (screen != SCREEN_HOME && screen != SCREEN_MESSAGE && screen != SCREEN_EDIT_TIME) enterScreen(screen);
}

// ============================================================
// JSON
// ============================================================

// Each answer is built in one String with its room reserved first, so it takes a single
// block of memory instead of growing (and being copied) many times while it is built.
const size_t STATUS_JSON_SIZE   = 512;  // the status, with up to about 40 SD card folders
const size_t SCHEDULE_JSON_SIZE = 80;   // one schedule

void addStatusJson(String& json) {
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"time\":\"%04d-%02d-%02dT%02d:%02d:%02d\",\"playing\":%d,\"label\":\"%s\",\"mp3\":%d,"
           "\"heap\":%lu,\"minHeap\":%lu,\"uptime\":%lu,\"reset\":\"%s\",\"folders\":[",
           rtcNow.year(), rtcNow.month(), rtcNow.day(), rtcNow.hour(), rtcNow.minute(), rtcNow.second(),
           isPlayerIdle() ? 0 : 1, playingLabel ? playingLabel : "", mp3Ok ? 1 : 0,
           (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
           (unsigned long)(esp_timer_get_time() / 1000000), resetReasonText());
  json += buf;
  bool first = true;
  for (uint8_t folder = 1; folder < 100; folder++) {
    if (!folderCounted[folder]) continue;
    snprintf(buf, sizeof(buf), "%s[%d,%d]", first ? "" : ",", folder, folderCounts[folder]);
    json += buf;
    first = false;
  }
  json += "]}";
}

void addSettingsJson(String& json) {
  char teller[25], happy[25], sunday[25], buf[420];
  slotsToHex(settings.tellerTimes, teller);
  slotsToHex(settings.happyTimes, happy);
  slotsToHex(settings.sundayTimes, sunday);
  snprintf(buf, sizeof(buf),
           "{\"teller\":\"%s\",\"happy\":\"%s\",\"sunday\":\"%s\",\"silenceOn\":%d,\"silenceStart\":%d,"
           "\"silenceEnd\":%d,\"months\":%d,\"churchName\":%d,\"extraQuotes\":%d,\"speaker\":%d,"
           "\"volMorning\":%d,\"volEvening\":%d,\"morningFrom\":%d,\"eveningFrom\":%d}",
           teller, happy, sunday, settings.silenceOn, settings.silenceStart, settings.silenceEnd,
           settings.monthlySongs, settings.churchName, settings.extraQuotes, settings.speakerOutput,
           settings.morningVolume, settings.eveningVolume, settings.morningFrom, settings.eveningFrom);
  json += buf;
}

void addSchedulesJson(String& json) {
  char buf[140];
  json += "[";
  for (uint8_t i = 0; i < scheduleCount; i++) {
    const Schedule& s = schedules[i];
    snprintf(buf, sizeof(buf), "%s{\"n\":\"%s\",\"h\":%d,\"m\":%d,\"d\":%d,\"mo\":%d,\"t\":%d,\"k\":%d,\"e\":%d}",
             i ? "," : "", s.name, s.hour, s.minute, s.days, s.months, s.songType, s.songNumber, s.enabled);
    json += buf;
  }
  json += "]";
}

void sendStatus() {
  String json;
  json.reserve(STATUS_JSON_SIZE);
  addStatusJson(json);
  sendJson(json);
}

void sendState() {
  String json;
  json.reserve(400 + scheduleCount * SCHEDULE_JSON_SIZE + STATUS_JSON_SIZE);  // settings + schedules + status
  json += "{\"settings\":";
  addSettingsJson(json);
  json += ",\"schedules\":";
  addSchedulesJson(json);
  json += ",\"status\":";
  addStatusJson(json);
  json += "}";
  sendJson(json);
}

// ============================================================
// REQUEST HANDLERS
// ============================================================

void handlePage() {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleState() {
  if (!authorized()) return;
  sendState();
}

void handleStatus() {
  if (!authorized()) return;
  sendStatus();
}

void handleSaveSettings() {
  if (!authorized()) return;
  Settings updated = settings;
  long v;
  if (!readSlots("teller", updated.tellerTimes)) return;
  if (!readSlots("happy", updated.happyTimes)) return;
  if (!readSlots("sunday", updated.sundayTimes)) return;
  if (!readNumber("silenceOn", 0, 1, v)) return;
  updated.silenceOn = v;
  if (!readNumber("silenceStart", 0, SLOT_COUNT - 1, v)) return;
  updated.silenceStart = v;
  if (!readNumber("silenceEnd", 0, SLOT_COUNT - 1, v)) return;
  updated.silenceEnd = v;
  if (!readNumber("months", 0, ALL_MONTHS, v)) return;
  updated.monthlySongs = v;
  if (!readNumber("churchName", 0, 1, v)) return;
  updated.churchName = v;
  if (!readNumber("extraQuotes", 0, 1, v)) return;
  updated.extraQuotes = v;
  if (!readNumber("speaker", 0, 1, v)) return;
  updated.speakerOutput = v;
  if (!readNumber("volMorning", 0, 10, v)) return;
  updated.morningVolume = v;
  if (!readNumber("volEvening", 0, 10, v)) return;
  updated.eveningVolume = v;
  if (!readNumber("morningFrom", 0, SLOT_COUNT - 1, v)) return;
  updated.morningFrom = v;
  if (!readNumber("eveningFrom", 0, SLOT_COUNT - 1, v)) return;
  updated.eveningFrom = v;

  settings = updated;
  saveSettings();
  if (!settings.speakerOutput) setRelay(false);
  redrawLcdMenu();
  sendState();
}

void handleSaveSchedules() {
  if (!authorized()) return;
  long count, v;
  if (!readNumber("count", 0, MAX_SCHEDULES, count)) return;

  Schedule updated[MAX_SCHEDULES] = {};
  char key[8];
  for (uint8_t i = 0; i < count; i++) {
    Schedule& s = updated[i];
    snprintf(key, sizeof(key), "n%d", i);
    cleanName(server.arg(key), s.name, sizeof(s.name));
    if (s.name[0] == '\0') snprintf(s.name, sizeof(s.name), "Schedule %d", i + 1);
    snprintf(key, sizeof(key), "h%d", i);
    if (!readNumber(key, 0, 23, v)) return;
    s.hour = v;
    snprintf(key, sizeof(key), "m%d", i);
    if (!readNumber(key, 0, 59, v)) return;
    s.minute = v;
    snprintf(key, sizeof(key), "d%d", i);
    if (!readNumber(key, 1, ALL_DAYS, v)) return;
    s.days = v;
    snprintf(key, sizeof(key), "mo%d", i);
    if (!readNumber(key, 1, ALL_MONTHS, v)) return;
    s.months = v;
    snprintf(key, sizeof(key), "t%d", i);
    if (!readNumber(key, SONG_NONE, SONG_FOLDER, v)) return;
    s.songType = (SongType)v;
    // Song number: a file 1-255 in folder 14, or a folder 20-99
    long low = s.songType == SONG_FOLDER ? FOLDER_FIRST_CUSTOM : (s.songType == SONG_TRACK ? 1 : 0);
    long high = s.songType == SONG_FOLDER ? 99 : 255;
    snprintf(key, sizeof(key), "k%d", i);
    if (!readNumber(key, low, high, v)) return;
    s.songNumber = s.songType == SONG_NONE ? 0 : v;
    snprintf(key, sizeof(key), "e%d", i);
    if (!readNumber(key, 0, 1, v)) return;
    s.enabled = v;
  }

  memcpy(schedules, updated, sizeof(updated));
  scheduleCount = count;
  saveSchedules();
  folderCountPending = true;  // count any new song folders once nothing is playing
  redrawLcdMenu();
  sendState();
}

void handleSetTime() {
  if (!authorized()) return;
  int y, mo, d, h, mi, s;
  String text = server.arg("t");
  bool ok = sscanf(text.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) == 6 &&
            y >= 2000 && y <= 2099 && mo >= 1 && mo <= 12 && d >= 1 && d <= daysInMonth(y, mo) &&
            h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59;
  if (!ok) {
    sendError(400, "Bad date or time");
    return;
  }
  rtcNow = DateTime(y, mo, d, h, mi, s);
  rtc.adjust(rtcNow);
  lastMinute = rtcNow.minute();  // don't announce just because the minute changed
  Serial.println("RTC time set from the phone.");
  if (screen == SCREEN_HOME) drawHome();
  sendStatus();
}

void handleTest() {
  if (!authorized()) return;
  if (!isPlayerIdle()) {
    sendError(409, "Already playing");
    return;
  }
  playTestSong();
  sendJson("{\"message\":\"Playing the test song\"}");
}

void handleAnnounce() {
  if (!authorized()) return;
  if (!isPlayerIdle()) {
    sendError(409, "Already playing");
    return;
  }
  playAnnouncement(rtcNow, -1);
  sendJson("{\"message\":\"Playing the announcement\"}");
}

void handleStop() {
  if (!authorized()) return;
  stopPlayback();
  sendJson("{\"message\":\"Stopped\"}");
}

void handleRescan() {
  if (!authorized()) return;
  if (!mp3Ok) {
    sendError(503, "MP3 module not found");
    return;
  }
  if (!isPlayerIdle()) {
    sendError(409, "Wait until playing has finished");
    return;
  }
  countAllFolders();
  sendState();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ============================================================
// START AND RUN
// ============================================================

// Serial Monitor lines when a phone joins or leaves the hotspot. Reason 3 or 8 means the phone
// left by itself (for example, it found no internet and switched networks); other reasons
// mean the clock lost the phone (weak signal or interference). Runs in the Wi-Fi event task.
void onHotspotEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    Serial.printf("Phone joined the hotspot (%d connected)\n", WiFi.softAPgetStationNum());
  } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    Serial.printf("Phone left the hotspot, reason %d (%d still connected)\n",
                  info.wifi_ap_stadisconnected.reason, WiFi.softAPgetStationNum());
  }
}

void startWebSettings() {
  WiFi.onEvent(onHotspotEvent);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(HOTSPOT_NAME, HOTSPOT_PASSWORD, HOTSPOT_CHANNEL)) {
    Serial.println("Hotspot did not start (the password needs at least 8 characters)");
    return;
  }
  if (!WiFi.setTxPower(WIFI_TX_POWER)) Serial.println("Could not set the Wi-Fi power");
  snprintf(hotspotIp, sizeof(hotspotIp), "%s", WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, handlePage);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/settings", HTTP_POST, handleSaveSettings);
  server.on("/api/schedules", HTTP_POST, handleSaveSchedules);
  server.on("/api/time", HTTP_POST, handleSetTime);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/announce", HTTP_POST, handleAnnounce);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/rescan", HTTP_POST, handleRescan);
  server.onNotFound(handleNotFound);
  server.begin();
  webStarted = true;
  Serial.printf("Settings page: join Wi-Fi \"%s\" and open http://%s\n", HOTSPOT_NAME, hotspotIp);
}

void webLoop() {
  if (webStarted) server.handleClient();
}
