pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "quiz"
    pageBackground: "#ffffff"
    pageBottomPadding: 24

    property var question: ({})
    property var progress: ({})
    property string errorMessage: ""

    readonly property color accent: "#007aff"
    readonly property color accentSoft: Qt.rgba(0 / 255, 122 / 255, 255 / 255, 0.10)
    readonly property color accentTrack: Qt.rgba(0 / 255, 122 / 255, 255 / 255, 0.16)
    readonly property color optionBg: "#f5f5f7"
    readonly property color green: "#34c759"
    readonly property color greenText: "#168a35"
    readonly property color greenSoft: Qt.rgba(52 / 255, 199 / 255, 89 / 255, 0.13)
    readonly property color red: "#ff3b30"
    readonly property color redSoft: Qt.rgba(255 / 255, 59 / 255, 48 / 255, 0.10)

    Component.onCompleted: refreshQuizState()

    Connections {
        target: appStateManager

        function onQuizStateChanged() {
            root.refreshQuizState();
        }
    }

    function refreshQuizState() {
        question = appStateManager.currentQuizQuestion();
        progress = appStateManager.quizProgress();
        errorMessage = appStateManager.lastError || "";
    }

    function submitAnswer(answer) {
        question = appStateManager.submitQuizAnswer(answer);
        progress = appStateManager.quizProgress();
        errorMessage = appStateManager.lastError || "";
    }

    function continueQuiz() {
        appStateManager.nextQuizQuestion();
        refreshQuizState();
    }

    function exitQuiz() {
        appStateManager.resetQuiz();
        appStateManager.goQuizHomePage();
    }

    function optionTextColor(option) {
        if (Boolean(question.isAnswered) && question.answer === option) {
            return greenText;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === option && question.answer !== option) {
            return red;
        }

        return "#222222";
    }

    function optionBackground(option) {
        if (Boolean(question.isAnswered) && question.answer === option) {
            return greenSoft;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === option && question.answer !== option) {
            return redSoft;
        }

        return optionBg;
    }

    function optionBorder(option) {
        if (Boolean(question.isAnswered) && question.answer === option) {
            return green;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === option && question.answer !== option) {
            return red;
        }

        return "transparent";
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 84

        Column {
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            spacing: 3

            Text {
                text: "Translation Quiz"
                color: root.textMain
                font.pixelSize: 24
                font.weight: Font.ExtraBold
            }

            Text {
                text: "German ↔ Native Language"
                color: root.textMuted
                font.pixelSize: 13
            }
        }

        Text {
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            text: "Quit"
            color: root.red
            font.pixelSize: 15
            font.weight: Font.DemiBold

            MouseArea {
                anchors.fill: parent
                anchors.margins: -12
                onClicked: root.exitQuiz()
            }
        }

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: "#ececec"
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 22
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "Question " + (root.progress.number || 0) + " of " + (root.progress.total || 0)
                color: "#666666"
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Text {
                text: (root.progress.correct || 0) + " correct"
                color: root.greenText
                font.pixelSize: 14
                font.weight: Font.Bold
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 9
            radius: 5
            color: root.accentTrack
            clip: true

            Rectangle {
                anchors {
                    left: parent.left
                    top: parent.top
                    bottom: parent.bottom
                }
                width: parent.width * Math.max(0, Math.min(1, Number(root.progress.percent || 0) / 100))
                radius: 5
                color: root.accent
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(178, questionContent.implicitHeight + 48)
            radius: 22
            color: root.accentSoft

            ColumnLayout {
                id: questionContent

                anchors.centerIn: parent
                width: parent.width - 36
                spacing: 12

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredHeight: 28
                    Layout.preferredWidth: 108
                    radius: 14
                    color: "#ffffff"

                    Text {
                        anchors.centerIn: parent
                        text: "DE → Native"
                        color: root.accent
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Select the native-language translation:"
                    color: "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: root.question.prompt || ""
                    color: root.textMain
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 34
                    font.weight: Font.ExtraBold
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 0.94
                }

                Text {
                    visible: (root.question.exampleDe || "").length > 0
                    Layout.fillWidth: true
                    text: root.question.exampleDe || ""
                    color: "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 15
                    font.italic: true
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 1.12
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

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: root.question.options || []

                delegate: Button {
                    id: optionButton

                    required property string modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 58
                    text: modelData
                    enabled: !Boolean(root.question.isAnswered)
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    onClicked: root.submitAnswer(modelData)

                    contentItem: Text {
                        anchors.fill: parent
                        leftPadding: 14
                        rightPadding: 14
                        text: optionButton.text
                        color: root.optionTextColor(optionButton.modelData)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font: optionButton.font
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        radius: 16
                        color: root.optionBackground(optionButton.modelData)
                        border.color: root.optionBorder(optionButton.modelData)
                        border.width: Boolean(root.question.isAnswered) ? 2 : 0
                    }
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
            text: (root.progress.number || 0) >= (root.progress.total || 0) ? "Show Results" : "Next Question"
            onClicked: root.continueQuiz()
        }
    }

    component FeedbackBlock: Rectangle {
        property bool correct: false
        property string message: ""

        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(52, feedbackText.implicitHeight + 28)
        radius: 16
        color: correct ? root.greenSoft : root.redSoft

        Text {
            id: feedbackText

            anchors.centerIn: parent
            width: parent.width - 28
            text: parent.message
            color: parent.correct ? root.greenText : root.red
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 15
            font.weight: Font.DemiBold
            lineHeight: 1.12
        }
    }

    component ContinueButton: Button {
        id: continueButton

        Layout.fillWidth: true
        Layout.preferredHeight: 56
        font.pixelSize: 17
        font.weight: Font.Bold

        contentItem: Text {
            text: continueButton.text
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: continueButton.font
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 16
            color: root.accent
        }
    }
}
