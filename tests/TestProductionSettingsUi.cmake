if(NOT EXISTS "${SETTINGS_QML}")
    message(FATAL_ERROR "Settings QML not found: ${SETTINGS_QML}")
endif()

file(READ "${SETTINGS_QML}" settings_qml)

set(hidden_file_transfer_markers
    ".devocab"
    "Backup / Import / Export"
    "Export vocabulary database"
    "Import and replace"
    "Import and merge"
    "exportVocabularyDatabase"
    "importDatabaseReplace"
    "importDatabaseMerge"
)

foreach(marker IN LISTS hidden_file_transfer_markers)
    string(FIND "${settings_qml}" "${marker}" marker_position)
    if(NOT marker_position EQUAL -1)
        message(FATAL_ERROR "Production settings still expose file transfer marker: ${marker}")
    endif()
endforeach()

set(required_unrelated_settings
    "Statistics"
    "Synchronization"
    "requestManualSync"
    "Danger zone"
)

foreach(marker IN LISTS required_unrelated_settings)
    string(FIND "${settings_qml}" "${marker}" marker_position)
    if(marker_position EQUAL -1)
        message(FATAL_ERROR "Unrelated production setting was removed: ${marker}")
    endif()
endforeach()
