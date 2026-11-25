# QML helper for the settings page and the app's device page

ecm_add_qml_module(kdeconnect_mirrorphone_qmlhelper
    URI "org.kde.kdeconnect.private.mirrorphone"
    GENERATE_PLUGIN_SOURCE
)

target_sources(kdeconnect_mirrorphone_qmlhelper PRIVATE mirrorphonedevice.cpp)

target_link_libraries(kdeconnect_mirrorphone_qmlhelper PRIVATE
    Qt::Qml
    Qt::DBus
)

ecm_finalize_qml_module(kdeconnect_mirrorphone_qmlhelper DESTINATION ${KDE_INSTALL_QMLDIR})

# The plugin

kdeconnect_add_plugin(kdeconnect_mirrorphone SOURCES mirrorphoneplugin.cpp)
target_link_libraries(kdeconnect_mirrorphone
    kdeconnectcore
    Qt::DBus
    Qt::Network
    KF6::I18n
    KF6::Notifications
)

install(FILES "kdeconnect_mirrorphone_config.qml" DESTINATION ${KDE_INSTALL_DATADIR}/kdeconnect)
