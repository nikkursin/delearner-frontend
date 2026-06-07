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

    property string quizType: ""
    property string quizTitle: "Quiz"
    property var groupOptions: [
        {
            id: -1,
            name: "All Words",
            count: 0
        }
    ]
    property int selectedGroupId: -1
    property int questionCount: 10
    property int availableCount: 0
    property string errorMessage: ""

    readonly property bool isArticle: quizType === "article"
    readonly property color accent: isArticle ? "#ff9500" : "#007aff"
    readonly property color accentText: isArticle ? "#c96f00" : "#007aff"
    readonly property color accentSoft: isArticle ? Qt.rgba(255 / 255, 149 / 255, 0 / 255, 0.12) : Qt.rgba(0 / 255, 122 / 255, 255 / 255, 0.10)
    readonly property color softPanel: "#f5f5f7"
    readonly property color rowLine: "#e3e3e8"
    readonly property color red: "#ff3b30"
    readonly property color redSoft: Qt.rgba(255 / 255, 59 / 255, 48 / 255, 0.10)

    Component.onCompleted: reloadSetup()

    function reloadSetup() {
        quizType = appStateManager.selectedQuizType();
        if (quizType.length === 0) {
            quizType = "translation";
        }

        quizTitle = quizType === "article" ? "Article Quiz" : "Translation Quiz";
        loadGroups();
        refreshAvailability();
    }

    function loadGroups() {
        var groups = appStateManager.availableGroups();
        var options = [
            {
                id: -1,
                name: "All Words",
                count: appStateManager.availableQuizQuestionCount(quizType, -1)
            }
        ];

        for (var i = 0; i < groups.length; ++i) {
            options.push({
                id: groups[i].id,
                name: groups[i].name,
                count: appStateManager.availableQuizQuestionCount(quizType, groups[i].id)
            });
        }

        groupOptions = options;
        selectedGroupId = -1;
    }

    function refreshAvailability() {
        availableCount = appStateManager.availableQuizQuestionCount(quizType, selectedGroupId);
        questionCount = Math.max(1, Math.min(questionCount, Math.max(1, availableCount)));
        errorMessage = "";
        appStateManager.canStartQuiz(quizType, selectedGroupId, questionCount);
        if (appStateManager.lastError) {
            errorMessage = appStateManager.lastError;
        }
    }

    function selectGroup(groupId) {
        selectedGroupId = groupId;
        refreshAvailability();
    }

    function selectQuestionCount(count) {
        questionCount = Math.max(1, Math.min(Math.round(count), Math.max(1, availableCount)));
        errorMessage = "";
        appStateManager.canStartQuiz(quizType, selectedGroupId, questionCount);
        if (appStateManager.lastError) {
            errorMessage = appStateManager.lastError;
        }
    }

    function startQuiz() {
        if (!appStateManager.startQuiz(quizType, selectedGroupId, questionCount)) {
            errorMessage = appStateManager.lastError || "Unable to start quiz.";
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 92

        Column {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                bottomMargin: 18
            }
            spacing: 4

            Text {
                width: parent.width
                text: "Quiz Setup"
                color: root.textMain
                font.pixelSize: 28
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: "Configure the selected quiz before starting."
                color: root.textMuted
                font.pixelSize: 14
                wrapMode: Text.WordWrap
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
        Layout.topMargin: 20
        spacing: 24

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            radius: 18
            color: root.accentSoft

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Rectangle {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        radius: 10
                        color: root.accent

                        Text {
                            anchors.centerIn: parent
                            text: root.isArticle ? "der" : "A"
                            color: "white"
                            font.pixelSize: root.isArticle ? 12 : 14
                            font.weight: Font.ExtraBold
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.quizTitle
                        color: root.accentText
                        font.pixelSize: 17
                        font.weight: Font.ExtraBold
                        elide: Text.ElideRight
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.isArticle ? "Practice German noun articles: der, die, and das." : "Practice translations between German and your selected native language."
                    color: "#555555"
                    font.pixelSize: 14
                    wrapMode: Text.WordWrap
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

        SetupSection {
            title: "Word Groups"

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: groupRows.implicitHeight
                radius: 16
                color: root.softPanel
                clip: true

                ColumnLayout {
                    id: groupRows

                    width: parent.width
                    spacing: 0

                    Repeater {
                        model: root.groupOptions

                        delegate: Button {
                            id: groupRow

                            required property int index
                            required property var modelData
                            readonly property bool selected: root.selectedGroupId === groupRow.modelData.id

                            Layout.fillWidth: true
                            Layout.preferredHeight: 54
                            text: ""
                            onClicked: root.selectGroup(groupRow.modelData.id)

                            contentItem: RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: 10

                                Rectangle {
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: 22
                                    radius: 11
                                    color: groupRow.selected ? root.accent : "transparent"
                                    border.color: groupRow.selected ? root.accent : "#c7c7cc"
                                    border.width: 2

                                    Text {
                                        anchors.centerIn: parent
                                        visible: groupRow.selected
                                        text: "✓"
                                        color: "white"
                                        font.pixelSize: 13
                                        font.weight: Font.Bold
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: groupRow.modelData.name
                                    color: root.textMain
                                    font.pixelSize: 15
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: groupRow.modelData.count + (root.isArticle ? " nouns" : " words")
                                    color: "#8a8a8e"
                                    font.pixelSize: 13
                                }
                            }

                            background: Rectangle {
                                color: groupRow.down ? Qt.rgba(0, 0, 0, 0.04) : "transparent"

                                Rectangle {
                                    anchors {
                                        left: parent.left
                                        right: parent.right
                                        bottom: parent.bottom
                                        leftMargin: 16
                                    }
                                    height: 1
                                    color: root.rowLine
                                    visible: groupRow.index < root.groupOptions.length - 1
                                }
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.isArticle ? "For Article Quiz, this list counts only nouns with der, die, or das." : "Choose all words or narrow the session to one saved group."
                color: "#8a8a8e"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                lineHeight: 1.12
            }
        }

        SetupSection {
            title: "Number of Questions"

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 118
                radius: 16
                color: root.softPanel

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: "Questions"
                            color: "#333333"
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }

                        Text {
                            text: String(root.questionCount)
                            color: root.accent
                            font.pixelSize: 22
                            font.weight: Font.ExtraBold
                        }
                    }

                    Slider {
                        id: questionSlider

                        Layout.fillWidth: true
                        from: 1
                        to: Math.max(1, root.availableCount)
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: root.questionCount
                        onMoved: root.selectQuestionCount(value)

                        background: Rectangle {
                            x: questionSlider.leftPadding
                            y: questionSlider.topPadding + questionSlider.availableHeight / 2 - height / 2
                            width: questionSlider.availableWidth
                            height: 6
                            radius: 3
                            color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.18)

                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, questionSlider.visualPosition))
                                height: parent.height
                                radius: 3
                                color: root.accent
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: "1"
                            color: "#8a8a8e"
                            font.pixelSize: 12
                        }

                        Text {
                            text: String(Math.max(1, root.availableCount))
                            color: "#8a8a8e"
                            font.pixelSize: 12
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: "The final number is limited by available words in the selected group."
                color: "#8a8a8e"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                font.pixelSize: 13
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.availableCount + (root.isArticle ? " nouns available · " : " words available · ") + root.questionCount + " questions selected"
            color: "#555555"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: 15
            lineHeight: 1.15
        }

        Button {
            id: startButton

            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: "Start " + root.quizTitle
            font.pixelSize: 17
            font.weight: Font.ExtraBold
            onClicked: root.startQuiz()

            contentItem: Text {
                text: startButton.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: startButton.font
                elide: Text.ElideRight
            }

            background: Rectangle {
                radius: 16
                color: root.accent
            }
        }
    }

    component SetupSection: ColumnLayout {
        id: section

        property string title: ""

        Layout.fillWidth: true
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: section.title.toUpperCase()
            color: "#666666"
            font.pixelSize: 12
            font.weight: Font.ExtraBold
            font.letterSpacing: 0.5
            elide: Text.ElideRight
        }
    }
}
