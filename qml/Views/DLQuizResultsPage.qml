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
    pageBackground: "#f7f8fa"
    pageBottomPadding: 24

    property var result: ({})

    readonly property color green: "#17934a"
    readonly property color greenSoft: "#eaf8ef"
    readonly property color red: "#d93030"
    readonly property color redSoft: "#fdecec"
    readonly property color blueSoft: "#eaf2ff"
    readonly property color primaryBlue: "#3366cc"

    Component.onCompleted: result = appStateManager.quizResult()

    function retryQuiz() {
        var type = result.quizType || appStateManager.selectedQuizType() || "translation";
        appStateManager.resetQuiz();
        appStateManager.goQuizSetupPage(type);
    }

    function backHome() {
        appStateManager.resetQuiz();
        appStateManager.goQuizHomePage();
    }

    function resultTitle() {
        var accuracy = Number(result.accuracy || 0);
        if (accuracy >= 90) {
            return "Excellent!";
        }

        if (accuracy >= 70) {
            return "Well Done!";
        }

        if (accuracy >= 50) {
            return "Good Practice";
        }

        return "Keep Practicing";
    }

    function resultSubtitle() {
        var accuracy = Number(result.accuracy || 0);
        if (accuracy >= 80) {
            return "You answered most questions correctly and completed the quiz successfully.";
        }

        if (accuracy >= 50) {
            return "You completed the quiz. A quick retry will help lock the tricky words in.";
        }

        return "You completed the quiz. Review the misses and give it another round.";
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 64

        Button {
            id: retryBackButton

            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            width: 42
            height: 42
            text: ""
            onClicked: root.retryQuiz()

            contentItem: Item {
                IconImage {
                    anchors.centerIn: parent
                    source: Qt.resolvedUrl("../../assets/icons/back_arrow_icon.svg")
                    width: 16
                    height: 26
                    color: root.textMain
                }
            }

            background: Rectangle {
                radius: 14
                color: "#ffffff"
            }
        }

        Text {
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            text: root.result.quizTitle || "Quiz"
            color: root.textMuted
            font.pixelSize: 15
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 34
        spacing: 0

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 120
            Layout.preferredHeight: 120
            radius: 60
            color: "#ffca28"

            Text {
                anchors.centerIn: parent
                text: "★"
                color: "#ffffff"
                font.pixelSize: 52
                font.weight: Font.ExtraBold
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 28
            text: root.resultTitle()
            color: root.textMain
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 34
            font.weight: Font.ExtraBold
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 30
            Layout.rightMargin: 30
            Layout.topMargin: 12
            text: root.resultSubtitle()
            color: root.textMuted
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 17
            lineHeight: 1.18
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 40
            spacing: 14

            StatCard {
                label: "Correct"
                value: String(root.result.correct || 0)
                accent: root.green
                cardColor: root.greenSoft
            }

            StatCard {
                label: "Wrong"
                value: String(root.result.wrong || 0)
                accent: root.red
                cardColor: root.redSoft
            }

            StatCard {
                label: "Score"
                value: (root.result.accuracy || 0) + "%"
                accent: root.primaryBlue
                cardColor: root.blueSoft
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 44
            spacing: 14

            Button {
                id: newQuizButton

                Layout.fillWidth: true
                Layout.preferredHeight: 56
                text: "New Quiz"
                font.pixelSize: 16
                font.weight: Font.Bold
                onClicked: root.retryQuiz()

                contentItem: Text {
                    text: newQuizButton.text
                    color: root.textMain
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: newQuizButton.font
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: 20
                    color: "#ffffff"
                }
            }

            Button {
                id: quizHomeButton

                Layout.fillWidth: true
                Layout.preferredHeight: 56
                text: "Quiz Home"
                font.pixelSize: 16
                font.weight: Font.Bold
                onClicked: root.backHome()

                contentItem: Text {
                    text: quizHomeButton.text
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: quizHomeButton.font
                    elide: Text.ElideRight
                }

                background: Rectangle {
                    radius: 20
                    color: root.primaryBlue
                }
            }
        }
    }

    component StatCard: Rectangle {
        id: statCard

        property string label: ""
        property string value: "0"
        property color accent: root.primaryBlue
        property color cardColor: root.blueSoft

        Layout.fillWidth: true
        Layout.preferredHeight: 98
        radius: 24
        color: statCard.cardColor

        Column {
            anchors.centerIn: parent
            width: parent.width - 12
            spacing: 8

            Text {
                width: parent.width
                text: statCard.value
                color: statCard.accent
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 30
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: statCard.label
                color: root.textMuted
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }
}
