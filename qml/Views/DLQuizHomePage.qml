pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "quiz"

    property var quizModes: []
    property string errorMessage: ""

    readonly property color fieldBg: "#f1f4f9"
    readonly property color green: "#35a969"
    readonly property color greenSoft: Qt.rgba(53 / 255, 169 / 255, 105 / 255, 0.12)
    readonly property color red: "#dc3545"
    readonly property color redSoft: Qt.rgba(220 / 255, 53 / 255, 69 / 255, 0.10)

    Component.onCompleted: reloadModes()

    Connections {
        target: appStateManager

        function onWordsChanged() {
            root.reloadModes()
        }
    }

    function reloadModes() {
        quizModes = appStateManager.availableQuizModes()
        errorMessage = appStateManager.lastError || ""
    }

    function openSetup(mode) {
        if (!mode.available) {
            errorMessage = mode.unavailableReason || "Not enough words for this quiz."
            return
        }

        appStateManager.goQuizSetupPage(mode.type)
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 112

        Column {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            spacing: 6

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
                text: "Practice saved words with quick multiple-choice rounds."
                color: root.textMuted
                font.pixelSize: 15
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
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

    Repeater {
        model: root.quizModes

        delegate: QuizModeCard {
            required property var modelData

            Layout.fillWidth: true
            Layout.bottomMargin: 12
            mode: modelData
            onClicked: root.openSetup(modelData)
        }
    }

    component QuizModeCard: Button {
        id: card

        property var mode: ({})

        Layout.fillWidth: true
        Layout.preferredHeight: 132
        enabled: true
        text: ""

        contentItem: Item {
            anchors.fill: parent

            Column {
                anchors {
                    left: parent.left
                    right: statusPill.left
                    verticalCenter: parent.verticalCenter
                    leftMargin: 16
                    rightMargin: 14
                }
                spacing: 7

                Text {
                    width: parent.width
                    text: card.mode.title || ""
                    color: root.textMain
                    font.pixelSize: 20
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: card.mode.description || ""
                    color: root.textMuted
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: card.mode.available
                          ? (card.mode.availableCount + " available")
                          : (card.mode.unavailableReason || "Unavailable")
                    color: card.mode.available ? root.green : root.red
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                id: statusPill

                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    rightMargin: 16
                }
                width: 92
                height: 34
                radius: 8
                color: card.mode.available ? root.greenSoft : root.fieldBg
                border.color: card.mode.available ? root.green : root.line
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: card.mode.available ? "Start" : "Locked"
                    color: card.mode.available ? root.green : root.textMuted
                    font.pixelSize: 13
                    font.weight: Font.ExtraBold
                }
            }
        }

        background: Rectangle {
            radius: 8
            color: root.cardBg
            border.color: card.down ? root.blue : root.line
            border.width: 1
        }
    }
}
