#include "WiFi.h"
#include "BLEDevice.h"
#include "PubSubClient.h"

// --------------------------------------------------------------------------------------------
//        UPDATE CONFIGURATION TO MATCH YOUR ENVIRONMENT
// --------------------------------------------------------------------------------------------

// Watson IoT connection details
#define MQTT_HOST "<YOUR_ORG>.messaging.internetofthings.ibmcloud.com"
#define MQTT_PORT 1883
#define MQTT_DEVICEID "d:<YOUR_ORG>:ESP32:ESPRESSIF"
#define MQTT_USER "use-token-auth"
#define MQTT_TOKEN "<YOUR_AUTH_TOKEN>"
#define MQTT_TOPIC "iot-2/evt/status/fmt/json"
#define MQTT_TOPIC_DISPLAY "iot-2/cmd/display/fmt/json"

// Add WiFi connection information
char ssid[] = "<YOUR_WIFI>";     //  your network SSID (name)
char pass[] = "<YOUR_WIFI_PASSWORD>";  // your network password

// MQTT objects
void callback(char* topic, byte* payload, unsigned int length);
WiFiClient wifiClient;
PubSubClient mqtt(MQTT_HOST, MQTT_PORT, callback, wifiClient);

void callback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] : ");
  
  payload[length] = 0; // ensure valid content is zero terminated so can treat as c-string
  Serial.println((char *)payload);
}

// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000fff0-0000-1000-8000-00805f9b34fb");
// The characteristic of the remote service we are interested in.
static BLEUUID    char1UUID("0000fff1-0000-1000-8000-00805f9b34fb");
static BLEUUID    char2UUID("0000fff2-0000-1000-8000-00805f9b34fb");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLERemoteCharacteristic* wRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    char content[50] = "";
    for (int i = 0; i < length; i++)
    {
      sprintf(&content[i*2], "%02x", pData[i]);
    }
    int runningspeed  = pData[10]/10;
    int seconds = pData[3];
    int minutes = pData[4];
    int distance_meters = pData[6];
    int distance_kilometers = pData[5];
    int total_distance = distance_kilometers * 1000 + (distance_meters * 10);
    int started = pData[14];
    String treadmill_status = "Not Running";

    Serial.print("Content: "); Serial.println(content);
    
    if (started == 3) {
      treadmill_status = "Running";
      Serial.print("runningspeed/seconds/minutes/distance:  "); 
      Serial.print(runningspeed);Serial.print(" ");
      Serial.print(seconds);Serial.print(" ");
      Serial.print(minutes);Serial.print(" ");
      Serial.println(total_distance);
    }
    if (started == 5) {
      treadmill_status = "Stopped";
      Serial.println("Stopped");
    }

     // Send data to Watson IoT Platform
    String payload = "{ \"d\" : {";
    payload += "\"Minutes\":\""; payload += minutes; payload += "\",";
    payload += "\"Seconds\":\""; payload += seconds; payload += "\",";
    payload += "\"Speed\":\""; payload += runningspeed; payload += "\",";    
    payload += "\"Distance\":\""; payload += total_distance; payload += "\",";
    payload += "\"Status\":\""; payload += treadmill_status; payload += "\"";
    payload += "}}";

    if (mqtt.publish(MQTT_TOPIC, (char*) payload.c_str())) {
      //Serial.println("Publish ok");
    } else {
      Serial.println("Publish failed");
    }

}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

bool connectToServer() {
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remote BLE Server.
    pClient->connect(myDevice);
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");

    // Obtain a reference to the first characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(char1UUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our first characteristic UUID: ");
      Serial.println(char1UUID.toString().c_str());
      pClient->disconnect();
      return false;
    }

    if(pRemoteCharacteristic->canNotify()) {
      Serial.println("Setting notifyCallback");
      pRemoteCharacteristic->registerForNotify(notifyCallback);
    }

    // Obtain a reference to the second characteristic in the service of the remote BLE server.
    wRemoteCharacteristic = pRemoteService->getCharacteristic(char2UUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our second characteristic UUID: ");
      Serial.println(char2UUID.toString().c_str());
      pClient->disconnect();
      return false;
    }

    connected = true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
 /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks

const uint8_t getdata[] = {0xf0, 0xc3, 0x03, 0x0, 0x0, 0x0, 0xb6};
bool onoff = true;
void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  
  // Start WiFi connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi Connected");

  // Connect to MQTT - IBM Watson IoT Platform
  if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) {
    Serial.println("MQTT Connected");
    mqtt.subscribe(MQTT_TOPIC_DISPLAY);

  } else {
    Serial.println("MQTT Failed to connect!");
    ESP.restart();
  }

  // Start BLE connection
  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} // End of setup.

// This is the Arduino main loop function.
void loop() {

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
      connected = true;
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  //  If we are connected to a peer BLE Server, update the characteristic each time we are reached
  //  with the current time since boot.
  if (connected) {
    String newValue = "Writing to chracteristic";
    
    wRemoteCharacteristic->writeValue((uint8_t*)getdata, 7, true);
  }else if(doScan){
    BLEDevice::getScan()->start(0);  // this is just eample to start scan after disconnect, most likely there is better way to do it in arduino
  }
  
  delay(1000); // Delay a second between loops.
} // End of loop
