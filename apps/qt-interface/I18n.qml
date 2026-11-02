import QtQuick

// One message catalogue for the interface. The language comes from [DISPLAY] LANGUAGE; every
// binding that calls t() re-evaluates when `language` changes, so the switch is live.
QtObject {
    id: tr
    property string language: "ru"

    readonly property var ru: ({
        "tab.work": "Работа",
        "tab.offset": "Смещения",
        "tab.tool": "Инструмент",
        "tab.alarm": "Сообщения",
        "tab.service": "Сервис",
        "cycle.start": "ПУСК",
        "cycle.hold": "СТОП",
        "cycle.reset": "СБРОС",
        "cycle.estop": "АВАРИЯ",
        "wp.auto": "АВТО",
        "wp.test": "ПРОГОН",
        "wp.setup": "НАЛАДКА",
        "svc.param": "ПАРАМЕТР",
        "svc.key": "КЛЮЧ INI",
        "svc.value": "ЗНАЧЕНИЕ",
        "svc.default": "ПО УМОЛЧ.",
        "svc.save": "Сохранить",
        "svc.back": "Назад",
        "svc.lock": "Разрешить запись",
        "header.program": "ПРОГРАММА",
        "header.ready": "ГОТОВ",
        "header.alarms": "активных сообщений",
        "deck.functions": "ФУНКЦИИ",
        "deck.jog": "ПЕРЕМЕЩЕНИЯ",
        "deck.mode": "РЕЖИМ"
    })
    readonly property var en: ({
        "tab.work": "Work",
        "tab.offset": "Offsets",
        "tab.tool": "Tool",
        "tab.alarm": "Messages",
        "tab.service": "Service",
        "cycle.start": "START",
        "cycle.hold": "STOP",
        "cycle.reset": "RESET",
        "cycle.estop": "E-STOP",
        "wp.auto": "AUTO",
        "wp.test": "DRY RUN",
        "wp.setup": "SETUP",
        "svc.param": "PARAMETER",
        "svc.key": "INI KEY",
        "svc.value": "VALUE",
        "svc.default": "DEFAULT",
        "svc.save": "Save",
        "svc.back": "Back",
        "svc.lock": "Allow writing",
        "header.program": "PROGRAM",
        "header.ready": "READY",
        "header.alarms": "active messages",
        "deck.functions": "FUNCTIONS",
        "deck.jog": "JOG",
        "deck.mode": "MODE"
    })

    function t(key) {
        var table = language === "en" ? en : ru
        return table[key] !== undefined ? table[key] : key
    }
}
