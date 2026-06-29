pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property string titleText: ""
    property string messageText: ""
    property string primaryText: "OK"
    property string secondaryText: "Cancel"
    property bool destructive: false
    property bool showTextInput: false
    property string inputPlaceholderText: ""
    property bool showPrimaryButton: true
    property bool showSecondaryButton: true
    property color cardBg: "#ffffff"
    property color fieldBg: "#f1f4f9"
    property color textMain: "#111827"
    property color textMuted: "#6b7280"
    property color line: "#e5e7eb"
    property color primaryColor: "#337fe6"
    property color destructiveColor: "#dc3545"
    property alias customContent: contentSlot.data
    property alias inputText: textInputField.text
    property alias inputField: textInputField

    signal primaryClicked()
    signal secondaryClicked()

    parent: Overlay.overlay
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    width: Math.max(280, Math.min((parent ? parent.width : 360) - 36, 380))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    Overlay.modal: Rectangle {
        color: Qt.rgba(17 / 255, 24 / 255, 39 / 255, 0.42)
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 130
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            property: "scale"
            from: 0.98
            to: 1
            duration: 130
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: 100
            easing.type: Easing.InCubic
        }
    }

    background: Rectangle {
        radius: 12
        color: root.cardBg
        border.color: root.line
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: 14

        Text {
            visible: root.titleText.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 18
            text: root.titleText
            color: root.textMain
            font.pixelSize: 21
            font.weight: Font.ExtraBold
            wrapMode: Text.WordWrap
        }

        Text {
            visible: root.messageText.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            text: root.messageText
            color: root.textMuted
            font.pixelSize: 14
            lineHeight: 1.25
            wrapMode: Text.WordWrap
        }

        TextField {
            id: textInputField

            visible: root.showTextInput
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            placeholderText: root.inputPlaceholderText
            font.pixelSize: 15
            color: root.textMain
            selectByMouse: true

            background: Rectangle {
                implicitHeight: 48
                radius: 8
                color: root.fieldBg
                border.color: root.line
                border.width: 1
            }
        }

        ColumnLayout {
            id: contentSlot

            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            spacing: 14
        }

        RowLayout {
            visible: root.showPrimaryButton || root.showSecondaryButton
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.bottomMargin: 18
            Layout.topMargin: 2
            spacing: 10

            Button {
                id: secondaryButton

                visible: root.showSecondaryButton
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                text: root.secondaryText
                font.pixelSize: 14
                font.weight: Font.Bold
                padding: 0

                onClicked: {
                    root.secondaryClicked()
                    root.close()
                }

                contentItem: Text {
                    text: secondaryButton.text
                    color: root.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: secondaryButton.font
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                background: Rectangle {
                    radius: 8
                    color: root.fieldBg
                    border.color: root.line
                    border.width: 1
                }
            }

            Button {
                id: primaryButton

                visible: root.showPrimaryButton
                Layout.fillWidth: true
                Layout.preferredHeight: 46
                text: root.primaryText
                font.pixelSize: 14
                font.weight: Font.ExtraBold
                padding: 0

                onClicked: root.primaryClicked()

                contentItem: Text {
                    text: primaryButton.text
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font: primaryButton.font
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                background: Rectangle {
                    radius: 8
                    color: root.destructive ? root.destructiveColor : root.primaryColor
                }
            }
        }
    }
}
