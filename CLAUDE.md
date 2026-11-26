# Project rules

## Keep the playback order documented

When the announcement clip order changes, update the docs in the same change:

- ESP32 (`playAnnouncement()` in `esp32-time-teller-clock/esp32-time-teller-clock.ino`):
  update the "Playback order" table in `esp32-time-teller-clock/README.md`, the order
  comment at the top of the `.ino`, and the "Church name clip" row (or another row) in the
  "Other changes from the Pi version" table if the ESP32 order now differs from the Pi.
- Raspberry Pi (the announcement function in `time-teller-clock-program.py`):
  update the "Playback sequence" table in `README.md`, plus the ESP32 difference note under it.
