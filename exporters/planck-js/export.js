// Physalis scene -> one Planck.js web page, index.html.
//
// The page is meant to read like a Planck.js example. createWorld() makes the
// world, then each body, fixture and joint, keeping them in variables named
// after the objects in the editor. step() calls world.step() and then does
// what the scene's rules say, written as ordinary if statements. draw() paints
// every fixture the world holds. Nothing of the editor itself is carried
// across: no scene description to load, no rules as data.
//
// index.html.tmpl is the page around the scene; this script works out the
// parts that depend on the scene, and writes a def field only when it differs
// from Planck's own default.
//
// Planck.js is Box2D 2.4, not v3, and several things the editor offers have no
// counterpart in it. Those are reported through io.log() rather than dropped.

var NEWLINE = String.fromCharCode(10);

var T = null;       // template loader
var PPM = 1000;     // scene units per metre
var MOTION = 0.05;  // 50 / PPM -- the scale the editor quotes world speeds at
var MISSED = null;  // what this target cannot represent

// How much every polygon is pulled in from its drawn outline, in scene units.
//
// In Box2D 2.4 a polygon collides as its vertices pushed out by a skin of
// 2 x linearSlop; Box2D v3, which the editor runs, has no skin. Pulling the
// vertices in by twice the skin puts the collision surface just inside the
// drawn outline, so two shapes drawn flush touch rather than overlap -- which
// matters most for anything held on a joint, since it cannot be shoved aside.
var INSET = 0.0;

var NAMES = null;   // editor name -> JavaScript variable
var TAKEN = null;
var USED = null;    // fixtures whose handles the step code needs kept
var STATE = null;   // variables the step code keeps between steps
var RESETS = null;  // what createWorld() sets those back to
var COUNTERS = null;
var STREAMS = null; // which Planck callbacks the step code needs recorded
var HITS = null;
var LOST = false;
var WORLD_SETTINGS = {};

// What the engine says an object has, under the names it publishes. The
// application keeps them in one bag per object rather than as fields of its
// own, because which ones exist is the engine's business -- and this converter
// writes Box2D through Planck, so it reads Box2D's names out of it.
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
    INSET = 2.0 * (2.0 * 0.005 * MOTION) * PPM;
    MISSED = {};

    var cache = {};
    T = function (name) {
        if (!(name in cache))
            cache[name] = io.read("templates/" + name);
        return cache[name];
    };

    var project = safeName(own.projectName) || "PhysalisScene";
    var source = String(own.librarySource || "cdn");
    var libraryTag;
    if (source === "npm") {
        libraryTag = '<script src="node_modules/planck/dist/planck.min.js"></script>' + NEWLINE;
        io.write("package.json", packageJson(project, own));
        io.log("npm: run 'npm install' beside index.html before opening it.");
    } else {
        libraryTag = '<script src="' + libraryUrl(own) + '"></script>' + NEWLINE;
        io.log("index.html loads Planck.js from " + libraryUrl(own) + " -- opening it needs a network.");
    }

    TAKEN = {};
    for (var r = 0; r < RESERVED.length; ++r)
        TAKEN[RESERVED[r]] = true;
    NAMES = nameObjects(scene);
    USED = {};
    STATE = [];
    RESETS = [];
    COUNTERS = { time: false, frame: false };
    STREAMS = { begun: false, ended: false, hits: false };
    HITS = null;
    LOST = destroys(scene);
    WORLD_SETTINGS = world;

    // The step code first: it decides which fixtures have to be kept and which
    // of Planck's callbacks have to be listened to.
    var stepCode = stepBody(scene, io);

    io.write("index.html", fill(T("index.html.tmpl"), {
        TITLE: project,
        PIXELS_PER_METER: num(PPM),
        SETTINGS: settingsCode(scene),
        IDS: idDeclarations(scene),
        STATE: STATE.length ? (NEWLINE + STATE.join("")) : "",
        RESETS: RESETS.length ? (RESETS.join("") + NEWLINE) : "",
        WORLD: worldCode(world),
        BODIES: bodiesCode(scene),
        FIELD_BOUNDS: fieldBoundsCode(world, field),
        JOINTS: jointsCode(scene),
        LISTENERS: listenersCode(),
        STEP: stepCode,
        RAY_HELPER: (scene.rays || []).length ? rayHelper() : "",
        RAY_DRAWING: rayDrawing(scene),
        STEPS_PER_SECOND: int(own.stepsPerSecond, 60),
        WIDTH: String(Math.round(field.width || 1000)),
        HEIGHT: String(Math.round(field.height || 600)),
        BACKGROUND: cssColour(field.backgroundColor, "#ffffff"),
        // The editor's own defaults, for an export made without a settings file.
        COLOR_DYNAMIC: cssColour(opaque(physics.bodyDynamicColor), "#2e86c1"),
        COLOR_STATIC: cssColour(opaque(physics.bodyStaticColor), "#279e6a"),
        COLOR_KINEMATIC: cssColour(opaque(physics.bodyKinematicColor), "#884ea0"),
        FILL_ALPHA: num(Math.max(0, Math.min(255, pickNumber(physics.fillAlpha, 90))) / 255),
        POLYGON_INSET: short(INSET),
        SENSOR_COLOR: cssColour(opaque(physics.sensorColor), "#05c936"),
        SENSOR_PATTERN: String(sensorPattern(physics.sensorPattern)),
        SENSOR_FILLED: bool(settingTrue(physics.sensorFillsBody)),
        DEBUG_VIEW: bool(own.debugView),
        AXIS_LENGTH: plain(pickNumber(physics.bodyAxisLength, 40)),
        AXIS_WIDTH: plain(pickNumber(physics.bodyAxisWidth, 2)),
        AXIS_X_COLOR: cssColour(physics.bodyAxisXColor, "#dc3232"),
        AXIS_Y_COLOR: cssColour(physics.bodyAxisYColor, "#28a03c"),
        JOINT_COLOR: cssColour(physics.jointColor, "rgba(232, 196, 106, 0.67)"),
        JOINT_ANCHOR_RADIUS: plain(pickNumber(physics.jointAnchorRadius, 7)),
        LIBRARY: libraryTag,
        CONTROLS: controlsHtml(own),
        CONTROL_WIRING: controlsWiring(own),
    }));

    io.log("Exported " + scene.simulation.bodies.length + " bodies, "
           + scene.simulation.joints.length + " joints and "
           + (scene.rules || []).length + " rules into index.html.");

    var missed = [];
    for (var key in MISSED) {
        if (Object.prototype.hasOwnProperty.call(MISSED, key))
            missed.push(key + " (" + MISSED[key] + ")");
    }
    if (missed.length) {
        io.log("Planck.js is Box2D 2.4 and has no equivalent for:");
        for (var i = 0; i < missed.length; ++i)
            io.log("  - " + missed[i]);
    }
    return true;
}

// Something the scene asks for that Planck cannot do, recorded once per kind.
function missing(what, where) {
    if (!(what in MISSED))
        MISSED[what] = where;
}

function libraryUrl(own) {
    var url = String(own.libraryUrl
                     || "https://cdn.jsdelivr.net/npm/planck@{version}/dist/planck.js").trim();
    return url.replace(/\{version\}/g, String(own.libraryVersion || "1.0.0").trim());
}

function packageJson(project, own) {
    var repo = String(own.libraryRepository || "https://github.com/piqnt/planck.js.git").trim();
    var ref = String(own.libraryRef || "").trim();
    var official = repo === "https://github.com/piqnt/planck.js.git";
    var dependency = official && !ref
        ? String(own.libraryVersion || "1.0.0")
        : ("git+" + repo + (ref ? "#" + ref : ""));
    return JSON.stringify({
        name: project.toLowerCase(),
        version: "1.0.0",
        private: true,
        description: "A Physalis scene, exported to Planck.js.",
        dependencies: { planck: dependency },
    }, null, 4) + NEWLINE;
}

// Box2D 2.4's tuning lengths assume a world built of metre-sized things. This
// one is not -- at 1000 px/m a drawn 40px box is 0.04m -- so they are scaled
// with the scene.
function settingsCode(scene) {
    var s = num(MOTION);
    var lines = [
        "// Planck's tolerances assume metre-sized objects; these are scaled to this scene.",
        "pl.Settings.linearSlop = 0.005 * " + s + ";",
        "pl.Settings.maxLinearCorrection = 0.2 * " + s + ";",
        "pl.Settings.maxTranslation = 2.0 * " + s + ";",
        "pl.Settings.linearSleepTolerance = 0.01 * " + s + ";",
        "pl.Settings.velocityThreshold = 1.0 * " + s + ";",
    ];
    if (hasRoundedBox(scene)) {
        lines.push("// A rounded box is a polygon that walks round its corners, which takes more");
        lines.push("// than Planck's usual twelve vertices.");
        lines.push("pl.Settings.maxPolygonVertices = " + (4 * (CORNER_STEPS + 1)) + ";");
    }
    return lines.join(NEWLINE) + NEWLINE;
}

// How many straight pieces each rounded corner is built from.
var CORNER_STEPS = 4;

function hasRoundedBox(scene) {
    var bodies = scene.simulation.bodies;
    for (var b = 0; b < bodies.length; ++b) {
        var parts = bodies[b].parts || [];
        for (var p = 0; p < parts.length; ++p) {
            if (parts[p].kind === "box" && (parts[p].cornerRadius || 0) > 0)
                return true;
        }
    }
    return false;
}

// The outline of a rounded box. Planck has no rounded polygon, so each
// corner's quarter circle is walked in CORNER_STEPS straight pieces.
function roundedBoxPoints(hw, hh, r, centre, degrees) {
    var turn = (degrees || 0) * Math.PI / 180;
    var cos = Math.cos(turn), sin = Math.sin(turn);
    var corners = [[hw - r, hh - r], [r - hw, hh - r], [r - hw, r - hh], [hw - r, r - hh]];
    var out = [];
    for (var k = 0; k < 4; ++k) {
        for (var s = 0; s <= CORNER_STEPS; ++s) {
            var a = (k + s / CORNER_STEPS) * Math.PI / 2;
            var x = corners[k][0] + r * Math.cos(a), y = corners[k][1] + r * Math.sin(a);
            var point = { x: centre.x + x * cos - y * sin, y: centre.y + x * sin + y * cos };
            var last = out.length ? out[out.length - 1] : null;
            // A capsule's two corners at each end meet in one point.
            if (!last || Math.abs(last.x - point.x) + Math.abs(last.y - point.y) > 1e-6)
                out.push(point);
        }
    }
    if (out.length > 1 && Math.abs(out[0].x - out[out.length - 1].x)
                          + Math.abs(out[0].y - out[out.length - 1].y) <= 1e-6)
        out.pop();
    return out;
}

// --- names -----------------------------------------------------------------

var RESERVED = ("world dt a b i m len rad pl step createWorld draw run reset canvas ctx debug "
    + "timer other contact contactsBegun contactsEnded hits points fixture body joint shape "
    + "castRay drawRay colourOf elapsed stepCount PIXELS_PER_METER planck window document "
    + "break case catch class const continue debugger default delete do else enum export "
    + "extends false finally for function if import in instanceof let new null return super "
    + "switch this throw true try typeof var void while with yield").split(" ");

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

function nameObjects(scene) {
    var names = {};
    var bodies = scene.simulation.bodies;
    for (var b = 0; b < bodies.length; ++b)
        names["body:" + b] = identifier(bodies[b].name, "body");
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

function idDeclarations(scene) {
    var names = [];
    var sim = scene.simulation;
    for (var b = 0; b < sim.bodies.length; ++b) {
        names.push(NAMES["body:" + b]);
        var parts = sim.bodies[b].parts || [];
        for (var p = 0; p < parts.length; ++p) {
            if (parts[p].name && USED[parts[p].name] && !isOutline(parts[p]))
                names.push(NAMES["shape:" + parts[p].name]);
        }
    }
    for (var j = 0; j < sim.joints.length; ++j)
        names.push(NAMES["joint:" + j]);
    var out = declare(names);
    var rays = scene.rays || [];
    for (var r = 0; r < rays.length; ++r)
        out += "var " + NAMES["ray:" + r] + " = { hit: false };" + NEWLINE;
    return out;
}

function declare(names) {
    if (names.length === 0)
        return "";
    var out = "var ";
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
    if (pickNumber(vals(world).contactHertz, 30) !== 30 || pickNumber(vals(world).contactDampingRatio, 10) !== 10)
        missing("contact stiffness and damping", "world");
    if (pickNumber(vals(world).restitutionThreshold, 1) !== 1)
        missing("restitution threshold", "world");
    if (pickNumber(vals(world).subStepCount, 4) !== 4)
        missing("solver sub-steps", "world -- Planck iterates instead");

    var fields = ["gravity: pl.Vec2(" + plain(g.x * MOTION) + ", " + plain(g.y * MOTION) + ")"];
    if (vals(world).enableSleep === false)
        fields.push("allowSleep: false");
    if (vals(world).enableContinuous === false)
        fields.push("continuousPhysics: false");

    // No pre-solve hook holding contacts back until shapes overlap: the polygon
    // inset already keeps shapes drawn flush from jamming, and holding contacts
    // back let a ball sink and bounce back out on every landing, so it never
    // came to rest the way it does in Box2D v3.
    return indent("    ", objectCall("world = pl.World(", fields, ");"));
}

// name(\n    field,\n    field,\n}) -- or all on one line when there is nothing.
function objectCall(head, fields, tail) {
    if (fields.length === 0)
        return [head + tail];
    var lines = [head + "{"];
    for (var i = 0; i < fields.length; ++i)
        lines.push("    " + fields[i] + ",");
    lines.push("}" + tail);
    return lines;
}

// --- bodies and fixtures ---------------------------------------------------

var GEOMETRY_COUNTS = null;

function bodiesCode(scene) {
    GEOMETRY_COUNTS = {};
    var out = "";
    var bodies = scene.simulation.bodies;
    for (var i = 0; i < bodies.length; ++i)
        out += NEWLINE + indent("    ", bodyLines(bodies[i], NAMES["body:" + i]));
    return out;
}

function bodyLines(body, name) {
    var fields = [];
    if (body.type === "dynamic" || body.type === "kinematic")
        fields.push("type: \"" + body.type + "\"");
    var p = body.position || { x: 0, y: 0 };
    if (p.x || p.y)
        fields.push("position: m(" + short(p.x) + ", " + short(p.y) + ")");
    if (body.rotation)
        fields.push("angle: rad(" + short(body.rotation) + ")");
    var v = { x: pickNumber(vals(body).velocityX, 0),
              y: pickNumber(vals(body).velocityY, 0) };
    if (v.x || v.y)
        fields.push("linearVelocity: pl.Vec2(" + num(v.x * MOTION) + ", " + num(v.y * MOTION) + ")");
    if (vals(body).angularVelocity)
        fields.push("angularVelocity: rad(" + short(vals(body).angularVelocity) + ")");
    if (vals(body).linearDamping)
        fields.push("linearDamping: " + num(vals(body).linearDamping));
    if (vals(body).angularDamping)
        fields.push("angularDamping: " + num(vals(body).angularDamping));
    if (differs(pickNumber(vals(body).gravityScale, 1), 1))
        fields.push("gravityScale: " + num(vals(body).gravityScale));
    if (vals(body).enableSleep === false)
        fields.push("allowSleep: false");
    if (vals(body).isAwake === false)
        fields.push("awake: false");
    if (vals(body).fixedRotation)
        fields.push("fixedRotation: true");
    if (vals(body).isBullet)
        fields.push("bullet: true");
    if (body.isEnabled === false)
        fields.push("active: false");
    if (vals(body).allowFastRotation)
        missing("allow fast rotation", "body " + body.name);

    var lines = fields.length ? objectCall(name + " = world.createBody(", fields, ");")
                              : [name + " = world.createBody();"];
    var parts = body.parts || [];
    for (var i = 0; i < parts.length; ++i)
        lines = lines.concat(fixtureLines(parts[i], body, name));
    return lines;
}

function isOutline(part) { return (part.kind === "polygon" && !isSolidPolygon(part)) || part.kind === "chain"; }

// Planck takes a convex polygon of up to twelve vertices.
function isSolidPolygon(part) {
    var pts = part.points || [];
    return part.kind === "polygon" && pts.length >= 3 && pts.length <= 12 && isConvex(pts);
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

function fixtureLines(part, body, bodyName) {
    if (isOutline(part) && body.type === "dynamic") {
        return ["// " + (part.name || "an outline") + " is an outline, which has no mass, "
                + "so a dynamic body cannot be made of it."];
    }
    if (pickNumber(vals(part).rollingResistance, 0) !== 0)
        missing("rolling resistance", "shape " + part.name);
    if (pickNumber(vals(part).tangentSpeed, 0) !== 0)
        missing("surface speed", "shape " + part.name);

    // Planck's own defaults are density 0 and friction 0.2.
    var def = [];
    if (differs(pickNumber(vals(part).density, 1), 0))
        def.push("density: " + plain(pickNumber(vals(part).density, 1)));
    if (differs(pickNumber(vals(part).friction, 0.6), 0.2))
        def.push("friction: " + plain(pickNumber(vals(part).friction, 0.6)));
    if (vals(part).restitution)
        def.push("restitution: " + plain(vals(part).restitution));
    if (vals(part).isSensor)
        def.push("isSensor: true");
    var category = filterBits(vals(part).categoryBits, 1, part.name);
    var mask = filterBits(vals(part).maskBits, 0xFFFF, part.name);
    if (category !== 1)
        def.push("filterCategoryBits: " + category);
    if (mask !== 0xFFFF)
        def.push("filterMaskBits: " + mask);
    if (vals(part).groupIndex)
        def.push("filterGroupIndex: " + vals(part).groupIndex);
    var defText = def.length ? (", { " + def.join(", ") + " }") : "";

    var keep = part.name && USED[part.name] && !isOutline(part)
        ? (NAMES["shape:" + part.name] + " = ") : "";
    var c = part.center || { x: 0, y: 0 };

    if (part.kind === "box" && (part.cornerRadius || 0) > 0) {
        var hw = pullIn(part.halfExtents.x), hh = pullIn(part.halfExtents.y);
        // The corners come in by the inset too, so growing the polygon back
        // out when it is drawn gives the corners the editor drew.
        var radius = Math.max(0, Math.min(part.cornerRadius - INSET, hw, hh));
        var rounded = roundedBoxPoints(hw, hh, radius, c, part.rotation);
        var corners = [];
        for (var q = 0; q < rounded.length; ++q)
            corners.push("m(" + short(rounded[q].x) + ", " + short(rounded[q].y) + ")");
        return ["// " + (part.name || "a box") + ", its corners rounded to " + short(part.cornerRadius) + "."]
            .concat(wrapList(keep + bodyName + ".createFixture(pl.Polygon([", corners,
                             "])" + defText + ");"));
    }

    if (part.kind === "box") {
        var shape = "pl.Box(len(" + short(pullIn(part.halfExtents.x)) + "), len("
                    + short(pullIn(part.halfExtents.y)) + ")";
        if (c.x || c.y || part.rotation)
            shape += ", m(" + short(c.x) + ", " + short(c.y) + "), rad(" + short(part.rotation || 0) + ")";
        return [keep + bodyName + ".createFixture(" + shape + ")" + defText + ");"];
    }

    if (part.kind === "circle") {
        var circle = (c.x || c.y)
            ? ("pl.Circle(m(" + short(c.x) + ", " + short(c.y) + "), len(" + short(part.radius) + "))")
            : ("pl.Circle(len(" + short(part.radius) + "))");
        return [keep + bodyName + ".createFixture(" + circle + defText + ");"];
    }

    var pts = isSolidPolygon(part) ? pulledInOutline(part.points || []) : (part.points || []);
    var list = [];
    for (var i = 0; i < pts.length; ++i)
        list.push("m(" + short(pts[i].x) + ", " + short(pts[i].y) + ")");

    if (isSolidPolygon(part)) {
        var lines = wrapList(keep + bodyName + ".createFixture(pl.Polygon([", list, "])" + defText + ");");
        return lines;
    }

    var points = geometryName("points");
    var out = wrapList("var " + points + " = [", list, "];");
    var closed = part.kind === "polygon" || part.closed;
    if (part.kind === "chain" && part.smoothChain && pts.length >= 4) {
        out.push(bodyName + ".createFixture(pl.Chain(" + points + ", " + bool(closed) + ")" + defText + ");");
        return out;
    }
    // One two-sided edge per segment.
    var edges = closed ? pts.length : pts.length - 1;
    out.push("for (var i = 0; i < " + edges + "; ++i)");
    out.push("    " + bodyName + ".createFixture(pl.Edge(" + points + "[i], " + points + "["
             + (closed ? ("(i + 1) % " + pts.length) : "i + 1") + "])" + defText + ");");
    return out;
}

function geometryName(kind) {
    GEOMETRY_COUNTS[kind] = (GEOMETRY_COUNTS[kind] || 0) + 1;
    return GEOMETRY_COUNTS[kind] === 1 ? kind : (kind + GEOMETRY_COUNTS[kind]);
}

function pullIn(half) {
    return Math.max(half - INSET, Math.min(half, 0.001));
}

// A convex outline with every edge moved in by INSET. The page draws each
// polygon grown back out by the same amount, so what is drawn is the outline
// the editor drew, and neighbours drawn edge to edge still meet.
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

// Planck filters with 16 bits where Box2D v3 has 64. A 64-bit all-ones mask
// means everything; anything else above the low 16 bits cannot be held.
function filterBits(value, fallback, where) {
    var n = Number(value);
    if (!isFinite(n) || n <= 0)
        return fallback;
    if (n >= 0xFFFF) {
        if (n > 0xFFFF && n < 18446744073709551615)
            missing("collision groups above the low 16 bits", where);
        return 0xFFFF;
    }
    return n & 0xFFFF;
}

function fieldBoundsCode(world, field) {
    if (!world.solidBounds)
        return "";
    var w = field.width || 1000, h = field.height || 1000, t = 40;
    var walls = [
        [w / 2 + t, t / 2, 0, -h / 2 - t / 2], [w / 2 + t, t / 2, 0, h / 2 + t / 2],
        [t / 2, h / 2, -w / 2 - t / 2, 0], [t / 2, h / 2, w / 2 + t / 2, 0],
    ];
    var lines = ["// The walls around the field.", "var walls = world.createBody();"];
    for (var i = 0; i < walls.length; ++i) {
        lines.push("walls.createFixture(pl.Box(len(" + short(walls[i][0]) + "), len(" + short(walls[i][1])
                   + "), m(" + short(walls[i][2]) + ", " + short(walls[i][3]) + "), 0));");
    }
    return NEWLINE + indent("    ", lines);
}

// --- joints ----------------------------------------------------------------

function jointsCode(scene) {
    var out = "";
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j) {
        var lines = jointLines(scene, joints[j], NAMES["joint:" + j]);
        if (lines.length)
            out += NEWLINE + indent("    ", lines);
    }
    return out;
}

function jointLines(scene, joint, name) {
    var p = joint.params || {};
    var bodies = scene.simulation.bodies;
    if (joint.bodyA < 0 || joint.bodyB < 0)
        return ["// " + name + " holds one body to a point in the world; not exported."];
    var A = NAMES["body:" + joint.bodyA], B = NAMES["body:" + joint.bodyB];
    var anchors = joint.anchors || [];
    var a = anchors.length > 0 ? anchors[0] : { x: 0, y: 0 };
    var b = anchors.length > 1 ? anchors[1] : a;
    var at = function (point) { return "m(" + short(point.x) + ", " + short(point.y) + ")"; };
    var fields = [];
    var type = joint.type;
    var resting = (bodies[joint.bodyB].rotation || 0) - (bodies[joint.bodyA].rotation || 0);
    var angle = function (field, value) {
        if (value)
            fields.push(field + ": rad(" + short(value) + ")");
    };
    var number = function (field, value, fallback) {
        if (differs(Number(value) || 0, fallback))
            fields.push(field + ": " + plain(value));
    };
    var flag = function (field, value) {
        if (value)
            fields.push(field + ": true");
    };
    var call;

    if (has(p, "constraintHertz") || has(p, "constraintDampingRatio"))
        missing("per-joint constraint tuning", "joint " + joint.name);

    if (type === "revolute") {
        if (p.enableSpring)
            missing("revolute spring", "joint " + joint.name);
        angle("referenceAngle", resting + pick(p, "referenceAngle", 0));
        flag("enableLimit", p.enableLimit);
        var angles = ordered(clampAngle(pick(p, "lowerAngle", 0)), clampAngle(pick(p, "upperAngle", 0)));
        angle("lowerAngle", angles.lower);
        angle("upperAngle", angles.upper);
        flag("enableMotor", p.enableMotor);
        number("maxMotorTorque", pick(p, "maxMotorTorque", 0), 0);
        angle("motorSpeed", pick(p, "motorSpeed", 0));
        flag("collideConnected", joint.collideConnected);
        if (differs(a.x, b.x) || differs(a.y, b.y)) {
            fields.unshift("localAnchorB: " + B + ".getLocalPoint(" + at(b) + ")");
            fields.unshift("localAnchorA: " + A + ".getLocalPoint(" + at(a) + ")");
            call = [A, B];
        } else {
            call = [A, B, at(a)];
        }
        return jointCall(name, "RevoluteJoint", fields, call);
    }
    if (type === "prismatic") {
        if (p.enableSpring)
            missing("prismatic spring and target translation", "joint " + joint.name);
        fields.push("localAnchorA: " + A + ".getLocalPoint(" + at(a) + ")");
        fields.push("localAnchorB: " + B + ".getLocalPoint(" + at(b) + ")");
        var ax = localAxis(joint, bodies[joint.bodyA]);
        fields.push("localAxisA: pl.Vec2(" + num(ax.x) + ", " + num(ax.y) + ")");
        angle("referenceAngle", resting + pick(p, "referenceAngle", 0));
        flag("enableLimit", p.enableLimit);
        var span = ordered(pick(p, "lowerTranslation", 0), pick(p, "upperTranslation", 0));
        if (span.lower) fields.push("lowerTranslation: len(" + short(span.lower) + ")");
        if (span.upper) fields.push("upperTranslation: len(" + short(span.upper) + ")");
        flag("enableMotor", p.enableMotor);
        number("maxMotorForce", pick(p, "maxMotorForce", 0), 0);
        if (pick(p, "motorSpeed", 0))
            fields.push("motorSpeed: len(" + short(p.motorSpeed) + ")");
        flag("collideConnected", joint.collideConnected);
        return jointCall(name, "PrismaticJoint", fields, [A, B]);
    }
    if (type === "wheel") {
        if (p.enableLimit)
            missing("wheel joint limit", "joint " + joint.name);
        flag("enableMotor", p.enableMotor);
        number("maxMotorTorque", pick(p, "maxMotorTorque", 0), 0);
        angle("motorSpeed", pick(p, "motorSpeed", 0));
        // No spring is a frequency of zero in Planck.
        var springs = pick(p, "enableSpring", true);
        var hertz = springs ? pick(p, "hertz", 1) : 0;
        number("frequencyHz", hertz, 2);
        number("dampingRatio", springs ? pick(p, "dampingRatio", 0.7) : 0, 0.7);
        flag("collideConnected", joint.collideConnected);
        var axis = joint.axis || { x: 1, y: 0 };
        var lines = jointCall(name, "WheelJoint", fields,
                              [A, B, at(a), "pl.Vec2(" + num(axis.x) + ", " + num(axis.y) + ")"]);
        if (!(hertz > 0)) {
            // Planck 1.0 fills these only when there is a spring and reads them
            // every step regardless; left undefined they make the world NaN.
            lines.push(name + ".m_sAx = 0;");
            lines.push(name + ".m_sBx = 0;");
        }
        return lines;
    }
    if (type === "distance") {
        if (p.enableLimit)
            missing("distance joint limit", "joint " + joint.name);
        if (p.enableMotor)
            missing("distance joint motor", "joint " + joint.name);
        if (pick(p, "length", 0) > 0)
            fields.push("length: len(" + short(p.length) + ")");
        number("frequencyHz", p.enableSpring ? pick(p, "hertz", 0) : 0, 0);
        number("dampingRatio", p.enableSpring ? pick(p, "dampingRatio", 0) : 0, 0);
        flag("collideConnected", joint.collideConnected);
        return jointCall(name, "DistanceJoint", fields, [A, B, at(a), at(b)]);
    }
    if (type === "weld") {
        if (pickNumber(p.linearHertz, 0) && pickNumber(p.angularHertz, 0) && p.linearHertz !== p.angularHertz)
            missing("separate linear and angular weld springs", "joint " + joint.name + " -- the linear one is used");
        angle("referenceAngle", resting + pick(p, "referenceAngle", 0));
        number("frequencyHz", pickNumber(p.linearHertz, 0) || pickNumber(p.angularHertz, 0), 0);
        number("dampingRatio", pickNumber(p.linearDampingRatio, 0) || pickNumber(p.angularDampingRatio, 0), 0);
        flag("collideConnected", joint.collideConnected);
        return jointCall(name, "WeldJoint", fields, [A, B, at(a)]);
    }
    if (type === "motor") {
        var ox = pick(p, "linearOffsetX", 0), oy = pick(p, "linearOffsetY", 0);
        if (ox || oy)
            fields.push("linearOffset: m(" + short(ox) + ", " + short(oy) + ")");
        angle("angularOffset", pick(p, "angularOffset", 0));
        number("maxForce", pick(p, "maxForce", 1), 1);
        number("maxTorque", pick(p, "maxTorque", 1), 1);
        number("correctionFactor", pick(p, "correctionFactor", 0.3), 0.3);
        flag("collideConnected", joint.collideConnected);
        return jointCall(name, "MotorJoint", fields, [A, B]);
    }
    if (type === "mouse") {
        var tx = pick(p, "targetX", 0), ty = pick(p, "targetY", 0);
        number("maxForce", pick(p, "maxForce", 1), 0);
        number("frequencyHz", pick(p, "hertz", 4), 5);
        number("dampingRatio", pick(p, "dampingRatio", 1), 0.7);
        return jointCall(name, "MouseJoint", fields, [A, B, at((tx === 0 && ty === 0) ? a : { x: tx, y: ty })]);
    }
    missing("the filter joint", "joint " + joint.name + " -- it is not created");
    return ["// " + name + ": Planck has no filter joint; not exported."];
}

function jointCall(name, type, fields, args) {
    var tail = (fields.length ? "}, " : "{}, ") + args.join(", ") + "));";
    if (!fields.length)
        return [name + " = world.createJoint(pl." + type + "({}, " + args.join(", ") + "));"];
    return objectCall(name + " = world.createJoint(pl." + type + "(", fields, "").slice(0, -1)
        .concat([tail]);
}

// A prismatic joint's axis, turned into body A's own frame.
function localAxis(joint, bodyA) {
    var ax = joint.axis || { x: 1, y: 0 };
    var length = Math.sqrt(ax.x * ax.x + ax.y * ax.y) || 1;
    var turn = -(bodyA.rotation || 0) * Math.PI / 180;
    return { x: (ax.x * Math.cos(turn) - ax.y * Math.sin(turn)) / length,
             y: (ax.x * Math.sin(turn) + ax.y * Math.cos(turn)) / length };
}

// --- the step: the scene's rules as code -----------------------------------
//
// Planck reports contacts through callbacks while it steps, and changing the
// world from inside one is not allowed, so the callbacks only note what
// happened and step() acts on it afterwards. A condition on a value is
// checked after every step and acts on the step it first becomes true: a
// variable remembers whether it was true last time.

function listenersCode() {
    var lines = [];
    if (STREAMS.begun) {
        lines.push("world.on(\"begin-contact\", function (contact) {");
        lines.push("    contactsBegun.push([contact.getFixtureA(), contact.getFixtureB()]);");
        lines.push("});");
    }
    if (STREAMS.ended) {
        lines.push("world.on(\"end-contact\", function (contact) {");
        lines.push("    contactsEnded.push([contact.getFixtureA(), contact.getFixtureB()]);");
        lines.push("});");
    }
    if (STREAMS.hits) {
        lines = lines.concat([
            "// Box2D 2.4 has no hit events: a hit is a contact closing at speed.",
            "world.on(\"pre-solve\", function (contact) {",
            "    var manifold = contact.getWorldManifold(null);",
            "    var point = manifold && manifold.points[0];",
            "    if (!point)",
            "        return;",
            "    var a = contact.getFixtureA(), b = contact.getFixtureB();",
            "    var closing = -pl.Vec2.dot(pl.Vec2.sub(b.getBody().getLinearVelocityFromWorldPoint(point),",
            "                                           a.getBody().getLinearVelocityFromWorldPoint(point)),",
            "                               manifold.normal);",
            "    if (closing > 0)",
            "        hits.push({ a: a, b: b, speed: closing, point: pl.Vec2.clone(point),",
            "                    normal: pl.Vec2.clone(manifold.normal) });",
            "});",
        ]);
    }
    return lines.length ? (NEWLINE + indent("    ", lines)) : "";
}

function stepBody(scene, io) {
    var rules = scene.rules || [];
    var loops = { begun: [], ended: [], hits: [] };
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
    for (var r = 0; r < rays.length; ++r)
        out.push(rayCast(rays[r], NAMES["ray:" + r]));

    var hitRecordLines = hitRecords();
    if (loops.hits.length || hitRecordLines.length) {
        STREAMS.hits = true;
        remember("var", "hits", "[]");
        out = out.concat(pairLoop("hits", "hits[i].a", "hits[i].b",
                                  hitRecordLines.length ? [{ raw: hitRecordLines }].concat(loops.hits)
                                                        : loops.hits));
    }
    if (loops.begun.length) {
        STREAMS.begun = true;
        remember("var", "contactsBegun", "[]");
        out = out.concat(pairLoop("contactsBegun", "contactsBegun[i][0]", "contactsBegun[i][1]", loops.begun));
    }
    if (loops.ended.length) {
        STREAMS.ended = true;
        remember("var", "contactsEnded", "[]");
        out = out.concat(pairLoop("contactsEnded", "contactsEnded[i][0]", "contactsEnded[i][1]", loops.ended));
    }
    for (var p = 0; p < polls.length; ++p) {
        out.push("");
        out = out.concat(polls[p]);
    }
    for (var c = 0; c < checks.length; ++c) {
        out.push("");
        out = out.concat(checks[c]);
    }
    return out.length ? wrapLong(indent("    ", out)) : "";
}

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
    if (text && TAKEN[text])
        text += "Rule";
    return identifier(text, "rule" + n);
}

function remember(kind, name, initial) {
    STATE.push("var " + name + ";" + NEWLINE);
    RESETS.push("    " + name + " = " + initial + ";" + NEWLINE);
}

function needsOther(rule) { return rule.target === "@other" || rule.target === "@otherBody"; }

function ruleCode(scene, rule, n, caption, loops, polls) {
    var base = ruleVariable(rule, n);
    var once = { test: "", set: [] };
    if (rule.once) {
        var done = base + "Done";
        TAKEN[done] = true;
        remember("var", done, "false");
        once = { test: " && !" + done, set: [done + " = true;"] };
    }
    var event = rule.event;
    var streams = { contactBegin: "begun", sensorBegin: "begun", contactEnd: "ended",
                    sensorEnd: "ended", contactHit: "hits" };

    if (streams[event]) {
        var subject = side(scene, rule.subject);
        if (!subject)
            return { error: "cannot tell which shape " + rule.subject + " is" };
        var partner = rule.when ? side(scene, String(rule.when)) : null;
        if (rule.when && !partner)
            return { error: "cannot tell which shape " + rule.when + " is" };
        var effect = effectLines(scene, rule, "other");
        if (!effect)
            return { error: "nothing in Planck does " + describe(rule).split(", ")[1] };
        var guard = "";
        var sensorEvent = event.indexOf("sensor") === 0;
        var fixtureBoth = subject.fixture && (!partner || partner.fixture);
        if (sensorEvent && !subject.isSensor && !(partner && partner.isSensor))
            guard = "(a.isSensor() || b.isSensor())";
        else if (!sensorEvent && event !== "contactHit"
                 && !(fixtureBoth && !subject.isSensor && !(partner && partner.isSensor)))
            guard = "!a.isSensor() && !b.isSensor()";
        loops[streams[event]].push({ caption: caption, subject: subject, partner: partner,
                                     guard: guard, effect: effect, usesOther: needsOther(rule),
                                     once: once });
        return {};
    }

    var condition, startsTrue = "false", effectOther = null;
    if (event === "bodyMoved" || event === "bodyFellAsleep") {
        var body = resolve(scene, rule.subject);
        if (body && body.kind === "shape")
            body = { kind: "body", handle: body.bodyHandle, index: body.body };
        if (!body || body.kind !== "body")
            return { error: rule.subject + " is not a body" };
        // Planck reports neither, so whether the body is awake is watched.
        var awake = body.handle + "Awake";
        var was = body.handle + "WasAwake";
        if (!TAKEN[was]) {
            TAKEN[was] = true;
            remember("var", was, "false");
            polls.push(["var " + awake + " = " + body.handle + " ? " + body.handle + ".isAwake() : false;"]);
            polls.push([was + " = " + awake + ";"]);
        }
        condition = event === "bodyMoved" ? awake : ("!" + awake + " && " + was);
        // Checked before the line above moves the remembered state along.
        var actions0 = effectLines(scene, rule, null);
        if (!actions0)
            return { error: "nothing in Planck does " + describe(rule).split(", ")[1] };
        var block = edgeBlock(caption, base, condition, actions0, once, "false");
        // Keep the check between reading and remembering.
        polls.splice(polls.length - 1, 0, block);
        return {};
    }
    if (event === "rayDetects") {
        var ray = resolve(scene, rule.subject);
        if (!ray || ray.kind !== "ray")
            return { error: rule.subject + " is not a ray" };
        condition = ray.handle + ".hit";
        if (rule.when) {
            var seen = side(scene, String(rule.when));
            if (!seen)
                return { error: "cannot tell which shape " + rule.when + " is" };
            condition += " && " + seen.test(ray.handle + ".fixture");
        }
        effectOther = ray.handle + ".fixture";
    } else if (event === "limitLower" || event === "limitUpper" || event === "limitEither") {
        var joint = resolve(scene, rule.subject);
        var reader = joint && joint.kind === "joint" ? LIMITS[joint.type] : null;
        if (!reader)
            return { error: rule.subject + " has no limit Planck reports" };
        var J = joint.handle;
        var lower = J + "." + reader + "() <= " + J + ".getLowerLimit() + 0.005";
        var upper = J + "." + reader + "() >= " + J + ".getUpperLimit() - 0.005";
        var at = event === "limitLower" ? lower : event === "limitUpper" ? upper
               : ("(" + lower + " || " + upper + ")");
        condition = J + ".isLimitEnabled() && " + at;
        startsTrue = "true";
    } else if (event) {
        return { error: "Planck has no " + event + " event to read" };
    } else {
        condition = valueCondition(scene, rule);
        if (!condition)
            return { error: "cannot read " + rule.subject + "." + rule.watch };
    }

    var actions = effectLines(scene, rule, effectOther);
    if (!actions)
        return { error: "nothing in Planck does " + describe(rule).split(", ")[1] };
    return { check: edgeBlock(caption, base, condition, actions, once, startsTrue) };
}

var LIMITS = { revolute: "getJointAngle", prismatic: "getJointTranslation" };

function edgeBlock(caption, base, condition, effect, once, startsTrue) {
    var before = base + "Before";
    TAKEN[before] = true;
    remember("var", before, startsTrue);
    var lines = [caption, "var " + base + " = " + condition + ";",
                 "if (" + base + " && !" + before + once.test + ") {"];
    lines = lines.concat(indentLines("    ", effect.concat(once.set)));
    lines.push("}");
    lines.push(before + " = " + base + ";");
    return lines;
}

function pairLoop(array, first, second, watchers) {
    var lines = ["", "for (var i = 0; i < " + array + ".length; ++i) {",
                 "    var a = " + first + ";", "    var b = " + second + ";"];
    for (var i = 0; i < watchers.length; ++i) {
        var w = watchers[i];
        if (w.raw) {
            lines = lines.concat(indentLines("    ", w.raw));
            continue;
        }
        lines.push("");
        lines.push("    " + w.caption);
        var test;
        if (w.partner) {
            test = "(" + w.subject.test("a") + " && " + w.partner.test("b") + ") || ("
                   + w.subject.test("b") + " && " + w.partner.test("a") + ")";
        } else {
            test = w.subject.test("a") + " || " + w.subject.test("b");
        }
        if (w.guard)
            test = w.guard + " && (" + test + ")";
        if (w.once.test)
            test = "(" + test + ")" + w.once.test;
        lines.push("    if (" + test + ") {");
        var body = w.effect.concat(w.once.set);
        if (w.usesOther)
            body = ["var other = " + w.subject.test("a") + " ? b : a;"].concat(body);
        lines = lines.concat(indentLines("        ", body));
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
    if (place.kind === "shape" && !place.outline) {
        return { fixture: true, isSensor: place.isSensor,
                 test: function (v) { return v + " === " + place.handle; } };
    }
    if (place.kind === "shape" || place.kind === "body") {
        var body = place.kind === "shape" ? place.bodyHandle : place.handle;
        return { fixture: false, isSensor: false,
                 test: function (v) { return v + ".getBody() === " + body; } };
    }
    return null;
}

function hitRecords() {
    var lines = [];
    for (var i = 0; HITS && i < HITS.length; ++i) {
        var f = HITS[i].handle;
        lines.push("");
        lines.push("// How hard " + f + " was last hit, and where.");
        lines.push("if (a === " + f + " || b === " + f + ") {");
        lines.push("    " + f + "HitSpeed = hits[i].speed;");
        lines.push("    " + f + "HitPoint = hits[i].point;");
        lines.push("    " + f + "HitNormal = a === " + f + " ? hits[i].normal : pl.Vec2.neg(hits[i].normal);");
        lines.push("}");
    }
    return lines;
}

function rayCast(ray, name) {
    var angle = (ray.angle || 0) * Math.PI / 180;
    var mask = filterBits(parseInt(String(ray.maskBits || "ffff"), 16), 0xFFFF, "ray " + ray.name);
    return name + " = castRay(m(" + short(ray.x) + ", " + short(ray.y) + "), m("
           + short(Math.cos(angle) * (ray.length || 0)) + ", " + short(Math.sin(angle) * (ray.length || 0))
           + ")" + (mask !== 0xFFFF ? (", " + mask) : "") + ");";
}

function rayHelper() {
    return [
        "",
        "// The nearest fixture along a ray, the way a rangefinder sees it.",
        "function castRay(from, translation, maskBits) {",
        "    var result = { hit: false, fraction: 1 };",
        "    world.rayCast(from, pl.Vec2.add(from, translation), function (fixture, point, normal, fraction) {",
        "        if (maskBits !== undefined && (fixture.getFilterCategoryBits() & maskBits) === 0)",
        "            return -1;",
        "        result = { hit: true, point: pl.Vec2.clone(point), fixture: fixture, fraction: fraction };",
        "        return fraction;",
        "    });",
        "    return result;",
        "}",
        "",
        "function drawRay(from, translation, result) {",
        "    var end = result.hit ? result.point : pl.Vec2.add(from, translation);",
        "    ctx.strokeStyle = result.hit ? \"#e86a6a\" : \"#9a9a9a\";",
        "    ctx.beginPath();",
        "    ctx.moveTo(from.x, from.y);",
        "    ctx.lineTo(end.x, end.y);",
        "    ctx.stroke();",
        "}",
    ].join(NEWLINE) + NEWLINE;
}

function rayDrawing(scene) {
    var rays = scene.rays || [];
    var lines = [];
    for (var i = 0; i < rays.length; ++i) {
        var angle = (rays[i].angle || 0) * Math.PI / 180;
        lines.push("drawRay(m(" + short(rays[i].x) + ", " + short(rays[i].y) + "), m("
                   + short(Math.cos(angle) * rays[i].length) + ", "
                   + short(Math.sin(angle) * rays[i].length) + "), " + NAMES["ray:" + i] + ");");
    }
    return lines.length ? indent("    ", lines) : "";
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
            var outline = isOutline(parts[p]);
            if (!outline)
                USED[name] = true;
            return { kind: "shape", handle: NAMES["shape:" + name], outline: outline,
                     shapeKind: parts[p].kind, isSensor: !!parts[p].isSensor,
                     bodyHandle: NAMES["body:" + i], body: i };
        }
    }
    return null;
}

var SHAPE_KEYS = {
    density: 1, friction: 1, restitution: 1, rollingResistance: 1, tangentSpeed: 1,
    categoryBits: 1, maskBits: 1, groupIndex: 1, radius: 1, isSensor: 1, mass: 1,
    enableContactEvents: 1, enableHitEvents: 1, enableSensorEvents: 1,
    lastHitSpeed: 1, lastHitX: 1, lastHitY: 1, lastHitNormalX: 1, lastHitNormalY: 1,
};

function targetOf(scene, rule, other) {
    if (!needsOther(rule))
        return resolve(scene, rule.target);
    if (!other)
        return null;
    if (rule.target === "@otherBody" || (rule.property && !SHAPE_KEYS[rule.property]) || rule.action)
        return { kind: "body", handle: other + ".getBody()", other: true };
    return { kind: "shape", handle: other, bodyHandle: other + ".getBody()", other: true };
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

function worldProperty(key) {
    if (key === "time") {
        if (!COUNTERS.time) {
            COUNTERS.time = true;
            remember("var", "elapsed", "0");
        }
        return prop("seconds", "elapsed", null);
    }
    if (key === "frame") {
        if (!COUNTERS.frame) {
            COUNTERS.frame = true;
            remember("var", "stepCount", "0");
        }
        return prop("int", "stepCount", null);
    }
    if (key === "gravityX")
        return prop("scaled", "world.getGravity().x", function (v) {
            return ["world.setGravity(pl.Vec2(" + v + ", world.getGravity().y));"];
        });
    if (key === "gravityY")
        return prop("scaled", "world.getGravity().y", function (v) {
            return ["world.setGravity(pl.Vec2(world.getGravity().x, " + v + "));"];
        });
    if (key === "enableSleep") return getSet("bool", "world", "getAllowSleeping", "setAllowSleeping");
    if (key === "enableContinuous") return getSet("bool", "world", "getContinuousPhysics", "setContinuousPhysics");
    if (key === "enableWarmStarting") return getSet("bool", "world", "getWarmStarting", "setWarmStarting");
    return null;
}

function getSet(unit, handle, getter, setter, extra) {
    return prop(unit, getter ? (handle + "." + getter + "()") : null, setter ? function (v) {
        return (extra || []).concat([handle + "." + setter + "(" + v + ");"]);
    } : null);
}

function bodyProperty(B, key) {
    var awake = B + ".setAwake(true);";
    switch (key) {
    case "positionX":
        return prop("len", B + ".getPosition().x", function (v) {
            return [B + ".setPosition(pl.Vec2(" + v + ", " + B + ".getPosition().y));", awake];
        });
    case "positionY":
        return prop("len", B + ".getPosition().y", function (v) {
            return [B + ".setPosition(pl.Vec2(" + B + ".getPosition().x, " + v + "));", awake];
        });
    case "angle":
        return prop("angle", B + ".getAngle()", function (v) { return [B + ".setAngle(" + v + ");", awake]; });
    case "velocityX":
        return prop("len", B + ".getLinearVelocity().x", function (v) {
            return [B + ".setLinearVelocity(pl.Vec2(" + v + ", " + B + ".getLinearVelocity().y));", awake];
        });
    case "velocityY":
        return prop("len", B + ".getLinearVelocity().y", function (v) {
            return [B + ".setLinearVelocity(pl.Vec2(" + B + ".getLinearVelocity().x, " + v + "));", awake];
        });
    case "speed": return prop("len", B + ".getLinearVelocity().length()", null);
    case "angularVelocity":
        return prop("angle", B + ".getAngularVelocity()", function (v) {
            return [B + ".setAngularVelocity(" + v + ");", awake];
        });
    case "impulseX":
    case "impulseY":
        return prop("len", null, function (v) {
            return [B + ".applyLinearImpulse(pl.Vec2(" + (key === "impulseX" ? (v + ", 0") : ("0, " + v))
                    + "), " + B + ".getWorldCenter(), true);"];
        });
    case "forceX":
    case "forceY":
        return prop("len", null, function (v) {
            return [B + ".applyForceToCenter(pl.Vec2(" + (key === "forceX" ? (v + ", 0") : ("0, " + v))
                    + "), true);"];
        });
    case "torque": return prop("torque", null, function (v) { return [B + ".applyTorque(" + v + ", true);"]; });
    case "angularImpulse":
        return prop("torque", null, function (v) { return [B + ".applyAngularImpulse(" + v + ", true);"]; });
    case "gravityScale": return getSet("none", B, "getGravityScale", "setGravityScale");
    case "linearDamping": return getSet("none", B, "getLinearDamping", "setLinearDamping");
    case "angularDamping": return getSet("none", B, "getAngularDamping", "setAngularDamping");
    case "mass":
    case "rotationalInertia":
        return prop("none", B + (key === "mass" ? ".getMass()" : ".getInertia()"), function (v) {
            return ["var data = { mass: 0, I: 0, center: pl.Vec2() };", B + ".getMassData(data);",
                    "data." + (key === "mass" ? "mass" : "I") + " = " + v + ";", B + ".setMassData(data);"];
        });
    case "isAwake": return getSet("bool", B, "isAwake", "setAwake");
    case "isEnabled": return getSet("bool", B, "isActive", "setActive");
    case "fixedRotation": return getSet("bool", B, "isFixedRotation", "setFixedRotation");
    case "isBullet": return getSet("bool", B, "isBullet", "setBullet");
    case "enableSleep": return getSet("bool", B, "isSleepingAllowed", "setSleepingAllowed");
    case "bodyType":
        return prop("int", "(" + B + ".isStatic() ? 0 : " + B + ".isKinematic() ? 1 : 2)", function (v) {
            var named = { "0": "\"static\"", "1": "\"kinematic\"", "2": "\"dynamic\"" };
            return [B + ".setType(" + (named[v] || ("[\"static\", \"kinematic\", \"dynamic\"][" + v + "]")) + ");"];
        });
    case "centerOfMassX": return prop("len", B + ".getWorldCenter().x", null);
    case "centerOfMassY": return prop("len", B + ".getWorldCenter().y", null);
    }
    return null;
}

function shapeProperty(place, key) {
    var F = place.handle;
    var awake = [F + ".getBody().setAwake(true);"];
    switch (key) {
    case "density":
        return prop("none", F + ".getDensity()", function (v) {
            return [F + ".setDensity(" + v + ");", F + ".getBody().resetMassData();"];
        });
    case "friction": return getSet("none", F, "getFriction", "setFriction", awake);
    case "restitution": return getSet("none", F, "getRestitution", "setRestitution", awake);
    case "categoryBits": return getSet("bits", F, "getFilterCategoryBits", "setFilterCategoryBits", awake);
    case "maskBits": return getSet("bits", F, "getFilterMaskBits", "setFilterMaskBits", awake);
    case "groupIndex": return getSet("int", F, "getFilterGroupIndex", "setFilterGroupIndex", awake);
    case "isSensor": return getSet("bool", F, "isSensor", "setSensor", awake);
    case "radius":
        return place.shapeKind === "circle" ? prop("len", F + ".getShape().getRadius()", null) : null;
    case "lastHitSpeed": return prop("len", hitVariable(place, "HitSpeed"), null);
    case "lastHitX": return prop("len", hitVariable(place, "HitPoint") + ".x", null);
    case "lastHitY": return prop("len", hitVariable(place, "HitPoint") + ".y", null);
    case "lastHitNormalX": return prop("none", hitVariable(place, "HitNormal") + ".x", null);
    case "lastHitNormalY": return prop("none", hitVariable(place, "HitNormal") + ".y", null);
    }
    return null;
}

function hitVariable(place, what) {
    HITS = HITS || [];
    var known = false;
    for (var i = 0; i < HITS.length; ++i)
        known = known || HITS[i].handle === place.handle;
    if (!known) {
        HITS.push({ handle: place.handle });
        remember("var", place.handle + "HitSpeed", "0");
        remember("var", place.handle + "HitPoint", "pl.Vec2()");
        remember("var", place.handle + "HitNormal", "pl.Vec2()");
    }
    return place.handle + what;
}

function jointProperty(place, key) {
    var J = place.handle;
    var t = place.type;
    var wake = [J + ".getBodyA().setAwake(true);", J + ".getBodyB().setAwake(true);"];
    var simple = function (unit, get, set) { return getSet(unit, J, get, set, wake); };
    // A limit is one call taking both ends.
    var limit = function (unit, end) {
        return prop(unit, J + (end === "lower" ? ".getLowerLimit()" : ".getUpperLimit()"), function (v) {
            var other = J + (end === "lower" ? ".getUpperLimit()" : ".getLowerLimit()");
            return wake.concat([J + ".setLimits(Math.min(" + v + ", " + other + "), Math.max(" + v + ", " + other + "));"]);
        });
    };

    if (key === "collideConnected") return prop("bool", J + ".getCollideConnected()", null);
    if (key === "constraintForce") return prop("none", J + ".getReactionForce(1 / dt).length()", null);
    if (key === "constraintTorque") return prop("none", J + ".getReactionTorque(1 / dt)", null);

    if (t === "revolute" || t === "prismatic" || t === "wheel") {
        switch (key) {
        case "enableMotor": case "motorEnabled": return simple("bool", "isMotorEnabled", "enableMotor");
        case "maxMotorTorque": return t === "prismatic" ? null : simple("none", "getMaxMotorTorque", "setMaxMotorTorque");
        case "motorTorque": return t === "prismatic" ? null : prop("none", J + ".getMotorTorque(1 / dt)", null);
        }
    }
    if (t === "revolute" || t === "prismatic") {
        switch (key) {
        case "enableLimit": case "limitEnabled": return simple("bool", "isLimitEnabled", "enableLimit");
        }
    }
    if (t === "revolute") {
        switch (key) {
        case "angle": return prop("angle", J + ".getJointAngle()", null);
        case "motorSpeed": return simple("angle", "getMotorSpeed", "setMotorSpeed");
        case "lowerAngle": return limit("angle", "lower");
        case "upperAngle": return limit("angle", "upper");
        }
    } else if (t === "prismatic") {
        switch (key) {
        case "translation": return prop("len", J + ".getJointTranslation()", null);
        case "speed": return prop("len", J + ".getJointSpeed()", null);
        case "motorSpeed": return simple("len", "getMotorSpeed", "setMotorSpeed");
        case "motorForce": return prop("none", J + ".getMotorForce(1 / dt)", null);
        case "maxMotorForce": return simple("none", "getMaxMotorForce", "setMaxMotorForce");
        case "lowerTranslation": return limit("len", "lower");
        case "upperTranslation": return limit("len", "upper");
        }
    } else if (t === "wheel") {
        switch (key) {
        case "translation": return prop("len", J + ".getJointTranslation()", null);
        case "motorSpeed": return simple("angle", "getMotorSpeed", "setMotorSpeed");
        case "hertz": return simple("none", "getSpringFrequencyHz", "setSpringFrequencyHz");
        case "dampingRatio": return simple("none", "getSpringDampingRatio", "setSpringDampingRatio");
        }
    } else if (t === "distance") {
        switch (key) {
        case "length": return simple("len", "getLength", "setLength");
        case "hertz": return simple("none", "getFrequency", "setFrequency");
        case "dampingRatio": return simple("none", "getDampingRatio", "setDampingRatio");
        }
    } else if (t === "motor") {
        switch (key) {
        case "maxForce": return simple("none", "getMaxForce", "setMaxForce");
        case "maxTorque": return simple("none", "getMaxTorque", "setMaxTorque");
        case "correctionFactor": return simple("none", "getCorrectionFactor", "setCorrectionFactor");
        case "angularOffset": return simple("angle", "getAngularOffset", "setAngularOffset");
        case "linearOffsetX":
        case "linearOffsetY":
            return prop("len", J + ".getLinearOffset()." + (key === "linearOffsetX" ? "x" : "y"), function (v) {
                var keep = J + ".getLinearOffset()." + (key === "linearOffsetX" ? "y" : "x");
                return wake.concat([J + ".setLinearOffset(pl.Vec2("
                                    + (key === "linearOffsetX" ? (v + ", " + keep) : (keep + ", " + v)) + "));"]);
            });
        }
    } else if (t === "mouse") {
        switch (key) {
        case "maxForce": return simple("none", "getMaxForce", "setMaxForce");
        case "hertz": return simple("none", "getFrequency", "setFrequency");
        case "dampingRatio": return simple("none", "getDampingRatio", "setDampingRatio");
        case "targetX":
        case "targetY":
            return prop("len", J + ".getTarget()." + (key === "targetX" ? "x" : "y"), function (v) {
                var keep = J + ".getTarget()." + (key === "targetX" ? "y" : "x");
                return wake.concat([J + ".setTarget(pl.Vec2("
                                    + (key === "targetX" ? (v + ", " + keep) : (keep + ", " + v)) + "));"]);
            });
        }
    } else if (t === "weld") {
        switch (key) {
        case "linearHertz": case "angularHertz": return simple("none", "getFrequency", "setFrequency");
        case "linearDampingRatio": case "angularDampingRatio":
            return simple("none", "getDampingRatio", "setDampingRatio");
        }
    }
    return null;
}

function rayProperty(place, key) {
    var R = place.handle;
    if (key === "hit") return prop("bool", R + ".hit", null);
    if (key === "distance") return prop("len", R + ".fraction * len(" + short(place.ray.length || 0) + ")", null);
    if (key === "hitX") return prop("len", R + ".point.x", null);
    if (key === "hitY") return prop("len", R + ".point.y", null);
    return null;
}

// --- values ----------------------------------------------------------------

function literal(unit, value) {
    var x = Number(value) || 0;
    if (unit === "bool")   return value === true || value === 1 || value === "true" ? "true" : "false";
    if (unit === "len")    return x === 0 ? "0" : ("len(" + short(x) + ")");
    if (unit === "angle")  return x === 0 ? "0" : ("rad(" + short(x) + ")");
    if (unit === "scaled") return num(x * MOTION);
    if (unit === "torque") return x === 0 ? "0" : ("len(len(" + short(x) + "))");
    if (unit === "int")    return String(Math.round(x));
    if (unit === "bits")   return String(Math.max(0, Math.round(x)) & 0xFFFF);
    return short(x);
}

function intoPlanck(unit, expr) {
    if (unit === "len")    return "len(" + expr + ")";
    if (unit === "angle")  return "rad(" + expr + ")";
    if (unit === "scaled") return "(" + expr + ") * " + num(MOTION);
    if (unit === "torque") return "len(len(" + expr + "))";
    return expr;
}

function outOfPlanck(unit, expr) {
    if (unit === "len")    return "(" + expr + ") * PIXELS_PER_METER";
    if (unit === "angle")  return "(" + expr + ") * 180 / Math.PI";
    if (unit === "scaled") return "(" + expr + ") / " + num(MOTION);
    return expr;
}

function valueCondition(scene, rule) {
    var subject = resolve(scene, rule.subject);
    if (!subject)
        return null;
    var compare = rule.compare || ">";
    if (subject.kind === "ray" && rule.watch === "hitName") {
        var seen = side(scene, String(rule.when));
        if (!seen || (compare !== "=" && compare !== "!="))
            return null;
        var test = subject.handle + ".hit && " + seen.test(subject.handle + ".fixture");
        return compare === "=" ? test : ("!(" + test + ")");
    }
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
        var against = p.unit === "seconds" ? short(Number(rule.when) || 0) : literal(p.unit, rule.when);
        var ops = { "=": "===", "!=": "!==" };
        if (compare === "=")
            result = "Math.abs(" + p.read + " - " + against + ") < 1e-4";
        else if (compare === "!=")
            result = "Math.abs(" + p.read + " - " + against + ") >= 1e-4";
        else
            result = p.read + " " + (ops[compare] || compare) + " " + against;
    }
    return guard ? (guard + " && " + result) : result;
}

// After a rule has destroyed something, the variable that held it is null.
function live(place) {
    if (!LOST || !place || place.other)
        return "";
    if (place.kind === "body" || place.kind === "joint") return place.handle;
    if (place.kind === "shape") return place.bodyHandle;
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
        if (from.unit === p.unit)
            value = from.read + (rule.sourceOffset ? (" + " + literal(p.unit, rule.sourceOffset)) : "");
        else
            value = intoPlanck(p.unit, outOfPlanck(from.unit, from.read)
                               + (rule.sourceOffset ? (" + " + short(rule.sourceOffset)) : ""));
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
            at = body + ".getPosition()";
        var radius = pickNumber(settings.radius, 0);
        if (!at || !(radius > 0))
            return null;
        var falloff = pickNumber(settings.falloff, 0);
        var mask = pickNumber(settings.maskBits, 0);
        // Planck has no world explode: every dynamic body within reach is
        // pushed away from the centre, fading to nothing across the falloff.
        var lines = [
            "var centre = " + at + ";",
            "for (var hit = world.getBodyList(); hit; hit = hit.getNext()) {",
            "    if (!hit.isDynamic())",
            "        continue;",
        ];
        if (mask > 0) {
            lines.push("    if ((hit.getFixtureList().getFilterCategoryBits() & " + (Math.round(mask) & 0xFFFF) + ") === 0)");
            lines.push("        continue;");
        }
        lines = lines.concat([
            "    var away = pl.Vec2.sub(hit.getWorldCenter(), centre);",
            "    var distance = away.length();",
            "    if (distance < 1e-6 || distance > len(" + short(radius + falloff) + "))",
            "        continue;",
            "    var strength = distance <= len(" + short(radius) + ") ? 1 : "
                + (falloff > 0 ? ("1 - (distance - len(" + short(radius) + ")) / len(" + short(falloff) + ")") : "0") + ";",
            "    away.mul(len(" + short(pickNumber(settings.impulse, 0)) + ") * strength / distance);",
            "    hit.applyLinearImpulse(away, hit.getWorldCenter(), true);",
            "}",
        ]);
        return lines;
    }
    if (rule.action === "pushAt") {
        if (target.kind !== "body" && target.kind !== "shape")
            return null;
        return [body + ".applyLinearImpulse(m(" + short(pickNumber(params.impulseX, 0)) + ", "
                + short(pickNumber(params.impulseY, 0)) + "), pl.Vec2.add(" + body + ".getWorldCenter(), m("
                + short(pickNumber(params.offsetX, 0)) + ", " + short(pickNumber(params.offsetY, 0)) + ")), true);"];
    }
    if (rule.action === "removeBody") {
        if (target.kind !== "body" && target.kind !== "shape")
            return null;
        // Destroying a body takes its joints with it; the variables that held
        // them are emptied so nothing below reaches for them.
        var out = [];
        var named = null;
        if (target.other)
            out.push("var doomed = " + body + ";");
        else
            named = body;
        var doomed = named || "doomed";
        var sim = scene.simulation;
        for (var j = 0; j < sim.joints.length; ++j) {
            var jn = NAMES["joint:" + j];
            if (named) {
                var bi = target.kind === "shape" ? target.body : target.index;
                if (sim.joints[j].bodyA === bi || sim.joints[j].bodyB === bi)
                    out.push(jn + " = null;");
            } else {
                out.push("if (" + jn + " && (" + jn + ".getBodyA() === doomed || " + jn + ".getBodyB() === doomed))");
                out.push("    " + jn + " = null;");
            }
        }
        out.unshift("world.destroyBody(" + doomed + ");");
        if (target.other)
            out.unshift("var doomed = " + body + ";");
        if (named) {
            out.push(named + " = null;");
        } else {
            for (var bb = 0; bb < sim.bodies.length; ++bb) {
                out.push("if (" + NAMES["body:" + bb] + " === doomed)");
                out.push("    " + NAMES["body:" + bb] + " = null;");
            }
        }
        // `var doomed` was pushed twice for "whatever touched it"; keep one.
        if (target.other)
            out.splice(out.indexOf("var doomed = " + body + ";", 1), 1);
        return out;
    }
    if (rule.action === "breakJoint") {
        if (target.kind !== "joint")
            return null;
        return [target.handle + ".getBodyA().setAwake(true);", target.handle + ".getBodyB().setAwake(true);",
                "world.destroyJoint(" + target.handle + ");", target.handle + " = null;"];
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

// --- the page around it ----------------------------------------------------

function controlsHtml(own) {
    if (!own.addControls && !own.debugView)
        return "";
    var out = "<div id=\"controls\">" + NEWLINE;
    if (own.addControls) {
        out += "  <button id=\"start\">Start</button>" + NEWLINE
               + "  <button id=\"pause\">Pause</button>" + NEWLINE
               + "  <button id=\"reset\">Reset</button>" + NEWLINE;
    }
    if (own.debugView)
        out += "  <label><input type=\"checkbox\" id=\"debug\"> Debug view</label>" + NEWLINE;
    return out + "</div>" + NEWLINE;
}

function controlsWiring(own) {
    var lines = [];
    if (own.addControls) {
        lines.push("document.getElementById(\"start\").onclick = function () { run(true); };");
        lines.push("document.getElementById(\"pause\").onclick = function () { run(false); };");
        lines.push("document.getElementById(\"reset\").onclick = reset;");
    }
    if (own.debugView) {
        lines.push("document.getElementById(\"debug\").checked = debug;");
        lines.push("document.getElementById(\"debug\").onchange = function () { debug = this.checked; draw(); };");
    }
    return lines.length ? (lines.join(NEWLINE) + NEWLINE) : "";
}

// --- odds and ends ---------------------------------------------------------

function wrapLong(text) {
    var lines = text.split(NEWLINE);
    var out = [];
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        var lead = line.match(/^ */)[0];
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
            line = lead + "    " + line.substring(cut + 1);
        }
        out.push(line);
    }
    return out.join(NEWLINE);
}

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

// Qt writes a colour as #aarrggbb, which no browser understands.
function cssColour(colour, fallback) {
    if (colour === undefined || colour === null || colour === "")
        return fallback;
    var text = String(colour);
    if (text.charAt(0) !== "#")
        return text;
    if (text.length === 9) {
        var a = parseInt(text.substring(1, 3), 16);
        var r = parseInt(text.substring(3, 5), 16);
        var g = parseInt(text.substring(5, 7), 16);
        var b = parseInt(text.substring(7, 9), 16);
        if (!isFinite(a) || !isFinite(r) || !isFinite(g) || !isFinite(b))
            return fallback;
        if (a >= 255)
            return "#" + text.substring(3);
        return "rgba(" + r + ", " + g + ", " + b + ", " + Math.round(a / 255 * 1000) / 1000 + ")";
    }
    return text;
}

// The body colours are drawn at full strength; the fill carries the alpha.
function opaque(colour) {
    var text = String(colour || "");
    return text.length === 9 && text.charAt(0) === "#" ? ("#ff" + text.substring(3)) : colour;
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

// A number as a person types it: 1, 0.6, 0.4905.
function plain(v) { return num(v).replace(/\.0$/, ""); }

function short(v) {
    return num(Math.round(Number(v) * 1000) / 1000).replace(/\.0$/, "");
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
