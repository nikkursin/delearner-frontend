import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: root

    readonly property color bg: "#f6f7fb"
    readonly property color textMain: "#111827"
    readonly property color textMuted: "#6b7280"
    readonly property color line: "#d1d5db"
    readonly property color blue: "#3478f6"
    readonly property color errorText: "#b91c1c"

    background: Rectangle { color: root.bg }

    Connections {
        target: appStateManager
        function onLastErrorChanged() {
            errorLabel.text = appStateManager.lastError
        }
    }

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            margins: 24
        }
        spacing: 14

        Text {
            Layout.fillWidth: true
            text: "Sign in"
            color: root.textMain
            font.pixelSize: 28
            font.weight: Font.Bold
        }

        Text {
            Layout.fillWidth: true
            text: "An account is required before this device can use DE Vocab Learner."
            color: root.textMuted
            font.pixelSize: 14
            wrapMode: Text.WordWrap
        }

        TextField {
            id: emailField
            Layout.fillWidth: true
            placeholderText: "Email"
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            enabled: !appStateManager.authBusy
        }

        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "Password"
            echoMode: TextInput.Password
            enabled: !appStateManager.authBusy
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                Layout.fillWidth: true
                text: appStateManager.authBusy ? "Signing in..." : "Sign in"
                enabled: !appStateManager.authBusy
                onClicked: appStateManager.signIn(emailField.text, passwordField.text)
            }

            Button {
                Layout.fillWidth: true
                text: "Create account"
                enabled: !appStateManager.authBusy
                onClicked: appStateManager.registerAccount(emailField.text, passwordField.text)
            }
        }

        Text {
            id: errorLabel
            Layout.fillWidth: true
            text: appStateManager.lastError
            color: root.errorText
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            visible: text.length > 0
        }
    }
}
