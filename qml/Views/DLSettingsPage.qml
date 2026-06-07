pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "settings"

    property int totalWords: 0
    property int totalGroups: 0
    property double databaseBytes: -1
    property string statusMessage: ""
    property bool statusIsError: false
    property string pendingImportMode: ""
    property string selectedImportPath: ""

    readonly property color green: "#35a969"
    readonly property color greenSoft: Qt.rgba(53 / 255, 169 / 255, 105 / 255, 0.12)
    readonly property color red: "#dc3545"
    readonly property color redSoft: Qt.rgba(220 / 255, 53 / 255, 69 / 255, 0.10)
    readonly property color orange: "#e27a34"
    readonly property color orangeSoft: Qt.rgba(226 / 255, 122 / 255, 52 / 255, 0.12)
    readonly property color fieldBg: "#f1f4f9"

    Component.onCompleted: reloadStats()

    Connections {
        target: appStateManager

        function onWordsChanged() {
            root.reloadStats()
        }
    }

    function reloadStats() {
        var stats = appStateManager.databaseStats()
        totalWords = Number(stats.word_count || 0)
        totalGroups = Number(stats.group_count || 0)
        databaseBytes = Number(stats.db_size_bytes === undefined ? -1 : stats.db_size_bytes)
        showErrorFromManager()
    }

    function showErrorFromManager() {
        if (appStateManager.lastError && appStateManager.lastError.length > 0) {
            showStatus(appStateManager.lastError, true)
        }
    }

    function showStatus(message, isError) {
        statusMessage = message
        statusIsError = isError
        statusTimer.restart()
    }

    function formatCount(value, singular, plural) {
        return value + " " + (value === 1 ? singular : plural)
    }

    function formatDatabaseSize(bytes) {
        if (bytes < 0) {
            return "Not available"
        }

        if (bytes < 1024) {
            return bytes + " B"
        }

        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(1) + " KB"
        }

        return (bytes / (1024 * 1024)).toFixed(1) + " MB"
    }

    function exportDatabase(path) {
        if (appStateManager.exportDatabase(path)) {
            showStatus("Database exported.", false)
        } else {
            showStatus(appStateManager.lastError || "Export failed.", true)
        }
    }

    function importDatabase(path, mode) {
        var success = mode === "replace"
                ? appStateManager.importDatabaseReplace(path)
                : appStateManager.importDatabaseMerge(path)

        if (success) {
            reloadStats()
            showStatus(mode === "replace" ? "Database replaced." : "Database merged.", false)
        } else {
            showStatus(appStateManager.lastError || "Import failed.", true)
        }
    }

    Timer {
        id: statusTimer

        interval: 4200
        repeat: false

        onTriggered: root.statusMessage = ""
    }

    FileDialog {
        id: exportDialog

        title: "Export vocabulary database"
        fileMode: FileDialog.SaveFile
        nameFilters: [ "SQLite database (*.sqlite *.db)", "All files (*)" ]
        defaultSuffix: "sqlite"

        onAccepted: root.exportDatabase(selectedFile.toString())
    }

    FileDialog {
        id: importDialog

        title: root.pendingImportMode === "replace" ? "Import and replace" : "Import and merge"
        fileMode: FileDialog.OpenFile
        nameFilters: [ "SQLite database (*.sqlite *.db)", "All files (*)" ]

        onAccepted: {
            root.selectedImportPath = selectedFile.toString()
            if (root.pendingImportMode === "replace") {
                replaceImportDialog.open()
            } else {
                mergeImportDialog.open()
            }
        }
    }

    Dialog {
        id: replaceImportDialog

        title: "Replace database?"
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok

        Label {
            width: Math.min(root.width - 72, 360)
            text: "This will replace all current vocabulary and groups with the selected database."
            wrapMode: Text.WordWrap
        }

        onAccepted: root.importDatabase(root.selectedImportPath, "replace")
    }

    Dialog {
        id: mergeImportDialog

        title: "Merge database?"
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok

        Label {
            width: Math.min(root.width - 72, 360)
            text: "Words and groups from the selected database will be added. Existing duplicate word pairs are skipped."
            wrapMode: Text.WordWrap
        }

        onAccepted: root.importDatabase(root.selectedImportPath, "merge")
    }

    Dialog {
        id: deleteAllDialog

        title: "Delete all data?"
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok

        Label {
            width: Math.min(root.width - 72, 360)
            text: "This permanently deletes all saved vocabulary and groups."
            wrapMode: Text.WordWrap
        }

        onAccepted: {
            if (appStateManager.deleteAllData()) {
                root.reloadStats()
                root.showStatus("All vocabulary and groups deleted.", false)
            } else {
                root.showStatus(appStateManager.lastError || "Delete failed.", true)
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 92

        Column {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            spacing: 5

            Text {
                width: parent.width
                text: "Settings"
                color: root.textMain
                font.pixelSize: 34
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: formatCount(root.totalWords, "word", "words") + " saved"
                color: root.textMuted
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }

    Rectangle {
        visible: root.statusMessage.length > 0
        Layout.fillWidth: true
        Layout.bottomMargin: visible ? 14 : 0
        implicitHeight: statusLabel.implicitHeight + 24
        radius: 8
        color: root.statusIsError ? root.redSoft : root.greenSoft
        border.color: root.statusIsError ? root.red : root.green
        border.width: 1

        Text {
            id: statusLabel

            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 14
                rightMargin: 14
            }
            text: root.statusMessage
            color: root.statusIsError ? root.red : root.green
            font.pixelSize: 14
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
    }

    SettingsSectionCard {
        title: "Statistics"

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            StatTile {
                label: "Words"
                value: root.totalWords.toString()
                accentColor: root.blue
            }

            StatTile {
                label: "Groups"
                value: root.totalGroups.toString()
                accentColor: root.green
            }
        }

        SettingsRow {
            label: "Database size"
            value: root.formatDatabaseSize(root.databaseBytes)
            iconText: "DB"
        }
    }
    
    SettingsSectionCard {
        title: "Backup / Import / Export"

        ActionRow {
            label: "Export vocabulary database"
            value: "Save a copy"
            iconText: "EX"
            buttonText: "Export"

            onTriggered: exportDialog.open()
        }

        ActionRow {
            label: "Import and replace"
            value: "Overwrite current data"
            iconText: "IR"
            buttonText: "Replace"
            buttonColor: root.orange

            onTriggered: {
                root.pendingImportMode = "replace"
                importDialog.open()
            }
        }

        ActionRow {
            label: "Import and merge"
            value: "Add missing data"
            iconText: "IM"
            buttonText: "Merge"

            onTriggered: {
                root.pendingImportMode = "merge"
                importDialog.open()
            }
        }
    }

    SettingsSectionCard {
        title: "Danger zone"
        danger: true
        Layout.bottomMargin: 18

        ActionRow {
            label: "Delete all vocabulary/groups"
            value: "Permanent"
            iconText: "!"
            buttonText: "Delete"
            buttonColor: root.red
            iconColor: root.red
            iconBackground: root.redSoft

            onTriggered: deleteAllDialog.open()
        }
    }

    component SettingsSectionCard: Rectangle {
        id: sectionCard

        required property string title
        property bool danger: false
        default property alias content: sectionContent.data

        Layout.fillWidth: true
        Layout.bottomMargin: 16
        radius: 8
        color: root.cardBg
        border.color: danger ? root.redSoft : root.line
        border.width: 1
        implicitHeight: sectionLayout.implicitHeight + 28

        ColumnLayout {
            id: sectionLayout

            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                leftMargin: 14
                rightMargin: 14
                topMargin: 14
            }
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: sectionCard.title
                color: sectionCard.danger ? root.red : root.textMain
                font.pixelSize: 16
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            ColumnLayout {
                id: sectionContent

                Layout.fillWidth: true
                spacing: 0
            }
        }
    }

    component StatTile: Rectangle {
        id: statTile

        required property string label
        required property string value
        property color accentColor: root.blue

        Layout.fillWidth: true
        Layout.preferredHeight: 88
        radius: 8
        color: root.fieldBg

        Column {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 14
                rightMargin: 14
            }
            spacing: 6

            Text {
                width: parent.width
                text: statTile.value
                color: statTile.accentColor
                font.pixelSize: 26
                font.weight: Font.ExtraBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: statTile.label
                color: root.textMuted
                font.pixelSize: 13
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }

    component SettingsRow: Item {
        id: settingsRow

        required property string label
        property string value: ""
        property string iconText: ""
        property color iconColor: root.blue
        property color iconBackground: Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.12)

        Layout.fillWidth: true
        Layout.preferredHeight: 62
        opacity: enabled ? 1 : 0.55

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: root.line
            opacity: 0.7
        }

        Rectangle {
            id: rowIcon

            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            width: 36
            height: 36
            radius: 8
            color: settingsRow.iconBackground

            Text {
                anchors.centerIn: parent
                text: settingsRow.iconText
                color: settingsRow.iconColor
                font.pixelSize: settingsRow.iconText.length > 1 ? 11 : 16
                font.weight: Font.ExtraBold
            }
        }

        Column {
            anchors {
                left: rowIcon.right
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 12
            }
            spacing: 3

            Text {
                width: parent.width
                text: settingsRow.label
                color: root.textMain
                font.pixelSize: 15
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                visible: settingsRow.value.length > 0
                width: parent.width
                text: settingsRow.value
                color: root.textMuted
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }
    }

    component ActionRow: Item {
        id: actionRow

        required property string label
        property string value: ""
        property string iconText: ""
        property string buttonText: ""
        property color buttonColor: root.blue
        property color iconColor: root.blue
        property color iconBackground: Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.12)

        signal triggered()

        Layout.fillWidth: true
        Layout.preferredHeight: 68

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: root.line
            opacity: 0.7
        }

        Rectangle {
            id: actionIcon

            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            width: 36
            height: 36
            radius: 8
            color: actionRow.iconBackground

            Text {
                anchors.centerIn: parent
                text: actionRow.iconText
                color: actionRow.iconColor
                font.pixelSize: actionRow.iconText.length > 1 ? 11 : 17
                font.weight: Font.ExtraBold
            }
        }

        Column {
            anchors {
                left: actionIcon.right
                right: actionButton.left
                verticalCenter: parent.verticalCenter
                leftMargin: 12
                rightMargin: 10
            }
            spacing: 3

            Text {
                width: parent.width
                text: actionRow.label
                color: root.textMain
                font.pixelSize: 15
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                visible: actionRow.value.length > 0
                width: parent.width
                text: actionRow.value
                color: root.textMuted
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }

        Button {
            id: actionButton

            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            width: 86
            height: 38
            text: actionRow.buttonText
            font.pixelSize: 13
            font.weight: Font.Bold

            background: Rectangle {
                radius: 8
                color: actionRow.buttonColor
            }

            contentItem: Text {
                text: actionButton.text
                color: "white"
                font: actionButton.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }

            onClicked: actionRow.triggered()
        }
    }
}
