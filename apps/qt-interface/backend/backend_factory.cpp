#include "backend_factory.h"

#include "sim_backend.h"

MachineBackend *createBackend(const QString &kind, const QString &fixturePath,
                              const QString &frozenClock, QObject *parent, QString *error)
{
    const QString name = kind.trimmed().isEmpty() ? QStringLiteral("sim") : kind.trimmed().toLower();
    if (name == QStringLiteral("linuxcnc")) {
        if (error)
            *error = QStringLiteral("backend linuxcnc ещё не подключён: используйте --backend=sim");
        return nullptr;
    }
    if (name != QStringLiteral("sim")) {
        if (error)
            *error = QStringLiteral("неизвестный backend \"%1\": ожидается sim или linuxcnc").arg(kind);
        return nullptr;
    }

    auto *backend = new SimBackend(parent);
    if (!backend->loadFixture(fixturePath)) {
        if (error)
            *error = backend->lastError();
        delete backend;
        return nullptr;
    }
    if (!frozenClock.isEmpty() && !backend->setFrozenClock(frozenClock)) {
        if (error)
            *error = backend->lastError();
        delete backend;
        return nullptr;
    }
    return backend;
}
