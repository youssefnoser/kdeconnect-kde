/**
 * SPDX-FileCopyrightText: 2026 KDE Connect contributors
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kdeconnect
import org.kde.kdeconnect.private.mirrorphone

Kirigami.ScrollablePage {
    id: root

    property string device

    readonly property var status: mirrorPhone.status
    readonly property var tools: status.tools || {}
    readonly property var connection: status.connection || {}
    readonly property var phone: status.phone || {}
    readonly property bool busy: status.busy || false

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
            visible: root.phone.known
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
            visible: root.phone.known && !root.phone.developerOptions
            text: i18n("Tap the notification on the device to open About phone, then tap \"Build number\" seven times. Some devices list it under Software information.")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
        }

        RowLayout {
            Kirigami.FormData.label: i18n("USB debugging:")
            visible: root.phone.known
            OnOff {
                on: root.phone.usbDebugging || false
            }
            QQC2.Button {
                visible: !root.phone.usbDebugging && root.phone.developerOptions
                text: i18n("Open on Device")
                icon.name: "smartphone"
                onClicked: mirrorPhone.openOnPhone("developer")
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Wireless debugging:")
            visible: root.phone.known
            OnOff {
                on: root.phone.wirelessDebugging || false
            }
            QQC2.Button {
                visible: !root.phone.wirelessDebugging && root.phone.developerOptions
                text: root.phone.canEnable ? i18n("Turn On") : i18n("Open on Device")
                icon.name: root.phone.canEnable ? "network-wireless" : "smartphone"
                onClicked: root.phone.canEnable ? mirrorPhone.enableWirelessDebugging() : mirrorPhone.openOnPhone("developer")
            }
        }

        QQC2.Label {
            Kirigami.FormData.label: i18n("Turn on by itself:")
            visible: root.phone.known
            text: root.phone.canEnable
                ? i18n("Allowed: the device turns Wireless debugging on when this computer needs it")
                : i18n("Allowed automatically the first time this computer connects over adb, by USB or Wireless debugging")
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
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
