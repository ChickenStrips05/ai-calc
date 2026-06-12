/* - ChickenStrips05 (@chickenstrips05) - Luke
   - Inspired by and used code from https://github.com/chromalock/TI-32/ (@chromalock)
   - Using ArTICL https://github.com/KermMartian/ArTICL by @KermMartian
*/

#include <CBL2.h>
#include "TIVar.h"
#include "launcher.h" // for future use
#include <WiFi.h>
#include <HTTPClient.h>
CBL2 cbl;

int lineRed = 7;
int lineWhite = 6;
const auto PASSWORD = 0; // any real number
bool unlocked = false;
const char* SSID = "iphone12"; 
const char* WIFI_PASSWORD = "calculator";

long responseStatus = -1;
String responseMessage = "";
bool currentlyWorking = false;
SemaphoreHandle_t responseMutex;
String userMessage = "";

String sourceAddress = "https://raw.githubusercontent.com/ChickenStrips05/ai-calc/refs/heads/main/req.txt";
String serverAddress = "http://209.227.162.180/";

#define MAXDATALEN 255
uint8_t header[16];
uint8_t data[MAXDATALEN];

int onReceived(uint8_t type, enum Endpoint model, int datalen);
int onRequested(uint8_t type, enum Endpoint model, int* headerlen, int* datalen, data_callback* data_callback);

int varIndex(int idx) {
  return idx == 9 ? 0 : (idx + 1);
}

String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char buf[4];

  for (size_t i = 0; i < str.length(); i++) {
    c = str.charAt(i);

    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }

  return encoded;
}


void sendMessage(void* parameter) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wifi connected");
    if (xSemaphoreTake(responseMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      Serial.println("took response mutex");
      if (currentlyWorking) {
        xSemaphoreGive(responseMutex);
        return;
      }
      currentlyWorking = true;
      responseStatus = 0;
      Serial.println("began request");
      String url = serverAddress + "generate?q=" + urlEncode(userMessage);
      Serial.print("Sending request to ");
      Serial.println(url);
      HTTPClient http;
      http.begin(url);
      int httpCode = http.GET();
      Serial.println(httpCode);
      if (httpCode == 200) {
        String localPayload = http.getString();
        responseMessage = localPayload;
        responseStatus = 1;
        Serial.println("request finished");
      } else {
        responseStatus = -1;
        responseMessage = "";
      }
      http.end();
      currentlyWorking = false;
      xSemaphoreGive(responseMutex);
    }
  }

  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Hello world");
  cbl.setLines(lineRed, lineWhite);
  cbl.resetLines();
  // cbl.setVerbosity(true, &Serial);
  cbl.setupCallbacks(header, data, MAXDATALEN, onReceived, onRequested);

  responseMutex = xSemaphoreCreateMutex();
}

void loop() {
  int rval;
  rval = cbl.eventLoopTick();
  if (rval && rval != ERR_READ_TIMEOUT) {
    Serial.print("Failed to run eventLoopTick: code ");
    Serial.println(rval);
  }
}

int onReceived(uint8_t type, enum Endpoint model, int datalen) {
  if (type == VarTypes82::VarReal) {
    char varName = header[3];


    if (varName == 'P' && !unlocked) {
      auto password = TIVar::realToLong8x(data, model);

      if (password == PASSWORD) {
        Serial.println("Unlocked calculator");
        unlocked = true;
        return 0;
      } else {
        Serial.println("Failed to unlock");
        return -1;
      }
    }

    if (varName == 'C' && unlocked) {
      auto command = TIVar::realToLong8x(data, model);
      Serial.print("Received command: ");
      Serial.println(command);

      if (command == 0) {
        // connect to wifi
        Serial.println("Connecting to wifi");
        if (WiFi.status() != WL_CONNECTED) {
          WiFi.begin(SSID, WIFI_PASSWORD);
          while (WiFi.status() != WL_CONNECTED) {
            delay(30);
          }
          Serial.println("Connected to wifi");
          Serial.print("IP: ");
          Serial.println(WiFi.localIP());
        }
        return 0;
      } else if (command == 1) {
        // disconnect from wifi
        Serial.println("Disconnecting from wifi");
        WiFi.disconnect();
        return 0;
      } else if (command == 2) {
        // fetch new server address
        Serial.println("Refreshing server address");
        if (WiFi.status() == WL_CONNECTED) {
          HTTPClient http;
          http.begin(sourceAddress);
          int httpCode = http.GET();
          Serial.println(httpCode);
          if (httpCode == 200) {
            serverAddress = http.getString();
            serverAddress.trim();
            Serial.print("Set server address to ");
            Serial.println(serverAddress);
          }

          http.end();
          return 0;
        }
      }
    }

    else {
      return -1;
    }

  }

  else if (type == VarTypes82::VarString) {
    int strIndex = varIndex(header[4]);
    if (strIndex == 0) {
      String message = TIVar::strVarToString8x(data, model);
      Serial.print("Received message: ");
      userMessage = message;
      Serial.println(userMessage);
      if (WiFi.status() == WL_CONNECTED) {
        // create request on another thred to allow Get(R) while request is being processed
        xTaskCreate(sendMessage, "Send_Message", 4096, NULL, 1, NULL);
      } else {
        responseStatus = -1;
      }
      return 0;
    }

    return -1;

  } else {
    Serial.println("Unknown variable");
    return -1;
  }
}

int onRequested(uint8_t type, enum Endpoint model, int* headerlen, int* datalen, data_callback* data_callback) {
  char varName = header[3];
  int strIndex = header[4];
  int realStrIndex = varIndex(header[4]);

  if (type == VarTypes82::VarString && realStrIndex == 1) {
    if (xSemaphoreTake(responseMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      String currentResponse = responseMessage;
      xSemaphoreGive(responseMutex);
      *datalen = TIVar::stringToStrVar8x(currentResponse, data, model);
    } else {
      *datalen = TIVar::stringToStrVar8x("Failed to take lock", data, model);
    }

    TIVar::intToSizeWord(*datalen, header);
    header[2] = VarTypes82::VarString;
    header[3] = 0xAA;
    header[4] = strIndex;
    *headerlen = 13;
    return 0;
  }

  // server source and address
  if (type == VarTypes82::VarString && (realStrIndex == 2 || realStrIndex == 3)) {
    *datalen = TIVar::stringToStrVar8x(realStrIndex == 2 ? sourceAddress : serverAddress, data, model);

    TIVar::intToSizeWord(*datalen, header);
    header[2] = VarTypes82::VarString;
    header[3] = 0xAA;
    header[4] = strIndex;
    *headerlen = 13;
    return 0;
  }

  if (type == VarTypes82::VarReal) {
    long currentStatus = 0;
    switch (varName) {
      case 'R':
        if (xSemaphoreTake(responseMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          currentStatus = responseStatus;
          xSemaphoreGive(responseMutex);
          *datalen = TIVar::longToReal8x(currentStatus, data, model);
        } else {
          *datalen = TIVar::longToReal8x(0, data, model);
        }

        TIVar::intToSizeWord(*datalen, header);
        header[2] = VarTypes82::VarReal;
        header[3] = 'R';
        header[4] = '\0';
        *headerlen = 13;
        Serial.print("Sent response status ");
        Serial.println(currentStatus);
        break;

      case 'W':
        *datalen = TIVar::longToReal8x(WiFi.isConnected() ? 1 : 0, data, model);
        TIVar::intToSizeWord(*datalen, header);
        header[2] = VarTypes82::VarReal;
        header[3] = 'W';
        header[4] = '\0';
        *headerlen = 13;
        Serial.println("Sent WiFi status");
        break;

      case 'U':
        *datalen = TIVar::longToReal8x(unlocked ? 1 : -1, data, model);
        TIVar::intToSizeWord(*datalen, header);
        header[2] = VarTypes82::VarReal;
        header[3] = 'U';
        header[4] = '\0';
        *headerlen = 13;
        Serial.println("Sent unlock status");
        break;

      default:
        return -1;
    }

    return 0;
  } else {
    return -1;
  }
}
