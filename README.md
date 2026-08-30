<h1 align="center">STORM PS4 PKG INSTALLER</h1>

## **О проекте**
STORM PS4 PKG INSTALLER — клиентское приложение для удаленной пакетной установки игр, обновлений и DLC в формате PKG на консоли Sony PlayStation 4 по локальной сети через Remote Package Installer (RPI).

## **Происхождение и форки**
Оригинальная разработка на базе спецификаций PlayStation 4 Remote Package Installer API.

## **Технологический стек**
- **Языки программирования**: TypeScript, JavaScript (Node.js)
- **Интерфейс**: React, Tailwind CSS, Electron
- **Сетевые протоколы**: HTTP RPI Client API, mDNS / SSDP автопоиск консолей в локальной сети

## **Ключевые возможности**
- **Пакетная отправка задач на установку**: Добавление десятков PKG-файлов в очередь с автоматическим переходом к следующему.
- **Интеграция с PS4**: Поддержка всех актуальных эксплойтов и версий GoldHEN / Mira.
- **Мониторинг прогресса**: Отображение статуса загрузки и установки на консоли в реальном времени.

## **Поддерживаемые платформы и эмуляторы**
- **Операционные системы**: Windows 10, Windows 11 (x64, ARM64)
- **Целевые устройства**: Sony PlayStation 4 (Fat, Slim, Pro) с кастомной прошивкой / GoldHEN

## **Установка и запуск**
1. Скачайте инсталлятор `STORM_PS4_PKG_INSTALLER_<версия>_Setup.exe` или архив `STORM_PS4_PKG_INSTALLER_<версия>.zip` из раздела **Releases**.
2. Запустите приложение, укажите IP-адрес PlayStation 4 с запущенным Remote Package Installer.

## **Благодарности**
- **flat_z** — за разработку оригинального приложения Remote Package Installer для PS4.
- **SiSTRo и GoldHEN Team** — за разработку и поддержку окружения GoldHEN для PlayStation 4.
- **Flatz, Specter, CTurt и PS4 Scene** — за исследования архитектуры Orbis OS.
