> 💡 **Релиз 1.60** — *Сетевой менеджер и установщик PKG-пакетов для PlayStation 4 по локальной сети.*

---

### 🚀 Ключевые изменения и улучшения
- 🌟 **[Мониторинг хранилища]**: Интеграция дискового анализатора раздела `/user` через `statvfs`/`statfs` с графическим 3D-индикатором памяти и предупреждением о нехватке места.
- 🌟 **[Pre-flight Space Check]**: Предварительная проверка доступного дискового пространства перед постановкой в очередь BGFT для предотвращения системных ошибок `0x80990004`.
- 🌟 **[Аппаратный детект PS4 Pro]**: Автоматическое определение ревизии консоли (PS4 Pro / Neo vs Fat/Slim) через `sceKernelIsNeoMode()` и определение версии ПО через `sceKernelGetSystemSwVersion`.
- 🌟 **[Тройной сетевой порт]**: Одновременная работа портов `12813` (STORM REST API), `12800` (RPI Flat_z) и `12801` (PackageFlow / playstation_installer).
- 🌟 **[Интерфейс]**: Сбалансированная группировка шапки 115px и блока статистики над таблицей без взаимных наложений элементов.
- 🌟 **[Локализация]**: 100% поддержка 6 языков (RU, EN, DE, FR, ZH, JA) с автоопределением языка системы Orbis OS.

<details>
<summary><b>📋 Полный список изменений (нажмите, чтобы развернуть)</b></summary>

- 🔹 **[Системный модуль]**: Создан высокопроизводительный модуль `SystemInfo` для сбора аппаратных и дисковых метрик консоли.
- 🔹 **[Сетевой демон]**: Добавлены эндпоинты `/ping`, `/system/info`, `/storage`, `/install/capabilities`, `/install/pair`, `/install/jobs` для совместимости с ПК-клиентами.
- 🔹 **[Защита от сбоев]**: Блокировка переполнения диска и корректная обработка ошибок до передачи задач демону `libSceBgft`.
- 🔹 **[Кроссплатформенная сборка]**: Адаптирован `Makefile` для бесшовной сборки как под Windows, так и под Linux / GitHub Actions CI.

</details>

<details>
<summary><b>🌐 English Changelog (click to expand)</b></summary>

- 🔸 **Storage Monitor**: Real-time `/user` partition space tracking with gauge bar and low-space warning.
- 🔸 **Pre-flight Space Check**: Prevents BGFT error `0x80990004` by validating free space before queueing downloads.
- 🔸 **PS4 Pro Hardware Detection**: Native detection of Neo mode and firmware version.
- 🔸 **Triple Port Server**: Simultaneous listening on ports 12813 (STORM API), 12800 (RPI), and 12801 (PackageFlow).
- 🔸 **Grouped UI**: Clean layout with separate system info, network status, storage gauge, and queue metrics.
- 🔸 **100% Localization**: Full support for 6 languages with system language auto-detection.

</details>

<details>
<summary><b>📦 Файлы и вложения к релизу (нажмите, чтобы развернуть)</b></summary>

- 📁 **Прикреплённые файлы**: Исполняемые файлы, инсталляторы и архивы доступны в секции **Assets** ниже.
- 🛡️ **Контроль целостности**: Все бинарные файлы собраны из официального исходного кода и проверены перед публикацией.
- 💻 **Установка**: Скачайте соответствующий архив/инсталлятор из списка Assets и следуйте стандартным инструкциям.

</details>
