#include "../include/Localization.h"
#include "../include/Common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

static AppLanguage s_currentLanguage = AppLanguage::English;

// Таблица строк для 6 поддерживаемых языков
static const char* s_translations[6][STR_COUNT] = {
    // 0: Русский (Russian)
    {
        "STORM PS4 PKG INSTALLER v1.50",
        "В СЕТИ",
        "ОФФЛАЙН",
        "ЗАВЕРШЕНО",
        "СКАЧИВАНИЕ",
        "УСТАНОВКА",
        "В ОЧЕРЕДИ",
        "ПОДГОТОВКА",
        "ОШИБКА",
        "№",
        "ТИП",
        "ЗНАЧОК",
        "НАЗВАНИЕ",
        "TITLE ID",
        "РАЗМЕР",
        "СТАТУС",
        "ПРОГРЕСС",
        "ИГРА",
        "ПАТЧ",
        "DLC",
        "ТЕМА",
        "Управление задачей",
        "Удалить / отменить задачу",
        "Назад (Круг)",
        "Нажмите X для выбора",
        "Нажмите любую кнопку для закрытия",
        "△ Сменить язык",
        "Значок отсутствует",
        "Задача остановлена",
        "Задача удалена из списка",
        "Нет фона",
        "Система готова",
        "Ожидание пакетов...",
        "Русский"
    },
    // 1: English
    {
        "STORM PS4 PKG INSTALLER v1.50",
        "ONLINE",
        "OFFLINE",
        "COMPLETED",
        "DOWNLOADING",
        "INSTALLING",
        "PENDING",
        "PREPARING",
        "ERROR",
        "ID",
        "CAT",
        "ICON",
        "TITLE",
        "TITLE ID",
        "SIZE",
        "STATUS",
        "PROGRESS",
        "GAME",
        "UPDATE",
        "DLC",
        "THEME",
        "Manage task",
        "Delete / cancel task",
        "Back (Circle)",
        "Press X to select",
        "Press any button to close",
        "△ Change language",
        "No icon available",
        "Task stopped",
        "Task removed from list",
        "No BG",
        "System ready",
        "Waiting for packages...",
        "English"
    },
    // 2: Deutsch (German)
    {
        "STORM PS4 PKG INSTALLER v1.50",
        "ONLINE",
        "OFFLINE",
        "ABGESCHLOSSEN",
        "HERUNTERLADEN",
        "INSTALLATION",
        "IN WARTESCHLANGE",
        "VORBEREITUNG",
        "FEHLER",
        "ID",
        "KAT",
        "SYMBOL",
        "TITEL",
        "TITLE ID",
        "GRÖSSE",
        "STATUS",
        "FORTSCHRITT",
        "SPIEL",
        "UPDATE",
        "DLC",
        "DESIGN",
        "Aufgabe verwalten",
        "Aufgabe löschen / abbrechen",
        "Zurück (Kreis)",
        "X zum Auswählen drücken",
        "Beliebige Taste zum Schließen",
        "△ Sprache ändern",
        "Kein Symbol verfügbar",
        "Aufgabe angehalten",
        "Aufgabe aus Liste entfernt",
        "Kein Hintergrund",
        "System bereit",
        "Warte auf Pakete...",
        "Deutsch"
    },
    // 3: Français (French)
    {
        "STORM PS4 PKG INSTALLER v1.50",
        "EN LIGNE",
        "HORS LIGNE",
        "TERMINÉ",
        "TÉLÉCHARGEMENT",
        "INSTALLATION",
        "EN ATTENTE",
        "PRÉPARATION",
        "ERREUR",
        "ID",
        "CAT",
        "ICÔNE",
        "TITRE",
        "TITLE ID",
        "TAILLE",
        "STATUT",
        "PROGRESSION",
        "JEU",
        "MAJ",
        "DLC",
        "THÈME",
        "Gérer la tâche",
        "Supprimer / annuler la tâche",
        "Retour (Cercle)",
        "Appuyez sur X pour choisir",
        "Appuyez sur une touche pour fermer",
        "△ Changer de langue",
        "Aucune icône disponible",
        "Tâche arrêtée",
        "Tâche supprimée de la liste",
        "Pas d'arrière-plan",
        "Système prêt",
        "En attente de paquets...",
        "Français"
    },
    // 4: 简体中文 (Chinese)
    {
        "STORM PS4 PKG INSTALLER v1.50",
        "在线",
        "离线",
        "已完成",
        "下载中",
        "安装中",
        "排队中",
        "准备中",
        "错误",
        "序号",
        "类别",
        "图标",
        "标题",
        "TITLE ID",
        "大小",
        "状态",
        "进度",
        "游戏",
        "更新",
        "DLC",
        "主题",
        "管理任务",
        "删除 / 取消任务",
        "返回 (圆形键)",
        "按 X 键确认",
        "按任意键关闭",
        "△ 切换语言",
        "无可用图标",
        "任务已停止",
        "任务已从列表移除",
        "无背景",
        "系统就绪",
        "等待安装包...",
        "中文"
    },
    // 5: 日本語 (Japanese)
    {
        "STORM PS4 PKG INSTALLER v1.50",
        "オンライン",
        "オフライン",
        "完了",
        "ダウンロード中",
        "インストール中",
        "待機中",
        "準備中",
        "エラー",
        "ID",
        "種別",
        "アイコン",
        "タイトル",
        "TITLE ID",
        "サイズ",
        "状態",
        "進行度",
        "ゲーム",
        "更新",
        "DLC",
        "テーマ",
        "タスク管理",
        "タスク削除 / 中止",
        "戻る (丸ボタン)",
        "Xボタンで選択",
        "ボタンを押して閉じる",
        "△ 言語変更",
        "アイコンなし",
        "タスク停止",
        "タスクをリストから削除",
        "背景なし",
        "準備完了",
        "パッケージ待機中...",
        "日本語"
    }
};

static AppLanguage DetectOrbisSystemLanguage() {
    // Опрос Orbis OS через системную службу sceSystemService
    typedef int (*PfnSceSystemServiceParamGetInt)(int, int*);
    PfnSceSystemServiceParamGetInt pfn = (PfnSceSystemServiceParamGetInt)dlsym(RTLD_DEFAULT, "sceSystemServiceParamGetInt");
    if (!pfn) {
        pfn = (PfnSceSystemServiceParamGetInt)dlsym(RTLD_DEFAULT, "sceSystemServiceGetParamInt");
    }

    int langCode = -1;
    if (pfn) {
        int res = pfn(1, &langCode); // 1 = ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG
        Log("Localization: sceSystemServiceParamGetInt(1) returned 0x%08X, langCode = %d", res, langCode);
    } else {
        Log("Localization: sceSystemServiceParamGetInt symbol not found via dlsym");
    }

    // Сопоставление кодов языков Orbis OS:
    // 0: Japanese
    // 1: English (US), 18: English (UK)
    // 2: French, 22: French (Canada)
    // 4: German
    // 8: Russian
    // 10: Chinese (Traditional), 11: Chinese (Simplified)
    switch (langCode) {
        case 8:
            return AppLanguage::Russian;
        case 0:
            return AppLanguage::Japanese;
        case 4:
            return AppLanguage::German;
        case 2:
        case 22:
            return AppLanguage::French;
        case 10:
        case 11:
            return AppLanguage::Chinese;
        case 1:
        case 18:
        default:
            return AppLanguage::English;
    }
}

void Localization_Init() {
    // 1. Проверяем сохранённые пользовательские настройки языка
    int saved = -1;
    FILE* f = fopen("/data/sppi_lang.ini", "r");
    if (f) {
        if (fscanf(f, "%d", &saved) == 1) {
            if (saved >= 0 && saved < 6) {
                s_currentLanguage = (AppLanguage)saved;
                fclose(f);
                Log("Localization: Restored saved language: %d", saved);
                return;
            }
        }
        fclose(f);
    }

    // 2. Если сохранённого языка нет — определяем язык операционной системы PS4
    s_currentLanguage = DetectOrbisSystemLanguage();
    Log("Localization: Auto-detected system language: %d", (int)s_currentLanguage);
}

AppLanguage Localization_GetLanguage() {
    return s_currentLanguage;
}

void Localization_SetLanguage(AppLanguage lang) {
    if ((int)lang >= 0 && (int)lang < 6) {
        s_currentLanguage = lang;
        FILE* f = fopen("/data/sppi_lang.ini", "w");
        if (f) {
            fprintf(f, "%d\n", (int)lang);
            fclose(f);
            Log("Localization: Saved language preference: %d", (int)lang);
        }
    }
}

AppLanguage Localization_CycleLanguage() {
    int next = ((int)s_currentLanguage + 1) % 6;
    Localization_SetLanguage((AppLanguage)next);
    return s_currentLanguage;
}

const char* Loc(StringId id) {
    int langIdx = (int)s_currentLanguage;
    if (langIdx < 0 || langIdx >= 6) langIdx = 1; // Fallback to English
    if (id < 0 || id >= STR_COUNT) return "";
    return s_translations[langIdx][id];
}

const char* LocCategory(const char* rawCategory) {
    if (!rawCategory) return Loc(STR_CAT_GAME);
    if (strcasecmp(rawCategory, "gd") == 0) return Loc(STR_CAT_GAME);
    if (strcasecmp(rawCategory, "gp") == 0) return Loc(STR_CAT_UPDATE);
    if (strcasecmp(rawCategory, "ac") == 0) return Loc(STR_CAT_DLC);
    if (strcasecmp(rawCategory, "th") == 0 || strcasecmp(rawCategory, "theme") == 0) return Loc(STR_CAT_THEME);
    return rawCategory;
}

const char* Localization_GetLanguageCode() {
    switch (s_currentLanguage) {
        case AppLanguage::Russian:  return "RU";
        case AppLanguage::English:  return "EN";
        case AppLanguage::German:   return "DE";
        case AppLanguage::French:   return "FR";
        case AppLanguage::Chinese:  return "ZH";
        case AppLanguage::Japanese: return "JA";
        default:                    return "EN";
    }
}
