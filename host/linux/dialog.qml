import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Controls.ApplicationWindow {
    id: root

    // the last argument is the file that describes the dialog, read again while it shows progress.
    readonly property string source: Qt.application.arguments[Qt.application.arguments.length - 1]
    property var view: ({kind: "", title: "", heading: "", text: "", buttons: [], permille: -1})
    readonly property bool progress: view.kind === "progress"
    property bool answered: false

    function load() {
        const request = new XMLHttpRequest()
        request.onreadystatechange = () => {
            if (request.readyState !== XMLHttpRequest.DONE)
                return
            try {
                root.view = JSON.parse(request.responseText)
            } catch (e) {
                if (!root.visible)
                    Qt.exit(1)
                return
            }
            root.visible = true
        }
        request.open("GET", "file://" + source)
        request.send()
    }

    function answer(index) {
        answered = true
        Qt.exit(10 + index)
    }

    title: view.title
    flags: progress ? Qt.Dialog | Qt.CustomizeWindowHint | Qt.WindowTitleHint : Qt.Dialog
    width: Kirigami.Units.gridUnit * 26
    readonly property int margin: Kirigami.Units.largeSpacing * 2
    height: column.implicitHeight + margin * 2
    minimumWidth: width
    maximumWidth: width
    minimumHeight: column.implicitHeight + margin * 2
    maximumHeight: column.implicitHeight + margin * 2
    visible: false

    onClosing: close => {
        if (progress)
            close.accepted = false
        else if (!answered)
            Qt.exit(9)
    }

    Component.onCompleted: load()

    Timer {
        interval: 100
        repeat: true
        running: root.progress
        onTriggered: root.load()
    }

    Shortcut {
        sequence: "Escape"
        enabled: !root.progress
        onActivated: root.view.buttons.length >= 2 ? root.answer(root.view.buttons.length - 1) : Qt.exit(9)
    }

    ColumnLayout {
        id: column

        x: root.margin
        y: root.margin
        width: root.width - root.margin * 2
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            spacing: Kirigami.Units.largeSpacing * 2

            Kirigami.Icon {
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: Kirigami.Units.iconSizes.large
                Layout.preferredHeight: Kirigami.Units.iconSizes.large
                visible: !root.progress
                source: root.view.kind === "error" ? "dialog-error" : root.view.kind === "ask" ? "dialog-question" : "dialog-information"
            }

            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 4
                    font.weight: Font.DemiBold
                    text: root.view.heading
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    visible: text.length > 0
                }

                Controls.Label {
                    Layout.fillWidth: true
                    text: root.view.text
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    visible: text.length > 0
                }
            }
        }

        Controls.ProgressBar {
            Layout.fillWidth: true
            visible: root.progress
            from: 0
            to: 1000
            value: root.view.permille
            indeterminate: root.view.permille < 0
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing
            visible: !root.progress

            Repeater {
                model: root.view.buttons

                Controls.Button {
                    required property int index
                    required property string modelData

                    text: modelData
                    highlighted: index === 0
                    focus: index === 0
                    onClicked: root.answer(index)
                    Keys.onReturnPressed: root.answer(index)
                    Keys.onEnterPressed: root.answer(index)
                }
            }
        }
    }
}
