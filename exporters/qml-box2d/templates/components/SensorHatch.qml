import QtQuick

// The hatching the editor lays over a sensor. A Rectangle or a Shape has no
// pattern fill, so a Canvas fills the pattern through the shape's outline.
Canvas {
    property color color: "#05c936"
    // One of Qt's pattern brushes: Qt.DiagCrossPattern, Qt.BDiagPattern, ...
    property int pattern: Qt.DiagCrossPattern
    // "rectangle", "ellipse" or "polygon"
    property string outline: "rectangle"
    // A rectangle's rounded corners.
    property real radius: 0
    // A polygon's corners, in this item's own coordinates.
    property var points: []

    antialiasing: true

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.fillStyle = ctx.createPattern(color, pattern);
        ctx.beginPath();
        if (outline === "ellipse") {
            ctx.ellipse(0, 0, width, height);
        } else if (outline === "polygon") {
            for (var i = 0; i < points.length; ++i) {
                if (i === 0)
                    ctx.moveTo(points[i].x, points[i].y);
                else
                    ctx.lineTo(points[i].x, points[i].y);
            }
            ctx.closePath();
        } else if (radius > 0) {
            ctx.roundedRect(0, 0, width, height, radius, radius);
        } else {
            ctx.rect(0, 0, width, height);
        }
        ctx.fill();
    }
}
