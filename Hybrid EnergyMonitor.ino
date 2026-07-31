#include <Arduino.h>          // Стандартная библиотека Arduino — базовая функциональность платформы
#include <PZEM004Tv30.h>      // Библиотека для работы с энергометром PZEM‑004T версии 3.0
#include <SoftwareSerial.h>   // Библиотека для создания программного последовательного порта
#include <PubSubClient.h>     // Библиотека MQTT‑клиента для Arduino
#include <ESP8266WiFi.h>      // Работа с Wi‑Fi на ESP8266: подключение, статус, IP и т.д.
#include <ESP8266mDNS.h>      // Поддержка mDNS: позволяет обращаться к устройству по имени (например, PZEM004MQTT.local)
#include <WiFiUdp.h>          // Поддержка UDP: нужна для некоторых сетевых функций и OTA
#include <ArduinoOTA.h>       // Поддержка OTA (Over‑The‑Air): обновление прошивки по Wi‑Fi без кабеля

// ===== КОНФИГУРАЦИЯ СИСТЕМЫ =====
#define PZEM1_RX_PIN D7           // Пин RX для приёма данных от PZEM1)
#define PZEM1_TX_PIN D3           // Пин TX для передачи данных к PZEM1)
#define PZEM2_RX_PIN D5           // Пин RX для приёма данных от PZEM2)
#define PZEM2_TX_PIN D6           // Пин TX для передачи данных к PZEM2)
#define PZEM3_RX_PIN D1           // Пин RX для приёма данных от PZEM3)
#define PZEM3_TX_PIN D2           // Пин TX для передачи данных к PZEM3)

#define WIFI_SSID "SSID"     // SSID Wi‑Fi сети для подключения
#define WIFI_PASS "PASS"     // Пароль Wi‑Fi сети

const char* ota_hostname = "PZEM004MQTT";        // Имя хоста для OTA и MQTT
const char* mqtt_host = "10.10.10.10";            // IP-адрес MQTT-сервера
const int mqtt_port = 1883;                      // Порт MQTT-сервера (стандартный)
const char* mqtt_user = "HAUser";                 // Логин для аутентификации в MQTT
const char* mqtt_pass = "HAPassword";    // Пароль для MQTT (символ " экранирован)
const String mqtt_base_topic = "homeassistant";  // Базовый топпик для публикаций в MQTT
// =============================

// Интервал опроса в мс (должен совпадать с тем, что используется для расчёта мощности)
const unsigned long POLL_INTERVAL_MS = 2000;

// Клиент для работы с Wi‑Fi
WiFiClient espClient;
// Клиент для работы с MQTT
PubSubClient mqttClient(espClient);

// Инициализация SoftwareSerial. Создаём три независимых программных последовательных порта для связи с устройствами PZEM
SoftwareSerial pzemSerial1(PZEM1_RX_PIN, PZEM1_TX_PIN);
SoftwareSerial pzemSerial2(PZEM2_RX_PIN, PZEM2_TX_PIN);
SoftwareSerial pzemSerial3(PZEM3_RX_PIN, PZEM3_TX_PIN);

// Объекты для трёх устройств PZEM с уникальными адресами (0x01, 0x02, 0x03)
PZEM004Tv30 pzem1(pzemSerial1, 0x01);
PZEM004Tv30 pzem2(pzemSerial2, 0x02); 
PZEM004Tv30 pzem3(pzemSerial3, 0x03);

// Структура для хранения данных, получаемых от устройства PZEM
struct PZEMData {
  float voltage;    // Напряжение, В
  float current;    // Ток, А
  float power;      // Мощность, Вт
  float energy;     // Энергия, кВт·ч
  float frequency;  // Частота, Гц
  float pf;         // Коэффициент мощности (power factor, безразмерная величина)
};

// Экземпляры структуры для трёх устройств
PZEMData data1, data2, data3;

unsigned long lastMsg = 0;                       // Переменная для хранения времени (в миллисекундах) последней отправки сообщения MQTT
String mqttChipID;                               // Переменная для хранения Chip ID в строковом формате
const int MAX_ERROR_COUNT = 3;                   // Максимальное количество ошибок перед перезагрузкой
int errorCount = 0;                              // Счётчик ошибок

// Переменные для расчёта солнечной выработки (pzem2)
float prev_energy_grid = 0.0;      // энергия из города (pzem1)
float prev_energy_inv = 0.0;       // энергия с выхода инвертора (pzem2)
float solar_energy_total = 0.0;    // накопленная «чистая» солнечная энергия (kWh)

void setup() {
  Serial.begin(115200);  // Инициализация Serial‑порта для отладки (скорость 115200 бод)
  Serial.println();      // выводим пустую строку для разделения сообщений

  // Устанавливаем режим работы Wi‑Fi как станция (STA)
  WiFi.mode(WIFI_STA);
  // Сначала задаём hostname, чтобы MDNS и OTA совпадали
  WiFi.hostname(ota_hostname);
  // Начинаем подключение к указанной Wi‑Fi сети
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Ожидаем установления соединения с Wi‑Fi
  // Каждые 500 мс выводим точку для индикации процесса
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  
  // Устанавливаем пароль для OTA: теперь при обновлении потребуется ввести этот пароль
  ArduinoOTA.setPassword((const char*)"5986237");

  // Запускаем OTA‑сервер: теперь устройство готово принимать обновления по Wi‑Fi
  ArduinoOTA.begin(ota_hostname);

  // После успешного подключения выводим информацию о подключении 
  Serial.println();
  Serial.print("Успешное подключение к сети Wi-Fi: ");
  Serial.println(WiFi.localIP()); // Выводим локальный IP‑адрес устройства

  // Получаем уникальный идентификатор микроконтроллера (Chip ID) как 32‑битное число
  uint32_t chipId = ESP.getChipId();
  
  // Преобразуем в строку шестнадцатеричного представления (без префикса "0x")
  mqttChipID = String(chipId, HEX);

  // Настройка MQTT‑клиента: задаём сервер и порт
  mqttClient.setServer(mqtt_host, mqtt_port);
  
  // Увеличиваем размер буфера MQTT‑клиента до 512 байт
  // Это позволяет обрабатывать более длинные сообщения
  mqttClient.setBufferSize(512);
  
  // Подключаемся к MQTT‑серверу
  connectToMQTT();

  // Публикуем конфигурационные сообщения (MQTT Discovery) для Home Assistant
  // Эти сообщения позволяют автоматически обнаружить устройства в системе
  // Публикуем для каждого из трёх устройств PZEM с уникальные именами на основе Chip ID
  publishDiscovery(mqttChipID + "_pzem1", 0x01, false); // город
  publishDiscovery(mqttChipID + "_pzem2", 0x02, true);  // солнце (с доп. сенсорами)
  publishDiscovery(mqttChipID + "_pzem3", 0x03, false);  // генератор

  // Задержка 5 секунд после старта и подключений, перед первым чтением данных
  Serial.println("Ожидание 5 секунд перед первым чтением данных с PZEM...");
  delay(5000);
}

void loop() {
  // Обязательно вызываем в loop(): обрабатывает входящие OTA‑запросы
  ArduinoOTA.handle();

  // Проверяем, подключён ли MQTT‑клиент
  if (!mqttClient.connected()) {
    // Если подключение потеряно — выводим сообщение и пытаемся переподключиться
    Serial.println("MQTT отключен, повторное подключение...");
    connectToMQTT();
  }

  // Обрабатываем MQTT‑события (подписка, публикации и т. д.)
  mqttClient.loop();

  // Получаем текущее время в миллисекундах с момента запуска программы
  unsigned long now = millis();
  
  // Проверяем, прошло ли более 2000 мс с момента последней публикации данных
  if (now - lastMsg > POLL_INTERVAL_MS) {
    // Обновляем метку времени последней публикации
    lastMsg = now;

    // Считываем данные с первого устройства PZEM
    data1 = readPZEM(pzem1, "PZEM1");
    // Публикуем полученные данные в MQTT c уникальным именем "mqttChipID_pzem1"
    publishData(mqttChipID + "_pzem1", data1, false);

    // Считываем данные со второго устройства PZEM
    data2 = readPZEM(pzem2, "PZEM2");
    // Публикуем полученные данные в MQTT c уникальным именем "mqttChipID_pzem2"
    // Для pzem2 дополнительно считаем и публикуем солнечную выработку
    calculateAndPublishSolar(mqttChipID + "_pzem2", data1.energy, data2.energy, data2.power);

    // Считываем данные с третьего устройства PZEM
    data3 = readPZEM(pzem3, "PZEM3");
    // Публикуем полученные данные в MQTT c уникальным именем "mqttChipID_pzem3"
    publishData(mqttChipID + "_pzem3", data3, false);
  }
}

// Функция расчёта и публикации солнечной выработки (только для pzem2)
void calculateAndPublishSolar(const String& name, float energy_grid, float energy_inv, float power_inv) {
  // Защита от NaN и некорректных значений
  if (isnan(energy_grid) || isnan(energy_inv)) {
    return;
  }

  // Вычисляем дельты (приращения энергии за интервал)
  float delta_grid = energy_grid - prev_energy_grid;
  float delta_inv  = energy_inv - prev_energy_inv;

  // Ограничиваем отрицательные дельты до 0 (защита от «обратного» счёта)
  if (delta_grid < 0) delta_grid = 0;
  if (delta_inv < 0)  delta_inv = 0;

  float delta_solar = delta_inv - delta_grid;
  if (delta_solar < 0) delta_solar = 0; // «чистое солнце» не может быть отрицательным

  solar_energy_total += delta_solar;

  // Мощность (Вт) = энергия (кВт·ч) * 3600 / время (с)
  float solar_power_w = (delta_solar * 3600.0f) / (POLL_INTERVAL_MS / 1000.0f);
  if (solar_power_w < 0) solar_power_w = 0;

  // Публикуем «чистую» солнечную энергию (kWh) как total_increasing
  String topic_energy = mqtt_base_topic + "/" + name + "/solar_energy";
  mqttClient.publish(topic_energy.c_str(), String(solar_energy_total, 3).c_str(), true);

  // Публикуем солнечную мощность (W) как measurement
  String topic_power = mqtt_base_topic + "/" + name + "/solar_power";
  mqttClient.publish(topic_power.c_str(), String(solar_power_w, 1).c_str(), true);

  // Обновляем предыдущие значения
  prev_energy_grid = energy_grid;
  prev_energy_inv  = energy_inv;
}

// Функция чтения данных с устройства PZEM с валидацией полученных значений
PZEMData readPZEM(PZEM004Tv30 &pzem, const String &label) {
  PZEMData d;

  // Добавляем небольшую задержку перед чтением данных
  delay(10);

  // Считываем параметры с устройства PZEM
  d.voltage = pzem.voltage();
  d.current = pzem.current();
  d.power = pzem.power();
  d.energy = pzem.energy();
  d.frequency = pzem.frequency();
  d.pf = pzem.pf();

  // Флаги валидности данных
  bool isValid = true;
  String invalidReasons = "";

  // Проверка на невалидные значения (NaN)
  if (isnan(d.voltage)) { invalidReasons += "NaN voltage, "; isValid = false; }
  if (isnan(d.current)) { invalidReasons += "NaN current, "; isValid = false; }
  if (isnan(d.power)) { invalidReasons += "NaN power, "; isValid = false; }
  if (isnan(d.energy)) { invalidReasons += "NaN energy, "; isValid = false; }
  if (isnan(d.frequency)) { invalidReasons += "NaN frequency, "; isValid = false; }
  if (isnan(d.pf)) { invalidReasons += "NaN pf, "; isValid = false; }

  // Логические проверки границ значений
  if (d.voltage < 150 || d.voltage > 260) {  // Напряжение: 150–260 В
    invalidReasons += "voltage вне диапазона (" + String(d.voltage) + " V), ";
    isValid = false;
  }
  if (d.current < 0 || d.current > 100) {  // Ток: 0–100 А
    invalidReasons += "current вне диапазона (" + String(d.current) + " A), ";
    isValid = false;
  }
  if (d.power < 0 || d.power > 25000) {  // Мощность: 0–25 кВт
    invalidReasons += "power вне диапазона (" + String(d.power) + " W), ";
    isValid = false;
  }
  if (d.energy < 0 || d.energy > 100000) {  // Энергия: 0–100 000 kWh
    invalidReasons += "energy вне диапазона (" + String(d.energy) + " kWh), ";
    isValid = false;
  }
  if (d.frequency < 45 || d.frequency > 55) {  // Частота: 45–55 Гц
    invalidReasons += "frequency вне диапазона (" + String(d.frequency) + " Hz), ";
    isValid = false;
  }
  if (d.pf < 0 || d.pf > 1) {  // Коэффициент мощности: 0–1
    invalidReasons += "power factor вне диапазона (" + String(d.pf) + "), ";
    isValid = false;
  }

  if (!isValid) {
    errorCount++;
    Serial.println("Обнаружены невалидные данные от " + label + ": " + invalidReasons);
    Serial.println("Счётчик ошибок: " + String(errorCount) + "/" + String(MAX_ERROR_COUNT));

    // Сброс всех значений на 0
    d.voltage = 0;
    d.current = 0;
    d.power = 0;
    d.energy = 0;
    d.frequency = 0;
    d.pf = 0;

    // Переинициализация PZEM
    pzem.resetEnergy();
    delay(1000);

    // Если превышено максимальное количество ошибок — перезагрузка
    if (errorCount >= MAX_ERROR_COUNT) {
      Serial.println("Критическое количество ошибок. Перезагрузка ESP...");
      delay(2000);
      ESP.restart();
    }
  } else {
    errorCount = 0;  // Сброс счётчика при успешных данных
  }

  return d;
}

// Функция подключения к MQTT-серверу с повторными попытками
void connectToMQTT() {
  int attempts = 0; // Счётчик попыток подключения

  // Пытаемся подключиться, пока не удастся или не исчерпаем попытки (максимум 5)
  while (!mqttClient.connected() && attempts < 5) {
    // Пытаемся подключиться с указанием имени хоста, логина и пароля
    if (mqttClient.connect(ota_hostname, mqtt_user, mqtt_pass)) {
      Serial.println("Успешное подключение к MQTT серверу!");
      return; // Успешное подключение — выходим из функции
    } else {
      // Вывод кода ошибки подключения
      Serial.print("Неудачное подключение к MQTT, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" — повтор попытки через 2 секунды");
      delay(2000); // Ждём 2 секунды перед следующей попыткой
      attempts++;   // Увеличиваем счётчик попыток
    }
  }

  // Если после 5 попыток подключение не удалось — перезапускаем устройство
  if (!mqttClient.connected()) {
    Serial.print("Перезагрузка ESP...");
    delay(1000);
    ESP.restart();
  }
}

// Функция публикации конфигурационных сообщений для Home Assistant (MQTT Discovery)
// Позволяет автоматически обнаруживать устройства и датчики в системе Home Assistant
void publishDiscovery(const String& name, uint8_t addr, bool isSolar) {
  // Массивы с параметрами для каждого типа данных, получаемых от PZEM
  const String keys[] = {"voltage", "current", "power", "energy", "frequency", "pf"};      // Названия параметров
  const String units[] = {"V", "A", "W", "kWh", "Hz", ""};                         // Единицы измерения
  const String device_class[] = {"voltage", "current", "power", "energy", "frequency", "power_factor"}; // Классы устройств (для правильной интерпретации в HA)
  // Русскоязычные понятные названия для friendly_name (без технических префиксов)
  const String friendlyNames[] = {
    "Напряжение",           // voltage
    "Ток",                  // current
    "Мощность",             // power
    "Энергопотребление",    // energy (лучше чем "Энергия" для счётчика)
    "Частота",              // frequency
    "Коэффициент мощности"  // pf
  };
  // Массив иконок для каждого параметра (MDI-иконки)
  const String icons[] = {
    "mdi:flash",            // voltage — молния (напряжение)
    "mdi:current-ac",       // current — переменный ток
    "mdi:lightning-bolt",   // power — разряд молнии (мощность)
    "mdi:counter",          // energy — счётчик (энергопотребление)
    "mdi:wave",             // frequency — волна (частота)
    "mdi:function"          // pf — математическая функция (коэф. мощности)
  };

  // Проходим по всем 6 типам параметров (напряжение, ток, мощность и т. д.)
  for (int i = 0; i < 6; i++) {
    String key = keys[i];  // Получаем название параметра (например, "voltage")

    // Формируем MQTT-топпик для конфигурационного сообщения
    // Пример: homeassistant/sensor/pzem1_voltage/config
    String topic = mqtt_base_topic + "/sensor/" + name + "_" + key + "/config";

    // Начинаем формировать JSON-payload для MQTT Discovery
    String payload = "{";

    // Добавляем поле "name" — читаемое имя датчика в Home Assistant
    //payload += "\"name\": \"" + name + "_" + key + "\",";
    //payload += "\"name\": \"EnergyCounter\",";
    payload += "\"name\": \"" + mqttChipID + "_pzem" + String(addr) + "_" + friendlyNames[i] + "\",";

    // Добавляем поле "state_topic" — топпик, откуда HA будет читать текущие значения
    // Пример: homeassistant/pzem1/voltage
    payload += "\"state_topic\": \"" + mqtt_base_topic + "/" + name + "/" + key + "\",";

    // Добавляем поле "unit_of_measurement" — единица измерения (В, А, Вт и т. д.)
    payload += "\"unit_of_measurement\": \"" + units[i] + "\",";

    // Читаемое имя на русском
    payload += "\"friendly_name\": \"" + friendlyNames[i] + "_" + String(addr) + "\",";

    // Добавляем поле "device_class" — класс устройства для правильной иконки и интерпретации в HA
    payload += "\"device_class\": \"" + device_class[i] + "\",";

    // Добавляем иконку
    payload += "\"icon\": \"" + icons[i] + "\"";

    // Для параметра "energy" (энергия) добавляем специальное поле "state_class"
    // "total_increasing" означает, что значение только растёт (счётчик кВт·ч)
    if (key.equals("energy")) {
      payload += ",\"state_class\": \"total_increasing\"";
    }

    // Для параметра "power" (мощность) добавляем специальное поле "state_class"
    // "measurement" означает, сенсор измеряет текущее значение, а не предсказанное или агрегированное,
    // и необходим для сбора долгосрочной статистики
    if (key.equals("power")) {
      payload += ",\"state_class\": \"measurement\"";
    }

    // Добавляем поле "unique_id" — уникальный идентификатор датчика
    payload += ",\"unique_id\": \"" + name + "_" + key + "\"";

    // Добавляем объект "device" — информация об устройстве в целом
    // Используется для группировки датчиков в один девайс в Home Assistant
    //payload += ",\"device\": {\"identifiers\": [\"" + name + "\"], \"name\": \"Energy Monitor\",\"model\": \"PZEM004T v3\",\"manufacturer\": \"Xenon\" }";
    payload += ",\"device\": {";
    payload += "\"identifiers\": [\"" + name + "\"],";
    payload += "\"name\": \"Energy Monitor\",";
    payload += "\"model\": \"PZEM004T v3\",";
    payload += "\"manufacturer\": \"Xenon\" }";

    // Завершаем JSON-объект
    payload += "}";

    // Публикуем конфигурационное сообщение в MQTT
    // Параметры:
    // - topic.c_str() — топпик (конвертируем String в C-строку)
    // - payload.c_str() — тело сообщения (JSON)
    // - true — флаг retained: сообщение сохраняется на сервере и доставляется новым подписчикам
    mqttClient.publish(topic.c_str(), payload.c_str(), true);
  }

  // --- СПЕЦИАЛЬНЫЕ СЕНСОРЫ ДЛЯ СОЛНЕЧНОЙ ВЕТКИ (только если isSolar == true) ---
  if (isSolar) {
    // 1. Сенсор чистой солнечной энергии (solar_energy)
    String topic_se = mqtt_base_topic + "/sensor/" + name + "_solar_energy/config";
    String payload_se = "{";
    payload_se += "\"name\": \"" + mqttChipID + "_pzem" + String(addr) + "_Solar Energy\",";
    payload_se += "\"state_topic\": \"" + mqtt_base_topic + "/" + name + "/solar_energy\",";
    payload_se += "\"unit_of_measurement\": \"kWh\",";
    payload_se += "\"friendly_name\": \"Солнечная энергия_" + String(addr) + "\",";
    payload_se += "\"device_class\": \"energy\",";
    payload_se += "\"icon\": \"mdi:solar-panel\",";
    // Важно: total_increasing — счётчик только растёт, подходит для Energy Dashboard
    payload_se += "\"state_class\": \"total_increasing\",";
    payload_se += "\"unique_id\": \"" + name + "_solar_energy\",";
    // Привязываем к тому же устройству (device), чтобы в HA это был один девайс
    payload_se += "\"device\": {\"identifiers\": [\"" + name + "\"], \"name\": \"Energy Monitor\", \"model\": \"PZEM004T v3\", \"manufacturer\": \"Xenon\"}";
    payload_se += "}";
    mqttClient.publish(topic_se.c_str(), payload_se.c_str(), true);

    // 2. Сенсор текущей мощности солнечной выработки (solar_power)
    String topic_sp = mqtt_base_topic + "/sensor/" + name + "_solar_power/config";
    String payload_sp = "{";
    payload_sp += "\"name\": \"" + mqttChipID + "_pzem" + String(addr) + "_Solar Power\",";
    payload_sp += "\"state_topic\": \"" + mqtt_base_topic + "/" + name + "/solar_power\",";
    payload_sp += "\"unit_of_measurement\": \"W\",";
    payload_sp += "\"friendly_name\": \"Солнечная мощность_" + String(addr) + "\",";
    payload_sp += "\"device_class\": \"power\",";
    payload_sp += "\"icon\": \"mdi:solar-power\",";
    // measurement — текущее измерение мощности (Вт), нужно для графиков и статистики
    payload_sp += "\"state_class\": \"measurement\",";
    payload_sp += "\"unique_id\": \"" + name + "_solar_power\",";
    payload_sp += "\"device\": {\"identifiers\": [\"" + name + "\"], \"name\": \"Energy Monitor\", \"model\": \"PZEM004T v3\", \"manufacturer\": \"Xenon\"}";
    payload_sp += "}";
    mqttClient.publish(topic_sp.c_str(), payload_sp.c_str(), true);
  }
}

void publishData(const String& name, const PZEMData& d, bool isSolar) {
  // Публикуем текущие значения всех параметров для указанного устройства (name)
  String baseTopic = mqtt_base_topic + "/" + name;
  // Каждое значение публикуется в отдельный топпик с флагом retained (true)
  mqttClient.publish((baseTopic + "/voltage").c_str(), String(d.voltage, 2).c_str(), true);      // Напряжение (V)
  mqttClient.publish((baseTopic + "/current").c_str(), String(d.current, 2).c_str(), true);      // Ток (A)
  mqttClient.publish((baseTopic + "/power").c_str(), String(d.power, 1).c_str(), true);          // Мощность (W)
  mqttClient.publish((baseTopic + "/energy").c_str(), String(d.energy, 3).c_str(), true);        // Энергия (kWh)
  mqttClient.publish((baseTopic + "/frequency").c_str(), String(d.frequency, 1).c_str(), true);  // Частота (Hz)
  mqttClient.publish((baseTopic + "/pf").c_str(), String(d.pf, 2).c_str(), true);                // Коэффициент мощности (power factor)

  if (isSolar) {
    // Публикация solar_energy и solar_power выполняется в calculateAndPublishSolar
  }
}