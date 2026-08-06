import QtQuick
import GigabyteRGBController

// One caption button of the frameless window's title bar. The glyph is drawn
// rather than shipped as an asset, like EffectGlyph, so it stays crisp at any
// DPI and follows the hover colour.
Item {
    id: root

    // min | max | restore | close
    property string glyph: "close"

    // Closing is the one caption action that cannot be undone, so it gets the
    // red hover fill instead of the neutral one.
    property bool destructive: false

    signal clicked()

    implicitWidth: 42
    implicitHeight: 30

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusSmall
        color: !hover.hovered ? "transparent"
                              : (root.destructive ? Theme.danger : Theme.cardAlt)

        Behavior on color { ColorAnimation { duration: Theme.anim } }

        Canvas {
            id: canvas
            anchors.centerIn: parent
            width: 16
            height: 16

            // Half-pixel offsets below assume a 1 px stroke on a 16 px box:
            // that is what keeps the horizontal and vertical edges from
            // landing between two rows of pixels.
            property color stroke: hover.hovered
                                       ? (root.destructive ? "#ffffff" : Theme.text)
                                       : Theme.textDim

            onStrokeChanged: requestPaint()

            Connections {
                target: root
                function onGlyphChanged() { canvas.requestPaint(); }
            }

            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = stroke;
                ctx.lineWidth = 1;
                ctx.lineJoin = "miter";

                switch (root.glyph) {
                case "min":
                    ctx.beginPath();
                    ctx.moveTo(3, 8.5);
                    ctx.lineTo(13, 8.5);
                    ctx.stroke();
                    break;

                case "max":
                    ctx.strokeRect(3.5, 3.5, 9, 9);
                    break;

                case "restore":
                    // Front pane plus the two visible edges of the one behind.
                    ctx.strokeRect(3.5, 5.5, 7, 7);
                    ctx.beginPath();
                    ctx.moveTo(6.5, 3.5);
                    ctx.lineTo(12.5, 3.5);
                    ctx.lineTo(12.5, 9.5);
                    ctx.stroke();
                    break;

                case "close":
                    ctx.lineWidth = 1.2;
                    ctx.lineCap = "round";
                    ctx.beginPath();
                    ctx.moveTo(4, 4);
                    ctx.lineTo(12, 12);
                    ctx.moveTo(12, 4);
                    ctx.lineTo(4, 12);
                    ctx.stroke();
                    break;
                }
            }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: root.clicked()
    }
}
