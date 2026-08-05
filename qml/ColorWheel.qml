import QtQuick
import GigabyteRGBController

// HSV wheel: hue around the rim, saturation from centre to edge.
//
// Value is deliberately fixed at 1 - on this hardware "how bright" is the
// controller's MaxBrightness field, not a property of the chosen colour, so a
// value axis here would be a second, conflicting brightness control.
Item {
    id: root

    property color selected: "#ff0000"

    // Emitted continuously while dragging; the backend debounces.
    signal picked(color c)

    implicitWidth: 220
    implicitHeight: 220

    readonly property real radius: Math.min(width, height) / 2 - 1
    readonly property real cx: width / 2
    readonly property real cy: height / 2

    Canvas {
        id: wheel
        anchors.fill: parent
        // The wheel never changes, so paint it once and let the scene graph
        // keep the texture.
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();

            const cx = root.cx, cy = root.cy, r = root.radius;

            // Hue: a fan of wedges. Each is drawn 1.5 degrees wide but stepped
            // by 1, so neighbours overlap and no seams show through.
            for (let a = 0; a < 360; ++a) {
                ctx.beginPath();
                ctx.moveTo(cx, cy);
                // Negated because canvas y grows downwards; this makes hue run
                // anticlockwise from the +x axis, matching hueAt() below.
                ctx.arc(cx, cy, r, -(a + 1.5) * Math.PI / 180, -a * Math.PI / 180);
                ctx.closePath();
                ctx.fillStyle = Qt.hsva(a / 360, 1, 1, 1);
                ctx.fill();
            }

            // Saturation: white in the middle fading to transparent at the rim.
            const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, r);
            g.addColorStop(0, Qt.rgba(1, 1, 1, 1));
            g.addColorStop(1, Qt.rgba(1, 1, 1, 0));
            ctx.fillStyle = g;
            ctx.beginPath();
            ctx.arc(cx, cy, r, 0, Math.PI * 2);
            ctx.fill();
        }
    }

    // Thin rim so the wheel reads as an object on a dark background.
    Rectangle {
        anchors.centerIn: parent
        width: root.radius * 2 + 2
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(0, 0, 0, 0.35)
    }

    // ---- cursor ------------------------------------------------------------
    Rectangle {
        id: knob
        width: 16
        height: 16
        radius: 8
        color: "transparent"
        border.width: 2.5
        border.color: "#ffffff"
        antialiasing: true

        // A fully desaturated colour reports hsvHue as -1, which would put the
        // angle somewhere meaningless - but its saturation is 0, so the radius
        // collapses and the knob lands dead centre regardless.
        x: root.cx + Math.cos(root.selected.hsvHue * 2 * Math.PI)
                     * root.selected.hsvSaturation * root.radius - width / 2
        y: root.cy - Math.sin(root.selected.hsvHue * 2 * Math.PI)
                     * root.selected.hsvSaturation * root.radius - height / 2

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1.5
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.5)
        }
    }

    function hueAt(px, py) {
        let a = Math.atan2(-(py - cy), px - cx);
        if (a < 0)
            a += Math.PI * 2;
        return a / (Math.PI * 2);
    }

    function pick(px, py) {
        const dx = px - cx;
        const dy = py - cy;
        const dist = Math.sqrt(dx * dx + dy * dy);
        const sat = Math.min(1, dist / radius);
        root.picked(Qt.hsva(hueAt(px, py), sat, 1, 1));
    }

    TapHandler {
        onTapped: (point) => root.pick(point.position.x, point.position.y)
    }

    DragHandler {
        target: null
        onActiveChanged: if (active) root.pick(centroid.position.x, centroid.position.y)
        onCentroidChanged: if (active) root.pick(centroid.position.x, centroid.position.y)
    }

    HoverHandler {
        cursorShape: Qt.CrossCursor
    }
}
