pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "quiz"

    property var result: ({})

    readonly property color fieldBg: "#f1f4f9"
    readonly property color green: "#35a969"
    readonly property color red: "#dc3545"

    Component.onCompleted: result = appStateManager.quizResult()

    function retryQuiz() {
        var type = result.quizType || appStateManager.selectedQuizType() || "translation"
        appStateManager.resetQuiz()
        appStateManager.goQuizSetupPage(type)
    }

    function backHome() {
        appStateManager.resetQuiz()
        appStateManager.goQuizHomePage()
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 58

        Text {
            anchors.centerIn: parent
            text: "Results"
            color: root.textMain
            font.pixelSize: 17
            font.weight: Font.ExtraBold
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 16

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 190
            radius: 8
            color: root.cardBg
            border.color: root.line
            border.width: 1

            Column {
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 8

                Text {
                    width: parent.width
                    text: root.result.quizTitle || "Quiz"
                    color: root.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: (root.result.accuracy || 0) + "%"
                    color: root.textMain
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 54
                    font.weight: Font.ExtraBold
                }

                Text {
                    width: parent.width
                    text: (root.result.correct || 0) + " of " + (root.result.total || 0) + " correct"
                    color: root.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            StatCard {
                label: "Correct"
                value: String(root.result.correct || 0)
                accent: root.green
            }

            StatCard {
                label: "Wrong"
                value: String(root.result.wrong || 0)
                accent: root.red
            }
        }

        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            text: "Retry"
            font.pixelSize: 15
            font.weight: Font.ExtraBold
            onClicked: root.retryQuiz()

            contentItem: Text {
                text: parent.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: parent.font
            }

            background: Rectangle {
                radius: 8
                color: root.blue
            }
        }

        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            text: "Back to Quiz Home"
            font.pixelSize: 15
            font.weight: Font.ExtraBold
            onClicked: root.backHome()

            contentItem: Text {
                text: parent.text
                color: root.blue
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: parent.font
            }

            background: Rectangle {
                radius: 8
                color: root.cardBg
                border.color: root.blue
                border.width: 1
            }
        }
    }

    component StatCard: Rectangle {
        id: statCard

        property string label: ""
        property string value: "0"
        property color accent: root.blue

        Layout.fillWidth: true
        Layout.preferredHeight: 96
        radius: 8
        color: root.cardBg
        border.color: root.line
        border.width: 1

        Column {
            anchors.centerIn: parent
            spacing: 6

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: statCard.value
                color: statCard.accent
                font.pixelSize: 30
                font.weight: Font.ExtraBold
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: statCard.label
                color: root.textMuted
                font.pixelSize: 13
                font.weight: Font.Bold
            }
        }
    }
}
