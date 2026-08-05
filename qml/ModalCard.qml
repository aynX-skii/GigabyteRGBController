import QtQuick
import GigabyteRGBController

// A dimmed overlay with a centred card. Children are laid out in a column under
// the title and subtitle.
Item {
    id: root

    property string title
    property string subtitle
    property bool   show: false
    property int    cardWidth: 420

    default property alias body: bodyColumn.data

    anchors.fill: parent
    visible: opacity > 0
    opacity: root.show ? 1 : 0
    z: 1000

    Behavior on opacity { NumberAnimation { duration: Theme.anim } }

    // Swallows clicks so the UI underneath cannot be operated while a dialog
    // is up, and dims it to make that obvious.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)

        TapHandler { onTapped: {} }
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.cardWidth
        height: layout.implicitHeight + 2 * Theme.pad + 8
        radius: Theme.radius
        color: Theme.card
        border.width: 1
        border.color: Theme.border

        y: root.show ? 0 : 12
        Behavior on y { NumberAnimation { duration: Theme.anim } }

        Column {
            id: layout
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.pad + 4
            anchors.rightMargin: Theme.pad + 4
            spacing: 8

            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }

            Text {
                width: parent.width
                text: root.subtitle
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
                visible: text.length > 0
            }

            Item { width: 1; height: 6 }

            Column {
                id: bodyColumn
                width: parent.width
                spacing: 12
            }
        }
    }
}
