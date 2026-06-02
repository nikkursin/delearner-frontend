pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    default property alias pageContent: contentColumn.data

    property string activeTab: ""
    property bool showHeader: title.length > 0
    property int pagePadding: 18

    readonly property color bg: "#f6f7fb"
    readonly property color cardBg: "#ffffff"
    readonly property color textMain: "#111827"
    readonly property color textMuted: "#6b7280"
    readonly property color line: "#e5e7eb"
    readonly property color blue: "#337fe6"

    background: Rectangle {
        color: root.bg
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            visible: root.showHeader
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 72 : 0

            Text {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                    leftMargin: root.pagePadding
                    rightMargin: root.pagePadding
                    bottomMargin: 14
                }
                text: root.title
                color: root.textMain
                font.pixelSize: 28
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }
        }

        Flickable {
            id: flickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: contentColumn.implicitHeight + root.pagePadding
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            ColumnLayout {
                id: contentColumn
                width: flickable.width
                spacing: 0
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: root.pagePadding
                    rightMargin: root.pagePadding
                    top: parent.top
                    topMargin: root.showHeader ? 0 : root.pagePadding
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            color: root.cardBg
            border.color: root.line
            border.width: 1

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 7
                    rightMargin: 7
                    topMargin: 7
                    bottomMargin: 10
                }
                spacing: 0

                Repeater {
                    model: [
                        { key: "words", label: "Words" },
                        { key: "groups", label: "Groups" },
                        { key: "add", label: "Add" },
                        { key: "quiz", label: "Quiz" },
                        { key: "settings", label: "Settings" }
                    ]

                    delegate: Item {
                        id: tabDelegate

                        required property var modelData

                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        readonly property bool selected: root.activeTab === tabDelegate.modelData.key

                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 4, 64)
                            height: 48
                            radius: 16
                            color: tabDelegate.selected ? Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.12) : "transparent"
                        }

                        Text {
                            anchors.centerIn: parent
                            text: tabDelegate.modelData.label
                            color: tabDelegate.selected ? root.blue : root.textMuted
                            font.pixelSize: 12
                            font.weight: Font.Bold
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            width: parent.width
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (tabDelegate.modelData.key === "words") {
                                    appStateManager.goWordsPage()
                                } else if (tabDelegate.modelData.key === "groups") {
                                    appStateManager.goGroupsPage()
                                } else if (tabDelegate.modelData.key === "add") {
                                    appStateManager.goAddEditWordPage()
                                } else if (tabDelegate.modelData.key === "quiz") {
                                    appStateManager.goQuizHomePage()
                                } else if (tabDelegate.modelData.key === "settings") {
                                    appStateManager.goSettingsPage()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
