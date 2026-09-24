// Physalis scene -> one Planck.js web page, index.html.
//
// The page is meant to read like a Planck.js example. createWorld() makes the
// world, then each body, fixture and joint, keeping them in variables named
// after the objects in the editor. step() calls world.step() and then does
// what the scene's rules say, written as ordinary if statements. draw() paints
// every fixture the world holds. Nothing of the editor itself is carried
// across: no scene description to load, no rules as data.
//
// index.html.tmpl is the page around the scene. Every piece of code that goes
// into it -- the world, each body, fixture and joint, the listeners, the
// helpers the rules call -- is a template under templates/objects/ (see
// render() below); this script works out the values that go into them, and
// leaves a def field out when it would only repeat Planck's own default. The
// rules' own code is the one part still written here.
//
// Planck.js is Box2D 2.4, not v3, and several things the editor offers have no
// counterpart in it. Those are reported through io.log() rather than dropped.

var NEWLINE = String.fromCharCode(10);

var T = null;       // template loader
var PPM = 1000;     // scene units per metre
var MOTION = 0.05;  // 50 / PPM -- the scale the editor quotes world speeds at
// The scale the editor sets Box2D's tolerances at, from the world's Contact
// Margin: 2 px by default, a contact seen coming that far off. Tighter, and a
// door slammed onto a ramp sank in before there was a contact to stop it.
var TOLERANCE = 0.1;
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
var EXPLODES = false; // whether the page needs explode()
var PENDING = false;  // whether a rule can stop or hold the run
var HELPERS = null;   // what the step code calls that has to be written out
var ANSWERING = false; // writing a "to be removed" answer, which removes for real
var TRAVEL = null;     // prismatic joint index -> where its travel starts, scene units
var WORLD_SETTINGS = {};

// What the engine says an object has, under the names it publishes. The
// application keeps them in one bag per object rather than as fields of its
// own, because which ones exist is the engine's business -- and this converter
// writes Box2D through Planck, so it reads Box2D's names out of it.
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
    INSET = 2.0 * (2.0 * 0.005 * TOLERANCE) * PPM;
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
        libraryTag = render("objects/library.html.tmpl", { URL: "node_modules/planck/dist/planck.min.js" });
        io.write("package.json", packageJson(project, own));
        io.log("npm: run 'npm install' beside index.html before opening it.");
    } else {
        libraryTag = render("objects/library.html.tmpl", { URL: libraryUrl(own) });
        io.log("index.html loads Planck.js from " + libraryUrl(own) + " -- opening it needs a network.");
    }

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
    COUNTERS = { time: false, frame: false };
    STREAMS = { begun: false, ended: false, hits: false, about: false };
    HITS = null;
    EXPLODES = false;
    PENDING = false;
    HELPERS = { initState: false, clones: {} };
    ANSWERING = false;
    TRAVEL = travelOrigins(scene);
    LOST = destroys(scene);
    WORLD_SETTINGS = world;

    // The step code first: it decides which fixtures have to be kept and which
    // of Planck's callbacks have to be listened to.
    var stepCode = stepBody(scene, io);

    io.write("index.html", page("index.html.tmpl", {
        TITLE: project,
        PIXELS_PER_METER: num(PPM),
        SETTINGS: settingsCode(scene),
        IDS: idDeclarations(scene),
        STATE: STATE.join(NEWLINE),
        RESETS: RESETS.join(NEWLINE),
        WORLD: worldCode(world),
        BODIES: bodiesCode(scene),
        FIELD_BOUNDS: fieldBoundsCode(world, field),
        JOINTS: jointsCode(scene),
        LISTENERS: listenersCode(),
        STEP: stepCode,
        HELPERS: helpersCode(scene),
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
    return page("package.json.tmpl", {
        NAME: JSON.stringify(project.toLowerCase()),
        DEPENDENCY: JSON.stringify(dependency),
    });
}

// Box2D 2.4's tuning lengths assume a world built of metre-sized things. This
// one is not -- at 1000 px/m a drawn 40px box is 0.04m -- so they are scaled
// with the scene.
function settingsCode(scene) {
    return render("objects/settings.js.tmpl", {
        TOLERANCE: num(TOLERANCE),
        MOTION: num(MOTION),
        ROUNDED_BOXES: hasRoundedBox(scene)
            ? render("objects/rounded-boxes.js.tmpl", { VERTICES: String(4 * (CORNER_STEPS + 1)) }) : "",
    });
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

var RESERVED = ("world dt a b i m len rad pl step createWorld draw run reset canvas ctx debug explode nearestPoint pending what initState clone doomed aboutToTouch "
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
    var out = names.length ? [render("objects/ids.js.tmpl", { NAMES: listValue(names) })] : [];
    var rays = scene.rays || [];
    for (var r = 0; r < rays.length; ++r)
        out.push(render("objects/ray-state.js.tmpl", { ID: NAMES["ray:" + r] }));
    return out.join(NEWLINE);
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
    // Planck has no switch for either: warm starting is always on, and it has
    // no speculative contacts at all.
    if (vals(world).enableWarmStarting === false)
        missing("switching warm starting off", "world -- Planck always warm-starts");
    if (vals(world).enableSpeculative === false)
        missing("switching speculative contacts off", "world -- Planck has none");

    // No pre-solve hook holding contacts back until shapes overlap: the polygon
    // inset already keeps shapes drawn flush from jamming, and holding contacts
    // back let a ball sink and bounce back out on every landing, so it never
    // came to rest the way it does in Box2D v3.
    return render("objects/world.js.tmpl", {
        GRAVITY_X: plain(g.x * MOTION),
        GRAVITY_Y: plain(g.y * MOTION),
        ALLOW_SLEEP: vals(world).enableSleep === false ? "false" : null,
        CONTINUOUS: vals(world).enableContinuous === false ? "false" : null,
    });
}

// --- bodies and fixtures ---------------------------------------------------

var GEOMETRY_COUNTS = null;

function bodiesCode(scene) {
    GEOMETRY_COUNTS = {};
    var out = [];
    var bodies = scene.simulation.bodies;
    for (var i = 0; i < bodies.length; ++i)
        out.push(bodyCode(bodies[i], NAMES["body:" + i]));
    return out.join(NEWLINE + NEWLINE);
}

function bodyCode(body, name) {
    var v = vals(body);
    var p = body.position || { x: 0, y: 0 };
    var velocity = { x: pickNumber(v.velocityX, 0), y: pickNumber(v.velocityY, 0) };
    var moving = velocity.x || velocity.y;
    if (v.allowFastRotation)
        missing("allow fast rotation", "body " + body.name);

    var fixtures = [];
    var parts = body.parts || [];
    for (var i = 0; i < parts.length; ++i)
        fixtures.push(fixtureCode(parts[i], body, name));
    return render("objects/body.js.tmpl", {
        ID: name,
        TYPE: body.type === "dynamic" || body.type === "kinematic" ? body.type : null,
        X: p.x || p.y ? short(p.x) : null,
        Y: p.x || p.y ? short(p.y) : null,
        ANGLE: body.rotation ? short(body.rotation) : null,
        // Scene units a second, divided by the scale -- see the same line in
        // the Box2D/Qt converter.
        VELOCITY_X: moving ? num(velocity.x / PPM) : null,
        VELOCITY_Y: moving ? num(velocity.y / PPM) : null,
        ANGULAR_VELOCITY: v.angularVelocity ? short(v.angularVelocity) : null,
        LINEAR_DAMPING: v.linearDamping ? num(v.linearDamping) : null,
        ANGULAR_DAMPING: v.angularDamping ? num(v.angularDamping) : null,
        GRAVITY_SCALE: differs(pickNumber(v.gravityScale, 1), 1) ? num(v.gravityScale) : null,
        ALLOW_SLEEP: v.enableSleep === false ? "false" : null,
        AWAKE: v.isAwake === false ? "false" : null,
        FIXED_ROTATION: v.fixedRotation ? "true" : null,
        BULLET: v.isBullet ? "true" : null,
        ACTIVE: body.isEnabled === false ? "false" : null,
        FIXTURES: fixtures.join(NEWLINE),
    });
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

function fixtureCode(part, body, bodyName) {
    if (isOutline(part) && body.type === "dynamic")
        return render("objects/massless-outline.js.tmpl", { NAME: part.name || "an outline" });
    if (pickNumber(vals(part).rollingResistance, 0) !== 0)
        missing("rolling resistance", "shape " + part.name);
    if (pickNumber(vals(part).tangentSpeed, 0) !== 0)
        missing("surface speed", "shape " + part.name);

    // Planck's own defaults are density 0 and friction 0.2.
    //
    // A shape built inside its outline (see INSET), or a rounded box walked
    // round its corners, is denser by the area it lost, so it weighs what the
    // editor's does and balances where it does: a see-saw pivoted on its centre
    // of mass stays level.
    var density = pickNumber(vals(part).density, 1) * densityScale(part);
    var category = filterBits(vals(part).categoryBits, 1, part.name);
    var mask = filterBits(vals(part).maskBits, 0xFFFF, part.name);
    var def = render("objects/fixture-def.js.tmpl", {
        DENSITY: differs(density, 0) ? plain(density) : null,
        FRICTION: differs(pickNumber(vals(part).friction, 0.6), 0.2) ? plain(pickNumber(vals(part).friction, 0.6)) : null,
        RESTITUTION: vals(part).restitution ? plain(vals(part).restitution) : null,
        SENSOR: vals(part).isSensor ? "true" : null,
        CATEGORY: category !== 1 ? String(category) : null,
        MASK: mask !== 0xFFFF ? String(mask) : null,
        GROUP: vals(part).groupIndex ? String(vals(part).groupIndex) : null,
    });

    var keep = part.name && USED[part.name] && !isOutline(part)
        ? (NAMES["shape:" + part.name] + " = ") : "";
    var c = part.center || { x: 0, y: 0 };
    var solid = function (shape, note) {
        return render("objects/fixture.js.tmpl", { NOTE: note || null, KEEP: keep, BODY: bodyName, SHAPE: shape, DEF: def });
    };
    var at = function (list) {
        var out = [];
        for (var i = 0; i < list.length; ++i)
            out.push("m(" + short(list[i].x) + ", " + short(list[i].y) + ")");
        return listValue(out);
    };

    if (part.kind === "box" && (part.cornerRadius || 0) > 0) {
        var hw = pullIn(part.halfExtents.x), hh = pullIn(part.halfExtents.y);
        // The corners come in by the inset too, so growing the polygon back
        // out when it is drawn gives the corners the editor drew.
        var radius = Math.max(0, Math.min(part.cornerRadius - INSET, hw, hh));
        return solid(render("objects/polygon-shape.js.tmpl", { POINTS: at(roundedBoxPoints(hw, hh, radius, c, part.rotation)) }),
                     (part.name || "a box") + ", its corners rounded to " + short(part.cornerRadius) + ".");
    }
    if (part.kind === "box") {
        return solid(render("objects/box-shape.js.tmpl", {
            HALF_WIDTH: short(pullIn(part.halfExtents.x)), HALF_HEIGHT: short(pullIn(part.halfExtents.y)),
            X: short(c.x), Y: short(c.y), ANGLE: short(part.rotation || 0),
        }));
    }
    if (part.kind === "circle")
        return solid(render("objects/circle-shape.js.tmpl", { X: short(c.x), Y: short(c.y), RADIUS: short(part.radius) }));

    if (isSolidPolygon(part))
        return solid(render("objects/polygon-shape.js.tmpl", { POINTS: at(pulledInOutline(part.points || [])) }));

    var pts = part.points || [];
    var closed = part.kind === "polygon" || part.closed;
    var name = geometryName("points");
    if (part.kind === "chain" && part.smoothChain && pts.length >= 4) {
        return [render("objects/points.js.tmpl", { NAME: name, POINTS: at(pts) }),
                render("objects/chain.js.tmpl", { BODY: bodyName, POINTS: name, LOOP: bool(closed), DEF: def })]
            .join(NEWLINE);
    }
    var edges = closed ? pts.length : pts.length - 1;
    return [render("objects/points.js.tmpl", { NAME: name, POINTS: at(closed ? pts.concat([pts[0]]) : pts) }),
            render("objects/edges.js.tmpl", { BODY: bodyName, POINTS: name, COUNT: String(edges), DEF: def })]
        .join(NEWLINE);
}

function geometryName(kind) {
    GEOMETRY_COUNTS[kind] = (GEOMETRY_COUNTS[kind] || 0) + 1;
    return GEOMETRY_COUNTS[kind] === 1 ? kind : (kind + GEOMETRY_COUNTS[kind]);
}

// How much denser a shape has to be to weigh what the editor's engine gives it:
// a box -- rounded or not -- by its full rectangle, a polygon by its outline.
function densityScale(part) {
    var built = 0, drawn = 0;
    if (part.kind === "box") {
        var hw = part.halfExtents.x, hh = part.halfExtents.y;
        drawn = 4 * hw * hh;
        if ((part.cornerRadius || 0) > 0) {
            var phw = pullIn(hw), phh = pullIn(hh);
            var r = Math.max(0, Math.min(part.cornerRadius - INSET, phw, phh));
            built = Math.abs(polygonArea(roundedBoxPoints(phw, phh, r, { x: 0, y: 0 }, 0)));
        } else {
            built = 4 * pullIn(hw) * pullIn(hh);
        }
    } else if (isSolidPolygon(part)) {
        drawn = Math.abs(polygonArea(part.points));
        built = Math.abs(polygonArea(pulledInOutline(part.points)));
    }
    return built > 0 && drawn > 0 ? drawn / built : 1;
}

function polygonArea(pts) {
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
    return render("objects/walls.js.tmpl", {
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
    for (var j = 0; j < joints.length; ++j) {
        var code = jointCode(scene, joints[j], NAMES["joint:" + j], j);
        if (code)
            out.push(code);
    }
    return out.join(NEWLINE + NEWLINE);
}

function jointCode(scene, joint, name, index) {
    var p = joint.params || {};
    var bodies = scene.simulation.bodies;
    var unsupported = function (why) {
        return render("objects/unsupported-joint.js.tmpl", { NAME: name, WHY: why });
    };
    if (joint.bodyA < 0 || joint.bodyB < 0)
        return unsupported("holds one body to a point in the world; not exported.");
    var A = NAMES["body:" + joint.bodyA], B = NAMES["body:" + joint.bodyB];
    var anchors = joint.anchors || [];
    var a = anchors.length > 0 ? anchors[0] : { x: 0, y: 0 };
    var b = anchors.length > 1 ? anchors[1] : a;
    var type = joint.type;
    var resting = (bodies[joint.bodyB].rotation || 0) - (bodies[joint.bodyA].rotation || 0);
    // A field is left out when it would only say Planck's own default.
    var angle = function (value) { return value ? short(value) : null; };
    var number = function (value, fallback) { return differs(Number(value) || 0, fallback) ? plain(value) : null; };
    var flag = function (value) { return value ? "true" : null; };
    var values = {
        ID: name, BODY_A: A, BODY_B: B, COLLIDE_CONNECTED: flag(joint.collideConnected),
        ANCHOR_A_X: short(a.x), ANCHOR_A_Y: short(a.y), ANCHOR_B_X: short(b.x), ANCHOR_B_Y: short(b.y),
        ANCHOR_X: short(a.x), ANCHOR_Y: short(a.y),
    };
    var build = function (template) { return render("objects/" + template, values); };

    if (has(p, "constraintHertz") || has(p, "constraintDampingRatio"))
        missing("per-joint constraint tuning", "joint " + joint.name);

    if (type === "revolute") {
        if (p.enableSpring)
            missing("revolute spring", "joint " + joint.name);
        var angles = ordered(clampAngle(pick(p, "lowerAngle", 0)), clampAngle(pick(p, "upperAngle", 0)));
        values.REFERENCE_ANGLE = angle(resting + pick(p, "referenceAngle", 0));
        values.ENABLE_LIMIT = flag(p.enableLimit);
        values.LOWER_ANGLE = angle(angles.lower);
        values.UPPER_ANGLE = angle(angles.upper);
        values.ENABLE_MOTOR = flag(p.enableMotor);
        values.MAX_MOTOR_TORQUE = number(pick(p, "maxMotorTorque", 0), 0);
        values.MOTOR_SPEED = angle(pick(p, "motorSpeed", 0));
        return build("revolute.js.tmpl");
    }
    if (type === "prismatic") {
        if (p.enableSpring)
            missing("prismatic spring and target translation", "joint " + joint.name);
        var ax = localAxis(joint, bodies[joint.bodyA]);
        // Box2D measures the travel between the anchors, so a joint whose anchors
        // start apart starts that far along; the editor's limits count from there.
        var origin = TRAVEL[index] || 0;
        var span = ordered(pick(p, "lowerTranslation", 0), pick(p, "upperTranslation", 0));
        values.AXIS_X = num(ax.x);
        values.AXIS_Y = num(ax.y);
        values.REFERENCE_ANGLE = angle(resting + pick(p, "referenceAngle", 0));
        values.ENABLE_LIMIT = flag(p.enableLimit);
        values.LOWER_TRANSLATION = span.lower + origin ? short(span.lower + origin) : null;
        values.UPPER_TRANSLATION = span.upper + origin ? short(span.upper + origin) : null;
        values.ENABLE_MOTOR = flag(p.enableMotor);
        values.MAX_MOTOR_FORCE = number(pick(p, "maxMotorForce", 0), 0);
        values.MOTOR_SPEED = pick(p, "motorSpeed", 0) ? short(p.motorSpeed) : null;
        return build("prismatic.js.tmpl");
    }
    if (type === "wheel") {
        if (p.enableLimit)
            missing("wheel joint limit", "joint " + joint.name);
        // No spring is a frequency of zero in Planck.
        var springs = pick(p, "enableSpring", true);
        var hertz = springs ? pick(p, "hertz", 1) : 0;
        var axis = joint.axis || { x: 1, y: 0 };
        values.ENABLE_MOTOR = flag(p.enableMotor);
        values.MAX_MOTOR_TORQUE = number(pick(p, "maxMotorTorque", 0), 0);
        values.MOTOR_SPEED = angle(pick(p, "motorSpeed", 0));
        values.FREQUENCY = number(hertz, 2);
        values.DAMPING_RATIO = number(springs ? pick(p, "dampingRatio", 0.7) : 0, 0.7);
        values.AXIS_X = num(axis.x);
        values.AXIS_Y = num(axis.y);
        values.NO_SPRING = hertz > 0 ? null : name;
        return build("wheel.js.tmpl");
    }
    if (type === "distance") {
        if (p.enableLimit)
            missing("distance joint limit", "joint " + joint.name);
        if (p.enableMotor)
            missing("distance joint motor", "joint " + joint.name);
        values.LENGTH = pick(p, "length", 0) > 0 ? short(p.length) : null;
        values.FREQUENCY = number(p.enableSpring ? pick(p, "hertz", 0) : 0, 0);
        values.DAMPING_RATIO = number(p.enableSpring ? pick(p, "dampingRatio", 0) : 0, 0);
        return build("distance.js.tmpl");
    }
    if (type === "weld") {
        if (pickNumber(p.linearHertz, 0) && pickNumber(p.angularHertz, 0) && p.linearHertz !== p.angularHertz)
            missing("separate linear and angular weld springs", "joint " + joint.name + " -- the linear one is used");
        values.REFERENCE_ANGLE = angle(resting + pick(p, "referenceAngle", 0));
        values.FREQUENCY = number(pickNumber(p.linearHertz, 0) || pickNumber(p.angularHertz, 0), 0);
        values.DAMPING_RATIO = number(pickNumber(p.linearDampingRatio, 0) || pickNumber(p.angularDampingRatio, 0), 0);
        return build("weld.js.tmpl");
    }
    if (type === "motor") {
        var ox = pick(p, "linearOffsetX", 0), oy = pick(p, "linearOffsetY", 0);
        values.OFFSET_X = ox || oy ? short(ox) : null;
        values.OFFSET_Y = ox || oy ? short(oy) : null;
        values.ANGULAR_OFFSET = angle(pick(p, "angularOffset", 0));
        values.MAX_FORCE = number(pick(p, "maxForce", 1), 1);
        values.MAX_TORQUE = number(pick(p, "maxTorque", 1), 1);
        values.CORRECTION_FACTOR = number(stepCorrection(pick(p, "correctionFactor", 0.3)), 0.3);
        return build("motor.js.tmpl");
    }
    if (type === "mouse") {
        var tx = pick(p, "targetX", 0), ty = pick(p, "targetY", 0);
        var target = (tx === 0 && ty === 0) ? a : { x: tx, y: ty };
        values.MAX_FORCE = number(pick(p, "maxForce", 1), 0);
        values.FREQUENCY = number(pick(p, "hertz", 4), 5);
        values.DAMPING_RATIO = number(pick(p, "dampingRatio", 1), 0.7);
        values.TARGET_X = short(target.x);
        values.TARGET_Y = short(target.y);
        return build("mouse.js.tmpl");
    }
    missing("the filter joint", "joint " + joint.name + " -- it is not created");
    return unsupported("Planck has no filter joint; not exported.");
}

// Where each prismatic joint's travel starts: the gap between its anchors along
// its axis, in scene units -- what the editor's own engine adds to its limits.
function travelOrigins(scene) {
    var out = {};
    var joints = scene.simulation.joints;
    for (var j = 0; j < joints.length; ++j) {
        if (joints[j].type !== "prismatic")
            continue;
        var anchors = joints[j].anchors || [];
        var a = anchors[0] || { x: 0, y: 0 }, b = anchors[1] || a;
        var ax = joints[j].axis || { x: 1, y: 0 };
        var length = Math.sqrt(ax.x * ax.x + ax.y * ax.y) || 1;
        out[j] = ((b.x - a.x) * ax.x + (b.y - a.y) * ax.y) / length;
    }
    return out;
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
    var out = [];
    if (STREAMS.begun)
        out.push(render("objects/begin-contact.js.tmpl", {}));
    if (STREAMS.ended)
        out.push(render("objects/end-contact.js.tmpl", {}));
    if (STREAMS.about)
        out.push(render("objects/pre-solve.js.tmpl", {}));
    if (STREAMS.hits)
        out.push(render("objects/hits.js.tmpl", {}));
    return out.join(NEWLINE);
}

function stepBody(scene, io) {
    var rules = scene.rules || [];
    var loops = { begun: [], ended: [], hits: [], about: [] };
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
    if (loops.about.length) {
        STREAMS.about = true;
        remember("var", "aboutToTouch", "[]");
        out = out.concat(pairLoop("aboutToTouch", "aboutToTouch[i][0]", "aboutToTouch[i][1]", loops.about));
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
    if (PENDING)
        out = out.concat([""]).concat(render("objects/pending.js.tmpl", {}).split(NEWLINE));
    return wrapLong(out.join(NEWLINE), 88);
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

function remember(kind, name, initial) {
    STATE.push(render("objects/state.js.tmpl", { NAME: name }));
    RESETS.push(render("objects/reset.js.tmpl", { NAME: name, VALUE: initial }));
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

function ruleCode(scene, rule, n, caption, loops, polls) {
    var base = ruleVariable(rule, n);
    var once = { test: "", set: [] };
    if (rule.once) {
        var done = base + "Done";
        TAKEN[done] = true;
        remember("var", done, "false");
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
    var streams = { contactBegin: "begun", sensorBegin: "begun", contactEnd: "ended",
                    sensorEnd: "ended", contactHit: "hits", preSolve: "about" };

    // Carried out where a removal is written: see actionLines.
    if (event === "@aboutToBeRemoved")
        return {};

    if (streams[event]) {
        var subject = side(scene, primary.subject);
        if (!subject)
            return { error: "cannot tell which shape " + primary.subject + " is" };
        var partner = primary.when ? side(scene, String(primary.when)) : null;
        if (primary.when && !partner)
            return { error: "cannot tell which shape " + primary.when + " is" };
        var effect = guarded(allEffectLines(scene, rule, "other"), guard);
        if (!effect)
            return { error: "nothing in Planck does " + describe(rule).split(", ")[1] };
        var guard = "";
        var sensorEvent = event.indexOf("sensor") === 0;
        var fixtureBoth = subject.fixture && (!partner || partner.fixture);
        if (sensorEvent && !subject.isSensor && !(partner && partner.isSensor))
            guard = "(a.isSensor() || b.isSensor())";
        else if (!sensorEvent && event !== "contactHit" && event !== "preSolve"
                 && !(fixtureBoth && !subject.isSensor && !(partner && partner.isSensor)))
            guard = "!a.isSensor() && !b.isSensor()";
        loops[streams[event]].push({ caption: caption, subject: subject, partner: partner,
                                     guard: guard, effect: effect, usesOther: ruleNeedsOther(rule),
                                     once: once });
        return {};
    }

    var condition, startsTrue = "false", effectOther = null;
    if (event === "@runStarted") {
        // Once, after the first step.
        condition = "true";
        var started = guarded(allEffectLines(scene, rule, null), guard);
        if (!started)
            return { error: "nothing in Planck does " + describe(rule).split(", ")[1] };
        return { check: edgeBlock(caption, base, condition, started, once, startsTrue) };
    }
    if (event === "bodyMoved" || event === "bodyFellAsleep") {
        var body = resolve(scene, primary.subject);
        if (body && body.kind === "shape")
            body = { kind: "body", handle: body.bodyHandle, index: body.body };
        if (!body || body.kind !== "body")
            return { error: primary.subject + " is not a body" };
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
        var actions0 = guarded(allEffectLines(scene, rule, null), guard);
        if (!actions0)
            return { error: "nothing in Planck does " + describe(rule).split(", ")[1] };
        var block = edgeBlock(caption, base, condition, actions0, once, "false");
        // Keep the check between reading and remembering.
        polls.splice(polls.length - 1, 0, block);
        return {};
    }
    if (event === "rayDetects") {
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
        var reader = joint && joint.kind === "joint" ? LIMITS[joint.type] : null;
        if (!reader)
            return { error: primary.subject + " has no limit Planck reports" };
        var J = joint.handle;
        var lower = J + "." + reader + "() <= " + J + ".getLowerLimit() + 0.005";
        var upper = J + "." + reader + "() >= " + J + ".getUpperLimit() - 0.005";
        var at = event === "limitLower" ? lower : event === "limitUpper" ? upper
               : ("(" + lower + " || " + upper + ")");
        condition = J + ".isLimitEnabled() && " + at;
        // A joint a rule can break is gone once it has been.
        if (LOST)
            condition = J + " && " + condition;
        startsTrue = "true";
    } else if (event) {
        return { error: "Planck has no " + event + " event to read" };
    } else {
        condition = valueConditions(scene, shape.values, shape.join);
        if (!condition)
            return { error: "one of its readings cannot be read here" };
    }

    var actions = guarded(allEffectLines(scene, rule, effectOther), guard);
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
        lines.push("");
        lines = lines.concat(render("objects/hit-record.js.tmpl", { ID: HITS[i].handle }).split(NEWLINE));
    }
    return lines;
}

// A ray and where it points, in scene units.
function rayGeometry(ray) {
    var angle = (ray.angle || 0) * Math.PI / 180;
    return { X: short(ray.x), Y: short(ray.y),
             DX: short(Math.cos(angle) * (ray.length || 0)), DY: short(Math.sin(angle) * (ray.length || 0)) };
}

function rayCast(ray, name) {
    var mask = filterBits(parseInt(String(ray.maskBits || "ffff"), 16), 0xFFFF, "ray " + ray.name);
    var values = rayGeometry(ray);
    values.ID = name;
    values.MASK = mask !== 0xFFFF ? String(mask) : "undefined";
    return render("objects/ray-cast.js.tmpl", values);
}

function rayDrawing(scene) {
    var rays = scene.rays || [];
    var out = [];
    for (var i = 0; i < rays.length; ++i) {
        var values = rayGeometry(rays[i]);
        values.ID = NAMES["ray:" + i];
        out.push(render("objects/ray-draw.js.tmpl", values));
    }
    return out.join(NEWLINE);
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
        // Travel counted from where the joint starts, as the editor counts it.
        var origin = "len(" + short(TRAVEL[place.index] || 0) + ")";
        var travel = function (end) {
            var getter = J + (end === "lower" ? ".getLowerLimit()" : ".getUpperLimit()");
            var other = J + (end === "lower" ? ".getUpperLimit()" : ".getLowerLimit()");
            return prop("len", "(" + getter + " - " + origin + ")", function (v) {
                var moved = "(" + v + ") + " + origin;
                return wake.concat([J + ".setLimits(Math.min(" + moved + ", " + other + "), Math.max("
                                    + moved + ", " + other + "));"]);
            });
        };
        switch (key) {
        case "translation": return prop("len", "(" + J + ".getJointTranslation() - " + origin + ")", null);
        case "speed": return prop("len", J + ".getJointSpeed()", null);
        case "motorSpeed": return simple("len", "getMotorSpeed", "setMotorSpeed");
        case "motorForce": return prop("none", J + ".getMotorForce(1 / dt)", null);
        case "maxMotorForce": return simple("none", "getMaxMotorForce", "setMaxMotorForce");
        case "lowerTranslation": return travel("lower");
        case "upperTranslation": return travel("upper");
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
        case "correctionFactor":
            // In the editor's terms: per sub-step, as Box2D v3 takes it.
            return prop("none", "(1 - Math.pow(1 - " + J + ".getCorrectionFactor(), 1 / " + SUB_STEPS + "))",
                        function (v) {
                return wake.concat([J + ".setCorrectionFactor(1 - Math.pow(1 - Math.max(0, Math.min(1, " + v
                                    + ")), " + SUB_STEPS + "));"]);
            });
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
        remember("var", CHANGE_STARTED, "false");
        CHANGE_SAVES.push(CHANGE_STARTED + " = true;");
    }
    var n = ++CHANGES;
    var now = "nowEq" + n;
    var was = "wasEq" + n;
    remember("var", was, "false");
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
        if (compare === "%") {
            // In the editor's units, as the rule was written: whole numbers,
            // a non-zero multiple.
            var step = Math.round(Number(rule.when) || 0);
            if (step === 0)
                return "false";
            var whole = "Math.round(" + outOfPlanck(p.unit, p.read) + ")";
            result = "(" + whole + " !== 0 && " + whole + " % " + step + " === 0)";
        } else if (compare === "=")
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
    var isBody = target.kind === "body" || target.kind === "shape";
    var who = target.kind === "shape" ? target.bodyHandle : target.handle;

    if (rule.action === "@initState") {
        if (!isBody)
            return null;
        HELPERS.initState = true;
        return ["initState(" + who + ");"];
    }
    if (rule.action === "@clone") {
        // A copy of a body named in the rule; one met through "the other" is
        // not known until the run, and there is nothing to copy it from.
        var index = target.kind === "shape" ? target.body : target.index;
        if (!isBody || index === undefined)
            return null;
        HELPERS.clones[index] = true;
        return ["cloneOf_" + NAMES["body:" + index] + "(m(" + short(pickNumber(params.x, 0)) + ", "
                + short(pickNumber(params.y, 0)) + "));"];
    }
    if (rule.action === "pushForceAt") {
        if (!isBody)
            return null;
        return [who + ".applyForce(m(" + short(pickNumber(params.impulseX, 0)) + ", "
                + short(pickNumber(params.impulseY, 0)) + "), pl.Vec2.add(" + who + ".getWorldCenter(), m("
                + short(pickNumber(params.offsetX, 0)) + ", " + short(pickNumber(params.offsetY, 0)) + ")), true);"];
    }
    if (rule.action === "resetMass")
        return isBody ? [who + ".resetMassData();"] : null;

    // Ending or holding the run cannot happen inside the step, with the rules
    // still going through the world; step() carries it out once they are done.
    if (rule.action === "@stopRun" || rule.action === "@holdRun") {
        if (!PENDING) {
            PENDING = true;
            remember("var", "pending", "null");
        }
        return ["pending = \"" + (rule.action === "@stopRun" ? "stop" : "hold") + "\";"];
    }
    var body = target.kind === "shape" ? target.bodyHandle : target.handle;

    if (rule.action === "explode") {
        var settings = {}, key;
        for (key in params) {
            if (has(params, key))
                settings[key] = params[key];
        }
        var at = null;
        if (target.kind === "explosion") {
            at = "m(" + short(target.explosion.x) + ", " + short(target.explosion.y) + ")";
            // The explosion's own settings win over the rule's, as in the editor.
            var own = target.explosion.params || {};
            for (key in own) {
                if (has(own, key))
                    settings[key] = own[key];
            }
        } else if (target.kind === "body" || target.kind === "shape") {
            at = body + ".getPosition()";
        }
        var radius = pickNumber(settings.radius, 0);
        if (!at || !(radius > 0))
            return null;
        EXPLODES = true;
        var mask = pickNumber(settings.maskBits, 0);
        return ["explode(" + at + ", len(" + short(pickNumber(settings.impulse, 0)) + "), len(" + short(radius)
                + "), len(" + short(pickNumber(settings.falloff, 0)) + ")"
                + (mask > 0 ? (", " + (Math.round(mask) & 0xFFFF)) : "") + ");"];
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
        if (!ANSWERING) {
            var answered = removal(scene, rule, target, function () {
                ANSWERING = true;
                var real = actionLines(scene, rule, target);
                ANSWERING = false;
                return real;
            });
            if (answered)
                return answered;
        }
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

// The answer in place of the removal, or null when nothing answers and the
// body is simply removed.
function removal(scene, rule, target, remove) {
    var answers = answersByBody(scene);
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
    var index = target.kind === "shape" ? target.body : target.index;
    if (index !== undefined)
        return answers[index] ? answer(answers[index]) : null;
    var keys = Object.keys(answers);
    if (!keys.length)
        return null;
    // Met through "the other": which body it is is only known during the run.
    var lines = ["var doomed = " + target.handle + ";"];
    for (var k = 0; k < keys.length; ++k) {
        lines.push((k ? "} else if (" : "if (") + "doomed === " + NAMES["body:" + keys[k]] + ") {");
        lines = lines.concat(indentLines("    ", answer(answers[keys[k]])));
    }
    lines.push("} else {");
    lines = lines.concat(indentLines("    ", remove()));
    lines.push("}");
    return lines;
}

// --- helpers the step code calls -------------------------------------------

function helpersCode(scene) {
    var out = [];
    var bodies = scene.simulation.bodies;
    if ((scene.rays || []).length)
        out.push(render("objects/cast-ray.js.tmpl", {}));
    // Box2D v3's explosion, which the editor runs, fixture by fixture.
    if (EXPLODES)
        out.push(render("objects/explode.js.tmpl", {}));
    if (HELPERS.initState) {
        var starts = [];
        for (var b = 0; b < bodies.length; ++b) {
            var p = bodies[b].position || { x: 0, y: 0 };
            starts.push("[" + NAMES["body:" + b] + ", " + short(p.x) + ", " + short(p.y) + ", "
                        + short(bodies[b].rotation || 0) + "]");
        }
        out.push(render("objects/init-state.js.tmpl", { STARTS: listValue(starts) }));
    }
    for (var index in HELPERS.clones) {
        if (!has(HELPERS.clones, index))
            continue;
        // Built exactly as the original is, keeping no fixture handles of its own.
        var kept = USED;
        USED = {};
        var made = bodyCode(bodies[index], "clone");
        USED = kept;
        out.push(render("objects/clone.js.tmpl", { NAME: bodies[index].name, ID: NAMES["body:" + index], BODY: made }));
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

// --- the page around it ----------------------------------------------------

function controlsHtml(own) {
    if (!own.addControls && !own.debugView)
        return "";
    return render("objects/controls.html.tmpl", {
        BUTTONS: own.addControls ? render("objects/buttons.html.tmpl", {}) : "",
        DEBUG_SWITCH: own.debugView ? render("objects/debug-switch.html.tmpl", {}) : "",
    });
}

function controlsWiring(own) {
    var out = [];
    if (own.addControls)
        out.push(render("objects/buttons-wiring.js.tmpl", {}));
    if (own.debugView)
        out.push(render("objects/debug-wiring.js.tmpl", {}));
    return out.join(NEWLINE);
}

// --- odds and ends ---------------------------------------------------------

function wrapLong(text, width) {
    var lines = text.split(NEWLINE);
    var out = [];
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        var lead = line.match(/^ */)[0];
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
            line = lead + "    " + line.substring(cut + 1);
        }
        out.push(line);
    }
    return out.join(NEWLINE);
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

// --- templates ---------------------------------------------------------------
//
// Every piece of code this converter writes is a template under templates/:
// the page itself, and one file per kind of object under objects/. render()
// fills one:
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
