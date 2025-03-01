#include <WiFi.h>              // Подключение библиотеки для работы с Wi-Fi
#include <WebSocketsServer.h>  // Подключение библиотеки для работы с WebSocket
#include <NTPClient.h>         // Подключение библиотеки для работы с NTP (синхронизация времени)
#include <WiFiUdp.h>           // Подключение библиотеки для работы с UDP (используется NTPClient)
#include <SPIFFS.h>            // Подключение библиотеки для работы с файловой системой SPIFFS
#include <ESPAsyncWebServer.h> // Подключение библиотеки для асинхронного HTTP сервера
#include <EEPROM.h>            // Подключение библиотеки для работы с EEPROM (постоянная память)
#include <AutoOTA.h>           // Подключение библиотеки для автоматической прошивки с Github

// Настройки WiFi
const char *ssid = "IZU";          // SSID Wi-Fi сети
const char *password = "9uthfim8"; // Пароль Wi-Fi сети

// Пины управления
const uint8_t relayPins[] = {25, 26, 27}; // Пины для управления реле
const uint8_t modeLedPin = 2;             // Пин для индикации режима

// Структура для сохранения настроек в EEPROM
struct TimerSettings
{
  uint8_t mode;         // Режим работы (0 - ручной, 1 - автоматический)
  uint8_t reserved : 7; // Зарезервированные биты
  struct
  {
    uint16_t startTime; // Время старта (минуты с полуночи)
    uint16_t duration;  // Длительность полива (минуты)
    uint16_t interval;  // Интервал между поливами (часы)
    bool state;         // Состояние реле
  } timer[3];           // Настройки для трех таймеров
  bool relayState[3];   // Состояние реле
};

TimerSettings settings;      // Экземпляр структуры настроек
bool settingsChanged = true; // Флаг для отслеживания изменений

// Структура для отслеживания состояния полива
struct TimerState
{
  bool isWatering = false;         // Флаг, указывающий, идет ли полив
  unsigned long wateringStart = 0; // Время начала полива
} timerState[3];                   // Состояние для трех таймеров

// NTP клиент для синхронизации времени
WiFiUDP ntpUDP;                                               // UDP клиент для NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", 10800, 3600000); // NTP клиент с настройками

// WebSocket и HTTP сервер
WebSocketsServer webSocket = WebSocketsServer(81); // WebSocket сервер на порту 81
AsyncWebServer server(80);                         // HTTP сервер на порту 80

// Временные переменные
unsigned long prevUpdate = 0;      // Для отслеживания времени
unsigned long startTimes[3] = {0}; // Время старта для каждого таймера
bool relayState[3] = {false};      // Состояние реле

// Функция сохранения настроек в EEPROM
void saveSettings()
{
  if (settingsChanged)
  {
    EEPROM.put(0, settings); // Запись настроек в EEPROM
    EEPROM.commit();         // Подтверждение записи
    settingsChanged = true;  // Сброс флага изменений
    // Serial.println("Сохранение настроек в EEPROM");
    Serial.printf("Режим: %d\n", settings.mode);
    for (int i = 0; i < 3; i++)
    {
      Serial.printf("Сохранение- Таймер %d - Время старта: %d, Длительность: %d, Интервал: %d, Состояние реле: %d\n",
                    i, settings.timer[i].startTime, settings.timer[i].duration, settings.timer[i].interval, settings.relayState[i]);
    }
  }
}

// Функция загрузки настроек из EEPROM
void loadSettings()
{
  EEPROM.get(0, settings); // Чтение настроек из EEPROM
  if (settings.mode > 1)
  {                                         // Проверка на первый запуск
    memset(&settings, 0, sizeof(settings)); // Очистка настроек
    settings.mode = 0;                      // Установка режима по умолчанию
    settingsChanged = true;                 // Установка флага изменений
    saveSettings();                         // Сохранение настроек
  }
  // Serial.println("Выгрузка настроек из EEPROM");
  Serial.printf("Режим: %d\n", settings.mode);
  for (int i = 0; i < 3; i++)
  {
    Serial.printf("Выгрузка- Таймер %d - Время старта: %d, Длительность: %d, Интервал: %d, Состояние реле: %d\n",
                  i, settings.timer[i].startTime, settings.timer[i].duration, settings.timer[i].interval, settings.relayState[i]);
  }
}

// Обработчик для шаблонов HTML
String processor(const String &var)
{
  if (var == "MODE")
    return String(settings.mode); // Возврат текущего режима
  return String();
}

// Обработчик сообщений WebSocket
void handleWebSocketMessage(uint8_t num, uint8_t *data, size_t len)
{
  String msg = String((char *)data);           // Преобразование данных в строку
  int sepIndex = msg.indexOf(':');             // Поиск разделителя
  String command = msg.substring(0, sepIndex); // Извлечение команды
  String value = msg.substring(sepIndex + 1);  // Извлечение значения
  // Serial.printf("Получено сообщение от клиента %u: %s\n", num, msg.c_str());

  // Обработка команды смены режима
  if (command == "MODE")
  {
    settings.mode = value.toInt();                        // Установка режима
    digitalWrite(modeLedPin, settings.mode ? HIGH : LOW); // Переключение светодиода
    // Serial.println(settings.mode ? "Авто" : "Ручной");
    settingsChanged = true;
    // Если режим переключен на автоматический, выключаем все реле
    if (settings.mode == 1)
    {
      for (int i = 0; i < 3; i++)
      {
        settings.relayState[i] = 0;                                // Выключение реле
        digitalWrite(relayPins[i], LOW);                           // Физическое выключение реле
        webSocket.broadcastTXT("RELAY_STATE_" + String(i) + ":0"); // Отправка состояния реле
      }
    }

    saveSettings();
  }

  // Обработка настроек таймера в автоматическом режиме
  else if (command.startsWith("AUTO_"))
  {
    uint8_t timerNum = command.substring(5).toInt(); // Извлечение номера таймера
    if (timerNum >= 3)
      return;                                                                            // Проверка на переполнение
    settings.timer[timerNum].startTime = value.substring(0, value.indexOf(',')).toInt(); // Установка времени старта
    value = value.substring(value.indexOf(',') + 1);
    settings.timer[timerNum].duration = value.substring(0, value.indexOf(',')).toInt();  // Установка длительности
    settings.timer[timerNum].interval = value.substring(value.indexOf(',') + 1).toInt(); // Установка интервала
    startTimes[timerNum] = millis();                                                     // Запись времени старта
    settingsChanged = true;                                                              // Установка флага изменений
    saveSettings();                                                                      // Сохранение настроек
  }

  // Обработка ручного управления реле
  else if (command.startsWith("MANUAL_"))
  {
    uint8_t timerNum = command.substring(7).toInt(); // Извлечение номера реле
    if (timerNum >= 3)
      return;                                                                      // Проверка на переполнение
    settings.relayState[timerNum] = value.toInt();                                 // Установка состояния реле
    digitalWrite(relayPins[timerNum], settings.relayState[timerNum] ? HIGH : LOW); // Переключение реле
    settingsChanged = true;                                                        // Установка флага изменений
    saveSettings();                                                                // Сохранение настроек
    // Serial.printf("Реле %d переключено в состояние: %d\n", timerNum, settings.relayState[timerNum]);
  }

  // Обработка запроса состояния реле
  else if (command.startsWith("GET_RELAY_"))
  {
    uint8_t relayNum = command.substring(10).toInt(); // Извлечение номера реле
    if (relayNum >= 3)
      return;
    webSocket.sendTXT(num, "RELAY_STATE_" + String(relayNum) + ":" + String(settings.relayState[relayNum])); // Отправка состояния реле
    // Serial.printf("Отправлено состояние реле %d: %d\n", relayNum, settings.relayState[relayNum]);
  }

  // Обработка запроса данных таймеров
  else if (command == "GET_TIMERS")
  {
    for (int i = 0; i < 3; i++)
    {
      String response = "TIMER_DATA:" + String(i) + "," + String(settings.timer[i].startTime) + "," + String(settings.timer[i].duration) + "," + String(settings.timer[i].interval);
      webSocket.sendTXT(num, response); // Отправка данных таймера
      // Serial.printf("Отправлены данные таймера %d\n", i);
    }
  }

  // Обработка запроса текущего режима
  else if (command == "GET_MODE")
  {
    webSocket.sendTXT(num, "CURRENT_MODE:" + String(settings.mode)); // Отправка текущего режима
    Serial.println("Отправлен текущий режим: " + String(settings.mode));
  }
}

// Обработчик событий WebSocket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WS_EVT_CONNECT: // Событие подключения клиента
    Serial.printf("Клиент %u подключился\n", num);
    webSocket.sendTXT(num, "CURRENT_MODE:" + String(settings.mode)); // Отправка текущего режима
    for (int i = 0; i < 3; i++)
    {
      webSocket.sendTXT(num, "RELAY_STATE_" + String(i) + ":" + String(settings.relayState[i])); // Отправка состояния реле
      Serial.printf("Отправлено состояние реле %d: %d\n", i, settings.relayState[i]);

      // Отправка данных таймера
      String timerData = "TIMER_DATA:" + String(i) + "," + String(settings.timer[i].startTime) + "," + String(settings.timer[i].duration) + "," + String(settings.timer[i].interval);
      webSocket.sendTXT(num, timerData);
      Serial.printf("Отправлены данные таймера %d\n", i);

      // Отправка состояния таймера
      if (timerState[i].isWatering)
      {
        webSocket.sendTXT(num, "TIMER_STATE_" + String(i) + ":Включен");
      }
      else
      {
        webSocket.sendTXT(num, "TIMER_STATE_" + String(i) + ":Выключен");
      }
    }
    break;

  case WS_EVT_DISCONNECT: // Событие отключения клиента
    Serial.printf("Клиент %u отключился\n", num);
    break;

  case WStype_TEXT:                               // Событие получения текстового сообщения
    handleWebSocketMessage(num, payload, length); // Обработка сообщения
    break;

  case WStype_PONG: // Событие получения PONG
    Serial.printf("Получен PONG от клиента %u\n", num);
    break;

  case WS_EVT_ERROR: // Событие ошибки
    Serial.printf("Ошибка WebSocket от клиента %u\n", num);
    break;

  case WS_EVT_PING: // Событие получения PING
    Serial.printf("Получен PING от клиента %u\n", num);
    break;

  default: // Неизвестное событие
    Serial.printf("Неизвестное событие %u от клиента %u\n", type, num);
    break;
  }
}

// Функция setup, выполняется один раз при старте
void setup()
{
  Serial.begin(115200); // Инициализация последовательного порта

  // Инициализация EEPROM с увеличенным размером
  size_t eepromSize = sizeof(TimerSettings) + 10; // Увеличиваем размер EEPROM на 10 байт
  if (!EEPROM.begin(eepromSize))
  {
    Serial.println("Ошибка инициализации EEPROM!");
    while (1)
      ; // Остановка выполнения программы
  }

  // Проверка размера структуры и размера EEPROM
  Serial.printf("Размер структуры TimerSettings: %d байт\n", sizeof(TimerSettings));
  Serial.printf("Размер EEPROM: %d байт\n", EEPROM.length());

  if (sizeof(TimerSettings) > EEPROM.length())
  {
    Serial.println("Ошибка: Размер структуры TimerSettings превышает размер EEPROM!");
    while (1)
      ; // Остановка выполнения программы
  }

  loadSettings(); // Загрузка настроек

  // Настройка пинов
  for (int i = 0; i < 3; i++)
  {
    pinMode(relayPins[i], OUTPUT);                                   // Настройка пинов реле как выходы
    digitalWrite(relayPins[i], settings.relayState[i] ? HIGH : LOW); // Установка начального состояния реле
  }
  pinMode(modeLedPin, OUTPUT);                          // Настройка пина светодиода как выход
  digitalWrite(modeLedPin, settings.mode ? HIGH : LOW); // Установка состояния светодиода

  // Подключение к WiFi
  WiFi.begin(ssid, password); // Подключение к Wi-Fi
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Подключение к WiFi...");
  }
  Serial.println("Подключено к WiFi");
  Serial.print("IP-адрес: ");
  Serial.println(WiFi.localIP()); // Вывод IP-адреса

  // Инициализация SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("Ошибка монтирования SPIFFS");
    while (1)
      ; // Остановка при ошибке
  }

  // NTP синхронизация
  timeClient.begin();                                              // Инициализация NTP клиента
  delay(3000);
  timeClient.update();                                             // Принудительная синхронизация времени
  webSocket.broadcastTXT("TIME:" + timeClient.getFormattedTime()); // Отправка времени
  Serial.println("Время при запуске: " + timeClient.getFormattedTime());

  // Восстановление состояния таймеров после перезагрузки
  for (int i = 0; i < 3; i++) {
    if (settings.relayState[i]) {
      unsigned long currentMillis = millis();
      unsigned long elapsedWatering = (currentMillis - timerState[i].wateringStart) / 1000UL;

      // Если время полива истекло
      if (elapsedWatering >= settings.timer[i].duration * 60) {
        digitalWrite(relayPins[i], LOW);                                  // Выключение реле
        timerState[i].isWatering = false;                                 // Сброс флага полива
        webSocket.broadcastTXT("TIMER_STATE_" + String(i) + ":Выключен"); // Отправка состояния таймера
        settings.relayState[i] = false;                                   // Обновление состояния реле
        settingsChanged = true;
        saveSettings(); // Сохранение настроек
      } else {
        timerState[i].isWatering = true; // Установка флага полива
        timerState[i].wateringStart = currentMillis - elapsedWatering * 1000UL; // Восстановление времени начала полива
        digitalWrite(relayPins[i], HIGH); // Включение реле
        webSocket.broadcastTXT("TIMER_STATE_" + String(i) + ":Включен"); // Отправка состояния таймера
      }
    }
  }

  // WebSocket сервер
  webSocket.begin();                 // Запуск WebSocket сервера
  webSocket.onEvent(webSocketEvent); // Установка обработчика событий
  Serial.println("WebSocket сервер запущен");

  // HTTP сервер
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              request->send(SPIFFS, "/index.html", "text/html", false, processor); // Обработка запроса главной страницы
            });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              request->send(SPIFFS, "/style.css", "text/css"); // Обработка запроса стилей
            });
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              request->send(SPIFFS, "/script.js", "text/javascript"); // Обработка запроса скрипта
            });
  server.on("/seven-segment.otf", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              request->send(SPIFFS, "/seven-segment.otf", "text/seven-segment"); // Обработка запроса шрифта
            });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              request->send(SPIFFS, "/favicon.ico", "image/ico"); // Обработка запроса иконки
            });

  server.begin(); // Запуск HTTP сервера
  Serial.println("HTTP сервер запущен");

  // Инициализация AutoOTA
  AutoOTA.begin("humaxoid", "timer_ntp", "ntp_time.bin", "9uthfim8");
  Serial.println("AutoOTA инициализирован");
}

// Функция loop, выполняется циклически
void loop()
{
  webSocket.loop(); // Обработка событий WebSocket

  // Синхронизация NTP каждый час
  static unsigned long lastNtpUpdate = 0;
  if (millis() - lastNtpUpdate >= 3600000)
  {
    timeClient.update(); // Обновление времени
    lastNtpUpdate = millis();
    Serial.println("Время синхронизировано с NTP сервером: " + timeClient.getFormattedTime());
  }

  if (millis() - prevUpdate >= 1000)
  {
    prevUpdate = millis();
    webSocket.broadcastTXT("TIME:" + timeClient.getFormattedTime()); // Отправка времени

    // Автоматический режим
    if (settings.mode)
    {
      for (int i = 0; i < 3; i++)
      {
        unsigned long currentMillis = millis();

        // Расчет текущего времени в секундах с полуночи
        int currentHours = timeClient.getHours();
        int currentMinutes = timeClient.getMinutes();
        int currentSeconds = timeClient.getSeconds();
        unsigned long currentTotalSeconds = currentHours * 3600 + currentMinutes * 60 + currentSeconds;

        // Расчет времени старта в секундах с полуночи
        unsigned long startTotalSeconds = settings.timer[i].startTime * 60;

        // Расчет времени до старта
        long timeUntilStartSeconds = startTotalSeconds - currentTotalSeconds;

        // Корректировка времени, если старт уже прошел
        if (timeUntilStartSeconds < 0)
        {
          timeUntilStartSeconds += 86400; // 24 часа в секундах
        }

        // Если таймер не активен
        if (!timerState[i].isWatering)
        {
          // Если время до старта меньше или равно 0, начинаем полив
          if (timeUntilStartSeconds <= 0)
          {
            digitalWrite(relayPins[i], HIGH);                                // Включение реле
            timerState[i].isWatering = true;                                 // Установка флага полива
            timerState[i].wateringStart = currentMillis;                     // Запись времени начала полива
            webSocket.broadcastTXT("TIMER_STATE_" + String(i) + ":Включен"); // Отправка состояния таймера
            // Serial.printf("Таймер %d включен\n", i);
            settings.relayState[i] = true; // Сохранение состояния реле
            settingsChanged = true;        // Установка флага изменений
            saveSettings();                // Сохранение настроек
          }
          else
          {
            // Отправка времени до старта
            webSocket.broadcastTXT("COUNTDOWN_" + String(i) + ":" + String(timeUntilStartSeconds));
          }
        }
        else
        {
          unsigned long elapsedWatering = (currentMillis - timerState[i].wateringStart) / 1000UL;

          // Если время полива истекло
          if (elapsedWatering >= settings.timer[i].duration * 60)
          {
            digitalWrite(relayPins[i], LOW);                                  // Выключение реле
            timerState[i].isWatering = false;                                 // Сброс флага полива
            webSocket.broadcastTXT("TIMER_STATE_" + String(i) + ":Выключен"); // Отправка состояния таймера
            // Serial.printf("Таймер %d выключен\n", i);

            // Установка времени следующего старта
            settings.timer[i].startTime = (currentHours * 60 + currentMinutes + settings.timer[i].interval * 60) % 1440;
            settings.relayState[i] = false; // Обновление состояния реле
            settingsChanged = true;
            saveSettings(); // Сохранение настроек
          }
          else
          {
            //
          }
        }
      }
    }
  }

  // Обработка AutoOTA
  AutoOTA.handle();
}