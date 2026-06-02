pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DLAppPage {
    id: root

    property bool editMode: false
    property string wordId: ""

    property var groupOptions: [{ id: -1, name: "No group" }]
    property int selectedGroupId: -1
    property string selectedArticle: ""
    property string selectedPartOfSpeech: "Nomen"
    property string errorMessage: ""

    title: ""
    showHeader: false
    activeTab: "add"

    readonly property var articleOptions: [
        { label: "None", value: "" },
        { label: "der", value: "der" },
        { label: "die", value: "die" },
        { label: "das", value: "das" }
    ]
    readonly property var partOfSpeechOptions: [
        "Nomen",
        "Verb",
        "Adjektiv",
        "Adverb",
        "Phrase",
        "Andere"
    ]

    readonly property color fieldBg: "#f1f4f9"
    readonly property color red: "#dc3545"
    readonly property color redSoft: Qt.rgba(220 / 255, 53 / 255, 69 / 255, 0.10)

    Component.onCompleted: {
        loadGroups()

        if (editMode) {
            loadWord()
        }
    }

    function loadGroups() {
        var groups = appStateManager.availableGroups()
        var options = [{ id: -1, name: "No group" }]

        for (var i = 0; i < groups.length; ++i) {
            options.push({
                id: groups[i].id,
                name: groups[i].name
            })
        }

        groupOptions = options
        selectGroup(selectedGroupId)
    }

    function loadWord() {
        var id = parseInt(wordId)
        if (!id || id <= 0) {
            errorMessage = "Unable to load this word."
            return
        }

        var word = appStateManager.wordById(id)
        if (!word || !word.id) {
            errorMessage = appStateManager.lastError || "Unable to load this word."
            return
        }

        germanWordField.text = word.german_word || ""
        translationField.text = word.native_translation || ""
        selectedArticle = word.article || ""
        selectedPartOfSpeech = word.part_of_speech || "Nomen"
        selectedGroupId = word.group_id === undefined || word.group_id === null ? -1 : word.group_id
        germanExampleField.text = word.example_phrase_de || ""
        nativeExampleField.text = word.example_phrase_native || ""

        selectGroup(selectedGroupId)
    }

    function selectGroup(groupId) {
        selectedGroupId = groupId === undefined || groupId === null ? -1 : groupId

        for (var i = 0; i < groupOptions.length; ++i) {
            if (groupOptions[i].id === selectedGroupId) {
                groupCombo.currentIndex = i
                return
            }
        }

        selectedGroupId = -1
        groupCombo.currentIndex = 0
    }

    function validateForm() {
        if (germanWordField.text.trim().length === 0) {
            errorMessage = "German word is required."
            germanWordField.forceActiveFocus()
            return false
        }

        if (translationField.text.trim().length === 0) {
            errorMessage = "Native translation is required."
            translationField.forceActiveFocus()
            return false
        }

        errorMessage = ""
        return true
    }

    function wordPayload() {
        return {
            german_word: germanWordField.text,
            native_translation: translationField.text,
            article: selectedArticle,
            part_of_speech: selectedPartOfSpeech,
            group_id: selectedGroupId >= 0 ? selectedGroupId : null,
            example_phrase_de: germanExampleField.text,
            example_phrase_native: nativeExampleField.text
        }
    }

    function saveWord() {
        if (!validateForm()) {
            return
        }

        if (editMode) {
            var existingId = parseInt(wordId)
            if (!existingId || existingId <= 0) {
                errorMessage = "Unable to save this word."
                return
            }

            if (!appStateManager.updateWord(existingId, wordPayload())) {
                errorMessage = appStateManager.lastError || "Unable to save this word."
                return
            }
        } else {
            var newId = appStateManager.createWord(wordPayload())
            if (newId < 0) {
                errorMessage = appStateManager.lastError || "Unable to save this word."
                return
            }
        }

        appStateManager.goWordsPage()
    }

    function cancel() {
        appStateManager.goWordsPage()
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 48

        Text {
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            text: "Cancel"
            color: root.blue
            font.pixelSize: 15
            font.weight: Font.Bold

            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                onClicked: root.cancel()
            }
        }

        Text {
            anchors.centerIn: parent
            text: root.editMode ? "Edit Word" : "Add Word"
            color: root.textMain
            font.pixelSize: 17
            font.weight: Font.ExtraBold
            elide: Text.ElideRight
        }

        Text {
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            text: "Save"
            color: root.blue
            font.pixelSize: 15
            font.weight: Font.Bold

            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                onClicked: root.saveWord()
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14

        Text {
            visible: root.errorMessage.length > 0
            Layout.fillWidth: true
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

        FieldBlock {
            label: "German word"
            requiredField: true

            TextField {
                id: germanWordField
                Layout.fillWidth: true
                placeholderText: "Tisch"
                font.pixelSize: 15
                color: root.textMain
                selectByMouse: true
                background: FieldBackground {}
                onTextChanged: if (root.errorMessage.length > 0) root.errorMessage = ""
            }
        }

        FieldBlock {
            label: "Native translation"
            requiredField: true

            TextField {
                id: translationField
                Layout.fillWidth: true
                placeholderText: "table"
                font.pixelSize: 15
                color: root.textMain
                selectByMouse: true
                background: FieldBackground {}
                onTextChanged: if (root.errorMessage.length > 0) root.errorMessage = ""
            }
        }

        FieldBlock {
            label: "Part of speech"

            Flow {
                Layout.fillWidth: true
                spacing: 8

                Repeater {
                    model: root.partOfSpeechOptions

                    ChipButton {
                        text: modelData
                        selected: root.selectedPartOfSpeech === modelData
                        onClicked: root.selectedPartOfSpeech = modelData
                    }
                }
            }
        }

        FieldBlock {
            label: "Article"

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Repeater {
                    model: root.articleOptions

                    ChipButton {
                        Layout.fillWidth: true
                        text: modelData.label
                        selected: root.selectedArticle === modelData.value
                        onClicked: root.selectedArticle = modelData.value
                    }
                }
            }
        }

        FieldBlock {
            label: "Group"

            ComboBox {
                id: groupCombo
                Layout.fillWidth: true
                model: root.groupOptions
                textRole: "name"
                font.pixelSize: 15
                background: FieldBackground {}
                contentItem: Text {
                    leftPadding: 14
                    rightPadding: 36
                    verticalAlignment: Text.AlignVCenter
                    text: groupCombo.displayText
                    color: root.textMain
                    font: groupCombo.font
                    elide: Text.ElideRight
                }
                onActivated: function(index) {
                    root.selectedGroupId = root.groupOptions[index].id
                }
            }
        }

        FieldBlock {
            label: "German example"

            TextArea {
                id: germanExampleField
                Layout.fillWidth: true
                Layout.preferredHeight: 88
                placeholderText: "Der Tisch ist gross."
                font.pixelSize: 15
                color: root.textMain
                wrapMode: TextArea.Wrap
                selectByMouse: true
                background: FieldBackground {}
            }
        }

        FieldBlock {
            label: "Native-language example"

            TextArea {
                id: nativeExampleField
                Layout.fillWidth: true
                Layout.preferredHeight: 88
                placeholderText: "The table is big."
                font.pixelSize: 15
                color: root.textMain
                wrapMode: TextArea.Wrap
                selectByMouse: true
                background: FieldBackground {}
            }
        }

        Button {
            id: saveButton

            Layout.fillWidth: true
            Layout.preferredHeight: 52
            text: root.editMode ? "Save Changes" : "Save Word"
            font.pixelSize: 16
            font.weight: Font.ExtraBold
            onClicked: root.saveWord()
            contentItem: Text {
                text: saveButton.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: saveButton.font
            }
            background: Rectangle {
                radius: 17
                color: root.blue
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 14
        }
    }

    component FieldBlock: ColumnLayout {
        property string label: ""
        property bool requiredField: false

        Layout.fillWidth: true
        spacing: 7

        Text {
            Layout.leftMargin: 4
            text: parent.requiredField ? parent.label + " *" : parent.label
            color: root.textMuted
            font.pixelSize: 12
            font.weight: Font.ExtraBold
            font.capitalization: Font.AllUppercase
        }
    }

    component FieldBackground: Rectangle {
        implicitHeight: 48
        radius: 14
        color: root.fieldBg
        border.color: root.line
        border.width: 1
    }

    component ChipButton: Button {
        id: chip

        property bool selected: false

        implicitHeight: 40
        font.pixelSize: 13
        font.weight: Font.Bold
        padding: 0

        contentItem: Text {
            text: chip.text
            color: chip.selected ? "white" : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: chip.font
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 20
            color: chip.selected ? root.blue : root.cardBg
            border.color: chip.selected ? root.blue : root.line
            border.width: 1
        }
    }
}
