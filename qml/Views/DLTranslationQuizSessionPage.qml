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

    function optionTextColor(option) {
        if (Boolean(question.isAnswered) && question.answer === option) {
            return green
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === option && question.answer !== option) {
            return red
        }

        return textMain
    }

    function optionBackground(option) {
        if (Boolean(question.isAnswered) && question.answer === option) {
            return greenSoft
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === option && question.answer !== option) {
            return redSoft
        }

        return cardBg
    }

    function optionBorder(option) {
        if (Boolean(question.isAnswered) && question.answer === option) {
            return green
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === option && question.answer !== option) {
            return red
        }

        return line
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
            text: "Translation Quiz"
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

        RowLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: "Question " + (root.progress.number || 0) + " of " + (root.progress.total || 0)
                color: root.textMuted
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            Text {
                text: (root.progress.correct || 0) + " correct"
                color: root.green
                font.pixelSize: 14
                font.weight: Font.Bold
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 210
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
                    text: "Choose the native translation"
                    color: root.textMuted
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: root.question.prompt || ""
                    color: root.textMain
                    font.pixelSize: 38
                    font.weight: Font.ExtraBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: root.line
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
                Layout.preferredHeight: 56
                text: modelData
                enabled: !Boolean(root.question.isAnswered)
                font.pixelSize: 15
                font.weight: Font.ExtraBold
                onClicked: root.submitAnswer(modelData)

                contentItem: Text {
                    anchors.fill: parent
                    leftPadding: 14
                    rightPadding: 14
                    text: optionButton.text
                    color: root.optionTextColor(optionButton.modelData)
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                    font: optionButton.font
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: 8
                    color: root.optionBackground(optionButton.modelData)
                    border.color: root.optionBorder(optionButton.modelData)
                    border.width: 1
                }
            }
        }

        FeedbackBlock {
            visible: Boolean(root.question.isAnswered)
            correct: Boolean(root.question.isCorrect)
            message: root.question.feedback || ""
        }

        ContinueButton {
            visible: Boolean(root.question.isAnswered)
            text: (root.progress.number || 0) >= (root.progress.total || 0) ? "Show Results" : "Next Translation"
            onClicked: root.continueQuiz()
        }
    }

    component FeedbackBlock: Rectangle {
        property bool correct: false
        property string message: ""

        Layout.fillWidth: true
        Layout.preferredHeight: 58
        radius: 8
        color: correct ? root.greenSoft : root.redSoft
        border.color: correct ? root.green : root.red
        border.width: 1

        Text {
            anchors.centerIn: parent
            width: parent.width - 24
            text: parent.message
            color: parent.correct ? root.green : root.red
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 15
            font.weight: Font.ExtraBold
            elide: Text.ElideRight
        }
    }

    component ContinueButton: Button {
        Layout.fillWidth: true
        Layout.preferredHeight: 52
        font.pixelSize: 15
        font.weight: Font.ExtraBold

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
