pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"

DLAppPage {
    id: root

    property bool editMode: false
    property string wordId: ""

    property var groupOptions: [{ id: "", name: "No group" }]
    property string selectedGroupId: ""
    property string selectedArticle: ""
    property string selectedPartOfSpeech: "Nomen"
    property string errorMessage: ""
    property string suggestionMessage: ""

    title: ""
    showHeader: false
    activeTab: "add"

    readonly property var articleOptions: [
        { label: "der", value: "der" },
        { label: "die", value: "die" },
        { label: "das", value: "das" }
    ]
    readonly property var partOfSpeechOptions: [
        "Nomen",
        "Verb",
        "Adjektiv",
        "Adverb",
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
        var options = [{ id: "", name: "No group" }]

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
        var id = wordId.trim()
        if (id.length === 0) {
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
        selectedPartOfSpeech = normalizedPartOfSpeech(word.part_of_speech || "Nomen")
        selectedGroupId = word.group_id === undefined || word.group_id === null ? "" : word.group_id
        pluralFormField.text = word.plural_form || word.pluralForm || ""
        praeteritumFormField.text = word.praeteritum_form || word.praeteritumForm || ""
        partizipIIFormField.text = word.partizip_ii_form || word.partizipIIForm || ""
        positiveFormField.text = word.positive_form || word.positiveForm || ""
        comparativeFormField.text = word.comparative_form || word.comparativeForm || ""
        superlativeFormField.text = word.superlative_form || word.superlativeForm || ""
        germanExampleField.text = word.example_phrase_de || ""
        nativeExampleField.text = word.example_phrase_native || ""

        selectGroup(selectedGroupId)
        updateSuggestion()
    }

    function normalizedPartOfSpeech(value) {
        var trimmedValue = (value || "").trim()

        if (trimmedValue === "Adj") {
            return "Adjektiv"
        }

        if (trimmedValue === "Adv") {
            return "Adverb"
        }

        if (trimmedValue === "Substantiv") {
            return "Nomen"
        }

        if (partOfSpeechOptions.indexOf(trimmedValue) >= 0) {
            return trimmedValue
        }

        return "Andere"
    }

    function articleFromGermanWord(value) {
        var match = (value || "").trim().match(/^(der|die|das)\s+(.+)$/i)
        if (!match) {
            return null
        }

        return {
            article: match[1].toLowerCase(),
            word: match[2].trim()
        }
    }

    function applyArticleDetection() {
        var parsed = articleFromGermanWord(germanWordField.text)
        if (!parsed || parsed.word.length === 0) {
            return
        }

        selectedArticle = parsed.article
        selectedPartOfSpeech = "Nomen"
        germanWordField.text = parsed.word
        germanWordField.cursorPosition = germanWordField.text.length
        updateSuggestion()
    }

    function updateSuggestion() {
        suggestionMessage = selectedPartOfSpeech === "Nomen" && selectedArticle.length === 0
            ? "Article is strongly suggested for nouns."
            : ""
    }

    function selectGroup(groupId) {
        selectedGroupId = groupId === undefined || groupId === null ? "" : groupId

        for (var i = 0; i < groupOptions.length; ++i) {
            if (groupOptions[i].id === selectedGroupId) {
                groupCombo.currentIndex = i
                return
            }
        }

        selectedGroupId = ""
        groupCombo.currentIndex = 0
    }

    function validateForm() {
        applyArticleDetection()

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
        updateSuggestion()
        return true
    }

    function wordPayload() {
        return {
            german_word: germanWordField.text.trim(),
            native_translation: translationField.text.trim(),
            article: selectedPartOfSpeech === "Nomen" ? selectedArticle : "",
            part_of_speech: selectedPartOfSpeech,
            group_id: selectedGroupId.length > 0 ? selectedGroupId : null,
            plural_form: selectedPartOfSpeech === "Nomen" ? pluralFormField.text.trim() : "",
            praeteritum_form: selectedPartOfSpeech === "Verb" ? praeteritumFormField.text.trim() : "",
            partizip_ii_form: selectedPartOfSpeech === "Verb" ? partizipIIFormField.text.trim() : "",
            positive_form: selectedPartOfSpeech === "Adjektiv" ? positiveFormField.text.trim() : "",
            comparative_form: selectedPartOfSpeech === "Adjektiv" ? comparativeFormField.text.trim() : "",
            superlative_form: selectedPartOfSpeech === "Adjektiv" ? superlativeFormField.text.trim() : "",
            example_phrase_de: germanExampleField.text.trim(),
            example_phrase_native: nativeExampleField.text.trim()
        }
    }

    function saveWord() {
        console.log("Trying to save word...")
        if (!validateForm()) {
            return
        }

        if (editMode) {
            var existingId = wordId.trim()
            if (existingId.length === 0) {
                errorMessage = "Unable to save this word."
                return
            }

            if (!appStateManager.updateWord(existingId, wordPayload())) {
                errorMessage = appStateManager.lastError || "Unable to save this word."
                return
            }
        } else {
            var newId = appStateManager.createWord(wordPayload())
            if (!newId || newId.length === 0) {
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
            font.pixelSize: 25
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
        spacing: 16

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
                onTextEdited: root.applyArticleDetection()
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

            SegmentedSelector {
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                model: root.partOfSpeechOptions
                selectedValue: root.selectedPartOfSpeech
                onSelected: function(value) {
                    root.selectedPartOfSpeech = value
                    root.updateSuggestion()
                }
            }
        }

        FieldBlock {
            label: "Article"
            visible: root.selectedPartOfSpeech === "Nomen"
            Layout.fillWidth: true

            SegmentedSelector {
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                model: root.articleOptions
                selectedValue: root.selectedArticle
                onSelected: function(value) {
                    root.selectedArticle = root.selectedArticle === value ? "" : value
                    root.updateSuggestion()
                }
            }
        }

        Text {
            visible: root.suggestionMessage.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 4
            text: root.suggestionMessage
            color: root.textMuted
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        FieldBlock {
            label: "Plural form"
            visible: root.selectedPartOfSpeech === "Nomen"
            Layout.fillWidth: true

            TextField {
                id: pluralFormField
                Layout.fillWidth: true
                placeholderText: "Tische"
                font.pixelSize: 15
                color: root.textMain
                selectByMouse: true
                background: FieldBackground {}
            }
        }

        FieldBlock {
            label: "Präteritum"
            visible: root.selectedPartOfSpeech === "Verb"
            Layout.fillWidth: true

            TextField {
                id: praeteritumFormField
                Layout.fillWidth: true
                placeholderText: "ging"
                font.pixelSize: 15
                color: root.textMain
                selectByMouse: true
                background: FieldBackground {}
            }
        }

        FieldBlock {
            label: "Partizip II"
            visible: root.selectedPartOfSpeech === "Verb"
            Layout.fillWidth: true

            TextField {
                id: partizipIIFormField
                Layout.fillWidth: true
                placeholderText: "gegangen"
                font.pixelSize: 15
                color: root.textMain
                selectByMouse: true
                background: FieldBackground {}
            }
        }

        FieldBlock {
            label: "Positive"
            visible: root.selectedPartOfSpeech === "Adjektiv"
            Layout.fillWidth: true

            TextField {
                id: positiveFormField
                Layout.fillWidth: true
                placeholderText: "gut"
                font.pixelSize: 15
                color: root.textMain
                selectByMouse: true
                background: FieldBackground {}
            }
        }

        FieldBlock {
            label: "Comparative"
            visible: root.selectedPartOfSpeech === "Adjektiv"
            Layout.fillWidth: true

            TextField {
                id: comparativeFormField
                Layout.fillWidth: true
                placeholderText: "besser"
                font.pixelSize: 15
                color: root.textMain
                selectByMouse: true
                background: FieldBackground {}
            }
        }

        FieldBlock {
            label: "Superlative"
            visible: root.selectedPartOfSpeech === "Adjektiv"
            Layout.fillWidth: true

            TextField {
                id: superlativeFormField
                Layout.fillWidth: true
                placeholderText: "am besten"
                font.pixelSize: 15
                color: root.textMain
                selectByMouse: true
                background: FieldBackground {}
            }
        }

        FieldBlock {
            label: "Group"

            DLGroupDropdown {
                id: groupCombo

                Layout.fillWidth: true
                Layout.preferredHeight: 52
                model: root.groupOptions
                cardBg: root.cardBg
                fieldBg: root.fieldBg
                textMain: root.textMain
                textMuted: root.textMuted
                line: root.line
                accent: root.blue
                emptyText: "No group"

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
        spacing: 9

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
        implicitHeight: 52
        radius: 16
        color: root.fieldBg
        border.color: root.line
        border.width: 1
    }

    component SegmentedSelector: Rectangle {
        id: selector

        property var model: []
        property var selectedValue: ""

        signal selected(var value)

        implicitHeight: 50
        radius: 16
        color: root.cardBg
        border.color: root.line
        border.width: 1
        clip: true

        function optionLabel(option) {
            if (typeof option === "object" && option !== null && option.label !== undefined) {
                return option.label
            }

            return option
        }

        function optionValue(option) {
            if (typeof option === "object" && option !== null && option.value !== undefined) {
                return option.value
            }

            return option
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Repeater {
                model: selector.model

                delegate: Item {
                    id: segment

                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    readonly property var value: selector.optionValue(segment.modelData)
                    readonly property bool checked: selector.selectedValue === segment.value

                    Rectangle {
                        anchors {
                            fill: parent
                            margins: 0
                        }
                        radius: selector.radius
                        color: segment.checked ? root.blue : "transparent"
                    }

                    Rectangle {
                        visible: segment.index > 0 && !segment.checked
                        anchors {
                            left: parent.left
                            verticalCenter: parent.verticalCenter
                        }
                        width: 1
                        height: parent.height - 18
                        color: root.line
                    }

                    Text {
                        anchors {
                            fill: parent
                            leftMargin: 4
                            rightMargin: 4
                        }
                        text: selector.optionLabel(segment.modelData)
                        color: segment.checked ? "white" : root.textMain
                        font.pixelSize: 13
                        font.weight: segment.checked ? Font.ExtraBold : Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: selector.selected(segment.value)
                    }
                }
            }
        }
    }
}
