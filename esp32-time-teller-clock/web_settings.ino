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
 *   POST /api/passwords  change the admin and user passwords
 *
 * Every /api/ request needs a login (HTTP Basic auth): user "admin" with the admin
 * password, or user "user" with the user password. A user can't change Sound,
 * Language and Season based songs, and can't use /api/schedules, /api/rescan and
 * /api/passwords or see the SD card folders and clock health.
 * The handlers run inside loop(), so they never overlap the clock or the LCD.
 */

#include "web_page.h"

const char* ADMIN_LOGIN = "admin";  // login names; the passwords are in settings
const char* USER_LOGIN  = "user";

WebServer server(80);
bool webStarted = false;
bool requestIsAdmin = false;  // the request being answered logged in as admin

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

// Checks the login (admin or user). Without it, answers 401 without a WWW-Authenticate
// header, so the phone shows the page's own login box instead of a browser pop-up.
bool authorized() {
  requestIsAdmin = server.authenticate(ADMIN_LOGIN, settings.adminPassword);
  if (requestIsAdmin || server.authenticate(USER_LOGIN, settings.userPassword)) return true;
  sendError(401, "Wrong password");
  return false;
}

// For requests only the admin may make: answers 403 to a user login
bool adminOnly() {
  if (!authorized()) return false;
  if (requestIsAdmin) return true;
  sendError(403, "Only the admin can do this");
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

// Reads the specific minutes of a Program ("HHMMFF" entries joined with commas,
// FF = ExactFlag bits as two hex digits; empty = none). Answers 400 and returns false if the text is not valid.
bool readExactTimes(const char* name, Program& p) {
  String text = server.arg(name);
  uint8_t count = 0;
  size_t i = 0;
  bool ok = true;
  while (ok && i < text.length()) {
    ok = count < MAX_EXACT_TIMES && i + 6 <= text.length();
    for (size_t k = 0; ok && k < 4; k++) ok = isdigit((unsigned char)text[i + k]);
    if (!ok) break;
    uint8_t hour = (text[i] - '0') * 10 + (text[i + 1] - '0');
    uint8_t minute = (text[i + 2] - '0') * 10 + (text[i + 3] - '0');
    char flag[3] = { text[i + 4], text[i + 5], '\0' };
    ok = hour < 24 && minute < 60 && isxdigit((unsigned char)flag[0]) && isxdigit((unsigned char)flag[1]);
    if (!ok) break;
    p.exact[count++] = { hour, minute, (uint8_t)(strtol(flag, nullptr, 16) & EXACT_ALL) };
    i += 6;
    if (i < text.length()) {
      ok = text[i] == ',';
      i++;
    }
  }
  if (!ok) {
    sendError(400, String("Bad value for ") + name);
    return false;
  }
  p.exactCount = count;
  return true;
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
const size_t PROGRAM_JSON_SIZE  = 320;  // one Program: five time lists and 20 specific minutes
const size_t SETTINGS_JSON_SIZE = 440 + PROGRAM_COUNT * PROGRAM_JSON_SIZE;

void addStatusJson(String& json) {
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"time\":\"%04d-%02d-%02dT%02d:%02d:%02d\",\"playing\":%d,\"label\":\"%s\",\"mp3\":%d,"
           "\"language\":%d,\"role\":\"%s\",",
           rtcNow.year(), rtcNow.month(), rtcNow.day(), rtcNow.hour(), rtcNow.minute(), rtcNow.second(),
           isPlayerIdle() ? 0 : 1, playingLabel ? playingLabel : "", mp3Ok ? 1 : 0,
           settings.language, requestIsAdmin ? "admin" : "user");
  json += buf;
  if (!requestIsAdmin) {  // clock health and SD card folders are for the admin only
    json += "\"folders\":[]}";
    return;
  }
  snprintf(buf, sizeof(buf), "\"heap\":%lu,\"minHeap\":%lu,\"uptime\":%lu,\"reset\":\"%s\",\"folders\":[",
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

// Specific minutes as "HHMMFF" entries joined with commas (FF = ExactFlag bits as two hex digits)
void exactToText(const Program& p, String& json) {
  char entry[9];
  for (uint8_t i = 0; i < p.exactCount; i++) {
    snprintf(entry, sizeof(entry), "%s%02d%02d%02x", i ? "," : "", p.exact[i].hour, p.exact[i].minute, p.exact[i].flags & EXACT_ALL);
    json += entry;
  }
}

// One Program: t, h, s, b, p = Teller, Happy, Sunday, Blessing and Promise quarter-hours;
// x = specific minutes
void addProgramJson(String& json, const Program& p) {
  char teller[25], happy[25], sunday[25], blessing[25], promise[25], buf[170];
  slotsToHex(p.tellerTimes, teller);
  slotsToHex(p.happyTimes, happy);
  slotsToHex(p.sundayTimes, sunday);
  slotsToHex(p.blessingTimes, blessing);
  slotsToHex(p.promiseTimes, promise);
  snprintf(buf, sizeof(buf), "{\"t\":\"%s\",\"h\":\"%s\",\"s\":\"%s\",\"b\":\"%s\",\"p\":\"%s\",\"x\":\"",
           teller, happy, sunday, blessing, promise);
  json += buf;
  exactToText(p, json);
  json += "\"}";
}

void addSettingsJson(String& json) {
  char buf[480];
  json += "{\"programs\":[";
  for (uint8_t i = 0; i < PROGRAM_COUNT; i++) {
    if (i) json += ",";
    addProgramJson(json, settings.programs[i]);
  }
  snprintf(buf, sizeof(buf),
           "],\"program\":%d,\"language\":%d,\"silenceOn\":%d,\"silenceStart\":%d,"
           "\"silenceEnd\":%d,\"months\":%d,\"churchName\":%d,\"extraQuotes\":%d,\"speaker\":%d,"
           "\"volMorning\":%d,\"volEvening\":%d,\"morningFrom\":%d,\"eveningFrom\":%d,"
           "\"showPhone\":%d,\"phone\":\"%s\",\"startName\":\"%s\",\"mp3Relay\":%d,\"seasons\":%d}",
           settings.program, settings.language, settings.silenceOn, settings.silenceStart, settings.silenceEnd,
           settings.monthlySongs, settings.churchName, settings.extraQuotes, settings.speakerOutput,
           settings.morningVolume, settings.eveningVolume, settings.morningFrom, settings.eveningFrom,
           settings.showPhone, settings.phone, settings.startName, settings.mp3Relay, settings.seasons);
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
  json.reserve(SETTINGS_JSON_SIZE + scheduleCount * SCHEDULE_JSON_SIZE + STATUS_JSON_SIZE);
  json += "{\"settings\":";
  addSettingsJson(json);
  json += ",\"schedules\":";
  if (requestIsAdmin) {
    addSchedulesJson(json);
  } else {
    json += "[]";  // schedules are for the admin only
  }
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
  // Program i is sent as p<i>t, p<i>h, p<i>s, p<i>b, p<i>p (quarter-hours) and p<i>x (specific minutes)
  char key[6];
  const char kinds[] = "thsbpx";
  for (uint8_t i = 0; i < PROGRAM_COUNT; i++) {
    Program& p = updated.programs[i];
    SlotSet* lists[5] = { &p.tellerTimes, &p.happyTimes, &p.sundayTimes, &p.blessingTimes, &p.promiseTimes };
    for (uint8_t k = 0; k < 6; k++) {
      snprintf(key, sizeof(key), "p%d%c", i, kinds[k]);
      if (k < 5 ? !readSlots(key, *lists[k]) : !readExactTimes(key, p)) return;
    }
  }
  if (!readNumber("program", 0, PROGRAM_COUNT - 1, v)) return;
  updated.program = v;
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
  // Sound, Language and Season based songs: admin only (a user's values are ignored)
  if (requestIsAdmin) {
    if (!readNumber("language", 0, LANGUAGE_COUNT - 1, v)) return;
    updated.language = v;
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
    if (server.hasArg("seasons")) {
      if (!readNumber("seasons", 0, ALL_SEASONS, v)) return;
      updated.seasons = v;
    }
  }
  if (server.hasArg("mp3Relay")) {
    if (!readNumber("mp3Relay", 0, 1, v)) return;
    updated.mp3Relay = v;
  }
  // Startup screen: optional, so an older page that doesn't send these still saves
  if (server.hasArg("showPhone")) {
    if (!readNumber("showPhone", 0, 1, v)) return;
    updated.showPhone = v;
  }
  if (server.hasArg("phone")) cleanName(server.arg("phone"), updated.phone, sizeof(updated.phone));
  if (server.hasArg("startName")) cleanName(server.arg("startName"), updated.startName, sizeof(updated.startName));

  bool languageChanged = updated.language != settings.language;
  settings = updated;
  saveSettings();
  if (!settings.speakerOutput) setRelay(false);
  updateMp3Relay();
  if (languageChanged) {
    languageCountPending = true;
    if (mp3Ok && isPlayerIdle()) countAllFolders();  // so the answer lists the new language's folders
  }
  redrawLcdMenu();
  sendState();
}

void handleSaveSchedules() {
  if (!adminOnly()) return;
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
    // Song number: a file 1-255 in Custom_songs, or a Custom_songs sub-folder 1-9
    long low = s.songType == SONG_NONE ? 0 : 1;
    long high = s.songType == SONG_FOLDER ? MAX_CUSTOM_FOLDERS : 255;
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
  if (!adminOnly()) return;
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

// A password: 4-16 characters the LCD can show, no spaces (the LCD menu "Web Passwords" shows it)
bool readPassword(const char* name, char* out, size_t size) {
  String text = server.arg(name);
  if (text.length() == 0) return true;  // empty = keep the current password
  bool ok = text.length() >= 4 && text.length() < size;
  for (size_t i = 0; ok && i < text.length(); i++) ok = text[i] >= 33 && text[i] <= 126;
  if (!ok) {
    sendError(400, "A password needs 4-16 letters, numbers or signs, without spaces");
    return false;
  }
  snprintf(out, size, "%s", text.c_str());
  return true;
}

void handlePasswords() {
  if (!adminOnly()) return;
  char admin[sizeof(settings.adminPassword)], user[sizeof(settings.userPassword)];
  memcpy(admin, settings.adminPassword, sizeof(admin));
  memcpy(user, settings.userPassword, sizeof(user));
  if (!readPassword("admin", admin, sizeof(admin)) || !readPassword("user", user, sizeof(user))) return;
  memcpy(settings.adminPassword, admin, sizeof(admin));
  memcpy(settings.userPassword, user, sizeof(user));
  saveSettings();
  sendJson("{\"message\":\"Passwords saved\"}");
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
  server.on("/api/passwords", HTTP_POST, handlePasswords);
  server.onNotFound(handleNotFound);
  server.begin();
  webStarted = true;
  Serial.printf("Settings page: join Wi-Fi \"%s\" and open http://%s\n", HOTSPOT_NAME, hotspotIp);
}

void webLoop() {
  if (webStarted) server.handleClient();
}
