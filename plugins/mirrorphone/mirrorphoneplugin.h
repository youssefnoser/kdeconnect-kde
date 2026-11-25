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
#define PACKET_TYPE_MIRRORPHONE_LOCK QStringLiteral("kdeconnect.mirrorphone.lock")

/**
 * Shows the phone's screen, or one app of the phone on a virtual display, in a scrcpy window.
 *
 * scrcpy needs adb. This plugin finds the phone among the adb devices, over USB or Wireless
 * debugging, by the phone's KDE Connect address. The phone reports its developer settings,
 * turns Wireless debugging on when asked, and this plugin pairs with it by QR or pairing code.
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
    Q_SCRIPTABLE void openApp(const QString &package, const QString &label);
    /// Answered by appsReceived()
    Q_SCRIPTABLE void requestApps();

    /// JSON: tools, connection, busy, message, phone and pairing; see statusJson()
    Q_SCRIPTABLE QString status() const;
    /// Asks the phone for its settings and looks for it among the adb devices, without changing anything
    Q_SCRIPTABLE void refreshStatus();
    Q_SCRIPTABLE void connectAdb();
    Q_SCRIPTABLE void enableWirelessDebugging();
    /// "developer" or "about": shows a notification on the phone that opens these settings
    Q_SCRIPTABLE void openOnPhone(const QString &target);
    /// The text for the QR code, which the phone scans in Wireless debugging, comes with the status
    Q_SCRIPTABLE void startQrPairing();
    /// With "Pair device with pairing code" open on the phone
    Q_SCRIPTABLE void pairWithCode(const QString &code);
    Q_SCRIPTABLE void cancelPairing();

Q_SIGNALS:
    /// A JSON array of {"name", "package"} sorted by name, or an error
    Q_SCRIPTABLE void appsReceived(const QString &apps, const QString &error);
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
    void openApp(const QString &package, const QString &label, Origin origin);

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

    // Pairing for Wireless debugging
    void pollPairing();
    void runPair(const QString &address, const QString &secret);
    void setPairing(const QString &state, const QString &message);
    void clearPairingSecrets();

    // scrcpy windows
    QProcess *newScrcpyProcess();
    void startWindow(const QString &action,
                     const QString &key,
                     const QString &title,
                     const QStringList &arguments,
                     const QString &serial,
                     Origin origin);
    void isLocked(const QString &serial, const std::function<void(bool locked)> &callback);
    void lockChanged(bool locked, bool screenOn);
    void showUnlockPrompt(const QString &key, const QString &serial);
    void whenAppStarted(const QString &key, const std::function<void(int display)> &callback);

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

    QString m_pairingState = QStringLiteral("idle");
    QString m_pairingMessage;
    QString m_qrName;
    QString m_qrPassword;
    QString m_pairingCode;
    QElapsedTimer m_pairingTimer;
    int m_pairingGeneration = 0;

    QProcess *m_appsRequest = nullptr;
    QHash<QString, QProcess *> m_windows;
    // The virtual display each window started its app on, as scrcpy reports it, and who waits for that
    QHash<QString, int> m_windowDisplays;
    QHash<QString, std::function<void(int display)>> m_displayWaiters;
    QString m_audioWindow;
};
