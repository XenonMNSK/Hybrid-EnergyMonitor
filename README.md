# Hybrid-EnergyMonitor для Home Assistant

[![latest](https://img.shields.io/github/v/release/XenonMNSK/Energy-Monitor.svg?color=brightgreen)](https://github.com/XenonMNSK/Energy-Monitor/releases)
[![Home Assistant](https://img.shields.io/badge/HomeAssistant-latest-yellowgreen?style=plastic&logo=homeassistant)](https://github.com/home-assistant/operating-system/releases)

## Содержание.
* [**_Описание._**](#description)
* [**_Возможности._**](#capabilitys)
* [**_Настройка._**](#settings)
* [**_Баги и обратная связь._**](#bugs)
* [**_Лицензия._**](#license)

<a id="description"></a>
## Описание.
Скетч написан на Arduino IDE для электросчетчика на базе Wemos D1 Mini и модулей PZEM-004T, для осуществления подсчетов потреблённой энергии городской сети и выработанной солнечной энергии с гибридного солнечного инвертора. Данный счетчик разрабатавлся специально для интеграции в Home Assistant.

> [!NOTE]
> PZEM‑004T работает по протоколу Modbus RTU. По умолчанию все модули имеют одинаковый адрес (0x01). При подключении нескольких модулей PZEM-004T нужно заранее изменить уникальный адрес: 0x01, 0x02, 0x03 и так далее. Максимальное число устройств на одной шине Modbus — до 247, но на практике ограничиваются 10–20 из‑за падения скорости.

<a id="capabilitys"></a>
## Возможности.

В данном скетче описано подключение 3х модулей PZEM-004T. PZEM1 замеряет потребленную городскую энергию, PZEM2 замеряет общую выдачу с инвертора включая город и солнце, а PZEM3 замеряет полученную энергию от генератора (при его наличии).

![image](https://github.com/XenonMNSK/Hybrid-EnergyMonitor/blob/fe1b3b6d9b5a6cd9855f504f3fa57be87d8a8713/images/raspred-energy.png)
![image](https://github.com/XenonMNSK/Hybrid-EnergyMonitor/blob/fe1b3b6d9b5a6cd9855f504f3fa57be87d8a8713/images/energy.png)

<a id="settings"></a>
## Настройка

> [!NOTE]
> Перед загрузкой скетча на Wemos нужно изменить данные вашего Wi-Fi и сервера Home Assistant.

Отдельная настройка не требуется. После того как счетчик подключится к Wi-Fi сети, с помощью Auto Discovery Home Assistant самостоятельно найдет новое устройства в локальной сети через MQTT-брокер без ручного создания сущностей в конфигурационных файлах. Остаётся только зайти в Home Assistant Energy Dashboard и добавить в соответствующие разделы счетчики городской сети, выработки солнечного инвертора и генератора, при его наличиии.

**Добавление сенсора городской сети:**

![image](https://github.com/XenonMNSK/Hybrid-EnergyMonitor/blob/1a393391cb8337c7c0d81599c9e0e9e2c4cba5ff/images/add-grid-1.png)
![image](https://github.com/XenonMNSK/Hybrid-EnergyMonitor/blob/1a393391cb8337c7c0d81599c9e0e9e2c4cba5ff/images/add-grid-2.png)

**Добавление сенсора солнечной выработки:**

![image](https://github.com/XenonMNSK/Hybrid-EnergyMonitor/blob/1a393391cb8337c7c0d81599c9e0e9e2c4cba5ff/images/add-solar.png)

<a id="bugs"></a>
## Баги и обратная связь.

При нахождении багов создавайте [Issue](https://github.com/XenonMNSK/Energy-Monitor/issues). Библиотека открыта для доработки и ваших [Pull Request'ов](https://github.com/XenonMNSK/Energy-Monitor/pulls)!

<a id="license"></a>
## Лицензия

Эта библиотека распространяется с открытым исходным кодом по лицензии [MIT](https://github.com/XenonMNSK/Energy-Monitor/blob/main/LICENSE).

## Разработчик
**[Деревягин Вадим](https://github.com/XenonMNSK)**
