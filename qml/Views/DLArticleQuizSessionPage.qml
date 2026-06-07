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
    pagePadding: 20
    pageTopPadding: 0
    pageBottomPadding: 24

    property var question: ({})
    property var progress: ({})
    property string errorMessage: ""

    readonly property color accent: "#ff9500"
    readonly property color accentText: "#c96f00"
    readonly property color accentSoft: Qt.rgba(255 / 255, 149 / 255, 0 / 255, 0.12)
    readonly property color accentTrack: Qt.rgba(255 / 255, 149 / 255, 0 / 255, 0.18)
    readonly property color optionBg: root.cardBg
    readonly property color optionBorderDefault: "#e3e3e8"
    readonly property color neutralBg: "#fff7f8"
    readonly property color neutralBorder: "#f4b6c3"
    readonly property color green: "#34c759"
    readonly property color greenSoft: Qt.rgba(52 / 255, 199 / 255, 89 / 255, 0.15)
    readonly property color greenText: "#168a35"
    readonly property color red: "#ff3b30"
    readonly property color redSoft: Qt.rgba(255 / 255, 59 / 255, 48 / 255, 0.13)

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

    function nounWithoutArticle(value) {
        var text = String(value || "").trim();
        return text.replace(/^(der|die|das)\s+/i, "");
    }

    function articleTextColor(article) {
        if (Boolean(question.isAnswered) && question.answer === article) {
            return green;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return red;
        }

        if (Boolean(question.isAnswered)) {
            return neutralBorder;
        }

        return root.textMain;
    }

    function articleBackground(article) {
        if (Boolean(question.isAnswered) && question.answer === article) {
            return greenSoft;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return redSoft;
        }

        if (Boolean(question.isAnswered)) {
            return neutralBg;
        }

        return optionBg;
    }

    function articleBorder(article) {
        if (Boolean(question.isAnswered) && question.answer === article) {
            return green;
        }

        if (Boolean(question.isAnswered) && question.selectedAnswer === article && question.answer !== article) {
            return red;
        }

        if (Boolean(question.isAnswered)) {
            return neutralBorder;
        }

        return optionBorderDefault;
    }

    function articleOpacity(article) {
        if (!Boolean(question.isAnswered)) {
            return 1.0;
        }

        return question.answer === article || question.selectedAnswer === article ? 1.0 : 0.75;
    }

    function progressRatio() {
        return Math.max(0, Math.min(1, Number(root.progress.percent || 0) / 100));
    }

    QuizHeader {
        titleText: "Article Quiz"
        subtitleText: "Select the correct article"
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 24
        spacing: 0

        ProgressBlock {
            accentColor: root.accent
            trackColor: root.accentTrack
        }

        QuestionCard {
            Layout.topMargin: 20
            promptText: "Select the correct article"
            wordText: root.nounWithoutArticle(root.question.prompt || "")
            cardColor: root.accentSoft
        }

        Text {
            visible: root.errorMessage.length > 0
            Layout.fillWidth: true
            Layout.topMargin: 14
            text: root.errorMessage
            color: root.red
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 22
            spacing: 12

            Repeater {
                model: root.question.options || ["der", "die", "das"]

                delegate: ArticleOption {
                    required property string modelData

                    text: modelData
                    enabled: !Boolean(root.question.isAnswered)
                    textColor: root.articleTextColor(modelData)
                    fillColor: root.articleBackground(modelData)
                    outlineColor: root.articleBorder(modelData)
                    opacity: root.articleOpacity(modelData)
                    onClicked: root.submitAnswer(modelData)
                }
            }
        }

        Text {
            visible: Boolean(root.question.isAnswered)
            Layout.fillWidth: true
            Layout.topMargin: 20
            text: root.question.feedback || ""
            color: root.question.isCorrect ? root.greenText : root.red
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        ContinueButton {
            visible: Boolean(root.question.isAnswered)
            Layout.topMargin: 18
            text: (root.progress.number || 0) >= (root.progress.total || 0) ? "Show Results" : "Next Question"
            onClicked: root.continueQuiz()
        }
    }

    component QuizHeader: Item {
        id: quizHeader

        property string titleText: ""
        property string subtitleText: ""

        Layout.fillWidth: true
        Layout.preferredHeight: 70

        Button {
            id: backButton

            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            width: 44
            height: 44
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
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 12
            }
            spacing: 2

            Text {
                width: parent.width
                text: quizHeader.titleText
                color: root.textMain
                font.pixelSize: 24
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: quizHeader.subtitleText
                color: root.textMuted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                maximumLineCount: 1
                elide: Text.ElideRight
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

    component ProgressBlock: ColumnLayout {
        id: progressBlock

        property color accentColor: root.accent
        property color trackColor: root.accentTrack

        Layout.fillWidth: true
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 8
            radius: 4
            color: progressBlock.trackColor
            clip: true

            Rectangle {
                anchors {
                    left: parent.left
                    top: parent.top
                    bottom: parent.bottom
                }
                width: parent.width * root.progressRatio()
                radius: 4
                color: progressBlock.accentColor
            }
        }

        Text {
            Layout.fillWidth: true
            text: "QUESTION " + (root.progress.number || 0) + " OF " + (root.progress.total || 0)
            color: root.textMuted
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 13
            font.weight: Font.Bold
        }
    }

    component QuestionCard: Rectangle {
        id: questionCard

        property string promptText: ""
        property string wordText: ""
        property color cardColor: root.accentSoft

        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(176, Math.min(232, root.height * 0.23), questionContent.implicitHeight + 54)
        radius: 18
        color: questionCard.cardColor

        ColumnLayout {
            id: questionContent

            anchors.centerIn: parent
            width: parent.width - 36
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: questionCard.promptText
                color: root.textMuted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: questionCard.wordText
                color: root.textMain
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeight: 0.94
                font.pixelSize: 38
                font.weight: Font.ExtraBold
            }
        }
    }

    component ArticleOption: Button {
        id: optionButton

        property color textColor: root.textMain
        property color fillColor: root.optionBg
        property color outlineColor: "transparent"

        Layout.fillWidth: true
        Layout.preferredHeight: 72
        font.pixelSize: 23
        font.weight: Font.ExtraBold

        contentItem: Text {
            text: optionButton.text
            color: optionButton.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: optionButton.font
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 16
            color: optionButton.fillColor
            border.color: optionButton.outlineColor
            border.width: Boolean(root.question.isAnswered) ? 2 : 1
        }
    }

    component ContinueButton: Button {
        id: continueButton

        Layout.fillWidth: true
        Layout.preferredHeight: 54
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
