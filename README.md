<div align="center">



# STORM PS4 PKG INSTALLER

<div align="center">

**Нативное приложение для PlayStation 4, предназначенное для фоновой установки PKG-файлов с использованием системного загрузчика (BGFT) и современного интерфейса ImGui.**

[![C++](https://img.shields.io/badge/C++-17-blue.svg)](https://isocpp.org/)
[![ImGui](https://img.shields.io/badge/ImGui-1.89-green.svg)](https://github.com/ocornut/imgui)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

</div>

## 🇷🇺 Особенности программы (Russian)

1. **🚀 Нативный код:** Приложение полностью написано на C++ для максимальной производительности на консоли.
2. **💻 Интерфейс ImGui:** Современный, быстрый и отзывчивый графический интерфейс в темных тонах.
3. **📥 Системный загрузчик (BGFT):** Использует нативный сервис PS4 для стабильного скачивания файлов в фоне.
4. **🔗 Расширенный API (12813):** Собственный порт для детального контроля установки и получения расширенных статусов (включая многопоточную обработку запросов).
5. **🔄 Совместимость с RPI:** Полная поддержка стандартных запросов Remote Package Installer (порт 12800).
6. **⏯️ Управление очередью:** Возможность паузы, возобновления, полной отмены и удаления задач из списка.
7. **📊 Детальный мониторинг:** Отображение реальной скорости, оставшегося времени (ETA) и прогресса загрузки.
8. **🛠️ Умная обработка ошибок:** Автоматическое исправление проблем с регистрацией контента (ошибка 0x80990004).
9. **📦 Поддержка всего контента:** Корректная установка Игр, Патчей, DLC и Тем любого размера (добавлена поддержка списков до 256 задач).
10. **🖼️ Визуализация:** Отображение оригинальных иконок приложений (.icon0) прямо в списке задач.
11. **🛡️ Валидация заголовков:** Проверка корректности ContentID перед началом скачивания для предотвращения сбоев.
12. **👻 Фоновая работа:** Загрузки продолжаются корректно даже при сворачивании приложения в меню PS4.
13. **📡 Статус сервера:** Индикация доступности сетевых портов и IP-адреса консоли на главном экране.
14. **📉 Оптимизация памяти:** Эффективное управление ресурсами и отсутствие конфликтов при одновременной установке нескольких файлов.

<img width="1914" height="1050" alt="screenshot" src="https://github.com/user-attachments/assets/placeholder-screenshot" />

---

## 🇺🇸 Program Features (English)

1. **🚀 Native C++:** Written entirely in C++ for maximum performance on the console hardware.
2. **💻 ImGui Interface:** Modern, fast, and responsive graphical user interface with a dark theme.
3. **📥 System Downloader (BGFT):** Uses the native PS4 service for the most stable background file delivery.
4. **🔗 Enhanced API (12813):** Custom port for granular installation control and extended status reporting (now with concurrent request handling).
5. **🔄 RPI Compatibility:** Full support for standard Remote Package Installer requests (port 12800).
6. **⏯️ Queue Management:** Capability to pause, resume, cancel, and delete tasks directly from the list.
7. **📊 Detailed Monitoring:** Displays real-time speed, estimated time of arrival (ETA), and download progress.
8. **🛠️ Smart Error Handling:** Automatically resolves content registration issues (fixes error 0x80990004).
9. **📦 All Content Support:** Correctly installs Games, Patches, DLCs, and Themes of any file size (supports up to 256 tasks).
10. **🖼️ Visualization:** Displays original application icons (.icon0) directly in the task list.
11. **🛡️ Header Validation:** Checks ContentID integrity before starting the download to prevent failures.
12. **👻 Background Operation:** Downloads continue seamlessly even when the app is minimized to the Dashboard.
13. **📡 Server Status:** Indicates network port availability and console IP address on the main screen.
14. **📉 Memory Optimization:** Efficient resource management and race-condition protection for simultaneous transfers.
