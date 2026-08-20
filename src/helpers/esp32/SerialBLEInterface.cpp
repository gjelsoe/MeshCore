#include "SerialBLEInterface.h"
#include "esp_mac.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  _pin_code = pin_code;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",
            addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", prefix, name);

  NimBLEDevice::init(dev_name);
  NimBLEDevice::setMTU(MAX_FRAME_SIZE);

  deviceConnected = false;
  oldDeviceConnected = false;
  adv_restart_time = 0;

  // Sikkerhed
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityPasskey(pin_code);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(this);

  pService = pServer->createService(SERVICE_UUID);

  // TX karakteristik
  pTxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_TX,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN
  );

  // RX karakteristik
  NimBLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN
  );
  pRxCharacteristic->setCallbacks(this);

  // --- OPRET ADVERTISING-DATA OG SCAN-RESPONSE MED NAVN ---
  NimBLEAdvertisementData advData;
  NimBLEAdvertisementData scanData;

  // Flag: general discoverable + LE only
  advData.setFlags(0x06);
  // Tilføj service UUID (i advertising-pakken)
  advData.addServiceUUID(SERVICE_UUID);

  // Sæt navnet i scan-response (det er her, der er mest plads)
  scanData.setName(dev_name);

  // Sæt også navnet i advertising-pakken (hvis der er plads – ellers overskriver NimBLE det automatisk)
  advData.setName(dev_name);

  // Overfør data til advertising-objektet
  auto* pAdvertising = pServer->getAdvertising();
  pAdvertising->setAdvertisementData(advData);
  pAdvertising->setScanResponseData(scanData);
  pAdvertising->enableScanResponse(true);   // vigtig!

  // Service UUID tilføjes også til advertising-filter (gøres allerede via addServiceUUID ovenfor)
  // pAdvertising->addServiceUUID(SERVICE_UUID);  // allerede gjort via advData

  // Du kan evt. sætte interval etc.
  // pAdvertising->setMinInterval(500);
  // pAdvertising->setMaxInterval(1000);
}
// -------- Security callbacks (part of NimBLEServerCallbacks)

uint32_t SerialBLEInterface::onPassKeyDisplay() {
  BLE_DEBUG_PRINTLN("onPassKeyDisplay()");
  return _pin_code;
}

void SerialBLEInterface::onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) {
  BLE_DEBUG_PRINTLN("onConfirmPassKey(%u)", pin);
  NimBLEDevice::injectConfirmPasskey(connInfo, true);
}

void SerialBLEInterface::onAuthenticationComplete(NimBLEConnInfo& connInfo) {
  if (connInfo.isEncrypted()) {
    if (_isEnabled) {
      BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
      deviceConnected = true;
    } else {
      BLE_DEBUG_PRINTLN("Auth success but not enabled, disconnecting");
      pServer->disconnect(connInfo.getConnHandle());
    }
  } else {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");
    pServer->disconnect(connInfo.getConnHandle());
    if (_isEnabled) {
      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    }
  }
}

// -------- NimBLEServerCallbacks methods

void SerialBLEInterface::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d, mtu=%d", connInfo.getConnHandle(), pServer->getPeerMTU(connInfo.getConnHandle()));
  last_conn_id = connInfo.getConnHandle();
}

void SerialBLEInterface::onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
  BLE_DEBUG_PRINTLN("onMtuChanged(), mtu=%d", MTU);
}

void SerialBLEInterface::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
  BLE_DEBUG_PRINTLN("onDisconnect(), reason=%d", reason);
  deviceConnected = false;
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- NimBLECharacteristicCallbacks methods

void SerialBLEInterface::onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
  std::string value = pCharacteristic->getValue();   // getData() is gone in NimBLE - getValue() gives us a safe copy
  size_t len = value.length();

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", (int)len);
  } else {
    Frame frame = {};
    frame.len = (uint8_t)len;
    memcpy(frame.buf, value.data(), len);

    if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {
      BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
    }
  }
}

// ---------- public methods

void SerialBLEInterface::clearBuffers() {
  xQueueReset(recv_queue);
  send_queue_len = 0;
}

void SerialBLEInterface::enable() {
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();

  // If disable() previously removeService()'d us, add it back before starting it again.
  // (Calling addService() when it was never removed would push a duplicate entry, so guard it.)
  if (pService->getRemoved()) {
    pServer->addService(pService);
  }

  // Start the service
  pService->start();

  // Start advertising

  //pServer->getAdvertising()->setMinInterval(500);
  //pServer->getAdvertising()->setMaxInterval(1000);

  pServer->getAdvertising()->start();
  adv_restart_time = 0;
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  pServer->getAdvertising()->stop();
  pServer->disconnect(last_conn_id);
  pServer->removeService(pService, false);   // hide the service (deleteSvc=false keeps pService/characteristics valid for re-adding in enable())
  oldDeviceConnected = deviceConnected = false;
  adv_restart_time = 0;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

#define  BLE_WRITE_MIN_INTERVAL   60

bool SerialBLEInterface::isReadBusy() const {
  return uxQueueMessagesWaiting(recv_queue) > 0;
}

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }

  Frame frame;
  if (xQueueReceive(recv_queue, &frame, 0) == pdTRUE) {
    memcpy(dest, frame.buf, frame.len);
    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", (uint32_t) frame.len, (uint32_t) dest[0]);
    return frame.len;
  }

  // Ghost-connect watchdog: don't rely solely on onDisconnect() having fired -
  // force deviceConnected back in sync with what the stack actually reports.
  if (pServer->getConnectedCount() == 0) deviceConnected = false;

  if (deviceConnected != oldDeviceConnected) {
    if (!deviceConnected) {    // disconnecting
      clearBuffers();

      BLE_DEBUG_PRINTLN("SerialBLEInterface -> disconnecting...");

      //pServer->getAdvertising()->setMinInterval(500);
      //pServer->getAdvertising()->setMaxInterval(1000);

      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> stopping advertising");
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> connecting...");
      // connecting
      // do stuff here on connecting
      pServer->getAdvertising()->stop();
      adv_restart_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
      pServer->getAdvertising()->start();  // re-Start advertising
    }
    adv_restart_time = 0;
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}