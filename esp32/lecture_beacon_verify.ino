#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "mbedtls/md.h"

const char* BLE_DEVICE_NAME = "LECTURE_BEACON";
const char* GATE_SECRET = "CHANGE_ME_GATE_SECRET";

const char* BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
const char* BLE_LECTURE_CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
const char* BLE_TOKEN_CHAR_UUID = "0f3f2e60-8e92-4ea7-9f93-8b4b31c0d9aa";

BLECharacteristic* lectureIdCharacteristic = nullptr;
BLECharacteristic* tokenCharacteristic = nullptr;
String lastLectureId;
String lastIssuedToken;

String hmacSha256Hex(const String& payload, const String& key) {
  unsigned char output[32];
  const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!mdInfo) return "";

  int result = mbedtls_md_hmac(
    mdInfo,
    reinterpret_cast<const unsigned char*>(key.c_str()),
    key.length(),
    reinterpret_cast<const unsigned char*>(payload.c_str()),
    payload.length(),
    output
  );
  if (result != 0) return "";

  const char* hex = "0123456789abcdef";
  String out;
  out.reserve(64);
  for (int i = 0; i < 32; i++) {
    out += hex[(output[i] >> 4) & 0x0F];
    out += hex[output[i] & 0x0F];
  }
  return out;
}

String buildNonce() {
  uint32_t a = esp_random();
  uint32_t b = esp_random();
  char buf[17];
  snprintf(buf, sizeof(buf), "%08lx%08lx", static_cast<unsigned long>(a), static_cast<unsigned long>(b));
  return String(buf);
}

String buildGateToken(const String& lectureId) {
  if (lectureId.length() == 0) return "";

  const int rssi = 0;
  const unsigned long issuedAtMs = millis();
  const String nonce = buildNonce();
  const String payload = lectureId + "." + String(issuedAtMs) + "." + nonce + "." + String(rssi);
  const String signature = hmacSha256Hex(payload, String(GATE_SECRET));
  if (signature.length() == 0) return "";

  return payload + "." + signature;
}

class LectureIdCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    lastLectureId = String(value.c_str());
    lastLectureId.trim();

    Serial.print("Received lectureId: ");
    Serial.println(lastLectureId);
  }
};

class TokenCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* characteristic) override {
    if (lastLectureId.length() == 0) {
      characteristic->setValue("ERR:missing_lecture_id");
      return;
    }

    lastIssuedToken = buildGateToken(lastLectureId);
    if (lastIssuedToken.length() == 0) {
      characteristic->setValue("ERR:token_sign_failed");
      return;
    }

    characteristic->setValue(lastIssuedToken.c_str());
    Serial.println("Issued BLE gate token");
  }
};

void setup() {
  Serial.begin(115200);

  BLEDevice::init(BLE_DEVICE_NAME);
  BLEServer* server = BLEDevice::createServer();
  BLEService* service = server->createService(BLE_SERVICE_UUID);

  lectureIdCharacteristic = service->createCharacteristic(
    BLE_LECTURE_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  lectureIdCharacteristic->setCallbacks(new LectureIdCallbacks());

  tokenCharacteristic = service->createCharacteristic(
    BLE_TOKEN_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ
  );
  tokenCharacteristic->setCallbacks(new TokenCallbacks());
  tokenCharacteristic->setValue("READY");

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(false);
  BLEDevice::startAdvertising();

  Serial.println("BLE beacon is ready");
}

void loop() {
  delay(100);
}
