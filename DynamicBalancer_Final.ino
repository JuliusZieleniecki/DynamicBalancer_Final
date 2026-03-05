/*
  Dynamic Spin Balancer â€“ Final ESP32-WROOM-32 Firmware (Arduino)
  - ESC + sensorless BLDC (no precise motor positioning required)
  - AS5047 absolute angle sensor (no magnet index)
  - MPU6050 accel (Y axis default)
  - Manual rotate-by-hand with LED targeting (0Â°, heavy, add, remove, custom target)
  - Hosts web UI from LittleFS (fallback embedded UI if missing)
  - WebSocket telemetry + REST API (profiles/sessions/settings/wifi)
  - mDNS hostname: http://balance.local (STA mode)   (ESPmDNS)  :contentReference[oaicite:2]{index=2}
  - AP fallback (BalancerSetup) with config portal if STA fails

  Required Arduino libraries:
    - ESPAsyncWebServer
    - AsyncTCP
    - ArduinoJson (v6)
    - ESP32 board package (espressif)
    - I2Cdev + MPU6050
    - AS5X47
    - ESP32_Servo

  Notes:
    - The original me-no-dev ESPAsyncWebServer repo was archived; many people use maintained forks
      (e.g. ESPAsyncWebServer-esphome via PlatformIO). :contentReference[oaicite:3]{index=3}
*/

#include <WiFi.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <ArduinoJson.h>

#include <I2Cdev.h>
#include <MPU6050.h>
#include <AS5X47.h>
#include <ESP32Servo.h>

// Optional local-only credentials override. Define DB_DEFAULT_WIFI_SSID/PASS in credentials.h.
#if defined(__has_include)
#if __has_include("credentials.h")
#include "credentials.h"
#endif
#endif

#ifndef DB_DEFAULT_WIFI_SSID
#define DB_DEFAULT_WIFI_SSID ""
#endif

#ifndef DB_DEFAULT_WIFI_PASS
#define DB_DEFAULT_WIFI_PASS ""
#endif

// -------------------- Hardware pins (from your working code) --------------------
static const int PIN_LED = 16;
static const int PIN_ESC = 17;
static const int PIN_AS5047_CS = 5;

#if CONFIG_IDF_TARGET_ESP32S3
static const int PIN_I2C_SDA = 8;
static const int PIN_I2C_SCL = 9;
static const int PIN_SPI_SCK = 12;
static const int PIN_SPI_MISO = 13;
static const int PIN_SPI_MOSI = 11;
#else
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;
static const int PIN_SPI_SCK = 18;
static const int PIN_SPI_MISO = 19;
static const int PIN_SPI_MOSI = 23;
#endif

// -------------------- ESC pulse bounds --------------------
static const int ESC_MIN_US = 1000;
static const int ESC_MAX_US = 2000;
static const int ESC_ARM_US = 800;

// -------------------- WiFi / AP fallback --------------------
static const uint32_t WIFI_STA_TIMEOUT_MS = 12000;   // try STA for 12s, then AP
static const char* AP_SSID = "BalancerSetup";
static const char* AP_PASS = ""; // open AP (you can set a password if you want)
static const bool WIFI_AP_ALWAYS_ON = true; // keep recovery AP available even when STA is connected
static const char* DEFAULT_WIFI_SSID = DB_DEFAULT_WIFI_SSID;
static const char* DEFAULT_WIFI_PASS = DB_DEFAULT_WIFI_PASS;
static const float NOISE_FLOOR_MAX_G = 0.5f;
static const uint8_t SETTINGS_SCHEMA_VER = 1;

// -------------------- Server --------------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
Preferences prefs;

// -------------------- Sensors --------------------
MPU6050 mpu;
AS5X47 as5047(PIN_AS5047_CS);
Servo esc;

// -------------------- State model --------------------
enum MotorState : uint8_t { MOTOR_STOPPED=0, MOTOR_RUNNING=1, MOTOR_FAULT=2 };
enum RunStep   : uint8_t { STEP_IDLE=0, STEP_SPINUP=1, STEP_MEASURE=2, STEP_RESULTS=3 };
enum LedMode   : uint8_t { LED_OFF=0, LED_ZERO=1, LED_HEAVY=2, LED_ADD=3, LED_REMOVE=4, LED_TARGET=5 };

struct Telemetry {
  float rpm = 0.0f;
  float vibMag = 0.0f;     // 1Ã— vibration amplitude in g (converted from MPU6050 raw)
  float phaseDeg = 0.0f;   // 0..360 (peak of 1Ã— component)
  float quality = 0.0f;    // 0..1
  float temp = NAN;        // optional
  float noiseRms = 0.0f;   // RMS acceleration in g (converted from MPU6050 raw)
  uint32_t timestampMs = 0;

  float heavyDeg = 0.0f;   // by convention == phaseDeg
  float addDeg = 180.0f;   // by convention heavy+180
  float removeDeg = 0.0f;  // by convention heavy

  bool ledOn = false;      // physical LED state
  uint8_t ledMode = LED_TARGET;
  float ledTargetDeg = 0.0f;
};

struct State {
  uint8_t motorState = MOTOR_STOPPED;
  uint8_t runStep = STEP_IDLE;
  char profileName[32] = "idle";
  bool phaseGuidanceStale = false;
  bool hasActiveProfile = false;
  char activeProfileId[24] = "";
  float activeProfilePhaseOffsetDeg = 0.0f;
  char errors[192] = "";
};

static Telemetry g_telem;
static State g_state;
static portMUX_TYPE telemMux = portMUX_INITIALIZER_UNLOCKED;
static Telemetry g_resultSnapshot;
static bool g_hasResultSnapshot = false;
static bool g_resultHasProfilePhaseOffset = false;
static float g_resultProfilePhaseOffsetDeg = 0.0f;
static uint32_t g_currentRunId = 0;
static uint32_t g_resultRunId = 0;

// -------------------- Settings persisted in NVS --------------------
struct Settings {
  // Angle alignment
  float zeroOffsetDeg = 0.0f;   // subtract from raw AS5047 to align physical mark to 0Â°
  float windowDeg = 1.0f;       // LED turns on within +/- window

  // Correction geometry
  float correctionRadiusMm = 25.0f; // distance from rotation axis to correction weight placement (mm)

  // LED targeting
  uint8_t ledMode = LED_ADD;    // default: show add-weight spot
  float ledTargetDeg = 0.0f;    // only for LED_TARGET

  // Sampling + reporting
  float rpmEmaAlpha = 0.35f;
  uint32_t samplePeriodUs = 2000;      // ~500Hz loop
  uint32_t measureWindowMs = 3000;     // compute mag/phase over window
  float noiseFloorTarget = 0.0f;       // 0 = single-window mode; >0 = keep measuring until noiseRms <= target
  uint32_t wsPublishMs = 200;

  // Control
  int escIdleUs = 1000;
  int escMaxUs  = 1800;

  // â€œstable RPMâ€ check
  float rpmStableTol = 120.0f;          // +/- RPM threshold for â€œstable enoughâ€
  uint32_t rpmStableHoldMs = 900;       // must be stable for this long
};

static Settings g_set;

// -------------------- WiFi credentials in NVS --------------------
struct WifiCreds {
  String ssid;
  String pass;
};
static WifiCreds g_wifi;
static bool g_isAPMode = false;

struct WifiScanMatch {
  bool found = false;
  int32_t rssi = -127;
  int32_t channel = 0;
  uint8_t bssid[6] = {0,0,0,0,0,0};
};

// -------------------- Profiles persisted in LittleFS --------------------
struct Profile {
  String id;
  String name;
  uint16_t targetRpm;
  uint16_t spinupMs;
  uint16_t dwellMs;
  uint8_t repeats;
  float phaseOffsetDeg;
};
static std::vector<Profile> g_profiles;

// -------------------- Sessions persisted in LittleFS --------------------
static const char* SESS_DIR = "/sessions";
static const char* SESS_INDEX = "/sessions/index.json";

// -------------------- Balance math accumulators (synchronous detection) --------------------
struct Accum {
  double sumC = 0.0;
  double sumS = 0.0;
  double sumY = 0.0;
  double sumY2 = 0.0;
  uint32_t n = 0;
  uint32_t windowStartMs = 0;
};

static Accum acc;

// -------------------- Fast ANGLECOM reader (4 MHz, DAEC compensated) ----------
// Pre-computed SPI command frames for the AS5047P pipelined protocol.
// ANGLECOM (0x3FFF): cmd = (1<<14)|0x3FFF = 0x7FFF, parity bit makes 0xFFFF
// NOP      (0x0000): cmd = (1<<14)|0x0000 = 0x4000, parity bit makes 0xC000
static const uint16_t CMD_ANGLECOM = 0xFFFF;
static const uint16_t CMD_NOP      = 0xC000;

static inline float readAngleComFast(uint16_t* raw14Out = nullptr) {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE1));
  digitalWrite(PIN_AS5047_CS, LOW);
  SPI.transfer16(CMD_ANGLECOM);
  digitalWrite(PIN_AS5047_CS, HIGH);
  delayMicroseconds(1);  // t_CSn minimum high time (350 ns typ)
  digitalWrite(PIN_AS5047_CS, LOW);
  uint16_t raw = SPI.transfer16(CMD_NOP);
  digitalWrite(PIN_AS5047_CS, HIGH);
  SPI.endTransaction();
  uint16_t raw14 = (uint16_t)(raw & 0x3FFF);
  if (raw14Out) *raw14Out = raw14;
  return (float)raw14 * (360.0f / 16384.0f);
}

// -------------------- Angle/RPM tracking --------------------
static float lastAngleDeg = 0.0f;
static uint32_t lastWrapMicros = 0;
static float rpmEMA = 0.0f;

// Delta-angle windowed RPM accumulator
static float    rpmAccumDeg    = 0.0f;     // accumulated signed degrees in current window
static uint32_t rpmAccumStartUs = 0;       // start of current RPM window
static const uint32_t RPM_WINDOW_US = 50000; // 50 ms RPM computation window

// -------------------- Diagnostics (shared with net task) --------------------
static volatile int16_t  g_sweepEscUs   = 0;     // >0 = direct ESC override (bypasses PI)
static volatile uint32_t g_wrapCount    = 0;      // total zero-crossings since boot
static volatile float    g_rawAngleDeg  = 0.0f;   // latest raw AS5047 reading
static volatile uint16_t g_rawAngle14bit = 0;     // latest ANGLECOM 14-bit value
static volatile uint32_t g_lastWrapDtUs = 0;      // Âµs between last two wraps
static volatile uint32_t g_sampleCount  = 0;      // total sensor loop iterations

// -------------------- Control state --------------------
static uint16_t g_targetRpm = 0;
static uint32_t g_spinupStartMs = 0;
static uint32_t g_spinupTimeoutMs = 2500;
static uint32_t g_measureStartMs = 0;
static uint32_t g_stableStartMs = 0;
static bool g_rpmStable = false;
static bool g_mpuReady = false;

// -------------------- Task handles --------------------
TaskHandle_t samplingTaskHandle = nullptr;
TaskHandle_t netTaskHandle = nullptr;

// ===================== Utility =====================
static inline float wrap360(float deg) {
  while (deg < 0) deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  return deg;
}
static inline float circDist(float a, float b) {
  float d = fabsf(wrap360(a - b));
  return (d > 180.0f) ? (360.0f - d) : d;
}
static inline float ema(float alpha, float latest, float stored) {
  return alpha*latest + (1.0f-alpha)*stored;
}

static Telemetry readTelemSnapshot() {
  Telemetry t;
  taskENTER_CRITICAL(&telemMux);
  t = g_telem;
  taskEXIT_CRITICAL(&telemMux);
  return t;
}

static void writeTelemSnapshot(const Telemetry& t) {
  taskENTER_CRITICAL(&telemMux);
  g_telem = t;
  taskEXIT_CRITICAL(&telemMux);
}

static State readStateSnapshot() {
  State s;
  taskENTER_CRITICAL(&telemMux);
  s = g_state;
  taskEXIT_CRITICAL(&telemMux);
  return s;
}

static void writeStateSnapshot(const State& s) {
  taskENTER_CRITICAL(&telemMux);
  g_state = s;
  taskEXIT_CRITICAL(&telemMux);
}

static void freezeResultSnapshot(const Telemetry& t) {
  taskENTER_CRITICAL(&telemMux);
  g_resultSnapshot = t;
  g_resultRunId = g_currentRunId;
  g_hasResultSnapshot = true;
  g_resultHasProfilePhaseOffset = g_state.hasActiveProfile;
  g_resultProfilePhaseOffsetDeg = g_state.hasActiveProfile ? g_state.activeProfilePhaseOffsetDeg : 0.0f;
  taskEXIT_CRITICAL(&telemMux);
}

static bool readResultSnapshot(
  Telemetry& outTelem,
  uint32_t& outResultRunId,
  uint32_t& outCurrentRunId,
  bool& outIsCurrent,
  bool& outHasProfilePhaseOffset,
  float& outProfilePhaseOffsetDeg
) {
  taskENTER_CRITICAL(&telemMux);
  bool hasSnapshot = g_hasResultSnapshot;
  outResultRunId = g_resultRunId;
  outCurrentRunId = g_currentRunId;
  outIsCurrent = hasSnapshot && (g_resultRunId == g_currentRunId);
  outHasProfilePhaseOffset = g_resultHasProfilePhaseOffset;
  outProfilePhaseOffsetDeg = g_resultProfilePhaseOffsetDeg;
  if (hasSnapshot) outTelem = g_resultSnapshot;
  taskEXIT_CRITICAL(&telemMux);
  return hasSnapshot;
}

static bool hasResultSnapshotFlag() {
  bool hasSnapshot = false;
  taskENTER_CRITICAL(&telemMux);
  hasSnapshot = g_hasResultSnapshot;
  taskEXIT_CRITICAL(&telemMux);
  return hasSnapshot;
}

static void setError(const char* msg) {
  State s = readStateSnapshot();
  strncpy(s.errors, msg, sizeof(s.errors)-1);
  s.errors[sizeof(s.errors)-1] = 0;
  writeStateSnapshot(s);
}
static void clearError() {
  State s = readStateSnapshot();
  s.errors[0] = 0;
  writeStateSnapshot(s);
}

static void logNvsWriteResult(const char* ns, const char* key, size_t bytesWritten) {
  if (bytesWritten == 0) {
    Serial.printf("[NVS] WARN write failed ns=%s key=%s\n", ns, key);
  }
}

// ===================== Settings / WiFi NVS =====================
static void loadSettings() {
  prefs.begin("balancer", true);
  g_set.zeroOffsetDeg = prefs.getFloat("zeroOffsetDeg", 0.0f);
  g_set.windowDeg     = prefs.getFloat("windowDeg", 1.0f);
  g_set.correctionRadiusMm = prefs.getFloat("corrRadMm", 25.0f);
  g_set.ledMode       = prefs.getUChar("ledMode", (uint8_t)LED_ADD);
  g_set.ledTargetDeg  = prefs.getFloat("ledTargetDeg", 0.0f);

  g_set.rpmEmaAlpha   = prefs.getFloat("rpmEmaAlpha", 0.35f);
  g_set.samplePeriodUs = prefs.getUInt("samplePeriodUs", 2000);
  g_set.measureWindowMs = prefs.getUInt("measureWindowMs", 3000);
  g_set.noiseFloorTarget = prefs.getFloat("noiseFloorTgt", 0.0f);
  g_set.wsPublishMs   = prefs.getUInt("wsPublishMs", 200);
  uint8_t settingsVer = prefs.getUChar("setVer", 0);

  g_set.escIdleUs     = prefs.getInt("escIdleUs", 1000);
  g_set.escMaxUs      = prefs.getInt("escMaxUs", 1800);

  g_set.rpmStableTol  = prefs.getFloat("rpmStableTol", 120.0f);
  g_set.rpmStableHoldMs = prefs.getUInt("rpmStableHoldMs", 900);
  prefs.end();

  g_set.escIdleUs = constrain(g_set.escIdleUs, ESC_MIN_US, ESC_MAX_US);
  g_set.escMaxUs  = constrain(g_set.escMaxUs, ESC_MIN_US, ESC_MAX_US);
  g_set.windowDeg = constrain(g_set.windowDeg, 0.1f, 10.0f);
  g_set.correctionRadiusMm = constrain(g_set.correctionRadiusMm, 1.0f, 500.0f);
  // Migrate legacy raw-ADC target values to g once, then persist schema version.
  if (settingsVer < SETTINGS_SCHEMA_VER && g_set.noiseFloorTarget > NOISE_FLOOR_MAX_G) {
    g_set.noiseFloorTarget = g_set.noiseFloorTarget / 16384.0f;
  }
  if (isnan(g_set.noiseFloorTarget) || isinf(g_set.noiseFloorTarget)) {
    g_set.noiseFloorTarget = 0.0f;
  }
  g_set.noiseFloorTarget = constrain(g_set.noiseFloorTarget, 0.0f, NOISE_FLOOR_MAX_G);
  g_set.measureWindowMs = constrain(g_set.measureWindowMs, 200UL, 15000UL);
  g_set.samplePeriodUs = constrain(g_set.samplePeriodUs, 750UL, 100000UL);
  g_set.wsPublishMs = constrain(g_set.wsPublishMs, 50UL, 10000UL);
  g_set.rpmEmaAlpha = constrain(g_set.rpmEmaAlpha, 0.01f, 1.0f);
  g_set.rpmStableTol = constrain(g_set.rpmStableTol, 10.0f, 1000.0f);
  g_set.rpmStableHoldMs = constrain(g_set.rpmStableHoldMs, 100UL, 30000UL);

  if (settingsVer < SETTINGS_SCHEMA_VER) {
    prefs.begin("balancer", false);
    logNvsWriteResult("balancer", "noiseFloorTgt", prefs.putFloat("noiseFloorTgt", g_set.noiseFloorTarget));
    logNvsWriteResult("balancer", "setVer", prefs.putUChar("setVer", SETTINGS_SCHEMA_VER));
    prefs.end();
  }
}

static void saveSettings() {
  prefs.begin("balancer", false);
  logNvsWriteResult("balancer", "zeroOffsetDeg", prefs.putFloat("zeroOffsetDeg", g_set.zeroOffsetDeg));
  logNvsWriteResult("balancer", "windowDeg", prefs.putFloat("windowDeg", g_set.windowDeg));
  logNvsWriteResult("balancer", "corrRadMm", prefs.putFloat("corrRadMm", g_set.correctionRadiusMm));
  logNvsWriteResult("balancer", "ledMode", prefs.putUChar("ledMode", (uint8_t)g_set.ledMode));
  logNvsWriteResult("balancer", "ledTargetDeg", prefs.putFloat("ledTargetDeg", g_set.ledTargetDeg));

  logNvsWriteResult("balancer", "rpmEmaAlpha", prefs.putFloat("rpmEmaAlpha", g_set.rpmEmaAlpha));
  logNvsWriteResult("balancer", "samplePeriodUs", prefs.putUInt("samplePeriodUs", g_set.samplePeriodUs));
  logNvsWriteResult("balancer", "measureWindowMs", prefs.putUInt("measureWindowMs", g_set.measureWindowMs));
  logNvsWriteResult("balancer", "noiseFloorTgt", prefs.putFloat("noiseFloorTgt", g_set.noiseFloorTarget));
  logNvsWriteResult("balancer", "wsPublishMs", prefs.putUInt("wsPublishMs", g_set.wsPublishMs));
  logNvsWriteResult("balancer", "setVer", prefs.putUChar("setVer", SETTINGS_SCHEMA_VER));

  logNvsWriteResult("balancer", "escIdleUs", prefs.putInt("escIdleUs", g_set.escIdleUs));
  logNvsWriteResult("balancer", "escMaxUs", prefs.putInt("escMaxUs", g_set.escMaxUs));

  logNvsWriteResult("balancer", "rpmStableTol", prefs.putFloat("rpmStableTol", g_set.rpmStableTol));
  logNvsWriteResult("balancer", "rpmStableHoldMs", prefs.putUInt("rpmStableHoldMs", g_set.rpmStableHoldMs));
  prefs.end();
}

static void loadWifiCreds() {
  prefs.begin("wifi", true);
  g_wifi.ssid = prefs.getString("ssid", "");
  g_wifi.pass = prefs.getString("pass", "");
  prefs.end();
  if (g_wifi.ssid.length() == 0) {
    g_wifi.ssid = DEFAULT_WIFI_SSID;
    g_wifi.pass = DEFAULT_WIFI_PASS;
    Serial.println("[NVS] No saved WiFi creds, using defaults.");
  } else {
    Serial.printf("[NVS] Loaded saved WiFi creds: SSID=\"%s\"\n", g_wifi.ssid.c_str());
  }
}

static void saveWifiCreds(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  logNvsWriteResult("wifi", "ssid", prefs.putString("ssid", ssid));
  logNvsWriteResult("wifi", "pass", prefs.putString("pass", pass));
  prefs.end();
  g_wifi.ssid = ssid;
  g_wifi.pass = pass;
}

// ===================== WiFi diagnostics =====================
static const char* wifiStatusName(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:    return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL:  return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:      return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST:return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED:   return "WL_DISCONNECTED";
    default:                return "WL_UNKNOWN";
  }
}

static String bssidToString(const uint8_t* bssid) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  return String(buf);
}

static WifiScanMatch findBestScanMatchForSsid(const String& targetSsid) {
  WifiScanMatch bestAny;
  WifiScanMatch bestGlobal;
  int foundCount = 0;

  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true, false, 300, 0);
  Serial.printf("[WiFi] Scan complete: %d network(s) visible on 2.4GHz.\n", n);

  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    int32_t ch = WiFi.channel(i);
    const uint8_t* bssid = WiFi.BSSID(i);
    bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

    if (ssid == targetSsid) {
      ++foundCount;
      bool locallyAdministered = ((bssid[0] & 0x02) != 0);
      Serial.printf("[WiFi] Match SSID \"%s\": ch=%ld rssi=%ld enc=%s bssid=%s\n",
                    ssid.c_str(), (long)ch, (long)rssi, open ? "open" : "secured",
                    bssidToString(bssid).c_str());
      if (!bestAny.found || rssi > bestAny.rssi) {
        bestAny.found = true;
        bestAny.rssi = rssi;
        bestAny.channel = ch;
        memcpy(bestAny.bssid, bssid, 6);
      }
      if (!locallyAdministered && (!bestGlobal.found || rssi > bestGlobal.rssi)) {
        bestGlobal.found = true;
        bestGlobal.rssi = rssi;
        bestGlobal.channel = ch;
        memcpy(bestGlobal.bssid, bssid, 6);
      }
    }
  }

  WifiScanMatch best = bestGlobal.found ? bestGlobal : bestAny;
  if (foundCount == 0) {
    Serial.printf("[WiFi] SSID \"%s\" not present in 2.4GHz scan results.\n", targetSsid.c_str());
  } else {
    Serial.printf("[WiFi] Using %s match: ch=%ld rssi=%ld bssid=%s\n",
                  bestGlobal.found ? "global-BSSID" : "strongest",
                  (long)best.channel, (long)best.rssi, bssidToString(best.bssid).c_str());
  }
  WiFi.scanDelete();
  return best;
}

// ===================== File helpers =====================
static bool ensureDir(const char* path) {
  if (LittleFS.exists(path)) return true;
  return LittleFS.mkdir(path);
}

static bool writeTextFile(const char* path, const String& content) {
  const String tmpPath = String(path) + ".tmp";
  File f = LittleFS.open(tmpPath.c_str(), "w");
  if (!f) return false;
  size_t bytesWritten = f.print(content);
  f.close();

  if (bytesWritten != content.length()) {
    LittleFS.remove(tmpPath.c_str());
    return false;
  }
  if (LittleFS.exists(path) && !LittleFS.remove(path)) {
    LittleFS.remove(tmpPath.c_str());
    return false;
  }
  if (!LittleFS.rename(tmpPath.c_str(), path)) {
    LittleFS.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

static String readTextFile(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

static void cleanupTmpFilesInDir(const char* dirPath) {
  File dir = LittleFS.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }
  File entry = dir.openNextFile();
  while (entry) {
    String name = entry.name();
    bool isDir = entry.isDirectory();
    entry.close();
    if (isDir) {
      cleanupTmpFilesInDir(name.c_str());
    } else if (name.endsWith(".tmp")) {
      LittleFS.remove(name);
    }
    entry = dir.openNextFile();
  }
  dir.close();
}

static void cleanupTmpFiles() {
  cleanupTmpFilesInDir("/");
}

// ===================== Profiles (LittleFS) =====================
static const char* PROFILES_PATH = "/profiles.json";

static float profilePhaseOffsetInterpolated(uint16_t rpm) {
  // Profile-calibration anchors from 2026-03-03 trial-mass campaign.
  static const uint16_t rpmPts[] = {1750, 2600, 3600, 4600};
  static const float offPts[] = {134.74f, 101.28f, 134.33f, 108.83f};
  const int n = 4;

  if (rpm <= rpmPts[0]) return offPts[0];
  if (rpm >= rpmPts[n - 1]) return offPts[n - 1];

  for (int i = 0; i < n - 1; i++) {
    if (rpm <= rpmPts[i + 1]) {
      float frac = (float)(rpm - rpmPts[i]) / (float)(rpmPts[i + 1] - rpmPts[i]);
      return offPts[i] + frac * (offPts[i + 1] - offPts[i]);
    }
  }
  return offPts[n - 1];
}

static float seedProfilePhaseOffset(const String& id, uint16_t rpm) {
  if (id == "1750") return 134.74f;
  if (id == "2600") return 101.28f;
  if (id == "3600") return 134.33f;
  if (id == "4600") return 108.83f;
  return profilePhaseOffsetInterpolated(rpm);
}

static void defaultProfiles(std::vector<Profile>& out) {
  out.clear();
  // Targets matched to stable ESC commutation bands (sweep 2026-03-02)
  out.push_back({"1750","1750 RPM",1750,3000,5000,1,134.74f});
  out.push_back({"2600","2600 RPM",2600,4000,5000,1,101.28f});
  out.push_back({"3600","3600 RPM",3600,5000,5000,1,134.33f});
  out.push_back({"4600","4600 RPM",4600,6000,5000,1,108.83f});
}

static bool saveProfiles() {
  StaticJsonDocument<2048> doc;
  JsonArray arr = doc.createNestedArray("profiles");
  for (auto& p : g_profiles) {
    JsonObject o = arr.createNestedObject();
    o["id"] = p.id;
    o["name"] = p.name;
    o["rpm"] = p.targetRpm;
    o["spinupMs"] = p.spinupMs;
    o["dwellMs"] = p.dwellMs;
    o["repeats"] = p.repeats;
    o["phaseOffsetDeg"] = p.phaseOffsetDeg;
  }
  String out;
  serializeJson(doc, out);
  return writeTextFile(PROFILES_PATH, out);
}

static bool loadProfiles() {
  if (!LittleFS.exists(PROFILES_PATH)) {
    defaultProfiles(g_profiles);
    return saveProfiles();
  }
  String s = readTextFile(PROFILES_PATH);
  if (s.length() < 2) return false;

  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, s)) return false;

  g_profiles.clear();
  bool migratedPhaseOffset = false;
  JsonArray arr = doc["profiles"].as<JsonArray>();
  for (JsonObject o : arr) {
    Profile p;
    p.id = o["id"] | "";
    p.name = o["name"] | p.id.c_str();
    p.targetRpm = o["rpm"] | 2500;
    p.spinupMs  = o["spinupMs"] | 2500;
    p.dwellMs   = o["dwellMs"] | 3500;
    p.repeats   = o["repeats"] | 1;
    float seeded = seedProfilePhaseOffset(p.id, p.targetRpm);
    if (o.containsKey("phaseOffsetDeg")) {
      p.phaseOffsetDeg = o["phaseOffsetDeg"] | seeded;
      if (!isfinite(p.phaseOffsetDeg)) {
        p.phaseOffsetDeg = seeded;
        migratedPhaseOffset = true;
      }
      float constrained = constrain(p.phaseOffsetDeg, -180.0f, 180.0f);
      if (constrained != p.phaseOffsetDeg) {
        p.phaseOffsetDeg = constrained;
        migratedPhaseOffset = true;
      }
    } else {
      p.phaseOffsetDeg = seeded;
      migratedPhaseOffset = true;
    }
    if (p.id.length()) g_profiles.push_back(p);
  }
  if (g_profiles.empty()) {
    defaultProfiles(g_profiles);
    return saveProfiles();
  }
  if (migratedPhaseOffset) saveProfiles();
  return true;
}

static Profile* findProfileById(const String& id) {
  for (auto& p : g_profiles) if (p.id == id) return &p;
  return nullptr;
}

// ===================== Sessions (LittleFS) =====================
static bool initSessionsStore() {
  if (!ensureDir(SESS_DIR)) return false;
  if (!LittleFS.exists(SESS_INDEX)) {
    StaticJsonDocument<128> doc;
    doc["sessions"] = JsonArray();
    String out; serializeJson(doc, out);
    return writeTextFile(SESS_INDEX, "{\"sessions\":[]}");
  }
  return true;
}

static bool appendSessionIndex(const String& id, const String& name, uint32_t ts) {
  String idx = readTextFile(SESS_INDEX);
  if (idx.length() < 2) idx = "{\"sessions\":[]}";

  StaticJsonDocument<4096> doc;
  if (deserializeJson(doc, idx)) {
    doc.clear();
    doc["sessions"] = doc.createNestedArray("sessions");
  }
  JsonArray arr = doc["sessions"].as<JsonArray>();
  JsonObject o = arr.createNestedObject();
  o["id"] = id;
  o["name"] = name;
  o["timestamp"] = ts;

  String out;
  serializeJson(doc, out);
  return writeTextFile(SESS_INDEX, out);
}

static String sessionPath(const String& id) {
  return String(SESS_DIR) + "/" + id + ".json";
}

static bool saveSessionFile(const String& id, const String& json) {
  return writeTextFile(sessionPath(id).c_str(), json);
}

// ===================== ESC control =====================
static void escAttachIfNeeded() {
  if (!esc.attached()) esc.attach(PIN_ESC, 800, 2000);
}

static void motorStop() {
  escAttachIfNeeded();
  esc.writeMicroseconds(g_set.escIdleUs);
  bool hasSnapshot = hasResultSnapshotFlag();
  State s = readStateSnapshot();
  s.motorState = MOTOR_STOPPED;
  s.runStep = STEP_IDLE;
  s.phaseGuidanceStale = hasSnapshot;
  s.hasActiveProfile = false;
  s.activeProfileId[0] = 0;
  s.activeProfilePhaseOffsetDeg = 0.0f;
  writeStateSnapshot(s);

  g_targetRpm = 0;
  g_rpmStable = false;
  g_stableStartMs = 0;
}

static void motorStopKeepRunStep(uint8_t keepRunStep) {
  escAttachIfNeeded();
  esc.writeMicroseconds(g_set.escIdleUs);
  bool hasSnapshot = hasResultSnapshotFlag();
  State s = readStateSnapshot();
  s.motorState = MOTOR_STOPPED;
  s.runStep = keepRunStep;
  s.phaseGuidanceStale = hasSnapshot;
  s.hasActiveProfile = false;
  s.activeProfileId[0] = 0;
  s.activeProfilePhaseOffsetDeg = 0.0f;
  writeStateSnapshot(s);

  g_targetRpm = 0;
  g_rpmStable = false;
  g_stableStartMs = 0;
}

static int rpmToEscUsFeedforward(uint16_t rpm) {
  // Linear interpolation between measured operating points.
  // Sweep 2026-03-02: 1020->318, 1040->1711, 1060->2600, 1080->3604, 1100->4661
  // Entire usable ESC range is ~1020-1120 us (~54 RPM per us)
  if (rpm <= 300)  return 1020;
  if (rpm >= 5500) return 1115;
  // Piecewise linear from sweep data
  const int pts[][2] = {{300,1020},{1700,1040},{2600,1060},{3600,1080},{4700,1100},{5500,1115}};
  const int N = 6;
  for (int i = 0; i < N-1; i++) {
    if (rpm <= (uint16_t)pts[i+1][0]) {
      float frac = (float)(rpm - pts[i][0]) / (float)(pts[i+1][0] - pts[i][0]);
      return pts[i][1] + (int)(frac * (pts[i+1][1] - pts[i][1]));
    }
  }
  return 1070;
}

// PI controller around the feedforward with integrator reset support.
static float escPI_integ = 0.0f;
static uint32_t escPI_lastMs = 0;

static void escPIReset() {
  escPI_integ = 0.0f;
  escPI_lastMs = 0;
}

static int escPIController(uint16_t targetRpm, float measuredRpm) {

  uint32_t now = millis();
  float dt = (escPI_lastMs == 0) ? 0.05f : (now - escPI_lastMs) / 1000.0f;
  escPI_lastMs = now;
  dt = constrain(dt, 0.005f, 0.25f);

  float err = (float)targetRpm - measuredRpm;

  // Tuned to be â€œstable-firstâ€, not â€œsnappyâ€.
  const float KP = 0.015f;
  const float KI = 0.005f;

  escPI_integ += err * dt;
  escPI_integ = constrain(escPI_integ, -2000.0f, 2000.0f);

  int ff = rpmToEscUsFeedforward(targetRpm);
  int u = (int)roundf(ff + KP * err + KI * escPI_integ);

  u = constrain(u, ESC_MIN_US, g_set.escMaxUs);
  return u;
}

static void motorRunToRPM(uint16_t targetRpm) {
  escAttachIfNeeded();
  int us = escPIController(targetRpm, rpmEMA);
  esc.writeMicroseconds(us);

  State s = readStateSnapshot();
  s.motorState = MOTOR_RUNNING;
  writeStateSnapshot(s);
}

// ===================== LED targeting =====================
static float currentAdjustedAngle(float rawAngleDeg) {
  return wrap360(rawAngleDeg - g_set.zeroOffsetDeg);
}

static float ledTargetDegFromMode(const Telemetry& t) {
  switch ((LedMode)g_set.ledMode) {
    case LED_OFF:   return 0.0f;
    case LED_ZERO:  return 0.0f;
    case LED_HEAVY: return wrap360(t.heavyDeg);
    case LED_ADD:   return wrap360(t.addDeg);
    case LED_REMOVE:return wrap360(t.removeDeg);
    case LED_TARGET:return wrap360(g_set.ledTargetDeg);
    default:        return 0.0f;
  }
}

static bool computeLedOn(float rawAngleDeg, const Telemetry& t) {
  if ((LedMode)g_set.ledMode == LED_OFF) return false;
  float adj = currentAdjustedAngle(rawAngleDeg);
  float tgt = ledTargetDegFromMode(t);
  return (circDist(adj, tgt) <= g_set.windowDeg);
}

static void updatePhysicalLed(bool on) {
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

// ===================== Balance math =====================
static void resetAccum() {
  acc = Accum{};
  acc.windowStartMs = millis();
}

// MPU6050 at Â±2g full-scale range: 16384 LSB per g
static const double MPU_LSB_PER_G = 16384.0;

static void computeBalanceAndPublishWindow(uint32_t nowMs) {
  // Compute mag/phase from synchronous detection (in raw ADC units)
  double n = (acc.n > 0) ? (double)acc.n : 1.0;
  double C = acc.sumC / n;
  double S = acc.sumS / n;

  double magRaw = 2.0 * sqrt(C*C + S*S);   // 1Ã— amplitude in ADC counts
  double phase = atan2(S, C) * 180.0 / 3.141592653589793;
  float phaseDeg = wrap360((float)phase);

  // Noise estimate in raw ADC
  double meanY = acc.sumY / n;
  double varRaw = (acc.sumY2 / n) - (meanY * meanY);
  if (varRaw < 0.0) varRaw = 0.0;
  double rmsRaw = sqrt(varRaw);

  // Quality: unit-independent ratio (computed from raw values before conversion)
  float quality = 0.0f;
  if (rmsRaw > 1e-6) quality = constrain((float)(magRaw / (rmsRaw * 1.2)), 0.0f, 1.0f);

  // Convert to physical g for telemetry output
  double vibMag_g = magRaw / MPU_LSB_PER_G;
  double noiseRms_g = rmsRaw / MPU_LSB_PER_G;

  Telemetry t = readTelemSnapshot();
  t.rpm = rpmEMA;
  t.vibMag = (float)vibMag_g;
  t.phaseDeg = phaseDeg;
  t.quality = quality;
  t.temp = NAN;
  t.noiseRms = (float)noiseRms_g;
  t.timestampMs = nowMs;

  State s = readStateSnapshot();
  if (s.hasActiveProfile) {
    // Convention (single-plane) with active profile phase offset.
    float correctedPhase = wrap360(phaseDeg + s.activeProfilePhaseOffsetDeg);
    t.heavyDeg = correctedPhase;
    t.addDeg = wrap360(correctedPhase + 180.0f);
    t.removeDeg = correctedPhase;
    s.phaseGuidanceStale = false;
    writeStateSnapshot(s);
  }

  // Default behavior: after each window, aim LED at add-weight location
  // unless user explicitly set LED_TARGET mode.
  if ((LedMode)g_set.ledMode == LED_ADD) {
    // nothing needed
  }

  // Copy mode/target state into telemetry for UI â€œblue LEDâ€
  t.ledMode = g_set.ledMode;
  t.ledTargetDeg = g_set.ledTargetDeg;

  writeTelemSnapshot(t);
  resetAccum();
}

// ===================== WebSocket broadcast =====================
static void wsBroadcast() {
  Telemetry t = readTelemSnapshot();
  State s = readStateSnapshot();

  StaticJsonDocument<1024> doc;
  doc["type"] = "telemetry";

  JsonObject tel = doc.createNestedObject("telemetry");
  tel["rpm"] = t.rpm;
  tel["vibMag"] = t.vibMag;
  tel["phaseDeg"] = t.phaseDeg;
  tel["quality"] = t.quality;
  if (isnan(t.temp)) tel["temp"] = nullptr; else tel["temp"] = t.temp;
  tel["noiseRms"] = t.noiseRms;
  tel["timestamp"] = t.timestampMs;

  tel["heavyDeg"] = t.heavyDeg;
  tel["addDeg"] = t.addDeg;
  tel["removeDeg"] = t.removeDeg;

  tel["ledOn"] = t.ledOn;
  tel["ledMode"] = (int)t.ledMode;
  tel["ledTargetDeg"] = t.ledTargetDeg;

  JsonObject st = doc.createNestedObject("state");
  st["motorState"] = (int)s.motorState;
  st["profileName"] = s.profileName;
  st["runStep"] = (int)s.runStep;
  st["phaseGuidanceStale"] = s.phaseGuidanceStale;
  st["activeProfileId"] = s.hasActiveProfile ? s.activeProfileId : "";
  if (s.hasActiveProfile) st["activeProfilePhaseOffsetDeg"] = s.activeProfilePhaseOffsetDeg;
  else st["activeProfilePhaseOffsetDeg"] = nullptr;
  st["hasResultSnapshot"] = hasResultSnapshotFlag();

  JsonArray errs = st.createNestedArray("errors");
  if (strlen(s.errors)) errs.add(s.errors);

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
}

// ===================== Embedded diagnostic page =====================
static const char DIAG_PAGE_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>AS5047 / SPI Diagnostics</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:system-ui,-apple-system,sans-serif;background:#0f172a;color:#e2e8f0;padding:16px;max-width:900px;margin:auto}
  h1{font-size:1.4rem;margin-bottom:12px;color:#38bdf8}
  h2{font-size:1.1rem;margin:16px 0 8px;color:#7dd3fc;border-bottom:1px solid #334155;padding-bottom:4px}
  .card{background:#1e293b;border-radius:8px;padding:12px 16px;margin-bottom:12px}
  .grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:8px}
  .field{background:#0f172a;border-radius:6px;padding:8px 10px}
  .field .label{font-size:.7rem;color:#94a3b8;text-transform:uppercase;letter-spacing:.5px}
  .field .value{font-size:1.3rem;font-weight:600;font-variant-numeric:tabular-nums}
  .ok{color:#4ade80} .warn{color:#fbbf24} .err{color:#f87171} .muted{color:#64748b}
  .bar{height:18px;background:#334155;border-radius:4px;overflow:hidden;margin-top:4px}
  .bar-fill{height:100%;border-radius:4px;transition:width .3s}
  button{background:#2563eb;color:#fff;border:none;padding:8px 16px;border-radius:6px;cursor:pointer;font-size:.85rem;margin:4px}
  button:hover{background:#1d4ed8}
  button.danger{background:#dc2626}button.danger:hover{background:#b91c1c}
  #log{background:#020617;font-family:'Cascadia Code',monospace;font-size:.75rem;padding:10px;border-radius:6px;max-height:250px;overflow-y:auto;white-space:pre-wrap;color:#94a3b8;margin-top:8px}
  .status-dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-right:6px}
  .status-dot.alive{background:#4ade80;box-shadow:0 0 6px #4ade80}
  .status-dot.dead{background:#f87171;box-shadow:0 0 6px #f87171}
  .angle-ring{width:140px;height:140px;border-radius:50%;border:3px solid #334155;position:relative;margin:12px auto}
  .angle-needle{position:absolute;top:50%;left:50%;width:2px;height:60px;background:#f472b6;transform-origin:top center;border-radius:2px}
  .angle-center{position:absolute;top:50%;left:50%;width:8px;height:8px;background:#f472b6;border-radius:50%;transform:translate(-50%,-50%)}
  .angle-label{text-align:center;font-size:1.8rem;font-weight:700;color:#f472b6;margin-top:4px}
</style>
</head>
<body>
<h1><span class="status-dot dead" id="statusDot"></span> AS5047 SPI Diagnostics</h1>

<div class="card">
  <h2>Angle (live)</h2>
  <div style="display:flex;align-items:center;gap:24px;flex-wrap:wrap">
    <div>
      <div class="angle-ring" id="ring">
        <div class="angle-needle" id="needle" style="transform:rotate(0deg)"></div>
        <div class="angle-center"></div>
      </div>
      <div class="angle-label" id="angleDeg">--</div>
    </div>
    <div class="grid" style="flex:1">
      <div class="field"><div class="label">readAngle()</div><div class="value" id="fReadAngle">--</div></div>
      <div class="field"><div class="label">ANGLE reg (raw)</div><div class="value" id="fAngleRaw">--</div></div>
      <div class="field"><div class="label">ANGLECOM reg</div><div class="value" id="fAngleCom">--</div></div>
      <div class="field"><div class="label">RPM (EMA)</div><div class="value" id="fRpm">--</div></div>
      <div class="field"><div class="label">Wrap count</div><div class="value" id="fWraps">--</div></div>
      <div class="field"><div class="label">Loop rate</div><div class="value" id="fLoopHz">--</div></div>
    </div>
  </div>
</div>

<div class="card">
  <h2>SPI Health</h2>
  <div class="grid">
    <div class="field"><div class="label">AGC</div><div class="value" id="fAgc">--</div>
      <div class="bar"><div class="bar-fill" id="agcBar" style="width:0%;background:#38bdf8"></div></div></div>
    <div class="field"><div class="label">Magnitude (CMAG)</div><div class="value" id="fMag">--</div></div>
    <div class="field"><div class="label">Mag Low</div><div class="value" id="fMagl">--</div></div>
    <div class="field"><div class="label">Mag High</div><div class="value" id="fMagh">--</div></div>
    <div class="field"><div class="label">CORDIC Overflow</div><div class="value" id="fCof">--</div></div>
    <div class="field"><div class="label">LF (offset done)</div><div class="value" id="fLf">--</div></div>
  </div>
</div>

<div class="card">
  <h2>Error Flags</h2>
  <div class="grid">
    <div class="field"><div class="label">Framing Error</div><div class="value" id="fFrerr">--</div></div>
    <div class="field"><div class="label">Invalid Command</div><div class="value" id="fInvcomm">--</div></div>
    <div class="field"><div class="label">Parity Error</div><div class="value" id="fParerr">--</div></div>
    <div class="field"><div class="label">Error Flag (EF)</div><div class="value" id="fEf">--</div></div>
  </div>
</div>

<div class="card">
  <h2>SPI Pins</h2>
  <div class="grid">
    <div class="field"><div class="label">CS</div><div class="value" id="fCs">--</div></div>
    <div class="field"><div class="label">SCK</div><div class="value" id="fSck">--</div></div>
    <div class="field"><div class="label">MISO</div><div class="value" id="fMiso">--</div></div>
    <div class="field"><div class="label">MOSI</div><div class="value" id="fMosi">--</div></div>
  </div>
</div>

<div class="card">
  <h2>Profiles & Test Runner</h2>
  <div id="profileBtns" style="margin-bottom:8px">Loading profiles...</div>
  <div class="grid" style="margin-bottom:8px">
    <div class="field"><div class="label">Motor State</div><div class="value" id="tMotor">--</div></div>
    <div class="field"><div class="label">Run Step</div><div class="value" id="tStep">--</div></div>
    <div class="field"><div class="label">Profile</div><div class="value" id="tProfile">--</div></div>
    <div class="field"><div class="label">Guidance</div><div class="value" id="tGuidance">--</div></div>
    <div class="field"><div class="label">RPM</div><div class="value" id="tRpm">--</div></div>
    <div class="field"><div class="label">Vib (g)</div><div class="value" id="tVib">--</div></div>
    <div class="field"><div class="label">Phase</div><div class="value" id="tPhase">--</div></div>
    <div class="field"><div class="label">Heavy @</div><div class="value" id="tHeavy">--</div></div>
    <div class="field"><div class="label">Quality</div><div class="value" id="tQual">--</div></div>
  </div>
  <button class="danger" onclick="doStop()" style="font-size:1rem;padding:10px 24px">STOP MOTOR</button>
</div>

<div class="card">
  <h2>SPI Actions</h2>
  <button onclick="doReinit()">Force SPI Re-Init & Read</button>
  <button onclick="doSingleRead()">Single Read (no reinit)</button>
  <button id="liveToggleBtn" onclick="toggleLive()">Pause Live Updates</button>
  <div class="muted" id="pollStatus" style="margin:8px 0 6px">Live: ON (150ms) | SPI health: idle-only active (5s)</div>
  <div id="log">Ready. Live polling starts automatically.</div>
</div>

<script>
const $ = id => document.getElementById(id);
const LIVE_POLL_MS = 150;
const SPI_IDLE_MS = 5000;
const MOTOR_NAMES = ['STOPPED','RUNNING','FAULT'];
const STEP_NAMES  = ['IDLE','SPINUP','MEASURE','RESULTS'];

let liveTimer = null;
let spiTimer = null;
let liveEnabled = true;
let liveInFlight = false;
let spiInFlight = false;
let latestMotorState = 0;
let latestRunStep = 0;
let lastSampleCount = 0;
let lastSampleTime = 0;
let lastSpiErrLogMs = 0;
let lastLiveErrLogMs = 0;

function bool2html(v) {
  return v ? '<span class="err">YES</span>' : '<span class="ok">no</span>';
}
function boolOk(v) {
  return v ? '<span class="ok">YES</span>' : '<span class="muted">no</span>';
}
function hex14(v) {
  return '0x' + (v & 0x3FFF).toString(16).toUpperCase().padStart(4, '0');
}
function updatePollStatus() {
  const idle = (latestMotorState === 0 && latestRunStep === 0);
  const spiStatus = idle ? 'idle-only active (5s)' : 'paused during run';
  $('pollStatus').textContent = 'Live: ' + (liveEnabled ? 'ON (150ms)' : 'PAUSED') + ' | SPI health: ' + spiStatus;
  $('liveToggleBtn').textContent = liveEnabled ? 'Pause Live Updates' : 'Resume Live Updates';
}

function updateSpiHealth(d) {
  // AGC
  const agc = d.agc || 0;
  $('fAgc').textContent = agc + ' / 255';
  $('agcBar').style.width = (agc/255*100).toFixed(0) + '%';
  $('agcBar').style.background = agc > 200 ? '#f87171' : agc > 100 ? '#fbbf24' : '#4ade80';

  // Mag
  $('fMag').textContent = d.cmag || 0;
  $('fMagl').innerHTML = d.magl ? '<span class="err">TOO LOW</span>' : '<span class="ok">OK</span>';
  $('fMagh').innerHTML = d.magh ? '<span class="err">TOO HIGH</span>' : '<span class="ok">OK</span>';
  $('fCof').innerHTML = bool2html(d.cof);
  $('fLf').innerHTML = boolOk(d.lf);

  // Errors
  $('fFrerr').innerHTML = bool2html(d.errfl_frerr);
  $('fInvcomm').innerHTML = bool2html(d.errfl_invcomm);
  $('fParerr').innerHTML = bool2html(d.errfl_parerr);
  $('fEf').innerHTML = bool2html(d.errfl_ef);

  // Pins
  $('fCs').textContent = 'GPIO ' + (d.cs_pin ?? '?');
  $('fSck').textContent = 'GPIO ' + (d.sck_pin ?? '?');
  $('fMiso').textContent = 'GPIO ' + (d.miso_pin ?? '?');
  $('fMosi').textContent = 'GPIO ' + (d.mosi_pin ?? '?');
}

function log(msg) {
  const el = $('log');
  const ts = new Date().toLocaleTimeString();
  el.textContent += '\n[' + ts + '] ' + msg;
  el.scrollTop = el.scrollHeight;
}
function updateRunnerFromRaw(d) {
  latestMotorState = Number(d.motorState ?? latestMotorState);
  latestRunStep = Number(d.runStep ?? latestRunStep);
  const stale = Boolean(d.phaseGuidanceStale);
  const hasResult = Boolean(d.hasResultSnapshot);

  $('tMotor').textContent = MOTOR_NAMES[latestMotorState] || latestMotorState;
  $('tMotor').className = 'value ' + (latestMotorState === 1 ? 'warn' : latestMotorState === 2 ? 'err' : 'ok');
  $('tStep').textContent = STEP_NAMES[latestRunStep] || latestRunStep;
  $('tStep').className = 'value ' + (latestRunStep === 2 ? 'warn' : latestRunStep === 3 ? 'ok' : 'muted');
  $('tProfile').textContent = d.profileName || '--';
  if (!hasResult) {
    $('tGuidance').textContent = 'NO RESULTS';
    $('tGuidance').className = 'value muted';
  } else if (stale) {
    $('tGuidance').textContent = 'STALE';
    $('tGuidance').className = 'value warn';
  } else {
    $('tGuidance').textContent = 'ACTIVE';
    $('tGuidance').className = 'value ok';
  }
  $('tRpm').textContent = Number(d.rpm || d.rpmEMA || 0).toFixed(0);
  $('tVib').textContent = Number(d.vibMag || 0).toFixed(4);
  $('tPhase').textContent = Number(d.phaseDeg || 0).toFixed(1) + '\u00b0';
  $('tHeavy').textContent = Number(d.heavyDeg || 0).toFixed(1) + '\u00b0';
  $('tQual').textContent = (Number(d.quality || 0) * 100).toFixed(0) + '%';
}

function updateLiveFromRaw(d) {
  const angleComDeg = Number(d.anglecom_deg_live ?? d.rawAngleDeg ?? 0);
  const angleDeg = Number(d.angle_deg_live ?? angleComDeg);
  const angleRaw14 = Number(d.angle_raw14_live ?? d.anglecom_raw14_live ?? 0);
  const rpm = Number(d.rpmEMA ?? d.rpm ?? 0);

  $('angleDeg').textContent = angleComDeg.toFixed(2) + '\u00b0';
  $('needle').style.transform = 'rotate(' + angleComDeg + 'deg)';
  $('fReadAngle').textContent = angleDeg.toFixed(2) + '\u00b0';
  $('fAngleRaw').textContent = hex14(angleRaw14) + ' (' + (angleRaw14 & 0x3FFF) + ')';
  $('fAngleCom').textContent = angleComDeg.toFixed(2) + '\u00b0';
  $('fRpm').textContent = rpm.toFixed(1);
  $('fWraps').textContent = Number(d.wrapCount || 0);

  const now = Date.now();
  if (lastSampleCount > 0 && lastSampleTime > 0) {
    const dt = (now - lastSampleTime) / 1000;
    const ds = Number(d.sampleCount || 0) - lastSampleCount;
    if (dt > 0) $('fLoopHz').textContent = (ds / dt).toFixed(0) + ' Hz';
  }
  lastSampleCount = Number(d.sampleCount || 0);
  lastSampleTime = now;

  updateRunnerFromRaw(d);

  // Status dot reflects active sampling loop updates.
  $('statusDot').className = 'status-dot ' + (lastSampleCount > 0 ? 'alive' : 'dead');
}

async function doRead(reinit) {
  if (spiInFlight) return null;
  spiInFlight = true;
  try {
    const url = '/diag/spi_test' + (reinit ? '?reinit=1' : '');
    const r = await fetch(url);
    if (r.status === 409) {
      const d = await r.json();
      log('SPI read blocked while motor running.');
      return d;
    }
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const d = await r.json();
    updateSpiHealth(d);
    const allZero = (d.angle_raw === 0 && d.diagagc_raw === 0 && d.mag_raw === 0);
    log((reinit ? 'REINIT+READ' : 'READ') + ': angle=' + (d.readAngle||0).toFixed(2)
       + ' agc=' + (d.agc||0) + ' mag=' + (d.cmag||0) + ' errfl=0x' + (d.errfl_raw||0).toString(16)
       + (allZero ? '  ** ALL ZEROS - SPI DEAD **' : '  OK'));
    return d;
  } catch(e) {
    log('ERROR: ' + e.message);
    return null;
  } finally {
    spiInFlight = false;
    updatePollStatus();
  }
}

function doReinit() { doRead(true); }
function doSingleRead() { doRead(false); }

async function fetchLiveOnce() {
  if (!liveEnabled || liveInFlight) return;
  liveInFlight = true;
  try {
    const r = await fetch('/diag/raw');
    const d = await r.json();
    updateLiveFromRaw(d);
  } catch (e) {
    const now = Date.now();
    if (now - lastLiveErrLogMs >= 5000) {
      log('Live poll error: ' + e.message);
      lastLiveErrLogMs = now;
    }
  } finally {
    liveInFlight = false;
    updatePollStatus();
  }
}

async function fetchSpiIdleOnce() {
  // SPI health background refresh is idle-only.
  if (spiInFlight) return;
  const idle = (latestMotorState === 0 && latestRunStep === 0);
  if (!idle) {
    updatePollStatus();
    return;
  }
  spiInFlight = true;
  try {
    const r = await fetch('/diag/spi_test');
    if (r.status === 409) return;
    if (!r.ok) throw new Error('HTTP ' + r.status);
    const d = await r.json();
    updateSpiHealth(d);
  } catch (e) {
    const now = Date.now();
    if (now - lastSpiErrLogMs >= 30000) {
      log('SPI health poll error: ' + e.message);
      lastSpiErrLogMs = now;
    }
  } finally {
    spiInFlight = false;
    updatePollStatus();
  }
}

function toggleLive() {
  liveEnabled = !liveEnabled;
  if (liveEnabled) {
    fetchLiveOnce();
    log('Live polling resumed.');
  } else {
    log('Live polling paused.');
  }
  updatePollStatus();
}

function startDiagLoops() {
  if (!liveTimer) liveTimer = setInterval(fetchLiveOnce, LIVE_POLL_MS);
  if (!spiTimer) spiTimer = setInterval(fetchSpiIdleOnce, SPI_IDLE_MS);
  fetchLiveOnce();
  fetchSpiIdleOnce();
  updatePollStatus();
}

async function loadProfiles() {
  try {
    const r = await fetch('/profiles');
    const d = await r.json();
    const c = $('profileBtns');
    c.innerHTML = '';
    (d.profiles||[]).forEach(p => {
      const b = document.createElement('button');
      b.textContent = p.name || p.id;
      b.style.fontSize = '1rem';
      b.style.padding = '10px 20px';
      b.onclick = () => runProfile(p.id);
      c.appendChild(b);
    });
    if (!d.profiles || d.profiles.length === 0) c.textContent = 'No profiles found.';
  } catch(e) { $('profileBtns').textContent = 'Error loading profiles: ' + e.message; }
}

async function runProfile(id) {
  log('Starting profile: ' + id);
  try {
    const r = await fetch('/cmd/start_test', {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({profileId: id})});
    const d = await r.json();
    if (d.ok) log('Profile ' + id + ' started OK');
    else log('Start failed: ' + JSON.stringify(d));
  } catch(e) { log('Start error: ' + e.message); }
}

async function doStop() {
  log('Stopping motor...');
  try {
    await fetch('/cmd/stop', {method:'POST', body:'{}'});
    log('Motor stopped.');
  } catch(e) { log('Stop error: ' + e.message); }
}

// Initial load
loadProfiles();
startDiagLoops();
doSingleRead();
log('Live polling ON (150ms). SPI health idle-only (5s).');
window.addEventListener('beforeunload', () => {
  if (liveTimer) clearInterval(liveTimer);
  if (spiTimer) clearInterval(spiTimer);
});
</script>
</body>
</html>
)HTML";

// ===================== Embedded fallback UI (always available) =====================
static const char FALLBACK_INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width,initial-scale=1" />
<title>Dynamic Balancer</title>
<style>
  body{font-family:system-ui,Segoe UI,Arial;margin:20px;max-width:900px}
  .row{display:flex;gap:12px;flex-wrap:wrap}
  .card{border:1px solid #ddd;border-radius:12px;padding:12px;min-width:200px}
  button{padding:10px 12px;border-radius:10px;border:1px solid #333;background:#111;color:#fff}
  input{padding:10px;border-radius:10px;border:1px solid #ccc}
  .led{width:16px;height:16px;border-radius:50%;display:inline-block;margin-left:8px;background:#444}
  .ledon{background:#1e90ff}
</style>
</head>
<body>
<h2>Dynamic Spin Balancer (Fallback UI)</h2>
<p>This is the minimal built-in UI. For the full React UI, upload your build to LittleFS as <code>/index.html</code> and assets.</p>

<div class="row">
  <div class="card">
    <b>Connection</b><br/>
    WebSocket: <code id="wsurl"></code><br/>
    LED:<span id="led" class="led"></span>
  </div>
  <div class="card">
    <b>Telemetry</b><br/>
    RPM: <span id="rpm">-</span><br/>
    VibMag: <span id="mag">-</span><br/>
    Heavy: <span id="heavy">-</span>Â°<br/>
    Add: <span id="add">-</span>Â°<br/>
    Qual: <span id="q">-</span>
  </div>
</div>

<h3>Quick Controls</h3>
<div class="row">
  <button onclick="post('/cmd/stop',{})">STOP</button>
  <button onclick="post('/cmd/start_test',{profileId:'2600'})">Start 2600</button>
  <button onclick="post('/cmd/led_mode',{mode:'add'})">LED: ADD</button>
  <button onclick="post('/cmd/led_mode',{mode:'heavy'})">LED: HEAVY</button>
  <button onclick="post('/cmd/led_mode',{mode:'zero'})">LED: ZERO</button>
</div>

<h3>Set LED target</h3>
<input id="tgt" type="number" step="0.1" value="0" />
<button onclick="post('/cmd/led_mode',{mode:'target'}).then(()=>post('/cmd/led_target',{targetDeg:parseFloat(document.getElementById('tgt').value)}))">Set TARGET</button>

<p>Wi-Fi setup: <a href="/setup">/setup</a></p>

<script>
const wsurl = `ws://${location.host}/ws`;
document.getElementById('wsurl').textContent = wsurl;
let ws = new WebSocket(wsurl);

ws.onmessage = (ev)=>{
  let msg = JSON.parse(ev.data);
  if(!msg.telemetry) return;
  const t = msg.telemetry;
  document.getElementById('rpm').textContent = (t.rpm||0).toFixed(0);
  document.getElementById('mag').textContent = (t.vibMag||0).toFixed(1);
  document.getElementById('heavy').textContent = (t.heavyDeg||0).toFixed(1);
  document.getElementById('add').textContent = (t.addDeg||0).toFixed(1);
  document.getElementById('q').textContent = (t.quality||0).toFixed(2);
  const led = document.getElementById('led');
  led.className = 'led ' + (t.ledOn ? 'ledon' : '');
}

async function post(path, obj){
  const r = await fetch(path, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(obj||{})});
  return r.json().catch(()=>({ok:false}));
}
</script>
</body>
</html>
)HTML";

static const char SETUP_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width,initial-scale=1" />
<title>Balancer Setup</title>
<style>
  body{font-family:system-ui,Segoe UI,Arial;margin:20px;max-width:900px}
  .card{border:1px solid #ddd;border-radius:12px;padding:12px;margin-bottom:12px}
  button{padding:10px 12px;border-radius:10px;border:1px solid #333;background:#111;color:#fff}
  input,select{padding:10px;border-radius:10px;border:1px solid #ccc;width:100%}
</style>
</head>
<body>
<h2>Wi-Fi Setup</h2>
<div class="card">
  <button onclick="scan()">Scan Networks</button>
  <p><small>Select SSID, enter password, Save.</small></p>
  <label>SSID</label>
  <select id="ssid"></select>
  <label>Password</label>
  <input id="pass" type="password" placeholder="(leave blank for open networks)" />
  <button onclick="save()">Save & Reboot</button>
</div>

<div class="card">
  <b>Status</b><br/>
  <pre id="status">Loading...</pre>
</div>

<script>
async function scan(){
  const r = await fetch('/wifi/scan');
  const j = await r.json();
  const sel = document.getElementById('ssid');
  sel.innerHTML = '';
  (j.ssids||[]).sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
    const opt=document.createElement('option');
    opt.value=n.ssid;
    opt.textContent = `${n.ssid} (${n.rssi} dBm)${n.enc?' ðŸ”’':''}`;
    sel.appendChild(opt);
  });
}
async function save(){
  const ssid = document.getElementById('ssid').value;
  const password = document.getElementById('pass').value;
  const r = await fetch('/wifi/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,password})});
  const j = await r.json();
  alert('Saved. Rebooting...');
}
async function status(){
  const r = await fetch('/wifi/status');
  const j = await r.json();
  document.getElementById('status').textContent = JSON.stringify(j,null,2);
}
scan(); status(); setInterval(status,1500);
</script>
</body>
</html>
)HTML";

// ===================== HTTP JSON body helper =====================
static bool parseJsonBody(AsyncWebServerRequest* req, JsonDocument& doc) {
  Serial.printf("[JSON] url=%s ct=%s len=%u params=%u plain=%d temp=%p\n",
                req->url().c_str(), req->contentType().c_str(), (unsigned)req->contentLength(),
                (unsigned)req->params(), req->hasParam("plain", true) ? 1 : 0, req->_tempObject);
  String body;
  if (req->hasParam("plain", true)) {
    body = req->getParam("plain", true)->value();
  } else if (req->_tempObject != nullptr) {
    String* collected = reinterpret_cast<String*>(req->_tempObject);
    body = *collected;
  } else {
    return false;
  }
  if (req->_tempObject != nullptr) {
    delete reinterpret_cast<String*>(req->_tempObject);
    req->_tempObject = nullptr;
  }
  Serial.printf("[JSON] body-bytes=%u\n", (unsigned)body.length());
  return deserializeJson(doc, body) == DeserializationError::Ok;
}

static void collectJsonBodyChunk(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
  if (index == 0) {
    if (req->_tempObject != nullptr) {
      delete reinterpret_cast<String*>(req->_tempObject);
    }
    String* body = new String();
    body->reserve(total);
    req->_tempObject = body;
  }
  if (req->_tempObject != nullptr) {
    String* body = reinterpret_cast<String*>(req->_tempObject);
    body->concat(reinterpret_cast<const char*>(data), len);
  }
}

static bool isSimpleIdToken(const String& id) {
  if (id.length() == 0) return false;
  for (size_t i = 0; i < id.length(); i++) {
    char c = id[i];
    bool ok = (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              (c == '_') || (c == '-');
    if (!ok) return false;
  }
  return true;
}

// ===================== WiFi start (STA then AP fallback) =====================
static bool startWiFiSTA() {
  if (g_wifi.ssid.length() == 0) {
    Serial.println("[WiFi] No SSID configured, skipping STA.");
    return false;
  }

#if CONFIG_IDF_TARGET_ESP32S3
  Serial.println("[WiFi] ESP32-S3 radio supports 2.4GHz only (cannot join 5GHz BSSID).");
#endif
  Serial.printf("[WiFi] Attempting STA connect to \"%s\" (timeout %lu ms)...\n",
                g_wifi.ssid.c_str(), (unsigned long)WIFI_STA_TIMEOUT_MS);
  WiFi.mode(WIFI_STA);
  WifiScanMatch match = findBestScanMatchForSsid(g_wifi.ssid);
  if (match.found) {
    // Lock to selected 2.4GHz BSSID to avoid accidental association to isolated mesh MACs.
    WiFi.begin(g_wifi.ssid.c_str(), g_wifi.pass.c_str(), (int32_t)match.channel, match.bssid, true);
  } else {
    WiFi.begin(g_wifi.ssid.c_str(), g_wifi.pass.c_str());
  }

  uint32_t start = millis();
  uint32_t lastLog = start;
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_STA_TIMEOUT_MS) {
    uint32_t now = millis();
    if (now - lastLog >= 1000) {
      lastLog = now;
      wl_status_t s = WiFi.status();
      Serial.printf("[WiFi] Waiting... status=%d (%s)\n", (int)s, wifiStatusName(s));
    }
    delay(150);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] STA connected! IP: %s  MAC: %s\n",
                  WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
    return true;
  }
  wl_status_t s = WiFi.status();
  Serial.printf("[WiFi] STA failed (status=%d, %s). Will fall back to AP.\n", (int)s, wifiStatusName(s));
  return false;
}

static void startWiFiAP() {
  g_isAPMode = true;
  WiFi.mode((WiFi.status() == WL_CONNECTED) ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[WiFi] AP mode started. SSID: \"%s\"  AP IP: %s\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] STA+AP active. STA IP: %s\n", WiFi.localIP().toString().c_str());
  }
}

static void startMdnsIfSTA() {
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("balance")) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("[mDNS] Registered: http://balance.local");
    } else {
      Serial.println("[mDNS] MDNS.begin() failed!");
    }
  } else {
    Serial.println("[mDNS] Skipped (not in STA mode).");
  }
}

// ===================== Routes =====================
static void setupRoutes() {
  // Static UI from LittleFS if present, else fallback index embedded
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    if (LittleFS.exists("/index.html")) {
      req->send(LittleFS, "/index.html", "text/html");
    } else {
      req->send_P(200, "text/html", FALLBACK_INDEX_HTML);
    }
  });

  // Setup page always available
  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send_P(200, "text/html", SETUP_HTML);
  });

  // Serve all other static files if present
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html"); // :contentReference[oaicite:5]{index=5}

  // WebSocket
  ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType t, void*, uint8_t*, size_t){
    if (t == WS_EVT_CONNECT) {
      wsBroadcast();
    }
  });
  server.addHandler(&ws);

  // ---- WiFi endpoints ----
  server.on("/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req){
    int n = WiFi.scanNetworks();
    StaticJsonDocument<2048> doc;
    JsonArray arr = doc.createNestedArray("ssids");
    for (int i=0;i<n;i++){
      JsonObject o = arr.createNestedObject();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["enc"]  = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/wifi/save", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;

      StaticJsonDocument<384> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
      String ssid = doc["ssid"] | "";
      String pass = doc["password"] | "";

      saveWifiCreds(ssid, pass);
      req->send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
      delay(250);
      ESP.restart();
    });

  server.on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<512> doc;
    bool connected = (WiFi.status() == WL_CONNECTED);
    doc["apMode"] = g_isAPMode;
    doc["ssidSaved"] = g_wifi.ssid;
    doc["connected"] = connected;
    doc["ip"] = connected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    if (connected) doc["staIp"] = WiFi.localIP().toString();
    else doc["staIp"] = "";
    doc["apIp"] = WiFi.softAPIP().toString();
    doc["mdns"] = connected ? "balance.local" : nullptr;
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  // ---- Settings ----
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<768> doc;

    JsonObject model = doc.createNestedObject("model");
    model["zeroOffsetDeg"] = g_set.zeroOffsetDeg;
    model["windowDeg"]     = g_set.windowDeg;
    model["correctionRadiusMm"] = g_set.correctionRadiusMm;

    JsonObject led = doc.createNestedObject("led");
    led["mode"] = (int)g_set.ledMode;
    led["targetDeg"] = g_set.ledTargetDeg;

    JsonObject sampling = doc.createNestedObject("sampling");
    sampling["samplePeriodUs"] = g_set.samplePeriodUs;
    sampling["measureWindowMs"] = g_set.measureWindowMs;
    sampling["noiseFloorTarget"] = g_set.noiseFloorTarget;
    sampling["wsPublishMs"] = g_set.wsPublishMs;

    JsonObject motor = doc.createNestedObject("motor");
    motor["escIdleUs"] = g_set.escIdleUs;
    motor["escMaxUs"]  = g_set.escMaxUs;
    motor["rpmStableTol"] = g_set.rpmStableTol;
    motor["rpmStableHoldMs"] = g_set.rpmStableHoldMs;

    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/settings", HTTP_PATCH, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;

      StaticJsonDocument<1024> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }

      if (doc["model"].is<JsonObject>()) {
        JsonObject m = doc["model"];
        if (m.containsKey("phaseOffsetDeg")) {
          req->send(400, "application/json", "{\"ok\":false,\"err\":\"deprecated_field\",\"field\":\"model.phaseOffsetDeg\"}");
          return;
        }
        if (m.containsKey("zeroOffsetDeg")) g_set.zeroOffsetDeg = m["zeroOffsetDeg"].as<float>();
        if (m.containsKey("windowDeg"))     g_set.windowDeg     = m["windowDeg"].as<float>();
        if (m.containsKey("correctionRadiusMm")) g_set.correctionRadiusMm = m["correctionRadiusMm"].as<float>();
      }
      if (doc["led"].is<JsonObject>()) {
        JsonObject l = doc["led"];
        if (l.containsKey("mode"))      g_set.ledMode = (uint8_t)l["mode"].as<int>();
        if (l.containsKey("targetDeg")) g_set.ledTargetDeg = l["targetDeg"].as<float>();
      }
      if (doc["sampling"].is<JsonObject>()) {
        JsonObject s = doc["sampling"];
        if (s.containsKey("samplePeriodUs")) g_set.samplePeriodUs = s["samplePeriodUs"].as<uint32_t>();
        if (s.containsKey("measureWindowMs")) g_set.measureWindowMs = s["measureWindowMs"].as<uint32_t>();
        if (s.containsKey("noiseFloorTarget")) g_set.noiseFloorTarget = s["noiseFloorTarget"].as<float>();
        if (s.containsKey("wsPublishMs")) g_set.wsPublishMs = s["wsPublishMs"].as<uint32_t>();
      }
      if (doc["motor"].is<JsonObject>()) {
        JsonObject m = doc["motor"];
        if (m.containsKey("escIdleUs")) g_set.escIdleUs = m["escIdleUs"].as<int>();
        if (m.containsKey("escMaxUs"))  g_set.escMaxUs  = m["escMaxUs"].as<int>();
        if (m.containsKey("rpmStableTol")) g_set.rpmStableTol = m["rpmStableTol"].as<float>();
        if (m.containsKey("rpmStableHoldMs")) g_set.rpmStableHoldMs = m["rpmStableHoldMs"].as<uint32_t>();
      }

      g_set.escIdleUs = constrain(g_set.escIdleUs, ESC_MIN_US, ESC_MAX_US);
      g_set.escMaxUs  = constrain(g_set.escMaxUs, ESC_MIN_US, ESC_MAX_US);
      g_set.windowDeg = constrain(g_set.windowDeg, 0.1f, 10.0f);
      g_set.correctionRadiusMm = constrain(g_set.correctionRadiusMm, 1.0f, 500.0f);
      g_set.noiseFloorTarget = constrain(g_set.noiseFloorTarget, 0.0f, NOISE_FLOOR_MAX_G);
      g_set.measureWindowMs = constrain(g_set.measureWindowMs, 200UL, 15000UL);
      g_set.samplePeriodUs = constrain(g_set.samplePeriodUs, 750UL, 100000UL);
      g_set.wsPublishMs = constrain(g_set.wsPublishMs, 50UL, 10000UL);
      g_set.rpmStableTol = constrain(g_set.rpmStableTol, 10.0f, 1000.0f);
      g_set.rpmStableHoldMs = constrain(g_set.rpmStableHoldMs, 100UL, 30000UL);
      saveSettings();
      req->send(200, "application/json", "{\"ok\":true}");
    });

  // ---- LED control ----
  server.on("/cmd/led_mode", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;

      StaticJsonDocument<256> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
      String mode = doc["mode"] | "add";

      if (mode == "off")    g_set.ledMode = LED_OFF;
      else if (mode == "zero")  g_set.ledMode = LED_ZERO;
      else if (mode == "heavy") g_set.ledMode = LED_HEAVY;
      else if (mode == "add")   g_set.ledMode = LED_ADD;
      else if (mode == "remove")g_set.ledMode = LED_REMOVE;
      else if (mode == "target")g_set.ledMode = LED_TARGET;
      else { req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad_mode\"}"); return; }

      saveSettings();
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/cmd/led_target", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;

      StaticJsonDocument<256> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
      float tgt = doc["targetDeg"] | 0.0f;
      g_set.ledTargetDeg = wrap360(tgt);
      g_set.ledMode = LED_TARGET;
      saveSettings();
      req->send(200, "application/json", "{\"ok\":true}");
    });

  // ---- Motor commands ----
  server.on("/cmd/start_test", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;

      StaticJsonDocument<256> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
      String profileId = doc["profileId"] | "1750";
      Profile* p = findProfileById(profileId);
      if (!p) { req->send(404, "application/json", "{\"ok\":false,\"err\":\"bad_profile\"}"); return; }
      if (!isfinite(p->phaseOffsetDeg) || p->phaseOffsetDeg < -180.0f || p->phaseOffsetDeg > 180.0f) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad_profile_phase_offset\"}");
        return;
      }

      clearError();
      g_targetRpm = p->targetRpm;
      g_spinupStartMs = millis();
      g_spinupTimeoutMs = p->spinupMs;
      g_measureStartMs = 0;
      g_stableStartMs = 0;
      g_rpmStable = false;
      escPIReset();
      resetAccum();
      taskENTER_CRITICAL(&telemMux);
      g_currentRunId++;
      taskEXIT_CRITICAL(&telemMux);

      State s = readStateSnapshot();
      s.runStep = STEP_SPINUP;
      strncpy(s.profileName, p->name.c_str(), sizeof(s.profileName)-1);
      s.profileName[sizeof(s.profileName)-1] = 0;
      s.motorState = MOTOR_RUNNING;
      s.phaseGuidanceStale = true;
      s.hasActiveProfile = true;
      strncpy(s.activeProfileId, p->id.c_str(), sizeof(s.activeProfileId)-1);
      s.activeProfileId[sizeof(s.activeProfileId)-1] = 0;
      s.activeProfilePhaseOffsetDeg = p->phaseOffsetDeg;
      writeStateSnapshot(s);
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/cmd/stop", HTTP_POST, [](AsyncWebServerRequest* req){
    motorStop();
    g_sweepEscUs = 0;  // also cancel any direct ESC override
    req->send(200, "application/json", "{\"ok\":true}");
  });

  // ---- Diagnostics: direct ESC override for sweep tests ----
  server.on("/cmd/set_esc", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;
      StaticJsonDocument<128> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
      int us = doc["us"] | 0;
      if (us == 0) {
        // Stop override, idle ESC
        g_sweepEscUs = 0;
        escAttachIfNeeded();
        esc.writeMicroseconds(g_set.escIdleUs);
        rpmEMA = 0.0f;
      } else {
        // Ensure motor is stopped from normal control first
        if (readStateSnapshot().motorState == MOTOR_RUNNING) motorStop();
        us = constrain(us, ESC_MIN_US, ESC_MAX_US);
        g_sweepEscUs = (int16_t)us;
      }
      StaticJsonDocument<128> resp;
      resp["ok"] = true;
      resp["us"] = (int)g_sweepEscUs;
      String r; serializeJson(resp, r);
      req->send(200, "application/json", r);
    });

  // ---- Diagnostics: raw sensor snapshot ----
  server.on("/diag/raw", HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<1024> doc;
    Telemetry t = readTelemSnapshot();
    State s = readStateSnapshot();

    // Existing raw diagnostics fields (kept for compatibility)
    doc["rawAngleDeg"]  = g_rawAngleDeg;
    doc["rpmEMA"]       = rpmEMA;
    doc["wrapCount"]    = g_wrapCount;
    doc["lastWrapDtUs"] = g_lastWrapDtUs;
    doc["sampleCount"]  = g_sampleCount;
    doc["sweepEscUs"]   = (int)g_sweepEscUs;
    doc["escMaxUs"]     = g_set.escMaxUs;

    // Live angle block for lightweight /diag updates.
    // We sample ANGLECOM in the real-time loop; expose equivalent fields additively.
    doc["anglecom_deg_live"] = g_rawAngleDeg;
    doc["anglecom_raw14_live"] = (int)g_rawAngle14bit;
    doc["angle_deg_live"] = g_rawAngleDeg;
    doc["angle_raw14_live"] = (int)g_rawAngle14bit;

    // Live runner telemetry/state snapshot.
    doc["motorState"] = (int)s.motorState;
    doc["runStep"] = (int)s.runStep;
    doc["profileName"] = s.profileName;
    doc["phaseGuidanceStale"] = s.phaseGuidanceStale;
    doc["activeProfileId"] = s.hasActiveProfile ? s.activeProfileId : "";
    if (s.hasActiveProfile) doc["activeProfilePhaseOffsetDeg"] = s.activeProfilePhaseOffsetDeg;
    else doc["activeProfilePhaseOffsetDeg"] = nullptr;
    doc["hasResultSnapshot"] = hasResultSnapshotFlag();
    doc["rpm"] = t.rpm;
    doc["vibMag"] = t.vibMag;
    doc["phaseDeg"] = t.phaseDeg;
    doc["heavyDeg"] = t.heavyDeg;
    doc["quality"] = t.quality;
    doc["noiseRms"] = t.noiseRms;

    String r; serializeJson(doc, r);
    req->send(200, "application/json", r);
  });

  // ---- Diagnostics: full AS5047 SPI health check ----
  server.on("/diag/spi_test", HTTP_GET, [](AsyncWebServerRequest* req){
    State s = readStateSnapshot();
    if (s.motorState == MOTOR_RUNNING) {
      StaticJsonDocument<192> busy;
      busy["ok"] = false;
      busy["err"] = "busy_running";
      busy["motorState"] = (int)s.motorState;
      busy["runStep"] = (int)s.runStep;
      String out; serializeJson(busy, out);
      req->send(409, "application/json", out);
      return;
    }

    StaticJsonDocument<768> doc;

    // Optional: force SPI re-init if ?reinit=1 query param
    bool doReinit = req->hasParam("reinit") && req->getParam("reinit")->value() == "1";
    doc["reinit"] = doReinit;
    if (doReinit) {
      SPI.end();
      SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_AS5047_CS);
      pinMode(PIN_AS5047_CS, OUTPUT);
      digitalWrite(PIN_AS5047_CS, HIGH);
      delayMicroseconds(100);
    }

    // Read ERRFL register (address 0x0001)
    ReadDataFrame errfl = as5047.readRegister(ERRFL_REG);
    doc["errfl_raw"] = errfl.raw;
    doc["errfl_frerr"] = (bool)((errfl.values.data >> 0) & 1);   // framing error
    doc["errfl_invcomm"] = (bool)((errfl.values.data >> 1) & 1); // invalid command
    doc["errfl_parerr"] = (bool)((errfl.values.data >> 2) & 1);  // parity error
    doc["errfl_ef"] = (bool)errfl.values.ef;                     // error flag in frame

    // Read DIAGAGC register (address 0x3FFC)
    ReadDataFrame diagagc = as5047.readRegister(DIAGAGC_REG);
    doc["diagagc_raw"] = diagagc.raw;
    doc["agc"] = (int)(diagagc.values.data & 0xFF);              // AGC value (0-255)
    doc["lf"] = (bool)((diagagc.values.data >> 8) & 1);         // offset compensation finished
    doc["cof"] = (bool)((diagagc.values.data >> 9) & 1);        // CORDIC overflow
    doc["magh"] = (bool)((diagagc.values.data >> 10) & 1);      // mag field too high
    doc["magl"] = (bool)((diagagc.values.data >> 11) & 1);      // mag field too low

    // Read MAG register (address 0x3FFD)
    ReadDataFrame mag = as5047.readRegister(MAG_REG);
    doc["mag_raw"] = mag.raw;
    doc["cmag"] = (int)(mag.values.data & 0x3FFF);               // CORDIC magnitude

    // Read ANGLE register (address 0x3FFE) â€” raw uncompensated
    ReadDataFrame angle = as5047.readRegister(ANGLE_REG);
    doc["angle_raw"] = angle.raw;
    doc["angle_14bit"] = (int)(angle.values.data & 0x3FFF);
    doc["angle_deg"] = (angle.values.data & 0x3FFF) / 16384.0f * 360.0f;
    doc["angle_ef"] = (bool)angle.values.ef;                     // error flag

    // Read ANGLECOM register (address 0x3FFF) â€” compensated (what readAngle uses)
    ReadDataFrame anglecom = as5047.readRegister(ANGLECOM_REG);
    doc["anglecom_raw"] = anglecom.raw;
    doc["anglecom_14bit"] = (int)(anglecom.values.data & 0x3FFF);
    doc["anglecom_deg"] = (anglecom.values.data & 0x3FFF) / 16384.0f * 360.0f;

    // Convenience: call readAngle() directly
    doc["readAngle"] = as5047.readAngle();

    // SPI pin info
    doc["cs_pin"] = PIN_AS5047_CS;
    doc["sck_pin"] = PIN_SPI_SCK;
    doc["miso_pin"] = PIN_SPI_MISO;
    doc["mosi_pin"] = PIN_SPI_MOSI;

    // Wrap detection state
    doc["rpmEMA"] = rpmEMA;
    doc["wrapCount"] = g_wrapCount;
    doc["lastAngleDeg"] = lastAngleDeg;

    String r; serializeJson(doc, r);
    req->send(200, "application/json", r);
  });

  // ---- Diagnostics: live sensor page ----
  server.on("/diag", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", DIAG_PAGE_HTML);
  });

  // ---- Profiles CRUD ----
  server.on(AsyncURIMatcher::exact("/profiles"), HTTP_GET, [](AsyncWebServerRequest* req){
    StaticJsonDocument<4096> doc;
    JsonArray arr = doc.createNestedArray("profiles");
    for (auto& p : g_profiles) {
      JsonObject o = arr.createNestedObject();
      o["id"] = p.id;
      o["name"] = p.name;
      o["rpm"] = p.targetRpm;
      o["spinupMs"] = p.spinupMs;
      o["dwellMs"] = p.dwellMs;
      o["repeats"] = p.repeats;
      o["phaseOffsetDeg"] = p.phaseOffsetDeg;
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on(AsyncURIMatcher::exact("/profiles"), HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;

      StaticJsonDocument<512> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }

      Profile p;
      p.id = doc["id"] | "";
      p.name = doc["name"] | p.id.c_str();
      p.targetRpm = doc["rpm"] | 2500;
      p.spinupMs  = doc["spinupMs"] | 2500;
      p.dwellMs   = doc["dwellMs"] | 3500;
      p.repeats   = doc["repeats"] | 1;
      p.phaseOffsetDeg = doc.containsKey("phaseOffsetDeg")
        ? doc["phaseOffsetDeg"].as<float>()
        : seedProfilePhaseOffset(p.id, p.targetRpm);

      if (p.id.length() == 0) { req->send(400, "application/json", "{\"ok\":false,\"err\":\"missing_id\"}"); return; }
      if (findProfileById(p.id)) { req->send(409, "application/json", "{\"ok\":false,\"err\":\"exists\"}"); return; }
      if (!isfinite(p.phaseOffsetDeg) || p.phaseOffsetDeg < -180.0f || p.phaseOffsetDeg > 180.0f) {
        req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad_phase_offset\"}");
        return;
      }

      g_profiles.push_back(p);
      saveProfiles();
      req->send(200, "application/json", "{\"ok\":true}");
    });

  // PATCH /profiles/:id
  server.on(AsyncURIMatcher::prefix("/profiles/"), HTTP_PATCH, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;

      String url = req->url();
      String id = url.substring(String("/profiles/").length());
      if (!isSimpleIdToken(id)) { req->send(404, "application/json", "{\"ok\":false}"); return; }
      Profile* p = findProfileById(id);
      if (!p) { req->send(404, "application/json", "{\"ok\":false}"); return; }

      StaticJsonDocument<512> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }

      if (doc.containsKey("name")) p->name = (const char*)doc["name"];
      if (doc.containsKey("rpm")) p->targetRpm = doc["rpm"];
      if (doc.containsKey("spinupMs")) p->spinupMs = doc["spinupMs"];
      if (doc.containsKey("dwellMs")) p->dwellMs = doc["dwellMs"];
      if (doc.containsKey("repeats")) p->repeats = doc["repeats"];
      if (doc.containsKey("phaseOffsetDeg")) {
        float phaseOffsetDeg = doc["phaseOffsetDeg"].as<float>();
        if (!isfinite(phaseOffsetDeg) || phaseOffsetDeg < -180.0f || phaseOffsetDeg > 180.0f) {
          req->send(400, "application/json", "{\"ok\":false,\"err\":\"bad_phase_offset\"}");
          return;
        }
        p->phaseOffsetDeg = phaseOffsetDeg;
      }

      saveProfiles();
      req->send(200, "application/json", "{\"ok\":true}");
    });

  // DELETE /profiles/:id
  server.on(AsyncURIMatcher::prefix("/profiles/"), HTTP_DELETE, [](AsyncWebServerRequest* req){
    String url = req->url();
    String id = url.substring(String("/profiles/").length());
    if (!isSimpleIdToken(id)) { req->send(404, "application/json", "{\"ok\":false}"); return; }
    for (size_t i=0;i<g_profiles.size();i++){
      if (g_profiles[i].id == id) {
        g_profiles.erase(g_profiles.begin()+i);
        saveProfiles();
        req->send(200, "application/json", "{\"ok\":true}");
        return;
      }
    }
    req->send(404, "application/json", "{\"ok\":false}");
  });

  // ---- Sessions ----
  server.on(AsyncURIMatcher::exact("/sessions"), HTTP_GET, [](AsyncWebServerRequest* req){
    if (!LittleFS.exists(SESS_INDEX)) initSessionsStore();
    String idx = readTextFile(SESS_INDEX);
    if (idx.length() < 2) idx = "{\"sessions\":[]}";
    req->send(200, "application/json", idx);
  });

  server.on(AsyncURIMatcher::prefix("/sessions/"), HTTP_GET, [](AsyncWebServerRequest* req){
    String url = req->url();
    String id = url.substring(String("/sessions/").length());
    if (!isSimpleIdToken(id)) { req->send(404, "application/json", "{\"ok\":false}"); return; }
    String path = sessionPath(id);
    if (!LittleFS.exists(path)) { req->send(404, "application/json", "{\"ok\":false}"); return; }
    String s = readTextFile(path.c_str());
    req->send(200, "application/json", s);
  });

  server.on("/cmd/save_session", HTTP_POST, [](AsyncWebServerRequest* req){}, nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
      collectJsonBodyChunk(req, data, len, index, total);
      if (index + len != total) return;

      StaticJsonDocument<512> doc;
      if (!parseJsonBody(req, doc)) { req->send(400, "application/json", "{\"ok\":false}"); return; }

      String name = doc["name"] | "Session";
      String notes = doc["notes"] | "";

      Telemetry t;
      uint32_t resultRunId = 0;
      uint32_t currentRunId = 0;
      bool isCurrent = false;
      bool hasProfilePhaseOffset = false;
      float profilePhaseOffsetDegUsed = 0.0f;
      if (!readResultSnapshot(
        t,
        resultRunId,
        currentRunId,
        isCurrent,
        hasProfilePhaseOffset,
        profilePhaseOffsetDegUsed
      )) {
        req->send(409, "application/json", "{\"ok\":false,\"err\":\"no_results\"}");
        return;
      }
      if (!isCurrent) {
        StaticJsonDocument<192> stale;
        stale["ok"] = false;
        stale["err"] = "stale_result";
        stale["resultRunId"] = resultRunId;
        stale["currentRunId"] = currentRunId;
        String payload; serializeJson(stale, payload);
        req->send(409, "application/json", payload);
        return;
      }

      State st = readStateSnapshot();
      String id = String((uint32_t)millis(), 10);

      StaticJsonDocument<2048> sess;
      sess["id"] = id;
      sess["name"] = name;
      sess["notes"] = notes;
      sess["timestamp"] = (uint32_t)millis();
      sess["profileName"] = st.profileName;
      if (hasProfilePhaseOffset) sess["profilePhaseOffsetDegUsed"] = profilePhaseOffsetDegUsed;
      else sess["profilePhaseOffsetDegUsed"] = nullptr;

      JsonObject tele = sess.createNestedObject("telemetry");
      tele["rpm"] = t.rpm;
      tele["vibMag"] = t.vibMag;
      tele["phaseDeg"] = t.phaseDeg;
      tele["quality"] = t.quality;
      tele["noiseRms"] = t.noiseRms;
      tele["heavyDeg"] = t.heavyDeg;
      tele["addDeg"] = t.addDeg;
      tele["removeDeg"] = t.removeDeg;

      String out;
      serializeJson(sess, out);

      if (!initSessionsStore()) { req->send(500, "application/json", "{\"ok\":false,\"err\":\"fs\"}"); return; }
      if (!saveSessionFile(id, out)) { req->send(500, "application/json", "{\"ok\":false,\"err\":\"write\"}"); return; }
      if (!appendSessionIndex(id, name, (uint32_t)millis())) {
        req->send(500, "application/json", "{\"ok\":false,\"err\":\"index_write\"}");
        return;
      }

      StaticJsonDocument<128> resp;
      resp["ok"] = true;
      resp["id"] = id;
      String r; serializeJson(resp, r);
      req->send(200, "application/json", r);
    });
}

// ===================== Sampling task (deterministic) =====================
static void samplingTask(void* pv) {
  (void)pv;

  resetAccum();
  uint32_t initUs = micros();
  lastWrapMicros   = initUs;
  rpmAccumStartUs  = initUs;
  rpmAccumDeg      = 0.0f;
  uint32_t lastWindowCompute = millis();

  for (;;) {
    // --- Read sensors ---
    int16_t ay = g_mpuReady ? mpu.getAccelerationY() : 0;
    // Fast 4 MHz read of ANGLECOM (DAEC-compensated for propagation delay at speed)
    uint16_t rawAngle14 = 0;
    float rawAngle = wrap360(readAngleComFast(&rawAngle14));

    // --- Publish raw angle for diagnostics ---
    g_rawAngleDeg = rawAngle;
    g_rawAngle14bit = rawAngle14;
    g_sampleCount++;

    // --- Delta-angle RPM estimation (windowed accumulation) ---
    uint32_t nowUs = micros();

    // Signed angular delta with wrap handling
    float delta = rawAngle - lastAngleDeg;
    if (delta >  180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;

    // Keep wrap counter for diagnostics / backward compat
    if ((lastAngleDeg - rawAngle) > 300.0f) {
      g_wrapCount++;
      g_lastWrapDtUs = nowUs - lastWrapMicros;
      lastWrapMicros = nowUs;
    }

    // Accumulate angle traversed in current window
    rpmAccumDeg += delta;
    uint32_t windowElapsed = nowUs - rpmAccumStartUs;
    if (windowElapsed >= RPM_WINDOW_US) {
      // Compute RPM from total degrees traversed over the window
      float windowRpm = (rpmAccumDeg / 360.0f) * (60.0e6f / (float)windowElapsed);
      // Deadband: treat near-zero as stopped
      if (fabsf(windowRpm) < 5.0f) windowRpm = 0.0f;
      rpmEMA = ema(g_set.rpmEmaAlpha, fabsf(windowRpm), rpmEMA);
      // Reset accumulator for next window
      rpmAccumDeg     = 0.0f;
      rpmAccumStartUs = nowUs;
    }
    lastAngleDeg = rawAngle;

    // --- Direct ESC override for sweep test ---
    int16_t sweepUs = g_sweepEscUs;
    if (sweepUs > 0) {
      escAttachIfNeeded();
      esc.writeMicroseconds(constrain(sweepUs, ESC_MIN_US, ESC_MAX_US));
    }

    // --- Update state machine / motor control ---
    State s = readStateSnapshot();
    uint32_t nowMs = millis();

    // Defensive guard: once a test reaches RESULTS, ensure motor output is idled.
    if (s.runStep == STEP_RESULTS && (s.motorState != MOTOR_STOPPED || g_targetRpm != 0)) {
      motorStopKeepRunStep(STEP_RESULTS);
      s = readStateSnapshot();
    }

    if (s.motorState == MOTOR_RUNNING && g_targetRpm > 0) {
      motorRunToRPM(g_targetRpm);

      // Determine if RPM stable
      float err = fabsf((float)g_targetRpm - rpmEMA);
      bool within = (err <= g_set.rpmStableTol);

      if (within) {
        if (!g_rpmStable) {
          g_rpmStable = true;
          g_stableStartMs = nowMs;
        }
      } else {
        g_rpmStable = false;
        g_stableStartMs = 0;
      }

      if (s.runStep == STEP_SPINUP) {
        // Switch to MEASURE once stable long enough, or after profile spinup timeout.
        bool stableHeld = (g_rpmStable && (nowMs - g_stableStartMs) >= g_set.rpmStableHoldMs);
        bool spinupTimedOut = ((nowMs - g_spinupStartMs) >= g_spinupTimeoutMs);
        if (stableHeld || spinupTimedOut) {
          s.runStep = STEP_MEASURE;
          writeStateSnapshot(s);
          g_measureStartMs = nowMs;
          resetAccum();
          lastWindowCompute = nowMs;
        }
      } else if (s.runStep == STEP_MEASURE) {
        // accumulate only during MEASURE
      }
    }

    // --- Accumulate synchronous detection during MEASURE ---
    s = readStateSnapshot();
    if (s.runStep == STEP_MEASURE) {
      float angAdj = currentAdjustedAngle(rawAngle);
      double theta = (double)angAdj * 3.141592653589793 / 180.0;
      double y = (double)ay;

      acc.sumC += y * cos(theta);
      acc.sumS += y * sin(theta);
      acc.sumY += y;
      acc.sumY2 += y * y;
      acc.n++;

      if ((nowMs - acc.windowStartMs) >= g_set.measureWindowMs) {
        computeBalanceAndPublishWindow(nowMs);

        const bool noiseGateEnabled = (g_set.noiseFloorTarget > 0.0f);
        Telemetry latest = readTelemSnapshot();
        const bool noiseFloorReached = (latest.noiseRms <= g_set.noiseFloorTarget);
        const uint32_t measureElapsedMs = (g_measureStartMs == 0) ? 0 : (nowMs - g_measureStartMs);
        uint32_t maxMeasureMs = g_set.measureWindowMs * 4U;
        if (maxMeasureMs < 6000U) maxMeasureMs = 6000U;
        if (maxMeasureMs > 60000U) maxMeasureMs = 60000U;
        const bool timedOut = (measureElapsedMs >= maxMeasureMs);

        if (!noiseGateEnabled || noiseFloorReached || timedOut) {
          freezeResultSnapshot(latest);
          // Step to RESULTS and stop motor output while preserving RESULTS state for UI.
          motorStopKeepRunStep(STEP_RESULTS);
        }

        // If you want continuous measuring while spinning, remove the explicit stop above.
      }
    }

    // --- LED update (physical + telemetry) ---
    Telemetry t = readTelemSnapshot();
    bool ledOn = computeLedOn(rawAngle, t);
    updatePhysicalLed(ledOn);

    t = readTelemSnapshot();
    t.ledOn = ledOn;
    t.ledMode = g_set.ledMode;
    t.ledTargetDeg = (LedMode)g_set.ledMode == LED_TARGET ? g_set.ledTargetDeg : ledTargetDegFromMode(t);
    writeTelemSnapshot(t);

    // --- Timing ---
    ets_delay_us(g_set.samplePeriodUs);
  }
}

// ===================== Net task (WS broadcast cadence) =====================
static void netTask(void* pv) {
  (void)pv;
  uint32_t lastSend = 0;
  for (;;) {
    uint32_t now = millis();
    if (now - lastSend >= g_set.wsPublishMs) {
      lastSend = now;
      wsBroadcast(); // AsyncWebSocket::textAll is the intended method :contentReference[oaicite:6]{index=6}
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ===================== Setup =====================
void setup() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("===== DynamicBalancer Boot =====");

  loadSettings();
  loadWifiCreds();
  Serial.printf("[Boot] WiFi creds loaded: SSID=\"%s\"\n", g_wifi.ssid.c_str());

  // SPI for AS5047
  // NOTE: The AS5X47 library constructor already called SPI.begin() with DEFAULT
  // pins during global init. We must re-call with our actual hardware pins.
  Serial.println("[Boot] Init SPI...");
  SPI.end();  // tear down whatever the library constructor set up
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_AS5047_CS);
  // Re-assert CS pin as output-high (library constructor may have used wrong pin state)
  pinMode(PIN_AS5047_CS, OUTPUT);
  digitalWrite(PIN_AS5047_CS, HIGH);
  delay(10);
  // Test read â€” log what we get
  {
    float testAngle = as5047.readAngle();
    ReadDataFrame testDiag = as5047.readRegister(DIAGAGC_REG);
    uint8_t agc = testDiag.values.data & 0xFF;
    bool magl = (testDiag.values.data >> 11) & 1;
    Serial.printf("[Boot] AS5047 test: angle=%.2f  AGC=%d  magl=%d  raw=0x%04X\n",
                  testAngle, agc, magl, testDiag.raw);
    if (testAngle == 0.0f && testDiag.raw == 0) {
      Serial.println("[Boot] WARNING: AS5047 returned all zeros â€” SPI may be misconfigured or sensor disconnected!");
      Serial.printf("[Boot]   SCK=%d  MISO=%d  MOSI=%d  CS=%d\n",
                    PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_AS5047_CS);
    }
  }
  Serial.println("[Boot] SPI ready.");

  // I2C
  Serial.printf("[Boot] Init I2C on SDA=%d SCL=%d...\n", PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  Serial.println("[Boot] I2C ready.");

  // IMU init
  Serial.println("[Boot] Init MPU6050...");
  mpu.initialize();
  if (mpu.testConnection()) {
    g_mpuReady = true;
    Serial.println("[Boot] MPU6050 connected. Calibrating accel...");
    mpu.CalibrateAccel(7);
    mpu.setDLPFMode(MPU6050_DLPF_BW_188);
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    Serial.println("[Boot] MPU6050 ready.");
  } else {
    g_mpuReady = false;
    setError("MPU6050 not detected");
    Serial.println("[Boot] MPU6050 not detected. Continuing with accel disabled.");
  }

  // ESC arm
  Serial.printf("[Boot] Arming ESC on pin %d...\n", PIN_ESC);
  esc.attach(PIN_ESC, 800, 2000);
  delay(1000);
  esc.writeMicroseconds(ESC_ARM_US);
  delay(1000);
  esc.writeMicroseconds(ESC_MIN_US);
  delay(1000);
  esc.writeMicroseconds(g_set.escIdleUs);
  delay(400);
  Serial.println("[Boot] ESC armed.");

  // FS
  Serial.println("[Boot] Mounting LittleFS...");
  if (!LittleFS.begin(true)) {
    setError("LittleFS mount failed");
    Serial.println("[Boot] LittleFS mount failed.");
  } else {
    cleanupTmpFiles();
    loadProfiles();
    initSessionsStore();
    Serial.println("[Boot] LittleFS ready.");
  }

  // WiFi
  Serial.println("[Boot] Starting WiFi...");
  bool sta = startWiFiSTA();
  if (!sta || WIFI_AP_ALWAYS_ON) {
    startWiFiAP();
  }
  startMdnsIfSTA();

  // Routes + server
  setupRoutes();
  server.begin();
  Serial.println("[Boot] Web server started on port 80.");
  Serial.printf("[Boot] Ready. Browse to: http://%s\n",
                g_isAPMode ? WiFi.softAPIP().toString().c_str()
                           : WiFi.localIP().toString().c_str());

  // Init state
  State st = readStateSnapshot();
  st.motorState = MOTOR_STOPPED;
  st.runStep = STEP_IDLE;
  strncpy(st.profileName, "idle", sizeof(st.profileName)-1);
  st.phaseGuidanceStale = false;
  st.hasActiveProfile = false;
  st.activeProfileId[0] = 0;
  st.activeProfilePhaseOffsetDeg = 0.0f;
  writeStateSnapshot(st);

  // Start tasks pinned to cores
  // Typical ESP32: WiFi stack prefers core 0; put sampling on core 1.
  xTaskCreatePinnedToCore(samplingTask, "sampling", 8192, nullptr, 2, &samplingTaskHandle, 1);
  xTaskCreatePinnedToCore(netTask, "net", 6144, nullptr, 1, &netTaskHandle, 0);
}

void loop() {
  // Keep empty to avoid timing jitter.
  delay(1000);
}
