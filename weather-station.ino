#include <vector>
#include <DHT.h>

#define DEBUG 1
#define SWITCH "SWITCH"
#define SENSOR "SENSOR"

#include "ItemhubUtilities/ItemhubUtilities.h"
#include "ItemhubUtilities/Certs.h"

#define HOST "itemhub.io"
#define PORT 443
#define USER "{your device client id}"
#define PWD "{your device client secrets}"

bool BC26Init = false;
bool isAuth = false;
int BC26ResponseTimeoutCount = 0;

std::vector<ItemhubPin> pins;

String remoteDeviceId = "";
String token = "";
String user = USER;
String pwd = PWD;

const int intervalSensor = 5 * 1000;
const int intervalSwitch = 2000;
const int intervalDeviceState = 2000;

unsigned long currentSensorTimestamp;
unsigned long previousSensorTimestamp;
unsigned long currentSwitchTimestamp;
unsigned long previousSwitchTimestamp;
unsigned long currentDeviceStateTimestamp;
unsigned long previousDeviceStateTimestamp;

int sendSuccessFlag = 0;
DHT dht(PA1, DHT11);

void setup()
{
  Serial2.begin(115200);
  Serial2.println("setup");
  dht.begin();
  ItemhubUtilities::initBC26Serial();
  pins.push_back(ItemhubPin(PA0, "PA0", SENSOR)); // voltage
  pins.push_back(ItemhubPin(PA1, "PA1", SENSOR)); // humidity
  pins.push_back(ItemhubPin(PA4, "PA4", SENSOR)); // temperature
  pins.push_back(ItemhubPin(PA5, "PA5", SENSOR)); // rain
  ItemhubUtilities::SendATCommand("AT+QPOWD=2");
  delay(5000);
}

void loop()
{
  if (BC26ResponseTimeoutCount > 5)
  {
    Serial2.println("reset bc26");
    ItemhubUtilities::SendATCommand("AT+QPOWD=2");
    delay(5000);
    BC26ResponseTimeoutCount = 0;
    BC26Init = false;
  }

  while (!BC26Init)
  {
    Serial2.println("BC26 init");
    Serial2.print(".");
    BC26Init = ItemhubUtilities::BC26init(CA_PEM, BC26ResponseTimeoutCount);
    if (BC26ResponseTimeoutCount > 10)
    {
      break;
    }
    delay(500);
  }

  if (!BC26Init)
  {
    Serial2.print("timeout count:");
    Serial2.println(BC26ResponseTimeoutCount);
    return;
  }

  Serial2.println("");
  Serial2.println("initialization OK");

  if (remoteDeviceId == "")
  {
    String postBody = "{\"clientId\":\"" + user + "\",\"clientSecret\":\"" + pwd + "\"}";
    AuthResponse authResponse = ItemhubUtilities::Auth(HOST, PORT, postBody, BC26ResponseTimeoutCount);
    if (authResponse.token == "")
    {
      return;
    }
    token = authResponse.token;
    remoteDeviceId = authResponse.remoteDeviceId;
  }

  // itemhub device state
  currentDeviceStateTimestamp = millis();
  if (currentDeviceStateTimestamp - previousDeviceStateTimestamp > intervalDeviceState && remoteDeviceId != "" && sendSuccessFlag == 0)
  {
    previousDeviceStateTimestamp = currentDeviceStateTimestamp;
    String onlineResp = ItemhubUtilities::Online(HOST, PORT, remoteDeviceId, token, BC26ResponseTimeoutCount);
    if (onlineResp != "")
    {
      sendSuccessFlag = 1;
    }
  }

  // itemhub sensor
  currentSensorTimestamp = millis();
  if (currentSensorTimestamp - previousSensorTimestamp > intervalSensor && remoteDeviceId != "" && sendSuccessFlag == 1)
  {
    previousSensorTimestamp = currentSensorTimestamp;
    for (int i = 0; i < pins.size(); i++)
    {
      String mode = pins[i].mode;
      if (strcmp(mode.c_str(), SENSOR) == 0 && i == 0)
      {
        // voltage
        std::vector<int> arr;
        for (int i = 0; i < 30; i++)
        {
          double value = analogRead(pins[0].pin);
          arr.push_back(value);
        }

        for (int i = 0; i < 30; i++)
        {
          for (int j = i + 1; j < 30; j++)
          {
            if (arr[i] > arr[j])
            {
              int temp = arr[i];
              arr[i] = arr[j];
              arr[j] = temp;
            }
          }
        }

        int median = arr[15];
        double currentVoltage = 16.5 * median / 1024;
        pins[i].value = String(currentVoltage);
      }
      else if (strcmp(mode.c_str(), SENSOR) == 0 && i == 1)
      {
        pins[1].value = String(dht.readHumidity());
        pins[2].value = String(dht.readTemperature());
      }
      else if (strcmp(mode.c_str(), SENSOR) == 0 && i == 3)
      {
        pins[3].value = String(digitalRead(pins[3].pin));
      }
    }
    bool sendResult = ItemhubUtilities::SendSensor(HOST, PORT, token, remoteDeviceId, pins, BC26ResponseTimeoutCount);
    if (sendResult)
    {
      sendSuccessFlag = 2;
    }
  }

  if (sendSuccessFlag == 2)
  {
    sendSuccessFlag = 0;
    Serial2.println("sleep");
    ItemhubUtilities::Sleep(1800, BC26ResponseTimeoutCount);
  }
}