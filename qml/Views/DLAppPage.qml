pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Felgo

Page {
    id: root

    default property alias pageContent: contentColumn.data

    property string activeTab: ""
    property bool showHeader: title.length > 0
    property int pagePadding: 20
    property int pageTopPadding: 18
    property int pageBottomPadding: 6
    property int navigationBarHeight: 64

    readonly property real safeAreaTop: root.safeAreaInset("top")
    readonly property real safeAreaBottom: root.safeAreaInset("bottom")

    readonly property color bg: "#f6f7fb"
    readonly property color cardBg: "#ffffff"
    readonly property color textMain: "#111827"
    readonly property color textMuted: "#6b7280"
    readonly property color line: "#e5e7eb"
    readonly property color blue: "#337fe6"

    background: Rectangle {
        color: root.bg
    }

    function safeAreaInset(edge) {
        var insets = NativeUtils.safeAreaInsets;
        if (insets && insets[edge] !== undefined && insets[edge] !== null) {
            var inset = Number(insets[edge]);
            return isNaN(inset) ? 0 : Math.max(0, inset);
        }

        return 0;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            visible: root.showHeader
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? root.safeAreaTop + 72 : 0

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
            contentHeight: contentColumn.y + contentColumn.implicitHeight + root.pageBottomPadding
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
                    topMargin: root.showHeader ? root.pageTopPadding : root.safeAreaTop + root.pageTopPadding
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.navigationBarHeight + root.safeAreaBottom
            color: root.cardBg
            border.color: root.line
            border.width: 1

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 7
                    rightMargin: 7
                    topMargin: 6
                    bottomMargin: root.safeAreaBottom
                }
                spacing: 0

                Repeater {
                    model: [
                        {
                            key: "words",
                            label: "Words",
                            icon: Qt.resolvedUrl("../../assets/icons/words_list_icon.svg")
                        },
                        {
                            key: "groups",
                            label: "Groups",
                            icon: Qt.resolvedUrl("../../assets/icons/groups_icon.svg")
                        },
                        {
                            key: "add",
                            label: "Add",
                            icon: Qt.resolvedUrl("../../assets/icons/add_new_word_icon.svg")
                        },
                        {
                            key: "quiz",
                            label: "Quiz",
                            icon: Qt.resolvedUrl("../../assets/icons/quiz_icon.svg")
                        },
                        {
                            key: "settings",
                            label: "Settings",
                            icon: Qt.resolvedUrl("../../assets/icons/settings_icon.svg")
                        }
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
                            height: 52
                            radius: 16
                            color: tabDelegate.selected ? Qt.rgba(51 / 255, 127 / 255, 230 / 255, 0.12) : "transparent"
                        }

                        Column {
                            anchors.centerIn: parent
                            width: parent.width
                            spacing: 3

                            IconImage {
                                anchors.horizontalCenter: parent.horizontalCenter
                                source: tabDelegate.modelData.icon
                                width: 24
                                height: 24
                                color: tabDelegate.selected ? root.blue : root.textMuted
                                opacity: tabDelegate.selected ? 1.0 : 0.82
                            }

                            Text {
                                text: tabDelegate.modelData.label
                                color: tabDelegate.selected ? root.blue : root.textMuted
                                font.pixelSize: 11
                                font.weight: Font.Bold
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (tabDelegate.modelData.key === "words") {
                                    appStateManager.goWordsPage();
                                } else if (tabDelegate.modelData.key === "groups") {
                                    appStateManager.goGroupsPage();
                                } else if (tabDelegate.modelData.key === "add") {
                                    appStateManager.goAddEditWordPage();
                                } else if (tabDelegate.modelData.key === "quiz") {
                                    appStateManager.goQuizHomePage();
                                } else if (tabDelegate.modelData.key === "settings") {
                                    appStateManager.goSettingsPage();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
