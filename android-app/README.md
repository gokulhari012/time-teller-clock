# Time Teller — Android app

A small Android app for the ESP32 version of the clock. It shows how to connect the phone to the clock, then opens the clock's settings page (`http://192.168.4.1`) inside the app. The page itself is served by the clock; see [Setting up from a phone](../esp32-time-teller-clock/README.md#setting-up-from-a-phone) for what it can do.

| File | What it is |
|------|------------|
| [TimeTeller.apk](TimeTeller.apk) | The app, ready to install (Android 5.0 and newer, about 50 KB) |
| [app/src/main/java/.../MainActivity.java](app/src/main/java/io/github/gokulhari012/timeteller/MainActivity.java) | All of the app's code |
| [app/src/main/res/values/config.xml](app/src/main/res/values/config.xml) | Clock address, Wi-Fi name and password shown in the app |
| [app/src/main/res/values/strings.xml](app/src/main/res/values/strings.xml) | Every text the app shows |
| [app/src/main/res/layout/activity_main.xml](app/src/main/res/layout/activity_main.xml) | The first screen |

---

## Installing it on a phone

1. Send `TimeTeller.apk` to the phone: WhatsApp, email, Google Drive or a USB cable all work. If the repository is on GitHub, you can also open `android-app/TimeTeller.apk` there on the phone and tap **Download**.
2. Tap the file on the phone. Android asks to allow installing apps from that source (for example *Files* or *WhatsApp*). Allow it; you only need to do this once.
3. Google Play Protect may warn that the app is from an unknown developer, because it is not from the Play Store. Choose **More details → Install anyway**.

The app appears as **Time Teller** with a blue clock icon.

**Updating:** install a newer `TimeTeller.apk` the same way. It replaces the old app and keeps the saved page password.

**iPhone:** there is no iPhone app. Open `http://192.168.4.1` in Safari and use **Share → Add to Home Screen**, as described in the [ESP32 README](../esp32-time-teller-clock/README.md#connecting).

---

## Using it

The app opens on a screen with two steps and a status box at the top:

1. **Connect to the clock's Wi-Fi**: *TimeTeller*, password *12345678*. **Open Wi-Fi settings** goes straight to the phone's Wi-Fi list. If the phone says the Wi-Fi has no internet, choose to stay connected.
2. **Turn off mobile data**, the suggested step. **Open mobile data settings** shows the mobile data switch; swiping down from the top of the screen and tapping *Mobile data* works too. Turn it back on when you are done.

The status box keeps checking whether the clock answers:

| Status | Meaning |
|--------|---------|
| Looking for the clock… | Checking now |
| ✓ Connected to the clock | Ready. Tap **Open clock settings** |
| ! Not connected to any Wi-Fi | Do step 1 |
| ! The clock is not answering | The phone is on a Wi-Fi, but not the clock's, or the clock is off |

**Open clock settings** opens the settings page in the app. The first time, the page asks for the settings password (*12345678*) and then remembers it. If the clock can't be found, the app says so and offers **Try again** or **Open anyway**.

On the settings page, **Back** returns to the steps screen. If there are changes that were not saved, the app asks first.

### What the app does for you

- **Uses the Wi-Fi even when mobile data is on.** The clock's Wi-Fi has no internet, so Android normally prefers mobile data and the page never loads. The app sends its own traffic through the Wi-Fi. Turning off mobile data is still suggested: some phones leave a Wi-Fi without internet on their own.
- **Always loads the clock's current page.** Nothing is cached, so after a firmware update the new page shows at once.
- **Stops asking the clock for its status** when the app is in the background.
- **Keeps the page when the phone is turned sideways**, including unsaved changes.
- **Follows the phone's dark mode**, in the same colours as the settings page.

---

## Changing the app

You need [Android Studio](https://developer.android.com/studio) (free). Open the `android-app` folder with **File → Open**. The first time, it downloads Gradle and the Android SDK parts it needs.

### If you change the clock's Wi-Fi name or passwords

The app only *shows* the Wi-Fi name and password as instructions, and the page asks for its own password, so nothing breaks if they change. To show the new ones, edit [config.xml](app/src/main/res/values/config.xml) and build again:

```xml
<string name="clock_url" translatable="false">http://192.168.4.1/</string>
<string name="wifi_name" translatable="false">TimeTeller</string>
<string name="wifi_password" translatable="false">12345678</string>
```

These match `HOTSPOT_NAME` and `HOTSPOT_PASSWORD` in the sketch's CONFIG section. The address only changes if you change the ESP32's hotspot address in the sketch.

### Building a new TimeTeller.apk

1. In [app/build.gradle](app/build.gradle), raise `versionCode` by 1 (for example `1` → `2`) and set `versionName` (for example `'1.1'`). Android refuses to replace an app with a lower `versionCode`, so raise it for every new APK.
2. Build the release APK: in Android Studio's terminal, run `gradlew assembleRelease`, or use **Build → Generate App Bundles or APKs → Generate APKs**.
3. Copy `app/build/outputs/apk/release/app-release.apk` over `TimeTeller.apk`.

### The signing key

Android only installs an update over an app that was signed with the **same key**. This app is signed with `signing/time-teller.jks`. Its password is in [app/build.gradle](app/build.gradle).

- The key is **not in git** (see [.gitignore](.gitignore)). Keep a copy somewhere safe, for example next to your other project backups.
- If the key is missing, the build still works but uses the computer's debug key. Phones that have the old app must then **uninstall it first** and install the new one.
- If you ever want the key in git (so any computer can build updates), anyone with the repository could then sign an app as "Time Teller". That is a small risk for this app, but keep it in mind if the repository is public.

### Settings in app/build.gradle

| Setting | Value | Meaning |
|---------|-------|---------|
| `applicationId` | `io.github.gokulhari012.timeteller` | The app's unique name on the phone. Changing it installs a second, separate app |
| `minSdk` | 21 | Oldest Android version: 5.0 |
| `targetSdk` | 36 | Built for Android 16 |
| `versionCode` / `versionName` | 1 / `1.0` | Raise for every new APK (see above) |

---

## Troubleshooting

| Problem | What to do |
|---------|------------|
| "App not installed" when updating | The new APK was signed with a different key (see [The signing key](#the-signing-key)). Uninstall the old app, then install the new one |
| "The clock is not answering" but the phone is on TimeTeller | Check the clock is on and its LCD works. Turn Wi-Fi off and on again, or walk closer to the clock |
| Saving on the page shows *Clock not reachable* | The phone left the clock's Wi-Fi, often because mobile data is on. Turn mobile data off, reconnect, then save again. The changes are still on the page until you leave it |
| The phone keeps switching back to the church Wi-Fi | Android prefers a Wi-Fi with internet. Tap **TimeTeller** in the Wi-Fi list again, or tell the phone to forget the other Wi-Fi while you are at the clock |
| Android System WebView is missing or updating | The app uses the phone's built-in web viewer. Wait a minute, or update *Android System WebView* in the Play Store |
