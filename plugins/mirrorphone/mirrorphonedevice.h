/**
 * SPDX-FileCopyrightText: 2026 KDE Connect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

/// The mirrorphone plugin of one device, for QML: its status and actions, over D-Bus
class MirrorPhoneDevice : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY deviceIdChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)

public:
    explicit MirrorPhoneDevice(QObject *parent = nullptr);

    QString deviceId() const;
    void setDeviceId(const QString &deviceId);
    QVariantMap status() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectAdb();
    Q_INVOKABLE void enableWirelessDebugging();
    Q_INVOKABLE void openOnPhone(const QString &target);
    Q_INVOKABLE void mirror();

Q_SIGNALS:
    void deviceIdChanged();
    void statusChanged();

private Q_SLOTS:
    void onStatusChanged(const QString &status);

private:
    QString path() const;
    void call(const QString &method, const QVariantList &arguments = {});
    void connectSignals(bool connect);

    QString m_deviceId;
    QVariantMap m_status;
};
