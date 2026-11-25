/**
 * SPDX-FileCopyrightText: 2026 KDE Connect contributors
 * Device matching, pairing and scrcpy handling adapted from Phone Kompanion,
 * SPDX-FileCopyrightText: 2026 Rok Krajnc <rok.krajnc@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "mirrorphoneplugin.h"

#include <KLocalizedString>
#include <KNotification>
#include <KPluginFactory>

#include <QDir>
#include <QHostAddress>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

#include <core/daemon.h>
#include <core/device.h>

#include "plugin_mirrorphone_debug.h"

K_PLUGIN_CLASS_WITH_JSON(MirrorPhonePlugin, "kdeconnect_mirrorphone.json")

static const QString ADB_TLS_SERVICE = QStringLiteral("_adb-tls-connect._tcp");
static const QString ADB_PAIRING_SERVICE = QStringLiteral("_adb-tls-pairing._tcp");
// The KDE Connect app, which turns Wireless debugging on when it has this permission
static const QString ANDROID_PACKAGE = QStringLiteral("org.kde.kdeconnect_tp");
static const QString WRITE_SECURE_SETTINGS = QStringLiteral("android.permission.WRITE_SECURE_SETTINGS");
// How long to wait for the phone's adb service after asking the phone to turn it on
static constexpr qint64 ADB_WAIT_MS = 25000;
// How long to wait for the user to allow USB debugging on the phone
static constexpr qint64 USB_AUTHORIZATION_WAIT_MS = 60000;
static constexpr int ADB_POLL_MS = 1500;
static constexpr qint64 QR_PAIRING_WAIT_MS = 120000;
static constexpr qint64 CODE_PAIRING_WAIT_MS = 30000;

MirrorPhonePlugin::~MirrorPhonePlugin()
{
    // Close the windows gracefully, so that scrcpy cleans up on the phone
    for (QProcess *process : std::as_const(m_windows)) {
        process->disconnect(this);
        process->terminate();
        if (!process->waitForFinished(3000)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
}

// Serials are "<mDNS name>._adb-tls-connect._tcp" or "<address>:<port>" over Wi-Fi, and the phone's hardware serial over USB
static bool isWirelessSerial(const QString &serial)
{
    return serial.endsWith(QLatin1Char('.') + ADB_TLS_SERVICE) || serial.contains(QLatin1Char(':'));
}

// IPv4-mapped IPv6 addresses as plain IPv4, so that addresses from different sources compare equal
static QString normalizedAddress(const QHostAddress &host)
{
    bool isIpv4 = false;
    const quint32 ipv4 = host.toIPv4Address(&isIpv4);
    return isIpv4 ? QHostAddress(ipv4).toString() : host.toString();
}

static QString hostOf(const QString &address)
{
    const int colon = address.lastIndexOf(QLatin1Char(':'));
    return colon > 0 ? address.left(colon) : QString();
}

// Name to "<address>:<port>" of the services of this type in the output of "adb mdns services"
static QHash<QString, QString> mdnsServices(const QString &output, const QString &type)
{
    QHash<QString, QString> services;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        // <name> <service type> <address>:<port>
        const QStringList fields = line.trimmed().split(QLatin1Char('\t'));
        if (fields.size() == 3 && fields.at(1) == type) {
            services.insert(fields.at(0), fields.at(2));
        }
    }
    return services;
}

void MirrorPhonePlugin::connected()
{
    sendAdbRequest(QStringLiteral("status"));
}

void MirrorPhonePlugin::receivePacket(const NetworkPacket &np)
{
    if (np.type() == PACKET_TYPE_MIRRORPHONE_REQUEST) {
        const QString action = np.get<QString>(QStringLiteral("action"));
        if (action == QLatin1String("mirror")) {
            mirrorScreen(Origin::Phone);
        } else if (action == QLatin1String("status")) {
            m_phoneWantsStatus = true;
            sendStatusToPhone();
            refreshStatus();
        } else {
            qCWarning(KDECONNECT_PLUGIN_MIRRORPHONE) << "Unknown action" << action;
        }
    } else if (np.type() == PACKET_TYPE_MIRRORPHONE_ADB_STATUS) {
        m_phone = QJsonObject{
            {QStringLiteral("known"), true},
            {QStringLiteral("developerOptions"), np.get<bool>(QStringLiteral("developerOptions"), false)},
            {QStringLiteral("usbDebugging"), np.get<bool>(QStringLiteral("usbDebugging"), false)},
            {QStringLiteral("wirelessDebugging"), np.get<bool>(QStringLiteral("wirelessDebugging"), false)},
            {QStringLiteral("canEnable"), np.get<bool>(QStringLiteral("canEnable"), false)},
        };
        qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << "Phone's developer settings" << m_phone;
        // The answer to our request to turn Wireless debugging on
        if (np.has(QStringLiteral("enabled")) && !np.get<bool>(QStringLiteral("enabled")) && m_adbBusy) {
            const QString error = np.get<QString>(QStringLiteral("error"));
            finishAdb(QString(), error.isEmpty() ? i18n("Wireless debugging is off on %1.", device()->name()) : error);
        }
        // When it is on, adbStep() finds the adb service as soon as the phone announces it
        emitStatus();
    }
}

// ---------------------------------------------------------------- actions

void MirrorPhonePlugin::mirrorScreen()
{
    mirrorScreen(Origin::Desktop);
}

void MirrorPhonePlugin::mirrorScreen(Origin origin)
{
    withAdb([this, origin](const QString &serial, const QString &error) {
        if (!error.isEmpty()) {
            reply(QStringLiteral("mirror"), error, origin);
            return;
        }
        startWindow(QStringLiteral("mirror"), QStringLiteral("screen"), device()->name(), {}, serial, origin);
    });
}

// ---------------------------------------------------------------- settings page

QString MirrorPhonePlugin::status() const
{
    return QString::fromUtf8(QJsonDocument(statusJson()).toJson(QJsonDocument::Compact));
}

QJsonObject MirrorPhonePlugin::statusJson() const
{
    QJsonObject phone = m_phone;
    if (phone.isEmpty()) {
        phone.insert(QStringLiteral("known"), false);
    }
    return QJsonObject{
        {QStringLiteral("tools"),
         QJsonObject{
             {QStringLiteral("adb"), tool(QStringLiteral("adb"))},
             {QStringLiteral("scrcpy"), tool(QStringLiteral("scrcpy"))},
         }},
        {QStringLiteral("connection"),
         QJsonObject{
             {QStringLiteral("serial"), m_serial},
             {QStringLiteral("transport"), transport()},
         }},
        {QStringLiteral("busy"), m_adbBusy},
        {QStringLiteral("message"), m_message},
        {QStringLiteral("phone"), phone},
        {QStringLiteral("pairing"),
         QJsonObject{
             {QStringLiteral("state"), m_pairingState},
             {QStringLiteral("message"), m_pairingMessage},
             {QStringLiteral("qr"), m_qrName.isEmpty() ? QString() : QStringLiteral("WIFI:T:ADB;S:%1;P:%2;;").arg(m_qrName, m_qrPassword)},
         }},
    };
}

QString MirrorPhonePlugin::transport() const
{
    return m_serial.isEmpty() ? QString() : isWirelessSerial(m_serial) ? QStringLiteral("wifi") : QStringLiteral("usb");
}

void MirrorPhonePlugin::emitStatus()
{
    Q_EMIT statusChanged(status());
    if (m_phoneWantsStatus) {
        sendStatusToPhone();
    }
}

void MirrorPhonePlugin::sendStatusToPhone()
{
    // What the settings page shows, without the phone's own settings, which the phone knows, and the QR code's secret
    NetworkPacket np(PACKET_TYPE_MIRRORPHONE_STATUS,
                     {
                         {QStringLiteral("transport"), transport()},
                         {QStringLiteral("busy"), m_adbBusy},
                         {QStringLiteral("message"), m_message},
                         {QStringLiteral("pairingState"), m_pairingState},
                         {QStringLiteral("pairingMessage"), m_pairingMessage},
                         {QStringLiteral("adb"), tool(QStringLiteral("adb"))},
                         {QStringLiteral("scrcpy"), tool(QStringLiteral("scrcpy"))},
                     });
    sendPacket(np);
}

void MirrorPhonePlugin::setMessage(const QString &message)
{
    m_message = message;
    emitStatus();
}

void MirrorPhonePlugin::refreshStatus()
{
    sendAdbRequest(QStringLiteral("status"));
    if (tool(QStringLiteral("adb")).isEmpty() || m_adbBusy) {
        emitStatus();
        return;
    }
    findSerial([this](const QString &serial, const QString &serviceAddress) {
        m_serial = serial;
        if (!serial.isEmpty()) {
            setMessage(QString());
        } else if (m_unauthorizedUsb) {
            setMessage(i18n("Allow USB debugging for this computer on %1.", device()->name()));
        } else if (!serviceAddress.isEmpty()) {
            setMessage(i18n("Wireless debugging is on, but this computer is not connected to it yet."));
        } else {
            setMessage(i18n("Not connected."));
        }
    });
}

void MirrorPhonePlugin::connectAdb()
{
    withAdb([](const QString &, const QString &) { });
}

void MirrorPhonePlugin::enableWirelessDebugging()
{
    sendAdbRequest(QStringLiteral("enable"));
}

void MirrorPhonePlugin::openOnPhone(const QString &target)
{
    if (target == QLatin1String("developer") || target == QLatin1String("about")) {
        sendAdbRequest(QStringLiteral("open"), target);
    }
}

void MirrorPhonePlugin::sendAdbRequest(const QString &action, const QString &target)
{
    NetworkPacket np(PACKET_TYPE_MIRRORPHONE_ADB, {{QStringLiteral("action"), action}});
    if (!target.isEmpty()) {
        np.set(QStringLiteral("target"), target);
    }
    sendPacket(np);
}

QString MirrorPhonePlugin::tool(const QString &name) const
{
    QString path = QStandardPaths::findExecutable(name);
    if (path.isEmpty() && name == QLatin1String("adb")) {
        // Android SDK platform tools downloaded by hand are often not in PATH
        const QString home = QDir::homePath();
        path = QStandardPaths::findExecutable(name, {home + QStringLiteral("/Android/Sdk/platform-tools"), home + QStringLiteral("/Android/platform-tools")});
    }
    return path;
}

// ---------------------------------------------------------------- adb

void MirrorPhonePlugin::withAdb(const AdbCallback &callback)
{
    QString missing;
    if (tool(QStringLiteral("adb")).isEmpty()) {
        missing = i18n("adb is not installed on %1. Install the android-tools package.", QSysInfo::machineHostName());
    } else if (tool(QStringLiteral("scrcpy")).isEmpty()) {
        missing = i18n("scrcpy is not installed on %1.", QSysInfo::machineHostName());
    }
    if (!missing.isEmpty()) {
        setMessage(missing);
        callback(QString(), missing);
        return;
    }
    m_adbWaiting.append(callback);
    if (m_adbBusy) {
        return;
    }
    m_adbBusy = true;
    m_askedPhone = false;
    m_notifiedUsb = false;
    m_triedAddresses.clear();
    m_adbTimer.start();
    setMessage(i18n("Connecting..."));
    adbStep();
}

void MirrorPhonePlugin::adbStep()
{
    findSerial([this](const QString &serial, const QString &serviceAddress) {
        if (!m_adbBusy) {
            return;
        } else if (!serial.isEmpty()) {
            finishAdb(serial, QString());
            return;
        }

        // Wireless debugging is on, but adb is not connected to it yet
        if (!serviceAddress.isEmpty() && !m_triedAddresses.contains(serviceAddress)) {
            m_triedAddresses.insert(serviceAddress);
            runAdb({QStringLiteral("connect"), serviceAddress}, [this, serviceAddress](int, const QString &output) {
                if (!m_adbBusy) {
                    return;
                }
                qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << "adb connect" << serviceAddress << output.trimmed();
                if (output.contains(QLatin1String("failed"))) {
                    // Typically "failed to authenticate": this computer was never paired
                    finishAdb(QString(), i18n("This computer is not paired for Wireless debugging with %1 yet.", device()->name()));
                    return;
                }
                adbStep();
            });
            return;
        }

        if (m_unauthorizedUsb && !m_notifiedUsb) {
            // Android shows its own prompt; we only point at it
            m_notifiedUsb = true;
            const QString text = i18n("Tap \"Allow\" on the USB debugging prompt on %1.", device()->name());
            setMessage(text);
            Daemon::instance()->sendSimpleNotification(QStringLiteral("notification"), device()->name(), text, QStringLiteral("smartphone"));
        }
        if (!m_askedPhone && !m_unauthorizedUsb) {
            m_askedPhone = true;
            qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << "Asking" << device()->name() << "to turn on Wireless debugging";
            setMessage(i18n("Asking %1 to turn on Wireless debugging...", device()->name()));
            sendAdbRequest(QStringLiteral("enable"));
        }
        if (m_adbTimer.elapsed() > (m_unauthorizedUsb ? USB_AUTHORIZATION_WAIT_MS : ADB_WAIT_MS)) {
            finishAdb(QString(),
                      m_unauthorizedUsb ? i18n("USB debugging was not allowed on %1.", device()->name())
                                        : i18n("Could not reach %1 over USB or Wireless debugging.", device()->name()));
            return;
        }
        QTimer::singleShot(ADB_POLL_MS, this, &MirrorPhonePlugin::adbStep);
    });
}

void MirrorPhonePlugin::finishAdb(const QString &serial, const QString &error)
{
    m_adbBusy = false;
    m_serial = serial;
    setMessage(error);
    const QList<AdbCallback> waiting = std::exchange(m_adbWaiting, {});
    for (const AdbCallback &callback : waiting) {
        callback(serial, error);
    }

    // Lets the phone turn Wireless debugging on by itself next time
    if (!serial.isEmpty() && !m_grantedPermission) {
        m_grantedPermission = true;
        runShell(serial, {QStringLiteral("pm"), QStringLiteral("grant"), ANDROID_PACKAGE, WRITE_SECURE_SETTINGS}, [this](int exitCode, const QString &output) {
            qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << "Granting WRITE_SECURE_SETTINGS:" << exitCode << output.trimmed();
            sendAdbRequest(QStringLiteral("status"));
        });
    }
}

QSet<QString> MirrorPhonePlugin::phoneAddresses() const
{
    QSet<QString> addresses;
    const QStringList reachable = device()->reachableAddresses();
    for (const QString &address : reachable) {
        addresses.insert(normalizedAddress(QHostAddress(address)));
    }
    return addresses;
}

void MirrorPhonePlugin::findSerial(const SerialCallback &callback)
{
    runAdb({QStringLiteral("mdns"), QStringLiteral("services")}, [this, callback](int, const QString &mdnsOutput) {
        const QHash<QString, QString> services = mdnsServices(mdnsOutput, ADB_TLS_SERVICE);
        runAdb({QStringLiteral("devices")}, [this, callback, services](int, const QString &devicesOutput) {
            const QSet<QString> addresses = phoneAddresses();
            const QString suffix = QLatin1Char('.') + ADB_TLS_SERVICE;

            // <serial> <state>
            QString wirelessSerial;
            QStringList usbSerials;
            m_unauthorizedUsb = false;
            for (const QString &line : devicesOutput.split(QLatin1Char('\n'))) {
                const QStringList fields = line.simplified().split(QLatin1Char(' '));
                if (fields.size() < 2 || line.startsWith(QLatin1String("List of devices"))) {
                    continue;
                }
                const QString &serial = fields.at(0);
                const bool wireless = isWirelessSerial(serial);
                if (fields.at(1) != QLatin1String("device")) {
                    m_unauthorizedUsb = m_unauthorizedUsb || (!wireless && fields.at(1) == QLatin1String("unauthorized"));
                    continue;
                }
                if (!wireless) {
                    usbSerials.append(serial);
                    continue;
                }
                const QString address = serial.endsWith(suffix) ? services.value(serial.chopped(suffix.size())) : serial;
                if (wirelessSerial.isEmpty() && addresses.contains(hostOf(address))) {
                    wirelessSerial = serial;
                }
            }
            QString serviceAddress;
            for (const QString &address : services) {
                if (addresses.contains(hostOf(address))) {
                    serviceAddress = address;
                    break;
                }
            }

            // USB first unless the user prefers Wi-Fi: faster, and needs no Wireless debugging
            const bool preferUsb = config()->getBool(QStringLiteral("preferUsb"), true);
            if (!preferUsb && !wirelessSerial.isEmpty()) {
                callback(wirelessSerial, QString());
                return;
            }
            matchUsb(usbSerials, addresses, [callback, wirelessSerial, serviceAddress](const QString &usbSerial) {
                if (!usbSerial.isEmpty()) {
                    callback(usbSerial, QString());
                } else {
                    callback(wirelessSerial, wirelessSerial.isEmpty() ? serviceAddress : QString());
                }
            });
        });
    });
}

void MirrorPhonePlugin::matchUsb(QStringList serials, const QSet<QString> &addresses, const std::function<void(const QString &serial)> &callback)
{
    if (serials.isEmpty() || addresses.isEmpty()) {
        callback(QString());
        return;
    }
    // A USB serial says nothing about which phone it is, but the phone's addresses do
    const QString serial = serials.takeFirst();
    runShell(serial, {QStringLiteral("ip"), QStringLiteral("-o"), QStringLiteral("addr")}, [this, serial, serials, addresses, callback](int, const QString &output) {
        static const QRegularExpression inet(QStringLiteral("\\binet6? ([0-9a-fA-F.:]+)/"));
        for (auto it = inet.globalMatch(output); it.hasNext();) {
            if (addresses.contains(normalizedAddress(QHostAddress(it.next().captured(1))))) {
                qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << "USB device" << serial << "is" << device()->name();
                callback(serial);
                return;
            }
        }
        matchUsb(serials, addresses, callback);
    });
}

void MirrorPhonePlugin::runAdb(const QStringList &arguments, const ProcessCallback &callback, int timeoutMs)
{
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto *timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this, [process, callback](int exitCode) {
        process->deleteLater();
        if (callback) {
            callback(exitCode, QString::fromUtf8(process->readAll()));
        }
    });
    connect(process, &QProcess::errorOccurred, this, [process, callback](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            if (callback) {
                callback(-1, process->errorString());
            }
        }
    });
    process->start(tool(QStringLiteral("adb")), arguments);
    timeout->start(timeoutMs);
}

void MirrorPhonePlugin::runShell(const QString &serial, const QStringList &command, const ProcessCallback &callback)
{
    runAdb(QStringList{QStringLiteral("-s"), serial, QStringLiteral("shell")} + command, callback);
}

// ---------------------------------------------------------------- pairing

void MirrorPhonePlugin::startQrPairing()
{
    if (tool(QStringLiteral("adb")).isEmpty()) {
        setPairing(QStringLiteral("failed"), i18n("adb is not installed."));
        return;
    }
    // Letters and digits only, as the QR code's format reserves some characters
    const auto randomText = [](int length) {
        static const QLatin1String characters("abcdefghijklmnopqrstuvwxyz0123456789");
        QString text;
        for (int i = 0; i < length; ++i) {
            text.append(characters.at(QRandomGenerator::system()->bounded(int(characters.size()))));
        }
        return text;
    };
    clearPairingSecrets();
    m_qrName = QStringLiteral("kdeconnect-") + randomText(10);
    m_qrPassword = randomText(12);
    m_pairingTimer.start();
    ++m_pairingGeneration;
    setPairing(QStringLiteral("scan"),
               i18n("On %1, open Developer options -> Wireless debugging -> Pair device with QR code, and scan this code.", device()->name()));
    pollPairing();
}

void MirrorPhonePlugin::pairWithCode(const QString &code)
{
    static const QRegularExpression codePattern(QRegularExpression::anchoredPattern(QStringLiteral("\\d{6}")));
    if (!codePattern.match(code.trimmed()).hasMatch()) {
        setPairing(QStringLiteral("failed"), i18n("Enter the six-digit pairing code shown on the phone."));
        return;
    } else if (tool(QStringLiteral("adb")).isEmpty()) {
        setPairing(QStringLiteral("failed"), i18n("adb is not installed."));
        return;
    }
    clearPairingSecrets();
    m_pairingCode = code.trimmed();
    m_pairingTimer.start();
    ++m_pairingGeneration;
    setPairing(QStringLiteral("code"), i18n("Looking for %1...", device()->name()));
    pollPairing();
}

void MirrorPhonePlugin::cancelPairing()
{
    ++m_pairingGeneration;
    clearPairingSecrets();
    setPairing(QStringLiteral("idle"), QString());
}

void MirrorPhonePlugin::clearPairingSecrets()
{
    m_qrName.clear();
    m_qrPassword.clear();
    m_pairingCode.clear();
}

void MirrorPhonePlugin::setPairing(const QString &state, const QString &message)
{
    m_pairingState = state;
    m_pairingMessage = message;
    emitStatus();
}

void MirrorPhonePlugin::pollPairing()
{
    const int generation = m_pairingGeneration;
    runAdb({QStringLiteral("mdns"), QStringLiteral("services")}, [this, generation](int, const QString &output) {
        if (generation != m_pairingGeneration) {
            return; // cancelled or restarted
        }
        // After "Pair device with QR code", the phone offers pairing as an mDNS service named as in the code;
        // after "Pair device with pairing code", under some name, at its address
        const QSet<QString> addresses = phoneAddresses();
        for (const auto &[name, address] : mdnsServices(output, ADB_PAIRING_SERVICE).asKeyValueRange()) {
            if (!m_qrName.isEmpty() && name == m_qrName) {
                runPair(address, m_qrPassword);
                return;
            } else if (!m_pairingCode.isEmpty() && addresses.contains(hostOf(address))) {
                runPair(address, m_pairingCode);
                return;
            }
        }
        if (m_pairingTimer.elapsed() > (m_qrName.isEmpty() ? CODE_PAIRING_WAIT_MS : QR_PAIRING_WAIT_MS)) {
            const QString message = m_qrName.isEmpty()
                ? i18n("Could not find %1 offering a pairing code. Open \"Pair device with pairing code\" on the phone, then enter the code.", device()->name())
                : i18n("The QR code was not scanned in time. Show a new code to try again.");
            clearPairingSecrets();
            setPairing(QStringLiteral("failed"), message);
            return;
        }
        QTimer::singleShot(1000, this, [this, generation] {
            if (generation == m_pairingGeneration) {
                pollPairing();
            }
        });
    });
}

void MirrorPhonePlugin::runPair(const QString &address, const QString &secret)
{
    const int generation = ++m_pairingGeneration;
    setPairing(QStringLiteral("pairing"), i18n("Pairing with %1...", device()->name()));
    // A wrong address can leave adb waiting for minutes
    runAdb(
        {QStringLiteral("pair"), address, secret},
        [this, generation](int exitCode, const QString &output) {
            if (generation != m_pairingGeneration) {
                return;
            }
            clearPairingSecrets();
            qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << "adb pair:" << exitCode << output.trimmed();
            if (exitCode == 0 && output.contains(QLatin1String("Successfully paired"))) {
                setPairing(QStringLiteral("paired"), i18n("Paired. This computer connects to %1 over Wireless debugging from now on.", device()->name()));
                // The phone announces its connection service right after pairing
                QTimer::singleShot(2000, this, &MirrorPhonePlugin::connectAdb);
            } else {
                setPairing(QStringLiteral("failed"), i18n("Pairing failed. Start pairing again on the phone.\n%1", output.trimmed()));
            }
        },
        30000);
}

// ---------------------------------------------------------------- scrcpy

QProcess *MirrorPhonePlugin::newScrcpyProcess()
{
    auto *process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // The same adb as ours, as two adb versions keep restarting each other's server
    environment.insert(QStringLiteral("ADB"), tool(QStringLiteral("adb")));
    process->setProcessEnvironment(environment);
    return process;
}

void MirrorPhonePlugin::startWindow(const QString &action,
                                    const QString &key,
                                    const QString &title,
                                    const QStringList &arguments,
                                    const QString &serial,
                                    Origin origin)
{
    if (m_windows.contains(key)) {
        reply(action, QString(), origin);
        return;
    }

    QProcess *process = newScrcpyProcess();
    process->setProcessChannelMode(QProcess::MergedChannels);
    m_windows.insert(key, process);

    auto output = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output] {
        output->append(process->readAllStandardOutput());
    });

    connect(process, &QProcess::finished, this, [this, key, title, process, output](int exitCode, QProcess::ExitStatus exitStatus) {
        m_windows.remove(key);
        if (m_audioWindow == key) {
            m_audioWindow.clear();
        }
        process->deleteLater();
        output->append(process->readAll());
        const QStringList lines = QString::fromUtf8(*output).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << "scrcpy for" << key << "ended with" << exitCode << (lines.isEmpty() ? QString() : lines.constLast());
        if (exitStatus == QProcess::NormalExit && exitCode == 0) {
            return;
        }
        QString error;
        for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
            if (it->startsWith(QLatin1String("ERROR:"))) {
                error = it->mid(6).trimmed();
                break;
            }
        }
        Daemon::instance()->sendSimpleNotification(QStringLiteral("error"),
                                                   title,
                                                   error.isEmpty() ? i18n("The window stopped unexpectedly.") : error,
                                                   QStringLiteral("dialog-error"));
    });
    connect(process, &QProcess::errorOccurred, this, [this, key, process, action, origin](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_windows.remove(key);
            process->deleteLater();
            reply(action, i18n("Could not start scrcpy: %1", process->errorString()), origin);
        }
    });
    connect(process, &QProcess::started, this, [this, action, origin] {
        reply(action, QString(), origin);
    });

    QStringList allArguments{QStringLiteral("--serial=") + serial};
    allArguments << arguments;
    // Only one window plays the phone's sound, as each would capture all of it
    if (m_audioWindow.isEmpty() && config()->getBool(QStringLiteral("audio"), true)) {
        m_audioWindow = key;
    } else {
        allArguments << QStringLiteral("--no-audio");
    }
    allArguments << QStringLiteral("--window-title=") + title;
    qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << "Starting scrcpy" << allArguments;
    process->start(tool(QStringLiteral("scrcpy")), allArguments);
}

// ----------------------------------------------------------------

void MirrorPhonePlugin::reply(const QString &action, const QString &error, Origin origin)
{
    qCDebug(KDECONNECT_PLUGIN_MIRRORPHONE) << action << (error.isEmpty() ? QStringLiteral("ok") : error);
    if (origin == Origin::Phone) {
        NetworkPacket np(PACKET_TYPE_MIRRORPHONE_RESPONSE,
                         {
                             {QStringLiteral("action"), action},
                             {QStringLiteral("success"), error.isEmpty()},
                             {QStringLiteral("error"), error},
                         });
        sendPacket(np);
    }
    if (!error.isEmpty()) {
        notifySetup(error);
    }
}

void MirrorPhonePlugin::notifySetup(const QString &text)
{
    // Errors here are about adb, so they lead to the plugin's settings, where it can be set up
    auto *notification = new KNotification(QStringLiteral("error"));
    notification->setComponentName(QStringLiteral("kdeconnect"));
    notification->setTitle(device()->name());
    notification->setText(text);
    notification->setIconName(QStringLiteral("dialog-error"));
    KNotificationAction *setup = notification->addAction(i18nc("@action:button", "Set Up..."));
    const QString deviceId = device()->id();
    connect(setup, &KNotificationAction::activated, this, [deviceId] {
        QProcess::startDetached(QStringLiteral("kdeconnect-app"), {QStringLiteral("--device"), deviceId, QStringLiteral("--plugin-config"), QStringLiteral("kdeconnect_mirrorphone")});
    });
    notification->sendEvent();
}

QString MirrorPhonePlugin::dbusPath() const
{
    return QLatin1String("/modules/kdeconnect/devices/%1/mirrorphone").arg(device()->id());
}

#include "moc_mirrorphoneplugin.cpp"
#include "mirrorphoneplugin.moc"
