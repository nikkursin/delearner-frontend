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
    readonly property color pink: "#e94f72"

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

    function articleAccent(article) {
        if (article === "der") {
            return root.blue
        }

        if (article === "die") {
            return root.pink
        }

        if (article === "das") {
            return root.green
        }

        return root.textMuted
    }

    function optionBackground(article) {
        if (Boolean(question.isAnswered) && question.answer === article) {
            return Qt.rgba(articleAccent(article).r, articleAccent(article).g, articleAccent(article).b, 0.14)
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return root.redSoft
        }

        return root.cardBg
    }

    function optionBorder(article) {
        if (Boolean(question.isAnswered) && question.answer === article) {
            return articleAccent(article)
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return root.red
        }

        return root.line
    }

    function optionTextColor(article) {
        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return root.red
        }

        return articleAccent(article)
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
            text: "Article Quiz"
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
                color: root.green
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
                text: (root.progress.wrong || 0) + " wrong"
                color: root.progress.wrong > 0 ? root.red : root.textMuted
                font.pixelSize: 14
                font.weight: Font.Bold
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            radius: 8
            color: root.cardBg
            border.color: root.line
            border.width: 1

            Column {
                anchors {
                    fill: parent
                    margins: 18
                }
                spacing: 14

                Text {
                    width: parent.width
                    text: "Choose the correct German article"
                    color: root.textMuted
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                RowLayout {
                    width: parent.width
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 76
                        Layout.preferredHeight: 54
                        radius: 8
                        color: root.fieldBg
                        border.color: root.line
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: "___"
                            color: Boolean(root.question.isAnswered) ? root.articleAccent(root.question.answer || "") : root.textMuted
                            font.pixelSize: 26
                            font.weight: Font.ExtraBold
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.question.prompt || ""
                        color: root.textMain
                        font.pixelSize: 36
                        font.weight: Font.ExtraBold
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }

                Text {
                    visible: (root.question.nativeTranslation || "").length > 0
                    width: parent.width
                    text: root.question.nativeTranslation || ""
                    color: root.textMuted
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
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

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Repeater {
                model: root.question.options || []

                delegate: Button {
                    id: articleButton

                    required property string modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 86
                    text: modelData
                    enabled: !Boolean(root.question.isAnswered)
                    font.pixelSize: 22
                    font.weight: Font.ExtraBold
                    onClicked: root.submitAnswer(modelData)

                    contentItem: Text {
                        text: articleButton.text
                        color: root.optionTextColor(articleButton.modelData)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font: articleButton.font
                    }

                    background: Rectangle {
                        radius: 8
                        color: root.optionBackground(articleButton.modelData)
                        border.color: root.optionBorder(articleButton.modelData)
                        border.width: 1
                    }
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
            text: (root.progress.number || 0) >= (root.progress.total || 0) ? "Show Results" : "Next Article"
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
                color: root.green
            }
        }
    }
}
