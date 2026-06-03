pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "quiz"

    property string quizType: ""
    property string quizTitle: "Quiz"
    property var groupOptions: [{ id: -1, name: "All words" }]
    property int selectedGroupId: -1
    property int questionCount: 10
    property int availableCount: 0
    property string errorMessage: ""

    readonly property color fieldBg: "#f1f4f9"
    readonly property color green: "#35a969"
    readonly property color red: "#dc3545"
    readonly property color redSoft: Qt.rgba(220 / 255, 53 / 255, 69 / 255, 0.10)

    readonly property var countOptions: [5, 10, 15, 20]

    Component.onCompleted: reloadSetup()

    function reloadSetup() {
        quizType = appStateManager.selectedQuizType()
        if (quizType.length === 0) {
            quizType = "translation"
        }

        quizTitle = quizType === "article" ? "Article Quiz" : "Translation Quiz"
        loadGroups()
        refreshAvailability()
    }

    function loadGroups() {
        var groups = appStateManager.availableGroups()
        var options = [{ id: -1, name: "All words" }]

        for (var i = 0; i < groups.length; ++i) {
            options.push({
                id: groups[i].id,
                name: groups[i].name
            })
        }

        groupOptions = options
        groupCombo.currentIndex = 0
        selectedGroupId = -1
    }

    function refreshAvailability() {
        availableCount = appStateManager.availableQuizQuestionCount(quizType, selectedGroupId)

        questionCount = Math.max(1, Math.min(questionCount, Math.max(1, availableCount)))
        errorMessage = ""
        appStateManager.canStartQuiz(quizType, selectedGroupId, questionCount)
        if (appStateManager.lastError) {
            errorMessage = appStateManager.lastError
        }
    }

    function selectQuestionCount(count) {
        questionCount = count
        errorMessage = ""
        appStateManager.canStartQuiz(quizType, selectedGroupId, questionCount)
        if (appStateManager.lastError) {
            errorMessage = appStateManager.lastError
        }
    }

    function startQuiz() {
        if (!appStateManager.startQuiz(quizType, selectedGroupId, questionCount)) {
            errorMessage = appStateManager.lastError || "Unable to start quiz."
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 58

        Text {
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
            }
            text: "Quiz"
            color: root.blue
            font.pixelSize: 15
            font.weight: Font.Bold

            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                onClicked: appStateManager.goQuizHomePage()
            }
        }

        Text {
            anchors.centerIn: parent
            text: "Setup"
            color: root.textMain
            font.pixelSize: 17
            font.weight: Font.ExtraBold
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 14

        Text {
            Layout.fillWidth: true
            text: root.quizTitle
            color: root.textMain
            font.pixelSize: 30
            font.weight: Font.ExtraBold
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: root.quizType === "article"
                  ? "Practice German noun articles."
                  : "Practice German-to-native translations."
            color: root.textMuted
            font.pixelSize: 15
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
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

        SetupBlock {
            label: "Scope"

            ComboBox {
                id: groupCombo

                Layout.fillWidth: true
                Layout.preferredHeight: 46
                model: root.groupOptions
                textRole: "name"

                onActivated: function(index) {
                    root.selectedGroupId = root.groupOptions[index].id
                    root.refreshAvailability()
                }

                background: Rectangle {
                    radius: 8
                    color: root.cardBg
                    border.color: root.line
                    border.width: 1
                }
            }
        }

        SetupBlock {
            label: "Questions"

            SpinBox {
                id: questionSpinBox

                Layout.fillWidth: true
                Layout.preferredHeight: 46
                from: 1
                to: Math.max(1, root.availableCount)
                value: root.questionCount
                editable: true
                onValueModified: root.selectQuestionCount(value)

                background: Rectangle {
                    radius: 8
                    color: root.cardBg
                    border.color: root.line
                    border.width: 1
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 10

                Repeater {
                    model: root.countOptions

                    Button {
                        id: countButton

                        required property int modelData

                        width: 72
                        height: 42
                        enabled: modelData <= Math.max(1, root.availableCount)
                        text: modelData.toString()
                        font.pixelSize: 14
                        font.weight: Font.ExtraBold
                        onClicked: root.selectQuestionCount(modelData)

                        contentItem: Text {
                            text: countButton.text
                            color: countButton.enabled
                                   ? (root.questionCount === countButton.modelData ? "white" : root.blue)
                                   : root.textMuted
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font: countButton.font
                        }

                        background: Rectangle {
                            radius: 8
                            color: root.questionCount === countButton.modelData && countButton.enabled
                                   ? root.blue
                                   : root.cardBg
                            border.color: countButton.enabled ? root.blue : root.line
                            border.width: 1
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            radius: 8
            color: root.cardBg
            border.color: root.line
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: root.availableCount + " available in selected scope"
                color: root.textMuted
                font.pixelSize: 14
                font.weight: Font.Bold
            }
        }

        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            text: "Start Quiz"
            font.pixelSize: 15
            font.weight: Font.ExtraBold
            onClicked: root.startQuiz()

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

    component SetupBlock: ColumnLayout {
        id: setupBlock

        property string label: ""

        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: setupBlock.label
            color: root.textMain
            font.pixelSize: 14
            font.weight: Font.ExtraBold
        }
    }
}
