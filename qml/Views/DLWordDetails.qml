pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "words"

    property var word: ({})
    property var groupOptions: []
    property string errorMessage: ""
    property string pendingDeleteWordId: ""
    property string pendingDeleteWord: ""

    readonly property color fieldBg: "#f1f4f9"
    readonly property color blueSoft: Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.12)
    readonly property color green: "#35a969"
    readonly property color red: "#dc3545"
    readonly property color redSoft: Qt.rgba(220 / 255, 53 / 255, 69 / 255, 0.10)
    readonly property color pink: "#e94f72"

    readonly property bool hasWord: !!word && (word.id || "").length > 0
    readonly property bool hasExamples: fieldText("example_phrase_de").length > 0
                                || fieldText("example_phrase_native").length > 0

    Component.onCompleted: reload()

    Connections {
        target: appStateManager

        function onWordsChanged() {
            root.reload()
        }

        function onGroupsChanged() {
            root.loadGroups()
        }
    }

    function reload() {
        loadGroups()
        loadWord()
    }

    function loadGroups() {
        groupOptions = appStateManager.availableGroups()
    }

    function loadWord() {
        var id = (appStateManager.selectedWordId || "").trim()
        if (id.length === 0) {
            word = ({})
            errorMessage = "Unable to load this word."
            return
        }

        var loadedWord = appStateManager.wordById(id)
        if (!loadedWord || !loadedWord.id) {
            word = ({})
            errorMessage = appStateManager.lastError || "Unable to load this word."
            return
        }

        word = loadedWord
        errorMessage = ""
    }

    function goBack() {
        appStateManager.goWordsPage()
    }

    function editWord() {
        var id = (word.id || appStateManager.selectedWordId || "").trim()
        if (id.length > 0) {
            appStateManager.openEditWord(id)
        }
    }

    function confirmDelete() {
        pendingDeleteWordId = word.id || ""
        pendingDeleteWord = fieldText("german_word")
        deleteWordDialog.open()
    }

    function deletePendingWord() {
        if (!appStateManager.deleteWord(pendingDeleteWordId)) {
            errorMessage = appStateManager.lastError || "Unable to delete this word."
            return
        }

        pendingDeleteWordId = ""
        pendingDeleteWord = ""
        appStateManager.goWordsPage()
    }

    function fieldText(key) {
        var value = word && word[key] !== undefined && word[key] !== null ? word[key] : ""
        return ("" + value).trim()
    }

    function displayValue(value) {
        return (value || "").toString().trim()
    }

    function partOfSpeechLabel() {
        var value = fieldText("part_of_speech")
        return value.length > 0 ? value : "Andere"
    }

    function groupForWord() {
        var groupId = word.group_id || ""
        if (groupId.length === 0) {
            return ({})
        }

        for (var i = 0; i < groupOptions.length; ++i) {
            if (groupOptions[i].id === groupId) {
                return groupOptions[i]
            }
        }

        return ({})
    }

    function groupName() {
        var group = groupForWord()
        return displayValue(group.name)
    }

    function groupColor() {
        var group = groupForWord()
        var colorValue = displayValue(group.color_hex)
        if (colorValue.length === 4 || colorValue.length === 7) {
            return colorValue.charAt(0) === "#" ? colorValue : root.blue
        }

        return root.blue
    }

    function correctCount() {
        return Number(word.correct_answers || 0)
    }

    function wrongCount() {
        return Number(word.wrong_answers || 0)
    }

    function successRate() {
        var total = correctCount() + wrongCount()
        return total > 0 ? Math.round((correctCount() / total) * 100) + "%" : "New"
    }

    function formattedDate(seconds) {
        var timestamp = Number(seconds || 0)
        if (timestamp <= 0) {
            return "Never"
        }

        var date = new Date(timestamp * 1000)
        var today = new Date()
        if (date.toDateString() === today.toDateString()) {
            return "Today"
        }

        return Qt.formatDateTime(date, "MMM d, yyyy")
    }

    function infoRows() {
        var rows = []
        var article = fieldText("article")
        var partOfSpeech = partOfSpeechLabel()
        var group = groupName()

        if (article.length > 0) {
            rows.push({ label: "Article", value: article })
        }

        rows.push({ label: "Part of speech", value: partOfSpeech })

        if (group.length > 0) {
            rows.push({ label: "Group", value: group })
        }

        if (partOfSpeech === "Nomen" || partOfSpeech === "Substantiv") {
            addOptionalInfoRow(rows, "Plural form", fieldText("plural_form") || fieldText("pluralForm"))
        } else if (partOfSpeech === "Verb") {
            addOptionalInfoRow(rows, "Präteritum", fieldText("praeteritum_form") || fieldText("praeteritumForm"))
            addOptionalInfoRow(rows, "Partizip II", fieldText("partizip_ii_form") || fieldText("partizipIIForm"))
        } else if (partOfSpeech === "Adjektiv" || partOfSpeech === "Adj") {
            addOptionalInfoRow(rows, "Positive", fieldText("positive_form") || fieldText("positiveForm"))
            addOptionalInfoRow(rows, "Comparative", fieldText("comparative_form") || fieldText("comparativeForm"))
            addOptionalInfoRow(rows, "Superlative", fieldText("superlative_form") || fieldText("superlativeForm"))
        }

        rows.push({ label: "Last reviewed", value: formattedDate(word.last_reviewed_at) })
        return rows
    }

    function addOptionalInfoRow(rows, label, value) {
        var text = displayValue(value)
        if (text.length > 0) {
            rows.push({ label: label, value: text })
        }
    }

    Dialog {
        id: deleteWordDialog

        title: "Delete word?"
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok

        Label {
            width: Math.min(root.width - 72, 360)
            text: "This permanently deletes \"" + root.pendingDeleteWord + "\" from your saved words."
            wrapMode: Text.WordWrap
        }

        onAccepted: root.deletePendingWord()
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 52
        Layout.bottomMargin: 18

        Button {
            id: backButton

            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            width: 40
            height: 40
            text: ""
            onClicked: root.goBack()

            contentItem: Item {
                IconImage {
                    anchors.centerIn: parent
                    source: Qt.resolvedUrl("../../assets/icons/back_arrow_icon.svg")
                    width: 16
                    height: 26
                    color: root.blue
                }
            }

            background: Rectangle {
                radius: 14
                color: root.cardBg
                border.color: root.line
                border.width: 1
            }
        }

        Text {
            anchors.centerIn: parent
            text: "Word Details"
            color: root.textMain
            font.pixelSize: 17
            font.weight: Font.ExtraBold
        }

        Button {
            id: editTopButton

            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            width: 66
            height: 40
            text: "Edit"
            enabled: root.hasWord
            font.pixelSize: 15
            font.weight: Font.Bold
            onClicked: root.editWord()

            contentItem: Text {
                text: editTopButton.text
                color: root.blue
                opacity: editTopButton.enabled ? 1.0 : 0.5
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: editTopButton.font
            }

            background: Rectangle {
                radius: 14
                color: root.blueSoft
            }
        }
    }

    Text {
        visible: root.errorMessage.length > 0
        Layout.fillWidth: true
        Layout.bottomMargin: 14
        text: root.errorMessage
        color: root.red
        wrapMode: Text.WordWrap
        font.pixelSize: 14
        font.weight: Font.DemiBold

        Rectangle {
            anchors {
                fill: parent
                margins: -10
            }
            z: -1
            radius: 14
            color: root.redSoft
        }
    }

    Rectangle {
        visible: root.hasWord
        Layout.fillWidth: true
        Layout.bottomMargin: 18
        implicitHeight: heroContent.implicitHeight + 48
        radius: 24
        border.color: Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.10)
        border.width: 1
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.14) }
            GradientStop { position: 1.0; color: Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.06) }
        }

        ColumnLayout {
            id: heroContent

            anchors {
                fill: parent
                margins: 20
            }
            spacing: 10

            Rectangle {
                visible: root.fieldText("article").length > 0
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Math.max(48, articleLabel.implicitWidth + 24)
                Layout.preferredHeight: 28
                radius: 14
                color: root.blue

                Text {
                    id: articleLabel

                    anchors.centerIn: parent
                    text: root.fieldText("article")
                    color: "white"
                    font.pixelSize: 14
                    font.weight: Font.ExtraBold
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.fieldText("german_word")
                color: root.textMain
                font.pixelSize: 34
                font.weight: Font.ExtraBold
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: root.fieldText("native_translation")
                color: root.textMuted
                font.pixelSize: 21
                font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Flow {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8
                layoutDirection: Qt.LeftToRight

                MetaPill {
                    text: root.partOfSpeechLabel()
                }

                MetaPill {
                    visible: root.groupName().length > 0
                    text: root.groupName()
                    dotColor: root.groupColor()
                    showDot: true
                }
            }
        }
    }

    SectionTitle {
        visible: root.hasWord
        text: "Statistics"
    }

    Rectangle {
        visible: root.hasWord
        Layout.fillWidth: true
        Layout.bottomMargin: 18
        implicitHeight: 82
        radius: 18
        color: root.cardBg
        border.color: root.line
        border.width: 1

        RowLayout {
            anchors {
                fill: parent
                topMargin: 16
                bottomMargin: 16
            }
            spacing: 0

            StatBlock {
                value: root.correctCount()
                label: "Correct"
                valueColor: root.green
                showDivider: true
            }

            StatBlock {
                value: root.wrongCount()
                label: "Wrong"
                valueColor: root.red
                showDivider: true
            }

            StatBlock {
                value: root.successRate()
                label: "Success"
                valueColor: root.blue
            }
        }
    }

    SectionTitle {
        visible: root.hasWord && root.hasExamples
        text: "Examples"
    }

    Rectangle {
        visible: root.hasWord && root.hasExamples
        Layout.fillWidth: true
        Layout.bottomMargin: 18
        implicitHeight: examplesColumn.implicitHeight + 32
        radius: 18
        color: root.cardBg
        border.color: root.line
        border.width: 1

        ColumnLayout {
            id: examplesColumn

            anchors {
                fill: parent
                margins: 16
            }
            spacing: 14

            ExampleBlock {
                visible: root.fieldText("example_phrase_de").length > 0
                label: "German"
                text: root.fieldText("example_phrase_de")
            }

            ExampleBlock {
                visible: root.fieldText("example_phrase_native").length > 0
                label: "Native translation"
                text: root.fieldText("example_phrase_native")
            }
        }
    }

    SectionTitle {
        visible: root.hasWord
        text: "Word Info"
    }

    Rectangle {
        visible: root.hasWord
        Layout.fillWidth: true
        Layout.bottomMargin: 22
        implicitHeight: infoColumn.implicitHeight
        radius: 18
        color: root.cardBg
        border.color: root.line
        border.width: 1
        clip: true

        ColumnLayout {
            id: infoColumn

            width: parent.width
            spacing: 0

            Repeater {
                model: root.infoRows()

                InfoRow {
                    id: infoDelegate

                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    label: infoDelegate.modelData.label
                    value: infoDelegate.modelData.value
                    showDivider: infoDelegate.index < root.infoRows().length - 1
                }
            }
        }
    }

    ColumnLayout {
        visible: root.hasWord
        Layout.fillWidth: true
        Layout.bottomMargin: 16
        spacing: 10

        Button {
            id: editActionButton

            Layout.fillWidth: true
            Layout.preferredHeight: 52
            text: "Edit Word"
            font.pixelSize: 17
            font.weight: Font.ExtraBold
            onClicked: root.editWord()

            contentItem: Text {
                text: editActionButton.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: editActionButton.font
            }

            background: Rectangle {
                radius: 16
                color: root.blue
            }
        }

        Button {
            id: deleteActionButton

            Layout.fillWidth: true
            Layout.preferredHeight: 52
            text: "Delete Word"
            font.pixelSize: 17
            font.weight: Font.ExtraBold
            onClicked: root.confirmDelete()

            contentItem: Text {
                text: deleteActionButton.text
                color: root.red
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: deleteActionButton.font
            }

            background: Rectangle {
                radius: 16
                color: root.redSoft
            }
        }
    }

    component SectionTitle: Text {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.bottomMargin: 9
        color: root.textMuted
        font.pixelSize: 13
        font.weight: Font.ExtraBold
        font.capitalization: Font.AllUppercase
    }

    component MetaPill: Rectangle {
        property alias text: label.text
        property bool showDot: false
        property color dotColor: root.blue

        implicitWidth: label.implicitWidth + (showDot ? 31 : 22)
        implicitHeight: 28
        radius: 14
        color: Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.78)

        Row {
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                visible: parent.parent.showDot
                width: 8
                height: 8
                radius: 4
                color: parent.parent.dotColor
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                id: label

                color: root.textMuted
                font.pixelSize: 13
                font.weight: Font.Bold
                maximumLineCount: 1
                elide: Text.ElideRight
            }
        }
    }

    component StatBlock: Item {
        property string value: ""
        property string label: ""
        property color valueColor: root.blue
        property bool showDivider: false

        Layout.fillWidth: true
        Layout.fillHeight: true

        Column {
            anchors.centerIn: parent
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: parent.parent.value
                color: parent.parent.valueColor
                font.pixelSize: 22
                font.weight: Font.ExtraBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: parent.parent.label
                color: root.textMuted
                font.pixelSize: 12
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }

        Rectangle {
            visible: parent.showDivider
            anchors {
                right: parent.right
                top: parent.top
                bottom: parent.bottom
            }
            width: 1
            color: root.line
        }
    }

    component ExampleBlock: Rectangle {
        property string label: ""
        property string text: ""

        Layout.fillWidth: true
        implicitHeight: exampleContent.implicitHeight + 28
        radius: 14
        color: root.fieldBg

        ColumnLayout {
            id: exampleContent

            anchors {
                fill: parent
                margins: 14
            }
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: parent.parent.label
                color: root.textMuted
                font.pixelSize: 12
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: parent.parent.text
                color: root.textMain
                font.pixelSize: 16
                font.weight: Font.DemiBold
                lineHeight: 1.18
                wrapMode: Text.WordWrap
            }
        }
    }

    component InfoRow: Item {
        property string label: ""
        property string value: ""
        property bool showDivider: true

        implicitHeight: Math.max(48, rowLayout.implicitHeight + 18)

        RowLayout {
            id: rowLayout

            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 16
                rightMargin: 16
            }
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: parent.parent.label
                color: root.textMuted
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                Layout.maximumWidth: parent.width * 0.55
                text: parent.parent.value
                color: root.textMain
                font.pixelSize: 15
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.WordWrap
            }
        }

        Rectangle {
            visible: parent.showDivider
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: root.line
        }
    }

}
