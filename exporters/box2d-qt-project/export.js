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
// main.cpp.tmpl is the program around the scene. This script works out the
// parts that depend on the scene, and writes a def field only when it differs
// from what b2Default*Def already sets.
//
// `scene` is the saved document plus scene.simulation (bodies and joints as
// the engine receives them), scene.settings (the editor's preferences) and
// scene.converterSettings (this converter's own options, from the manifest).

var NEWLINE = String.fromCharCode(10);

var T = null;       // template loader
var PPM = 1000;     // scene units per metre
var MOTION = 0.05;  // 50 / PPM -- the scale the editor quotes world speeds at

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

    var cache = {};
    T = function (name) {
        if (!(name in cache))
            cache[name] = io.read("templates/" + name);
        return cache[name];
    };

    var project = safeName(own.projectName) || "PhysalisScene";
    var prefix = String(own.qtPath || "").trim();
    io.write("CMakeLists.txt", fill(T("CMakeLists.txt.tmpl"), {
        PROJECT: project,
        QT_PREFIX: prefix
            ? ('set(CMAKE_PREFIX_PATH "' + prefix.split("\\").join("/")
               + '" ${CMAKE_PREFIX_PATH})' + NEWLINE)
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
    COUNTERS = { time: false, frame: false, contact: false };
    HITS = null;
    WORLD_SETTINGS = world;
    LOST = destroys(scene);

    // The step code first: it decides which shape ids have to be kept.
    var stepCode = stepBody(scene, io);
    var colours = bodyColours(physics);
    var steps = int(own.stepsPerSecond, 60);
    var width = Math.round(field.width || 1000);
    var height = Math.round(field.height || 600);

    io.write("main.cpp", fill(T("main.cpp.tmpl"), {
        TITLE: project,
        PIXELS_PER_METER: num(PPM),
        COLOURS: colours.constants,
        IDS: idDeclarations(scene),
        STATE: STATE.length ? (NEWLINE + STATE.join("")) : "",
        RESETS: RESETS.length ? (RESETS.join("") + NEWLINE) : "",
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
        RAY_HELPER: (scene.rays || []).length ? T("draw-ray.tmpl") : "",
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

    return declare("b2BodyId", bodies) + declare("b2ShapeId", shapes)
           + declare("b2JointId", joints) + declare("b2RayResult", rays);
}

// One declaration for a list of names, wrapped the way a person would.
function declare(type, names) {
    if (names.length === 0)
        return "";
    var out = type + " ";
    var line = out;
    for (var i = 0; i < names.length; ++i) {
        var piece = names[i] + (i + 1 < names.length ? ", " : ";");
        if (line.length + piece.length > 88) {
            out = out.replace(/ $/, "") + NEWLINE + "    ";
            line = "    ";
        }
        out += piece;
        line += piece;
    }
    return out + NEWLINE;
}

// --- the world -------------------------------------------------------------

function worldCode(world) {
    var g = { x: pickNumber(vals(world).gravityX, 0),
              y: pickNumber(vals(world).gravityY, 9.81) };
    var lines = [
        "// Box2D's tolerances are lengths fixed for metre-sized objects: 5 mm of slop,",
        "// contacts made 2 cm before shapes meet. This scene is drawn at " + num(PPM) + " units per",
        "// metre, so they are scaled to what they would be at 50, or they show on screen.",
        "b2SetLengthUnitsPerMeter(" + fnum(MOTION) + ");",
        "",
        "b2WorldDef worldDef = b2DefaultWorldDef();",
        "worldDef.gravity = b2Vec2{ " + fnum(g.x * MOTION) + ", " + fnum(g.y * MOTION) + " };",
    ];
    // b2SetLengthUnitsPerMeter scales Box2D's own defaults for these as well, so
    // a value is left out only when it matches the default after that call.
    var speeds = [
        ["maximumLinearSpeed", 400, 400], ["maxContactPushSpeed", 3, 3],
        ["restitutionThreshold", 1, 1], ["hitEventThreshold", 1, 1],
    ];
    for (var i = 0; i < speeds.length; ++i) {
        var value = pickNumber(world[speeds[i][0]], speeds[i][1]) * MOTION;
        if (differs(value, speeds[i][2] * MOTION))
            lines.push("worldDef." + speeds[i][0] + " = " + fnum(value) + ";");
    }
    if (differs(pickNumber(vals(world).contactHertz, 30), 30))
        lines.push("worldDef.contactHertz = " + fnum(vals(world).contactHertz) + ";");
    if (differs(pickNumber(vals(world).contactDampingRatio, 10), 10))
        lines.push("worldDef.contactDampingRatio = " + fnum(vals(world).contactDampingRatio) + ";");
    if (vals(world).enableSleep === false)
        lines.push("worldDef.enableSleep = false;");
    if (vals(world).enableContinuous === false)
        lines.push("worldDef.enableContinuous = false;");
    lines.push("world = b2CreateWorld(&worldDef);");
    return indent("    ", lines);
}

// --- bodies and shapes -----------------------------------------------------

// The shapes an event rule watches. The editor switches contact and hit events
// on for these when a run starts, whatever the shape says, so the export does
// the same -- without it Box2D never reports the contact the rule waits for.
var WATCHED = null;

function watchedShapes(scene) {
    var names = {};
    var rules = scene.rules || [];
    for (var i = 0; i < rules.length; ++i) {
        if (rules[i].event && rules[i].enabled !== false)
            names[rules[i].subject] = true;
    }
    return names;
}

function bodiesCode(scene, colours) {
    WATCHED = watchedShapes(scene);
    var out = "";
    var bodies = scene.simulation.bodies;
    for (var i = 0; i < bodies.length; ++i)
        out += NEWLINE + bodyCode(bodies[i], NAMES["body:" + i], colours);
    return out;
}

function bodyCode(body, name, colours) {
    var lines = ["b2BodyDef bodyDef = b2DefaultBodyDef();"];
    if (body.type === "dynamic")
        lines.push("bodyDef.type = b2_dynamicBody;");
    else if (body.type === "kinematic")
        lines.push("bodyDef.type = b2_kinematicBody;");

    var p = body.position || { x: 0, y: 0 };
    if (p.x || p.y)
        lines.push("bodyDef.position = m(" + short(p.x) + ", " + short(p.y) + ");");
    if (body.rotation)
        lines.push("bodyDef.rotation = b2MakeRot(rad(" + short(body.rotation) + "));");
    var v = { x: pickNumber(vals(body).velocityX, 0),
              y: pickNumber(vals(body).velocityY, 0) };
    if (v.x || v.y) {
        lines.push("bodyDef.linearVelocity = b2Vec2{ " + fnum(v.x * MOTION) + ", "
                   + fnum(v.y * MOTION) + " };");
    }
    if (vals(body).angularVelocity)
        lines.push("bodyDef.angularVelocity = rad(" + short(vals(body).angularVelocity) + ");");
    if (vals(body).linearDamping)
        lines.push("bodyDef.linearDamping = " + fnum(vals(body).linearDamping) + ";");
    if (vals(body).angularDamping)
        lines.push("bodyDef.angularDamping = " + fnum(vals(body).angularDamping) + ";");
    if (differs(pickNumber(vals(body).gravityScale, 1), 1))
        lines.push("bodyDef.gravityScale = " + fnum(vals(body).gravityScale) + ";");
    if (differs(pickNumber(vals(body).sleepThreshold, 0.05) * MOTION, 0.05 * MOTION))
        lines.push("bodyDef.sleepThreshold = " + fnum(vals(body).sleepThreshold * MOTION) + ";");
    if (vals(body).enableSleep === false)
        lines.push("bodyDef.enableSleep = false;");
    if (vals(body).isAwake === false)
        lines.push("bodyDef.isAwake = false;");
    if (vals(body).fixedRotation)
        lines.push("bodyDef.fixedRotation = true;");
    if (vals(body).isBullet)
        lines.push("bodyDef.isBullet = true;");
    if (vals(body).allowFastRotation)
        lines.push("bodyDef.allowFastRotation = true;");
    if (body.isEnabled === false)
        lines.push("bodyDef.isEnabled = false;");
    lines.push(name + " = b2CreateBody(world, &bodyDef);");

    var counts = {};
    var parts = body.parts || [];
    for (var i = 0; i < parts.length; ++i) {
        lines.push("");
        lines = lines.concat(shapeLines(parts[i], body, name, colours, i === 0, counts));
    }
    return "    {" + NEWLINE + indent("        ", lines) + "    }" + NEWLINE;
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

function shapeLines(part, body, bodyName, colours, first, counts) {
    // An outline has no area and so no mass; Box2D would leave a dynamic body
    // made of one frozen in place, so the editor does not build it either.
    if (isOutline(part) && body.type === "dynamic") {
        return ["// " + (part.name || "an outline") + " is an outline, which has no mass, "
                + "so a dynamic body cannot be made of it."];
    }

    var lines = [first ? "b2ShapeDef shapeDef = b2DefaultShapeDef();"
                       : "shapeDef = b2DefaultShapeDef();"];
    if (differs(pickNumber(vals(part).density, 1), 1))
        lines.push("shapeDef.density = " + fnum(vals(part).density) + ";");
    if (differs(pickNumber(vals(part).friction, 0.6), 0.6))
        lines.push("shapeDef.material.friction = " + fnum(vals(part).friction) + ";");
    if (vals(part).restitution)
        lines.push("shapeDef.material.restitution = " + fnum(vals(part).restitution) + ";");
    if (vals(part).rollingResistance)
        lines.push("shapeDef.material.rollingResistance = " + fnum(vals(part).rollingResistance) + ";");
    if (vals(part).tangentSpeed)
        lines.push("shapeDef.material.tangentSpeed = m(" + short(vals(part).tangentSpeed) + ");");
    if (colours.byType[body.type]) {
        lines.push("shapeDef.material.customColor = " + colours.byType[body.type]
                   + (vals(part).isSensor ? " | SENSOR" : "") + ";");
    }
    if (String(vals(part).categoryBits) !== "1" && vals(part).categoryBits !== undefined)
        lines.push("shapeDef.filter.categoryBits = " + vals(part).categoryBits + "ull;");
    if (String(vals(part).maskBits) !== "18446744073709551615" && vals(part).maskBits !== undefined)
        lines.push("shapeDef.filter.maskBits = " + vals(part).maskBits + "ull;");
    if (vals(part).groupIndex)
        lines.push("shapeDef.filter.groupIndex = " + vals(part).groupIndex + ";");
    if (vals(part).isSensor)
        lines.push("shapeDef.isSensor = true;");
    if (vals(part).enableSensorEvents)
        lines.push("shapeDef.enableSensorEvents = true;");
    // b2DefaultShapeDef leaves contact events off.
    var watched = WATCHED[part.name] || WATCHED[body.name];
    if (vals(part).enableContactEvents || watched)
        lines.push("shapeDef.enableContactEvents = true;");
    if (vals(part).enableHitEvents || watched)
        lines.push("shapeDef.enableHitEvents = true;");
    if (vals(part).enablePreSolveEvents)
        lines.push("shapeDef.enablePreSolveEvents = true;");

    var keep = part.name && USED[part.name] && !isOutline(part)
        ? (NAMES["shape:" + part.name] + " = ") : "";
    var c = part.center || { x: 0, y: 0 };

    if (part.kind === "box") {
        var hw = part.halfExtents.x, hh = part.halfExtents.y;
        var r = Math.max(0, Math.min(part.cornerRadius || 0, Math.min(hw, hh)));
        var box = geometryName("box", counts);
        var rot = part.rotation ? ("b2MakeRot(rad(" + short(part.rotation) + "))") : "b2Rot_identity";
        var make;
        if (r > 0 && !c.x && !c.y && !part.rotation) {
            make = "b2MakeRoundedBox(m(" + short(Math.max(hw - r, 0.01)) + "), m("
                   + short(Math.max(hh - r, 0.01)) + "), m(" + short(r) + "))";
        } else if (r > 0) {
            make = "b2MakeOffsetRoundedBox(m(" + short(Math.max(hw - r, 0.01)) + "), m("
                   + short(Math.max(hh - r, 0.01)) + "), m(" + short(c.x) + ", " + short(c.y)
                   + "), " + rot + ", m(" + short(r) + "))";
        } else if (!c.x && !c.y && !part.rotation) {
            make = "b2MakeBox(m(" + short(hw) + "), m(" + short(hh) + "))";
        } else {
            make = "b2MakeOffsetBox(m(" + short(hw) + "), m(" + short(hh) + "), m("
                   + short(c.x) + ", " + short(c.y) + "), " + rot + ")";
        }
        lines.push("b2Polygon " + box + " = " + make + ";");
        lines.push(keep + "b2CreatePolygonShape(" + bodyName + ", &shapeDef, &" + box + ");");
        return lines;
    }

    if (part.kind === "circle") {
        var circle = geometryName("circle", counts);
        lines.push("b2Circle " + circle + " = { m(" + short(c.x) + ", " + short(c.y) + "), m("
                   + short(part.radius) + ") };");
        lines.push(keep + "b2CreateCircleShape(" + bodyName + ", &shapeDef, &" + circle + ");");
        return lines;
    }

    var pts = part.points || [];
    var points = geometryName("points", counts);
    var list = [];
    for (var i = 0; i < pts.length; ++i)
        list.push("m(" + short(pts[i].x) + ", " + short(pts[i].y) + ")");
    lines = lines.concat(wrapList("b2Vec2 " + points + "[] = { ", list, " };"));

    if (isSolidPolygon(part)) {
        var hull = geometryName("hull", counts);
        var polygon = geometryName("polygon", counts);
        lines.push("b2Hull " + hull + " = b2ComputeHull(" + points + ", " + pts.length + ");");
        lines.push("b2Polygon " + polygon + " = b2MakePolygon(&" + hull + ", 0.0f);");
        lines.push(keep + "b2CreatePolygonShape(" + bodyName + ", &shapeDef, &" + polygon + ");");
        return lines;
    }

    var closed = part.kind === "polygon" || part.closed;
    if (part.kind === "chain" && part.smoothChain && pts.length >= 4) {
        // A chain is one-sided and smooth across its joins; the editor builds
        // it with b2CreateChain when asked to.
        lines.push("b2ChainDef chainDef = b2DefaultChainDef();");
        lines.push("chainDef.points = " + points + ";");
        lines.push("chainDef.count = " + pts.length + ";");
        if (closed)
            lines.push("chainDef.isLoop = true;");
        lines.push("chainDef.filter = shapeDef.filter;");
        lines.push("chainDef.materials = &shapeDef.material;");
        lines.push("b2CreateChain(" + bodyName + ", &chainDef);");
        return lines;
    }

    // Otherwise one two-sided segment per edge.
    var edges = closed ? pts.length : pts.length - 1;
    var next = closed ? ("(i + 1) % " + pts.length) : "i + 1";
    lines.push("for (int i = 0; i < " + edges + "; ++i) {");
    lines.push("    b2Segment segment = { " + points + "[i], " + points + "[" + next + "] };");
    lines.push("    b2CreateSegmentShape(" + bodyName + ", &shapeDef, &segment);");
    lines.push("}");
    return lines;
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
    var walls = [
        [w / 2 + t, t / 2, 0, -h / 2 - t / 2], [w / 2 + t, t / 2, 0, h / 2 + t / 2],
        [t / 2, h / 2, -w / 2 - t / 2, 0], [t / 2, h / 2, w / 2 + t / 2, 0],
    ];
    var lines = [
        "// The walls around the field.",
        "b2BodyDef bodyDef = b2DefaultBodyDef();",
        "b2BodyId walls = b2CreateBody(world, &bodyDef);",
        "b2ShapeDef shapeDef = b2DefaultShapeDef();",
    ];
    for (var i = 0; i < walls.length; ++i) {
        lines.push((i === 0 ? "b2Polygon wall = " : "wall = ") + "b2MakeOffsetBox(m("
                   + short(walls[i][0]) + "), m(" + short(walls[i][1]) + "), m("
                   + short(walls[i][2]) + ", " + short(walls[i][3]) + "), b2Rot_identity);");
        lines.push("b2CreatePolygonShape(walls, &shapeDef, &wall);");
    }
    return NEWLINE + "    {" + NEWLINE + indent("        ", lines) + "    }" + NEWLINE;
}

// --- joints ----------------------------------------------------------------

function jointsCode(scene) {
    var out = "";
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j)
        out += NEWLINE + jointCode(scene, joints[j], NAMES["joint:" + j]);
    return out;
}

function jointCode(scene, joint, name) {
    var p = joint.params || {};
    var bodies = scene.simulation.bodies;
    if (joint.bodyA < 0 || joint.bodyB < 0) {
        return "    // " + name + " holds one body to a point in the world; not exported."
               + NEWLINE;
    }
    var A = NAMES["body:" + joint.bodyA], B = NAMES["body:" + joint.bodyB];
    var anchors = joint.anchors || [];
    var a = anchors.length > 0 ? anchors[0] : { x: 0, y: 0 };
    var b = anchors.length > 1 ? anchors[1] : a;
    var type = joint.type;
    var Type = type.charAt(0).toUpperCase() + type.slice(1);

    var lines = ["b2" + Type + "JointDef jointDef = b2Default" + Type + "JointDef();",
                 "jointDef.bodyIdA = " + A + ";", "jointDef.bodyIdB = " + B + ";"];
    if (type !== "motor" && type !== "filter" && type !== "mouse") {
        lines.push("jointDef.localAnchorA = b2Body_GetLocalPoint(" + A + ", m(" + short(a.x)
                   + ", " + short(a.y) + "));");
        lines.push("jointDef.localAnchorB = b2Body_GetLocalPoint(" + B + ", m(" + short(b.x)
                   + ", " + short(b.y) + "));");
    }

    var set = function (field, value) { lines.push("jointDef." + field + " = " + value + ";"); };
    var flag = function (field, value, fallback) {
        if (!!value !== fallback)
            set(field, bool(value));
    };
    var number = function (field, value, fallback) {
        if (differs(Number(value) || 0, fallback))
            set(field, fnum(value));
    };
    var length = function (field, value) {
        if (value)
            set(field, "m(" + short(value) + ")");
    };
    var angle = function (field, value) {
        if (value)
            set(field, "rad(" + short(value) + ")");
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
        if (differs(x, fallbackX) || differs(y, fallbackY))
            set("localAxisA", "b2Vec2{ " + fnum(x) + ", " + fnum(y) + " }");
    };

    if (type === "revolute") {
        angle("referenceAngle", resting + pick(p, "referenceAngle", 0));
        angle("targetAngle", pick(p, "targetAngle", 0));
        flag("enableSpring", p.enableSpring, false);
        number("hertz", pick(p, "hertz", 0), 0);
        number("dampingRatio", pick(p, "dampingRatio", 0), 0);
        flag("enableLimit", p.enableLimit, false);
        // Box2D asserts on lower > upper and on a limit past a half turn.
        var angles = ordered(clampAngle(pick(p, "lowerAngle", 0)),
                             clampAngle(pick(p, "upperAngle", 0)));
        angle("lowerAngle", angles.lower);
        angle("upperAngle", angles.upper);
        flag("enableMotor", p.enableMotor, false);
        number("maxMotorTorque", pick(p, "maxMotorTorque", 0), 0);
        angle("motorSpeed", pick(p, "motorSpeed", 0));
    } else if (type === "prismatic" || type === "wheel") {
        var wheel = type === "wheel";
        axis(wheel ? 0 : 1, wheel ? 1 : 0);
        if (!wheel) {
            angle("referenceAngle", resting + pick(p, "referenceAngle", 0));
            length("targetTranslation", pick(p, "targetTranslation", 0));
        }
        flag("enableSpring", pick(p, "enableSpring", wheel), wheel);
        number("hertz", pick(p, "hertz", wheel ? 1 : 0), wheel ? 1 : 0);
        number("dampingRatio", pick(p, "dampingRatio", wheel ? 0.7 : 0), wheel ? 0.7 : 0);
        flag("enableLimit", p.enableLimit, false);
        var span = ordered(pick(p, "lowerTranslation", 0), pick(p, "upperTranslation", 0));
        length("lowerTranslation", span.lower);
        length("upperTranslation", span.upper);
        flag("enableMotor", p.enableMotor, false);
        if (wheel) {
            number("maxMotorTorque", pick(p, "maxMotorTorque", 0), 0);
            angle("motorSpeed", pick(p, "motorSpeed", 0));
        } else {
            number("maxMotorForce", pick(p, "maxMotorForce", 0), 0);
            length("motorSpeed", pick(p, "motorSpeed", 0));
        }
    } else if (type === "distance") {
        // Zero length means however far apart the anchors already are.
        var distance = pick(p, "length", 0);
        if (!(distance > 0))
            distance = Math.sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        set("length", "m(" + short(Math.max(distance, 1)) + ")");
        flag("enableSpring", p.enableSpring, false);
        number("hertz", pick(p, "hertz", 0), 0);
        number("dampingRatio", pick(p, "dampingRatio", 0), 0);
        flag("enableLimit", p.enableLimit, false);
        length("minLength", pick(p, "minLength", 0));
        length("maxLength", pick(p, "maxLength", 0));
        flag("enableMotor", p.enableMotor, false);
        number("maxMotorForce", pick(p, "maxMotorForce", 0), 0);
        length("motorSpeed", pick(p, "motorSpeed", 0));
    } else if (type === "weld") {
        angle("referenceAngle", resting + pick(p, "referenceAngle", 0));
        number("linearHertz", pick(p, "linearHertz", 0), 0);
        number("angularHertz", pick(p, "angularHertz", 0), 0);
        number("linearDampingRatio", pick(p, "linearDampingRatio", 0), 0);
        number("angularDampingRatio", pick(p, "angularDampingRatio", 0), 0);
    } else if (type === "motor") {
        var ox = pick(p, "linearOffsetX", 0), oy = pick(p, "linearOffsetY", 0);
        if (ox || oy)
            set("linearOffset", "m(" + short(ox) + ", " + short(oy) + ")");
        angle("angularOffset", pick(p, "angularOffset", 0));
        number("maxForce", pick(p, "maxForce", 1), 1);
        number("maxTorque", pick(p, "maxTorque", 1), 1);
        number("correctionFactor", pick(p, "correctionFactor", 0.3), 0.3);
    } else if (type === "mouse") {
        var tx = pick(p, "targetX", 0), ty = pick(p, "targetY", 0);
        var target = (tx === 0 && ty === 0) ? a : { x: tx, y: ty };
        set("target", "m(" + short(target.x) + ", " + short(target.y) + ")");
        number("hertz", pick(p, "hertz", 4), 4);
        number("dampingRatio", pick(p, "dampingRatio", 1), 1);
        number("maxForce", pick(p, "maxForce", 1), 1);
    }
    if (type !== "filter")
        flag("collideConnected", joint.collideConnected, false);
    lines.push(name + " = b2Create" + Type + "Joint(world, &jointDef);");

    var hertz = pick(p, "constraintHertz", 60), damping = pick(p, "constraintDampingRatio", 2);
    if (differs(hertz, 60) || differs(damping, 2))
        lines.push("b2Joint_SetConstraintTuning(" + name + ", " + fnum(hertz) + ", "
                   + fnum(damping) + ");");
    return "    {" + NEWLINE + indent("        ", lines) + "    }" + NEWLINE;
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
    var loops = { begin: [], end: [], hit: [], sensorBegin: [], sensorEnd: [],
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
        out = out.concat(rayCast(rays[r], NAMES["ray:" + r]));

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

    for (var c = 0; c < checks.length; ++c) {
        out.push("");
        out = out.concat(checks[c]);
    }
    return out.length ? wrapLong(indent("    ", out)) : "";
}

// Breaks a line longer than about 90 columns after its last && or || that
// fits, and indents what follows under it.
function wrapLong(text) {
    var lines = text.split(NEWLINE);
    var out = [];
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        var lead = line.match(/^ */)[0];
        var continuation = lead + "    ";
        while (line.length > 92 && line.replace(/^ */, "").indexOf("//") !== 0) {
            // At an || if there is one, so a bracketed pair stays together.
            var cut = -1;
            var ops = [" || ", " && "];
            for (var o = 0; o < ops.length && cut < 0; ++o) {
                var at = line.lastIndexOf(ops[o], 90);
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
    var when = rule.event
        ? (rule.subject + " " + rule.event + (rule.when ? (" " + rule.when) : ""))
        : (rule.subject + "." + rule.watch + " " + (rule.compare || ">") + " " + String(rule.when));
    var then;
    if (rule.action)
        then = rule.action + " " + rule.target;
    else if (rule.sourceObject)
        then = rule.target + "." + rule.property + " " + (rule.op || "set") + " "
               + rule.sourceObject + "." + rule.sourceProperty;
    else
        then = rule.target + "." + rule.property + " " + (rule.op || "set")
               + (rule.value === null || rule.value === undefined ? "" : (" " + String(rule.value)));
    return "when " + when + ", " + then;
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

    var event = rule.event;
    var pair = { contactBegin: "begin", contactEnd: "end", contactHit: "hit",
                 sensorBegin: "sensorBegin", sensorEnd: "sensorEnd" };

    if (pair[event]) {
        var subject = side(scene, rule.subject);
        if (!subject)
            return { error: "cannot tell which shape " + rule.subject + " is" };
        var partner = rule.when ? side(scene, String(rule.when)) : null;
        if (rule.when && !partner)
            return { error: "cannot tell which shape " + rule.when + " is" };
        var effect = effectLines(scene, rule, "other");
        if (!effect)
            return { error: "nothing in Box2D does " + describe(rule).split(", ")[1] };
        loops[pair[event]].push({ caption: caption, subject: subject, partner: partner,
                                  sensor: event.indexOf("sensor") === 0, effect: effect,
                                  usesOther: needsOther(rule), once: once });
        return {};
    }

    if (event === "bodyFellAsleep" || event === "bodyMoved") {
        var body = resolve(scene, rule.subject);
        if (body && body.kind === "shape")
            body = { kind: "body", handle: body.bodyHandle };
        if (!body || body.kind !== "body")
            return { error: rule.subject + " is not a body" };
        var acting = effectLines(scene, rule, null);
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
    if (event === "rayDetects") {
        var ray = resolve(scene, rule.subject);
        if (!ray || ray.kind !== "ray")
            return { error: rule.subject + " is not a ray" };
        condition = ray.handle + ".hit";
        if (rule.when) {
            var seen = side(scene, String(rule.when));
            if (!seen)
                return { error: "cannot tell which shape " + rule.when + " is" };
            condition += " && " + seen.test(ray.handle + ".shapeId");
        }
        effectOther = ray.handle + ".shapeId";
    } else if (event === "limitLower" || event === "limitUpper" || event === "limitEither") {
        var joint = resolve(scene, rule.subject);
        var reader = joint && joint.kind === "joint" ? LIMITS[joint.type] : null;
        if (!reader)
            return { error: rule.subject + " has no limit Box2D reports" };
        var J = joint.handle;
        var value = reader.value + "(" + J + ")";
        var lower = value + " <= " + reader.prefix + "GetLowerLimit(" + J + ") + 0.005f";
        var upper = value + " >= " + reader.prefix + "GetUpperLimit(" + J + ") - 0.005f";
        var at = event === "limitLower" ? lower : event === "limitUpper" ? upper
               : ("(" + lower + " || " + upper + ")");
        condition = reader.prefix + "IsLimitEnabled(" + J + ") && " + at;
        // Starting against the stop is not arriving at it.
        startsTrue = "true";
    } else if (event) {
        return { error: "Box2D has no " + event + " event to read" };
    } else {
        condition = valueCondition(scene, rule);
        if (!condition)
            return { error: "cannot read " + rule.subject + "." + rule.watch };
    }

    var actions = effectLines(scene, rule, effectOther);
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
    STATE.push(type + " " + name + ";" + NEWLINE);
    RESETS.push("    " + name + " = " + initial + ";" + NEWLINE);
}

function needsOther(rule) { return rule.target === "@other" || rule.target === "@otherBody"; }

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
        var s = HITS[i];
        lines.push("");
        lines.push("// How hard " + s.handle + " was last hit, and where.");
        lines.push("if (B2_ID_EQUALS(a, " + s.handle + ") || B2_ID_EQUALS(b, " + s.handle + ")) {");
        lines.push("    " + s.handle + "HitSpeed = contacts.hitEvents[i].approachSpeed;");
        lines.push("    " + s.handle + "HitPoint = contacts.hitEvents[i].point;");
        lines.push("    " + s.handle + "HitNormal = B2_ID_EQUALS(a, " + s.handle
                   + ") ? contacts.hitEvents[i].normal : b2Neg(contacts.hitEvents[i].normal);");
        lines.push("}");
    }
    return lines.length ? [{ raw: lines }] : [];
}

function rayCast(ray, name) {
    var angle = (ray.angle || 0) * Math.PI / 180;
    var dx = Math.cos(angle) * (ray.length || 0), dy = Math.sin(angle) * (ray.length || 0);
    var call = "b2World_CastRayClosest(world, m(" + short(ray.x) + ", " + short(ray.y) + "), m("
               + short(dx) + ", " + short(dy) + "), ";
    var mask = String(ray.maskBits || "ffffffffffffffff").toLowerCase();
    if (/^f+$/.test(mask) && mask.length >= 16)
        return [name + " = " + call + "b2DefaultQueryFilter());"];
    return ["{",
            "    b2QueryFilter filter = b2DefaultQueryFilter();",
            "    filter.maskBits = 0x" + mask + "ull;",
            "    " + name + " = " + call + "filter);",
            "}"];
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
    var lines = ["", "if (debug) {"];
    var joints = [], bodies = [];
    for (var j = 0; j < scene.simulation.joints.length; ++j)
        joints.push(NAMES["joint:" + j]);
    for (var b = 0; b < scene.simulation.bodies.length; ++b)
        bodies.push(NAMES["body:" + b]);
    if (joints.length) {
        lines = lines.concat(wrapList("    for (b2JointId joint : { ", joints, " })"));
        lines.push("        drawJoint(painter, joint);");
    }
    if (bodies.length) {
        lines = lines.concat(wrapList("    for (b2BodyId body : { ", bodies, " })"));
        lines.push("        drawAxes(painter, body);");
    }
    lines = lines.concat(rayLines(scene));
    lines.push("}");
    return indent("        ", lines);
}

function rayLines(scene) {
    var rays = scene.rays || [];
    var lines = [];
    for (var i = 0; i < rays.length; ++i) {
        var angle = (rays[i].angle || 0) * Math.PI / 180;
        lines.push("    drawRay(painter, m(" + short(rays[i].x) + ", " + short(rays[i].y) + "), m("
                   + short(Math.cos(angle) * rays[i].length) + ", "
                   + short(Math.sin(angle) * rays[i].length) + "), " + NAMES["ray:" + i] + ");");
    }
    return lines;
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
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j) {
        if (joints[j].name === name)
            return { kind: "joint", type: joints[j].type, handle: NAMES["joint:" + j] };
    }
    var bodies = scene.simulation.bodies;
    for (var b = 0; b < bodies.length; ++b) {
        if (bodies[b].name === name)
            return { kind: "body", handle: NAMES["body:" + b] };
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
                     bodyHandle: NAMES["body:" + i] };
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
    } else if (t === "prismatic") {
        switch (key) {
        case "translation": return simple("len", "GetTranslation", null);
        case "speed": return simple("len", "GetSpeed", null);
        case "motorSpeed": return simple("len", "GetMotorSpeed", "SetMotorSpeed");
        case "motorForce": return simple("none", "GetMotorForce", null);
        case "maxMotorForce": return simple("none", "GetMaxMotorForce", "SetMaxMotorForce");
        case "targetTranslation": return simple("len", null, "SetTargetTranslation");
        case "lowerTranslation": return limit("len", "lower", "GetLowerLimit", "GetUpperLimit", "SetLimits", false);
        case "upperTranslation": return limit("len", "upper", "GetLowerLimit", "GetUpperLimit", "SetLimits", false);
        }
    } else if (t === "wheel") {
        switch (key) {
        case "motorSpeed": return simple("angle", "GetMotorSpeed", "SetMotorSpeed");
        case "motorTorque": return simple("none", "GetMotorTorque", null);
        case "maxMotorTorque": return simple("none", "GetMaxMotorTorque", "SetMaxMotorTorque");
        case "lowerTranslation": return limit("len", "lower", "GetLowerLimit", "GetUpperLimit", "SetLimits", false);
        case "upperTranslation": return limit("len", "upper", "GetLowerLimit", "GetUpperLimit", "SetLimits", false);
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
    if (unit === "bits")   return String(Math.max(0, Math.round(x))) + "ull";
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

function valueCondition(scene, rule) {
    var subject = resolve(scene, rule.subject);
    if (!subject)
        return null;
    var compare = rule.compare || ">";

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
        if (compare === "=")
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
        if (op === "add" && p.read)
            value = p.read + " + " + value;
    } else if (op === "add") {
        if (!p.read || p.unit === "bool")
            return null;
        value = p.read + " + " + literal(p.unit, rule.value);
    } else {
        value = literal(p.unit, rule.value);
    }
    return p.write(value);
}

function actionLines(scene, rule, target) {
    var params = rule.actionParams || {};
    var body = target.kind === "shape" ? target.bodyHandle : target.handle;

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
            lines.push("    explosion.maskBits = " + Math.round(settings.maskBits) + "ull;");
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
        // Box2D takes the body's shapes and joints with it.
        return ["b2DestroyBody(" + body + ");"];
    }
    if (rule.action === "breakJoint") {
        if (target.kind !== "joint")
            return null;
        return ["b2Joint_WakeBodies(" + target.handle + ");", "b2DestroyJoint(" + target.handle + ");"];
    }
    return null;
}

function destroys(scene) {
    var rules = scene.rules || [];
    for (var i = 0; i < rules.length; ++i) {
        if (rules[i].action === "removeBody" || rules[i].action === "breakJoint")
            return true;
    }
    return false;
}

// --- the window around it --------------------------------------------------

function controlsCode(own) {
    if (!own.addControls && !own.debugView)
        return "";
    var lines = ["", "QToolBar *toolbar = window.addToolBar(\"Controls\");"];
    if (own.addControls) {
        lines.push("QObject::connect(toolbar->addAction(\"Start\"), &QAction::triggered, view,");
        lines.push("                 [view] { view->run(true); });");
        lines.push("QObject::connect(toolbar->addAction(\"Pause\"), &QAction::triggered, view,");
        lines.push("                 [view] { view->run(false); });");
        lines.push("QObject::connect(toolbar->addAction(\"Reset\"), &QAction::triggered, view,");
        lines.push("                 [view] { view->reset(); });");
    }
    if (own.debugView) {
        lines.push("QAction *debug = toolbar->addAction(\"Debug view\");");
        lines.push("debug->setCheckable(true);");
        lines.push("debug->setChecked(view->debug);");
        lines.push("QObject::connect(debug, &QAction::toggled, view, [view](bool on) { view->setDebug(on); });");
    }
    return indent("    ", lines);
}

// The editor paints a body by what it is -- dynamic, static, kinematic -- and
// Box2D lets a shape carry its own debug-draw colour, so each shape is given
// its body's.
function bodyColours(physics) {
    var out = { constants: "", byType: {} };
    // The editor's own defaults, for an export made without a settings file.
    var types = [["dynamic", "COLOR_DYNAMIC", physics.bodyDynamicColor, "2e86c1"],
                 ["static", "COLOR_STATIC", physics.bodyStaticColor, "279e6a"],
                 ["kinematic", "COLOR_KINEMATIC", physics.bodyKinematicColor, "884ea0"]];
    var lines = [];
    for (var i = 0; i < types.length; ++i) {
        var hex = rgb(types[i][2]) || types[i][3];
        lines.push("const b2HexColor " + types[i][1] + " = b2HexColor(0x" + hex + ");");
        out.byType[types[i][0]] = types[i][1];
    }
    var alpha = Math.max(0, Math.min(255, Math.round(pickNumber(physics.fillAlpha, 90))));
    lines.push("const int FILL_ALPHA = " + alpha + ";");

    // A sensor is drawn open and hatched, the way the editor draws it. Box2D
    // hands a shape's custom colour to the draw callbacks as it is, so a bit
    // above the 24 an RGB colour uses marks the shapes that are sensors.
    var sensor = qtColour(physics.sensorColor, "255, 5, 201, 54");
    lines.push("");
    lines.push("const uint32_t SENSOR = 0x1000000;");
    lines.push("const QColor SENSOR_COLOR(" + sensor + ");");
    lines.push("const Qt::BrushStyle SENSOR_PATTERN = Qt::" + BRUSH_STYLES[sensorPattern(physics.sensorPattern)] + ";");
    lines.push("const bool SENSOR_FILLED = " + bool(settingTrue(physics.sensorFillsBody)) + ";");
    out.constants = NEWLINE + lines.join(NEWLINE) + NEWLINE;
    return out;
}

// "#aarrggbb" or "#rrggbb" to "rrggbb".
function rgb(colour) {
    var text = String(colour || "");
    if (/^#[0-9a-fA-F]{8}$/.test(text)) return text.substring(3);
    if (/^#[0-9a-fA-F]{6}$/.test(text)) return text.substring(1);
    return "";
}

// --- odds and ends ---------------------------------------------------------

function indent(prefix, lines) {
    var out = "";
    for (var i = 0; i < lines.length; ++i)
        out += (lines[i] ? (prefix + lines[i]) : "") + NEWLINE;
    return out;
}

function indentLines(prefix, lines) {
    var out = [];
    for (var i = 0; i < lines.length; ++i)
        out.push(lines[i] ? (prefix + lines[i]) : "");
    return out;
}

function fill(template, values) {
    var out = template;
    for (var key in values) {
        if (Object.prototype.hasOwnProperty.call(values, key))
            out = out.split("{{" + key + "}}").join(values[key]);
    }
    return out;
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
