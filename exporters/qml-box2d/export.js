// Physalis scene -> a Qt Quick project built on the qml-box2d plugin.
//
// The scene is written as QML objects the way a qml-box2d example is: a World,
// and for every body an Item that draws it with a Body inside that moves it,
// holding one fixture per shape; the joints sit beside them. The rules become
// afterStep(), plain JavaScript run once the world has stepped, written as
// ordinary if statements. Nothing of the editor itself is carried across.
//
// What each object looks like in QML is not written here: every one is a
// template under templates/ (see render() below), and this file works out the
// values that go into them.
//
// qml-box2d is Box2D 2.3 underneath, and several things the editor offers have
// no counterpart in it. Those are reported through io.log() rather than dropped.
//
// Units. qml-box2d takes lengths in pixels and angles in degrees, with Y
// pointing down -- the editor's own units -- so nearly everything is written as
// the editor has it. Velocities are the exception, in metres per second, and
// gravity and the initial velocities are quoted at the editor's motion scale.
// Every value a rule reads or writes is kept in the editor's units, so a rule
// compares against the number that was typed into it.

var NEWLINE = String.fromCharCode(10);

var T = null;       // template loader
var PPM = 1000;     // scene units per metre
var MOTION = 0.05;  // 50 / PPM -- the scale the editor quotes world speeds at
// The scale the editor sets Box2D's tolerances at, from the world's Contact
// Margin: 2 px by default, a contact seen coming that far off. Tighter, and a
// door slammed onto a ramp sank in before there was a contact to stop it.
var TOLERANCE = 0.1;
var MISSED = null;  // what this target cannot represent

// How far every box and polygon is pulled in from its drawn outline, in pixels.
//
// Box2D 2.3 collides a polygon as its outline grown by a skin of twice the
// linear slop; Box2D v3, which the editor runs, has none. Pulling the outline
// in by twice the skin puts the collision surface just inside what is drawn, so
// two shapes drawn flush touch rather than overlap -- a lift drawn beside a
// wall slides past it instead of catching.
var INSET = 0.0;

var NAMES = null;     // editor name -> QML id
var TAKEN = null;
var USED = null;      // shapes a rule names, whose fixtures need an id
var HANDLERS = null;  // shape name -> { begun: true, ended: true }
var FIXTURE_IDS = null;
var STATE = null;     // properties the step code keeps between steps
var COUNTERS = null;
var HELPERS = null;
var LOST = false;
var TRAVEL = null;    // prismatic joint index -> where its travel starts, in pixels
var ANSWERING = false; // writing a "to be removed" answer, which removes for real
var CLONING = false;   // writing a body as a Component to copy, with no ids of its own

function vals(owner) {
    return (owner && owner.physics) || {};
}

// Box2D v3, which the editor runs, splits each step into sub-steps and a motor
// joint closes `correctionFactor` of the gap on every one of them; Box2D 2.x
// closes it once a step. The same number would move a motor joint's body
// several times more slowly here, so it is turned into the once-a-step factor
// that closes as much of the gap in a step: 0.05 at four sub-steps is 0.185.
function stepCorrection(factor) {
    var f = Math.max(0, Math.min(1, Number(factor)));
    return 1 - Math.pow(1 - f, SUB_STEPS);
}

var SUB_STEPS = 4;

function exportScene(scene, io) {
    var world = scene.world || {};
    var field = scene.field || {};
    var own = scene.converterSettings || {};
    var physics = (scene.settings && scene.settings.Physics) || {};

    PPM = world.pixelsPerMeter || 1000;
    MOTION = 50.0 / PPM;
    // The world's Contact Margin, in scene units: Box2D makes a contact 4 x slop
    // away, so its length unit is the margin over 4 x 5 mm at this scale.
    TOLERANCE = Math.max(0.1, pickNumber(vals(world).contactMargin, 2)) / (4.0 * 0.005 * PPM);
    SUB_STEPS = Math.max(1, Math.round(pickNumber(vals(world).subStepCount, 4)));
    MISSED = {};
    INSET = 2.0 * (2.0 * 0.005 * TOLERANCE) * PPM;

    var cache = {};
    T = function (name) {
        if (!(name in cache))
            cache[name] = io.read("templates/" + name);
        return cache[name];
    };

    var project = safeName(own.projectName) || "PhysalisScene";

    TAKEN = {};
    for (var r = 0; r < RESERVED.length; ++r)
        TAKEN[RESERVED[r]] = true;
    NAMES = nameObjects(scene);
    USED = {};
    HANDLERS = {};
    FIXTURE_IDS = {};
    STATE = [];
    CHANGES = 0;
    CHANGE_READS = [];
    CHANGE_SAVES = [];
    COUNTERS = { time: false, frame: false };
    HELPERS = { clones: {} };
    ANSWERING = false;
    CLONING = false;
    LOST = destroys(scene);
    TRAVEL = travelOrigins(scene);

    var colours = {
        dynamic: qmlColour(physics.bodyDynamicColor, "#2e86c1"),
        "static": qmlColour(physics.bodyStaticColor, "#279e6a"),
        kinematic: qmlColour(physics.bodyKinematicColor, "#884ea0"),
        sensor: qmlColour(physics.sensorColor, "#05c936"),
        sensorPattern: sensorPattern(physics.sensorPattern),
        sensorFills: settingTrue(physics.sensorFillsBody),
        joint: qmlColour(physics.jointColor, "#aae8c46a"),
        jointAnchorRadius: pickNumber(physics.jointAnchorRadius, 7),
        alpha: Math.max(0, Math.min(255, pickNumber(physics.fillAlpha, 90))),
        axisLength: pickNumber(physics.bodyAxisLength, 40),
        axisWidth: pickNumber(physics.bodyAxisWidth, 2),
        axisX: opaqueColour(qmlColour(physics.bodyAxisXColor, "#dc3232")),
        axisY: opaqueColour(qmlColour(physics.bodyAxisYColor, "#28a03c")),
    };

    // The step code first: it decides which fixtures need an id, which of
    // them report contacts, and which helpers the scene needs.
    var stepCode = stepBody(scene, io);

    var g = { x: pickNumber(vals(world).gravityX, 0), y: pickNumber(vals(world).gravityY, 9.81) };
    worldWarnings(world);

    io.write("Scene.qml", page("Scene.qml.tmpl", {
        TITLE: project,
        BACKGROUND: qmlColour(field.backgroundColor, "#ffffff"),
        PIXELS_PER_METER: plain(PPM),
        STEPS_PER_SECOND: int(own.stepsPerSecond, 60),
        GRAVITY_X: plain(g.x * MOTION),
        GRAVITY_Y: plain(g.y * MOTION),
        STATE: STATE.join(NEWLINE),
        BODIES: bodiesCode(scene, colours),
        FIELD_BOUNDS: fieldBoundsCode(world, field),
        JOINTS: jointsCode(scene),
        PRE_SOLVE: HELPERS.preSolve ? render("objects/pre-solve.qml.tmpl", {}) : "",
        CLONES: clonesCode(scene, colours),
        JOINT_VIEW: jointViewCode(colours),
        RAYS: raysCode(scene),
        STEP: stepCode,
        HELPERS: helpersCode(scene),
    }));

    io.write("Main.qml", page("Main.qml.tmpl", {
        TITLE: project,
        WIDTH: String(Math.round(field.width || 1000)),
        HEIGHT: String(Math.round(field.height || 600)),
        CONTROLS_HEIGHT: (own.addControls || own.debugView) ? " + toolbar.implicitHeight" : "",
        CONTROLS: controlsCode(own),
        DEBUG_VIEW: bool(own.debugView),
    }));

    io.write("main.cpp", page("main.cpp.tmpl", { PROJECT: project }));

    // What Scene.qml draws with, as they are.
    io.write("JointView.qml", T("components/JointView.qml"));
    io.write("SensorHatch.qml", T("components/SensorHatch.qml"));

    var qtPath = String(own.qtPath || "").trim().replace(/\\/g, "/");
    io.write("CMakeLists.txt", page("CMakeLists.txt.tmpl", {
        PROJECT: project,
        QT_PREFIX: qtPath ? ("list(APPEND CMAKE_PREFIX_PATH \"" + qtPath + "\")") : "",
        REPOSITORY: String(own.repository || "https://github.com/qml-box2d/qml-box2d.git").trim(),
        REF: String(own.ref || "master").trim(),
    }));

    io.write("scale-box2d.cmake", page("scale-box2d.cmake.tmpl", {
        PIXELS_PER_METER: plain(PPM),
        TOLERANCE: plain(TOLERANCE),
        MOTION: plain(MOTION),
        LINEAR_SLOP: tolerance(0.005 * TOLERANCE),
        AABB_EXTENSION: tolerance(0.1 * TOLERANCE),
        VELOCITY_THRESHOLD: tolerance(pickNumber(vals(world).restitutionThreshold, 1) * MOTION),
        MAX_LINEAR_CORRECTION: tolerance(0.2 * TOLERANCE),
        MAX_TRANSLATION: tolerance(2.0 * MOTION),
        LINEAR_SLEEP_TOLERANCE: tolerance(0.01 * MOTION),
    }));

    io.log("Exported " + scene.simulation.bodies.length + " bodies, "
           + scene.simulation.joints.length + " joints and "
           + (scene.rules || []).length + " rules into Scene.qml.");
    io.log("Open CMakeLists.txt in Qt Creator, or build it with CMake; qml-box2d is fetched on the first configure.");

    var missed = [];
    for (var key in MISSED) {
        if (has(MISSED, key))
            missed.push(key + " (" + MISSED[key] + ")");
    }
    if (missed.length) {
        io.log("qml-box2d is Box2D 2.3 and has no equivalent for:");
        for (var i = 0; i < missed.length; ++i)
            io.log("  - " + missed[i]);
    }
    return true;
}

// Something the scene asks for that qml-box2d cannot do, recorded once per kind.
function missing(what, where) {
    if (!(what in MISSED))
        MISSED[what] = where;
}

function worldWarnings(world) {
    var v = vals(world);
    if (pickNumber(v.contactHertz, 30) !== 30 || pickNumber(v.contactDampingRatio, 10) !== 10)
        missing("contact stiffness and damping", "world");
    if (pickNumber(v.subStepCount, 4) !== 4)
        missing("solver sub-steps", "world -- Box2D 2.3 iterates instead");
    if (pickNumber(v.maxContactPushSpeed, 3) !== 3)
        missing("max push speed", "world");
    if (v.enableSleep === false)
        missing("switching sleep off for the whole world", "world -- set it on each body instead");
    // Box2D 2.3 behind qml-box2d has neither switch exposed.
    if (vals(world).enableWarmStarting === false)
        missing("switching warm starting off", "world -- qml-box2d does not expose it");
    if (vals(world).enableSpeculative === false)
        missing("switching speculative contacts off", "world -- Box2D 2.3 has none");
}

function tolerance(v) {
    return Number(v).toPrecision(6).replace(/0+$/, "").replace(/\.$/, ".0");
}

// --- names -----------------------------------------------------------------

var RESERVED = ("scene world field window loader toolbar ground walls rayCaster ppm dt running debug "
    + "elapsed stepCount contactsBegun contactsEnded aboutToTouch starts blastShapes nearestPoint afterStep castRay explode hitSpeed doomed cloneItem cloneBody "
    + "initState holdRequested stopRequested parent x y z width height color clip visible rotation "
    + "opacity scale enabled state states data children resources other a b i c d k s body fixture "
    + "point normal fraction mask best target Qt Math console JSON Number String Object Array "
    + "Item Rectangle Shape ShapePath PathLine World Body Box Circle Polygon Chain Edge DebugDraw "
    + "RayCast RevoluteJoint PrismaticJoint WheelJoint DistanceJoint RopeJoint WeldJoint MotorJoint "
    + "MouseJoint property signal readonly required alias import as on "
    + "break case catch class const continue debugger default delete do else enum export "
    + "extends false finally for function if in instanceof let new null return super "
    + "switch this throw true try typeof var void while with yield undefined").split(" ");

// A QML id starts with a lower-case letter and holds letters, digits and
// underscores; ids and the scene's own properties share one namespace.
function identifier(text, fallback) {
    var base = String(text || "").replace(/[^A-Za-z0-9_]/g, "_");
    if (!base)
        base = fallback;
    if (/^[0-9_]/.test(base))
        base = fallback + "_" + base;
    base = base.charAt(0).toLowerCase() + base.slice(1);
    var name = base;
    for (var n = 2; TAKEN[name]; ++n)
        name = base + "_" + n;
    TAKEN[name] = true;
    return name;
}

function nameObjects(scene) {
    var names = {};
    var bodies = scene.simulation.bodies;
    for (var b = 0; b < bodies.length; ++b) {
        names["body:" + b] = identifier(bodies[b].name, "body");
        names["item:" + b] = identifier(names["body:" + b] + "Item", "item");
    }
    for (var s = 0; s < bodies.length; ++s) {
        var parts = bodies[s].parts || [];
        for (var p = 0; p < parts.length; ++p) {
            if (parts[p].name)
                names["shape:" + parts[p].name] = identifier(parts[p].name, "fixture");
        }
    }
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j)
        names["joint:" + j] = identifier(joints[j].name, "joint");
    var rays = scene.rays || [];
    for (var r = 0; r < rays.length; ++r)
        names["ray:" + r] = identifier(rays[r].name, "ray");
    return names;
}

// The ids a shape's fixtures go by. A solid shape is one fixture; a run of
// segments is one Edge per segment, each with an id of its own.
function fixtureId(part, k) {
    var key = part.name + "#" + k;
    if (!has(FIXTURE_IDS, key))
        FIXTURE_IDS[key] = k === 0 ? NAMES["shape:" + part.name] : identifier(NAMES["shape:" + part.name] + "_" + (k + 1), "edge");
    return FIXTURE_IDS[key];
}

// --- bodies and fixtures ---------------------------------------------------

function bodiesCode(scene, colours) {
    var out = [];
    var bodies = scene.simulation.bodies;
    for (var i = 0; i < bodies.length; ++i)
        out.push(bodyCode(bodies[i], i, colours));
    return out.join(NEWLINE + NEWLINE);
}

var BODY_TYPES = { "static": "Body.Static", kinematic: "Body.Kinematic", dynamic: "Body.Dynamic" };

function bodyCode(body, index, colours) {
    var v = vals(body);
    var p = body.position || { x: 0, y: 0 };
    var vx = pickNumber(v.velocityX, 0), vy = pickNumber(v.velocityY, 0);
    if (v.allowFastRotation)
        missing("allow fast rotation", "body " + body.name);

    var parts = body.parts || [];
    var fixtures = [], looks = [];
    for (var i = 0; i < parts.length; ++i) {
        fixtures.push(fixtureCode(parts[i], body));
        looks.push(looksCode(parts[i], body, colours));
    }
    return render("objects/body.qml.tmpl", {
        NAME: body.name,
        ITEM_ID: CLONING ? "cloneItem" : NAMES["item:" + index],
        BODY_ID: CLONING ? "cloneBody" : NAMES["body:" + index],
        X: short(p.x),
        Y: short(p.y),
        ROTATION: body.rotation ? short(body.rotation) : null,
        BODY_TYPE: body.type === "dynamic" || body.type === "kinematic" ? BODY_TYPES[body.type] : null,
        // Scene units a second, divided by the scale -- see the same line in
        // the Box2D/Qt converter.
        LINEAR_VELOCITY: vx || vy ? "Qt.point(" + num(vx / PPM) + ", " + num(vy / PPM) + ")" : null,
        ANGULAR_VELOCITY: v.angularVelocity ? short(v.angularVelocity) : null,
        LINEAR_DAMPING: v.linearDamping ? plain(v.linearDamping) : null,
        ANGULAR_DAMPING: v.angularDamping ? plain(v.angularDamping) : null,
        GRAVITY_SCALE: differs(pickNumber(v.gravityScale, 1), 1) ? plain(v.gravityScale) : null,
        SLEEPING_ALLOWED: v.enableSleep === false ? "false" : null,
        AWAKE: v.isAwake === false ? "false" : null,
        FIXED_ROTATION: v.fixedRotation ? "true" : null,
        BULLET: v.isBullet ? "true" : null,
        ACTIVE: body.isEnabled === false ? "false" : null,
        FIXTURES: fixtures.join(NEWLINE),
        LOOKS: looks.join(NEWLINE),
        AXES: axesCode(body, colours),
    });
}

function isOutline(part) {
    return (part.kind === "polygon" && !isSolidPolygon(part)) || part.kind === "chain";
}

// Box2D 2.3 takes a convex polygon of up to eight vertices.
function isSolidPolygon(part) {
    var pts = part.points || [];
    return part.kind === "polygon" && pts.length >= 3 && pts.length <= 8 && isConvex(pts);
}

function isConvex(points) {
    var sign = 0;
    for (var i = 0; i < points.length; ++i) {
        var a = points[i], b = points[(i + 1) % points.length], c = points[(i + 2) % points.length];
        var cross = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (Math.abs(cross) < 1e-9)
            continue;
        var s = cross > 0 ? 1 : -1;
        if (sign && s !== sign)
            return false;
        sign = s;
    }
    return sign !== 0;
}

function fixtureCode(part, body) {
    if (isOutline(part) && body.type === "dynamic") {
        if (part.kind === "polygon" && (part.points || []).length > 8)
            missing("solid polygons of more than eight points", "shape " + part.name);
        return render("objects/massless-outline.qml.tmpl", { NAME: part.name || "an outline" });
    }
    var v = vals(part);
    if (pickNumber(v.rollingResistance, 0) !== 0)
        missing("rolling resistance", "shape " + part.name);
    if (pickNumber(v.tangentSpeed, 0) !== 0)
        missing("surface speed", "shape " + part.name);
    if (part.kind === "box" && (part.cornerRadius || 0) > 0)
        missing("rounded corners in collisions", "shape " + part.name + " -- it collides as a square box");

    // Box2D 2.3's own defaults are density 0 and friction 0.2.
    //
    // A shape built inside its outline (see INSET) is denser by the area it
    // lost, so it weighs what the editor's does and balances where it does: a
    // see-saw pivoted on its centre of mass stays level.
    var category = filterBits(v.categoryBits, 1, part.name);
    var mask = filterBits(v.maskBits, 0xFFFF, part.name);
    var material = render("objects/material.qml.tmpl", {
        DENSITY: plain(pickNumber(v.density, 1) * insetDensityScale(part)),
        FRICTION: differs(pickNumber(v.friction, 0.6), 0.2) ? plain(pickNumber(v.friction, 0.6)) : null,
        RESTITUTION: v.restitution ? plain(v.restitution) : null,
        SENSOR: v.isSensor ? "true" : null,
        CATEGORIES: category !== 1 ? String(category) : null,
        COLLIDES_WITH: mask !== 0xFFFF ? String(mask) : null,
        GROUP_INDEX: v.groupIndex ? String(Math.round(v.groupIndex)) : null,
    });

    var c = part.center || { x: 0, y: 0 };
    var named = !CLONING && part.name && (USED[part.name] || HANDLERS[part.name]);
    var fixture = function (k, template, values) {
        values.ID = named ? fixtureId(part, k) : null;
        values.MATERIAL = material;
        values.CONTACTS = contactsCode(part, k);
        return render("objects/" + template, values);
    };

    if (part.kind === "box") {
        var hw = pullIn(part.halfExtents.x), hh = pullIn(part.halfExtents.y);
        return fixture(0, "box.qml.tmpl", {
            X: short(c.x - hw), Y: short(c.y - hh), WIDTH: short(2 * hw), HEIGHT: short(2 * hh),
            ROTATION: part.rotation ? short(part.rotation) : null,
        });
    }
    if (part.kind === "circle") {
        return fixture(0, "circle.qml.tmpl", {
            X: short(c.x - part.radius), Y: short(c.y - part.radius), RADIUS: short(part.radius),
        });
    }

    var pts = part.points || [];
    if (isSolidPolygon(part))
        return fixture(0, "polygon.qml.tmpl", { VERTICES: listValue(points(pulledInOutline(pts))) });

    var closed = part.kind === "polygon" || part.closed;
    if (part.kind === "chain" && part.smoothChain && pts.length >= 4)
        return fixture(0, "chain.qml.tmpl", { VERTICES: listValue(points(pts)), LOOP: closed ? "true" : null });

    // One two-sided edge per segment.
    var out = [];
    var edges = closed ? pts.length : pts.length - 1;
    for (var e = 0; e < edges; ++e)
        out.push(fixture(e, "edge.qml.tmpl", { FROM: point(pts[e]), TO: point(pts[(e + 1) % pts.length]) }));
    return out.join(NEWLINE);
}

// A fixture a rule watches says so as soon as its contact starts or ends; the
// step code then goes through what was said.
function contactsCode(part, k) {
    var wanted = CLONING ? null : HANDLERS[part.name];
    if (!wanted)
        return "";
    var self = fixtureId(part, k);
    return render("objects/contacts.qml.tmpl", {
        BEGIN: wanted.begun ? self : null,
        END: wanted.ended ? self : null,
    });
}

function point(p) { return "Qt.point(" + short(p.x) + ", " + short(p.y) + ")"; }

function points(pts) {
    var list = [];
    for (var i = 0; i < pts.length; ++i)
        list.push(point(pts[i]));
    return list;
}

// What the editor draws for a shape, in the body's own coordinates.
// A sensor keeps the colour of its body and is left open unless the settings
// say to fill it, with hatching in the sensor colour laid over it.
function looksCode(part, body, colours) {
    var sensor = !!vals(part).isSensor;
    var colour = colours[body.type] || colours.dynamic;
    var fillColour = (sensor && !colours.sensorFills) ? "transparent" : withAlpha(colour, colours.alpha);
    var drawn = drawnCode(part, colour, fillColour);
    if (sensor && drawn)
        return [drawn, hatchCode(part, colours)].join(NEWLINE);
    return drawn;
}

// Qt's pattern brushes, by the number the settings store them as.
var BRUSH_STYLES = {
    9: "HorPattern", 10: "VerPattern", 11: "CrossPattern",
    12: "BDiagPattern", 13: "FDiagPattern", 14: "DiagCrossPattern",
};

function sensorPattern(value) {
    return BRUSH_STYLES[Math.round(Number(value))] || "DiagCrossPattern";
}

function settingTrue(value) { return value === true || value === "true" || value === 1; }

function hatchCode(part, colours) {
    var values = { COLOR: colours.sensor, PATTERN: colours.sensorPattern, ROTATION: null, RADIUS: null, POINTS: null };
    if (part.kind === "box" || part.kind === "circle") {
        var box = part.kind === "box";
        var c = part.center || { x: 0, y: 0 };
        var hw = box ? part.halfExtents.x : part.radius;
        var hh = box ? part.halfExtents.y : part.radius;
        values.X = short(c.x - hw);
        values.Y = short(c.y - hh);
        values.WIDTH = short(2 * hw);
        values.HEIGHT = short(2 * hh);
        values.OUTLINE = box ? "rectangle" : "ellipse";
        if (box && part.rotation)
            values.ROTATION = short(part.rotation);
        if (box && part.cornerRadius)
            values.RADIUS = short(part.cornerRadius);
        return render("objects/hatch.qml.tmpl", values);
    }
    var pts = part.points || [];
    if (!pts.length || !isSolidPolygon(part))
        return "";
    var minX = pts[0].x, minY = pts[0].y, maxX = minX, maxY = minY;
    for (var i = 1; i < pts.length; ++i) {
        minX = Math.min(minX, pts[i].x); maxX = Math.max(maxX, pts[i].x);
        minY = Math.min(minY, pts[i].y); maxY = Math.max(maxY, pts[i].y);
    }
    var corners = [];
    for (var k = 0; k < pts.length; ++k)
        corners.push(point({ x: pts[k].x - minX, y: pts[k].y - minY }));
    values.X = short(minX);
    values.Y = short(minY);
    values.WIDTH = short(maxX - minX);
    values.HEIGHT = short(maxY - minY);
    values.OUTLINE = "polygon";
    values.POINTS = listValue(corners);
    return render("objects/hatch.qml.tmpl", values);
}

function drawnCode(part, colour, fillColour) {
    var c = part.center || { x: 0, y: 0 };

    if (part.kind === "box" || part.kind === "circle") {
        var box = part.kind === "box";
        var hw = box ? part.halfExtents.x : part.radius;
        var hh = box ? part.halfExtents.y : part.radius;
        var radius = box ? (part.cornerRadius || 0) : part.radius;
        return render("objects/rectangle.qml.tmpl", {
            X: short(c.x - hw), Y: short(c.y - hh), WIDTH: short(2 * hw), HEIGHT: short(2 * hh),
            ROTATION: box && part.rotation ? short(part.rotation) : null,
            RADIUS: radius ? short(radius) : null,
            FILL: fillColour,
            STROKE: opaqueColour(colour),
        });
    }

    var pts = part.points || [];
    if (!pts.length)
        return "";
    var closed = part.kind === "polygon" || part.closed;
    var filled = closed && isSolidPolygon(part);
    var path = [];
    for (var i = 1; i < pts.length; ++i)
        path.push(render("objects/path-line.qml.tmpl", { X: short(pts[i].x), Y: short(pts[i].y) }));
    if (closed)
        path.push(render("objects/path-line.qml.tmpl", { X: short(pts[0].x), Y: short(pts[0].y) }));
    return render("objects/outline.qml.tmpl", {
        STROKE: opaqueColour(colour),
        FILL: filled ? fillColour : "transparent",
        START_X: short(pts[0].x),
        START_Y: short(pts[0].y),
        PATH: path.join(NEWLINE),
    });
}

// The body's axes, shown with the debug view: from where the editor puts the
// centre of mass, worked out from the shapes -- qml-box2d's own would sit at the
// origin of every static body, which has no mass to have a centre.
function axesCode(body, colours) {
    var c = body.centerOfMass || { x: 0, y: 0 };
    return render("objects/axes.qml.tmpl", {
        X: short(c.x),
        Y: short(c.y),
        // Each bar is centred across the axis it draws.
        ACROSS_X: short(c.x - colours.axisWidth / 2),
        ACROSS_Y: short(c.y - colours.axisWidth / 2),
        LENGTH: short(colours.axisLength),
        THICKNESS: short(colours.axisWidth),
        X_COLOR: colours.axisX,
        Y_COLOR: colours.axisY,
    });
}

// How much denser a shape has to be to weigh the same once pulled in.
function insetDensityScale(part) {
    if (part.kind === "box") {
        var hw = part.halfExtents.x, hh = part.halfExtents.y;
        return (hw * hh) / (pullIn(hw) * pullIn(hh));
    }
    if (isSolidPolygon(part)) {
        var drawn = Math.abs(area(part.points)), built = Math.abs(area(pulledInOutline(part.points)));
        return built > 0 ? drawn / built : 1;
    }
    return 1;
}

function area(pts) {
    var twice = 0;
    for (var i = 0; i < pts.length; ++i) {
        var p = pts[i], q = pts[(i + 1) % pts.length];
        twice += p.x * q.y - q.x * p.y;
    }
    return twice / 2;
}

function pullIn(half) {
    return Math.max(half - INSET, Math.min(half, 0.001));
}

// A convex outline with every edge moved in by INSET.
function pulledInOutline(pts) {
    if (pts.length < 3 || INSET <= 0.0)
        return pts;
    var area = 0.0, i;
    for (i = 0; i < pts.length; ++i) {
        var p = pts[i], q = pts[(i + 1) % pts.length];
        area += p.x * q.y - q.x * p.y;
    }
    // The outward normal of the edge from a to b, whichever way the points run.
    var normal = function (a, b) {
        var dx = b.x - a.x, dy = b.y - a.y;
        var length = Math.sqrt(dx * dx + dy * dy) || 1.0;
        return area > 0 ? { x: dy / length, y: -dx / length } : { x: -dy / length, y: dx / length };
    };
    var out = [];
    for (i = 0; i < pts.length; ++i) {
        var prev = pts[(i + pts.length - 1) % pts.length], v = pts[i], next = pts[(i + 1) % pts.length];
        var n1 = normal(prev, v), n2 = normal(v, next);
        var k = INSET / Math.max(1.0 + n1.x * n2.x + n1.y * n2.y, 0.1);
        out.push({ x: v.x - (n1.x + n2.x) * k, y: v.y - (n1.y + n2.y) * k });
    }
    return out;
}

// Box2D 2.3 filters with 16 bits where the editor has 64. A 64-bit all-ones
// mask means everything; anything else above the low 16 bits cannot be held.
function filterBits(value, fallback, where) {
    var n = Number(value);
    if (!isFinite(n) || n <= 0)
        return fallback;
    if (n >= 0xFFFF) {
        if (n > 0xFFFF && n < 9007199254740991)
            missing("collision groups above the low 16 bits", where);
        return 0xFFFF;
    }
    return n & 0xFFFF;
}

function fieldBoundsCode(world, field) {
    if (!world.solidBounds)
        return "";
    var w = field.width || 1000, h = field.height || 1000, t = 40;
    return render("objects/walls.qml.tmpl", {
        LEFT: short(-w / 2 - t),
        RIGHT: short(w / 2),
        TOP: short(-h / 2 - t),
        BOTTOM: short(h / 2),
        SPAN: short(w + 2 * t),
        INNER_TOP: short(-h / 2),
        INNER_HEIGHT: short(h),
        THICKNESS: short(t),
    });
}

// --- joints ----------------------------------------------------------------

// Where a point in the scene lies in a body's own frame, in pixels.
function localPoint(pt, body) {
    var p = body.position || { x: 0, y: 0 };
    var turn = -(body.rotation || 0) * Math.PI / 180;
    var dx = pt.x - p.x, dy = pt.y - p.y;
    return { x: dx * Math.cos(turn) - dy * Math.sin(turn), y: dx * Math.sin(turn) + dy * Math.cos(turn) };
}

function localAxis(joint, bodyA) {
    var ax = joint.axis || { x: 1, y: 0 };
    var length = Math.sqrt(ax.x * ax.x + ax.y * ax.y) || 1;
    var turn = -(bodyA.rotation || 0) * Math.PI / 180;
    return { x: (ax.x * Math.cos(turn) - ax.y * Math.sin(turn)) / length,
             y: (ax.x * Math.sin(turn) + ax.y * Math.cos(turn)) / length };
}

// Box2D measures a prismatic joint between its two anchors, so one whose
// anchors start apart starts at that distance. The editor counts travel from
// where the joint starts; this is the offset between the two.
function travelOrigins(scene) {
    var out = {};
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j) {
        var joint = joints[j];
        if (joint.type !== "prismatic")
            continue;
        var anchors = joint.anchors || [];
        var a = anchors[0] || { x: 0, y: 0 }, b = anchors[1] || a;
        var ax = joint.axis || { x: 1, y: 0 };
        var length = Math.sqrt(ax.x * ax.x + ax.y * ax.y) || 1;
        out[j] = ((b.x - a.x) * ax.x + (b.y - a.y) * ax.y) / length;
    }
    return out;
}

var GROUND_NEEDED = false;
var JOINT_ENDS = [];  // { id, a, b }: each joint and its anchors on its own bodies

// The joints as the editor draws them, with the debug view: qml-box2d's own
// debug drawing shows nothing where a joint's anchor sits on its body's origin.
function jointViewCode(colours) {
    if (!JOINT_ENDS.length)
        return "";
    var list = [];
    for (var i = 0; i < JOINT_ENDS.length; ++i) {
        var e = JOINT_ENDS[i];
        list.push("[" + e.id + ", " + point(e.a) + ", " + point(e.b) + "]");
    }
    return render("objects/joint-view.qml.tmpl", {
        COLOR: colours.joint,
        RING_RADIUS: short(colours.jointAnchorRadius),
        JOINTS: listValue(list),
    });
}

function jointsCode(scene) {
    GROUND_NEEDED = false;
    JOINT_ENDS = [];
    var out = [];
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j) {
        var code = jointCode(scene, joints[j], j);
        if (code)
            out.push(code);
    }
    if (GROUND_NEEDED)
        out.unshift(render("objects/ground.qml.tmpl", {}));
    return out.join(NEWLINE + NEWLINE);
}

function jointCode(scene, joint, index) {
    var p = joint.params || {};
    var name = NAMES["joint:" + index];
    var bodies = scene.simulation.bodies;
    var type = joint.type;
    var anchors = joint.anchors || [];
    var a = anchors.length > 0 ? anchors[0] : { x: 0, y: 0 };
    var b = anchors.length > 1 ? anchors[1] : a;
    var values = { NAME: joint.name, ID: name, COLLIDE_CONNECTED: joint.collideConnected ? "true" : null };
    var flag = function (on) { return on ? "true" : null; };
    // Where the joint view draws its ends, on each body; a motor joint has no
    // anchors and is drawn between the two bodies' origins, as Box2D reports it.
    var ends = { a: { x: 0, y: 0 }, b: { x: 0, y: 0 } };
    var anchored = function (bodyA, bodyB, first, second) {
        ends = { a: localPoint(first, bodyA), b: localPoint(second, bodyB) };
        values.ANCHOR_A = point(ends.a);
        values.ANCHOR_B = point(ends.b);
    };
    var build = function (template, A, B) {
        if (A !== "ground")
            JOINT_ENDS.push({ id: name, a: ends.a, b: ends.b });
        values.BODY_A = A;
        values.BODY_B = B;
        return render("objects/" + template, values);
    };
    var unsupported = function (why) {
        return render("objects/unsupported-joint.qml.tmpl", { NAME: joint.name, WHY: why });
    };

    if (has(p, "constraintHertz") || has(p, "constraintDampingRatio"))
        missing("per-joint constraint tuning", "joint " + joint.name);

    if (type === "mouse") {
        var only = joint.bodyB >= 0 ? joint.bodyB : joint.bodyA;
        if (only < 0)
            return "";
        GROUND_NEEDED = true;
        var tx = pick(p, "targetX", 0), ty = pick(p, "targetY", 0);
        values.TARGET = point((tx === 0 && ty === 0) ? a : { x: tx, y: ty });
        values.MAX_FORCE = plain(pick(p, "maxForce", 1));
        values.FREQUENCY = plain(pick(p, "hertz", 4));
        values.DAMPING_RATIO = plain(pick(p, "dampingRatio", 1));
        return build("mouse.qml.tmpl", "ground", NAMES["body:" + only]);
    }
    if (joint.bodyA < 0 || joint.bodyB < 0)
        return unsupported("holds one body to a point in the world; not exported.");

    var A = NAMES["body:" + joint.bodyA], B = NAMES["body:" + joint.bodyB];
    var bodyA = bodies[joint.bodyA], bodyB = bodies[joint.bodyB];
    var resting = (bodyB.rotation || 0) - (bodyA.rotation || 0);
    var referenceAngle = pick(p, "referenceAngle", 0) ? short(resting + p.referenceAngle) : null;
    var motor = p.enableMotor || pick(p, "motorSpeed", 0);

    if (type === "revolute") {
        if (p.enableSpring)
            missing("revolute spring", "joint " + joint.name);
        anchored(bodyA, bodyB, a, b);
        // qml-box2d turns each end into Box2D's angle with its sign flipped,
        // which turns the range over: the editor's upper end goes in as the
        // lower one.
        var angles = ordered(clampAngle(pick(p, "lowerAngle", 0)), clampAngle(pick(p, "upperAngle", 0)));
        values.REFERENCE_ANGLE = referenceAngle;
        values.ENABLE_LIMIT = flag(p.enableLimit);
        values.LOWER_ANGLE = p.enableLimit ? short(angles.upper) : null;
        values.UPPER_ANGLE = p.enableLimit ? short(angles.lower) : null;
        values.ENABLE_MOTOR = flag(p.enableMotor);
        values.MOTOR_SPEED = motor ? short(pick(p, "motorSpeed", 0)) : null;
        values.MAX_MOTOR_TORQUE = motor ? plain(pick(p, "maxMotorTorque", 0)) : null;
        return build("revolute.qml.tmpl", A, B);
    }
    if (type === "prismatic") {
        if (p.enableSpring)
            missing("prismatic spring and target translation", "joint " + joint.name);
        anchored(bodyA, bodyB, a, b);
        var axis = localAxis(joint, bodyA);
        var origin = TRAVEL[index] || 0;
        var span = ordered(pick(p, "lowerTranslation", 0), pick(p, "upperTranslation", 0));
        values.AXIS = "Qt.point(" + num(axis.x) + ", " + num(axis.y) + ")";
        values.REFERENCE_ANGLE = referenceAngle;
        values.ENABLE_LIMIT = flag(p.enableLimit);
        values.LOWER_TRANSLATION = p.enableLimit ? short(span.lower + origin) : null;
        values.UPPER_TRANSLATION = p.enableLimit ? short(span.upper + origin) : null;
        values.ENABLE_MOTOR = flag(p.enableMotor);
        values.MOTOR_SPEED = motor ? num(prismaticMotor(pick(p, "motorSpeed", 0))) : null;
        values.MAX_MOTOR_FORCE = motor ? plain(pick(p, "maxMotorForce", 0)) : null;
        return build("prismatic.qml.tmpl", A, B);
    }
    if (type === "wheel") {
        if (p.enableLimit)
            missing("wheel joint limit", "joint " + joint.name);
        anchored(bodyA, bodyB, a, a);
        var wheelAxis = localAxis(joint, bodyA);
        var springs = pick(p, "enableSpring", true);
        values.AXIS = "Qt.point(" + num(wheelAxis.x) + ", " + num(wheelAxis.y) + ")";
        values.FREQUENCY = plain(springs ? pick(p, "hertz", 1) : 0);
        values.DAMPING_RATIO = plain(springs ? pick(p, "dampingRatio", 0.7) : 0);
        values.ENABLE_MOTOR = flag(p.enableMotor);
        values.MOTOR_SPEED = motor ? short(pick(p, "motorSpeed", 0)) : null;
        values.MAX_MOTOR_TORQUE = motor ? plain(pick(p, "maxMotorTorque", 0)) : null;
        return build("wheel.qml.tmpl", A, B);
    }
    if (type === "distance") {
        if (p.enableMotor)
            missing("distance joint motor", "joint " + joint.name);
        anchored(bodyA, bodyB, a, b);
        // A spring with no stiffness held by a maximum length is a rope.
        if (p.enableLimit && p.enableSpring && !pick(p, "hertz", 0)) {
            values.MAX_LENGTH = short(pick(p, "maxLength", pick(p, "length", 0)));
            return build("rope.qml.tmpl", A, B);
        }
        if (p.enableLimit)
            missing("distance joint limit", "joint " + joint.name);
        values.LENGTH = pick(p, "length", 0) > 0 ? short(p.length) : null;
        values.FREQUENCY = p.enableSpring ? plain(pick(p, "hertz", 0)) : null;
        values.DAMPING_RATIO = p.enableSpring ? plain(pick(p, "dampingRatio", 0)) : null;
        return build("distance.qml.tmpl", A, B);
    }
    if (type === "weld") {
        if (pickNumber(p.linearHertz, 0) && pickNumber(p.angularHertz, 0) && p.linearHertz !== p.angularHertz)
            missing("separate linear and angular weld springs", "joint " + joint.name + " -- the linear one is used");
        anchored(bodyA, bodyB, a, a);
        var hertz = pickNumber(p.linearHertz, 0) || pickNumber(p.angularHertz, 0);
        values.REFERENCE_ANGLE = referenceAngle;
        values.FREQUENCY = hertz ? plain(hertz) : null;
        values.DAMPING_RATIO = hertz
            ? plain(pickNumber(p.linearDampingRatio, 0) || pickNumber(p.angularDampingRatio, 0)) : null;
        return build("weld.qml.tmpl", A, B);
    }
    if (type === "motor") {
        values.LINEAR_OFFSET = has(p, "linearOffsetX") || has(p, "linearOffsetY")
            ? "Qt.point(" + short(pick(p, "linearOffsetX", 0)) + ", " + short(pick(p, "linearOffsetY", 0)) + ")"
            : null;
        values.ANGULAR_OFFSET = has(p, "angularOffset") ? short(p.angularOffset) : null;
        values.MAX_FORCE = plain(pick(p, "maxForce", 1));
        values.MAX_TORQUE = plain(pick(p, "maxTorque", 1));
        values.CORRECTION_FACTOR = plain(stepCorrection(pick(p, "correctionFactor", 0.3)));
        return build("motor.qml.tmpl", A, B);
    }
    missing("the filter joint", "joint " + joint.name + " -- it is not created");
    return unsupported("Box2D 2.3 has no filter joint; not exported.");
}

// qml-box2d hands a prismatic joint's motor speed to Box2D through its angle
// conversion -- degrees to radians, sign flipped -- so the speed the editor
// means, in pixels per second, is written in the units that undoes.
function prismaticMotor(pixelsPerSecond) {
    return -pixelsPerSecond / PPM * 180 / Math.PI;
}

// --- rays ------------------------------------------------------------------

function rayEnds(ray) {
    var angle = (ray.angle || 0) * Math.PI / 180;
    return { x0: ray.x || 0, y0: ray.y || 0,
             x1: (ray.x || 0) + Math.cos(angle) * (ray.length || 0),
             y1: (ray.y || 0) + Math.sin(angle) * (ray.length || 0) };
}

function raysCode(scene) {
    var rays = scene.rays || [];
    if (!rays.length)
        return "";
    var out = [render("objects/ray-caster.qml.tmpl", {})];
    for (var r = 0; r < rays.length; ++r) {
        var e = rayEnds(rays[r]);
        out.push(render("objects/ray.qml.tmpl", {
            NAME: rays[r].name, ID: NAMES["ray:" + r], X: short(e.x0), Y: short(e.y0),
        }));
    }
    return out.join(NEWLINE);
}

// --- the step: the scene's rules as code -----------------------------------
//
// qml-box2d reports contacts through each fixture's signals once the step is
// over, so the fixtures a rule watches note what happened and afterStep() acts
// on it. A condition on a value is checked after every step and acts on the
// step it first becomes true: a property remembers whether it was true last
// time.

function stepBody(scene, io) {
    var rules = scene.rules || [];
    var loops = { begun: [], ended: [], about: [] };
    var polls = [];
    var checks = [];

    for (var i = 0; i < rules.length; ++i) {
        var rule = rules[i];
        var n = i + 1;
        var caption = "// " + (rule.name ? rule.name : describe(rule));
        if (rule.enabled === false) {
            checks.push([caption, "// (switched off in the editor)"]);
            continue;
        }
        var made = ruleCode(scene, rule, n, caption, loops, polls);
        if (made.error) {
            io.log("Rule " + (rule.name || n) + " was not exported: " + made.error + ".");
            missing("rule " + (rule.name || n), made.error);
            checks.push([caption, "// Not exported: " + made.error + "."]);
        } else if (made.check) {
            checks.push(made.check);
        }
    }

    var out = [];
    if (COUNTERS.time)
        out.push("elapsed += dt;");
    if (COUNTERS.frame)
        out.push("++stepCount;");
    var rays = scene.rays || [];
    for (var r = 0; r < rays.length; ++r) {
        var e = rayEnds(rays[r]);
        var mask = filterBits(parseInt(String(rays[r].maskBits || "ffff"), 16), 0xFFFF, "ray " + rays[r].name);
        out.push(NAMES["ray:" + r] + " = castRay(" + short(e.x0) + ", " + short(e.y0) + ", "
                 + short(e.x1) + ", " + short(e.y1) + ", " + mask + ");");
    }
    if (loops.begun.length) {
        remember("contactsBegun", "[]");
        out = out.concat(pairLoop("contactsBegun", loops.begun));
    }
    if (loops.ended.length) {
        remember("contactsEnded", "[]");
        out = out.concat(pairLoop("contactsEnded", loops.ended));
    }
    if (loops.about.length) {
        remember("aboutToTouch", "[]");
        out = out.concat(pairLoop("aboutToTouch", loops.about));
    }
    for (var p = 0; p < polls.length; ++p) {
        out.push("");
        out = out.concat(polls[p]);
    }
    if (CHANGE_READS.length) {
        // Before any rule is judged, so they all see the same readings.
        out = CHANGE_READS.concat([""]).concat(out);
    }

    for (var c = 0; c < checks.length; ++c) {
        out.push("");
        out = out.concat(checks[c]);
    }

    if (CHANGE_SAVES.length) {
        // After every rule, so this step's readings become the step before's.
        out.push("");
        out = out.concat(CHANGE_SAVES);
    }
    for (var k = 0; k < rays.length; ++k) {
        var end = rayEnds(rays[k]);
        remember(NAMES["ray:" + k], "({ hit: false, fixture: null, point: Qt.point(" + short(end.x1) + ", "
                 + short(end.y1) + "), fraction: 1 })");
        HELPERS.castRay = true;
    }
    return wrapLong(out.join(NEWLINE), 92);
}

function describe(rule) {
    var conditions = conditionsOf(rule);
    var actions = actionsOf(rule);
    if (conditions.length > 1 || actions.length > 1) {
        var whens = [];
        for (var c = 0; c < conditions.length; ++c)
            whens.push(describeCondition(conditions[c]));
        var thens = [];
        for (var a = 0; a < actions.length; ++a)
            thens.push(describeAction(actions[a]));
        return "when " + whens.join(rule.join === "any" ? " or " : " and ")
               + ", " + thens.join("; ");
    }
    return "when " + describeCondition(conditions[0]) + ", " + describeAction(actions[0]);
}

function describeCondition(rule) {
    return rule.event
        ? (rule.subject + " " + rule.event + (rule.when ? (" " + rule.when) : ""))
        : (rule.subject + "." + rule.watch + " " + (rule.compare || ">") + " " + String(rule.when));
}

function describeAction(rule) {
    var then;
    if (rule.action)
        then = rule.action + " " + rule.target;
    else if (rule.sourceObject)
        then = rule.target + "." + rule.property + " " + (rule.op || "set") + " "
               + rule.sourceObject + "." + rule.sourceProperty;
    else
        then = rule.target + "." + rule.property + " " + (rule.op || "set")
               + (rule.value === null || rule.value === undefined ? "" : (" " + String(rule.value)));
    return then;
}

// --- a rule's conditions and actions ---------------------------------------
//
// Both are lists. A rule written before they were carries its one condition and
// its one action on itself, so the rule doubles as the single entry -- every
// helper below reads the same field names either way.

function conditionsOf(rule) {
    return (rule.conditions && rule.conditions.length) ? rule.conditions : [rule];
}

function actionsOf(rule) {
    return (rule.actions && rule.actions.length) ? rule.actions : [rule];
}

function isEventCondition(condition) {
    return !!condition.event;
}

// The conditions that are readings rather than events, as one expression.
// Joined the way the card says, and bracketed, since it may be dropped inside
// an event's own test.
function valueConditions(scene, conditions, join) {
    var parts = [];
    for (var i = 0; i < conditions.length; ++i) {
        var made = valueCondition(scene, conditions[i]);
        if (!made)
            return null;
        parts.push("(" + made + ")");
    }
    if (parts.length === 0)
        return null;
    return parts.length === 1 ? parts[0] : ("(" + parts.join(join) + ")");
}

// Every action of one rule, run together. A rule whose actions cannot all be
// written is not written at all: half of what the card says is worse than a
// note saying it was left out.
function allEffectLines(scene, rule, other) {
    var actions = actionsOf(rule);
    var lines = [];
    for (var i = 0; i < actions.length; ++i) {
        var made = effectLines(scene, actions[i], other);
        if (!made)
            return null;
        lines = lines.concat(made);
    }
    return lines.length ? lines : null;
}

// What this converter can and cannot write. Several readings joined are
// ordinary boolean logic; an event is not, because it exists only inside the
// loop over the step's events. One event with readings to narrow it is still
// one loop, so that works; two events would need both to be raised on the same
// step and the second is not in scope inside the first's loop.
function ruleShape(rule) {
    var conditions = conditionsOf(rule);
    var events = [];
    var values = [];
    for (var i = 0; i < conditions.length; ++i)
        (isEventCondition(conditions[i]) ? events : values).push(conditions[i]);

    var any = rule.join === "any";
    if (events.length > 1)
        return { error: "it watches more than one event, which this converter cannot join" };
    if (events.length === 1 && values.length > 0 && any) {
        return { error: "it joins an event with a reading using \"or\", which this"
                        + " converter cannot write" };
    }
    return { events: events, values: values, any: any,
             join: any ? " || " : " && " };
}

// An event with readings beside it: the loop over the step's events is what
// finds the event, and the readings narrow it from inside. Joined with "and",
// so everything has to hold at once.
function guarded(lines, guard) {
    if (!lines || !guard)
        return lines;
    return ["if (" + guard + ") {"].concat(indentLines("    ", lines)).concat(["}"]);
}

function ruleVariable(rule, n) {
    var words = String(rule.name || "").split(/[^A-Za-z0-9]+/).filter(function (w) { return w; });
    var text = "";
    for (var i = 0; i < words.length; ++i) {
        var w = words[i].toLowerCase();
        text += i === 0 ? w : (w.charAt(0).toUpperCase() + w.slice(1));
    }
    if (text && TAKEN[text])
        text += "Rule";
    return identifier(text, "rule" + n);
}

function remember(name, initial) {
    TAKEN[name] = true;
    STATE.push("property var " + name + ": " + initial);
}

function needsOther(rule) { return rule.target === "@other" || rule.target === "@otherBody"; }

// The same question of a whole rule: any one of its actions is enough.
function ruleNeedsOther(rule) {
    var actions = actionsOf(rule);
    for (var i = 0; i < actions.length; ++i) {
        if (needsOther(actions[i]))
            return true;
    }
    return false;
}

// Every fixture of whatever a rule watches reports its contacts.
function watch(scene, place, stream) {
    var bodies = scene.simulation.bodies;
    var parts = place.kind === "shape" ? [place.part] : (bodies[place.index].parts || []);
    for (var i = 0; i < parts.length; ++i) {
        if (!parts[i].name)
            continue;
        HANDLERS[parts[i].name] = HANDLERS[parts[i].name] || {};
        HANDLERS[parts[i].name][stream] = true;
    }
}

function ruleCode(scene, rule, n, caption, loops, polls) {
    var base = ruleVariable(rule, n);
    var once = { test: "", set: [] };
    if (rule.once) {
        var done = base + "Done";
        remember(done, "false");
        once = { test: " && !" + done, set: [done + " = true;"] };
    }
    // What the card asks for, and whether it can be written here at all.
    var shape = ruleShape(rule);
    if (shape.error)
        return { error: shape.error };
    // The event drives the code where there is one; otherwise the readings do.
    var primary = shape.events.length ? shape.events[0] : conditionsOf(rule)[0];
    // Readings alongside an event become a test inside its loop.
    var guard = shape.events.length && shape.values.length
                    ? valueConditions(scene, shape.values, " && ")
                    : null;
    if (shape.events.length && shape.values.length && !guard)
        return { error: "one of its readings cannot be read here" };
    var event = primary.event;
    var streams = { contactBegin: "begun", sensorBegin: "begun", contactEnd: "ended", sensorEnd: "ended",
                    contactHit: "begun", preSolve: "about" };

    // Carried out where a removal is written: see actionLines.
    if (event === "@aboutToBeRemoved")
        return {};

    if (streams[event]) {
        var place = resolve(scene, primary.subject);
        if (place && place.kind === "shape" && !place.part.name)
            place = null;
        if (!place || (place.kind !== "shape" && place.kind !== "body"))
            return { error: "cannot tell which shape " + primary.subject + " is" };
        var subject = side(scene, primary.subject);
        var partner = primary.when ? side(scene, String(primary.when)) : null;
        if (primary.when && !partner)
            return { error: "cannot tell which shape " + primary.when + " is" };
        var effect = guarded(allEffectLines(scene, rule, "b"), guard);
        if (!effect)
            return { error: "nothing in qml-box2d does " + describe(rule).split(", ")[1] };
        if (event === "preSolve")
            HELPERS.preSolve = true;
        else
            watch(scene, place, streams[event]);
        var sensorEvent = event.indexOf("sensor") === 0;
        var guard = sensorEvent ? "(a.sensor || b.sensor)" : "!a.sensor && !b.sensor";
        if (event === "contactHit") {
            // qml-box2d reports no hits: a contact that begins with the two
            // bodies closing faster than the world's hit threshold is one.
            HELPERS.hitSpeed = true;
            guard += " && hitSpeed(a, b) >= "
                     + num(pickNumber(vals(scene.world).hitEventThreshold, 1) * MOTION);
        }
        loops[streams[event]].push({ caption: caption, subject: subject, partner: partner,
                                     guard: guard, effect: effect, once: once });
        return {};
    }

    var condition, startsTrue = "false", effectOther = null;
    if (event === "@runStarted") {
        // Carried out once, after the first step.
        condition = "true";
    } else if (event === "bodyMoved" || event === "bodyFellAsleep") {
        var body = resolve(scene, primary.subject);
        if (body && body.kind === "shape")
            body = { kind: "body", handle: body.bodyHandle, index: body.body };
        if (!body || body.kind !== "body")
            return { error: primary.subject + " is not a body" };
        // qml-box2d reports neither, so whether the body is awake is watched.
        var was = body.handle + "WasAwake";
        if (!TAKEN[was]) {
            remember(was, "false");
            polls.push(["var " + body.handle + "Awake = " + body.handle + ".active && " + body.handle + ".awake;"]);
            polls.push([was + " = " + body.handle + "Awake;"]);
        }
        var awake = body.handle + "Awake";
        condition = event === "bodyMoved" ? awake : ("!" + awake + " && " + was);
        var actions0 = guarded(allEffectLines(scene, rule, null), guard);
        if (!actions0)
            return { error: "nothing in qml-box2d does " + describe(rule).split(", ")[1] };
        // Checked between reading the state and remembering it.
        polls.splice(polls.length - 1, 0, edgeBlock(caption, base, condition, actions0, once, "false"));
        return {};
    } else if (event === "rayDetects") {
        var ray = resolve(scene, primary.subject);
        if (!ray || ray.kind !== "ray")
            return { error: primary.subject + " is not a ray" };
        condition = ray.handle + ".hit";
        if (primary.when) {
            var seen = side(scene, String(primary.when));
            if (!seen)
                return { error: "cannot tell which shape " + primary.when + " is" };
            condition += " && " + seen.test(ray.handle + ".fixture");
        }
        effectOther = ray.handle + ".fixture";
    } else if (event === "limitLower" || event === "limitUpper" || event === "limitEither") {
        var joint = resolve(scene, primary.subject);
        var reading = joint && joint.kind === "joint" ? LIMITS[joint.type] : null;
        if (!reading)
            return { error: primary.subject + " has no limit qml-box2d reports" };
        var now = jointProperty(joint, reading.value).read;
        var lowEnd = jointProperty(joint, reading.lower).read;
        var highEnd = jointProperty(joint, reading.upper).read;
        var lower = now + " <= " + lowEnd + " + " + reading.slack;
        var upper = now + " >= " + highEnd + " - " + reading.slack;
        var at = event === "limitLower" ? lower : event === "limitUpper" ? upper
               : ("(" + lower + " || " + upper + ")");
        condition = joint.handle + ".enableLimit && " + at;
        if (LOST)
            condition = joint.handle + ".bodyB && " + condition;
        startsTrue = "true";
    } else if (event) {
        return { error: "qml-box2d has no " + event + " event to read" };
    } else {
        condition = valueConditions(scene, shape.values, shape.join);
        if (!condition)
            return { error: "one of its readings cannot be read here" };
    }

    var actions = guarded(allEffectLines(scene, rule, effectOther), guard);
    if (!actions)
        return { error: "nothing in qml-box2d does " + describe(rule).split(", ")[1] };
    return { check: edgeBlock(caption, base, condition, actions, once, startsTrue) };
}

// How far along a joint is, and its two ends, with how near counts as there.
var LIMITS = {
    revolute: { value: "angle", lower: "lowerAngle", upper: "upperAngle", slack: "0.3" },
    prismatic: { value: "translation", lower: "lowerTranslation", upper: "upperTranslation", slack: "0.5" },
};

function edgeBlock(caption, base, condition, effect, once, startsTrue) {
    var before = base + "Before";
    remember(before, startsTrue);
    var lines = [caption, "var " + base + " = " + condition + ";",
                 "if (" + base + " && !" + before + once.test + ") {"];
    lines = lines.concat(indentLines("    ", effect.concat(once.set)));
    lines.push("}");
    lines.push(before + " = " + base + ";");
    return lines;
}

// Each watched fixture reported itself first and the other party second, so
// the subject is always `a`.
function pairLoop(array, watchers) {
    var lines = ["", "for (var i = 0; i < " + array + ".length; ++i) {",
                 "    var a = " + array + "[i][0];", "    var b = " + array + "[i][1];"];
    for (var i = 0; i < watchers.length; ++i) {
        var w = watchers[i];
        lines.push("");
        lines.push("    " + w.caption);
        var test = w.subject.test("a");
        if (w.partner)
            test += " && " + w.partner.test("b");
        test = w.guard + " && " + test;
        if (w.once.test)
            test = "(" + test + ")" + w.once.test;
        lines.push("    if (" + test + ") {");
        lines = lines.concat(indentLines("        ", w.effect.concat(w.once.set)));
        lines.push("    }");
    }
    lines.push("}");
    lines.push(array + " = [];");
    return lines;
}

function side(scene, name) {
    var place = resolve(scene, name);
    if (!place)
        return null;
    if (place.kind === "shape" && !place.outline && place.part.name) {
        USED[place.part.name] = true;
        var id = fixtureId(place.part, 0);
        return { test: function (v) { return v + " === " + id; } };
    }
    if (place.kind === "shape" || place.kind === "body") {
        var body = place.kind === "shape" ? place.bodyHandle : place.handle;
        return { test: function (v) { return v + ".getBody() === " + body; } };
    }
    return null;
}

// --- what a name refers to, and what can be read or written on it ----------

function resolve(scene, name) {
    if (!name)
        return null;
    if (name === "@world")
        return { kind: "world" };
    var rays = scene.rays || [];
    for (var r = 0; r < rays.length; ++r) {
        if (rays[r].name === name)
            return { kind: "ray", handle: NAMES["ray:" + r], ray: rays[r] };
    }
    var explosions = scene.explosions || [];
    for (var x = 0; x < explosions.length; ++x) {
        if (explosions[x].name === name)
            return { kind: "explosion", explosion: explosions[x] };
    }
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j) {
        if (joints[j].name === name)
            return { kind: "joint", type: joints[j].type, handle: NAMES["joint:" + j], index: j };
    }
    var bodies = scene.simulation.bodies;
    for (var b = 0; b < bodies.length; ++b) {
        if (bodies[b].name === name)
            return { kind: "body", handle: NAMES["body:" + b], index: b };
    }
    for (var i = 0; i < bodies.length; ++i) {
        var parts = bodies[i].parts || [];
        for (var p = 0; p < parts.length; ++p) {
            if (parts[p].name !== name)
                continue;
            return { kind: "shape", part: parts[p], outline: isOutline(parts[p]),
                     shapeKind: parts[p].kind, bodyHandle: NAMES["body:" + i], body: i,
                     handle: NAMES["shape:" + name] };
        }
    }
    return null;
}

var SHAPE_KEYS = {
    density: 1, friction: 1, restitution: 1, rollingResistance: 1, tangentSpeed: 1,
    categoryBits: 1, maskBits: 1, groupIndex: 1, radius: 1, isSensor: 1, mass: 1,
    enableContactEvents: 1, enableHitEvents: 1, enableSensorEvents: 1,
};

function targetOf(scene, rule, other) {
    if (!needsOther(rule)) {
        var place = resolve(scene, rule.target);
        if (place && place.kind === "shape" && !place.outline && place.part.name)
            USED[place.part.name] = true;
        return place;
    }
    if (!other)
        return null;
    if (rule.target === "@otherBody" || (rule.property && !SHAPE_KEYS[rule.property]) || rule.action)
        return { kind: "body", handle: other + ".getBody()", other: true };
    return { kind: "shape", handle: other, bodyHandle: other + ".getBody()", other: true, shapeKind: "" };
}

function prop(unit, read, write) { return { unit: unit, read: read, write: write }; }

function property(place, key) {
    if (place.kind === "world") return worldProperty(key);
    if (place.kind === "body")  return bodyProperty(place.handle, key);
    if (place.kind === "shape") return place.outline ? null : shapeProperty(place, key);
    if (place.kind === "joint") return jointProperty(place, key);
    if (place.kind === "ray")   return rayProperty(place, key);
    return null;
}

// A property the step code reads and writes directly: `handle.field`, with
// whatever has to follow a change.
function field(unit, handle, name, after) {
    return prop(unit, handle + "." + name, function (v) {
        return [handle + "." + name + " = " + v + ";"].concat(after || []);
    });
}

function worldProperty(key) {
    var motion = num(MOTION);
    if (key === "time") {
        if (!COUNTERS.time) {
            COUNTERS.time = true;
            remember("elapsed", "0");
        }
        return prop("num", "elapsed", null);
    }
    if (key === "frame") {
        if (!COUNTERS.frame) {
            COUNTERS.frame = true;
            remember("stepCount", "0");
        }
        return prop("int", "stepCount", null);
    }
    if (key === "gravityX")
        return prop("num", "(world.gravity.x / " + motion + ")", function (v) {
            return ["world.gravity = Qt.point((" + v + ") * " + motion + ", world.gravity.y);"];
        });
    if (key === "gravityY")
        return prop("num", "(world.gravity.y / " + motion + ")", function (v) {
            return ["world.gravity = Qt.point(world.gravity.x, (" + v + ") * " + motion + ");"];
        });
    return null;
}

function bodyProperty(B, key) {
    var awake = B + ".awake = true;";
    var velocity = B + ".linearVelocity";
    switch (key) {
    case "positionX": return field("num", B + ".target", "x", [awake]);
    case "positionY": return field("num", B + ".target", "y", [awake]);
    case "angle": return field("num", B + ".target", "rotation", [awake]);
    case "velocityX":
        return prop("num", "(" + velocity + ".x * ppm)", function (v) {
            return [velocity + " = Qt.point((" + v + ") / ppm, " + velocity + ".y);", awake];
        });
    case "velocityY":
        return prop("num", "(" + velocity + ".y * ppm)", function (v) {
            return [velocity + " = Qt.point(" + velocity + ".x, (" + v + ") / ppm);", awake];
        });
    case "speed":
        return prop("num", "(Math.sqrt(" + velocity + ".x * " + velocity + ".x + " + velocity + ".y * "
                    + velocity + ".y) * ppm)", null);
    case "angularVelocity": return field("num", B, "angularVelocity", [awake]);
    case "impulseX":
    case "impulseY":
        return prop("num", null, function (v) {
            return [B + ".applyLinearImpulse(Qt.point(" + (key === "impulseX" ? ("(" + v + ") / ppm, 0") : ("0, (" + v + ") / ppm"))
                    + "), " + B + ".getWorldCenter());"];
        });
    case "forceX":
    case "forceY":
        return prop("num", null, function (v) {
            return [B + ".applyForceToCenter(Qt.point(" + (key === "forceX" ? ("(" + v + ") / ppm, 0") : ("0, (" + v + ") / ppm"))
                    + "));"];
        });
    // Box2D turns the other way round from the editor, and a torque comes
    // down by the scale twice: a force times a distance.
    case "torque":
        return prop("num", null, function (v) { return [B + ".applyTorque(-(" + v + ") / (ppm * ppm));"]; });
    case "angularImpulse":
        return prop("num", null, function (v) { return [B + ".applyAngularImpulse(-(" + v + ") / (ppm * ppm));"]; });
    case "gravityScale": return field("num", B, "gravityScale");
    case "linearDamping": return field("num", B, "linearDamping");
    case "angularDamping": return field("num", B, "angularDamping");
    case "mass": return prop("num", B + ".getMass()", null);
    case "rotationalInertia": return prop("num", B + ".getInertia()", null);
    case "kineticEnergy":
        return prop("num", "(500 * (" + B + ".getMass() * (" + velocity + ".x * " + velocity + ".x + "
                    + velocity + ".y * " + velocity + ".y) + " + B + ".getInertia() * Math.pow("
                    + B + ".angularVelocity * Math.PI / 180, 2)))", null);
    case "isAwake": return field("bool", B, "awake");
    case "isEnabled": return field("bool", B, "active");
    case "fixedRotation": return field("bool", B, "fixedRotation");
    case "isBullet": return field("bool", B, "bullet");
    case "enableSleep": return field("bool", B, "sleepingAllowed");
    case "bodyType": return field("int", B, "bodyType");
    case "centerOfMassX": return prop("num", B + ".getWorldCenter().x", null);
    case "centerOfMassY": return prop("num", B + ".getWorldCenter().y", null);
    }
    return null;
}

function shapeProperty(place, key) {
    var F = place.other ? place.handle : fixtureId(place.part, 0);
    var awake = [F + ".getBody().awake = true;"];
    switch (key) {
    case "density": return field("num", F, "density", [F + ".getBody().resetMassData();"]);
    case "friction": return field("num", F, "friction", awake);
    case "restitution": return field("num", F, "restitution", awake);
    case "categoryBits": return field("bits", F, "categories", awake);
    case "maskBits": return field("bits", F, "collidesWith", awake);
    case "groupIndex": return field("int", F, "groupIndex", awake);
    case "isSensor": return field("bool", F, "sensor", awake);
    case "radius": return place.shapeKind === "circle" ? field("num", F, "radius", awake) : null;
    case "mass": return null;
    }
    if (key === "lastHitSpeed" || key.indexOf("lastHit") === 0)
        missing("hit events", "shape " + (place.part ? place.part.name : ""));
    return null;
}

function jointProperty(place, key) {
    var J = place.handle;
    var t = place.type;
    var wake = [J + ".bodyA.awake = true;", J + ".bodyB.awake = true;"];
    var simple = function (unit, name) { return field(unit, J, name, wake); };

    if (key === "collideConnected") return prop("bool", J + ".collideConnected", null);

    if (t === "revolute" || t === "prismatic" || t === "wheel") {
        if (key === "enableMotor" || key === "motorEnabled") return simple("bool", "enableMotor");
        if (key === "maxMotorTorque" && t !== "prismatic") return simple("num", "maxMotorTorque");
    }
    if ((t === "revolute" || t === "prismatic") && (key === "enableLimit" || key === "limitEnabled"))
        return simple("bool", "enableLimit");

    if (t === "revolute") {
        switch (key) {
        case "angle": return prop("num", J + ".getJointAngle()", null);
        case "motorSpeed": return simple("num", "motorSpeed");
        // Swapped: see the joint itself.
        case "lowerAngle": return simple("num", "upperAngle");
        case "upperAngle": return simple("num", "lowerAngle");
        }
    } else if (t === "prismatic") {
        var origin = short(TRAVEL[place.index] || 0);
        var travel = function (name) {
            return prop("num", "(" + J + "." + name + " - " + origin + ")", function (v) {
                return wake.concat([J + "." + name + " = (" + v + ") + " + origin + ";"]);
            });
        };
        var k = num(prismaticMotor(1));
        switch (key) {
        case "translation": return prop("num", "(" + J + ".getJointTranslation() - " + origin + ")", null);
        case "speed": return prop("num", "(" + J + ".getJointSpeed() * ppm)", null);
        case "motorSpeed":
            return prop("num", "(" + J + ".motorSpeed / " + k + ")", function (v) {
                return wake.concat([J + ".motorSpeed = (" + v + ") * " + k + ";"]);
            });
        case "maxMotorForce": return simple("num", "maxMotorForce");
        case "lowerTranslation": return travel("lowerTranslation");
        case "upperTranslation": return travel("upperTranslation");
        }
    } else if (t === "wheel") {
        switch (key) {
        case "translation": return prop("num", J + ".getJointTranslation()", null);
        case "motorSpeed": return simple("num", "motorSpeed");
        case "hertz": return simple("num", "frequencyHz");
        case "dampingRatio": return simple("num", "dampingRatio");
        }
    } else if (t === "distance") {
        switch (key) {
        case "length": return simple("num", "length");
        case "hertz": return simple("num", "frequencyHz");
        case "dampingRatio": return simple("num", "dampingRatio");
        }
    } else if (t === "motor") {
        switch (key) {
        case "maxForce": return simple("num", "maxForce");
        case "maxTorque": return simple("num", "maxTorque");
        case "correctionFactor":
            // In the editor's terms: per sub-step, as Box2D v3 takes it.
            return prop("num", "(1 - Math.pow(1 - " + J + ".correctionFactor, 1 / " + SUB_STEPS + "))",
                        function (v) {
                return wake.concat([J + ".correctionFactor = 1 - Math.pow(1 - Math.max(0, Math.min(1, " + v
                                    + ")), " + SUB_STEPS + ");"]);
            });
        case "angularOffset": return simple("num", "angularOffset");
        case "linearOffsetX":
        case "linearOffsetY":
            var mine = key === "linearOffsetX" ? "x" : "y";
            return prop("num", J + ".linearOffset." + mine, function (v) {
                var keep = J + ".linearOffset." + (mine === "x" ? "y" : "x");
                return wake.concat([J + ".linearOffset = Qt.point("
                                    + (mine === "x" ? (v + ", " + keep) : (keep + ", " + v)) + ");"]);
            });
        }
    } else if (t === "mouse") {
        switch (key) {
        case "maxForce": return simple("num", "maxForce");
        case "hertz": return simple("num", "frequencyHz");
        case "dampingRatio": return simple("num", "dampingRatio");
        case "targetX":
        case "targetY":
            var axis = key === "targetX" ? "x" : "y";
            return prop("num", J + ".target." + axis, function (v) {
                var keep = J + ".target." + (axis === "x" ? "y" : "x");
                return wake.concat([J + ".target = Qt.point("
                                    + (axis === "x" ? (v + ", " + keep) : (keep + ", " + v)) + ");"]);
            });
        }
    } else if (t === "weld") {
        switch (key) {
        case "linearHertz": case "angularHertz": return simple("num", "frequencyHz");
        case "linearDampingRatio": case "angularDampingRatio": return simple("num", "dampingRatio");
        }
    }
    return null;
}

function rayProperty(place, key) {
    var R = place.handle;
    if (key === "hit") return prop("bool", R + ".hit", null);
    if (key === "distance") return prop("num", "(" + R + ".fraction * " + short(place.ray.length || 0) + ")", null);
    if (key === "hitX") return prop("num", R + ".point.x", null);
    if (key === "hitY") return prop("num", R + ".point.y", null);
    return null;
}

// --- values ----------------------------------------------------------------

function literal(unit, value) {
    var x = Number(value) || 0;
    if (unit === "bool") return value === true || value === 1 || value === "true" ? "true" : "false";
    if (unit === "int")  return String(Math.round(x));
    if (unit === "bits") return String(filterBits(x, 0, "a rule"));
    return short(x);
}

// --- "changed to" and "changed from" ---------------------------------------
//
// Both ask what the reading was on the step before, which no engine keeps, so
// the generated code keeps it: a flag per condition saying whether the reading
// equalled the value, read once at the top of the step and saved again at the
// bottom. Reading it once is what the editor does too, so every condition on a
// step sees the same answer however far down the list it sits and whatever the
// rules above it have already done.
//
// CHANGE_READS are the lines at the top of the step, CHANGE_SAVES the ones at
// the bottom, and CHANGE_STARTED is the flag that keeps the first step quiet:
// there is no step before it, so nothing has changed yet.
var CHANGES = 0;
var CHANGE_READS = null;
var CHANGE_SAVES = null;
var CHANGE_STARTED = "rulesStarted";

function changeFlag(scene, rule) {
    // The equality this condition turns on, built by the ordinary machinery
    // below so units, flags and rays are all handled the one way.
    var eq = valueCondition(scene, { subject: rule.subject, watch: rule.watch,
                                     when: rule.when, compare: "=" });
    if (!eq)
        return null;

    if (CHANGES === 0) {
        remember(CHANGE_STARTED, "false");
        CHANGE_SAVES.push(CHANGE_STARTED + " = true;");
    }
    var n = ++CHANGES;
    var now = "nowEq" + n;
    var was = "wasEq" + n;
    remember(was, "false");
    CHANGE_READS.push("var " + now + " = " + eq + ";");
    CHANGE_SAVES.push(was + " = " + now + ";");

    var became = "(" + now + " && !" + was + ")";
    var ceased = "(!" + now + " && " + was + ")";
    return CHANGE_STARTED + " && " + (rule.compare === "->" ? became : ceased);
}

function valueCondition(scene, rule) {
    var subject = resolve(scene, rule.subject);
    if (!subject)
        return null;
    var compare = rule.compare || ">";
    if (compare === "->" || compare === "<-")
        return changeFlag(scene, rule);
    if (subject.kind === "ray" && rule.watch === "hitName") {
        var seen = side(scene, String(rule.when));
        if (!seen || (compare !== "=" && compare !== "!="))
            return null;
        var test = subject.handle + ".hit && " + seen.test(subject.handle + ".fixture");
        return compare === "=" ? test : ("!(" + test + ")");
    }
    if (subject.kind === "shape" && !subject.outline && subject.part.name)
        USED[subject.part.name] = true;
    var p = property(subject, rule.watch);
    if (!p || !p.read)
        return null;
    var guard = live(subject);
    var result;
    if (p.unit === "bool") {
        var wanted = literal("bool", rule.when) === "true";
        if (compare === "=")
            result = (wanted ? "" : "!") + p.read;
        else if (compare === "!=")
            result = (wanted ? "!" : "") + p.read;
        else
            return null;
    } else {
        var against = literal(p.unit, rule.when);
        if (compare === "%") {
            // Whole numbers, a non-zero multiple.
            var step = Math.round(Number(rule.when) || 0);
            if (step === 0)
                return "false";
            var whole = "Math.round(" + p.read + ")";
            result = "(" + whole + " !== 0 && " + whole + " % " + step + " === 0)";
        } else if (compare === "=") {
            result = "Math.abs(" + p.read + " - " + against + ") < 1e-4";
        } else if (compare === "!=") {
            result = "Math.abs(" + p.read + " - " + against + ") >= 1e-4";
        } else {
            result = p.read + " " + compare + " " + against;
        }
    }
    return guard ? (guard + " && " + result) : result;
}

// Once a rule has taken something out, what it held is no longer to be read.
function live(place) {
    if (!LOST || !place || place.other)
        return "";
    if (place.kind === "body") return place.handle + ".active";
    if (place.kind === "shape") return place.bodyHandle + ".active";
    if (place.kind === "joint") return place.handle + ".bodyB";
    return "";
}

function effectLines(scene, rule, other) {
    var target = targetOf(scene, rule, other);
    if (!target)
        return null;
    var lines = rule.action ? actionLines(scene, rule, target) : writeLines(scene, rule, target);
    if (!lines)
        return null;
    var guard = live(target);
    if (!guard)
        return lines;
    return ["if (" + guard + ") {"].concat(indentLines("    ", lines)).concat(["}"]);
}

function writeLines(scene, rule, target) {
    var p = property(target, rule.property);
    if (!p || !p.write) {
        missing("setting " + rule.property + " from a rule", "rule " + (rule.name || ""));
        return null;
    }
    var op = rule.op || "set";
    var value;
    if (op === "toggle") {
        if (p.unit !== "bool" || !p.read)
            return null;
        value = "!" + p.read;
    } else if (op === "negate") {
        if (p.unit === "bool" || !p.read)
            return null;
        value = "-" + p.read;
    } else if (rule.sourceObject && rule.sourceProperty) {
        var source = resolve(scene, rule.sourceObject);
        var from = source ? property(source, rule.sourceProperty) : null;
        if (!from || !from.read || from.unit === "bool")
            return null;
        value = from.read + (rule.sourceOffset ? (" + " + short(rule.sourceOffset)) : "");
        if ((op === "add" || op === "subtract") && p.read)
            value = p.read + (op === "add" ? " + " : " - ") + value;
    } else if (op === "add" || op === "subtract") {
        if (!p.read || p.unit === "bool")
            return null;
        value = p.read + (op === "add" ? " + " : " - ") + literal(p.unit, rule.value);
    } else {
        value = literal(p.unit, rule.value);
    }
    return p.write(value);
}

function actionLines(scene, rule, target) {
    var params = rule.actionParams || {};
    var body = target.kind === "shape" ? target.bodyHandle : target.handle;
    var isBody = target.kind === "body" || target.kind === "shape";

    if (rule.action === "@stopRun")
        return ["scene.stopRequested();"];
    if (rule.action === "@holdRun")
        return ["scene.holdRequested();"];
    if (rule.action === "@initState") {
        if (!isBody)
            return null;
        HELPERS.initState = true;
        return ["initState(" + body + ");"];
    }
    if (rule.action === "explode") {
        var settings = {}, key;
        for (key in params) {
            if (has(params, key))
                settings[key] = params[key];
        }
        var at = null;
        if (target.kind === "explosion") {
            at = short(target.explosion.x) + ", " + short(target.explosion.y);
            var own = target.explosion.params || {};
            for (key in own) {
                if (has(own, key))
                    settings[key] = own[key];
            }
        } else if (isBody) {
            // Where the body stands, as the editor's engine takes it.
            at = body + ".target.x, " + body + ".target.y";
        }
        var radius = pickNumber(settings.radius, 0);
        if (!at || !(radius > 0))
            return null;
        HELPERS.explode = true;
        var mask = pickNumber(settings.maskBits, 0);
        return ["explode(" + at + ", " + short(pickNumber(settings.impulse, 0)) + ", " + short(radius) + ", "
                + short(pickNumber(settings.falloff, 0)) + ", " + (mask > 0 ? filterBits(mask, 0xFFFF, "explosion") : 0) + ");"];
    }
    if (rule.action === "pushAt" || rule.action === "pushForceAt") {
        if (!isBody)
            return null;
        var x = pickNumber(rule.action === "pushAt" ? params.impulseX : params.forceX, pickNumber(params.impulseX, 0));
        var y = pickNumber(rule.action === "pushAt" ? params.impulseY : params.forceY, pickNumber(params.impulseY, 0));
        var call = rule.action === "pushAt" ? "applyLinearImpulse" : "applyForce";
        return [body + "." + call + "(Qt.point(" + short(x) + " / ppm, " + short(y) + " / ppm), Qt.point("
                + body + ".getWorldCenter().x + " + short(pickNumber(params.offsetX, 0)) + ", "
                + body + ".getWorldCenter().y + " + short(pickNumber(params.offsetY, 0)) + "));"];
    }
    if (rule.action === "resetMass")
        return isBody ? [body + ".resetMassData();"] : null;
    if (rule.action === "removeBody") {
        if (!isBody)
            return null;
        // Taken out of the world and out of sight; its joints stop with it --
        // unless the body answers "to be removed".
        return removal(scene, rule, target, body, function (doomed) {
            return [doomed + ".active = false;", doomed + ".target.visible = false;"];
        });
    }
    if (rule.action === "@clone") {
        // A copy of a body named in the rule; one met through "the other" is
        // not known until the run, and there is nothing to copy it from.
        var index = target.kind === "shape" ? target.body : target.index;
        if (!isBody || index === undefined)
            return null;
        HELPERS.clones[index] = true;
        return [NAMES["body:" + index] + "Clone.createObject(field, { x: " + short(pickNumber(params.x, 0))
                + ", y: " + short(pickNumber(params.y, 0)) + " });"];
    }
    if (rule.action === "breakJoint") {
        if (target.kind !== "joint")
            return null;
        // Letting go of a body destroys the joint in qml-box2d.
        return [target.handle + ".bodyA.awake = true;", target.handle + ".bodyB.awake = true;",
                target.handle + ".bodyB = null;"];
    }
    return null;
}

// --- "to be removed" ---------------------------------------------------------
//
// Before a rule removes a body, the editor looks for rules on that body (or its
// shapes) waiting for "to be removed"; if there are any, they are carried out
// instead and the body stays. "The other object" in an answer is the subject of
// the rule that did the removing. An answer that itself removes really removes.

function answersByBody(scene) {
    var byBody = {};
    var rules = scene.rules || [];
    var bodies = scene.simulation.bodies;
    for (var i = 0; i < rules.length; ++i) {
        var rule = rules[i];
        if (rule.enabled === false)
            continue;
        // Any condition watching the removal makes the rule an answer; the
        // others are not weighed, since there is no step in which to read one.
        var conditions = conditionsOf(rule);
        var watching = null;
        for (var w = 0; w < conditions.length; ++w) {
            if (conditions[w].event === "@aboutToBeRemoved")
                watching = conditions[w];
        }
        if (!watching)
            continue;
        for (var b = 0; b < bodies.length; ++b) {
            var named = bodies[b].name === watching.subject;
            var parts = bodies[b].parts || [];
            for (var p = 0; p < parts.length && !named; ++p)
                named = parts[p].name === watching.subject;
            if (named)
                (byBody[b] = byBody[b] || []).push(rule);
        }
    }
    return byBody;
}

function removal(scene, rule, target, body, remove) {
    var answers = ANSWERING ? {} : answersByBody(scene);
    var remover = resolve(scene, conditionsOf(rule)[0].subject);
    var other = null;
    if (remover && remover.kind === "shape" && !remover.outline && remover.part.name) {
        USED[remover.part.name] = true;
        other = fixtureId(remover.part, 0);
    }
    var answer = function (rules) {
        ANSWERING = true;
        var lines = [];
        for (var i = 0; i < rules.length; ++i) {
            var made = allEffectLines(scene, rules[i], other);
            lines = lines.concat(made || ["// (" + (rules[i].name || "an answer") + " could not be exported)"]);
        }
        ANSWERING = false;
        return lines;
    };
    var index = target.kind === "shape" ? target.body : target.index;
    if (index !== undefined)
        return answers[index] ? answer(answers[index]) : remove(body);
    var keys = Object.keys(answers);
    if (!keys.length)
        return remove(body);
    // Met through "the other": which body it is is only known during the run.
    var lines = ["var doomed = " + body + ";"];
    for (var k = 0; k < keys.length; ++k) {
        lines.push((k ? "} else if (" : "if (") + "doomed === " + NAMES["body:" + keys[k]] + ") {");
        lines = lines.concat(indentLines("    ", answer(answers[keys[k]])));
    }
    lines.push("} else {");
    lines = lines.concat(indentLines("    ", remove("doomed")));
    lines.push("}");
    return lines;
}

// Each body a rule copies, as a Component made on demand: built as the
// original is, with no ids of its own and no rules watching it -- the editor's
// rules name no clone either.
function clonesCode(scene, colours) {
    var out = [];
    var bodies = scene.simulation.bodies;
    for (var index in HELPERS.clones) {
        if (!has(HELPERS.clones, index))
            continue;
        CLONING = true;
        var body = bodyCode(bodies[index], index, colours);
        CLONING = false;
        out.push(render("objects/clone.qml.tmpl", {
            NAME: bodies[index].name, ID: NAMES["body:" + index] + "Clone", BODY: body,
        }));
    }
    return out.join(NEWLINE + NEWLINE);
}

function destroys(scene) {
    var rules = scene.rules || [];
    for (var i = 0; i < rules.length; ++i) {
        var acts = actionsOf(rules[i]);
        var destroying = false;
        for (var a = 0; a < acts.length; ++a)
            destroying = destroying || acts[a].action === "removeBody"
                         || acts[a].action === "breakJoint";
        if (destroying)
            return true;
    }
    return false;
}

// --- helpers the step code calls -------------------------------------------

function helpersCode(scene) {
    var out = [];
    var bodies = scene.simulation.bodies;
    if (HELPERS.initState) {
        var starts = [];
        for (var b = 0; b < bodies.length; ++b) {
            var p = bodies[b].position || { x: 0, y: 0 };
            starts.push("[" + NAMES["body:" + b] + ", " + short(p.x) + ", " + short(p.y) + ", "
                        + short(bodies[b].rotation || 0) + "]");
        }
        out.push(render("objects/init-state.qml.tmpl", { STARTS: listValue(starts) }));
    }
    if (HELPERS.explode)
        out.push(render("objects/explode.qml.tmpl", { BLAST_SHAPES: listValue(blastShapes(bodies)) }));
    if (HELPERS.hitSpeed)
        out.push(render("objects/hit-speed.qml.tmpl", {}));
    if (HELPERS.castRay)
        out.push(render("objects/cast-ray.qml.tmpl", {}));
    return out.join(NEWLINE + NEWLINE);
}

// Every shape of every dynamic body as an explosion sees it: its outline in the
// body's own frame (a circle is its centre), its radius and its collision group.
function blastShapes(bodies) {
    var out = [];
    for (var b = 0; b < bodies.length; ++b) {
        if (bodies[b].type !== "dynamic")
            continue;
        var parts = bodies[b].parts || [];
        for (var p = 0; p < parts.length; ++p) {
            var part = parts[p], c = part.center || { x: 0, y: 0 }, pts = [], radius = 0;
            if (part.kind === "circle") {
                pts = [c];
                radius = part.radius;
            } else if (part.kind === "box") {
                var hw = part.halfExtents.x, hh = part.halfExtents.y;
                var turn = (part.rotation || 0) * Math.PI / 180;
                var corners = [[-hw, -hh], [hw, -hh], [hw, hh], [-hw, hh]];
                for (var k = 0; k < 4; ++k) {
                    pts.push({ x: c.x + corners[k][0] * Math.cos(turn) - corners[k][1] * Math.sin(turn),
                               y: c.y + corners[k][0] * Math.sin(turn) + corners[k][1] * Math.cos(turn) });
                }
            } else if (isSolidPolygon(part)) {
                pts = part.points;
            } else {
                continue;
            }
            var list = [];
            for (var i = 0; i < pts.length; ++i)
                list.push("[" + short(pts[i].x) + ", " + short(pts[i].y) + "]");
            out.push("[" + NAMES["body:" + b] + ", [" + list.join(", ") + "], " + short(radius) + ", "
                     + filterBits(vals(part).categoryBits, 1, part.name) + "]");
        }
    }
    return out;
}

// --- the window around it --------------------------------------------------

function controlsCode(own) {
    if (!own.addControls && !own.debugView)
        return "";
    return render("objects/toolbar.qml.tmpl", {
        BUTTONS: own.addControls ? render("objects/buttons.qml.tmpl", {}) : "",
        DEBUG_SWITCH: own.debugView ? render("objects/debug-switch.qml.tmpl", {}) : "",
    });
}

// --- odds and ends ---------------------------------------------------------

function wrapLong(text, width) {
    var lines = text.split(NEWLINE);
    var out = [];
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        var lead = line.match(/^ */)[0];
        while (line.length > width && line.replace(/^ */, "").indexOf("//") !== 0) {
            var cut = -1;
            var ops = [" || ", " && "];
            for (var o = 0; o < ops.length && cut < 0; ++o) {
                var at = line.lastIndexOf(ops[o], width - 2);
                if (at > lead.length + 8)
                    cut = at + ops[o].length - 1;
            }
            if (cut < 0)
                break;
            out.push(line.substring(0, cut));
            line = lead + "    " + line.substring(cut + 1);
        }
        out.push(line);
    }
    return out.join(NEWLINE);
}

function wrapList(head, items, tail) {
    var out = [];
    var line = head;
    for (var i = 0; i < items.length; ++i) {
        var piece = items[i] + (i + 1 < items.length ? ", " : "");
        if (line.length + piece.length > 84 && line !== head) {
            out.push(line.replace(/ $/, ""));
            line = "    ";
        }
        line += piece;
    }
    out.push(line + tail);
    return out;
}

function indentLines(prefix, lines) {
    var out = [];
    for (var i = 0; i < lines.length; ++i)
        out.push(lines[i] ? (prefix + lines[i]) : "");
    return out;
}

// QML reads Qt's own #aarrggbb as it is.
function qmlColour(colour, fallback) {
    if (colour === undefined || colour === null || colour === "")
        return fallback;
    return String(colour);
}

function rgbOf(colour) {
    var text = String(colour);
    return text.length === 9 ? text.substring(3) : text.substring(1);
}

function opaqueColour(colour) { return "#" + rgbOf(colour); }

function withAlpha(colour, alpha) {
    var a = Math.max(0, Math.min(255, Math.round(alpha))).toString(16);
    return "#" + (a.length < 2 ? "0" + a : a) + rgbOf(colour);
}

// --- templates ---------------------------------------------------------------
//
// Every QML object this converter writes is a template under templates/: the
// files of the project at the top, one file per kind of object under objects/,
// and the components Scene.qml draws with under components/, copied as they
// are. render() fills one:
//
//  - a placeholder alone on its line takes a block -- any number of lines, each
//    indented as the placeholder is -- and an empty block leaves the line out;
//  - a placeholder inside a line takes a value, and a value of null leaves the
//    whole line out. That is how a template lists every property an object
//    can have while a scene sets only the ones it needs;
//  - a line starting //! is a note for whoever edits the template.
//
// A placeholder this file does not fill is an error naming the template, so a
// template edited out of step with the code says so rather than writing {{X}}.
function render(name, values) {
    var lines = T(name).replace(/\r/g, "").replace(/\n$/, "").split(NEWLINE);
    var out = [];
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        if (/^\s*\/\/!/.test(line))
            continue;
        var block = line.match(/^(\s*)\{\{([A-Z0-9_]+)\}\}\s*$/);
        if (block) {
            var body = valueFor(name, values, block[2]);
            if (body === null || body === "")
                continue;
            var parts = body.split(NEWLINE);
            for (var k = 0; k < parts.length; ++k)
                out.push(parts[k] ? block[1] + parts[k] : "");
            continue;
        }
        var lead = line.match(/^\s*/)[0];
        var dropped = false;
        var filled = line.replace(/\{\{([A-Z0-9_]+)\}\}/g, function (all, key) {
            var value = valueFor(name, values, key);
            if (value === null) {
                dropped = true;
                return "";
            }
            return value.split(NEWLINE).join(NEWLINE + lead);
        });
        if (!dropped)
            out = out.concat(filled.split(NEWLINE));
    }
    return tidy(out).join(NEWLINE);
}

// A whole file: a template rendered, ending in a newline as a file should.
function page(name, values) {
    return render(name, values) + NEWLINE;
}

function valueFor(name, values, key) {
    if (!has(values, key))
        throw new Error("templates/" + name + " asks for {{" + key + "}}, which export.js does not fill");
    var value = values[key];
    if (value === null || value === undefined)
        return null;
    return String(value);
}

// Blocks left empty leave their blank lines behind: no two in a row, and none
// just inside a brace or at either end.
function tidy(lines) {
    var out = [];
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        if (line.trim() === "") {
            var before = out.length ? out[out.length - 1].trim() : "";
            if (!out.length || before === "" || /[{\[]$/.test(before))
                continue;
            out.push("");
            continue;
        }
        if (/^[}\]]/.test(line.trim()) && out.length && out[out.length - 1] === "")
            out.pop();
        out.push(line);
    }
    while (out.length && out[out.length - 1] === "")
        out.pop();
    return out;
}

// A list written as a template value: as many items to a line as fit, the rest
// on lines of their own, one step in from where the list starts.
function listValue(items) {
    return wrapList("", items, "").join(NEWLINE);
}

function ordered(lower, upper) {
    return lower > upper ? { lower: upper, upper: lower } : { lower: lower, upper: upper };
}

function clampAngle(degrees) { return Math.max(-178.0, Math.min(178.0, degrees || 0)); }

function has(map, key) { return Object.prototype.hasOwnProperty.call(map, key); }
function pick(map, key, fallback) { return has(map, key) ? map[key] : fallback; }
function bool(v) { return v ? "true" : "false"; }
function differs(a, b) { return Math.abs(Number(a) - Number(b)) > 1e-6; }

function pickNumber(v, fallback) {
    var n = Number(v);
    return isFinite(n) && v !== null && v !== undefined && v !== "" ? n : fallback;
}

function int(v, fallback) {
    var n = Math.round(Number(v));
    return isFinite(n) ? String(n) : String(fallback);
}

function num(v) {
    var n = Number(v);
    if (!isFinite(n))
        n = 0;
    var text = n.toFixed(6).replace(/0+$/, "");
    if (text.charAt(text.length - 1) === ".")
        text += "0";
    return text === "-0.0" ? "0.0" : text;
}

function plain(v) { return num(v).replace(/\.0$/, ""); }

function short(v) {
    return num(Math.round(Number(v) * 1000) / 1000).replace(/\.0$/, "");
}

function safeName(name) {
    return name ? String(name).replace(/[^A-Za-z0-9_]/g, "_") : "";
}
