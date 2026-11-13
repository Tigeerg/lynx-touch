#include "backend_factory.h"

#include "linuxcnc_backend.h"
#include "linuxcnc_link.h"
#include "sim_backend.h"

MachineBackend *createBackend(const QString &kind, const QString &fixturePath,
                              const QString &frozenClock, QObject *parent, QString *error)
{
    const QString name = kind.trimmed().isEmpty() ? QStringLiteral("sim") : kind.trimmed().toLower();
    if (name == QStringLiteral("linuxcnc")) {
        // The adapter is ready and tested against a fake link; the link itself needs the controller
        // (docs/LINUXCNC-BACKEND.md). No silent fallback: a panel on a machine must know.
        LinuxCncLink *link = createLinuxCncLink(error);
        if (!link)
            return nullptr;
        auto *backend = new LinuxCncBackend(link, parent);
        // The link lives exactly as long as the backend that reads it.
        link->setParent(backend);
        return backend;
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
