import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    // ─── State ───────────────────────────────────────────────────────────────
    // Possible values: "loading" | "error"
    property string state: "loading"
    property string errorMessage: ""

    // Steps: each has a label and a status: "pending" | "active" | "done"
    property var steps: [
        { label: "Opening local database",  status: "done"    },
        { label: "Checking migrations",     status: "done"    },
        { label: "Loading words and groups", status: "active" }
    ]

    // ─── Signals ─────────────────────────────────────────────────────────────
    signal readyToNavigate()

    // ─── Palette ─────────────────────────────────────────────────────────────
    readonly property color bg:       "#f6f7fb"
    readonly property color cardBg:   "#ffffff"
    readonly property color textMain: "#111827"
    readonly property color textMuted:"#6b7280"
    readonly property color line:     "#e5e7eb"
    readonly property color blue:     "#3478f6"
    readonly property color blueSoft: Qt.rgba(52/255, 120/255, 246/255, 0.12)
    readonly property color errorBg:  Qt.rgba(239/255, 68/255, 68/255, 0.08)
    readonly property color errorText:"#b91c1c"
    readonly property color dotPending:"#c8d0dc"

    background: Rectangle { color: root.bg }

    // ─── Layout ──────────────────────────────────────────────────────────────
    ColumnLayout {
        anchors {
            top: parent.top
            bottom: versionLabel.top
            left: parent.left
            right: parent.right
            topMargin: 28
            bottomMargin: 12
            leftMargin: 22
            rightMargin: 22
        }
        spacing: 0

        // Vertical centering spacer
        Item { Layout.fillHeight: true }

        // ── Logo ─────────────────────────────────────────────────────────────
        Item {
            Layout.alignment: Qt.AlignHCenter
            width: 142; height: 142          // 126 + 2×8 for the outer ring

            // Outer subtle ring
            Rectangle {
                anchors.fill: parent
                radius: 42
                color: "transparent"
                border.color: Qt.rgba(52/255, 120/255, 246/255, 0.12)
                border.width: 1
            }

            // Inner glow container
            Rectangle {
                anchors.centerIn: parent
                width: 126; height: 126
                radius: 36
                gradient: Gradient {
                    orientation: Gradient.Diagonal
                    GradientStop { position: 0.0; color: Qt.rgba(52/255,120/255,246/255,0.16) }
                    GradientStop { position: 1.0; color: Qt.rgba(52/255,120/255,246/255,0.06) }
                }
                border.color: Qt.rgba(52/255, 120/255, 246/255, 0.12)
                border.width: 1

                layer.enabled: true
                layer.effect: null   // replace with MultiEffect drop shadow if available

                // Blue logo tile
                Rectangle {
                    anchors.centerIn: parent
                    width: 78; height: 78
                    radius: 24
                    color: root.blue

                    Text {
                        anchors.centerIn: parent
                        text: "DE"
                        color: "white"
                        font { pixelSize: 29; weight: Font.ExtraBold; letterSpacing: -1 }
                    }
                }
            }
        }

        // ── App title ────────────────────────────────────────────────────────
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 28
            text: "DE Vocab Learner"
            color: root.textMain
            font { pixelSize: 32; weight: Font.ExtraBold; letterSpacing: -0.7 }
        }

        // ── Subtitle ─────────────────────────────────────────────────────────
        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 10
            Layout.maximumWidth: 270
            text: "Preparing your vocabulary database and loading the app."
            color: root.textMuted
            font.pixelSize: 15
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.4
        }

        // ── Status card ───────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 42
            radius: 22
            color: root.cardBg
            border.color: Qt.rgba(229/255,231/255,235/255,0.75)
            border.width: 1
            implicitHeight: statusCardColumn.implicitHeight + 40   // 20px top+bottom padding

            ColumnLayout {
                id: statusCardColumn
                anchors {
                    top: parent.top; left: parent.left; right: parent.right
                    topMargin: 20; leftMargin: 18; rightMargin: 18; bottomMargin: 20
                }
                spacing: 0

                // Spinner row
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12

                    // CSS-style spinning ring
                    Item {
                        width: 24; height: 24
                        visible: root.state === "loading"

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: "transparent"
                            border.color: Qt.rgba(52/255,120/255,246/255,0.18)
                            border.width: 3
                        }
                        Rectangle {
                            id: spinnerArc
                            anchors.fill: parent
                            radius: width / 2
                            color: "transparent"
                            border.color: root.blue
                            border.width: 3
                            // Masking trick: only a quarter arc visible via clipping
                            clip: true
                            Rectangle {
                                width: parent.width / 2
                                height: parent.height
                                color: root.cardBg
                                anchors.right: parent.right
                            }
                        }
                        RotationAnimation on rotation {
                            running: root.state === "loading"
                            from: 0; to: 360
                            duration: 900
                            loops: Animation.Infinite
                        }
                    }

                    Text {
                        text: root.state === "loading" ? "Starting app..." : "Startup failed"
                        color: root.state === "error" ? root.errorText : root.textMain
                        font { pixelSize: 16; weight: Font.Bold }
                    }
                }

                // Steps list
                ColumnLayout {
                    Layout.topMargin: 18
                    spacing: 10

                    Repeater {
                        model: root.steps

                        RowLayout {
                            spacing: 10

                            // Status dot
                            Rectangle {
                                width: 8; height: 8
                                radius: 4
                                color: {
                                    if (modelData.status === "done" || modelData.status === "active")
                                        return root.blue
                                    return root.dotPending
                                }
                            }

                            Text {
                                text: modelData.label
                                font.pixelSize: 13
                                color: {
                                    if (modelData.status === "done")  return root.textMain
                                    if (modelData.status === "active") return root.blue
                                    return root.textMuted
                                }
                                font.weight: modelData.status === "active" ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }
            }
        }

        // ── Error banner (hidden unless state === "error") ────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 14
            visible: root.state === "error"
            radius: 16
            color: root.errorBg
            implicitHeight: errorText.implicitHeight + 24

            Text {
                id: errorText
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
                text: root.errorMessage.length > 0
                      ? root.errorMessage
                      : "Startup error message would appear here if database initialization fails."
                color: root.errorText
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                lineHeight: 1.35
            }
        }

        // Vertical centering spacer
        Item { Layout.fillHeight: true }
    }

    // ── Version label ─────────────────────────────────────────────────────────
    Text {
        id: versionLabel
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 24 }
        text: "MVP 1 · Local database mode"
        color: "#a3aab7"
        font.pixelSize: 12
    }
}
