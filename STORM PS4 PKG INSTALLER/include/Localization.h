#pragma once

#include <stdint.h>
#include <stddef.h>

// Поддерживаемые языки в соответствии со стандартами STORM SOFT
enum class AppLanguage {
    Russian = 0,    // 🇷🇺 Русский (Russian)
    English = 1,    // 🇬🇧 English
    German = 2,     // 🇩🇪 Deutsch (German)
    French = 3,     // 🇫🇷 Français (French)
    Chinese = 4,    // 🇨🇳 简体中文 (Chinese)
    Japanese = 5    // 🇯🇵 日本語 (Japanese)
};

// Идентификаторы локализованных строк
enum StringId {
    STR_APP_TITLE = 0,
    STR_ONLINE,
    STR_OFFLINE,
    STR_COMPLETED,
    STR_DOWNLOADING,
    STR_INSTALLING,
    STR_PENDING,
    STR_PREPARING,
    STR_ERROR,
    STR_COL_ID,
    STR_COL_CAT,
    STR_COL_ICON,
    STR_COL_TITLE,
    STR_COL_TITLE_ID,
    STR_COL_SIZE,
    STR_COL_STATUS,
    STR_COL_PROGRESS,
    STR_CAT_GAME,
    STR_CAT_UPDATE,
    STR_CAT_DLC,
    STR_CAT_THEME,
    STR_MENU_MANAGE,
    STR_MENU_DELETE,
    STR_MENU_BACK,
    STR_HELP_SELECT,
    STR_HELP_CLOSE,
    STR_HELP_LANG,
    STR_NO_ICON,
    STR_TASK_STOPPED,
    STR_TASK_REMOVED,
    STR_NO_BG,
    STR_APP_READY,
    STR_WAITING_PACKAGES,
    STR_LANG_NAME,
    STR_STORAGE,
    STR_FREE_OF,
    STR_ERR_NO_SPACE,
    STR_STORAGE_LOW,
    STR_CONSOLE_MODEL,
    STR_COUNT
};

// Инициализация локализации (определение языка системы при запуске)
void Localization_Init();

// Получить текущий язык
AppLanguage Localization_GetLanguage();

// Установить язык приложения
void Localization_SetLanguage(AppLanguage lang);

// Циклическое переключение языка (RU -> EN -> DE -> FR -> ZH -> JA -> RU)
AppLanguage Localization_CycleLanguage();

// Получить перевод строки по ID
const char* Loc(StringId id);

// Получить локализованное название категории контента
const char* LocCategory(const char* rawCategory);

// Получить код языка в виде строки ("RU", "EN", "DE", "FR", "ZH", "JA")
const char* Localization_GetLanguageCode();
