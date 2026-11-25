/*
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
    property QtObject device
    property var apps: []
    property bool loading: true
    property string error: ""
    title: i18nd("kdeconnect-app", "Open app")

    header: QQC2.Control {
        padding: Kirigami.Units.largeSpacing
        contentItem: ColumnLayout {
            Kirigami.SearchField {
                id: search
                focus: true
                Layout.fillWidth: true
            }
            // The app runs on a display of its own, so the device's screen is not needed
            QQC2.CheckBox {
                id: screenOff
                text: i18nd("kdeconnect-app", "Turn the device's screen off")
                checked: config.getBool("appScreenOff", false)
                onToggled: config.set("appScreenOff", checked)
            }
        }
    }

    KdeConnectPluginConfig {
        id: config
        deviceId: root.device.id()
        pluginName: "kdeconnect_mirrorphone"
    }

    actions: [
        Kirigami.Action {
            icon.name: "view-refresh"
            text: i18nd("kdeconnect-app", "Refresh")
            enabled: !root.loading
            onTriggered: root.refresh()
        }
    ]

    function refresh() {
        loading = true;
        error = "";
        mirrorPhone.requestApps();
    }

    Component.onCompleted: refresh()

    MirrorPhoneDevice {
        id: mirrorPhone
        deviceId: root.device.id()
        onAppsReceived: (apps, error) => {
            root.loading = false;
            root.error = error;
            root.apps = apps;
        }
    }

    ListView {
        id: list
        model: {
            const query = search.text.toLowerCase();
            return query ? root.apps.filter(app => app.name.toLowerCase().includes(query) || app.package.toLowerCase().includes(query)) : root.apps;
        }
        delegate: QQC2.ItemDelegate {
            required property var modelData
            width: ListView.view.width
            contentItem: ColumnLayout {
                spacing: 0
                QQC2.Label {
                    text: modelData.name
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                QQC2.Label {
                    text: modelData.package
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
            onClicked: {
                mirrorPhone.openApp(modelData.package, modelData.name, screenOff.checked);
                showPassiveNotification(i18nd("kdeconnect-app", "Opening %1...", modelData.name));
            }
        }

        QQC2.BusyIndicator {
            anchors.centerIn: parent
            visible: root.loading
            running: visible
        }

        Kirigami.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 4
            visible: !root.loading && root.error.length > 0
            icon.name: "dialog-error"
            text: root.error
        }
    }
}
