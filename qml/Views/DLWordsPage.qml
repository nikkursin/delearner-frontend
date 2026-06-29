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

    property var wordsModel: []
    property var groupOptions: [{ id: -1, name: "All" }]
    property int selectedGroupId: -1
    property string selectedGroupName: "All"
    property string searchQuery: ""
    property string sortMode: "newest"
    property int totalWordCount: 0
    property int pendingDeleteWordId: -1
    property string pendingDeleteWord: ""
    property string errorMessage: ""

    readonly property color fieldBg: "#f1f4f9"
    readonly property color green: "#35a969"
    readonly property color greenSoft: Qt.rgba(53 / 255, 169 / 255, 105 / 255, 0.12)
    readonly property color red: "#dc3545"
    readonly property color redSoft: Qt.rgba(220 / 255, 53 / 255, 69 / 255, 0.10)
    readonly property color pink: "#e94f72"
    readonly property color orange: "#e27a34"
    readonly property url sortWordsIcon: Qt.resolvedUrl("../../assets/icons/sort_words_icon.svg")

    readonly property var sortOptions: [
        { label: "Newest", value: "newest" },
        { label: "Oldest", value: "oldest" },
        { label: "A-Z", value: "az" },
        { label: "Z-A", value: "za" }
    ]

    Component.onCompleted: reloadAll()

    Connections {
        target: appStateManager

        function onWordsChanged() {
            root.reloadAll()
        }
    }

    function reloadAll() {
        loadGroups()
        reloadWords()
    }

    function loadGroups() {
        var groups = appStateManager.availableGroups()
        var options = [{ id: -1, name: "All" }]

        for (var i = 0; i < groups.length; ++i) {
            options.push({
                id: groups[i].id,
                name: groups[i].name
            })
        }

        groupOptions = options

        if (!selectGroupById(selectedGroupId, false)) {
            selectedGroupId = -1
            selectedGroupName = "All"
        }
    }

    function reloadWords() {
        var query = searchQuery.trim()
        totalWordCount = appStateManager.wordCount(selectedGroupId)

        if (query.length > 0) {
            wordsModel = appStateManager.searchWords(query, sortMode, selectedGroupId)
        } else {
            wordsModel = appStateManager.loadWords(sortMode, selectedGroupId)
        }

        errorMessage = appStateManager.lastError || ""
    }

    function selectGroupById(groupId, reload) {
        for (var i = 0; i < groupOptions.length; ++i) {
            if (groupOptions[i].id === groupId) {
                selectedGroupId = groupId
                selectedGroupName = groupOptions[i].name
                if (reload) {
                    reloadWords()
                }
                return true
            }
        }

        return false
    }

    function groupNameFor(groupId) {
        if (groupId === undefined || groupId === null || groupId < 0) {
            return ""
        }

        for (var i = 0; i < groupOptions.length; ++i) {
            if (groupOptions[i].id === groupId) {
                return groupOptions[i].name
            }
        }

        return ""
    }

    function articleColor(article) {
        if (article === "der") {
            return root.blue
        }

        if (article === "die") {
            return root.pink
        }

        if (article === "das") {
            return root.green
        }

        return "#d1d5db"
    }

    function articleTextColor(article) {
        return article === "der" || article === "die" || article === "das" ? "white" : "#4b5563"
    }

    function articleLabel(article) {
        return article && article.length > 0 ? article : "-"
    }

    function scoreLabel(word) {
        var correct = word.correct_answers || 0
        var wrong = word.wrong_answers || 0
        var total = correct + wrong

        if (total <= 0) {
            return "New"
        }

        return Math.round((correct / total) * 100) + "%"
    }

    function hasActiveQuery() {
        return searchQuery.trim().length > 0
    }

    function sectionLabel() {
        if (hasActiveQuery()) {
            return "Search results"
        }

        return sortMode === "newest" ? "Recent words" : "All words"
    }

    function countSubtitle() {
        var noun = totalWordCount === 1 ? "word" : "words"
        if (selectedGroupId >= 0) {
            return totalWordCount + " " + noun + " in " + selectedGroupName
        }

        return totalWordCount + " saved " + noun
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 90

        Column {
            anchors {
                left: parent.left
                right: actionRow.left
                verticalCenter: parent.verticalCenter
                rightMargin: 12
            }
            spacing: 5

            Text {
                width: parent.width
                text: "Words"
                color: root.textMain
                font.pixelSize: 34
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: root.countSubtitle()
                color: root.textMuted
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }

        RowLayout {
            id: actionRow
            anchors {
                right: parent.right
                top: parent.top
                topMargin: 24
            }
            spacing: 10

            ComboBox {
                id: sortCombo

                Layout.preferredWidth: 44
                Layout.preferredHeight: 40
                model: root.sortOptions
                textRole: "label"
                font.pixelSize: 13

                onActivated: function(index) {
                    root.sortMode = root.sortOptions[index].value
                    root.reloadWords()
                }

                background: Rectangle {
                    radius: 14
                    color: root.cardBg
                    border.color: root.line
                    border.width: 1
                }

                indicator: null

                delegate: ItemDelegate {
                    id: sortOptionDelegate

                    required property int index
                    required property var modelData

                    width: sortCombo.popup.width
                    height: 42
                    highlighted: sortCombo.highlightedIndex === sortOptionDelegate.index

                    contentItem: Text {
                        text: sortOptionDelegate.modelData.label
                        color: sortOptionDelegate.highlighted ? root.blue : root.textMain
                        font.pixelSize: 14
                        font.weight: Font.Bold
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        color: sortOptionDelegate.highlighted ? Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.10) : root.cardBg
                    }
                }

                contentItem: Item {
                    implicitWidth: 44
                    implicitHeight: 40

                    IconImage {
                        anchors.centerIn: parent
                        source: root.sortWordsIcon
                        width: 22
                        height: 22
                        color: root.blue
                    }
                }

                popup: Popup {
                    y: sortCombo.height + 6
                    x: sortCombo.width - width
                    width: 132
                    implicitHeight: contentItem.implicitHeight
                    padding: 0

                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: sortCombo.popup.visible ? sortCombo.delegateModel : null
                        currentIndex: sortCombo.highlightedIndex
                    }

                    background: Rectangle {
                        radius: 14
                        color: root.cardBg
                        border.color: root.line
                        border.width: 1
                    }
                }
            }
        }
    }

    Text {
        visible: root.errorMessage.length > 0
        Layout.fillWidth: true
        Layout.bottomMargin: 12
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

    TextField {
        id: searchField

        Layout.fillWidth: true
        Layout.preferredHeight: 50
        Layout.bottomMargin: 12
        placeholderText: "Search German or translation"
        text: root.searchQuery
        font.pixelSize: 15
        color: root.textMain
        selectByMouse: true
        leftPadding: 16
        rightPadding: clearSearchButton.visible ? 42 : 16

        onTextEdited: {
            root.searchQuery = text
            root.reloadWords()
        }

        background: Rectangle {
            radius: 18
            color: root.cardBg
            border.color: root.line
            border.width: 1
        }

        Button {
            id: clearSearchButton

            visible: searchField.text.length > 0
            anchors {
                right: parent.right
                rightMargin: 8
                verticalCenter: parent.verticalCenter
            }
            width: 32
            height: 32
            text: "x"
            font.pixelSize: 14
            font.weight: Font.Bold
            onClicked: {
                searchField.text = ""
                root.searchQuery = ""
                root.reloadWords()
            }

            contentItem: Text {
                text: clearSearchButton.text
                color: root.textMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: clearSearchButton.font
            }

            background: Rectangle {
                radius: 16
                color: root.fieldBg
            }
        }
    }

    Flickable {
        Layout.fillWidth: true
        Layout.preferredHeight: 42
        Layout.bottomMargin: 12
        clip: true
        contentWidth: groupRow.implicitWidth
        contentHeight: height
        interactive: contentWidth > width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        Row {
            id: groupRow

            height: parent.height
            spacing: 8

            Repeater {
                model: root.groupOptions

                ChipButton {
                    id: groupDelegate

                    required property var modelData

                    text: groupDelegate.modelData.name
                    selected: root.selectedGroupId === groupDelegate.modelData.id
                    onClicked: root.selectGroupById(groupDelegate.modelData.id, true)
                }
            }
        }
    }

    Text {
        visible: root.wordsModel.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.bottomMargin: 9
        text: root.sectionLabel()
        color: root.textMuted
        font.pixelSize: 12
        font.weight: Font.ExtraBold
        font.capitalization: Font.AllUppercase
    }

    ColumnLayout {
        visible: root.wordsModel.length > 0
        Layout.fillWidth: true
        spacing: 10

        Repeater {
            model: root.wordsModel

            WordCard {
                id: wordDelegate

                required property var modelData

                Layout.fillWidth: true
                word: wordDelegate.modelData
            }
        }
    }

    EmptyState {
        visible: root.wordsModel.length === 0
        Layout.fillWidth: true
        Layout.topMargin: 42
        titleText: root.totalWordCount === 0 && !root.hasActiveQuery() ? "No words yet" : "No matches found"
        bodyText: root.totalWordCount === 0 && !root.hasActiveQuery()
                  ? "Add your first German word to start building your vocabulary."
                  : "Try another search term or clear the current filters."
        primaryText: root.totalWordCount === 0 && !root.hasActiveQuery() ? "Add Word" : "Clear Search"
        onPrimaryClicked: {
            if (root.totalWordCount === 0 && !root.hasActiveQuery()) {
                appStateManager.goAddEditWordPage()
            } else {
                searchField.text = ""
                root.searchQuery = ""
                root.selectGroupById(-1, false)
                root.reloadWords()
            }
        }
    }

    component WordCard: Rectangle {
        id: card

        property var word: ({})

        Layout.preferredHeight: 116
        radius: 18
        color: root.cardBg
        border.color: root.line
        border.width: 1

        MouseArea {
            anchors.fill: parent
            onClicked: appStateManager.openWordDetails(card.word.id)
        }

        ColumnLayout {
            anchors {
                fill: parent
                margins: 14
            }
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 24
                    radius: 7
                    color: root.articleColor(card.word.article || "")

                    Text {
                        anchors.centerIn: parent
                        text: root.articleLabel(card.word.article || "")
                        color: root.articleTextColor(card.word.article || "")
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3

                    Text {
                        Layout.fillWidth: true
                        text: card.word.german_word || ""
                        color: root.textMain
                        font.pixelSize: 18
                        font.weight: Font.ExtraBold
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: card.word.native_translation || ""
                        color: root.textMuted
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }

                MetaPill {
                    text: root.scoreLabel(card.word)
                    colorOverride: text === "New" ? root.textMuted : root.blue
                    bgOverride: text === "New" ? root.fieldBg : Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.12)
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 7

                MetaPill {
                    visible: (card.word.part_of_speech || "").length > 0
                    text: card.word.part_of_speech || ""
                }

                MetaPill {
                    visible: root.groupNameFor(card.word.group_id).length > 0
                    text: root.groupNameFor(card.word.group_id)
                }
            }
        }
    }

    component MetaPill: Rectangle {
        property alias text: label.text
        property color colorOverride: root.textMuted
        property color bgOverride: root.fieldBg

        implicitWidth: label.implicitWidth + 16
        implicitHeight: 28
        radius: 14
        color: bgOverride

        Text {
            id: label

            anchors.centerIn: parent
            color: parent.colorOverride
            font.pixelSize: 12
            font.weight: Font.Bold
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    component ChipButton: Button {
        id: chip

        property bool selected: false

        width: Math.max(64, contentItem.implicitWidth + 24)
        height: 38
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
            maximumLineCount: 1
        }

        background: Rectangle {
            radius: 19
            color: chip.selected ? root.blue : root.cardBg
            border.color: chip.selected ? root.blue : root.line
            border.width: 1
        }
    }

    component EmptyState: ColumnLayout {
        signal primaryClicked()

        property string titleText: ""
        property string bodyText: ""
        property string primaryText: ""

        spacing: 14

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 76
            Layout.preferredHeight: 76
            radius: 26
            color: Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.12)

            Text {
                anchors.centerIn: parent
                text: "+"
                color: root.blue
                font.pixelSize: 34
                font.weight: Font.ExtraBold
            }
        }

        Text {
            Layout.fillWidth: true
            text: parent.titleText
            color: root.textMain
            font.pixelSize: 22
            font.weight: Font.ExtraBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            text: parent.bodyText
            color: root.textMuted
            font.pixelSize: 14
            lineHeight: 1.25
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Button {
            id: emptyActionButton

            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 164
            Layout.preferredHeight: 48
            text: parent.primaryText
            onClicked: parent.primaryClicked()

            contentItem: Text {
                text: emptyActionButton.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 15
                font.weight: Font.ExtraBold
            }

            background: Rectangle {
                radius: 16
                color: root.blue
            }
        }
    }
}
