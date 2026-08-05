import QtQuick
import GigabyteRGBController

// A padded surface. Sizes itself to its single child unless a layout overrides
// it, so content-height cards and fill-height cards both work.
Rectangle {
    id: root

    default property alias content: inner.data
    property int padding: Theme.pad

    color: Theme.card
    radius: Theme.radius
    border.width: 1
    border.color: Theme.borderSoft

    implicitWidth:  inner.implicitWidth  + 2 * padding
    implicitHeight: inner.implicitHeight + 2 * padding

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: root.padding

        // Reading the child's implicit size (not its actual size) keeps this
        // clear of the anchors.fill that the child uses to fill us back.
        implicitWidth:  children.length > 0 ? children[0].implicitWidth  : 0
        implicitHeight: children.length > 0 ? children[0].implicitHeight : 0
    }
}
