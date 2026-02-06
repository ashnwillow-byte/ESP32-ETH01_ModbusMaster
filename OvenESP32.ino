#include <TimeLib.h>
#include <ETH.h>
#include <ModbusMaster.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <ESP32Ping.h> 

#define ETH_TYPE ETH_PHY_LAN8720
#define ETH_MDC_PIN 23
#define ETH_MDIO_PIN 18
#define ETH_POWER_PIN 16
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN  

#define DE_PIN 2

ModbusMaster node;
const unsigned long MY_TIMEOUT = 200;  // Таймаут ответа от slave

#define RX_PIN 5
#define TX_PIN 17

HardwareSerial mySerial(2);

// NTP для времени
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600 * 3, 1800000);  // UTC+3, обновление каждые 60 сек

// Настройки отправки
const char* server_url = "http://172.16.1.44:5000/upload";  // IP сервера, порт 5000 (замени на реальный)
const char* api_key = "secret_key";  // Ключ для аутентификации (согласуй с админом)

// Массив для хранения данных опроса (для 2 устройств, расширь до 30 при необходимости)
float temperatures[32];
float setpoints[32];
float powers[32];
bool data_valid[32];

// Буфер для неудачных отправок (до 10 пакетов)
String buffer[2];
int buffer_index = 0;

// Структура моделей
struct DeviceInfo {
  uint8_t slave_id;
  const char* model;
};

// Структура регистров
struct RegisterMap {
  const char* model;
  uint16_t temperature;
  uint16_t setpoint;
  uint16_t power;
  const char* endian;
};

// Словарь устройств (slave_id → model)
const DeviceInfo devices[] = {
  {1, "TRM10"},  //1-я печь
  {2, "TRM10"},
  {3, "TRM10"},
  {4, "TRM10"},
  {5, "TRM10"},
  {6, "TRM10"},
  {7, "TRM10"},
  {8, "TRM10"},
  {9, "TRM210"}, //2-я печь
  {10, "TRM210"},
  {11, "TRM210"},
  {12, "TRM210"},
  {13, "TRM210"},
  {14, "TRM210"},
  {15, "TRM210"},
  {16, "TRM210"},
  {17, "TRM210"}, //3-я печь
  {18, "TRM210"},
  {19, "TRM210"},
  {20, "TRM210"},
  {21, "TRM210"},
  {22, "TRM210"},
  {23, "TRM210"},
  {24, "TRM210"}, 
  {25, "TRM210"}, //4-я печь
  {26, "TRM210"},
  {27, "TRM210"},
  {28, "TRM210"},
  {29, "TRM210"},
  {30, "TRM210"},
  {31, "TRM10"},
  {32, "TERMODAT"}
};
const int DEVICES_COUNT = sizeof(devices) / sizeof(devices[0]);

// Словарь регистров (model → регистры: температура, уставка, мощность)
// 0xFFFF - Нет регистра
const RegisterMap registers[] = {
  {"TRM10",  0x0000, 0x0200, 0x0206, "little"},
  {"TRM210",  0x1009, 0x100B, 0xFFFF, "big"},
  {"TERMODAT",   0xFFFF, 0xFFFF, 0xFFFF, "big"}
};
const int REGISTERS_COUNT = sizeof(registers) / sizeof(registers[0]);

// Поиск модели по slave_id
const char* getModelBySlaveId(uint8_t slave_id) {
  for (int i = 0; i < DEVICES_COUNT; i++) {
    if (devices[i].slave_id == slave_id) {
      return devices[i].model;
    }
  }
  return nullptr;  // Не найдено
}

// Поиск регистров по модели
const RegisterMap* getRegistersByModel(const char* model) {
  for (int i = 0; i < REGISTERS_COUNT; i++) {
    if (strcmp(registers[i].model, model) == 0) {
      return &registers[i];
    }
  }
  return nullptr;  // Не найдено
}

// Функция для начала приема
void preTransmission() { 
  delay(1);
  digitalWrite(DE_PIN, 1);
  //digitalWrite(RE_PIN, 1);
  delay(20);
}

// Функция для конца приема
void postTransmission() {
  delay(1);
  digitalWrite(DE_PIN, 0);
  //digitalWrite(RE_PIN, 0);
  delay(20);
}

float transform_regs(
    int16_t val1,   // первый регистр (может быть знаковым)
    uint16_t val2,  // второй регистр (может быть знаковым)
    const char* endian
) {
  
    union {
        uint8_t bytes[4];
        float f;
    } converter;

    if (strcmp(endian, "little") == 0) {
        // Little Endian: low-byte first в каждом регистре
        converter.bytes[0] = val1 & 0xFF;           // младший байт val1
        converter.bytes[1] = (val1 >> 8) & 0xFF;    // старший байт val1
        converter.bytes[2] = val2 & 0xFF;           // младший байт val2
        converter.bytes[3] = (val2 >> 8) & 0xFF;    // старший байт val2
    } 
    else if (strcmp(endian, "big") == 0) {
        // Big Endian: объединяем val1 (старшие 16 бит) и val2 (младшие 16 бит)
        // в 32-битное целое, затем копируем биты в float
        uint32_t combined = (static_cast<uint32_t>(val1) << 16) | static_cast<uint32_t>(val2);
        
        memcpy(converter.bytes, &combined, sizeof(uint32_t));
    }
    else {
        // Неизвестный формат: возвращаем NaN и логируем ошибку
        #ifdef SERIAL_DEBUG
        Serial.print("Error: Unknown endian format '");
        Serial.print(endian);
        Serial.println("'");
        #endif
        return NAN;
    }

    return converter.f;
}



float get_value(uint16_t reg, int slave_id, HardwareSerial& mySerial, const char* Endian) {
  // Переинициализируем узел с новым ID
  node.begin(slave_id, mySerial);
  
  unsigned long startTime = millis(); // Ставим таймер
  // Отправляем запрос
  uint8_t result = node.readHoldingRegisters(reg, 2);

  // Если запрос длился дольше нашего таймаута — считаем ошибкой
  if (millis() - startTime > MY_TIMEOUT) {
    result = node.ku8MBResponseTimedOut;
  }

  if (result == node.ku8MBSuccess) {
    int16_t value1 = (int16_t)node.getResponseBuffer(0);
    uint16_t value2 = (uint16_t)node.getResponseBuffer(1);
    return transform_regs(value1, value2, Endian);
  } else {
    Serial.print("Ошибка запроса для Slave ID или Timeout");
    return NAN;
  }
}

void send_data(String json_payload) {
  HTTPClient http;

  // Выводим URL для проверки
  Serial.print("Sending to: ");
  Serial.println(server_url);

  http.begin(server_url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", api_key);  // Аутентификация
  http.setTimeout(2000);

  int httpResponseCode = http.POST(json_payload);

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response: ");
    Serial.println(httpResponseCode);

    // Выводим тело ответа (если есть)
    String response = http.getString();
    Serial.print("Response: ");
    Serial.println(response);
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(http.errorToString(httpResponseCode));
    // Буферизация
    if (buffer_index < 2) {
      buffer[buffer_index++] = json_payload;
    }
    // Дополнительно: проверьте, удалось ли установить соединение
    if (httpResponseCode == -1) {
      Serial.println("Connection failed (check IP/port/firewall)");
    }
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  pinMode(DE_PIN, OUTPUT);
  //pinMode(RE_PIN, OUTPUT);

  mySerial.begin(115200, SERIAL_8N2, RX_PIN, TX_PIN);
  while (!mySerial) delay(10);

  

  // Ручное питание PHY
  pinMode(ETH_POWER_PIN, OUTPUT);
  digitalWrite(ETH_POWER_PIN, LOW);
  delay(100);

  // Инициализация Ethernet
  ETH.begin(ETH_TYPE, 1, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_POWER_PIN, ETH_CLK_MODE);
  ETH.setHostname("ESP32-ETH01");

  // Ожидание link UP (до 10 сек)
  unsigned long startTime = millis();
  while (!ETH.linkUp() && millis() - startTime < 10000) {
    Serial.print(".");
    delay(500);
  }

  if (!ETH.linkUp()) {
    Serial.println("❌ Link not UP. Check connections.");
    // Не продолжай с сетевыми функциями
  } else {
    // Для DHCP: ничего не делай, или 
    //ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    delay(10000);  // Жди присвоения IP
    Serial.println("✅ Link UP!");
    Serial.print("IP: "); Serial.println(ETH.localIP());
    }
  
    Serial.print("Pinging 172.16.1.44: ");
    if (Ping.ping("172.16.1.44", 3)) {  // 3 попытки
      Serial.println("SUCCESS");
    } else {
      Serial.println("FAILED");
    }

  // Modbus

  Serial.println("Modbus setup...");

  preTransmission();

  node.begin(1, mySerial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("✅ Modbus ready to work");
}

String getISO8601Time(unsigned long epochTime) {
  char buffer[25];
  // Формат: YYYY-MM-DDTHH:MM:SS
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d",
           year(epochTime), month(epochTime), day(epochTime),
           hour(epochTime), minute(epochTime), second(epochTime));
  return String(buffer);
}

void loop() {

  // Синхронизация времени
  timeClient.update();

  for (int id = 9; id <= 16; id++) { // Опрашиваем все id (1-32)
    
    const char* model = getModelBySlaveId(id); // Получаем модель прибора

    if (model) {   //Модель найдена
      Serial.print("Slave id: ");
      Serial.print(id);
      Serial.print(" Model: ");
      Serial.print(model);

      const RegisterMap* regs = getRegistersByModel(model); // Получаем регистры

      if (regs) {
        Serial.print(" Registers: [");
        Serial.print("Temperature register: 0x");
        Serial.print(regs->temperature, HEX);
        Serial.print(" ,Setpoint register: 0x");
        Serial.print(regs->setpoint, HEX);
        Serial.print(" ,Power register: 0x");
        Serial.print(regs->power, HEX);
        Serial.print(" ,Endian order: ");
        Serial.print(regs->endian);
        Serial.println("]");

        if(regs->temperature != 0xFFFF){
          float temp = get_value(regs->temperature, id, mySerial, regs->endian);
          temperatures[id-1] = temp;
          data_valid[id-1] = !isnan(temp);
            if (!isnan(temp)) {
              Serial.print(" ---- Температура: ");
              Serial.println(temp, 6);
            } else {
              Serial.println("Нет данных");
            }
        }
        if (regs->setpoint != 0xFFFF){
          float setpoint = get_value(regs->setpoint, id, mySerial, regs->endian);
          setpoints[id-1] = setpoint;
            if (!isnan(setpoint)) {
              Serial.print(" ---- Setpoint (Уставка): ");
              Serial.println(setpoint, 6);
            } else {
              Serial.println("Нет данных");
            }
        }
        if (regs->power != 0xFFFF){
          float power = get_value(regs->power, id , mySerial, regs->endian);
          powers[id-1] = power;
          if (!isnan(power)) {
              Serial.print(" ---- Мощность Out.1 (0-100): ");
              Serial.println(power, 6);
            } else {
              Serial.println("Нет данных");
            }
        }
        
      }
      else {
        Serial.print("Registers not found");
      }

    } else {
      Serial.println("Device not found");
    }

    delay(200);
  }

  // Формируем JSON пакет
  DynamicJsonDocument doc(1024);  // Размер под 30 устройств
  JsonArray data_array = doc.createNestedArray("data");

  for (int i = 0; i < 32; i++) {
    if (data_valid[i]) {
      JsonObject entry = data_array.createNestedObject();
      entry["slave_id"] = i + 1;
      unsigned long epochTime = timeClient.getEpochTime();
      entry["timestamp"] = getISO8601Time(epochTime);
      entry["temperature"] = temperatures[i];
      entry["setpoint"] = setpoints[i];
      entry["power"] = powers[i];
    }
  }

  String json_payload;
  serializeJson(doc, json_payload);
  Serial.println(json_payload);

  // Отправка данных
  send_data(json_payload);

  // Повтор буферизованных данных
  for (int i = 0; i < buffer_index; i++) {
    send_data(buffer[i]);
    delay(1000);  // Задержка между повторами
  }
  buffer_index = 0;  // Очистка буфера после успешных отправок

  delay(10000);  // Общее время ~5 мин
}
