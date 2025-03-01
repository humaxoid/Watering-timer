// Подключение к WebSocket серверу
const ws = new WebSocket('ws://' + location.hostname + ':81/');

// Элементы интерфейса
const modeSwitch = document.getElementById('mode');          // Переключатель режима
const modeText = document.getElementById('modeText');        // Текст режима
const currentTime = document.getElementById('currentTime');  // Блок текущего времени

// Инициализация состояния кнопок
const relayButtons = [
  document.getElementById('relay0'),
  document.getElementById('relay1'),
  document.getElementById('relay2')
];

// Проверка подключения WebSocket
ws.onopen = function () {
  console.log('WebSocket подключен');
}

ws.onerror = function (error) {
  console.error('Ошибка WebSocket:', error);
}

// Обработчик сообщений от WebSocket
ws.onmessage = function (event) {
  try {
    const [command, ...timeParts] = event.data.split(':');  // Разделение команды и значения
    const value = timeParts.join(':');      // Объединение оставшихся частей

    switch (command) {
      case 'CURRENT_MODE':                  // Обработка текущего режима - "Ручной / Авто"
        updateModeUI(value == '1');         // Обновление интерфейса
        if (modeSwitch) {
          modeSwitch.checked = value == '1';  // Установка состояния переключателя
        }
        break;

      case 'TIMER_DATA':                    // Обработка данных таймера
        const [timerNumber, startMins, duration, interval] = value.split(',');    // Разделение данных
        updateTimerFields(parseInt(timerNumber), startMins, duration, interval);  // Обновление полей таймера
        break;

      case 'TIME':                         // Обработка текущего времени
        if (currentTime) {
          currentTime.textContent = value;   // Обновление времени на странице
        }
        break;

      case 'COUNTDOWN_0':                  // Обработка обратного отсчета для таймера 0
        updateCountdown(0, value);
        break;

      case 'COUNTDOWN_1':                 // Обработка обратного отсчета для таймера 1
        updateCountdown(1, value);
        break;

      case 'COUNTDOWN_2':                 // Обработка обратного отсчета для таймера 2
        updateCountdown(2, value);
        break;

      // Обработка состояний таймеров (Включен/Выключен)
      case 'TIMER_STATE_0':
      case 'TIMER_STATE_1':
      case 'TIMER_STATE_2': {
        const timerNum = command.split('_')[2];      // Извлечение номера таймера
        const countdownElement = document.getElementById(`countdown${timerNum}`);
        if (countdownElement && value === "Включен") {
          countdownElement.textContent = "Включен";  // Обновление текста
          countdownElement.style.color = "#de1616";  // Установка цвета
        }
        break;
      }

      // Обработка состояний реле
      case 'RELAY_STATE_0':
      case 'RELAY_STATE_1':
      case 'RELAY_STATE_2': {
        const timerNum = parseInt(command.split('_')[2]);  // Извлечение номера реле
        const state = parseInt(value);                     // Извлечение состояния
        updateRelayButton(timerNum, state);                // Обновление кнопки реле
        break;
      }

      default:  // Неизвестная команда
        console.log('Неизвестная команда:', command);
    }
  } catch (error) {
    console.error('Ошибка обработки сообщения:', error);
  }
  console.log("Received message:", event.data);  // Логирование всех входящих сообщений
}

// Обновление состояния кнопки реле
function updateRelayButton(timerNum, state) {
  const btn = relayButtons[timerNum];        // Получение кнопки
  if (btn) {                                 // Проверка, что кнопка существует
    btn.textContent = state ? ' ON' : 'OFF';  // Установка текста
    btn.style.backgroundColor = state ? '#0eb514' : '#de1616';  // Установка цвета
    btn.style.fontSize = '1.5em';             // Увеличение размера шрифта в полтора раза
  }
}

// Обновление обратного отсчета
function updateCountdown(timer, seconds) {
  const h = Math.floor(seconds / 3600);         // Часы
  const m = Math.floor((seconds % 3600) / 60);  // Минуты
  const s = seconds % 60;                       // Секунды
  const countdownElement = document.getElementById(`countdown${timer}`);
  if (countdownElement) {
    countdownElement.textContent = `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;  // Форматирование времени
    countdownElement.style.color = "#2f2";  // Установка цвета
  }
}

// Применение настроек автоматического режима
function applyAuto(timer) {
  const start = document.getElementById(`start${timer}`).value;        // Получение времени старта
  const duration = document.getElementById(`duration${timer}`).value;  // Получение длительности
  const interval = document.getElementById(`interval${timer}`).value;  // Получение интервала

  // Проверка заполнения полей
  if (!start || !duration || !interval) {
    alert('Необходимо заполните все поля!');
    return;
  }

  // Преобразование времени в минуты с полуночи
  const [hours, minutes] = start.split(':');
  const startMins = parseInt(hours) * 60 + parseInt(minutes);

  // Отправка настроек на сервер
  ws.send(`AUTO_${timer}:${startMins},${duration},${interval}`);
}

// Переключение кнопок в ручном режиме
function toggleRelay(timer) {
  const btn = relayButtons[timer];                  // Получение кнопки
  const state = btn.textContent === 'OFF' ? 1 : 0;  // Определение нового состояния

  // Обновление текста и цвета кнопки
  btn.textContent = state ? ' ON' : 'OFF';
  btn.style.backgroundColor = state ? '#0eb514' : '#de1616';
  btn.style.fontSize = '1.5em';                     // Увеличение размера шрифта в полтора раза

  // Отправка состояния на сервер
  ws.send('MANUAL_' + timer + ':' + state);
}

// Обновление интерфейса в зависимости от режима
function updateModeUI(isAutoMode) {
  if (modeText) {
    modeText.textContent = isAutoMode ? ' Авто' : ' Ручной';  // Обновление текста режима
  }

  // Переключение видимости блоков
  document.querySelectorAll('.auto-mode').forEach(el =>
    el.style.display = isAutoMode ? 'block' : 'none');
  document.querySelectorAll('.manual-mode').forEach(el =>
    el.style.display = isAutoMode ? 'none' : 'block');
    
  // Если режим переключен на автоматический, выключаем все реле
  if (isAutoMode) {
    for (let i = 0; i < 3; i++) {
      const btn = relayButtons[i];
      if (btn && btn.textContent === 'ON') {
        btn.textContent = 'OFF';
        btn.style.backgroundColor = '#de1616';
        ws.send('MANUAL_' + i + ':0');  // Отправка команды на выключение реле
      }
    }
  }
}

// Обновление полей таймера
function updateTimerFields(timerNumber, startMins, duration, interval) {
  const hours = Math.floor(startMins / 60).toString().padStart(2, '0');  // Преобразование минут в часы
  const minutes = (startMins % 60).toString().padStart(2, '0');          // Преобразование минут в минуты
  const startElement = document.getElementById(`start${timerNumber}`);
  const durationElement = document.getElementById(`duration${timerNumber}`);
  const intervalElement = document.getElementById(`interval${timerNumber}`);
  if (startElement) startElement.value = `${hours}:${minutes}`;  // Установка времени старта
  if (durationElement) durationElement.value = duration;    // Установка длительности
  if (intervalElement) intervalElement.value = interval;    // Установка интервала
}

// Обработчик изменения режима
if (modeSwitch) {
  modeSwitch.addEventListener('change', () => {
    const mode = modeSwitch.checked ? 1 : 0;  // Получение режима

    // Обновление текста режима
    if (modeText) {
      modeText.textContent = mode ? ' Авто' : ' Ручной';
    }

    // Переключение видимости блоков
    document.querySelectorAll('.auto-mode').forEach(el =>
      el.style.display = mode ? 'block' : 'none');
    document.querySelectorAll('.manual-mode').forEach(el =>
      el.style.display = mode ? 'none' : 'block');

    // Отправка режима на сервер
    ws.send('MODE:' + mode);
    console.log(`Отправлен режим: ${mode}`);
    
    // Обновление интерфейса
    updateModeUI(mode);
  });
}

// При загрузке страницы запрашиваем состояния всех реле
window.onload = () => {
  ws.send('GET_MODE');          // Запрос текущего режима "Ручной/Авто"
  ws.send('GET_TIMERS');        // Запрос настроек таймеров
  for (let i = 0; i < 3; i++) {
    ws.send('GET_RELAY_' + i);  // Запрос состояния реле
  }
};