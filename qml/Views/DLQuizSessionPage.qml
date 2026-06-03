pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "quiz"

    property var question: ({})
    property var progress: ({})
    property string errorMessage: ""

    readonly property color fieldBg: "#f1f4f9"
    readonly property color green: "#35a969"
    readonly property color greenSoft: Qt.rgba(53 / 255, 169 / 255, 105 / 255, 0.12)
    readonly property color red: "#dc3545"
    readonly property color redSoft: Qt.rgba(220 / 255, 53 / 255, 69 / 255, 0.10)

    Component.onCompleted: refreshQuizState()

    Connections {
        target: appStateManager

        function onQuizStateChanged() {
            root.refreshQuizState()
        }
    }

    function refreshQuizState() {
        question = appStateManager.currentQuizQuestion()
        progress = appStateManager.quizProgress()
        errorMessage = appStateManager.lastError || ""
    }

    function submitAnswer(answer) {
        question = appStateManager.submitQuizAnswer(answer)
        progress = appStateManager.quizProgress()
        errorMessage = appStateManager.lastError || ""
    }

    function continueQuiz() {
        appStateManager.nextQuizQuestion()
        refreshQuizState()
    }

    function exitQuiz() {
        appStateManager.resetQuiz()
        appStateManager.goQuizHomePage()
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 58

        Text {
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            text: "Exit"
            color: root.red
            font.pixelSize: 15
            font.weight: Font.Bold

            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                onClicked: root.exitQuiz()
            }
        }

        Text {
            anchors.centerIn: parent
            text: root.progress.quizTitle || "Quiz"
            color: root.textMain
            font.pixelSize: 17
            font.weight: Font.ExtraBold
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 16

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 10
            radius: 5
            color: root.line

            Rectangle {
                anchors {
                    left: parent.left
                    top: parent.top
                    bottom: parent.bottom
                }
                width: parent.width * Math.max(0, Math.min(1, Number(root.progress.percent || 0) / 100))
                radius: 5
                color: root.blue
            }
        }

        Text {
            Layout.fillWidth: true
            text: "Question " + (root.progress.number || 0) + " of " + (root.progress.total || 0)
            color: root.textMuted
            font.pixelSize: 14
            font.weight: Font.Bold
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 178
            radius: 8
            color: root.cardBg
            border.color: root.line
            border.width: 1

            Column {
                anchors {
                    fill: parent
                    margins: 18
                }
                spacing: 10

                Text {
                    width: parent.width
                    text: root.question.instruction || ""
                    color: root.textMuted
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: root.question.prompt || ""
                    color: root.textMain
                    font.pixelSize: 34
                    font.weight: Font.ExtraBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    visible: (root.question.exampleDe || "").length > 0
                    width: parent.width
                    text: root.question.exampleDe || ""
                    color: root.textMuted
                    font.pixelSize: 14
                    font.italic: true
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
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
        }

        Repeater {
            model: root.question.options || []

            delegate: Button {
                id: optionButton

                required property string modelData

                Layout.fillWidth: true
                Layout.preferredHeight: 52
                text: modelData
                enabled: !Boolean(root.question.isAnswered)
                font.pixelSize: 15
                font.weight: Font.ExtraBold
                onClicked: root.submitAnswer(modelData)

                readonly property bool selected: root.question.selectedAnswer === modelData
                readonly property bool correct: root.question.answer === modelData
                readonly property bool answered: Boolean(root.question.isAnswered)

                contentItem: Text {
                    text: optionButton.text
                    color: {
                        if (optionButton.answered && optionButton.correct) {
                            return root.green
                        }
                        if (optionButton.answered && optionButton.selected && !optionButton.correct) {
                            return root.red
                        }
                        return root.textMain
                    }
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: optionButton.font
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: 8
                    color: {
                        if (optionButton.answered && optionButton.correct) {
                            return root.greenSoft
                        }
                        if (optionButton.answered && optionButton.selected && !optionButton.correct) {
                            return root.redSoft
                        }
                        return root.cardBg
                    }
                    border.color: {
                        if (optionButton.answered && optionButton.correct) {
                            return root.green
                        }
                        if (optionButton.answered && optionButton.selected && !optionButton.correct) {
                            return root.red
                        }
                        return root.line
                    }
                    border.width: 1
                }
            }
        }

        Rectangle {
            visible: Boolean(root.question.isAnswered)
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            radius: 8
            color: root.question.isCorrect ? root.greenSoft : root.redSoft
            border.color: root.question.isCorrect ? root.green : root.red
            border.width: 1

            Text {
                anchors.centerIn: parent
                width: parent.width - 24
                text: root.question.feedback || ""
                color: root.question.isCorrect ? root.green : root.red
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 15
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }
        }

        Button {
            visible: Boolean(root.question.isAnswered)
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            text: (root.progress.number || 0) >= (root.progress.total || 0) ? "Show Results" : "Next Question"
            font.pixelSize: 15
            font.weight: Font.ExtraBold
            onClicked: root.continueQuiz()

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
    }
}
