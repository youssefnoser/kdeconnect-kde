/**
 * SPDX-FileCopyrightText: 2026 KDE Connect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "mirrorphonedevice.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>

static const QString SERVICE = QStringLiteral("org.kde.kdeconnect");
static const QString INTERFACE = QStringLiteral("org.kde.kdeconnect.device.mirrorphone");

MirrorPhoneDevice::MirrorPhoneDevice(QObject *parent)
    : QObject(parent)
{
}

QString MirrorPhoneDevice::deviceId() const
{
    return m_deviceId;
}

void MirrorPhoneDevice::setDeviceId(const QString &deviceId)
{
    if (deviceId == m_deviceId) {
        return;
    }
    connectSignals(false);
    m_deviceId = deviceId;
    m_status.clear();
    connectSignals(true);
    Q_EMIT deviceIdChanged();
    Q_EMIT statusChanged();
    if (m_deviceId.isEmpty()) {
        return;
    }
    // The last status; refresh() for a fresh one, which runs adb
    QDBusMessage message = QDBusMessage::createMethodCall(SERVICE, path(), INTERFACE, QStringLiteral("status"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, deviceId](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        if (!reply.isError() && deviceId == m_deviceId) {
            onStatusChanged(reply.value());
        }
    });
}

QVariantMap MirrorPhoneDevice::status() const
{
    return m_status;
}

QString MirrorPhoneDevice::path() const
{
    return QStringLiteral("/modules/kdeconnect/devices/%1/mirrorphone").arg(m_deviceId);
}

void MirrorPhoneDevice::connectSignals(bool connect)
{
    if (m_deviceId.isEmpty()) {
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (connect) {
        bus.connect(SERVICE, path(), INTERFACE, QStringLiteral("statusChanged"), this, SLOT(onStatusChanged(QString)));
    } else {
        bus.disconnect(SERVICE, path(), INTERFACE, QStringLiteral("statusChanged"), this, SLOT(onStatusChanged(QString)));
    }
}

void MirrorPhoneDevice::call(const QString &method, const QVariantList &arguments)
{
    if (m_deviceId.isEmpty()) {
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(SERVICE, path(), INTERFACE, method);
    message.setArguments(arguments);
    QDBusConnection::sessionBus().asyncCall(message);
}

void MirrorPhoneDevice::onStatusChanged(const QString &status)
{
    m_status = QJsonDocument::fromJson(status.toUtf8()).object().toVariantMap();
    Q_EMIT statusChanged();
}

void MirrorPhoneDevice::refresh()
{
    call(QStringLiteral("refreshStatus"));
}

void MirrorPhoneDevice::connectAdb()
{
    call(QStringLiteral("connectAdb"));
}

void MirrorPhoneDevice::enableWirelessDebugging()
{
    call(QStringLiteral("enableWirelessDebugging"));
}

void MirrorPhoneDevice::openOnPhone(const QString &target)
{
    call(QStringLiteral("openOnPhone"), {target});
}

void MirrorPhoneDevice::startQrPairing()
{
    // The QR text arrives with the status
    call(QStringLiteral("startQrPairing"));
}

void MirrorPhoneDevice::pairWithCode(const QString &code)
{
    call(QStringLiteral("pairWithCode"), {code});
}

void MirrorPhoneDevice::cancelPairing()
{
    call(QStringLiteral("cancelPairing"));
}

void MirrorPhoneDevice::mirror()
{
    call(QStringLiteral("mirrorScreen"));
}

#include "moc_mirrorphonedevice.cpp"
