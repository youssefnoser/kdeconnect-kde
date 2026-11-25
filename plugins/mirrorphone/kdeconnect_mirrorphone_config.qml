/**
 * SPDX-FileCopyrightText: 2026 Youssef Noser <youssefnoser1i@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.prison as Prison
import org.kde.kdeconnect
import org.kde.kdeconnect.private.mirrorphone

Kirigami.ScrollablePage {
    id: root

    property string device

    readonly property var status: mirrorPhone.status
    readonly property var tools: status.tools || {}
    readonly property var connection: status.connection || {}
    readonly property var phone: status.phone || {}
    readonly property var pairing: status.pairing || {}
    readonly property bool busy: status.busy || false
    property bool enteringCode: false

    Component.onCompleted: mirrorPhone.refresh()

    MirrorPhoneDevice {
        id: mirrorPhone
        deviceId: root.device
    }

    KdeConnectPluginConfig {
        id: config
        deviceId: root.device
        pluginName: "kdeconnect_mirrorphone"
    }

    actions: Kirigami.Action {
        icon.name: "view-refresh"
        text: i18n("Refresh")
        onTriggered: mirrorPhone.refresh()
    }

    component OnOff: QQC2.Label {
        property bool on
        text: on ? i18nc("@info a setting's state", "On") : i18nc("@info a setting's state", "Off")
        color: on ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
    }

    Kirigami.FormLayout {
        // ------------------------------------------------ connection
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Connection")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Status:")
            QQC2.BusyIndicator {
                visible: root.busy
                running: visible
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                text: {
                    if (root.busy) {
                        return root.status.message || i18n("Connecting...");
                    } else if (root.connection.transport === "usb") {
                        return i18n("Connected over USB");
                    } else if (root.connection.transport === "wifi") {
                        return i18n("Connected over Wireless debugging");
                    }
                    return root.status.message || i18n("Not connected");
                }
                wrapMode: Text.Wrap
                Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            }
        }

        QQC2.Button {
            text: i18n("Connect")
            icon.name: "network-connect"
            visible: !root.connection.serial
            enabled: !root.busy && !!root.tools.adb
            onClicked: mirrorPhone.connectAdb()
        }

        // ------------------------------------------------ phone
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Device")
        }

        QQC2.Label {
            visible: !root.phone.known
            text: i18n("Waiting for the device to report its developer settings. It needs the KDE Connect version with \"Mirror phone on desktop\".")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Developer options:")
            visible: !!root.phone.known
            OnOff {
                on: root.phone.developerOptions || false
            }
            QQC2.Button {
                visible: !root.phone.developerOptions
                text: i18n("Show Me How")
                icon.name: "help-hint"
                onClicked: mirrorPhone.openOnPhone("about")
            }
        }

        QQC2.Label {
            visible: !!root.phone.known && !root.phone.developerOptions
            text: i18n("Tap the notification on the device to open About phone, then tap \"Build number\" seven times. Some devices list it under Software information.")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }

        RowLayout {
            Kirigami.FormData.label: i18n("USB debugging:")
            visible: !!root.phone.known
            OnOff {
                on: root.phone.usbDebugging || false
            }
            QQC2.Button {
                visible: !root.phone.usbDebugging && !!root.phone.developerOptions
                text: i18n("Open on Device")
                icon.name: "smartphone"
                onClicked: mirrorPhone.openOnPhone("developer")
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Wireless debugging:")
            visible: !!root.phone.known
            OnOff {
                on: root.phone.wirelessDebugging || false
            }
            QQC2.Button {
                visible: !root.phone.wirelessDebugging && !!root.phone.developerOptions
                text: root.phone.canEnable ? i18n("Turn On") : i18n("Open on Device")
                icon.name: root.phone.canEnable ? "network-wireless" : "smartphone"
                onClicked: root.phone.canEnable ? mirrorPhone.enableWirelessDebugging() : mirrorPhone.openOnPhone("developer")
            }
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Turn on by itself:")
            visible: !!root.phone.known
            text: root.phone.canEnable
                ? i18n("Allowed: the device turns Wireless debugging on when this computer needs it")
                : i18n("Allowed automatically the first time this computer connects over adb, by USB or Wireless debugging")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }

        // ------------------------------------------------ pairing
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Wireless Debugging Pairing")
        }

        QQC2.Label {
            text: i18n("Needed once for each computer to use Wireless debugging. A USB cable needs no pairing, only \"Allow\" on the device.")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }

        RowLayout {
            visible: root.pairing.state !== "scan" && root.pairing.state !== "pairing" && root.pairing.state !== "code"
            QQC2.Button {
                text: i18n("Pair with QR Code")
                icon.name: "view-barcode-qr"
                enabled: !!root.tools.adb
                onClicked: {
                    root.enteringCode = false;
                    mirrorPhone.startQrPairing();
                }
            }
            QQC2.Button {
                text: i18n("Pair with Pairing Code")
                icon.name: "dialog-password"
                enabled: !!root.tools.adb
                onClicked: root.enteringCode = true
            }
        }

        // The phone may scan from the dark theme's background too, so the code gets its own light one
        Rectangle {
            visible: root.pairing.state === "scan" && !!root.pairing.qr
            color: "white"
            implicitWidth: Kirigami.Units.gridUnit * 12
            implicitHeight: implicitWidth
            Prison.Barcode {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                barcodeType: Prison.Barcode.QRCode
                content: root.pairing.qr || ""
                foregroundColor: "black"
                backgroundColor: "white"
            }
        }

        RowLayout {
            visible: root.enteringCode && root.pairing.state !== "pairing" && root.pairing.state !== "code"
            QQC2.TextField {
                id: codeField
                placeholderText: i18n("Six-digit code")
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator {
                    regularExpression: /\d{0,6}/
                }
                onAccepted: pairButton.clicked()
            }
            QQC2.Button {
                id: pairButton
                text: i18n("Pair")
                enabled: codeField.text.length === 6
                onClicked: mirrorPhone.pairWithCode(codeField.text)
            }
        }

        QQC2.Label {
            visible: root.enteringCode && !root.pairing.message
            text: i18n("On the device, open Developer options -> Wireless debugging -> Pair device with pairing code, then enter the code shown there.")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }

        RowLayout {
            visible: !!root.pairing.message
            QQC2.BusyIndicator {
                visible: root.pairing.state === "scan" || root.pairing.state === "code" || root.pairing.state === "pairing"
                running: visible
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
            }
            QQC2.Label {
                text: root.pairing.message || ""
                color: root.pairing.state === "failed" ? Kirigami.Theme.negativeTextColor
                     : root.pairing.state === "paired" ? Kirigami.Theme.positiveTextColor
                     : Kirigami.Theme.textColor
                wrapMode: Text.Wrap
                Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            }
        }

        QQC2.Button {
            visible: root.pairing.state === "scan" || root.pairing.state === "code"
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onClicked: {
                root.enteringCode = false;
                mirrorPhone.cancelPairing();
            }
        }

        // ------------------------------------------------ options
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Options")
        }

        QQC2.CheckBox {
            text: i18n("Play the device's sound on this computer")
            checked: config.getBool("audio", true)
            onToggled: config.set("audio", checked)
        }

        QQC2.CheckBox {
            text: i18n("Prefer USB over Wireless debugging")
            checked: config.getBool("preferUsb", true)
            onToggled: config.set("preferUsb", checked)
        }

        // ------------------------------------------------ tools
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Tools on This Computer")
        }

        QQC2.Label {
            Kirigami.FormData.label: "adb:"
            text: root.tools.adb || i18n("Not installed. Install the android-tools package.")
            color: root.tools.adb ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
        }

        QQC2.Label {
            Kirigami.FormData.label: "scrcpy:"
            text: root.tools.scrcpy || i18n("Not installed. Install the scrcpy package.")
            color: root.tools.scrcpy ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
        }
    }
}
