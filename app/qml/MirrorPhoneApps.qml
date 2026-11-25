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
    // package -> URL of its cached icon; the phone sends missing icons one by one, after the list
    property var icons: ({})
    // Android's stock app icon, for apps whose icon has not arrived yet
    property string placeholderIcon
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
            onTriggered: root.refresh(true)
        }
    ]

    function refresh(reloadIcons) {
        loading = true;
        error = "";
        mirrorPhone.requestApps(reloadIcons);
    }

    Component.onCompleted: refresh(false)

    MirrorPhoneDevice {
        id: mirrorPhone
        deviceId: root.device.id()
        onAppsReceived: (apps, error) => {
            const icons = {};
            for (const app of apps) {
                if (app.icon) {
                    icons[app.package] = app.icon;
                }
            }
            root.loading = false;
            root.error = error;
            root.icons = icons;
            root.apps = apps;
        }
        onIconReceived: (pkg, url) => {
            // A new URL, as a fresh icon keeps the cached icon's file name
            const fresh = url + "?" + Date.now();
            if (pkg === "") {
                root.placeholderIcon = fresh;
            } else {
                root.icons = Object.assign({}, root.icons, {[pkg]: fresh});
            }
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
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Image {
                    source: root.icons[modelData.package] || root.placeholderIcon
                    sourceSize.width: Kirigami.Units.iconSizes.medium
                    sourceSize.height: Kirigami.Units.iconSizes.medium
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                }
                ColumnLayout {
                    spacing: 0
                    Layout.fillWidth: true
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
