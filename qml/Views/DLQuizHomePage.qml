pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.impl

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "quiz"
    pageBackground: "#ffffff"
    pageBottomPadding: 24

    property var quizModes: []
    property string errorMessage: ""

    readonly property color softPanel: "#f5f5f7"
    readonly property color iosBlue: "#007aff"
    readonly property color iosOrange: "#ff9500"
    readonly property color orangeText: "#c96f00"
    readonly property color green: "#35a969"
    readonly property color red: "#ff3b30"
    readonly property color redSoft: Qt.rgba(255 / 255, 59 / 255, 48 / 255, 0.10)

    Component.onCompleted: reloadModes()

    Connections {
        target: appStateManager

        function onWordsChanged() {
            root.reloadModes();
        }
    }

    function reloadModes() {
        quizModes = appStateManager.availableQuizModes();
        errorMessage = appStateManager.lastError || "";
    }

    function openSetup(mode) {
        if (!mode.available) {
            errorMessage = mode.unavailableReason || "Not enough words for this quiz.";
            return;
        }

        appStateManager.goQuizSetupPage(mode.type);
    }

    function modeForType(type) {
        for (var i = 0; i < quizModes.length; ++i) {
            if (quizModes[i].type === type) {
                return quizModes[i];
            }
        }

        return ({
                availableCount: 0
            });
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 90

        Column {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            spacing: 5

            Text {
                width: parent.width
                text: "Quiz"
                color: root.textMain
                font.pixelSize: 34
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: "Choose a practice mode and start a quiz session."
                color: root.textMuted
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 14
        spacing: 14

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 164

            ColumnLayout {
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: 10
                    rightMargin: 10
                }
                spacing: 10

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 82
                    Layout.preferredHeight: 82
                    radius: 26
                    color: Qt.rgba(0 / 255, 122 / 255, 255 / 255, 0.10)

                    IconImage {
                        anchors.centerIn: parent
                        width: 42
                        height: 42
                        source: Qt.resolvedUrl("../../assets/icons/brain_quiz_icon.svg")
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: "Choose Quiz Type"
                    color: root.textMain
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 24
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    text: "Practice vocabulary through translation and German article recognition."
                    color: root.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: 15
                    lineHeight: 1.22
                }
            }
        }

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

        Repeater {
            model: root.quizModes

            delegate: QuizModeCard {
                required property var modelData

                Layout.fillWidth: true
                mode: modelData
                onClicked: root.openSetup(modelData)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            Layout.topMargin: 0
            radius: 16
            color: root.softPanel

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                StatBlock {
                    label: "Words Available"
                    value: String((root.modeForType("translation").availableCount || 0))
                }

                StatBlock {
                    label: "Nouns Available"
                    value: String((root.modeForType("article").availableCount || 0))
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            text: "Translation Quiz requires enough translated words. Article Quiz requires nouns with saved articles."
            color: "#8a8a8e"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            lineHeight: 1.15
        }
    }

    component QuizModeCard: Button {
        id: card

        property var mode: ({})
        readonly property bool article: card.mode.type === "article"
        readonly property color accent: article ? root.iosOrange : root.iosBlue
        readonly property color titleColor: article ? root.orangeText : root.iosBlue

        Layout.fillWidth: true
        Layout.preferredHeight: 96
        enabled: true
        text: ""

        contentItem: RowLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 46
                Layout.preferredHeight: 46
                radius: 14
                color: card.mode.available ? card.accent : "#c7c7cc"

                Text {
                    anchors.centerIn: parent
                    text: card.article ? "der" : "A"
                    color: "white"
                    font.pixelSize: card.article ? 15 : 19
                    font.weight: Font.ExtraBold
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    Layout.fillWidth: true
                    text: card.mode.title || ""
                    color: card.mode.available ? card.titleColor : root.textMuted
                    font.pixelSize: 18
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: card.mode.available ? (card.mode.description || "") : (card.mode.unavailableReason || "Unavailable")
                    color: "#5d5d63"
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 1.08
                }
            }

            Text {
                text: "›"
                color: "#9a9aa0"
                font.pixelSize: 28
                font.weight: Font.Light
            }
        }

        background: Rectangle {
            radius: 18
            color: card.article ? Qt.rgba(255 / 255, 149 / 255, 0 / 255, card.down ? 0.18 : 0.12) : Qt.rgba(0 / 255, 122 / 255, 255 / 255, card.down ? 0.15 : 0.10)
            border.color: card.article ? Qt.rgba(255 / 255, 149 / 255, 0 / 255, 0.35) : Qt.rgba(0 / 255, 122 / 255, 255 / 255, 0.32)
            border.width: 2
            opacity: card.mode.available ? 1.0 : 0.72
        }
    }

    component StatBlock: ColumnLayout {
        id: statBlock

        property string label: ""
        property string value: "0"

        Layout.fillWidth: true
        spacing: 3

        Text {
            Layout.fillWidth: true
            text: statBlock.value
            color: root.textMain
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 19
            font.weight: Font.ExtraBold
        }

        Text {
            Layout.fillWidth: true
            text: statBlock.label
            color: "#7a7a80"
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 11
        }
    }
}
