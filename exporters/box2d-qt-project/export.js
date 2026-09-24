// Physalis scene -> one plain Box2D program: main.cpp, and a CMakeLists.txt
// to build it.
//
// What comes out is meant to read like the Box2D manual. A world is created,
// then each body, its shapes and the joints, with their ids kept in variables
// named after the objects in the editor. step() calls b2World_Step and then
// does what the scene's rules say, written as ordinary if statements calling
// Box2D. Nothing of the editor itself is carried across: no scene description
// to load, no rules as data, nothing interpreting anything.
//
// main.cpp.tmpl is the program around the scene. Every piece of code that goes
// into it -- the world, each body, shape and joint, the helpers the rules call,
// the toolbar -- is a template under templates/objects/ (see render() below);
// this script works out the values that go into them, and leaves a def field
// out when it would only repeat what b2Default*Def already sets. The rules'
// own code is the one part still written here.
//
// `scene` is the saved document plus scene.simulation (bodies and joints as
// the engine receives them), scene.settings (the editor's preferences) and
// scene.converterSettings (this converter's own options, from the manifest).

var NEWLINE = String.fromCharCode(10);

var T = null;       // template loader
var PPM = 1000;     // scene units per metre
var MOTION = 0.05;  // 50 / PPM -- the scale the editor quotes world speeds at
var TOLERANCE = 0.1; // Box2D's length unit, from the world's Contact Margin, as the editor sets it

var NAMES = null;   // editor name -> C++ identifier
var TAKEN = null;   // identifiers already in use
var USED = null;    // shapes whose ids the step code needs kept
var STATE = null;   // globals the step code keeps between steps
var RESETS = null;  // what createWorld() sets those back to
var LOST = false;   // whether any rule destroys a body or joint
var COUNTERS = null;

// What the engine says an object has, under the names it publishes. The
// application keeps them in one bag per object rather than as fields of its
// own, because which ones exist is the engine's business -- and this converter
// writes Box2D, so it reads Box2D's names out of it.
function vals(owner) {
    return (owner && owner.physics) || {};
}

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

    var cache = {};
    T = function (name) {
        if (!(name in cache))
            cache[name] = io.read("templates/" + name);
        return cache[name];
    };

    var project = safeName(own.projectName) || "PhysalisScene";
    var prefix = String(own.qtPath || "").trim();
    io.write("CMakeLists.txt", page("CMakeLists.txt.tmpl", {
        PROJECT: project,
        QT_PREFIX: prefix
            ? ('set(CMAKE_PREFIX_PATH "' + prefix.split("\\").join("/") + '" ${CMAKE_PREFIX_PATH})')
            : "",
        CXX_STANDARD: String(own.cxxStandard || "17"),
        BOX2D_REPO: String(own.box2dRepository || "https://github.com/erincatto/box2d.git").trim(),
        BOX2D_REF: String(own.box2dRef || "v3.1.1").trim(),
    }));

    TAKEN = {};
    for (var r = 0; r < RESERVED.length; ++r)
        TAKEN[RESERVED[r]] = true;
    NAMES = nameObjects(scene);
    USED = {};
    STATE = [];
    RESETS = [];
    CHANGES = 0;
    CHANGE_READS = [];
    CHANGE_SAVES = [];
    declareVariables(scene);
    COUNTERS = { time: false, frame: false, contact: false };
    HITS = null;
    WORLD_SETTINGS = world;
    LOST = destroys(scene);

    HELPERS = { initState: false, preSolve: false, clones: {} };
    ANSWERING = false;
    // The step code first: it decides which shape ids have to be kept.
    var stepCode = stepBody(scene, io);
    var colours = bodyColours(physics);
    WATCHED = watchedShapes(scene);
    var helpers = helpersCode(scene, colours);
    var steps = int(own.stepsPerSecond, 60);
    var width = Math.round(field.width || 1000);
    var height = Math.round(field.height || 600);

    io.write("main.cpp", page("main.cpp.tmpl", {
        TITLE: project,
        PIXELS_PER_METER: num(PPM),
        COLOR_DYNAMIC: colours.hex.dynamic,
        COLOR_STATIC: colours.hex["static"],
        COLOR_KINEMATIC: colours.hex.kinematic,
        FILL_ALPHA: colours.fillAlpha,
        SENSOR_COLOR: colours.sensorColor,
        SENSOR_PATTERN: colours.sensorPattern,
        SENSOR_FILLED: colours.sensorFilled,
        IDS: idDeclarations(scene),
        STATE: STATE.join(NEWLINE),
        HELPERS: helpers,
        RESETS: RESETS.join(NEWLINE),
        WORLD: worldCode(world),
        BODIES: bodiesCode(scene, colours),
        FIELD_BOUNDS: fieldBoundsCode(world, field),
        JOINTS: jointsCode(scene),
        SUB_STEPS: int(vals(world).subStepCount, 4),
        STEP: stepCode,
        AXIS_LENGTH: fnum(pickNumber(physics.bodyAxisLength, 40) / PPM),
        // The axes the debug view draws take their colours from the editor's.
        AXIS_X_COLOR: rgb(physics.bodyAxisXColor) || "dc3232",
        AXIS_Y_COLOR: rgb(physics.bodyAxisYColor) || "28a03c",
        STEPS_PER_SECOND: String(steps),
        WIDTH: String(width),
        HEIGHT: String(height),
        VIEW_CENTER_X: "0.0",
        VIEW_CENTER_Y: "0.0",
        BACKGROUND: field.backgroundColor || "#ffffffff",
        DEBUG_VIEW: bool(own.debugView),
        DRAW_RAY: (scene.rays || []).length ? render("objects/draw-ray.cpp.tmpl", {}) : "",
        DEBUG_DRAWING: debugDrawing(scene),
        JOINT_COLOR: qtColour(physics.jointColor, "170, 232, 196, 106"),
        JOINT_ANCHOR_RADIUS: short(pickNumber(physics.jointAnchorRadius, 7)),
        CONTROLS: controlsCode(own),
    }));

    io.log("Exported " + scene.simulation.bodies.length + " bodies, "
           + scene.simulation.joints.length + " joints and "
           + (scene.rules || []).length + " rules into main.cpp.");
    return true;
}

// --- names -----------------------------------------------------------------

// Words the generated code already uses for something, and C++'s own.
var RESERVED = ("world dt a b i m rad step createWorld main view window app toolbar scroll "
    + "debug other sensor visitor contacts sensors moves bodyDef shapeDef jointDef chainDef "
    + "worldDef box circle polygon hull points segment material filter data explosion walls "
    + "wall elapsed stepCount colorOf penOf drawRay FILL_ALPHA PIXELS_PER_METER "
    + "COLOR_DYNAMIC COLOR_STATIC COLOR_KINEMATIC SceneView "
    + "alignas alignof and asm auto bool break case catch char class const constexpr "
    + "continue decltype default delete do double else enum explicit export extern false "
    + "float for friend goto if inline int long mutable namespace new noexcept not nullptr "
    + "operator or private protected public register return short signed sizeof static "
    + "struct switch template this throw true try typedef typeid typename union unsigned "
    + "using virtual void volatile while xor").split(" ");

function identifier(text, fallback) {
    var base = String(text || "").replace(/[^A-Za-z0-9_]/g, "_");
    if (!base)
        base = fallback;
    if (/^[0-9]/.test(base))
        base = fallback + "_" + base;
    var name = base;
    for (var n = 2; TAKEN[name]; ++n)
        name = base + "_" + n;
    TAKEN[name] = true;
    return name;
}

// Every body, joint, shape and ray gets its editor name as its variable.
function nameObjects(scene) {
    var names = {};
    var bodies = scene.simulation.bodies;
    for (var b = 0; b < bodies.length; ++b)
        names["body:" + b] = identifier(bodies[b].name, "body");
    for (var s = 0; s < bodies.length; ++s) {
        var parts = bodies[s].parts || [];
        for (var p = 0; p < parts.length; ++p) {
            if (parts[p].name)
                names["shape:" + parts[p].name] = identifier(parts[p].name, "shape");
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

function idDeclarations(scene) {
    var bodies = [], shapes = [], joints = [], rays = [];
    var sim = scene.simulation;
    for (var b = 0; b < sim.bodies.length; ++b) {
        bodies.push(NAMES["body:" + b]);
        var parts = sim.bodies[b].parts || [];
        for (var p = 0; p < parts.length; ++p) {
            if (parts[p].name && USED[parts[p].name] && !isOutline(parts[p]))
                shapes.push(NAMES["shape:" + parts[p].name]);
        }
    }
    for (var j = 0; j < sim.joints.length; ++j)
        joints.push(NAMES["joint:" + j]);
    for (var r = 0; r < (scene.rays || []).length; ++r)
        rays.push(NAMES["ray:" + r]);
    var list = function (names) { return names.length ? listValue(names) : null; };
    return render("objects/ids.cpp.tmpl", {
        BODIES: list(bodies), SHAPES: list(shapes), JOINTS: list(joints), RAYS: list(rays),
    });
}

// --- the world -------------------------------------------------------------

function worldCode(world) {
    var g = { x: pickNumber(vals(world).gravityX, 0),
              y: pickNumber(vals(world).gravityY, 9.81) };
    // b2SetLengthUnitsPerMeter scales Box2D's own defaults for these as well, so
    // a value is left out only when it matches the default after that call.
    var v = vals(world);
    // From the world's physics block, not from the world object itself: the
    // settings live in the block, so reading them off the object found nothing
    // and every one of these was written at its default whatever the scene said.
    var speed = function (key, fallback) {
        var value = pickNumber(v[key], fallback) * MOTION;
        return differs(value, fallback * TOLERANCE) ? fnum(value) : null;
    };
    return render("objects/world.cpp.tmpl", {
        PIXELS_PER_METER: num(PPM),
        LENGTH_UNITS: fnum(TOLERANCE),
        GRAVITY_X: fnum(g.x * MOTION),
        GRAVITY_Y: fnum(g.y * MOTION),
        MAXIMUM_LINEAR_SPEED: speed("maximumLinearSpeed", 400),
        MAX_CONTACT_PUSH_SPEED: speed("maxContactPushSpeed", 3),
        RESTITUTION_THRESHOLD: speed("restitutionThreshold", 1),
        HIT_EVENT_THRESHOLD: speed("hitEventThreshold", 1),
        CONTACT_HERTZ: differs(pickNumber(v.contactHertz, 30), 30) ? fnum(v.contactHertz) : null,
        CONTACT_DAMPING_RATIO: differs(pickNumber(v.contactDampingRatio, 10), 10) ? fnum(v.contactDampingRatio) : null,
        ENABLE_SLEEP: v.enableSleep === false ? "false" : null,
        ENABLE_CONTINUOUS: v.enableContinuous === false ? "false" : null,
        ENABLE_WARM_STARTING: v.enableWarmStarting === false ? "false" : null,
        ENABLE_SPECULATIVE: v.enableSpeculative === false ? "false" : null,
        PRE_SOLVE: HELPERS.preSolve ? "notePreSolve" : null,
    });
}

// --- bodies and shapes -----------------------------------------------------

// The shapes an event rule watches. The editor switches contact and hit events
// on for these when a run starts, whatever the shape says, so the export does
// the same -- without it Box2D never reports the contact the rule waits for.
var WATCHED = null;
var HELPERS = null;    // what the step code calls that has to be written out
var ANSWERING = false; // writing a "to be removed" answer, which removes for real

function watchedShapes(scene) {
    var names = {};
    var rules = scene.rules || [];
    for (var i = 0; i < rules.length; ++i) {
        if (rules[i].enabled === false)
            continue;
        var watched = conditionsOf(rules[i]);
        for (var w = 0; w < watched.length; ++w) {
            if (watched[w].event)
                names[watched[w].subject] = true;
        }
    }
    return names;
}

function bodiesCode(scene, colours) {
    WATCHED = watchedShapes(scene);
    var out = [];
    var bodies = scene.simulation.bodies;
    for (var i = 0; i < bodies.length; ++i)
        out.push(bodyCode(bodies[i], NAMES["body:" + i], colours));
    return out.join(NEWLINE + NEWLINE);
}

var BODY_TYPES = { dynamic: "b2_dynamicBody", kinematic: "b2_kinematicBody" };

function bodyCode(body, name, colours) {
    var v = vals(body);
    var p = body.position || { x: 0, y: 0 };
    var velocity = { x: pickNumber(v.velocityX, 0), y: pickNumber(v.velocityY, 0) };
    var moving = velocity.x || velocity.y;
    var counts = {};
    var shapes = [];
    var parts = body.parts || [];
    for (var i = 0; i < parts.length; ++i)
        shapes.push(shapeCode(parts[i], body, name, colours, i === 0, counts));
    return render("objects/body.cpp.tmpl", {
        ID: name,
        TYPE: BODY_TYPES[body.type] || null,
        X: p.x || p.y ? short(p.x) : null,
        Y: p.x || p.y ? short(p.y) : null,
        ANGLE: body.rotation ? short(body.rotation) : null,
        // Scene units a second, as the editor means it: divided by the scale,
        // the same as a position. It was once multiplied by the pace instead,
        // which is what gravity is quoted at -- so an exported scene started
        // its bodies fifty times faster than the app did.
        VELOCITY_X: moving ? fnum(velocity.x / PPM) : null,
        VELOCITY_Y: moving ? fnum(velocity.y / PPM) : null,
        ANGULAR_VELOCITY: v.angularVelocity ? short(v.angularVelocity) : null,
        LINEAR_DAMPING: v.linearDamping ? fnum(v.linearDamping) : null,
        ANGULAR_DAMPING: v.angularDamping ? fnum(v.angularDamping) : null,
        GRAVITY_SCALE: differs(pickNumber(v.gravityScale, 1), 1) ? fnum(v.gravityScale) : null,
        // b2DefaultBodyDef scales its own by the length unit, not by the pace.
        SLEEP_THRESHOLD: differs(pickNumber(v.sleepThreshold, 0.05) * MOTION, 0.05 * TOLERANCE)
            ? fnum(v.sleepThreshold * MOTION) : null,
        ENABLE_SLEEP: v.enableSleep === false ? "false" : null,
        AWAKE: v.isAwake === false ? "false" : null,
        FIXED_ROTATION: v.fixedRotation ? "true" : null,
        BULLET: v.isBullet ? "true" : null,
        ALLOW_FAST_ROTATION: v.allowFastRotation ? "true" : null,
        ENABLED: body.isEnabled === false ? "false" : null,
        SHAPES: shapes.join(NEWLINE + NEWLINE),
    });
}

function isOutline(part) { return part.kind === "polygon" && !isSolidPolygon(part) || part.kind === "chain"; }

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

// A variable name for a piece of geometry: `box`, then `box2` for the next one
// in the same body.
function geometryName(kind, counts) {
    counts[kind] = (counts[kind] || 0) + 1;
    return counts[kind] === 1 ? kind : (kind + counts[kind]);
}

function shapeCode(part, body, bodyName, colours, first, counts) {
    // An outline has no area and so no mass; Box2D would leave a dynamic body
    // made of one frozen in place, so the editor does not build it either.
    if (isOutline(part) && body.type === "dynamic")
        return render("objects/massless-outline.cpp.tmpl", { NAME: part.name || "an outline" });

    var v = vals(part);
    var category = bits64(v.categoryBits, "0x1ull");
    var mask = bits64(v.maskBits, ALL_BITS);
    var watched = WATCHED[part.name] || WATCHED[body.name];
    var def = render("objects/shape-def.cpp.tmpl", {
        DECLARE: first ? "b2ShapeDef " : "",
        DENSITY: differs(pickNumber(v.density, 1), 1) ? fnum(v.density) : null,
        FRICTION: differs(pickNumber(v.friction, 0.6), 0.6) ? fnum(v.friction) : null,
        RESTITUTION: v.restitution ? fnum(v.restitution) : null,
        ROLLING_RESISTANCE: v.rollingResistance ? fnum(v.rollingResistance) : null,
        // A speed, so it goes through the scale like every other one.
        TANGENT_SPEED: v.tangentSpeed ? fnum(v.tangentSpeed / PPM) : null,
        CUSTOM_COLOR: colours.byType[body.type]
            ? colours.byType[body.type] + (v.isSensor ? " | SENSOR" : "") : null,
        CATEGORY: category !== "0x1ull" ? category : null,
        MASK: mask !== ALL_BITS ? mask : null,
        GROUP: v.groupIndex ? String(v.groupIndex) : null,
        SENSOR: v.isSensor ? "true" : null,
        SENSOR_EVENTS: v.enableSensorEvents ? "true" : null,
        CONTACT_EVENTS: v.enableContactEvents || watched ? "true" : null,
        HIT_EVENTS: v.enableHitEvents || watched ? "true" : null,
        PRE_SOLVE_EVENTS: v.enablePreSolveEvents || (watched && HELPERS && HELPERS.preSolve) ? "true" : null,
    });

    var keep = part.name && USED[part.name] && !isOutline(part)
        ? (NAMES["shape:" + part.name] + " = ") : "";
    var c = part.center || { x: 0, y: 0 };

    if (part.kind === "box") {
        var hw = part.halfExtents.x, hh = part.halfExtents.y;
        var r = Math.max(0, Math.min(part.cornerRadius || 0, Math.min(hw, hh)));
        var box = geometryName("box", counts);
        var rot = part.rotation ? render("objects/rotation.cpp.tmpl", { ANGLE: short(part.rotation) })
                                : "b2Rot_identity";
        var centred = !c.x && !c.y && !part.rotation;
        var size = r > 0 ? { HALF_WIDTH: short(Math.max(hw - r, 0.01)), HALF_HEIGHT: short(Math.max(hh - r, 0.01)) }
                         : { HALF_WIDTH: short(hw), HALF_HEIGHT: short(hh) };
        size.X = short(c.x);
        size.Y = short(c.y);
        size.ROTATION = rot;
        size.RADIUS = short(r);
        var make = render("objects/" + (r > 0 ? (centred ? "make-rounded-box" : "make-offset-rounded-box")
                                              : (centred ? "make-box" : "make-offset-box")) + ".cpp.tmpl", size);
        return render("objects/box.cpp.tmpl", { DEF: def, NAME: box, MAKE: make, KEEP: keep, BODY: bodyName });
    }

    if (part.kind === "circle") {
        return render("objects/circle.cpp.tmpl", {
            DEF: def, NAME: geometryName("circle", counts), X: short(c.x), Y: short(c.y),
            RADIUS: short(part.radius), KEEP: keep, BODY: bodyName,
        });
    }

    var pts = part.points || [];
    var points = geometryName("points", counts);
    var list = [];
    for (var i = 0; i < pts.length; ++i)
        list.push("m(" + short(pts[i].x) + ", " + short(pts[i].y) + ")");
    var pointsCode = render("objects/points.cpp.tmpl", { NAME: points, POINTS: listValue(list) });

    if (isSolidPolygon(part)) {
        var hull = geometryName("hull", counts);
        return render("objects/polygon.cpp.tmpl", {
            DEF: def, POINTS: pointsCode, HULL: hull, POINTS_NAME: points, COUNT: String(pts.length),
            NAME: geometryName("polygon", counts), KEEP: keep, BODY: bodyName,
        });
    }

    var closed = part.kind === "polygon" || part.closed;
    if (part.kind === "chain" && part.smoothChain && pts.length >= 4) {
        return render("objects/chain.cpp.tmpl", {
            DEF: def, POINTS: pointsCode, POINTS_NAME: points, COUNT: String(pts.length),
            LOOP: closed ? "true" : null, BODY: bodyName,
        });
    }
    return render("objects/segments.cpp.tmpl", {
        DEF: def, POINTS: pointsCode, POINTS_NAME: points,
        COUNT: String(closed ? pts.length : pts.length - 1),
        NEXT: closed ? ("(i + 1) % " + pts.length) : "i + 1", BODY: bodyName,
    });
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

// Walls around the editor's field, when the scene asks for them.
function fieldBoundsCode(world, field) {
    if (!world.solidBounds)
        return "";
    var w = field.width || 1000, h = field.height || 1000, t = 40;
    return render("objects/walls.cpp.tmpl", {
        HALF_SPAN: short(w / 2 + t),
        HALF_THICKNESS: short(t / 2),
        HALF_HEIGHT: short(h / 2),
        TOP: short(-h / 2 - t / 2),
        BOTTOM: short(h / 2 + t / 2),
        LEFT: short(-w / 2 - t / 2),
        RIGHT: short(w / 2 + t / 2),
    });
}

// --- joints ----------------------------------------------------------------

function jointsCode(scene) {
    var out = [];
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j)
        out.push(jointCode(scene, joints[j], NAMES["joint:" + j], j));
    return out.join(NEWLINE + NEWLINE);
}

// Where each sliding joint's travel starts: the gap between its anchors along
// its axis, in scene units. Box2D measures a prismatic or wheel joint between
// its anchors, so one whose anchors start apart starts that far along; the
// editor counts travel from there, and its engine adds this to the limits.
function travelOrigin(joint) {
    if (!joint || (joint.type !== "prismatic" && joint.type !== "wheel"))
        return 0;
    var anchors = joint.anchors || [];
    var a = anchors[0] || { x: 0, y: 0 }, b = anchors[1] || a;
    var ax = joint.axis || { x: 1, y: 0 };
    var length = Math.sqrt(ax.x * ax.x + ax.y * ax.y) || 1;
    return ((b.x - a.x) * ax.x + (b.y - a.y) * ax.y) / length;
}

function jointCode(scene, joint, name, index) {
    var p = joint.params || {};
    var bodies = scene.simulation.bodies;
    if (joint.bodyA < 0 || joint.bodyB < 0) {
        return render("objects/unsupported-joint.cpp.tmpl", {
            NAME: name, WHY: "holds one body to a point in the world; not exported.",
        });
    }
    var anchors = joint.anchors || [];
    var a = anchors.length > 0 ? anchors[0] : { x: 0, y: 0 };
    var b = anchors.length > 1 ? anchors[1] : a;
    var type = joint.type;

    // A field is left out when it would only say what b2Default*JointDef does.
    var flag = function (value, fallback) { return !!value !== fallback ? bool(value) : null; };
    var number = function (value, fallback) { return differs(Number(value) || 0, fallback) ? fnum(value) : null; };
    var length = function (value) { return value ? short(value) : null; };
    var angle = function (value) { return value ? short(value) : null; };
    var hertz = pick(p, "constraintHertz", 60), damping = pick(p, "constraintDampingRatio", 2);
    var tuned = differs(hertz, 60) || differs(damping, 2);
    var values = {
        ID: name, BODY_A: NAMES["body:" + joint.bodyA], BODY_B: NAMES["body:" + joint.bodyB],
        ANCHOR_A_X: short(a.x), ANCHOR_A_Y: short(a.y), ANCHOR_B_X: short(b.x), ANCHOR_B_Y: short(b.y),
        COLLIDE_CONNECTED: flag(joint.collideConnected, false),
        CONSTRAINT_HERTZ: tuned ? fnum(hertz) : null,
        CONSTRAINT_DAMPING: tuned ? fnum(damping) : null,
    };
    // The angle the two bodies already stand at, so a joint made in place
    // starts unstrained.
    var resting = (bodies[joint.bodyB].rotation || 0) - (bodies[joint.bodyA].rotation || 0);
    var axis = function (fallbackX, fallbackY) {
        var ax = joint.axis || { x: 1, y: 0 };
        var len = Math.sqrt(ax.x * ax.x + ax.y * ax.y) || 1;
        // Into body A's frame: turned back by A's own rotation.
        var turn = -(bodies[joint.bodyA].rotation || 0) * Math.PI / 180;
        var x = (ax.x * Math.cos(turn) - ax.y * Math.sin(turn)) / len;
        var y = (ax.x * Math.sin(turn) + ax.y * Math.cos(turn)) / len;
        var set = differs(x, fallbackX) || differs(y, fallbackY);
        values.AXIS_X = set ? fnum(x) : null;
        values.AXIS_Y = set ? fnum(y) : null;
    };

    if (type === "revolute") {
        // Box2D asserts on lower > upper and on a limit past a half turn.
        var angles = ordered(clampAngle(pick(p, "lowerAngle", 0)), clampAngle(pick(p, "upperAngle", 0)));
        values.REFERENCE_ANGLE = angle(resting + pick(p, "referenceAngle", 0));
        values.TARGET_ANGLE = angle(pick(p, "targetAngle", 0));
        values.ENABLE_SPRING = flag(p.enableSpring, false);
        values.HERTZ = number(pick(p, "hertz", 0), 0);
        values.DAMPING_RATIO = number(pick(p, "dampingRatio", 0), 0);
        values.ENABLE_LIMIT = flag(p.enableLimit, false);
        values.LOWER_ANGLE = angle(angles.lower);
        values.UPPER_ANGLE = angle(angles.upper);
        values.ENABLE_MOTOR = flag(p.enableMotor, false);
        values.MAX_MOTOR_TORQUE = number(pick(p, "maxMotorTorque", 0), 0);
        values.MOTOR_SPEED = angle(pick(p, "motorSpeed", 0));
    } else if (type === "prismatic" || type === "wheel") {
        var wheel = type === "wheel";
        axis(wheel ? 0 : 1, wheel ? 1 : 0);
        var origin = travelOrigin(joint);
        var span = ordered(pick(p, "lowerTranslation", 0), pick(p, "upperTranslation", 0));
        if (!wheel) {
            values.REFERENCE_ANGLE = angle(resting + pick(p, "referenceAngle", 0));
            values.TARGET_TRANSLATION = length(origin + pick(p, "targetTranslation", 0));
        }
        values.ENABLE_SPRING = flag(pick(p, "enableSpring", wheel), wheel);
        values.HERTZ = number(pick(p, "hertz", wheel ? 1 : 0), wheel ? 1 : 0);
        values.DAMPING_RATIO = number(pick(p, "dampingRatio", wheel ? 0.7 : 0), wheel ? 0.7 : 0);
        values.ENABLE_LIMIT = flag(p.enableLimit, false);
        values.LOWER_TRANSLATION = length(origin + span.lower);
        values.UPPER_TRANSLATION = length(origin + span.upper);
        values.ENABLE_MOTOR = flag(p.enableMotor, false);
        if (wheel) {
            values.MAX_MOTOR_TORQUE = number(pick(p, "maxMotorTorque", 0), 0);
            values.MOTOR_SPEED = angle(pick(p, "motorSpeed", 0));
        } else {
            values.MAX_MOTOR_FORCE = number(pick(p, "maxMotorForce", 0), 0);
            values.MOTOR_SPEED = length(pick(p, "motorSpeed", 0));
        }
    } else if (type === "distance") {
        // Zero length means however far apart the anchors already are.
        var distance = pick(p, "length", 0);
        if (!(distance > 0))
            distance = Math.sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        values.LENGTH = short(Math.max(distance, 1));
        values.ENABLE_SPRING = flag(p.enableSpring, false);
        values.HERTZ = number(pick(p, "hertz", 0), 0);
        values.DAMPING_RATIO = number(pick(p, "dampingRatio", 0), 0);
        values.ENABLE_LIMIT = flag(p.enableLimit, false);
        values.MIN_LENGTH = length(pick(p, "minLength", 0));
        values.MAX_LENGTH = length(pick(p, "maxLength", 0));
        values.ENABLE_MOTOR = flag(p.enableMotor, false);
        values.MAX_MOTOR_FORCE = number(pick(p, "maxMotorForce", 0), 0);
        values.MOTOR_SPEED = length(pick(p, "motorSpeed", 0));
    } else if (type === "weld") {
        values.REFERENCE_ANGLE = angle(resting + pick(p, "referenceAngle", 0));
        values.LINEAR_HERTZ = number(pick(p, "linearHertz", 0), 0);
        values.ANGULAR_HERTZ = number(pick(p, "angularHertz", 0), 0);
        values.LINEAR_DAMPING_RATIO = number(pick(p, "linearDampingRatio", 0), 0);
        values.ANGULAR_DAMPING_RATIO = number(pick(p, "angularDampingRatio", 0), 0);
    } else if (type === "motor") {
        var ox = pick(p, "linearOffsetX", 0), oy = pick(p, "linearOffsetY", 0);
        values.OFFSET_X = ox || oy ? short(ox) : null;
        values.OFFSET_Y = ox || oy ? short(oy) : null;
        values.ANGULAR_OFFSET = angle(pick(p, "angularOffset", 0));
        values.MAX_FORCE = number(pick(p, "maxForce", 1), 1);
        values.MAX_TORQUE = number(pick(p, "maxTorque", 1), 1);
        values.CORRECTION_FACTOR = number(pick(p, "correctionFactor", 0.3), 0.3);
    } else if (type === "mouse") {
        var tx = pick(p, "targetX", 0), ty = pick(p, "targetY", 0);
        var target = (tx === 0 && ty === 0) ? a : { x: tx, y: ty };
        values.TARGET_X = short(target.x);
        values.TARGET_Y = short(target.y);
        values.HERTZ = number(pick(p, "hertz", 4), 4);
        values.DAMPING_RATIO = number(pick(p, "dampingRatio", 1), 1);
        values.MAX_FORCE = number(pick(p, "maxForce", 1), 1);
    } else if (type !== "filter") {
        return render("objects/unsupported-joint.cpp.tmpl", {
            NAME: name, WHY: "is a kind of joint this converter does not know; not exported.",
        });
    }
    return render("objects/" + type + ".cpp.tmpl", values);
}

// --- the step: the scene's rules as code -----------------------------------
//
// A rule is "when this, do that". An event -- two shapes starting to touch --
// is read straight from Box2D's event arrays after the step. A condition on a
// value -- an angle past a limit -- is checked every step, and acts on the
// step it first becomes true: a bool remembers whether it was true last time,
// because "while the angle is past -14 degrees, stop the motor" would stop it
// again every step and nothing else could ever start it.

function stepBody(scene, io) {
    var rules = scene.rules || [];
    var loops = { begin: [], end: [], hit: [], sensorBegin: [], sensorEnd: [], preSolve: [],
                  moved: [], asleep: [] };
    var checks = [];

    for (var i = 0; i < rules.length; ++i) {
        var rule = rules[i];
        var n = i + 1;
        var caption = "// " + (rule.name ? rule.name : describe(rule));
        if (rule.enabled === false) {
            checks.push([caption, "// (switched off in the editor)"]);
            continue;
        }
        var made = ruleCode(scene, rule, n, caption, loops);
        if (made.error) {
            io.log("Rule " + (rule.name || n) + " was not exported: " + made.error + ".");
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
    for (var r = 0; r < rays.length; ++r)
        out = out.concat(rayCast(rays[r], NAMES["ray:" + r]).split(NEWLINE));

    var contactLoops = [];
    contactLoops = contactLoops.concat(eventLoop("contacts.beginCount", "contacts.beginEvents",
                                                 "shapeIdA", "shapeIdB", "a", "b", loops.begin));
    contactLoops = contactLoops.concat(eventLoop("contacts.endCount", "contacts.endEvents",
                                                 "shapeIdA", "shapeIdB", "a", "b", loops.end));
    contactLoops = contactLoops.concat(eventLoop("contacts.hitCount", "contacts.hitEvents",
                                                 "shapeIdA", "shapeIdB", "a", "b",
                                                 hitRecords().concat(loops.hit)));
    if (contactLoops.length) {
        out.push("");
        out.push("b2ContactEvents contacts = b2World_GetContactEvents(world);");
        out = out.concat(contactLoops);
    }

    // Box2D's pre-solve callback noted each pair as the step went through it.
    var preSolveLoop = eventLoop("static_cast<int>(aboutToTouch.size())", "aboutToTouch", "first", "second",
                                 "a", "b", loops.preSolve);
    if (preSolveLoop.length) {
        out.push("");
        out = out.concat(preSolveLoop);
        out.push("aboutToTouch.clear();");
    }

    var sensorLoops = [];
    sensorLoops = sensorLoops.concat(eventLoop("sensors.beginCount", "sensors.beginEvents",
                                               "sensorShapeId", "visitorShapeId",
                                               "sensor", "visitor", loops.sensorBegin));
    sensorLoops = sensorLoops.concat(eventLoop("sensors.endCount", "sensors.endEvents",
                                               "sensorShapeId", "visitorShapeId",
                                               "sensor", "visitor", loops.sensorEnd));
    if (sensorLoops.length) {
        out.push("");
        out.push("b2SensorEvents sensors = b2World_GetSensorEvents(world);");
        out = out.concat(sensorLoops);
    }

    if (loops.moved.length || loops.asleep.length) {
        out.push("");
        var movedFlags = {};
        for (var k = 0; k < loops.moved.length; ++k) {
            if (!movedFlags[loops.moved[k].flag]) {
                movedFlags[loops.moved[k].flag] = true;
                out.push("bool " + loops.moved[k].flag + " = false;");
            }
        }
        out.push("b2BodyEvents moves = b2World_GetBodyEvents(world);");
        out.push("for (int i = 0; i < moves.moveCount; ++i) {");
        out.push("    b2BodyMoveEvent move = moves.moveEvents[i];");
        for (var mv = 0; mv < loops.moved.length; ++mv) {
            out.push("    if (B2_ID_EQUALS(move.bodyId, " + loops.moved[mv].body
                     + ") && !move.fellAsleep)");
            out.push("        " + loops.moved[mv].flag + " = true;");
        }
        for (var as = 0; as < loops.asleep.length; ++as) {
            var sleeper = loops.asleep[as];
            out.push("");
            out.push("    " + sleeper.caption);
            out.push("    if (B2_ID_EQUALS(move.bodyId, " + sleeper.body + ") && move.fellAsleep"
                     + sleeper.once.test + ") {");
            out = out.concat(indentLines("        ", sleeper.effect.concat(sleeper.once.set)));
            out.push("    }");
        }
        out.push("}");
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
    return wrapLong(out.join(NEWLINE), 88);
}

// Breaks a line longer than about 90 columns after its last && or || that
// fits, and indents what follows under it.
function wrapLong(text, width) {
    var lines = text.split(NEWLINE);
    var out = [];
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        var lead = line.match(/^ */)[0];
        var continuation = lead + "    ";
        while (line.length > width && line.replace(/^ */, "").indexOf("//") !== 0) {
            // At an || if there is one, so a bracketed pair stays together.
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
            line = continuation + line.substring(cut + 1);
        }
        out.push(line);
    }
    return out.join(NEWLINE);
}

// The editor's own line for a rule, for the comment above its code.
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
    // A rule called "float" or "step" cannot be a variable of that name.
    if (text && TAKEN[text])
        text += "Rule";
    return identifier(text, "rule" + n);
}

function ruleCode(scene, rule, n, caption, loops) {
    var base = ruleVariable(rule, n);
    var once = { test: "", set: [] };
    if (rule.once) {
        var done = base + "Done";
        TAKEN[done] = true;
        remember("bool", done, "false");
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
    var pair = { contactBegin: "begin", contactEnd: "end", contactHit: "hit",
                 sensorBegin: "sensorBegin", sensorEnd: "sensorEnd", preSolve: "preSolve" };

    // Carried out where a removal is written: see actionLines.
    if (event === "@aboutToBeRemoved")
        return {};

    if (pair[event]) {
        var subject = side(scene, primary.subject);
        if (!subject)
            return { error: "cannot tell which shape " + primary.subject + " is" };
        var partner = primary.when ? side(scene, String(primary.when)) : null;
        if (primary.when && !partner)
            return { error: "cannot tell which shape " + primary.when + " is" };
        var effect = guarded(allEffectLines(scene, rule, "other"), guard);
        if (!effect)
            return { error: "nothing in Box2D does " + describe(rule).split(", ")[1] };
        if (event === "preSolve")
            HELPERS.preSolve = true;
        loops[pair[event]].push({ caption: caption, subject: subject, partner: partner,
                                  sensor: event.indexOf("sensor") === 0, effect: effect,
                                  usesOther: ruleNeedsOther(rule), once: once });
        return {};
    }

    if (event === "bodyFellAsleep" || event === "bodyMoved") {
        var body = resolve(scene, primary.subject);
        if (body && body.kind === "shape")
            body = { kind: "body", handle: body.bodyHandle };
        if (!body || body.kind !== "body")
            return { error: primary.subject + " is not a body" };
        var acting = guarded(allEffectLines(scene, rule, null), guard);
        if (!acting)
            return { error: "nothing in Box2D does " + describe(rule).split(", ")[1] };
        if (event === "bodyFellAsleep") {
            loops.asleep.push({ caption: caption, body: body.handle, effect: acting, once: once });
            return {};
        }
        var flag = body.handle.replace(/[^A-Za-z0-9_]/g, "_") + "Moved";
        loops.moved.push({ body: body.handle, flag: flag });
        return { check: edgeBlock(caption, base, flag, acting, once, "false") };
    }

    var condition;
    var startsTrue = "false";
    var effectOther = null;
    if (event === "@runStarted") {
        // Once, after the first step.
        condition = "true";
    } else if (event === "rayDetects") {
        var ray = resolve(scene, primary.subject);
        if (!ray || ray.kind !== "ray")
            return { error: primary.subject + " is not a ray" };
        condition = ray.handle + ".hit";
        if (primary.when) {
            var seen = side(scene, String(primary.when));
            if (!seen)
                return { error: "cannot tell which shape " + primary.when + " is" };
            condition += " && " + seen.test(ray.handle + ".shapeId");
        }
        effectOther = ray.handle + ".shapeId";
    } else if (event === "limitLower" || event === "limitUpper" || event === "limitEither") {
        var joint = resolve(scene, primary.subject);
        var reader = joint && joint.kind === "joint" ? LIMITS[joint.type] : null;
        if (!reader)
            return { error: primary.subject + " has no limit Box2D reports" };
        var J = joint.handle;
        var value = reader.value + "(" + J + ")";
        var lower = value + " <= " + reader.prefix + "GetLowerLimit(" + J + ") + 0.005f";
        var upper = value + " >= " + reader.prefix + "GetUpperLimit(" + J + ") - 0.005f";
        var at = event === "limitLower" ? lower : event === "limitUpper" ? upper
               : ("(" + lower + " || " + upper + ")");
        condition = reader.prefix + "IsLimitEnabled(" + J + ") && " + at;
        // A joint a rule can break is gone once it has been.
        if (LOST)
            condition = "b2Joint_IsValid(" + J + ") && " + condition;
        // Starting against the stop is not arriving at it.
        startsTrue = "true";
    } else if (event) {
        return { error: "Box2D has no " + event + " event to read" };
    } else {
        condition = valueConditions(scene, shape.values, shape.join);
        if (!condition)
            return { error: "one of its readings cannot be read here" };
    }

    var actions = guarded(allEffectLines(scene, rule, effectOther), guard);
    if (!actions)
        return { error: "nothing in Box2D does " + describe(rule).split(", ")[1] };
    return { check: edgeBlock(caption, base, condition, actions, once, startsTrue) };
}

// bool now = ...; if (now && !before) { ... } before = now;
function edgeBlock(caption, base, condition, effect, once, startsTrue) {
    var before = base + "Before";
    TAKEN[before] = true;
    remember("bool", before, startsTrue);
    var lines = [caption, "bool " + base + " = " + condition + ";",
                 "if (" + base + " && !" + before + once.test + ") {"];
    lines = lines.concat(indentLines("    ", effect.concat(once.set)));
    lines.push("}");
    lines.push(before + " = " + base + ";");
    return lines;
}

function remember(type, name, initial) {
    STATE.push(render("objects/state.cpp.tmpl", { TYPE: type, NAME: name }));
    RESETS.push(render("objects/reset.cpp.tmpl", { NAME: name, VALUE: initial }));
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

// The same question of a whole rule: any one of its actions is enough.
function ruleNeedsOther(rule) {
    var actions = actionsOf(rule);
    for (var i = 0; i < actions.length; ++i) {
        if (needsOther(actions[i]))
            return true;
    }
    return false;
}

// for (...) { b2ShapeId a = ...; b2ShapeId b = ...; if (...) { ... } }
function eventLoop(count, array, firstField, secondField, first, second, watchers) {
    if (watchers.length === 0)
        return [];
    var lines = ["for (int i = 0; i < " + count + "; ++i) {",
                 "    b2ShapeId " + first + " = " + array + "[i]." + firstField + ";",
                 "    b2ShapeId " + second + " = " + array + "[i]." + secondField + ";"];
    if (LOST) {
        lines.push("    if (!b2Shape_IsValid(" + first + ") || !b2Shape_IsValid(" + second + "))");
        lines.push("        continue;");
    }
    for (var i = 0; i < watchers.length; ++i) {
        var w = watchers[i];
        if (w.raw) {
            lines = lines.concat(indentLines("    ", w.raw));
            continue;
        }
        lines.push("");
        lines.push("    " + w.caption);
        var test;
        var otherFrom;
        if (w.sensor && w.subject.isSensor) {
            // The sensor is always the first of the two.
            test = w.subject.test(first) + (w.partner ? (" && " + w.partner.test(second)) : "");
            otherFrom = second;
        } else {
            var forward = w.subject.test(first) + (w.partner ? (" && " + w.partner.test(second)) : "");
            var backward = w.subject.test(second) + (w.partner ? (" && " + w.partner.test(first)) : "");
            test = "(" + forward + ") || (" + backward + ")";
            if (!w.partner)
                test = w.subject.test(first) + " || " + w.subject.test(second);
            otherFrom = w.subject.test(first) + " ? " + second + " : " + first;
        }
        lines.push("    if (" + (w.once.test ? ("(" + test + ")" + w.once.test) : test) + ") {");
        var body = w.effect.concat(w.once.set);
        if (w.usesOther)
            body = ["b2ShapeId other = " + otherFrom + ";"].concat(body);
        lines = lines.concat(indentLines("        ", body));
        lines.push("    }");
    }
    lines.push("}");
    return lines;
}

// A test for "this shape id is the object the rule names": the shape itself,
// or -- for a body, or an outline that Box2D holds as many shapes -- any shape
// of that body.
function side(scene, name) {
    var place = resolve(scene, name);
    if (!place)
        return null;
    if (place.kind === "shape" && !place.outline) {
        return { isSensor: place.isSensor,
                 test: function (v) { return "B2_ID_EQUALS(" + v + ", " + place.handle + ")"; } };
    }
    if (place.kind === "shape" || place.kind === "body") {
        var body = place.kind === "shape" ? place.bodyHandle : place.handle;
        return { isSensor: !!place.isSensor,
                 test: function (v) { return "B2_ID_EQUALS(b2Shape_GetBody(" + v + "), " + body + ")"; } };
    }
    return null;
}

// What the last impact on a shape was like, kept for a rule that asks.
var HITS = null;

function hitRecords() {
    var lines = [];
    for (var i = 0; HITS && i < HITS.length; ++i) {
        lines.push("");
        lines = lines.concat(render("objects/hit-record.cpp.tmpl", { ID: HITS[i].handle }).split(NEWLINE));
    }
    return lines.length ? [{ raw: lines }] : [];
}

// A ray and where it points, in scene units.
function rayGeometry(ray) {
    var angle = (ray.angle || 0) * Math.PI / 180;
    return { X: short(ray.x), Y: short(ray.y),
             DX: short(Math.cos(angle) * (ray.length || 0)), DY: short(Math.sin(angle) * (ray.length || 0)) };
}

function rayCast(ray, name) {
    var values = rayGeometry(ray);
    values.ID = name;
    var mask = String(ray.maskBits || "ffffffffffffffff").toLowerCase();
    if (/^f+$/.test(mask) && mask.length >= 16)
        return render("objects/ray-cast.cpp.tmpl", values);
    values.MASK = "0x" + mask + "ull";
    return render("objects/ray-cast-filtered.cpp.tmpl", values);
}

// The editor's colour as QColor's arguments: red, green, blue, alpha.
function qtColour(colour, fallback) {
    var text = String(colour || "");
    if (/^#[0-9a-fA-F]{8}$/.test(text)) {
        var v = function (i) { return parseInt(text.substring(i, i + 2), 16); };
        return v(3) + ", " + v(5) + ", " + v(7) + ", " + v(1);
    }
    if (/^#[0-9a-fA-F]{6}$/.test(text)) {
        var w = function (i) { return parseInt(text.substring(i, i + 2), 16); };
        return w(1) + ", " + w(3) + ", " + w(5);
    }
    var parts = fallback.split(", ");
    return parts[1] + ", " + parts[2] + ", " + parts[3] + ", " + parts[0];
}

// What the debug view adds: joints, each body's axes, and the rays.
function debugDrawing(scene) {
    var joints = [], bodies = [], rays = [];
    for (var j = 0; j < scene.simulation.joints.length; ++j)
        joints.push(NAMES["joint:" + j]);
    for (var b = 0; b < scene.simulation.bodies.length; ++b)
        bodies.push(NAMES["body:" + b]);
    var sceneRays = scene.rays || [];
    for (var i = 0; i < sceneRays.length; ++i) {
        var values = rayGeometry(sceneRays[i]);
        values.ID = NAMES["ray:" + i];
        rays.push(render("objects/ray-draw.cpp.tmpl", values));
    }
    return render("objects/debug-drawing.cpp.tmpl", {
        JOINTS: joints.length ? render("objects/joint-drawing.cpp.tmpl", { JOINTS: listValue(joints) }) : "",
        AXES: bodies.length ? render("objects/axes-drawing.cpp.tmpl", { BODIES: listValue(bodies) }) : "",
        RAYS: rays.join(NEWLINE),
    });
}

// --- what a name refers to, and what can be read or written on it ----------

// --- the scene's own variables ---------------------------------------------
//
// A score, a count of lives, a flag saying which way a lift is going: values
// the scene carries that no engine has heard of. Each becomes a global the
// step code reads and writes, set back to what the scene declares whenever the
// world is built -- which is what the editor does when a run starts.
var VARIABLES = null;

function declareVariables(scene) {
    VARIABLES = {};
    var variables = scene.variables || [];
    for (var i = 0; i < variables.length; ++i) {
        var variable = variables[i];
        if (!variable.name)
            continue;
    var DECL = variable.type === "bool" ? "bool"
                 : variable.type === "int" ? "int" : "float";
    var start = variable.type === "bool" ? (variable.value ? "true" : "false")
                  : variable.type === "int" ? String(Math.round(Number(variable.value) || 0))
                  : fnum(Number(variable.value) || 0);
        var ident = identifier(variable.name, "variable");
        TAKEN[ident] = true;
        remember(DECL, ident, start);
        VARIABLES[variable.name] = { handle: ident, type: variable.type };
    }
}

// A variable as a property, which is all the rule machinery needs it to be:
// something to read and something to write. The unit is the plain one, since a
// variable is a number the scene made up rather than a length or an angle.
function variableProperty(key) {
    var variable = VARIABLES ? VARIABLES[key] : null;
    if (!variable)
        return null;
    var unit = variable.type === "bool" ? "bool" : variable.type === "int" ? "int" : "num";
    return prop(unit, variable.handle, function (value) {
        return [variable.handle + " = " + value + ";"];
    });
}

function resolve(scene, name) {
    if (!name)
        return null;
    if (name === "@world")
        return { kind: "world" };
    if (name === "@variables")
        return { kind: "variables" };

    var rays = scene.rays || [];
    for (var r = 0; r < rays.length; ++r) {
        if (rays[r].name === name)
            return { kind: "ray", handle: NAMES["ray:" + r], ray: rays[r] };
    }
    // What a rule sets off; the action finds where it is and what it carries.
    var explosions = scene.explosions || [];
    for (var x = 0; x < explosions.length; ++x) {
        if (explosions[x].name === name)
            return { kind: "explosion", explosion: explosions[x] };
    }
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j) {
        if (joints[j].name === name)
            return { kind: "joint", type: joints[j].type, handle: NAMES["joint:" + j],
                     origin: travelOrigin(joints[j]) };
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
            var outline = isOutline(parts[p]);
            if (!outline)
                USED[name] = true;
            return { kind: "shape", handle: NAMES["shape:" + name], outline: outline,
                     shapeKind: parts[p].kind, isSensor: !!parts[p].isSensor,
                     bodyHandle: NAMES["body:" + i], index: i };
        }
    }
    return null;
}

// Where "the shape that touched it" lands: the shape, or for a body's
// property the body it belongs to.
function targetOf(scene, rule, other) {
    if (!needsOther(rule))
        return resolve(scene, rule.target);
    if (!other)
        return null;
    if (rule.target === "@otherBody" || (rule.property && !SHAPE_KEYS[rule.property])
        || rule.action)
        return { kind: "body", handle: "b2Shape_GetBody(" + other + ")" };
    return { kind: "shape", handle: other, bodyHandle: "b2Shape_GetBody(" + other + ")" };
}

var SHAPE_KEYS = {
    density: 1, friction: 1, restitution: 1, rollingResistance: 1, tangentSpeed: 1,
    categoryBits: 1, maskBits: 1, groupIndex: 1, radius: 1, isSensor: 1, mass: 1,
    enableContactEvents: 1, enableHitEvents: 1, enableSensorEvents: 1,
    lastHitSpeed: 1, lastHitX: 1, lastHitY: 1, lastHitNormalX: 1, lastHitNormalY: 1,
};

// A property as Box2D has it: its unit, a getter expression, and a setter
// taking the value already in Box2D's units. Units:
//   len    metres, written m(scene units)
//   angle  radians, written rad(degrees)
//   scaled a world speed, quoted at 50 px/m
//   torque comes down by the scale twice: a force times a distance
//   none   as it is
function property(place, key) {
    if (place.kind === "world")  return worldProperty(key);
    if (place.kind === "variables") return variableProperty(key);
    if (place.kind === "body")   return bodyProperty(place.handle, key);
    if (place.kind === "shape")  return place.outline ? null : shapeProperty(place, key);
    if (place.kind === "joint")  return jointProperty(place, key);
    if (place.kind === "ray")    return rayProperty(place, key);
    return null;
}

function prop(unit, read, write) { return { unit: unit, read: read, write: write }; }

function worldProperty(key) {
    var W = "world";
    if (key === "time") {
        if (!COUNTERS.time) {
            COUNTERS.time = true;
            TAKEN.elapsed = true;
            remember("float", "elapsed", "0.0f");
        }
        return prop("seconds", "elapsed", null);
    }
    if (key === "frame") {
        if (!COUNTERS.frame) {
            COUNTERS.frame = true;
            remember("int", "stepCount", "0");
        }
        return prop("int", "stepCount", null);
    }
    if (key === "gravityX")
        return prop("scaled", "b2World_GetGravity(world).x", function (v) {
            return ["b2World_SetGravity(world, b2Vec2{ " + v + ", b2World_GetGravity(world).y });"];
        });
    if (key === "gravityY")
        return prop("scaled", "b2World_GetGravity(world).y", function (v) {
            return ["b2World_SetGravity(world, b2Vec2{ b2World_GetGravity(world).x, " + v + " });"];
        });
    var speeds = { restitutionThreshold: "RestitutionThreshold",
                   hitEventThreshold: "HitEventThreshold", maximumLinearSpeed: "MaximumLinearSpeed" };
    if (has(speeds, key))
        return prop("scaled", "b2World_Get" + speeds[key] + "(" + W + ")", function (v) {
            return ["b2World_Set" + speeds[key] + "(world, " + v + ");"];
        });
    var tuning = { contactHertz: ["contactHertz", "none"],
                   contactDampingRatio: ["contactDampingRatio", "none"],
                   maxContactPushSpeed: ["contactPushSpeed", "scaled"] };
    if (has(tuning, key)) {
        if (!COUNTERS.contact) {
            // Box2D has no getter for these, and setting one sets all three.
            COUNTERS.contact = true;
            remember("float", "contactHertz", fnum(pickNumber(WORLD_SETTINGS.contactHertz, 30)));
            remember("float", "contactDampingRatio",
                     fnum(pickNumber(WORLD_SETTINGS.contactDampingRatio, 10)));
            remember("float", "contactPushSpeed",
                     fnum(pickNumber(WORLD_SETTINGS.maxContactPushSpeed, 3) * MOTION));
        }
        var variable = tuning[key][0];
        return prop(tuning[key][1], variable, function (v) {
            return [variable + " = " + v + ";",
                    "b2World_SetContactTuning(world, contactHertz, contactDampingRatio, contactPushSpeed);"];
        });
    }
    if (key === "enableSleep")
        return prop("bool", "b2World_IsSleepingEnabled(world)", call1("b2World_EnableSleeping", "world"));
    if (key === "enableContinuous")
        return prop("bool", "b2World_IsContinuousEnabled(world)", call1("b2World_EnableContinuous", "world"));
    if (key === "enableWarmStarting")
        return prop("bool", "b2World_IsWarmStartingEnabled(world)", call1("b2World_EnableWarmStarting", "world"));
    if (key === "awakeBodyCount")
        return prop("int", "b2World_GetAwakeBodyCount(world)", null);
    return null;
}

var WORLD_SETTINGS = {};

function call1(fn, handle) {
    return function (v) { return [fn + "(" + handle + ", " + v + ");"]; };
}

function bodyProperty(B, key) {
    var awake = "b2Body_SetAwake(" + B + ", true);";
    switch (key) {
    case "positionX":
        return prop("len", "b2Body_GetPosition(" + B + ").x", function (v) {
            return ["b2Body_SetTransform(" + B + ", b2Vec2{ " + v + ", b2Body_GetPosition(" + B
                    + ").y }, b2Body_GetRotation(" + B + "));", awake];
        });
    case "positionY":
        return prop("len", "b2Body_GetPosition(" + B + ").y", function (v) {
            return ["b2Body_SetTransform(" + B + ", b2Vec2{ b2Body_GetPosition(" + B + ").x, " + v
                    + " }, b2Body_GetRotation(" + B + "));", awake];
        });
    case "angle":
        return prop("angle", "b2Rot_GetAngle(b2Body_GetRotation(" + B + "))", function (v) {
            return ["b2Body_SetTransform(" + B + ", b2Body_GetPosition(" + B + "), b2MakeRot(" + v
                    + "));", awake];
        });
    case "velocityX":
        return prop("len", "b2Body_GetLinearVelocity(" + B + ").x", function (v) {
            return ["b2Body_SetLinearVelocity(" + B + ", b2Vec2{ " + v
                    + ", b2Body_GetLinearVelocity(" + B + ").y });", awake];
        });
    case "velocityY":
        return prop("len", "b2Body_GetLinearVelocity(" + B + ").y", function (v) {
            return ["b2Body_SetLinearVelocity(" + B + ", b2Vec2{ b2Body_GetLinearVelocity(" + B
                    + ").x, " + v + " });", awake];
        });
    case "speed":
        return prop("len", "b2Length(b2Body_GetLinearVelocity(" + B + "))", null);
    case "angularVelocity":
        return prop("angle", "b2Body_GetAngularVelocity(" + B + ")", function (v) {
            return ["b2Body_SetAngularVelocity(" + B + ", " + v + ");", awake];
        });
    case "impulseX":
    case "impulseY":
        return prop("len", null, function (v) {
            return ["b2Body_ApplyLinearImpulseToCenter(" + B + ", b2Vec2{ "
                    + (key === "impulseX" ? (v + ", 0.0f") : ("0.0f, " + v)) + " }, true);"];
        });
    case "forceX":
    case "forceY":
        return prop("len", null, function (v) {
            return ["b2Body_ApplyForceToCenter(" + B + ", b2Vec2{ "
                    + (key === "forceX" ? (v + ", 0.0f") : ("0.0f, " + v)) + " }, true);"];
        });
    case "torque":
        return prop("torque", null, function (v) { return ["b2Body_ApplyTorque(" + B + ", " + v + ", true);"]; });
    case "angularImpulse":
        return prop("torque", null, function (v) {
            return ["b2Body_ApplyAngularImpulse(" + B + ", " + v + ", true);"];
        });
    case "targetX":
    case "targetY":
        return prop("len", null, function (v) {
            var p = key === "targetX" ? (v + ", b2Body_GetPosition(" + B + ").y")
                                      : ("b2Body_GetPosition(" + B + ").x, " + v);
            return ["b2Body_SetTargetTransform(" + B + ", b2Transform{ b2Vec2{ " + p
                    + " }, b2Body_GetRotation(" + B + ") }, dt);"];
        });
    case "targetAngle":
        return prop("angle", null, function (v) {
            return ["b2Body_SetTargetTransform(" + B + ", b2Transform{ b2Body_GetPosition(" + B
                    + "), b2MakeRot(" + v + ") }, dt);"];
        });
    case "gravityScale": return getSet("none", "b2Body_GetGravityScale", "b2Body_SetGravityScale", B);
    case "linearDamping": return getSet("none", "b2Body_GetLinearDamping", "b2Body_SetLinearDamping", B);
    case "angularDamping": return getSet("none", "b2Body_GetAngularDamping", "b2Body_SetAngularDamping", B);
    case "sleepThreshold": return getSet("scaled", "b2Body_GetSleepThreshold", "b2Body_SetSleepThreshold", B);
    case "mass":
    case "rotationalInertia":
        return prop("none", key === "mass" ? ("b2Body_GetMass(" + B + ")")
                                           : ("b2Body_GetRotationalInertia(" + B + ")"), function (v) {
            return ["{", "    b2MassData data = b2Body_GetMassData(" + B + ");",
                    "    data." + key + " = " + v + ";", "    b2Body_SetMassData(" + B + ", data);", "}"];
        });
    case "isAwake": return getSet("bool", "b2Body_IsAwake", "b2Body_SetAwake", B);
    case "fixedRotation": return getSet("bool", "b2Body_IsFixedRotation", "b2Body_SetFixedRotation", B);
    case "isBullet": return getSet("bool", "b2Body_IsBullet", "b2Body_SetBullet", B);
    case "enableSleep": return getSet("bool", "b2Body_IsSleepEnabled", "b2Body_EnableSleep", B);
    case "isEnabled":
        return prop("bool", "b2Body_IsEnabled(" + B + ")", function (v) {
            if (v === "true") return ["b2Body_Enable(" + B + ");"];
            if (v === "false") return ["b2Body_Disable(" + B + ");"];
            return ["if (" + v + ")", "    b2Body_Enable(" + B + ");", "else",
                    "    b2Body_Disable(" + B + ");"];
        });
    case "bodyType":
        return prop("int", "int(b2Body_GetType(" + B + "))", function (v) {
            var named = { "0": "b2_staticBody", "1": "b2_kinematicBody", "2": "b2_dynamicBody" };
            return ["b2Body_SetType(" + B + ", " + (named[v] || ("b2BodyType(" + v + ")")) + ");"];
        });
    case "centerOfMassX": return prop("len", "b2Body_GetWorldCenterOfMass(" + B + ").x", null);
    case "centerOfMassY": return prop("len", "b2Body_GetWorldCenterOfMass(" + B + ").y", null);
    }
    return null;
}

function getSet(unit, getter, setter, handle) {
    return prop(unit, getter + "(" + handle + ")", function (v) {
        return [setter + "(" + handle + ", " + v + ");"];
    });
}

function shapeProperty(place, key) {
    var S = place.handle;
    var awake = "b2Body_SetAwake(b2Shape_GetBody(" + S + "), true);";
    var material = function (field, unit) {
        return prop(unit, "b2Shape_GetSurfaceMaterial(" + S + ")." + field, function (v) {
            return [awake, "{", "    b2SurfaceMaterial material = b2Shape_GetSurfaceMaterial(" + S + ");",
                    "    material." + field + " = " + v + ";",
                    "    b2Shape_SetSurfaceMaterial(" + S + ", material);", "}"];
        });
    };
    var filter = function (field, unit) {
        return prop(unit, "b2Shape_GetFilter(" + S + ")." + field, function (v) {
            return [awake, "{", "    b2Filter filter = b2Shape_GetFilter(" + S + ");",
                    "    filter." + field + " = " + v + ";", "    b2Shape_SetFilter(" + S + ", filter);", "}"];
        });
    };
    switch (key) {
    case "density":
        return prop("none", "b2Shape_GetDensity(" + S + ")", function (v) {
            return ["b2Shape_SetDensity(" + S + ", " + v + ", true);"];
        });
    case "friction": return withWake(getSet("none", "b2Shape_GetFriction", "b2Shape_SetFriction", S), awake);
    case "restitution": return withWake(getSet("none", "b2Shape_GetRestitution", "b2Shape_SetRestitution", S), awake);
    case "rollingResistance": return material("rollingResistance", "none");
    case "tangentSpeed": return material("tangentSpeed", "len");
    case "categoryBits": return filter("categoryBits", "bits");
    case "maskBits": return filter("maskBits", "bits");
    case "groupIndex": return filter("groupIndex", "int");
    case "isSensor": return prop("bool", "b2Shape_IsSensor(" + S + ")", null);
    case "enableContactEvents":
        return getSet("bool", "b2Shape_AreContactEventsEnabled", "b2Shape_EnableContactEvents", S);
    case "enableHitEvents": return getSet("bool", "b2Shape_AreHitEventsEnabled", "b2Shape_EnableHitEvents", S);
    case "enableSensorEvents":
        return getSet("bool", "b2Shape_AreSensorEventsEnabled", "b2Shape_EnableSensorEvents", S);
    case "mass": return prop("none", "b2Shape_GetMassData(" + S + ").mass", null);
    case "radius":
        if (place.shapeKind !== "circle")
            return null;
        return prop("len", "b2Shape_GetCircle(" + S + ").radius", function (v) {
            return [awake, "{", "    b2Circle circle = b2Shape_GetCircle(" + S + ");",
                    "    circle.radius = " + v + ";", "    b2Shape_SetCircle(" + S + ", &circle);", "}"];
        });
    case "lastHitSpeed": return prop("len", hitVariable(place, "HitSpeed"), null);
    case "lastHitX": return prop("len", hitVariable(place, "HitPoint") + ".x", null);
    case "lastHitY": return prop("len", hitVariable(place, "HitPoint") + ".y", null);
    case "lastHitNormalX": return prop("none", hitVariable(place, "HitNormal") + ".x", null);
    case "lastHitNormalY": return prop("none", hitVariable(place, "HitNormal") + ".y", null);
    }
    return null;
}

function withWake(p, awake) {
    var write = p.write;
    p.write = function (v) { return [awake].concat(write(v)); };
    return p;
}

function hitVariable(place, what) {
    HITS = HITS || [];
    var known = false;
    for (var i = 0; i < HITS.length; ++i)
        known = known || HITS[i].handle === place.handle;
    if (!known) {
        HITS.push({ handle: place.handle });
        remember("float", place.handle + "HitSpeed", "0.0f");
        remember("b2Vec2", place.handle + "HitPoint", "b2Vec2_zero");
        remember("b2Vec2", place.handle + "HitNormal", "b2Vec2_zero");
    }
    return place.handle + what;
}

// What a limit is measured along, for the joints Box2D reports one on.
// Box2D cannot report a wheel joint's travel, so a wheel has none here.
var LIMITS = {
    revolute: { prefix: "b2RevoluteJoint_", value: "b2RevoluteJoint_GetAngle" },
    prismatic: { prefix: "b2PrismaticJoint_", value: "b2PrismaticJoint_GetTranslation" },
};

function jointProperty(place, key) {
    var J = place.handle;
    var wake = "b2Joint_WakeBodies(" + J + ");";
    var t = place.type;
    var P = "b2" + t.charAt(0).toUpperCase() + t.slice(1) + "Joint_";
    var simple = function (unit, get, set) {
        return prop(unit, get ? (P + get + "(" + J + ")") : null, set ? function (v) {
            return [wake, P + set + "(" + J + ", " + v + ");"];
        } : null);
    };

    switch (key) {
    case "constraintForce": return prop("none", "b2Length(b2Joint_GetConstraintForce(" + J + "))", null);
    case "constraintTorque": return prop("none", "b2Joint_GetConstraintTorque(" + J + ")", null);
    case "linearSeparation": return prop("len", "b2Joint_GetLinearSeparation(" + J + ")", null);
    case "angularSeparation": return prop("angle", "b2Joint_GetAngularSeparation(" + J + ")", null);
    case "collideConnected":
        return prop("bool", "b2Joint_GetCollideConnected(" + J + ")", function (v) {
            return ["b2Joint_SetCollideConnected(" + J + ", " + v + ");"];
        });
    case "constraintHertz":
    case "constraintDampingRatio":
        return prop("none", null, function (v) {
            var which = key === "constraintHertz" ? "hertz" : "dampingRatio";
            return [wake, "{", "    float hertz, dampingRatio;",
                    "    b2Joint_GetConstraintTuning(" + J + ", &hertz, &dampingRatio);",
                    "    " + which + " = " + v + ";",
                    "    b2Joint_SetConstraintTuning(" + J + ", hertz, dampingRatio);", "}"];
        });
    }

    var springs = t === "revolute" || t === "prismatic" || t === "wheel" || t === "distance";
    if (springs) {
        switch (key) {
        case "enableSpring": case "springEnabled": return simple("bool", "IsSpringEnabled", "EnableSpring");
        case "enableLimit": case "limitEnabled": return simple("bool", "IsLimitEnabled", "EnableLimit");
        case "enableMotor": case "motorEnabled": return simple("bool", "IsMotorEnabled", "EnableMotor");
        case "hertz": return simple("none", "GetSpringHertz", "SetSpringHertz");
        case "dampingRatio": return simple("none", "GetSpringDampingRatio", "SetSpringDampingRatio");
        }
    }

    // A limit is one call taking both ends, and Box2D asserts on lower > upper.
    var limit = function (unit, end, getLower, getUpper, set, clamp) {
        return prop(unit, P + (end === "lower" ? getLower : getUpper) + "(" + J + ")", function (v) {
            var value = clamp ? ("b2ClampFloat(" + v + ", -0.98f * B2_PI, 0.98f * B2_PI)") : v;
            var other = P + (end === "lower" ? getUpper : getLower) + "(" + J + ")";
            return [wake, P + set + "(" + J + ", b2MinFloat(" + value + ", " + other + "), b2MaxFloat("
                    + value + ", " + other + "));"];
        });
    };

    if (t === "revolute") {
        switch (key) {
        case "angle": return simple("angle", "GetAngle", null);
        case "motorSpeed": return simple("angle", "GetMotorSpeed", "SetMotorSpeed");
        case "motorTorque": return simple("none", "GetMotorTorque", null);
        case "maxMotorTorque": return simple("none", "GetMaxMotorTorque", "SetMaxMotorTorque");
        case "targetAngle": return simple("angle", null, "SetTargetAngle");
        case "lowerAngle": return limit("angle", "lower", "GetLowerLimit", "GetUpperLimit", "SetLimits", true);
        case "upperAngle": return limit("angle", "upper", "GetLowerLimit", "GetUpperLimit", "SetLimits", true);
        }
    }
    // Travel counted from where the joint starts, as the editor counts it.
    var origin = "m(" + short(place.origin || 0) + ")";
    var travel = function (end) {
        var getter = P + (end === "lower" ? "GetLowerLimit" : "GetUpperLimit") + "(" + J + ")";
        var other = P + (end === "lower" ? "GetUpperLimit" : "GetLowerLimit") + "(" + J + ")";
        return prop("len", "(" + getter + " - " + origin + ")", function (v) {
            var moved = "(" + v + ") + " + origin;
            return [wake, P + "SetLimits(" + J + ", b2MinFloat(" + moved + ", " + other + "), b2MaxFloat("
                    + moved + ", " + other + "));"];
        });
    };
    if (t === "prismatic") {
        switch (key) {
        case "translation": return prop("len", "(" + P + "GetTranslation(" + J + ") - " + origin + ")", null);
        case "speed": return simple("len", "GetSpeed", null);
        case "motorSpeed": return simple("len", "GetMotorSpeed", "SetMotorSpeed");
        case "motorForce": return simple("none", "GetMotorForce", null);
        case "maxMotorForce": return simple("none", "GetMaxMotorForce", "SetMaxMotorForce");
        case "targetTranslation":
            return prop("len", null, function (v) {
                return [wake, P + "SetTargetTranslation(" + J + ", (" + v + ") + " + origin + ");"];
            });
        case "lowerTranslation": return travel("lower");
        case "upperTranslation": return travel("upper");
        }
    } else if (t === "wheel") {
        switch (key) {
        case "motorSpeed": return simple("angle", "GetMotorSpeed", "SetMotorSpeed");
        case "motorTorque": return simple("none", "GetMotorTorque", null);
        case "maxMotorTorque": return simple("none", "GetMaxMotorTorque", "SetMaxMotorTorque");
        case "lowerTranslation": return travel("lower");
        case "upperTranslation": return travel("upper");
        }
    } else if (t === "distance") {
        switch (key) {
        case "length": return simple("len", "GetLength", "SetLength");
        case "currentLength": return simple("len", "GetCurrentLength", null);
        case "motorSpeed": return simple("len", "GetMotorSpeed", "SetMotorSpeed");
        case "motorForce": return simple("none", "GetMotorForce", null);
        case "maxMotorForce": return simple("none", "GetMaxMotorForce", "SetMaxMotorForce");
        case "minLength": return limit("len", "lower", "GetMinLength", "GetMaxLength", "SetLengthRange", false);
        case "maxLength": return limit("len", "upper", "GetMinLength", "GetMaxLength", "SetLengthRange", false);
        }
    } else if (t === "motor") {
        switch (key) {
        case "maxForce": return simple("none", "GetMaxForce", "SetMaxForce");
        case "maxTorque": return simple("none", "GetMaxTorque", "SetMaxTorque");
        case "correctionFactor": return simple("none", "GetCorrectionFactor", "SetCorrectionFactor");
        case "angularOffset": return simple("angle", "GetAngularOffset", "SetAngularOffset");
        case "linearOffsetX":
        case "linearOffsetY":
            return prop("len", P + "GetLinearOffset(" + J + ")." + (key === "linearOffsetX" ? "x" : "y"),
                        function (v) {
                var keep = P + "GetLinearOffset(" + J + ")." + (key === "linearOffsetX" ? "y" : "x");
                return [wake, P + "SetLinearOffset(" + J + ", b2Vec2{ "
                        + (key === "linearOffsetX" ? (v + ", " + keep) : (keep + ", " + v)) + " });"];
            });
        }
    } else if (t === "mouse") {
        switch (key) {
        case "hertz": return simple("none", "GetSpringHertz", "SetSpringHertz");
        case "dampingRatio": return simple("none", "GetSpringDampingRatio", "SetSpringDampingRatio");
        case "maxForce": return simple("none", "GetMaxForce", "SetMaxForce");
        case "targetX":
        case "targetY":
            return prop("len", P + "GetTarget(" + J + ")." + (key === "targetX" ? "x" : "y"), function (v) {
                var keep = P + "GetTarget(" + J + ")." + (key === "targetX" ? "y" : "x");
                return [wake, P + "SetTarget(" + J + ", b2Vec2{ "
                        + (key === "targetX" ? (v + ", " + keep) : (keep + ", " + v)) + " });"];
            });
        }
    } else if (t === "weld") {
        switch (key) {
        case "linearHertz": return simple("none", "GetLinearHertz", "SetLinearHertz");
        case "angularHertz": return simple("none", "GetAngularHertz", "SetAngularHertz");
        case "linearDampingRatio": return simple("none", "GetLinearDampingRatio", "SetLinearDampingRatio");
        case "angularDampingRatio": return simple("none", "GetAngularDampingRatio", "SetAngularDampingRatio");
        }
    }
    return null;
}

function rayProperty(place, key) {
    var R = place.handle;
    if (key === "hit") return prop("bool", R + ".hit", null);
    if (key === "distance")
        return prop("len", R + ".fraction * m(" + short(place.ray.length || 0) + ")", null);
    if (key === "hitX") return prop("len", R + ".point.x", null);
    if (key === "hitY") return prop("len", R + ".point.y", null);
    return null;
}

// --- values ----------------------------------------------------------------

// A number from the editor, in Box2D's units, as C++.
function literal(unit, value) {
    var x = Number(value) || 0;
    if (unit === "bool")   return value === true || value === 1 || value === "true" ? "true" : "false";
    if (unit === "len")    return x === 0 ? "0.0f" : ("m(" + short(x) + ")");
    if (unit === "angle")  return x === 0 ? "0.0f" : ("rad(" + short(x) + ")");
    if (unit === "scaled") return fnum(x * MOTION);
    if (unit === "torque")
        return x === 0 ? "0.0f" : ("m(m(" + short(x) + "))");
    if (unit === "int")    return String(Math.round(x));
    if (unit === "bits")   return bits64(value, "0x0ull");
    return fnum(x);
}

// An expression in the editor's units, turned into Box2D's.
function intoBox2D(unit, expr) {
    if (unit === "len")    return "m(" + expr + ")";
    if (unit === "angle")  return "rad(" + expr + ")";
    if (unit === "scaled") return "(" + expr + ") * " + fnum(MOTION);
    if (unit === "torque") return "m(m(" + expr + "))";
    return expr;
}

// And a Box2D reading back into the editor's.
function outOfBox2D(unit, expr) {
    if (unit === "len")    return "(" + expr + ") * PIXELS_PER_METER";
    if (unit === "angle")  return "(" + expr + ") * 180.0f / B2_PI";
    if (unit === "scaled") return "(" + expr + ") / " + fnum(MOTION);
    return expr;
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
        remember("bool", CHANGE_STARTED, "false");
        CHANGE_SAVES.push(CHANGE_STARTED + " = true;");
    }
    var n = ++CHANGES;
    var now = "nowEq" + n;
    var was = "wasEq" + n;
    remember("bool", was, "false");
    CHANGE_READS.push("bool " + now + " = " + eq + ";");
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

    // "the ray sees the wall" compares ids, not names.
    if (subject.kind === "ray" && rule.watch === "hitName") {
        var seen = side(scene, String(rule.when));
        if (!seen || (compare !== "=" && compare !== "!="))
            return null;
        var test = subject.handle + ".hit && " + seen.test(subject.handle + ".shapeId");
        return compare === "=" ? test : ("!(" + test + ")");
    }

    var p = property(subject, rule.watch);
    if (!p || !p.read)
        return null;
    var guard = live(subject);
    var read = p.read;
    var result;
    if (p.unit === "bool") {
        var wanted = literal("bool", rule.when) === "true";
        if (compare === "=")
            result = (wanted ? "" : "!") + read;
        else if (compare === "!=")
            result = (wanted ? "!" : "") + read;
        else
            return null;
    } else {
        var against = p.unit === "seconds" ? fnum(Number(rule.when) || 0) : literal(p.unit, rule.when);
        if (compare === "%") {
            // In the editor's units, as the rule was written: whole numbers,
            // a non-zero multiple.
            var step = Math.round(Number(rule.when) || 0);
            if (step === 0)
                return "false";
            var whole = "std::llround(" + outOfBox2D(p.unit, read) + ")";
            result = "(" + whole + " != 0 && " + whole + " % " + step + " == 0)";
        } else if (compare === "=")
            result = "std::fabs(" + read + " - " + against + ") < 0.0001f";
        else if (compare === "!=")
            result = "std::fabs(" + read + " - " + against + ") >= 0.0001f";
        else
            result = read + " " + compare + " " + against;
    }
    return guard ? (guard + " && " + result) : result;
}

// Only worth checking when something in the scene can destroy it.
function live(place) {
    if (!LOST || !place)
        return "";
    if (place.kind === "body") return "b2Body_IsValid(" + place.handle + ")";
    if (place.kind === "joint") return "b2Joint_IsValid(" + place.handle + ")";
    if (place.kind === "shape" && !place.outline) return "b2Shape_IsValid(" + place.handle + ")";
    return "";
}

// The statements a rule runs, or null when Box2D has nothing that does it.
function effectLines(scene, rule, other) {
    var target = targetOf(scene, rule, other);
    if (!target)
        return null;
    var lines = rule.action ? actionLines(scene, rule, target) : writeLines(scene, rule, target);
    if (!lines)
        return null;
    var guard = live(target);
    if (!guard || needsOther(rule))
        return lines;
    return ["if (" + guard + ") {"].concat(indentLines("    ", lines)).concat(["}"]);
}

function writeLines(scene, rule, target) {
    var p = property(target, rule.property);
    if (!p || !p.write)
        return null;
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
        // One object following another: what that one reads now, plus an offset.
        var source = resolve(scene, rule.sourceObject);
        var from = source ? property(source, rule.sourceProperty) : null;
        if (!from || !from.read || from.unit === "bool")
            return null;
        if (from.unit === p.unit) {
            value = from.read + (rule.sourceOffset ? (" + " + literal(p.unit, rule.sourceOffset)) : "");
        } else {
            value = intoBox2D(p.unit, outOfBox2D(from.unit, from.read)
                              + (rule.sourceOffset ? (" + " + short(rule.sourceOffset)) : ""));
        }
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

    // Ending or holding the run cannot happen inside the step; the view does it
    // once the step is over.
    if (rule.action === "@stopRun")
        return ["pendingRun = 1;"];
    if (rule.action === "@holdRun")
        return ["pendingRun = 2;"];
    if (rule.action === "@initState") {
        if (!isBody)
            return null;
        HELPERS.initState = true;
        return ["initState(" + body + ");"];
    }
    if (rule.action === "@clone") {
        // A copy of a body named in the rule; one met through "the other" is
        // not known until the run, and there is nothing to copy it from.
        if (!isBody || target.index === undefined)
            return null;
        HELPERS.clones[target.index] = true;
        return ["cloneOf_" + NAMES["body:" + target.index] + "(m("
                + short(pickNumber(params.x, 0)) + ", " + short(pickNumber(params.y, 0)) + "));"];
    }
    if (rule.action === "pushForceAt") {
        if (!isBody)
            return null;
        var force = "m(" + short(pickNumber(params.impulseX, 0)) + ", " + short(pickNumber(params.impulseY, 0)) + ")";
        var fx = pickNumber(params.offsetX, 0), fy = pickNumber(params.offsetY, 0);
        if (!fx && !fy)
            return ["b2Body_ApplyForceToCenter(" + body + ", " + force + ", true);"];
        return ["b2Body_ApplyForce(" + body + ", " + force + ", b2Add(b2Body_GetWorldCenterOfMass("
                + body + "), m(" + short(fx) + ", " + short(fy) + ")), true);"];
    }
    if (rule.action === "resetMass")
        return isBody ? ["b2Body_ApplyMassFromShapes(" + body + ");"] : null;

    if (rule.action === "explode") {
        var settings = {}, key;
        for (key in params) {
            if (has(params, key))
                settings[key] = params[key];
        }
        var at = null;
        var explosions = scene.explosions || [];
        for (var i = 0; i < explosions.length; ++i) {
            if (explosions[i].name !== rule.target)
                continue;
            at = "m(" + short(explosions[i].x) + ", " + short(explosions[i].y) + ")";
            var own = explosions[i].params || {};
            for (key in own) {
                if (has(own, key))
                    settings[key] = own[key];
            }
        }
        if (!at && (target.kind === "body" || target.kind === "shape"))
            at = "b2Body_GetPosition(" + body + ")";
        var radius = pickNumber(settings.radius, 0);
        if (!at || !(radius > 0))
            return null;
        var lines = ["{", "    b2ExplosionDef explosion = b2DefaultExplosionDef();",
                     "    explosion.position = " + at + ";",
                     "    explosion.radius = m(" + short(radius) + ");"];
        if (pickNumber(settings.falloff, 0))
            lines.push("    explosion.falloff = m(" + short(settings.falloff) + ");");
        if (pickNumber(settings.impulse, 0))
            lines.push("    explosion.impulsePerLength = m(" + short(settings.impulse) + ");");
        if (pickNumber(settings.maskBits, 0) > 0)
            lines.push("    explosion.maskBits = " + bits64(settings.maskBits, ALL_BITS) + ";");
        lines.push("    b2World_Explode(world, &explosion);");
        lines.push("}");
        return lines;
    }
    if (rule.action === "pushAt") {
        if (target.kind !== "body" && target.kind !== "shape")
            return null;
        var impulse = "m(" + short(pickNumber(params.impulseX, 0)) + ", "
                      + short(pickNumber(params.impulseY, 0)) + ")";
        var ox = pickNumber(params.offsetX, 0), oy = pickNumber(params.offsetY, 0);
        if (!ox && !oy)
            return ["b2Body_ApplyLinearImpulseToCenter(" + body + ", " + impulse + ", true);"];
        return ["b2Body_ApplyLinearImpulse(" + body + ", " + impulse + ", b2Add(b2Body_GetWorldCenterOfMass("
                + body + "), m(" + short(ox) + ", " + short(oy) + ")), true);"];
    }
    if (rule.action === "removeBody") {
        if (target.kind !== "body" && target.kind !== "shape")
            return null;
        // Box2D takes the body's shapes and joints with it -- unless the body
        // answers "to be removed", in which case the answer is carried out.
        return removal(scene, rule, target, body, function (doomed) {
            return ["b2DestroyBody(" + doomed + ");"];
        });
    }
    if (rule.action === "breakJoint") {
        if (target.kind !== "joint")
            return null;
        return ["b2Joint_WakeBodies(" + target.handle + ");", "b2DestroyJoint(" + target.handle + ");"];
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
    var other = remover && remover.kind === "shape" && !remover.outline ? remover.handle : null;
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
    if (target.index !== undefined)
        return answers[target.index] ? answer(answers[target.index]) : remove(body);
    var keys = Object.keys(answers);
    if (!keys.length)
        return remove(body);
    // Met through "the other": which body it is is only known during the run.
    var lines = ["b2BodyId doomed = " + body + ";"];
    for (var k = 0; k < keys.length; ++k) {
        lines.push((k ? "} else if (" : "if (") + "B2_ID_EQUALS(doomed, " + NAMES["body:" + keys[k]] + ")) {");
        lines = lines.concat(indentLines("    ", answer(answers[keys[k]])));
    }
    lines.push("} else {");
    lines = lines.concat(indentLines("    ", remove("doomed")));
    lines.push("}");
    return lines;
}

// --- helpers the step code calls -------------------------------------------

function helpersCode(scene, colours) {
    var out = [];
    var bodies = scene.simulation.bodies;
    if (HELPERS.preSolve) {
        RESETS.push(render("objects/pre-solve-reset.cpp.tmpl", {}));
        out.push(render("objects/pre-solve.cpp.tmpl", {}));
    }
    if (HELPERS.initState) {
        var starts = [];
        for (var b = 0; b < bodies.length; ++b) {
            var p = bodies[b].position || { x: 0, y: 0 };
            starts.push("{ " + NAMES["body:" + b] + ", " + fnum(p.x) + ", " + fnum(p.y) + ", "
                        + fnum(bodies[b].rotation || 0) + " }");
        }
        out.push(render("objects/init-state.cpp.tmpl", { STARTS: listValue(starts) }));
    }
    for (var index in HELPERS.clones) {
        if (!has(HELPERS.clones, index))
            continue;
        // Built exactly as the original is, keeping no shape ids of its own.
        var kept = USED;
        USED = {};
        var made = bodyCode(bodies[index], "clone", colours);
        USED = kept;
        out.push(render("objects/clone.cpp.tmpl", { NAME: bodies[index].name, ID: NAMES["body:" + index], BODY: made }));
    }
    return out.join(NEWLINE + NEWLINE);
}

// A 64-bit collision filter as an exact C++ literal. A scene keeps these as hex
// strings, "0xffffffffffffffff", because a JavaScript number cannot hold 64 bits:
// turned into one, all-ones came out as 18446744073709552000, which C++ wraps
// round to 384 -- and every shape then collided with nothing.
var ALL_BITS = "0xffffffffffffffffull";

function bits64(value, fallback) {
    if (value === undefined || value === null || value === "")
        return fallback;
    var text = String(value).trim().toLowerCase();
    if (/^0x[0-9a-f]+$/.test(text)) {
        var digits = text.slice(2).replace(/^0+(?=.)/, "");
        if (digits.length > 16)
            return ALL_BITS;
        return (/^f{16}$/.test(digits) ? "0xffffffffffffffff" : "0x" + digits) + "ull";
    }
    var n = Number(value);
    if (!isFinite(n) || n < 0)
        return fallback;
    // Past 2^53 a number is no longer exact; all-ones is what that almost
    // always meant.
    if (n >= 18446744073709549568)
        return ALL_BITS;
    if (n > 9007199254740991)
        return "0x" + n.toString(16) + "ull";
    return "0x" + Math.round(n).toString(16) + "ull";
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

// --- the window around it --------------------------------------------------

function controlsCode(own) {
    if (!own.addControls && !own.debugView)
        return "";
    return render("objects/toolbar.cpp.tmpl", {
        BUTTONS: own.addControls ? render("objects/buttons.cpp.tmpl", {}) : "",
        DEBUG_SWITCH: own.debugView ? render("objects/debug-switch.cpp.tmpl", {}) : "",
    });
}

// The editor paints a body by what it is -- dynamic, static, kinematic -- and
// Box2D lets a shape carry its own debug-draw colour, so each shape is given
// its body's. The editor's own colours stand in for an export made without a
// settings file.
function bodyColours(physics) {
    return {
        hex: {
            dynamic: rgb(physics.bodyDynamicColor) || "2e86c1",
            "static": rgb(physics.bodyStaticColor) || "279e6a",
            kinematic: rgb(physics.bodyKinematicColor) || "884ea0",
        },
        byType: { dynamic: "COLOR_DYNAMIC", "static": "COLOR_STATIC", kinematic: "COLOR_KINEMATIC" },
        fillAlpha: String(Math.max(0, Math.min(255, Math.round(pickNumber(physics.fillAlpha, 90))))),
        sensorColor: qtColour(physics.sensorColor, "255, 5, 201, 54"),
        sensorPattern: BRUSH_STYLES[sensorPattern(physics.sensorPattern)],
        sensorFilled: bool(settingTrue(physics.sensorFillsBody)),
    };
}

// "#aarrggbb" or "#rrggbb" to "rrggbb".
function rgb(colour) {
    var text = String(colour || "");
    if (/^#[0-9a-fA-F]{8}$/.test(text)) return text.substring(3);
    if (/^#[0-9a-fA-F]{6}$/.test(text)) return text.substring(1);
    return "";
}

// --- odds and ends ---------------------------------------------------------

function indentLines(prefix, lines) {
    var out = [];
    for (var i = 0; i < lines.length; ++i)
        out.push(lines[i] ? (prefix + lines[i]) : "");
    return out;
}

// --- templates ---------------------------------------------------------------
//
// Every piece of code this converter writes is a template under templates/:
// the project's files, and one file per kind of object under objects/.
// render() fills one:
//
//  - a placeholder alone on its line takes a block -- any number of lines, each
//    indented as the placeholder is -- and an empty block leaves the line out;
//  - a placeholder inside a line takes a value, and a value of null leaves the
//    whole line out. That is how a template lists every field an object can
//    have while a scene sets only the ones it needs;
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

// Box2D asserts on a revolute limit past just inside a half turn.
function clampAngle(degrees) { return Math.max(-176.0, Math.min(176.0, degrees || 0)); }

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

// A plain decimal, never exponential.
function num(v) {
    var n = Number(v);
    if (!isFinite(n))
        n = 0;
    var text = n.toFixed(6).replace(/0+$/, "");
    if (text.charAt(text.length - 1) === ".")
        text += "0";
    return text === "-0.0" ? "0.0" : text;
}

// A float literal.
function fnum(v) { return num(v) + "f"; }

// A number the way a person types it into m(...) or rad(...): 240, not 240.0.
function short(v) {
    var text = num(Math.round(Number(v) * 1000) / 1000);
    return text.replace(/\.0$/, "");
}

function safeName(name) {
    return name ? String(name).replace(/[^A-Za-z0-9_]/g, "_") : "";
}

// Qt's pattern brushes, by the number the settings store them as.
var BRUSH_STYLES = {
    9: "HorPattern", 10: "VerPattern", 11: "CrossPattern",
    12: "BDiagPattern", 13: "FDiagPattern", 14: "DiagCrossPattern",
};

function sensorPattern(value) {
    var n = Math.round(Number(value));
    return BRUSH_STYLES[n] ? n : 14;
}

function settingTrue(value) { return value === true || value === "true" || value === 1; }
