#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <M5Stack.h>
#include <Wire.h>

// Device Name: Maximum 30 bytes
#define DEVICE_NAME "LINE Things Thermometer"

// User service UUID: Change this to your generated service UUID
#define USER_SERVICE_UUID "{YOUR_SERVICE_UUID}"
// User service characteristics
#define WRITE_CHARACTERISTIC_UUID "E9062E71-9E62-4BC6-B0D3-35CDCD9B027B"
#define NOTIFY_CHARACTERISTIC_UUID "62FBD229-6EDD-4D1A-B554-5C4E1BB29169"

// PSDI Service UUID: Fixed value for Developer Trial
#define PSDI_SERVICE_UUID "E625601E-9E55-4597-A598-76018A0D293D"
#define PSDI_CHARACTERISTIC_UUID "26E2B12B-85F0-4F3F-9FDD-91D114270E6E"

BLEServer *thingsServer;
BLESecurity *thingsSecurity;
BLEService *userService;
BLEService *psdiService;
BLECharacteristic *psdiCharacteristic;
BLECharacteristic *writeCharacteristic;
BLECharacteristic *notifyCharacteristic;

bool deviceConnected = false;
bool oldDeviceConnected = false;

class serverCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
  }
};

class writeCallback : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *bleWriteCharacteristic)
  {
    std::string value = bleWriteCharacteristic->getValue();
  }
};

void setup()
{
  Serial.begin(115200);
  M5.Lcd.clear();

  BLEDevice::init("");
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_NO_MITM);

  // Security Settings
  BLESecurity *thingsSecurity = new BLESecurity();
  thingsSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);
  thingsSecurity->setCapability(ESP_IO_CAP_NONE);
  thingsSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  setupServices();
  startAdvertising();

  // M5Stack LCD Setup
  Wire.begin();
  M5.begin();
  M5.Lcd.clear();
  M5.Lcd.setTextColor(YELLOW);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(65, 10);
  M5.Lcd.println("Ready to Connect");
  Serial.println("Ready to Connect");
}

uint16_t result;
float temperature;
// temperature;

void loop()
{

  M5.update();

  temperature = getTemperature();
  M5.Lcd.fillRect(120, 100, 120, 100, BLACK);
  M5.Lcd.setCursor(120, 100);

  char buf[24];
  snprintf(buf, 24, "%.0f", temperature * 10);

  M5.Lcd.println(temperature);
  Serial.println(buf);

  delay(500);

  M5.Lcd.clear();

  notifyCharacteristic->setValue(buf);
  notifyCharacteristic->notify();

  // Disconnection
  if (!deviceConnected && oldDeviceConnected)
  {
    delay(500);                       // Wait for BLE Stack to be ready
    thingsServer->startAdvertising(); // Restart advertising
    oldDeviceConnected = deviceConnected;
    M5.Lcd.clear();
    M5.Lcd.setTextColor(YELLOW);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(65, 10);
    M5.Lcd.println("Ready to Connect");
  }
  // Connection
  if (deviceConnected && !oldDeviceConnected)
  {
    oldDeviceConnected = deviceConnected;
    M5.Lcd.clear();
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(100, 10);
    M5.Lcd.println("Connected");
  }
}

void setupServices(void)
{
  // Create BLE Server
  thingsServer = BLEDevice::createServer();
  thingsServer->setCallbacks(new serverCallbacks());

  // Setup User Service
  userService = thingsServer->createService(USER_SERVICE_UUID);
  // Create Characteristics for User Service
  writeCharacteristic = userService->createCharacteristic(WRITE_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE);
  writeCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  writeCharacteristic->setCallbacks(new writeCallback());

  notifyCharacteristic = userService->createCharacteristic(NOTIFY_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  notifyCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  BLE2902 *ble9202 = new BLE2902();
  ble9202->setNotifications(true);
  ble9202->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  notifyCharacteristic->addDescriptor(ble9202);

  // Setup PSDI Service
  psdiService = thingsServer->createService(PSDI_SERVICE_UUID);
  psdiCharacteristic = psdiService->createCharacteristic(PSDI_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);
  psdiCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);

  // Set PSDI (Product Specific Device ID) value
  uint64_t macAddress = ESP.getEfuseMac();
  psdiCharacteristic->setValue((uint8_t *)&macAddress, sizeof(macAddress));

  // Start BLE Services
  userService->start();
  psdiService->start();
}

void startAdvertising(void)
{
  // Start Advertising
  BLEAdvertisementData scanResponseData = BLEAdvertisementData();
  scanResponseData.setFlags(0x06); // GENERAL_DISC_MODE 0x02 | BR_EDR_NOT_SUPPORTED 0x04
  scanResponseData.setName(DEVICE_NAME);

  thingsServer->getAdvertising()->addServiceUUID(userService->getUUID());
  thingsServer->getAdvertising()->setScanResponseData(scanResponseData);
  thingsServer->getAdvertising()->start();
}

float getTemperature(void)
{
  Wire.beginTransmission(0x5A); // Send Initial Signal and I2C Bus Address
  Wire.write(0x07);             // Send data only once and add one address automatically.
  Wire.endTransmission(false);  // Stop signal
  Wire.requestFrom(0x5A, 2);    // Get 2 consecutive data from 0x5A, and the data is stored only.
  result = Wire.read();         // Receive DATAd
  result |= Wire.read() << 8;   // Receive DATA

  temperature = result * 0.02 - 273.15;

  return temperature;
}
