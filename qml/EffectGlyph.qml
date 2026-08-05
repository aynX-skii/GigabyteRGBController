import QtQuick
import GigabyteRGBController

// The line-art icon inside an effect button. Drawn rather than shipped as
// assets so it stays crisp at any DPI and recolours with the selection state.
Canvas {
    id: root

    property string glyph: "sun"
    property color  stroke: "#ffffff"
    property real   thickness: 1.6

    onGlyphChanged: requestPaint()
    onStrokeChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();

        const cx = width / 2;
        const cy = height / 2;
        const r  = Math.min(width, height) / 2;

        ctx.strokeStyle = root.stroke;
        ctx.fillStyle   = root.stroke;
        ctx.lineWidth   = root.thickness;
        ctx.lineCap     = "round";
        ctx.lineJoin    = "round";

        switch (root.glyph) {
        case "sun":    paintSun(ctx, cx, cy, r);            break;
        case "wave":   paintWave(ctx, cx, cy, r);           break;
        case "spark":  paintSparkles(ctx, cx, cy, r, 1);    break;
        case "spark2": paintSparkles(ctx, cx, cy, r, 2);    break;
        case "cycle":  paintInfinity(ctx, cx, cy, r);       break;
        case "off":    paintOff(ctx, cx, cy, r);            break;
        }
    }

    // A filled core with eight rays - "always on".
    function paintSun(ctx, cx, cy, r) {
        ctx.beginPath();
        ctx.arc(cx, cy, r * 0.34, 0, Math.PI * 2);
        ctx.fill();

        for (let i = 0; i < 8; ++i) {
            const a = i * Math.PI / 4;
            const c = Math.cos(a), s = Math.sin(a);
            ctx.beginPath();
            ctx.moveTo(cx + c * r * 0.56, cy + s * r * 0.56);
            ctx.lineTo(cx + c * r * 0.92, cy + s * r * 0.92);
            ctx.stroke();
        }
    }

    // Two and a half cycles of a sine - "breathing".
    function paintWave(ctx, cx, cy, r) {
        const w = r * 1.8;
        const amp = r * 0.62;
        ctx.beginPath();
        for (let i = 0; i <= 60; ++i) {
            const t = i / 60;
            const x = cx - w / 2 + t * w;
            const y = cy - Math.sin(t * Math.PI * 5) * amp;
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        }
        ctx.stroke();
    }

    // One four-pointed sparkle, or a large plus a small one for double flash.
    function paintSparkles(ctx, cx, cy, r, count) {
        if (count === 1) {
            sparkle(ctx, cx, cy, r * 0.95);
        } else {
            sparkle(ctx, cx - r * 0.22, cy + r * 0.1, r * 0.78);
            sparkle(ctx, cx + r * 0.46, cy - r * 0.44, r * 0.46);
        }
    }

    // Concave-sided star: the control points sit near the centre, which is what
    // pulls the edges in.
    function sparkle(ctx, cx, cy, r) {
        const k = r * 0.13;
        ctx.beginPath();
        ctx.moveTo(cx, cy - r);
        ctx.quadraticCurveTo(cx + k, cy - k, cx + r, cy);
        ctx.quadraticCurveTo(cx + k, cy + k, cx, cy + r);
        ctx.quadraticCurveTo(cx - k, cy + k, cx - r, cy);
        ctx.quadraticCurveTo(cx - k, cy - k, cx, cy - r);
        ctx.closePath();
        ctx.stroke();
    }

    // Lemniscate of Gerono-ish figure eight - "cycle".
    function paintInfinity(ctx, cx, cy, r) {
        const a = r * 0.95;
        ctx.beginPath();
        for (let i = 0; i <= 90; ++i) {
            const t = i / 90 * Math.PI * 2;
            const d = 1 + Math.sin(t) * Math.sin(t);
            const x = cx + a * Math.cos(t) / d;
            const y = cy + a * Math.sin(t) * Math.cos(t) / d;
            i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
        }
        ctx.closePath();
        ctx.stroke();
    }

    function paintOff(ctx, cx, cy, r) {
        ctx.beginPath();
        ctx.arc(cx, cy, r * 0.82, 0, Math.PI * 2);
        ctx.stroke();

        const d = r * 0.82 * Math.SQRT1_2;
        ctx.beginPath();
        ctx.moveTo(cx - d, cy + d);
        ctx.lineTo(cx + d, cy - d);
        ctx.stroke();
    }
}
