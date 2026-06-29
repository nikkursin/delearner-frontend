pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"

DLAppPage {
    id: root

    title: ""
    showHeader: false
    activeTab: "groups"

    property var groupsModel: []
    property int editingGroupId: -1
    property int pendingDeleteGroupId: -1
    property string pendingDeleteGroupName: ""
    property string errorMessage: ""
    property string dialogErrorMessage: ""
    property string selectedColor: "#337fe6"

    readonly property color fieldBg: "#f1f4f9"
    readonly property color green: "#35a969"
    readonly property color greenSoft: Qt.rgba(53 / 255, 169 / 255, 105 / 255, 0.12)
    readonly property color red: "#dc3545"
    readonly property color redSoft: Qt.rgba(220 / 255, 53 / 255, 69 / 255, 0.10)

    readonly property var colorOptions: [
        "#337fe6",
        "#35a969",
        "#e94f72",
        "#e27a34",
        "#7c3aed",
        "#0891b2"
    ]

    Component.onCompleted: reloadGroups()

    Connections {
        target: appStateManager

        function onGroupsChanged() {
            root.reloadGroups()
        }
    }

    function reloadGroups() {
        groupsModel = appStateManager.availableGroups()
        errorMessage = appStateManager.lastError || ""
    }

    function groupCountText() {
        var groupCount = groupsModel.length
        var wordCount = 0

        for (var i = 0; i < groupsModel.length; ++i) {
            wordCount += Number(groupsModel[i].word_count || 0)
        }

        return groupCount + " " + (groupCount === 1 ? "group" : "groups")
                + " with " + wordCount + " " + (wordCount === 1 ? "word" : "words")
    }

    function groupColor(group) {
        var colorValue = group && group.color_hex ? group.color_hex.toString() : ""
        if (colorValue.length === 4 || colorValue.length === 7) {
            return colorValue.charAt(0) === "#" ? colorValue : "#337fe6"
        }

        return "#337fe6"
    }

    function openCreateDialog() {
        editingGroupId = -1
        dialogErrorMessage = ""
        selectedColor = colorOptions[0]
        groupNameField.text = ""
        groupDialog.titleText = "New Group"
        groupDialog.open()
        groupNameField.forceActiveFocus()
    }

    function openEditDialog(group) {
        editingGroupId = Number(group.id || -1)
        dialogErrorMessage = ""
        selectedColor = groupColor(group)
        groupNameField.text = group.name || ""
        groupDialog.titleText = "Edit Group"
        groupDialog.open()
        groupNameField.forceActiveFocus()
        groupNameField.selectAll()
    }

    function saveGroup() {
        var name = groupNameField.text.trim()
        if (name.length === 0) {
            dialogErrorMessage = "Group name is required."
            groupNameField.forceActiveFocus()
            return
        }

        var success = false
        if (editingGroupId > 0) {
            success = appStateManager.updateGroup(editingGroupId, name, selectedColor)
        } else {
            success = appStateManager.createGroup(name, selectedColor) > 0
        }

        if (!success) {
            dialogErrorMessage = appStateManager.lastError || "Unable to save this group."
            return
        }

        groupDialog.close()
        reloadGroups()
    }

    function confirmDelete(group) {
        pendingDeleteGroupId = Number(group.id || -1)
        pendingDeleteGroupName = group.name || ""
        deleteGroupDialog.open()
    }

    function deletePendingGroup() {
        if (!appStateManager.deleteGroup(pendingDeleteGroupId)) {
            errorMessage = appStateManager.lastError || "Unable to delete this group."
            return
        }

        pendingDeleteGroupId = -1
        pendingDeleteGroupName = ""
        reloadGroups()
    }

    DLCustomPopup {
        id: groupDialog

        cardBg: root.cardBg
        fieldBg: root.fieldBg
        textMain: root.textMain
        textMuted: root.textMuted
        line: root.line
        primaryColor: root.blue
        destructiveColor: root.red
        primaryText: "Save"
        secondaryText: "Cancel"
        onPrimaryClicked: root.saveGroup()

        customContent: [
            Text {
                visible: root.dialogErrorMessage.length > 0
                Layout.fillWidth: true
                text: root.dialogErrorMessage
                color: root.red
                wrapMode: Text.WordWrap
                font.pixelSize: 14
                font.weight: Font.DemiBold
            },
            FieldBlock {
                label: "Name"

                TextField {
                    id: groupNameField

                    Layout.fillWidth: true
                    placeholderText: "Travel"
                    text: ""
                    font.pixelSize: 15
                    color: root.textMain
                    selectByMouse: true
                    background: FieldBackground {}
                    onTextChanged: if (root.dialogErrorMessage.length > 0) root.dialogErrorMessage = ""
                    onAccepted: root.saveGroup()
                }
            },
            FieldBlock {
                label: "Color"

                Flow {
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: root.colorOptions

                        Rectangle {
                            id: colorDelegate

                            required property var modelData

                            width: 38
                            height: 38
                            radius: 19
                            color: colorDelegate.modelData
                            border.color: root.selectedColor === colorDelegate.modelData ? root.textMain : "transparent"
                            border.width: 3

                            Rectangle {
                                anchors.centerIn: parent
                                width: 16
                                height: 16
                                radius: 8
                                visible: root.selectedColor === colorDelegate.modelData
                                color: "white"
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.selectedColor = colorDelegate.modelData
                            }
                        }
                    }
                }
            }
        ]
    }

    DLCustomPopup {
        id: deleteGroupDialog

        titleText: "Delete group?"
        messageText: "This deletes \"" + root.pendingDeleteGroupName + "\". Words in this group will remain saved without a group."
        primaryText: "Delete"
        secondaryText: "Cancel"
        destructive: true
        cardBg: root.cardBg
        fieldBg: root.fieldBg
        textMain: root.textMain
        textMuted: root.textMuted
        line: root.line
        primaryColor: root.blue
        destructiveColor: root.red

        onPrimaryClicked: {
            root.deletePendingGroup()
            deleteGroupDialog.close()
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: 92

        Column {
            anchors {
                left: parent.left
                right: addButton.left
                verticalCenter: parent.verticalCenter
                rightMargin: 12
            }
            spacing: 5

            Text {
                width: parent.width
                text: "Groups"
                color: root.textMain
                font.pixelSize: 34
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: root.groupCountText()
                color: root.textMuted
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }

        Button {
            id: addButton

            anchors {
                right: parent.right
                top: parent.top
                topMargin: 24
            }
            width: 44
            height: 44
            text: "+"
            font.pixelSize: 26
            font.weight: Font.Bold
            onClicked: root.openCreateDialog()

            contentItem: Text {
                text: addButton.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: addButton.font
            }

            background: Rectangle {
                radius: 8
                color: root.blue
            }
        }
    }

    Text {
        visible: root.errorMessage.length > 0
        Layout.fillWidth: true
        Layout.bottomMargin: 14
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
            radius: 8
            color: root.redSoft
        }
    }

    ColumnLayout {
        visible: root.groupsModel.length > 0
        Layout.fillWidth: true
        spacing: 10

        Repeater {
            model: root.groupsModel

            GroupCard {
                id: groupDelegate

                required property var modelData

                Layout.fillWidth: true
                group: groupDelegate.modelData
            }
        }
    }

    EmptyState {
        visible: root.groupsModel.length === 0
        Layout.fillWidth: true
        Layout.topMargin: 42
        titleText: "No groups yet"
        bodyText: "Create your first group to organize vocabulary by topic, trip, course, or goal."
        primaryText: "Add Group"
        onPrimaryClicked: root.openCreateDialog()
    }

    component GroupCard: Rectangle {
        id: card

        property var group: ({})

        Layout.preferredHeight: 104
        radius: 8
        color: root.cardBg
        border.color: root.line
        border.width: 1

        RowLayout {
            anchors {
                fill: parent
                margins: 14
            }
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 12
                Layout.fillHeight: true
                radius: 6
                color: root.groupColor(card.group)
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: card.group.name || ""
                    color: root.textMain
                    font.pixelSize: 19
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: Number(card.group.word_count || 0)
                          + " " + (Number(card.group.word_count || 0) === 1 ? "word" : "words")
                    color: root.textMuted
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 8

                SmallActionButton {
                    label: "Edit"
                    textColor: root.textMuted
                    backgroundColor: root.fieldBg
                    onTriggered: root.openEditDialog(card.group)
                }

                SmallActionButton {
                    label: "Delete"
                    textColor: root.red
                    backgroundColor: root.redSoft
                    onTriggered: root.confirmDelete(card.group)
                }
            }
        }
    }

    component SmallActionButton: Button {
        id: action

        signal triggered()

        property string label: ""
        property color textColor: root.textMuted
        property color backgroundColor: root.fieldBg

        Layout.preferredWidth: 58
        Layout.preferredHeight: 34
        text: label
        font.pixelSize: 12
        font.weight: Font.Bold
        padding: 0
        onClicked: action.triggered()

        contentItem: Text {
            text: action.text
            color: action.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: action.font
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        background: Rectangle {
            radius: 8
            color: action.backgroundColor
        }
    }

    component FieldBlock: ColumnLayout {
        property string label: ""

        Layout.fillWidth: true
        spacing: 7

        Text {
            Layout.leftMargin: 4
            text: parent.label
            color: root.textMuted
            font.pixelSize: 12
            font.weight: Font.ExtraBold
            font.capitalization: Font.AllUppercase
        }
    }

    component FieldBackground: Rectangle {
        implicitHeight: 48
        radius: 8
        color: root.fieldBg
        border.color: root.line
        border.width: 1
    }

    component EmptyState: ColumnLayout {
        signal primaryClicked()

        property string titleText: ""
        property string bodyText: ""
        property string primaryText: ""

        spacing: 14

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 76
            Layout.preferredHeight: 76
            radius: 8
            color: Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.12)

            Text {
                anchors.centerIn: parent
                text: "+"
                color: root.blue
                font.pixelSize: 34
                font.weight: Font.ExtraBold
            }
        }

        Text {
            Layout.fillWidth: true
            text: parent.titleText
            color: root.textMain
            font.pixelSize: 22
            font.weight: Font.ExtraBold
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            text: parent.bodyText
            color: root.textMuted
            font.pixelSize: 14
            lineHeight: 1.25
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Button {
            id: emptyActionButton

            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 164
            Layout.preferredHeight: 48
            text: parent.primaryText
            onClicked: parent.primaryClicked()

            contentItem: Text {
                text: emptyActionButton.text
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 15
                font.weight: Font.ExtraBold
            }

            background: Rectangle {
                radius: 8
                color: root.blue
            }
        }
    }
}
