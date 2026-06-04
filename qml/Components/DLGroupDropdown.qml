pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: root

    property var model: []
    property int currentIndex: 0
    property color cardBg: "#ffffff"
    property color fieldBg: "#f1f4f9"
    property color textMain: "#111827"
    property color textMuted: "#6b7280"
    property color line: "#e5e7eb"
    property color accent: "#337fe6"
    property string emptyText: "No group"

    signal activated(int index)

    implicitHeight: 52

    function optionName(index) {
        if (index >= 0 && index < model.length && model[index] && model[index].name !== undefined) {
            return model[index].name
        }

        return emptyText
    }

    Rectangle {
        id: controlBg

        anchors.fill: parent
        radius: 16
        color: root.cardBg
        border.color: dropdownPopup.visible ? root.accent : root.line
        border.width: dropdownPopup.visible ? 2 : 1

        Behavior on border.color {
            ColorAnimation {
                duration: 140
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: dropdownPopup.visible ? dropdownPopup.close() : dropdownPopup.open()
    }

    Text {
        anchors {
            left: parent.left
            right: chevron.left
            verticalCenter: parent.verticalCenter
            leftMargin: 16
            rightMargin: 12
        }
        text: root.optionName(root.currentIndex)
        color: root.textMain
        font.pixelSize: 15
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    Item {
        id: chevron

        anchors {
            right: parent.right
            verticalCenter: parent.verticalCenter
            rightMargin: 16
        }
        width: 16
        height: 16
        rotation: dropdownPopup.visible ? 180 : 0

        Behavior on rotation {
            NumberAnimation {
                duration: 140
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            width: 9
            height: 2
            radius: 1
            color: root.textMuted
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: -3
            rotation: 45
        }

        Rectangle {
            width: 9
            height: 2
            radius: 1
            color: root.textMuted
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 3
            rotation: -45
        }
    }

    Popup {
        id: dropdownPopup

        x: 0
        y: root.height + 6
        width: root.width
        padding: 0
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: 120
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                property: "scale"
                from: 0.98
                to: 1
                duration: 120
                easing.type: Easing.OutCubic
            }
        }

        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: 90
                easing.type: Easing.InCubic
            }
        }

        contentItem: ListView {
            id: optionList

            implicitHeight: Math.min(contentHeight, 236)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.model
            currentIndex: root.currentIndex

            ScrollBar.vertical: ScrollBar {
                policy: optionList.contentHeight > optionList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            delegate: ItemDelegate {
                id: optionDelegate

                required property int index
                required property var modelData

                width: optionList.width
                height: 46
                highlighted: root.currentIndex === optionDelegate.index

                onClicked: {
                    root.currentIndex = optionDelegate.index
                    root.activated(optionDelegate.index)
                    dropdownPopup.close()
                }

                contentItem: Row {
                    spacing: 10

                    Text {
                        width: parent.width - selectedDot.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: optionDelegate.modelData.name
                        color: optionDelegate.highlighted ? root.accent : root.textMain
                        font.pixelSize: 14
                        font.weight: optionDelegate.highlighted ? Font.ExtraBold : Font.DemiBold
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    Rectangle {
                        id: selectedDot

                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        visible: optionDelegate.highlighted
                        color: root.accent
                    }
                }

                background: Rectangle {
                    color: optionDelegate.highlighted ? Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.10) : root.cardBg
                }
            }
        }

        background: Rectangle {
            radius: 16
            color: root.cardBg
            border.color: root.line
            border.width: 1
        }
    }
}
