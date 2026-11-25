/**
 * SPDX-FileCopyrightText: 2026 KDE Connect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QSet>

#include <functional>

#include <core/kdeconnectplugin.h>

class QProcess;

#define PACKET_TYPE_MIRRORPHONE_REQUEST QStringLiteral("kdeconnect.mirrorphone.request")
#define PACKET_TYPE_MIRRORPHONE_RESPONSE QStringLiteral("kdeconnect.mirrorphone.response")
#define PACKET_TYPE_MIRRORPHONE_ADB QStringLiteral("kdeconnect.mirrorphone.adb")
#define PACKET_TYPE_MIRRORPHONE_ADB_STATUS QStringLiteral("kdeconnect.mirrorphone.adb.status")
#define PACKET_TYPE_MIRRORPHONE_STATUS QStringLiteral("kdeconnect.mirrorphone.status")

/**
 * Shows the phone's screen in a scrcpy window.
 *
 * scrcpy needs adb. This plugin finds the phone among the adb devices, over USB or Wireless
 * debugging, by the phone's KDE Connect address. The phone reports its developer settings and
 * turns Wireless debugging on when asked.
 */
class MirrorPhonePlugin : public KdeConnectPlugin
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kdeconnect.device.mirrorphone")

public:
    using KdeConnectPlugin::KdeConnectPlugin;
    ~MirrorPhonePlugin() override;

    void receivePacket(const NetworkPacket &np) override;
    void connected() override;
    QString dbusPath() const override;

    Q_SCRIPTABLE void mirrorScreen();

    /// JSON: tools, connection, busy, message, and phone; see statusJson()
    Q_SCRIPTABLE QString status() const;
    /// Asks the phone for its settings and looks for it among the adb devices, without changing anything
    Q_SCRIPTABLE void refreshStatus();
    Q_SCRIPTABLE void connectAdb();
    Q_SCRIPTABLE void enableWirelessDebugging();
    /// "developer" or "about": shows a notification on the phone that opens these settings
    Q_SCRIPTABLE void openOnPhone(const QString &target);

Q_SIGNALS:
    Q_SCRIPTABLE void statusChanged(const QString &status);

private:
    enum class Origin {
        Phone,
        Desktop,
    };
    using AdbCallback = std::function<void(const QString &serial, const QString &error)>;
    using ProcessCallback = std::function<void(int exitCode, const QString &output)>;
    using SerialCallback = std::function<void(const QString &serial, const QString &serviceAddress)>;

    void mirrorScreen(Origin origin);

    // adb connection
    void withAdb(const AdbCallback &callback);
    void adbStep();
    void finishAdb(const QString &serial, const QString &error);
    void findSerial(const SerialCallback &callback);
    void matchUsb(QStringList serials, const QSet<QString> &addresses, const std::function<void(const QString &serial)> &callback);
    void runAdb(const QStringList &arguments, const ProcessCallback &callback = {}, int timeoutMs = 15000);
    void runShell(const QString &serial, const QStringList &command, const ProcessCallback &callback = {});
    QSet<QString> phoneAddresses() const;
    void sendAdbRequest(const QString &action, const QString &target = QString());

    // scrcpy windows
    QProcess *newScrcpyProcess();
    void startWindow(const QString &action,
                     const QString &key,
                     const QString &title,
                     const QStringList &arguments,
                     const QString &serial,
                     Origin origin);

    void reply(const QString &action, const QString &error, Origin origin);
    void notifySetup(const QString &text);
    void setMessage(const QString &message);
    void emitStatus();
    QJsonObject statusJson() const;
    void sendStatusToPhone();
    /// "usb", "wifi", or empty when not connected
    QString transport() const;

    QString tool(const QString &name) const;

    QList<AdbCallback> m_adbWaiting;
    bool m_adbBusy = false;
    bool m_askedPhone = false;
    bool m_notifiedUsb = false;
    QSet<QString> m_triedAddresses;
    QElapsedTimer m_adbTimer;
    bool m_grantedPermission = false;
    bool m_unauthorizedUsb = false;

    // The phone asked for our status, for its settings page; it gets every change from then on
    bool m_phoneWantsStatus = false;
    QString m_serial;
    QString m_message;
    QJsonObject m_phone;

    QHash<QString, QProcess *> m_windows;
    QString m_audioWindow;
};
