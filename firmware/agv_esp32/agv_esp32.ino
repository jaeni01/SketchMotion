/*
 * agv_esp32.ino - SketchMotion AGV 펌웨어 (Bridge Protocol v1, 포트 9102)
 *
 * 하드웨어: ESP32 + ACEBOTT 스마트카 쉴드(4모터 드라이버 내장) + 메카넘 4륜
 * 라이브러리: ArduinoJson 6.x (Library Manager에서 설치)
 *
 * 구조 (DESIGN_V2 §2.5/§6.3 — MockBridge.cpp와 동일한 두-태스크 구조):
 *   NetTask     : TCP JSON Lines 수신 → 상태 갱신 (estop은 즉시 처리)
 *   ControlTask : 50Hz — 캐럿 경로추종(PathTracker 포팅) → 메카넘 믹싱 → PWM
 *   워치독      : follow 중 set_pose_estimate 500ms 결측 → 정지 + fault
 *
 * !! 팀 설정 필요 (Sprint 3에서 실측·확정) !!
 *   1) WIFI_SSID/PASS, 고정 IP
 *   2) 모터 핀맵: ACEBOTT 쉴드 실물 라벨 확인 후 MOTOR_PINS 수정
 *   3) GEOM_*: 바퀴 반지름/휠베이스 실측, PWM_PER_RADS: 바퀴 속도 캘리브레이션
 */
#include <WiFi.h>
#include <ArduinoJson.h>

// ---------------- 설정 ----------------
static const char* WIFI_SSID = "SKETCHMOTION_AP";   // TODO(팀): 실제 SSID
static const char* WIFI_PASS = "sketchmotion";      // TODO(팀)
static const uint16_t PORT = 9102;

// ACEBOTT 쉴드 모터 핀 (방향2핀 + PWM1핀 x 4모터 가정) - TODO(팀): 실물 확인
struct MotorPin { int in1, in2, pwmCh, pwmPin; };
static MotorPin MOTOR_PINS[4] = {
  // FL        FR         RL         RR
  {26, 27, 0, 14}, {32, 33, 1, 25}, {18, 19, 2, 23}, {16, 17, 3, 4},
};

static const float GEOM_L = 0.075f;      // 반휠베이스 [m] TODO 실측
static const float GEOM_W = 0.085f;      // 반트레드 [m] TODO 실측
static const float GEOM_R = 0.030f;      // 바퀴 반지름 [m] TODO 실측
static const float PWM_PER_RADS = 18.0f; // 바퀴 rad/s -> PWM duty. TODO 캘리브레이션
static const float POSE_TIMEOUT_MS = 500.0f;

// ---------------- 경로추종 (Core::PathTracker 포팅 - 캐럿 방식) ----------------
struct Vec2 { float x, y; };
static const int MAX_WP = 64;

struct Tracker {
  Vec2 wp[MAX_WP]; float cum[MAX_WP];
  int n = 0; float total = 0, along = 0;
  // 파라미터 (Core TrackerParams와 동일 기본값)
  float lookahead = 0.15f, kPos = 1.8f, kTheta = 2.5f;
  float vMax = 0.30f, omegaMax = 1.8f, arriveRadius = 0.03f;

  void setPath(const Vec2* pts, int count) {
    n = min(count, MAX_WP);
    for (int i = 0; i < n; ++i) wp[i] = pts[i];
    cum[0] = 0;
    for (int i = 1; i < n; ++i)
      cum[i] = cum[i - 1] + hypotf(wp[i].x - wp[i - 1].x, wp[i].y - wp[i - 1].y);
    total = n ? cum[n - 1] : 0;
    along = 0;
  }
  static float wrapA(float a) {
    while (a > PI) a -= 2 * PI;
    while (a <= -PI) a += 2 * PI;
    return a;
  }
  Vec2 pointAt(float s) const {
    s = constrain(s, 0.0f, total);
    for (int i = 1; i < n; ++i)
      if (s <= cum[i] || i == n - 1) {
        const float seg = cum[i] - cum[i - 1];
        const float t = seg > 1e-6f ? (s - cum[i - 1]) / seg : 0;
        return { wp[i - 1].x + (wp[i].x - wp[i - 1].x) * t,
                 wp[i - 1].y + (wp[i].y - wp[i - 1].y) * t };
      }
    return wp[n - 1];
  }
  Vec2 tangentAt(float s) const {
    s = constrain(s, 0.0f, total);
    for (int i = 1; i < n; ++i)
      if (s <= cum[i] || i == n - 1) {
        const float dx = wp[i].x - wp[i - 1].x, dy = wp[i].y - wp[i - 1].y;
        const float len = hypotf(dx, dy);
        return len > 1e-6f ? Vec2{ dx / len, dy / len } : Vec2{ 1, 0 };
      }
    return { 1, 0 };
  }
  // 반환: done. out: vx,vy(바디),omega
  bool step(float x, float y, float th, float& vx, float& vy, float& om) {
    vx = vy = om = 0;
    if (n < 2) return true;
    // 최근접 진행거리 (단조)
    float best = 1e18f, alongNow = 0;
    for (int i = 1; i < n; ++i) {
      const float ax = wp[i - 1].x, ay = wp[i - 1].y;
      const float bx = wp[i].x, by = wp[i].y;
      const float abx = bx - ax, aby = by - ay;
      const float lenSq = abx * abx + aby * aby;
      float t = lenSq > 1e-9f ? ((x - ax) * abx + (y - ay) * aby) / lenSq : 0;
      t = constrain(t, 0.0f, 1.0f);
      const float cx = ax + abx * t, cy = ay + aby * t;
      const float d = hypotf(x - cx, y - cy);
      if (d < best) { best = d; alongNow = cum[i - 1] + t * sqrtf(lenSq); }
    }
    along = max(along, alongNow);
    const float dGoal = hypotf(x - wp[n - 1].x, y - wp[n - 1].y);
    if (dGoal < arriveRadius && along > total - 3 * lookahead) return true;

    const float carrotS = min(total, along + lookahead);
    const Vec2 carrot = pointAt(carrotS);
    float wx = kPos * (carrot.x - x), wy = kPos * (carrot.y - y);
    const float mag = hypotf(wx, wy);
    if (mag > vMax) { wx *= vMax / mag; wy *= vMax / mag; }
    const Vec2 tan = tangentAt(carrotS);
    om = constrain(kTheta * wrapA(atan2f(tan.y, tan.x) - th), -omegaMax, omegaMax);
    const float c = cosf(th), s = sinf(th);
    vx = c * wx + s * wy;
    vy = -s * wx + c * wy;
    return false;
  }
};

// ---------------- 공유 상태 ----------------
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Tracker tracker;
volatile bool following = false;
volatile float estX = 0, estY = 0, estTh = 0;
volatile uint32_t lastPoseMs = 0;
volatile uint32_t followStartMs = 0;
volatile bool everPose = false;

WiFiServer server(PORT);
WiFiClient client;

// ---------------- 모터 ----------------
void motorSetup() {
  for (auto& m : MOTOR_PINS) {
    pinMode(m.in1, OUTPUT); pinMode(m.in2, OUTPUT);
    ledcSetup(m.pwmCh, 1000 /*Hz - 설계 §8.1*/, 8);
    ledcAttachPin(m.pwmPin, m.pwmCh);
  }
}
void motorWrite(int i, float radPerS) {
  const bool fwd = radPerS >= 0;
  digitalWrite(MOTOR_PINS[i].in1, fwd ? HIGH : LOW);
  digitalWrite(MOTOR_PINS[i].in2, fwd ? LOW : HIGH);
  ledcWrite(MOTOR_PINS[i].pwmCh, (uint32_t)constrain(fabsf(radPerS) * PWM_PER_RADS, 0.f, 255.f));
}
void motorsStop() { for (int i = 0; i < 4; ++i) motorWrite(i, 0); }

// (vx,vy,omega) -> 4륜 (Core::MecanumWheelSpeeds와 동일 부호 규약: FL,FR,RL,RR)
void driveBody(float vx, float vy, float om) {
  const float k = (GEOM_L + GEOM_W) * om;
  motorWrite(0, (vx - vy - k) / GEOM_R);
  motorWrite(1, (vx + vy + k) / GEOM_R);
  motorWrite(2, (vx + vy - k) / GEOM_R);
  motorWrite(3, (vx - vy + k) / GEOM_R);
}

// ---------------- 프로토콜 ----------------
void sendLine(const String& s) { if (client && client.connected()) client.print(s + "\n"); }
void sendOk(long id, const String& result = "{}") {
  sendLine(String("{\"id\":") + id + ",\"ok\":true,\"result\":" + result + "}");
}
void sendErr(long id, const char* msg) {
  sendLine(String("{\"id\":") + id + ",\"ok\":false,\"error\":\"" + msg + "\"}");
}
void sendEvent(const char* name, const String& data) {
  sendLine(String("{\"event\":\"") + name + "\",\"data\":" + data + "}");
}

void handleMsg(JsonDocument& doc) {
  const long id = doc["id"] | -1;
  const char* cmd = doc["cmd"] | "";
  if (!strcmp(cmd, "estop") || !strcmp(cmd, "stop")) {
    portENTER_CRITICAL(&mux); following = false; portEXIT_CRITICAL(&mux);
    motorsStop();
    sendOk(id);
  } else if (!strcmp(cmd, "hello")) {
    sendOk(id, "{\"device\":\"agv_mecanum\",\"proto\":1,\"version\":\"fw-0.1\"}");
  } else if (!strcmp(cmd, "telemetry")) {
    const int age = everPose ? (int)(millis() - lastPoseMs) : -1;
    sendOk(id, String("{\"pose_age_ms\":") + age + ",\"wheel\":[0,0,0,0],\"state\":\"" +
                   (following ? "follow" : "idle") + "\"}");
  } else if (!strcmp(cmd, "set_pose_estimate")) {
    JsonObject p = doc["params"];
    portENTER_CRITICAL(&mux);
    estX = p["x"] | 0.0f; estY = p["y"] | 0.0f; estTh = p["theta"] | 0.0f;
    lastPoseMs = millis(); everPose = true;
    portEXIT_CRITICAL(&mux);
    sendOk(id);
  } else if (!strcmp(cmd, "follow_path")) {
    JsonArray path = doc["params"]["path"];
    if (path.isNull() || path.size() < 2) { sendErr(id, "path missing"); return; }
    Vec2 pts[MAX_WP];
    int n = 0;
    for (JsonObject wp : path) {
      if (n >= MAX_WP) break;
      pts[n].x = wp["x"] | 0.0f; pts[n].y = wp["y"] | 0.0f; ++n;
    }
    portENTER_CRITICAL(&mux);
    tracker.vMax = doc["params"]["v_max"] | 0.30f;
    tracker.setPath(pts, n);
    following = true;
    followStartMs = millis();
    everPose = false; // 새 미션은 신선한 pose 스트림 필요
    portEXIT_CRITICAL(&mux);
    sendOk(id, "{\"accepted\":true}");
  } else {
    sendErr(id, "unknown cmd");
  }
}

// ---------------- ControlTask: 50Hz ----------------
void controlTask(void*) {
  uint32_t lastProgress = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(20));
    bool f; float x, y, th;
    portENTER_CRITICAL(&mux);
    f = following; x = estX; y = estY; th = estTh;
    portEXIT_CRITICAL(&mux);
    if (!f) { motorsStop(); continue; }

    // 워치독 (프로토콜 §6-1). 시작 직후엔 첫 pose까지 500ms 유예
    const bool stale = everPose ? (millis() - lastPoseMs > POSE_TIMEOUT_MS)
                                : (millis() - followStartMs > POSE_TIMEOUT_MS);
    if (stale) {
      portENTER_CRITICAL(&mux); following = false; portEXIT_CRITICAL(&mux);
      motorsStop();
      sendEvent("fault", "{\"reason\":\"watchdog\",\"detail\":\"pose stream stale\"}");
      continue;
    }
    if (!everPose) continue; // grace 구간: 첫 pose 대기
    float vx, vy, om;
    const bool done = tracker.step(x, y, th, vx, vy, om);
    if (done) {
      portENTER_CRITICAL(&mux); following = false; portEXIT_CRITICAL(&mux);
      motorsStop();
      sendEvent("done", "{\"phase\":\"path\"}");
      continue;
    }
    driveBody(vx, vy, om);
    if (millis() - lastProgress > 1000) {
      lastProgress = millis();
      sendEvent("progress", String("{\"phase\":\"path\",\"seg\":0,\"of\":0,\"pct\":") +
                                (tracker.total > 0 ? tracker.along / tracker.total * 100.f : 0.f) + "}");
    }
  }
}

// ---------------- setup / NetTask(loop) ----------------
String rxBuf;

void setup() {
  Serial.begin(115200);
  motorSetup();
  motorsStop();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());
  server.begin();
  xTaskCreatePinnedToCore(controlTask, "ctrl", 8192, nullptr, 2, nullptr, 1);
}

void loop() {
  if (!client || !client.connected()) {
    // 연결 워치독: 끊기면 정지
    if (following) {
      portENTER_CRITICAL(&mux); following = false; portEXIT_CRITICAL(&mux);
      motorsStop();
    }
    WiFiClient nc = server.available();
    if (nc) { client = nc; rxBuf = ""; Serial.println("client connected"); }
    delay(10);
    return;
  }
  while (client.available()) {
    const char c = client.read();
    if (c == '\n') {
      if (rxBuf.length() > 2) {
        StaticJsonDocument<4096> doc;
        if (deserializeJson(doc, rxBuf) == DeserializationError::Ok)
          handleMsg(doc);
      }
      rxBuf = "";
    } else if (rxBuf.length() < 4000) {
      rxBuf += c;
    }
  }
  delay(2);
}
