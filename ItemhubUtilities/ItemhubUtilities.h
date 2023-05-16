#include <math.h>
#include <ArduinoJson.h>
#include <STM32LowPower.h>

HardwareSerial BC26Serial(PA10, PA9);

class ItemhubPin
{
public:
  ItemhubPin(byte pin, String pinString, String mode)
  {
    this->pin = pin;
    this->pinString = pinString;
    this->mode = mode;
    if (this->mode == "SWITCH")
    {
      pinMode(pin, OUTPUT);
    }
    if (this->mode == "SENSOR")
    {
      pinMode(pin, INPUT);
    }
  }
  byte pin;
  String pinString;
  String mode;
  String value;
};

class AuthResponse
{
public:
  AuthResponse(String token, String remoteDeviceId)
  {
    this->token = token;
    this->remoteDeviceId = remoteDeviceId;
  }
  String token;
  String remoteDeviceId;
};

class ItemhubUtilities
{
public:
  static void initBC26Serial()
  {
    BC26Serial.begin(115200);
  }

  static String Online(String host, int port, String &remoteDeviceId, String token, int &timeoutCount)
  {
    debug("online");
    String deviceOnlineEndpoint = "/api/v1/my/devices/" + remoteDeviceId + "/online";
    String resp = ItemhubUtilities::Send(host, port, "POST", deviceOnlineEndpoint, "", token, timeoutCount);
    int httpStatus = GetHttpStatus(resp);
    if (httpStatus == 401 || httpStatus == 403)
    {
      remoteDeviceId = "";
    }
    return resp;
  }

  static AuthResponse Auth(String host, int port, String postBody, int &timeoutCount)
  {
    debug("auth");
    String resp = Send(host, port, "POST", "/api/v1/oauth/exchange-token-for-device", postBody, "", timeoutCount);
    if (resp == "")
    {
      return AuthResponse("", "");
    }
    String body = ItemhubUtilities::ExtractBody(resp);
    String token = ItemhubUtilities::GetJsonValue(body, "token", 256);
    String remoteDeviceId = ItemhubUtilities::GetJsonValue(body, "deviceId", 256);
    return AuthResponse(token, remoteDeviceId);
  }

  static bool CheckSwitchState(String host, int port, String token, String &remoteDeviceId, std::vector<ItemhubPin> &pins, int &timeoutCount)
  {
    debug("check switch state");
    String deviceStateEndpoint = "/api/v1/my/devices/" + remoteDeviceId + "/switches";
    String emptyString = "";
    String resp = ItemhubUtilities::Send(host, port, "GET", deviceStateEndpoint, "", token, timeoutCount);
    if (resp == "")
    {
      return false;
    }
    int httpStatus = GetHttpStatus(resp);
    if (httpStatus == 401 || httpStatus == 403)
    {
      remoteDeviceId = "";
      return false;
    }
    debug(ItemhubUtilities::ExtractBody(resp).c_str());
    StaticJsonDocument<600> doc;
    deserializeJson(doc, ItemhubUtilities::ExtractBody(resp));
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0)
    {
      return true;
    }
    for (int i = 0; i < pins.size(); i++)
    {
      if (strcmp(pins[i].mode.c_str(), SWITCH) != 0)
      {
        continue;
      }
      for (auto item : arr)
      {
        const char *pin = item["pin"];
        int value = item["value"].as<int>();
        String stringValue = String(value);
        if (strcmp(pins[i].pinString.c_str(), pin) == 0 && value == 0)
        {
          digitalWrite(pins[i].pin, LOW);
        }
        else if (strcmp(pins[i].pinString.c_str(), pin) == 0 && value == 1)
        {
          digitalWrite(pins[i].pin, HIGH);
        }
      }
      return true;
    }
  }

  static bool SendSensor(String host, int port, String token, String &remoteDeviceId, std::vector<ItemhubPin> &pins, int &timeoutCount)
  {
    debug("send sensor");
    String devicePinDataEndpoint = "/api/v1/my/devices/" + remoteDeviceId;
    int shouldBeSendCount = 0;
    int sentCount = 0;
    for (int i = 0; i < pins.size(); i++)
    {
      String mode = pins[i].mode;
      if (strcmp(mode.c_str(), SENSOR) == 0)
      {
        shouldBeSendCount++;
        String endpoint = devicePinDataEndpoint + "/sensors/" + pins[i].pinString;
        String postBody = "{\"value\":" + pins[i].value + "}";
        String resp = ItemhubUtilities::Send(host, port, "POST", endpoint, postBody, token, timeoutCount);
        if (resp == "")
        {
          continue;
        }
        int httpStatus = ItemhubUtilities::GetHttpStatus(resp);
        if (httpStatus == 401 || httpStatus == 403)
        {
          remoteDeviceId = "";
          return false;
        }
        sentCount++;
      }
    }
    return (shouldBeSendCount == sentCount);
  }

  static int
  GetHttpStatus(String resp)
  {
    int startOfHttpMeta = resp.indexOf("HTTP/1.1");
    if (startOfHttpMeta != -1)
    {
      String httpStatus = resp.substring(startOfHttpMeta + 9, startOfHttpMeta + 9 + 3);
      return ToBase10((char *)httpStatus.c_str(), httpStatus.length(), 10);
    }
    return 500;
  }

  static String GetJsonValue(String jsonString, const char *fieldName, int size)
  {
    DynamicJsonDocument doc(size);
    deserializeJson(doc, jsonString.c_str());
    String test = doc[fieldName];
    return test;
  }

  static String ExtractBody(String resp)
  {
    String keepAlive = "keep-alive";
    int contentLengthStart = resp.indexOf(keepAlive) + keepAlive.length() + 4;
    int contentLengthEnd = resp.indexOf("\n", contentLengthStart + 1);
    String hexContentLength = resp.substring(contentLengthStart, contentLengthEnd - 1);
    int contentLength = ToBase10((char *)hexContentLength.c_str(), hexContentLength.length(), 16);
    String respBody = resp.substring(contentLengthEnd + 1, contentLengthEnd + 1 + contentLength);
    return respBody;
  }

  static void SendATCommand(String command)
  {
    debug(command.c_str());
    BC26Serial.println(command);
  }

  static String GetBC26Data()
  {
    String data;
    long begin = millis();
    while (BC26Serial.available() && millis() - begin <= 5000)
    {
      delay(10);
      char c = BC26Serial.read();
      if (c == '\n')
      {
        break;
      }
      data += c;
    }
    data.trim();
    return data;
  }

  static unsigned int ToBase10(char *d_str, int len, int base)
  {
    if (len < 1)
    {
      return 0;
    }
    char d = d_str[0];
    // chars 0-9 = 48-57, chars a-f = 97-102
    int val = (d > 57) ? d - ('a' - 10) : d - '0';
    int result = val * pow(base, (len - 1));
    d_str++; // increment pointer
    return result + ToBase10(d_str, len - 1, base);
  }

  static bool WaitingBC26ResponseMessage(String message, unsigned int timeout)
  {
    unsigned long begin = millis();
    String data = GetBC26Data();

    while (data && millis() - begin < timeout)
    {
      String data = GetBC26Data();
      if (data == message)
      {
        return true;
      }
    }
    return false;
  }

  static bool WaitingBC26ResponseMessageStartsWith(String message, unsigned int timeout)
  {
    unsigned long begin = millis();
    String data = GetBC26Data();

    while (data && millis() - begin < timeout)
    {
      String data = GetBC26Data();
      if (data.startsWith(message))
      {
        return true;
      }
    }
    return false;
  }

  static bool WaitingBC26ResponseMessageEndsWith(String message, unsigned int timeout)
  {
    unsigned long begin = millis();
    String data = GetBC26Data();

    while (data && millis() - begin < timeout)
    {
      String data = GetBC26Data();
      if (data.endsWith(message))
      {
        return true;
      }
    }
    return false;
  }

  static bool BC26init(char *ca, int &timeoutCount)
  {
    SendATCommand("AT");
    if (!WaitingBC26ResponseMessage("OK", 2000))
    {
      timeoutCount++;
      return false;
    }

    SendATCommand("AT+CGPADDR=1");
    if (!WaitingBC26ResponseMessageStartsWith("+CGPADDR:", 5000))
    {
      timeoutCount++;
      return false;
    }

    SendATCommand("AT+QIDNSCFG=1,8.8.8.8");
    if (!WaitingBC26ResponseMessage("OK", 5000))
    {
      timeoutCount++;
      return false;
    }

    SendATCommand("AT+QSSLCFG=1,0,\"seclevel\",1");
    if (!WaitingBC26ResponseMessage("OK", 5000))
    {
      timeoutCount++;
      return false;
    }
    SendATCommand("AT+QSSLCFG=1,0,\"cacert\"");
    if (!WaitingBC26ResponseMessage(">", 5000))
    {
      timeoutCount++;
      return false;
    }

    delay(500);
    BC26Serial.print(ca);

    delay(50);
    BC26Serial.write(0x1a);

    if (!WaitingBC26ResponseMessageStartsWith("OK", 5000))
    {
      timeoutCount++;
      return false;
    }

    SendATCommand("AT+QSSLCLOSE=1,0");
    delay(5000);
    timeoutCount = 0;
    return true;
  }

  static String Send(String host, int port, String method, String path, String postBody, String token, int &timeoutCount)
  {

    SendATCommand("AT+QSSLOPEN=1,0,\"" + host + "\"," + port + ",0");
    if (!WaitingBC26ResponseMessage("OK", 60000))
    {
      debug("AT+QSSLOPEN timeout");
      timeoutCount++;
      return "";
    }

    debug("waiting ssl connection opened");
    if (!WaitingBC26ResponseMessageStartsWith("+QSSLOPEN", 60000))
    {
      debug("QSSLOPEN failed");
      SendATCommand("AT+QSSLCLOSE=1,0");
      delay(1000);
      timeoutCount++;
      return "";
    }

    debug("SSL connection ready");
    String requestHeader = method + " " + path + " HTTP/1.1\nHost: " + host + "\nUser-Agent: ItemHub\nContent-Type: application/json\nAccept: */*\n";
    if (token.length() > 0)
    {
      requestHeader += "Authorization: Bearer " + token + "\n";
    }
    char sslSendCommand[20];

    String postMeta = "";
    if (method == "POST")
    {
      postMeta = "Content-Length: " + String(postBody.length()) + "\n\n";
    }

    sprintf(sslSendCommand, "AT+QSSLSEND=1,0,%d", requestHeader.length() + postBody.length() + postMeta.length() + 1);
    SendATCommand(sslSendCommand);
    if (!WaitingBC26ResponseMessage(">", 60000))
    {
      debug("QSSLSEND failed");
      SendATCommand("AT+QSSLCLOSE=1,0");
      delay(1000);
      timeoutCount++;
      return "";
    }

    delay(500);
    debug("Begin Request");

    if (method == "POST")
    {
      BC26Serial.print(requestHeader);
      BC26Serial.print(postMeta);
      BC26Serial.print(postBody);
    }
    else
    {
      BC26Serial.print(requestHeader);
    }
    BC26Serial.print("\n");
    if (!WaitingBC26ResponseMessageStartsWith("+QSSLSEND", 5000))
    {
      debug("QSSLSEND timeout");
      timeoutCount++;
      return "";
    }
    long begin = millis();
    String resp = "";
    while (millis() - begin < 5 * 1000)
    {
      if (BC26Serial.available())
      {
        resp += (char)BC26Serial.read();
      }
    }
    String receivedStartOfMessage = "+QSSLURC: \"recv\",";
    int startOfAT = resp.indexOf(receivedStartOfMessage);
    if (startOfAT == -1)
    {
      debug("received from server error");
      return "";
    }

    int startOfLengthResp = resp.indexOf(",", startOfAT + receivedStartOfMessage.length() + 2);
    int endOfLengthResp = resp.indexOf(",", startOfLengthResp + 1);
    String respLengthString = resp.substring(startOfLengthResp + 1, endOfLengthResp);
    int respLength = ToBase10((char *)respLengthString.c_str(), respLengthString.length(), 10);

    int startOfResp = resp.indexOf("\"", startOfAT + receivedStartOfMessage.length()) + 1;
    String respBody = resp.substring(startOfResp, startOfResp + respLength);

    SendATCommand("AT+QSSLCLOSE=1,0");
    if (!WaitingBC26ResponseMessage("OK", 10000))
    {
      debug("close ssl connection timeout");
      return "";
    }
    return respBody;
  }

  static void Sleep(int seconds, int &timeoutCount)
  {
    SendATCommand("AT+QPOWD=0");
    if (!WaitingBC26ResponseMessage("OK", 5000))
    {
      debug("power down off timeout");
      timeoutCount++;
      return;
    }
    delay(500);
    LowPower.deepSleep(seconds * 1000);
  }

  static void debug(const char *msg)
  {
#ifdef DEBUG
    Serial2.println(msg);
#endif
  }
};
