import QtQuick

// The joints as the editor draws them: a ring at each anchor, joined by a
// shaft. qml-box2d's own debug drawing shows a joint only as lines from each
// body's origin to its anchor -- nothing at all where the two coincide, as they
// do for most pins -- so this draws them instead, after every step.
Canvas {
    id: view

    // The World to follow.
    property var world
    // [joint, anchor on body A, anchor on body B], each anchor in its own
    // body's coordinates.
    property var joints: []
    // Where (0, 0) of the world's coordinates lies in this item.
    property point origin: Qt.point(0, 0)
    property color color: "#aae8c46a"
    property color edgeColor: "#404040"
    property real ringRadius: 7
    property real shaftWidth: 3.5

    antialiasing: true
    onVisibleChanged: requestPaint()
    onOriginChanged: requestPaint()

    Connections {
        target: view.world
        function onStepped() {
            if (view.visible)
                view.requestPaint();
        }
    }

    onPaint: {
        var ctx = getContext("2d");
        ctx.reset();
        ctx.translate(origin.x, origin.y);
        ctx.fillStyle = color;
        ctx.strokeStyle = edgeColor;
        ctx.lineWidth = 1;
        var waist = shaftWidth / 2;
        for (var i = 0; i < joints.length; ++i) {
            var joint = joints[i][0];
            // A broken joint has let go of its body.
            if (!joint || !joint.bodyA || !joint.bodyB)
                continue;
            var a = joint.bodyA.toWorldPoint(joints[i][1]);
            var b = joint.bodyB.toWorldPoint(joints[i][2]);
            var dx = b.x - a.x, dy = b.y - a.y;
            var length = Math.sqrt(dx * dx + dy * dy);
            if (length > ringRadius) {
                var cx = -dy / length * waist, cy = dx / length * waist;
                ctx.beginPath();
                ctx.moveTo(a.x + cx, a.y + cy);
                ctx.lineTo(b.x + cx, b.y + cy);
                ctx.lineTo(b.x - cx, b.y - cy);
                ctx.lineTo(a.x - cx, a.y - cy);
                ctx.closePath();
                ctx.fill();
                ctx.stroke();
            }
            var ends = length > 1 ? [a, b] : [a];
            for (var k = 0; k < ends.length; ++k) {
                ctx.beginPath();
                ctx.arc(ends[k].x, ends[k].y, ringRadius, 0, 2 * Math.PI);
                ctx.fill();
                ctx.stroke();
            }
        }
    }
}
