pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
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

    readonly property color accent: "#ff9500"
    readonly property color accentText: "#c96f00"
    readonly property color accentSoft: Qt.rgba(255 / 255, 149 / 255, 0 / 255, 0.12)
    readonly property color accentTrack: Qt.rgba(255 / 255, 149 / 255, 0 / 255, 0.18)
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

    function articleName(article) {
        if (article === "der") {
            return "Masculine article";
        }

        if (article === "die") {
            return "Feminine article";
        }

        if (article === "das") {
            return "Neuter article";
        }

        return "German article";
    }

    function optionBackground(article) {
        if (Boolean(question.isAnswered) && question.answer === article) {
            return greenSoft;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return redSoft;
        }

        return optionBg;
    }

    function optionBorder(article) {
        if (Boolean(question.isAnswered) && question.answer === article) {
            return green;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return red;
        }

        return "transparent";
    }

    function labelBackground(article) {
        if (Boolean(question.isAnswered) && question.answer === article) {
            return green;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return red;
        }

        return "#ffffff";
    }

    function labelColor(article) {
        if (Boolean(question.isAnswered) && (question.answer === article || question.selectedAnswer === article)) {
            return "white";
        }

        return accentText;
    }

    function hintText(article) {
        if (!Boolean(question.isAnswered)) {
            return articleName(article);
        }

        if (question.answer === article) {
            return "Correct answer";
        }

        if (question.selectedAnswer === article) {
            return "Selected answer";
        }

        return "Not selected";
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 84

        Button {
            id: backButton

            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            width: 44
            height: 44
            text: ""
            onClicked: root.exitQuiz()

            contentItem: Item {
                IconImage {
                    anchors.centerIn: parent
                    source: Qt.resolvedUrl("../../assets/icons/back_arrow_icon.svg")
                    width: 18
                    height: 28
                    color: root.accent
                }
            }

            background: Rectangle {
                radius: 14
                color: root.accentSoft
                border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.24)
                border.width: 1
            }
        }

        Column {
            anchors {
                left: backButton.right
                verticalCenter: parent.verticalCenter
                leftMargin: 12
            }
            spacing: 3

            Text {
                text: "Article Quiz"
                color: root.textMain
                font.pixelSize: 24
                font.weight: Font.ExtraBold
            }

            Text {
                text: "German noun articles"
                color: root.textMuted
                font.pixelSize: 13
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
                text: (root.progress.wrong || 0) + " wrong"
                color: root.progress.wrong > 0 ? root.red : root.textMuted
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
            Layout.preferredHeight: Math.max(170, questionContent.implicitHeight + 48)
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
                    Layout.preferredWidth: 118
                    radius: 14
                    color: "#ffffff"

                    Text {
                        anchors.centerIn: parent
                        text: "Article Practice"
                        color: root.accentText
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "Select the correct article:"
                    color: "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
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
                    visible: (root.question.nativeTranslation || "").length > 0
                    Layout.fillWidth: true
                    text: root.question.nativeTranslation || ""
                    color: "#666666"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 15
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

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 12

            Repeater {
                model: root.question.options || []

                delegate: Button {
                    id: articleButton

                    required property string modelData

                    Layout.fillWidth: true
                    Layout.preferredHeight: 64
                    text: modelData
                    enabled: !Boolean(root.question.isAnswered)
                    onClicked: root.submitAnswer(modelData)

                    opacity: Boolean(root.question.isAnswered) && root.question.answer !== modelData && root.question.selectedAnswer !== modelData ? 0.55 : 1.0

                    contentItem: RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 38
                            radius: 12
                            color: root.labelBackground(articleButton.modelData)

                            Text {
                                anchors.centerIn: parent
                                text: articleButton.modelData
                                color: root.labelColor(articleButton.modelData)
                                font.pixelSize: 18
                                font.weight: Font.ExtraBold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                Layout.fillWidth: true
                                text: articleButton.modelData + " " + (root.question.prompt || "")
                                color: root.textMain
                                font.pixelSize: 16
                                font.weight: Font.Bold
                                elide: Text.ElideRight
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.hintText(articleButton.modelData)
                                color: "#777777"
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                    }

                    background: Rectangle {
                        radius: 18
                        color: root.optionBackground(articleButton.modelData)
                        border.color: root.optionBorder(articleButton.modelData)
                        border.width: Boolean(root.question.isAnswered) ? 2 : 0
                    }
                }
            }
        }

        Rectangle {
            visible: Boolean(root.question.isAnswered)
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(52, feedbackText.implicitHeight + 28)
            radius: 16
            color: root.question.isCorrect ? root.greenSoft : root.redSoft

            Text {
                id: feedbackText

                anchors.centerIn: parent
                width: parent.width - 28
                text: root.question.feedback || ""
                color: root.question.isCorrect ? root.greenText : root.red
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: 15
                font.weight: Font.DemiBold
                lineHeight: 1.12
            }
        }

        Button {
            id: continueButton

            visible: Boolean(root.question.isAnswered)
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: (root.progress.number || 0) >= (root.progress.total || 0) ? "Show Results" : "Next Question"
            font.pixelSize: 17
            font.weight: Font.Bold
            onClicked: root.continueQuiz()

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
}
