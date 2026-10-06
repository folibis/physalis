// Physalis scene -> one self-contained web page: index.html.
//
// The engine is box2d3wasm -- the same Box2D the application runs, compiled to
// WebAssembly -- with the library and its wasm built into the page, so it opens
// straight from disk with no web server and no network. Every b2* call below is
// the one the C++ converter makes, spelled for JavaScript: this converter is a
// transliteration of that one, which is why the two agree step for step.
//
// page.html.tmpl is the page around the scene. Every piece of code that goes
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
var PPM = 100;      // scene units to the page's metre -- chosen, see below
var SCENE_PPM = 1000; // scene units to the editor's metre
var SCALE = 10;     // SCENE_PPM / PPM: how much bigger a length is on the page
var MOTION = 0.5;   // 50 / PPM -- the scale the editor quotes world speeds at
var TOLERANCE = 1.0; // Box2D's length unit, which this target cannot be told

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

// Which of a joint type's parameters the engine tagged as saying it is a
// spring, and what it rests at. Found by role, so no key is named here.
function springKeysByType(scene) {
    var byType = {};
    var types = (scene.engine && scene.engine.jointTypes) || [];
    for (var t = 0; t < types.length; ++t) {
        var keys = { enabled: null, enabledDefault: false,
                     stiffness: null, stiffnessDefault: 0,
                     rest: null, restDefault: 0 };
        var params = types[t].params || [];
        for (var p = 0; p < params.length; ++p) {
            var param = params[p];
            if (param.role === "springEnabled") {
                keys.enabled = param.key;
                keys.enabledDefault = !!param["default"];
            } else if (param.role === "springStiffness" && !keys.stiffness) {
                keys.stiffness = param.key;
                keys.stiffnessDefault = Number(param["default"]) || 0;
            } else if (param.role === "springRestLength") {
                keys.rest = param.key;
                keys.restDefault = Number(param["default"]) || 0;
            }
        }
        byType[types[t].id] = keys;
    }
    return byType;
}

// How many turns a joint is drawn with: its rest length over the pitch one
// turn stands for, or zero where it is not a spring at all. Measured from the
// anchors where the engine lets a rest length of zero mean "wherever they
// start", which is what the editor does too.
function springTurnsOf(joint, keys, pitch) {
    if (!keys)
        return 0;
    var params = joint.params || {};
    if (keys.enabled) {
        var on = params[keys.enabled];
        if (!(on === undefined ? keys.enabledDefault : on))
            return 0;
    } else if (keys.stiffness) {
        var k = params[keys.stiffness];
        if (!((k === undefined ? keys.stiffnessDefault : Number(k)) > 0))
            return 0;
    } else {
        return 0;
    }

    var rest = 0;
    if (keys.rest)
        rest = Number(params[keys.rest] === undefined ? keys.restDefault : params[keys.rest]) || 0;
    if (rest <= 0 && joint.anchorA && joint.anchorB) {
        var dx = joint.anchorB.x - joint.anchorA.x, dy = joint.anchorB.y - joint.anchorA.y;
        rest = Math.sqrt(dx * dx + dy * dy);
    }
    return Math.max(2, Math.min(60, Math.round(rest / Math.max(pitch, 1))));
}

// Whether this scene quotes its speeds at the engine's reference scale instead
// of at its own. Found by role, so no key is named here.
function keepsPaceAcrossScales(scene) {
    var props = (scene.engine && scene.engine.worldProperties) || [];
    var settings = vals(scene.world || {});
    for (var i = 0; i < props.length; ++i) {
        if (props[i].role === "paceAcrossScales") {
            var set = settings[props[i].key];
            return !!(set === undefined ? props[i]["default"] : set);
        }
    }
    return false;
}

function exportScene(scene, io) {
    var world = scene.world || {};
    var field = scene.field || {};
    var own = scene.converterSettings || {};
    var physics = (scene.settings && scene.settings.Physics) || {};

    SCENE_PPM = world.pixelsPerMeter || 1000;
    // Box2D's tolerances are lengths fixed in metres: 5 mm of slop, a contact
    // made at four times that. The editor shrinks them with
    // b2SetLengthUnitsPerMeter, which box2d3wasm does not expose -- so the page
    // chooses its metre instead, the one at which that fixed 2 cm is exactly the
    // contact margin the scene asked for. At the usual 2 units that is 100 scene
    // units to the metre rather than 1000.
    //
    // Everything else follows from dividing by that number instead: lengths,
    // velocities and gravity all come out as the same motion in scene units.
    // What does not follow is mass -- an area is a length squared, so every body
    // would weigh SCALE^2 times what it does in the editor, and a motor torque
    // would no longer turn a wheel at the same rate. Dividing density by the
    // same square puts every mass back exactly, and then forces and torques,
    // which are already divided by the scale once and twice, land where the
    // editor puts them too.
    var margin = Math.max(0.1, pickNumber(vals(world).contactMargin, 2));
    PPM = margin / (4.0 * 0.005);
    SCALE = SCENE_PPM / PPM;
    // The page measures in its own metre, so a speed has to be converted into
    // it. Where the scene quotes speeds at 50 px per metre that is 50 / PPM;
    // where it means plain metres per second it is simply how much bigger a
    // length is on the page.
    MOTION = keepsPaceAcrossScales(scene) ? 50.0 / PPM : SCALE;
    TOLERANCE = 1.0;

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

    var width = Math.round(field.width || 1000);
    var height = Math.round(field.height || 600);

    var readout = logCode(scene, physics);
    var sling = slingshotCode(scene, physics);
    var engine = engineDelivery(io, own);
    // Built after the rules, which is what decides whether a generator is
    // needed at all; the fill is read at render time either way.
    var luck = randomCode(world);
    // Drawing has to know them too, and it runs over the simulation's bodies
    // where the saved document is what carries the setting.
    SHOOTABLE = {};
    for (var sb = 0; sb < (scene.bodies || []).length; ++sb) {
        var itsShot = scene.bodies[sb].shot;
        if (itsShot && itsShot.enabled)
            SHOOTABLE[scene.bodies[sb].name] = true;
    }
    io.write("index.html", page("page.html.tmpl", {
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
        STATE: toJavaScript(STATE.join(NEWLINE)),
        HELPERS: toJavaScript(helpers),
        RESETS: toJavaScript(RESETS.join(NEWLINE)),
        WORLD: worldCode(world),
        BODIES: bodiesCode(scene, colours),
        FIELD_BOUNDS: fieldBoundsCode(world, field),
        JOINTS: jointsCode(scene),
        SUB_STEPS: int(vals(world).subStepCount, 4),
        STEP: toJavaScript(stepCode),
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
        DRAW_RAY: (scene.rays || []).length ? render("objects/draw-ray.js.tmpl", {}) : "",
        DEBUG_DRAWING: debugDrawing(scene),
        LOG: toJavaScript(readout.code),
        RANDOM: luck,
        SLINGSHOT: toJavaScript(sling.code),
        SHOOTABLE: sling.list,
        DRAW_SHOT: sling.draw,
        SHOT_DOWN: sling.down,
        SHOT_MOVE: sling.move,
        SHOT_UP: sling.up,
        SHOT_CANCEL: sling.cancel,
        DRAW_LOG: readout.call,
        JOINT_COLOR: cssColour(physics.jointColor, "rgba(232, 196, 106, 0.667)"),
        JOINT_ANCHOR_RADIUS: short(pickNumber(physics.jointAnchorRadius, 7)),
        SPRING_WIDTH: short(pickNumber(physics.springWidth, 9)),
        CONTROLS: controlsCode(own),
        CONTROL_WIRING: controlWiring(own),
        WASM_SCRIPT: engine.wasmScript,
        LIBRARY_SCRIPT: engine.libraryScript,
        BOOT: engine.boot,
        BOOT_MODULE: engine.bootModule,
    }));

    // The scale is chosen so Box2D's fixed tolerances land on the scene's contact
    // margin, so there is nothing left to warn about there. What it cannot carry
    // is a world field the bindings do not have at all.
    if (differs(pickNumber(vals(world).contactSpeed, 3), 3)) {
        io.log("This scene sets Contact Speed (contactSpeed) to "
               + short(vals(world).contactSpeed) + ", which box2d3wasm's b2WorldDef does not"
               + " carry under that name or Box2D 3.1's: the page separates overlapping shapes"
               + " at Box2D's own default instead.");
    }

    io.log("Exported " + scene.simulation.bodies.length + " bodies, "
           + scene.simulation.joints.length + " joints and "
           + (scene.rules || []).length + " rules into index.html.");
    return true;
}

// box2d3wasm ships as an ES module, and a module on a page opened from a file
// URL is fetched -- which the browser refuses. These five edits make the very
// same file loadable as a plain script instead: the Node-only branch uses
// top-level await, and import.meta exists only in a module. Nothing else is
// touched, so the copy under vendor/ stays comparable with upstream.
// Where the page gets Box2D, which is the converter's own setting rather than
// anything the scene says. Built in is the one that asks nothing of the reader:
// the wasm travels as base64 and the library as a plain script, so the file
// opens from a disk with no server and no network. The other two trade that
// away for a smaller page.
function engineDelivery(io, own) {
    var kBesideName = "box2d3wasm.js";
    var choice = String(own.engineSource || "Built into the page");

    if (choice === "From a CDN") {
        var url = String(own.engineUrl || "").trim();
        if (!url)
            throw new Error("Engine is From a CDN and no CDN address was given.");
        io.log("The engine is not in this export: the page imports it from " + url
               + ", so it needs a server and a network to run.");
        return {
            wasmScript: null, libraryScript: null, boot: null,
            bootModule: render("objects/boot-cdn.html.tmpl", { ENGINE_URL: url }),
        };
    }

    var wasm = io.read("vendor/Box2D.compat.wasm.b64").replace(/\s+/g, "");
    var library = engineSource(io);
    var boot = render("objects/boot-built-in.js.tmpl", {});

    if (choice === "Beside the page") {
        // One file holding both, so index.html carries the scene and nothing
        // else. Still no fetch: the bytes are a string in that file.
        io.write(kBesideName,
                 "// box2d3wasm, and its wasm as base64. Written beside the page by"
                 + NEWLINE + "// Physalis; index.html loads it before the scene." + NEWLINE
                 + NEWLINE + library + NEWLINE
                 + "globalThis.Box2DWasmBytes = \"" + wasm + "\";" + NEWLINE);
        io.log("The engine was written beside the page as " + kBesideName
               + ": keep the two together.");
        return {
            wasmScript: null,
            libraryScript: "<script src=\"" + kBesideName + "\"></script>",
            boot: render("objects/boot-beside.js.tmpl", {}),
            bootModule: null,
        };
    }

    return {
        wasmScript: "<script type=\"application/octet-stream;base64\" id=\"wasm\">"
                    + wasm + "</script>",
        libraryScript: "<script>" + library + "</script>",
        boot: boot,
        bootModule: null,
    };
}

function engineSource(io) {
    var source = io.read("vendor/Box2D.compat.mjs");
    var edits = [
        [/if \(ENVIRONMENT_IS_NODE\) \{[\s\S]*?createRequire\(import\.meta\.url\);\s*\}/,
         "{ /* node branch removed: this copy runs as a classic browser script */ }"],
        [/var _scriptName = import\.meta\.url;/, 'var _scriptName = "";'],
        ["  // Use bundler-friendly `new URL(..., import.meta.url)` pattern;"
         + " works in browsers too.", ""],
        [/return new URL\("Box2D\.compat\.wasm", import\.meta\.url\)\.href;/,
         'return "";  // never reached: the bytes arrive as wasmBinary'],
        [/export default Box2D;/, "globalThis.Box2DFactory = Box2D;"],
    ];
    for (var i = 0; i < edits.length; ++i) {
        var was = source;
        source = source.replace(edits[i][0], edits[i][1]);
        if (source === was)
            throw new Error("vendor/Box2D.compat.mjs: nothing matched " + edits[i][0]);
    }
    if (/import\.meta|^\s*export\s/m.test(source))
        throw new Error("vendor/Box2D.compat.mjs: module syntax left after patching");
    return source;
}

// The rules' own code is the one part this converter writes rather than filling
// in from a template, and it is written once for both converters -- as C++, the
// spelling Box2D's own manual uses. This turns that spelling into JavaScript:
// the calls and the fields are the same, only the way they are written differs.
//
// The event arrays are the one real difference. The C API indexes them,
// contacts.beginEvents[i]; the bindings hand them over one at a time,
// contacts.GetBeginEvent(i), because an array in wasm memory is not a JS array.
function toJavaScript(code) {
    var out = [];
    var lines = String(code || "").split(NEWLINE);
    for (var i = 0; i < lines.length; ++i) {
        var line = lines[i];
        if (/^\s*\/\//.test(line)) {          // a comment is prose; leave it alone
            out.push(line);
            continue;
        }
        // An event handed over one at a time.
        line = line.replace(/\.beginEvents\[([^\]]+)\]/g, ".GetBeginEvent($1)")
                   .replace(/\.endEvents\[([^\]]+)\]/g, ".GetEndEvent($1)")
                   .replace(/\.hitEvents\[([^\]]+)\]/g, ".GetHitEvent($1)")
                   .replace(/\.moveEvents\[([^\]]+)\]/g, ".GetMoveEvent($1)")
                   .replace(/\.beginTouchEvents\[([^\]]+)\]/g, ".GetBeginTouchEvent($1)")
                   .replace(/\.endTouchEvents\[([^\]]+)\]/g, ".GetEndTouchEvent($1)");
        // A vector written the way C++ builds one.
        line = line.replace(/\bb2Vec2\s*\{([^{}]*)\}/g, function (all, inside) {
            return "vec(" + inside.replace(/\s+/g, " ").trim() + ")";
        });
        // A declaration keeps its name and loses its type.
        line = line.replace(/\b(?:const\s+)?b2[A-Za-z0-9_]+\s+(\w+)\s*=/g, "var $1 =")
                   .replace(/\bfor\s*\(\s*int\s+/g, "for (var ")
                   .replace(/\b(?:const\s+)?(?:float|int|bool|double)\s+(\w+)\s*=/g, "var $1 =");
        // Box2D's own small maths helpers, which the bindings do not carry over
        // because JavaScript already has them.
        line = line.replace(/\bb2MinFloat\s*\(/g, "Math.min(")
                   .replace(/\bb2MaxFloat\s*\(/g, "Math.max(")
                   .replace(/\bb2AbsFloat\s*\(/g, "Math.abs(")
        // A scalar clamp the bindings do not carry, and pi, which they do --
        // as a number on the module rather than as something to call, so the
        // rule below that prefixes calls never reaches it.
        line = line.replace(/\bb2ClampFloat\s*\(/g, "clampf(")
                   .replace(/\bB2_PI\b/g, "b2.B2_PI");
        // The standard library, as far as this code reaches into it.
        line = line.replace(/\bstd::(?:llround|lround|round)\s*\(/g, "Math.round(")
                   .replace(/\bstd::(?:fabs|abs)\s*\(/g, "Math.abs(")
                   .replace(/\bstd::(?:floor|ceil|sqrt|pow|min|max)\s*\(/g, function (all) {
                       return "Math." + all.slice(5);
                   });
        // Everything Box2D is a member of the module. Done with the offset rather
        // than a lookbehind -- which this engine need not have, and which a plain
        // capture gets wrong anyway where one call opens inside another.
        line = line.replace(/(b2[A-Z]\w*|B2_[A-Z_]+)\s*\(/g, function (all, name, at, whole) {
            var before = at > 0 ? whole.charAt(at - 1) : "";
            return /[\w.]/.test(before) ? all : "b2." + all;
        });
        // m() took a point or a bare length in C++; here the second is len().
        // Innermost first and round again, since a torque is written m(m(x)).
        for (var was = ""; was !== line; ) {
            was = line;
            line = line.replace(/\bm\(([^(),]*)\)/g, "len($1)");
        }
        // C++ spellings with no meaning here: a float suffix, a null pointer, and
        // the address of something that is already a reference.
        line = line.replace(/(\d)f\b/g, "$1")
                   .replace(/\bnullptr\b/g, "null")
                   .replace(/([^&])&([A-Za-z_]\w*)/g, "$1$2");
        out.push(line);
    }
    return out.join(NEWLINE);
}

// --- names -----------------------------------------------------------------

// Words the generated code already uses for something, and C++'s own.
var RESERVED = ("b2 world dt step createWorld reset run draw canvas ctx debug timer "
    + "elapsed stepCount other sensor visitor contacts sensors moves hits pendingRun tick "
    + "randomState nextRandom randomUnit rollBetween rollSteps "
    + "colourOf penOf drawJoint drawRay axis centreOf hatch SENSOR_HATCH SENSOR_FILLED "
    + "PIXELS_PER_METER m len rad bodiesDrawn jointsDrawn view camera pan zoom "
    + "Box2DFactory wasmBinary decodeWasm "
    + "await break case catch class const continue debugger default delete do else enum "
    + "export extends false finally for function if implements import in instanceof "
    + "interface let new null package private protected public return static super switch "
    + "this throw true try typeof var void while with yield "
    + "Array Boolean Date Error Function JSON Math Number Object String document window "
    + "globalThis undefined NaN Infinity parseInt parseFloat setInterval clearInterval "
    + "requestAnimationFrame").split(" ");

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
    // A ray has found nothing until the first step casts it, and the page draws
    // once before that step: left undefined, drawing it threw and took the rest
    // of the boot with it, so the scene appeared and never moved.
    for (var r = 0; r < (scene.rays || []).length; ++r)
        rays.push(NAMES["ray:" + r] + " = { hit: false }");
    var list = function (names) { return names.length ? listValue(names) : null; };
    return render("objects/ids.js.tmpl", {
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
    return render("objects/world.js.tmpl", {
        PIXELS_PER_METER: num(PPM),
        SLOP_UNITS: short(4.0 * 0.005 * PPM),
        GRAVITY_X: fnum(g.x * MOTION),
        GRAVITY_Y: fnum(g.y * MOTION),
        MAXIMUM_LINEAR_SPEED: speed("maximumLinearSpeed", 400),
        RESTITUTION_THRESHOLD: speed("restitutionThreshold", 1),
        HIT_EVENT_THRESHOLD: speed("hitEventThreshold", 1),
        CONTACT_HERTZ: differs(pickNumber(v.contactHertz, 30), 30) ? fnum(v.contactHertz) : null,
        CONTACT_DAMPING_RATIO: differs(pickNumber(v.contactDampingRatio, 10), 10) ? fnum(v.contactDampingRatio) : null,
        ENABLE_SLEEP: v.enableSleep === false ? "false" : null,
        ENABLE_CONTINUOUS: v.enableContinuous === false ? "false" : null,
        ENABLE_WARM_STARTING: v.enableWarmStarting === false ? "false" : null,
        PRE_SOLVE: HELPERS.preSolve ? "notePreSolve" : null,
    });
}

// --- the log ---------------------------------------------------------------

// The readout the editor keeps in a corner of the canvas while a run is going:
// the properties the scene was told to watch, read every frame. The scene
// carries the list; what each one is called and how it is read belong to the
// catalogue, exactly as they do when a rule reads one.
function logCode(scene, style) {
    var watches = scene.log || [];
    if (!watches.length)
        return { code: "", call: null };

    var rows = [];
    for (var i = 0; i < watches.length; ++i) {
        var watch = watches[i];
        var subject = resolve(scene, watch.object);
        if (!subject)
            continue;
        var p = property(subject, watch.property);
        if (!p || !p.read)
            continue;
        // "@world" is a handle, not something to show a reader.
        var who = watch.object === "@world" ? "World" : String(watch.object);
        var label = String(watch.label || watch.property);
        var caption = (who + " \u00b7 " + label + "   ").split("\"").join("'");
        var value = p.unit === "bool"
            ? "(" + p.read + " ? \"true\" : \"false\")"
            : "logNumber(" + outOfBox2D(p.unit, p.read) + ")";
        var line = "rows.push(\"" + caption + "\" + " + value + ");";
        // A body a rule can take away leaves nothing to read.
        var guard = live(subject);
        rows.push(guard ? ("if (" + guard + ") " + line) : line);
    }
    if (!rows.length)
        return { code: "", call: null };

    // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right -- Qt::Corner.
    var corner = Math.round(pickNumber(style.logCorner, 0));
    var family = String(style.logFont || "").split("'").join("").trim();
    return {
        code: render("objects/log.js.tmpl", {
            ROWS: rows.join(NEWLINE),
            FONT_SIZE: String(Math.round(pickNumber(style.logFontSize, 10))),
            FONT_FAMILY: family ? ("'" + family + "', sans-serif") : "sans-serif",
            TEXT_COLOR: cssColour(style.logColor, "#202020"),
            AT_RIGHT: (corner === 1 || corner === 3) ? "true" : "false",
            AT_BOTTOM: (corner === 2 || corner === 3) ? "true" : "false",
        }),
        call: "drawLog();",
    };
}

// mulberry32, the same four lines the application runs, so a seeded scene gives
// the same numbers here as it does there. A seed of zero is the scene asking for
// a different run every time, and gets one from the clock.
function randomCode(world) {
    if (!RANDOM_USED)
        return "";
    // The scene's own, beside pixelsPerMeter, not one of the engine's settings.
    var seed = Math.round(pickNumber(world.randomSeed, 0)) >>> 0;
    return render("objects/random.js.tmpl", {
        SEED: seed ? String(seed) : "(Date.now() >>> 0) | 1",
    });
}

// --- the slingshot ---------------------------------------------------------

// Qt's pen styles as a canvas dash, in scene units: solid, dash, dot, dash-dot,
// dash-dot-dot. 0 is NoPen, which draws nothing, so the line is left solid and
// the width below decides whether it shows at all.
var PEN_DASHES = { 2: "[8, 4]", 3: "[1, 4]", 4: "[8, 4, 1, 4]", 5: "[8, 4, 1, 4, 1, 4]" };

// Four numbers for a colour, whatever the settings spell it as.
function rgbaParts(colour, fallback) {
    var text = qtColour(colour, fallback);
    var parts = text.split(", ");
    while (parts.length < 4)
        parts.push("255");
    return parts.join(", ");
}

// Flinging a body with the mouse. Which bodies can be flung, how hard and how
// far they are pulled belong to the scene; how the pull line looks is the
// application's own setting, the same as the joint colours.
function slingshotCode(scene, style) {
    var saved = scene.bodies || [];
    var byName = {};
    for (var i = 0; i < saved.length; ++i) {
        var shot = saved[i].shot;
        if (shot && shot.enabled)
            byName[saved[i].name] = shot;
    }

    var rows = [];
    var bodies = scene.simulation.bodies;
    for (var b = 0; b < bodies.length; ++b) {
        var shot = byName[bodies[b].name];
        // A static or kinematic body does not answer to an impulse, and the
        // editor will not aim at one either.
        if (!shot || bodies[b].type !== "dynamic")
            continue;
        rows.push("shootable.push({ body: " + NAMES["body:" + b]
                  + ", maxPull: " + short(pickNumber(shot.maxPull, 0))
                  + ", fullImpulse: " + short(pickNumber(shot.fullImpulse, 0)) + " });");
    }
    if (!rows.length)
        return { code: "", list: "", draw: null, down: null, move: null, up: null, cancel: null };

    var width = pickNumber(style.shotLineWidth, 2);
    var dash = PEN_DASHES[Math.round(pickNumber(style.shotLineStyle, 1))] || "[]";
    return {
        list: ["shootable = [];"].concat(rows).join(NEWLINE),
        code: render("objects/slingshot.js.tmpl", {
            VIEW_CENTER_X: "0.0",
            VIEW_CENTER_Y: "0.0",
            LIGHT_RGBA: rgbaParts(style.shotLightColor, "255, 255, 255, 120"),
            FULL_RGBA: rgbaParts(style.shotFullColor, "232, 106, 106, 255"),
            LINE_WIDTH: short(width),
            LINE_DASH: dash,
        }),
        draw: "drawShot();",
        down: "if (beginShot(e)) { canvas.setPointerCapture(e.pointerId); return; }",
        move: "if (shot) { aimShot(e); if (!timer) draw(); return; }",
        up: "if (shot) { releaseShot(); canvas.releasePointerCapture(e.pointerId); return; }",
        cancel: "if (event.key === \"Escape\") cancelShot();",
    };
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
    // Box2D reports a sensor overlap only when the sensor and the shape entering
    // it both have sensor events on, and neither does by default. Once a rule
    // watches a sensor the editor gives them to every shape in the world, so a
    // pocket does not need every ball ticked by hand -- and without the same
    // here, a rule on a sensor fired for nothing the scene had not ticked.
    SENSOR_WATCHED = false;
    var all = scene.simulation.bodies;
    for (var w = 0; !SENSOR_WATCHED && w < all.length; ++w) {
        var parts = all[w].parts || [];
        for (var q = 0; q < parts.length; ++q) {
            if (WATCHED[parts[q].name] && vals(parts[q]).isSensor)
                SENSOR_WATCHED = true;
        }
    }
    var out = [];
    var bodies = scene.simulation.bodies;
    for (var i = 0; i < bodies.length; ++i)
        out.push(bodyCode(bodies[i], NAMES["body:" + i], colours));
    return out.join(NEWLINE + NEWLINE);
}

var BODY_TYPES = { dynamic: "b2.b2BodyType.b2_dynamicBody", kinematic: "b2.b2BodyType.b2_kinematicBody" };

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
    return render("objects/body.js.tmpl", {
        ID: name,
        DRAW: drawRecord(body, name),
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
        LOCK_LINEAR_X: v.lockLinearX ? "true" : null,
        LOCK_LINEAR_Y: v.lockLinearY ? "true" : null,
        LOCK_ANGULAR_Z: v.lockAngularZ ? "true" : null,
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

// Where a body balances, in its own frame: each solid part's centroid weighted
// by its area and what it is made of. Box2D answers this for a body that has
// mass, but a static one has none and reports its origin instead -- and the
// editor draws the axes of every body at the shapes' centre whatever its type,
// so the point is worked out here rather than asked of the solver.
function localCentre(body) {
    var all = body.parts || [];
    var weighted = { x: 0, y: 0 }, total = 0;
    for (var i = 0; i < all.length; ++i) {
        var part = all[i];
        var c = part.center || { x: 0, y: 0 };
        var area = 0, at = c;
        if (part.kind === "box") {
            area = (part.halfExtents.x * 2) * (part.halfExtents.y * 2);
        } else if (part.kind === "circle") {
            area = Math.PI * part.radius * part.radius;
        } else if (isSolidPolygon(part)) {
            var pts = part.points || [], twice = 0, cx = 0, cy = 0;
            for (var k = 0; k < pts.length; ++k) {
                var a = pts[k], b = pts[(k + 1) % pts.length];
                var cross = a.x * b.y - b.x * a.y;
                twice += cross;
                cx += (a.x + b.x) * cross;
                cy += (a.y + b.y) * cross;
            }
            if (twice !== 0) {
                area = Math.abs(twice) / 2;
                at = { x: cx / (3 * twice), y: cy / (3 * twice) };
            }
        }
        // An outline encloses nothing, so it carries no mass.
        if (area <= 0)
            continue;
        var mass = area * Math.max(0, pickNumber(vals(part).density, 1));
        if (mass <= 0)
            continue;
        weighted.x += at.x * mass;
        weighted.y += at.y * mass;
        total += mass;
    }
    if (total <= 0)
        return { x: 0, y: 0 };      // nothing solid: the origin, as the editor has it
    return { x: weighted.x / total, y: weighted.y / total };
}

// How a body is painted: its kind, and each of its shapes as the editor drew it
// -- body-local, in scene units. Nothing is read back out of the solver, so a
// rounded box stays rounded and an outline stays open.
function drawRecord(body, name) {
    var parts = [];
    var all = body.parts || [];
    for (var i = 0; i < all.length; ++i) {
        var part = all[i];
        if (isOutline(part) && body.type === "dynamic")
            continue;   // never built, so never drawn
        var v = vals(part);
        var c = part.center || { x: 0, y: 0 };
        var sensor = v.isSensor ? "true" : "false";
        if (part.kind === "circle") {
            parts.push("{ kind: \"circle\", x: " + short(c.x) + ", y: " + short(c.y)
                       + ", radius: " + short(part.radius) + ", sensor: " + sensor + " }");
            continue;
        }
        var corners = [];
        var rounding = 0;
        if (part.kind === "box") {
            // The four corners, turned and moved the way the shape is: what the
            // editor drew, rather than the inset hull Box2D collides with. The
            // corner radius rides along, since at half the shorter side the
            // editor draws a capsule and four sharp corners are a different shape.
            var hw = part.halfExtents.x, hh = part.halfExtents.y;
            rounding = Math.max(0, Math.min(part.cornerRadius || 0, Math.min(hw, hh)));
            var turn = (part.rotation || 0) * Math.PI / 180;
            var cs = Math.cos(turn), sn = Math.sin(turn);
            var box = [[-hw, -hh], [hw, -hh], [hw, hh], [-hw, hh]];
            for (var k = 0; k < 4; ++k) {
                corners.push("[" + short(c.x + box[k][0] * cs - box[k][1] * sn) + ", "
                             + short(c.y + box[k][0] * sn + box[k][1] * cs) + "]");
            }
        } else {
            var pts = part.points || [];
            for (var q = 0; q < pts.length; ++q)
                corners.push("[" + short(pts[q].x) + ", " + short(pts[q].y) + "]");
            // An outline is what the editor drew a line along and what Box2D
            // was given as segments: it encloses nothing, so it is stroked and
            // never filled. A closed one comes back round to its first point.
            // Deciding this by the same question the shape was built with keeps
            // the two from disagreeing -- a closed outline used to be painted
            // as a solid polygon while the solver had it as a row of segments.
            if (isOutline(part)) {
                if (part.kind === "polygon" || part.closed)
                    corners.push(corners[0]);
                parts.push("{ kind: \"segments\", points: [" + corners.join(", ")
                           + "], sensor: " + sensor + " }");
                continue;
            }
        }
        parts.push("{ kind: \"polygon\", points: [" + corners.join(", ")
                   + "]" + (rounding > 0 ? ", radius: " + short(rounding) : "")
                   + ", sensor: " + sensor + " }");
    }
    var centre = localCentre(body);
    var flung = SHOOTABLE[body.name] && (body.type || "static") === "dynamic";
    return "drawn.push({ id: " + name + ", type: \"" + (body.type || "static")
           + "\", centre: [" + short(centre.x) + ", " + short(centre.y) + "]"
           + (flung ? ", shootable: true" : "")
           + ", parts: [" + parts.join(", ") + "] });";
}

// What to write for a shape's density so its mass is the editor's. Box2D's own
// default is 1, and a scene at that default still has to say so, because at the
// page's metre the number is no longer 1.
function densityAtPageScale(v) {
    var density = pickNumber(v.density, 1) / (SCALE * SCALE);
    return differs(density, 1) ? fnum(density) : null;
}

function shapeCode(part, body, bodyName, colours, first, counts) {
    // An outline has no area and so no mass; Box2D would leave a dynamic body
    // made of one frozen in place, so the editor does not build it either.
    if (isOutline(part) && body.type === "dynamic")
        return render("objects/massless-outline.js.tmpl", { NAME: part.name || "an outline" });

    var v = vals(part);
    var category = bits64(v.categoryBits, "0x1ull");
    var mask = bits64(v.maskBits, ALL_BITS);
    var watched = WATCHED[part.name] || WATCHED[body.name];
    var def = render("objects/shape-def.js.tmpl", {
        DECLARE: first ? "var " : "",
        // An area is SCALE^2 bigger at the page's metre, so the density comes
        // down by the same square and every body weighs what it does in the editor.
        DENSITY: densityAtPageScale(v),
        FRICTION: differs(pickNumber(v.friction, 0.6), 0.6) ? fnum(v.friction) : null,
        RESTITUTION: v.restitution ? fnum(v.restitution) : null,
        ROLLING_RESISTANCE: v.rollingResistance ? fnum(v.rollingResistance) : null,
        // A speed, so it goes through the scale like every other one.
        TANGENT_SPEED: v.tangentSpeed ? fnum(v.tangentSpeed / PPM) : null,
        CATEGORY: category !== "0x1ull" ? category : null,
        MASK: mask !== ALL_BITS ? mask : null,
        GROUP: v.groupIndex ? String(v.groupIndex) : null,
        SENSOR: v.isSensor ? "true" : null,
        SENSOR_EVENTS: (v.enableSensorEvents || SENSOR_WATCHED) ? "true" : null,
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
        var rot = part.rotation ? render("objects/rotation.js.tmpl", { ANGLE: short(part.rotation) })
                                : "noTurn()";
        var centred = !c.x && !c.y && !part.rotation;
        var size = r > 0 ? { HALF_WIDTH: short(Math.max(hw - r, 0.01)), HALF_HEIGHT: short(Math.max(hh - r, 0.01)) }
                         : { HALF_WIDTH: short(hw), HALF_HEIGHT: short(hh) };
        size.X = short(c.x);
        size.Y = short(c.y);
        size.ROTATION = rot;
        size.RADIUS = short(r);
        var make = render("objects/" + (r > 0 ? (centred ? "make-rounded-box" : "make-offset-rounded-box")
                                              : (centred ? "make-box" : "make-offset-box")) + ".js.tmpl", size);
        return render("objects/box.js.tmpl", { DEF: def, NAME: box, MAKE: make, KEEP: keep, BODY: bodyName });
    }

    if (part.kind === "circle") {
        return render("objects/circle.js.tmpl", {
            DEF: def, NAME: geometryName("circle", counts), X: short(c.x), Y: short(c.y),
            RADIUS: short(part.radius), KEEP: keep, BODY: bodyName,
        });
    }

    var pts = part.points || [];
    var points = geometryName("points", counts);
    var list = [];
    for (var i = 0; i < pts.length; ++i)
        list.push("m(" + short(pts[i].x) + ", " + short(pts[i].y) + ")");
    var pointsCode = render("objects/points.js.tmpl", { NAME: points, POINTS: listValue(list) });

    if (isSolidPolygon(part)) {
        var hull = geometryName("hull", counts);
        return render("objects/polygon.js.tmpl", {
            DEF: def, POINTS: pointsCode, HULL: hull, POINTS_NAME: points, COUNT: String(pts.length),
            NAME: geometryName("polygon", counts), KEEP: keep, BODY: bodyName,
        });
    }

    var closed = part.kind === "polygon" || part.closed;
    if (part.kind === "chain" && part.smoothChain && pts.length >= 4) {
        return render("objects/chain.js.tmpl", {
            DEF: def, POINTS: pointsCode, POINTS_NAME: points, COUNT: String(pts.length),
            LOOP: closed ? "true" : null, BODY: bodyName,
        });
    }
    return render("objects/segments.js.tmpl", {
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
        return render("objects/unsupported-joint.js.tmpl", {
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
    // Box2D 3.2 defines a joint by a frame on each body rather than by anchors, a
    // reference angle and an axis: it works to bring frame A onto frame B. Frame
    // A's x axis is the axis for the kinds that slide, and frame B is turned back
    // by the reference angle, so the bodies come to rest that far apart.
    //
    // The two angles are always written. The anchors always were, and the
    // rotations now carry what the reference angle and the axis used to say.
    var frames = function (referenceDegrees) {
        var ax = joint.axis || { x: 1, y: 0 };
        // Into body A's frame: turned back by A's own rotation.
        var turn = -(bodies[joint.bodyA].rotation || 0);
        var axisDegrees = needsAxis
            ? Math.atan2(ax.y, ax.x) * 180 / Math.PI + turn
            : 0;
        values.FRAME_A_DEG = short(axisDegrees);
        values.FRAME_B_DEG = short(axisDegrees - referenceDegrees);
    };
    var needsAxis = type === "prismatic" || type === "wheel";

    if (type === "revolute") {
        // Box2D asserts on lower > upper and on a limit past a half turn.
        var angles = ordered(clampAngle(pick(p, "lowerAngle", 0)), clampAngle(pick(p, "upperAngle", 0)));
        frames(resting + pick(p, "referenceAngle", 0));
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
        var origin = travelOrigin(joint);
        var span = ordered(pick(p, "lowerTranslation", 0), pick(p, "upperTranslation", 0));
        frames(resting + (wheel ? 0 : pick(p, "referenceAngle", 0)));
        if (!wheel)
            values.TARGET_TRANSLATION = length(origin + pick(p, "targetTranslation", 0));
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
        frames(resting);
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
        frames(resting + pick(p, "referenceAngle", 0));
        values.LINEAR_HERTZ = number(pick(p, "linearHertz", 0), 0);
        values.ANGULAR_HERTZ = number(pick(p, "angularHertz", 0), 0);
        values.LINEAR_DAMPING_RATIO = number(pick(p, "linearDampingRatio", 0), 0);
        values.ANGULAR_DAMPING_RATIO = number(pick(p, "angularDampingRatio", 0), 0);
    } else if (type === "motor") {
        frames(resting);
        values.LINEAR_VELOCITY_X = length(pick(p, "linearVelocityX", 0));
        values.LINEAR_VELOCITY_Y = length(pick(p, "linearVelocityY", 0));
        values.ANGULAR_VELOCITY = angle(pick(p, "angularVelocity", 0));
        values.MAX_VELOCITY_FORCE = number(pick(p, "maxVelocityForce", 1), 1);
        values.MAX_VELOCITY_TORQUE = number(pick(p, "maxVelocityTorque", 1), 1);
        values.LINEAR_HERTZ = number(pick(p, "linearHertz", 0), 0);
        values.LINEAR_DAMPING_RATIO = number(pick(p, "linearDampingRatio", 1), 1);
        values.MAX_SPRING_FORCE = number(pick(p, "maxSpringForce", 1), 1);
        values.ANGULAR_HERTZ = number(pick(p, "angularHertz", 0), 0);
        values.ANGULAR_DAMPING_RATIO = number(pick(p, "angularDampingRatio", 1), 1);
        values.MAX_SPRING_TORQUE = number(pick(p, "maxSpringTorque", 1), 1);
    } else if (type === "filter") {
        frames(resting);
    } else if (type !== "filter") {
        return render("objects/unsupported-joint.js.tmpl", {
            NAME: name, WHY: "is a kind of joint this converter does not know; not exported.",
        });
    }
    return render("objects/" + type + ".js.tmpl", values);
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
    for (var t = 0; t < TIMERS.length; ++t) {
        out = out.concat(render("objects/timer-tick.js.tmpl",
                                { NAME: TIMERS[t].handle,
                                  RUNNING: TIMERS[t].running }).split(NEWLINE));
    }
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
    if (events.length > 1) {
        // Two different events would need both raised on the same step, and the
        // second is not in scope inside the first's loop. The same event on the
        // same subject is not that: those differ only in what was touched, so
        // one loop with the others OR-ed together says exactly the same thing.
        var alike = any;
        for (var e = 1; alike && e < events.length; ++e)
            alike = events[e].event === events[0].event;
        if (!alike) {
            return { error: "it watches more than one event, which this converter"
                            + " cannot join" };
        }
    }
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
        // One pair of shapes per condition: several alike events joined with "or"
        // are one loop over the step's events with a wider test, since each is
        // asking about the same two shapes in the same array.
        var pairs = [];
        for (var w = 0; w < shape.events.length; ++w) {
            var each = shape.events[w];
            var who = side(scene, each.subject);
            if (!who)
                return { error: "cannot tell which shape " + each.subject + " is" };
            var with_ = null;
            if (each.when) {
                with_ = side(scene, String(each.when));
                if (!with_)
                    return { error: "cannot tell which shape " + each.when + " is" };
            }
            pairs.push({ subject: who, partner: with_ });
        }
        if (!pairs.length) {
            var only = side(scene, primary.subject);
            if (!only)
                return { error: "cannot tell which shape " + primary.subject + " is" };
            pairs.push({ subject: only, partner: null });
        }
        // "The other object" is whichever of the two is not the subject, and for a
        // contact that is only answerable with one subject: either shape of the
        // pair could be it. A sensor event names its two sides -- the sensor and
        // the visitor -- so where every subject is a sensor the other one is the
        // visitor however many there are. Six pockets watching for a ball is
        // exactly that, and refusing it left a pool table that swallowed nothing.
        var everySubjectIsASensor = event.indexOf("sensor") === 0;
        for (var p = 0; everySubjectIsASensor && p < pairs.length; ++p)
            everySubjectIsASensor = pairs[p].subject.isSensor && !pairs[p].partner;
        if (pairs.length > 1 && ruleNeedsOther(rule) && !everySubjectIsASensor) {
            return { error: "it watches several shapes and acts on the other one,"
                            + " which this converter cannot tell apart" };
        }
        var effect = guarded(allEffectLines(scene, rule, "other"), guard);
        if (!effect)
            return { error: "nothing in Box2D does " + describe(rule).split(", ")[1] };
        if (event === "preSolve")
            HELPERS.preSolve = true;
        loops[pair[event]].push({ caption: caption, pairs: pairs,
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
    var rayPick = null;   // the line naming which of several rays found something
    if (event === "@runStarted") {
        // Once, after the first step.
        condition = "true";
    } else if (event === "rayDetects") {
        // Every ray the card names, not just the first. A ray is not an event
        // the step hands out, it is a cast this converter made itself, so there
        // is no loop to OR the others into -- they are OR-ed here instead. A
        // card watching two rays used to be written as though it watched one,
        // and the second ray was cast, drawn, and read by nothing.
        var rays = [];
        for (var r = 0; r < shape.events.length; ++r) {
            var watching = shape.events[r];
            var ray = resolve(scene, watching.subject);
            if (!ray || ray.kind !== "ray")
                return { error: watching.subject + " is not a ray" };
            var test = ray.handle + ".hit";
            if (watching.when) {
                var seen = side(scene, String(watching.when));
                if (!seen)
                    return { error: "cannot tell which shape " + watching.when + " is" };
                test += " && " + seen.test(ray.handle + ".shapeId");
            }
            rays.push({ handle: ray.handle, test: test });
        }
        if (rays.length === 1) {
            condition = rays[0].test;
            effectOther = rays[0].handle + ".shapeId";
        } else {
            var tests = [];
            for (var t = 0; t < rays.length; ++t)
                tests.push("(" + rays[t].test + ")");
            condition = tests.join(" || ");
            // Which one found it is the rule's "the other object". Worked out
            // once into a name: an action that takes the body away makes the
            // ray's own answer stale, so asking a second time would hand the
            // next action a shape that has just been destroyed.
            var found = identifier(base + "Found", "found");
            var pick = rays[rays.length - 1].handle + ".shapeId";
            for (var p = rays.length - 2; p >= 0; --p)
                pick = "(" + rays[p].test + " ? " + rays[p].handle + ".shapeId : " + pick + ")";
            rayPick = "b2ShapeId " + found + " = " + pick + ";";
            effectOther = found;
        }
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
    if (rayPick)
        actions = [rayPick].concat(actions);
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
    STATE.push(render("objects/state.js.tmpl", { TYPE: type, NAME: name }));
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
        // Each pair the rule named, either way round, OR-ed together.
        var tests = [];
        var otherFrom = null;
        for (var q = 0; q < w.pairs.length; ++q) {
            var who = w.pairs[q].subject, with_ = w.pairs[q].partner;
            if (w.sensor && who.isSensor) {
                // The sensor is always the first of the two.
                tests.push(who.test(first) + (with_ ? (" && " + with_.test(second)) : ""));
                otherFrom = second;
                continue;
            }
            if (!with_) {
                tests.push(who.test(first) + " || " + who.test(second));
            } else {
                tests.push("(" + who.test(first) + " && " + with_.test(second) + ")");
                tests.push("(" + who.test(second) + " && " + with_.test(first) + ")");
            }
            otherFrom = who.test(first) + " ? " + second + " : " + first;
        }
        var test = tests.length === 1 ? tests[0] : ("(" + tests.join(") || (") + ")");
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
        lines = lines.concat(render("objects/hit-record.js.tmpl", { ID: HITS[i].handle }).split(NEWLINE));
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
        return render("objects/ray-cast.js.tmpl", values);
    values.MASK = "0x" + mask + "ull";
    return render("objects/ray-cast-filtered.js.tmpl", values);
}

// The editor's colour as QColor's arguments: red, green, blue, alpha.
// "#aarrggbb" or "#rrggbb" as CSS. Qt counts alpha in 0..255 and CSS in 0..1,
// which is the one part that cannot simply be copied across.
function cssColour(colour, fallback) {
    var text = String(colour || "");
    var hex = /^#[0-9a-fA-F]{8}$/.test(text) ? text.substring(1)
            : /^#[0-9a-fA-F]{6}$/.test(text) ? "ff" + text.substring(1)
            : null;
    if (!hex)
        return fallback;
    var at = function (i) { return parseInt(hex.substring(i, i + 2), 16); };
    return "rgba(" + at(2) + ", " + at(4) + ", " + at(6) + ", "
           + (Math.round(at(0) / 255 * 1000) / 1000) + ")";
}

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
    var physics = (scene.settings && scene.settings.Physics) || {};
    var coils = physics.springsAsCoils === undefined ? true : !!physics.springsAsCoils;
    var pitch = pickNumber(physics.springPitch, 14);
    var springKeys = springKeysByType(scene);
    var sceneJoints = scene.joints || [];
    for (var j = 0; j < scene.simulation.joints.length; ++j) {
        var turns = coils && sceneJoints[j]
                        ? springTurnsOf(sceneJoints[j], springKeys[sceneJoints[j].type], pitch)
                        : 0;
        joints.push("[" + NAMES["joint:" + j] + ", " + turns + "]");
    }
    for (var b = 0; b < scene.simulation.bodies.length; ++b)
        bodies.push(NAMES["body:" + b]);
    var sceneRays = scene.rays || [];
    for (var i = 0; i < sceneRays.length; ++i) {
        var values = rayGeometry(sceneRays[i]);
        values.ID = NAMES["ray:" + i];
        rays.push(render("objects/ray-draw.js.tmpl", values));
    }
    return render("objects/debug-drawing.js.tmpl", {
        JOINTS: joints.length ? render("objects/joint-drawing.js.tmpl", { JOINTS: listValue(joints) }) : "",
        AXES: bodies.length ? render("objects/axes-drawing.js.tmpl", {}) : "",
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
// The timer variables, in declaration order, so the step code can move each one
// on; and the ops that are a timer's rather than a value's.
var TIMERS = [];
var TIMER_VERBS = { timerStart: true, timerPause: true, timerStop: true, timerReset: true };

function declareVariables(scene) {
    VARIABLES = {};
    TIMERS = [];
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
        // A timer is a millisecond count and a flag saying whether it is
        // counting. The count is a float so the fraction of a millisecond a
        // step leaves over is not lost, which is what the editor keeps too.
        if (variable.type === "timer") {
            var flag = identifier(variable.name + "_running", "timer_running");
            remember("bool", flag, "false");
            VARIABLES[variable.name].running = flag;
            VARIABLES[variable.name].start = start;
            TIMERS.push({ handle: ident, running: flag });
        }
    }
}

// A variable as a property, which is all the rule machinery needs it to be:
// something to read and something to write. The unit is the plain one, since a
// variable is a number the scene made up rather than a length or an angle.
function variableProperty(key) {
    var variable = VARIABLES ? VARIABLES[key] : null;
    if (!variable)
        return null;
    var unit = variable.type === "bool" ? "bool"
                 : variable.type === "int" ? "int" : "num";
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
                     // The sensor flag is one of the engine's own properties, so it
                     // sits in the part's physics block and not on the part. Read
                     // from the wrong place it was always false, and a rule on a
                     // sensor was written as though it watched an ordinary shape.
                     shapeKind: parts[p].kind, isSensor: !!vals(parts[p]).isSensor,
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
    // Read, not kept: a fresh number every time a rule asks, so "chance < 25"
    // is that rule about one time in four.
    if (key === "chance") {
        RANDOM_USED = true;
        return prop("float", "randomUnit() * 100.0f", null);
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
                     fnum(pickNumber(WORLD_SETTINGS.contactSpeed, 3) * MOTION));
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
var SHOOTABLE = {};   // body name -> can be flung, for the mark drawing shows
var SENSOR_WATCHED = false;   // a rule watches a sensor, so every shape reports them
var RANDOM_USED = false;      // a rule rolls a value or asks the world its chance

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
            var named = { "0": "b2.b2BodyType.b2_staticBody", "1": "b2.b2BodyType.b2_kinematicBody", "2": "b2.b2BodyType.b2_dynamicBody" };
            return ["b2Body_SetType(" + B + ", " + (named[v] || ("b2BodyType(" + v + ")")) + ");"];
        });
    case "centerOfMassX": return prop("len", "b2Body_GetWorldCenter(" + B + ").x", null);
    case "centerOfMassY": return prop("len", "b2Body_GetWorldCenter(" + B + ").y", null);
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
    case "mass": return prop("none", "b2Shape_ComputeMassData(" + S + ").mass", null);
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
        remember("b2Vec2", place.handle + "HitPoint", "vec(0, 0)");
        remember("b2Vec2", place.handle + "HitNormal", "vec(0, 0)");
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
        // 3.2 turned the motor joint from a position offset into a velocity with
        // an optional spring: linearOffset, angularOffset, correctionFactor,
        // maxForce and maxTorque are gone. A rule naming one of those is refused
        // rather than written as a call that does not exist.
        switch (key) {
        case "maxVelocityForce": return simple("none", "GetMaxVelocityForce", "SetMaxVelocityForce");
        case "maxVelocityTorque": return simple("none", "GetMaxVelocityTorque", "SetMaxVelocityTorque");
        case "maxSpringForce": return simple("none", "GetMaxSpringForce", "SetMaxSpringForce");
        case "maxSpringTorque": return simple("none", "GetMaxSpringTorque", "SetMaxSpringTorque");
        case "linearHertz": return simple("none", "GetLinearHertz", "SetLinearHertz");
        case "linearDampingRatio": return simple("none", "GetLinearDampingRatio", "SetLinearDampingRatio");
        case "angularHertz": return simple("none", "GetAngularHertz", "SetAngularHertz");
        case "angularDampingRatio": return simple("none", "GetAngularDampingRatio", "SetAngularDampingRatio");
        case "angularVelocity": return simple("angle", "GetAngularVelocity", "SetAngularVelocity");
        case "linearVelocityX":
        case "linearVelocityY":
            return prop("len", P + "GetLinearVelocity(" + J + ")." + (key === "linearVelocityX" ? "x" : "y"),
                        function (v) {
                var keep = P + "GetLinearVelocity(" + J + ")." + (key === "linearVelocityX" ? "y" : "x");
                return [wake, P + "SetLinearVelocity(" + J + ", b2Vec2{ "
                        + (key === "linearVelocityX" ? (v + ", " + keep) : (keep + ", " + v)) + " });"];
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
    // A clone is built from the copy the export already holds, never from the
    // body in the world, so it is the one action that outlives its subject --
    // the editor clones a removed body too. Guarding it stopped a scene that
    // takes its own balls away from ever making another one.
    var guard = rule.action === "@clone" ? "" : live(target);
    if (!guard || needsOther(rule))
        return lines;
    return ["if (" + guard + ") {"].concat(indentLines("    ", lines)).concat(["}"]);
}

// Starting, pausing or winding back a timer is not writing a value to it, so
// it is answered before the ordinary value path is reached at all. Null when the
// rule is not one of the four, which leaves everything else alone.
function timerVerbLines(rule) {
    var op = rule.op || "set";
    if (rule.target !== "@variables" || !TIMER_VERBS[op])
        return null;
    var variable = VARIABLES ? VARIABLES[rule.property] : null;
    if (!variable || variable.type !== "timer")
        return null;

    var run = function (on) { return variable.running + " = " + on + ";"; };
    var rewind = function () { return variable.handle + " = " + variable.start + ";"; };
    if (op === "timerStart")
        return [run("true")];
    if (op === "timerPause")
        return [run("false")];
    if (op === "timerStop")
        return [run("false"), rewind()];
    return [rewind()]; // timerReset: back to the start, still counting
}

// A number a rule carries, as code: the number itself, or a roll between two of
// them. The same shape the editor writes -- a map of from, to and step -- so a
// value and an action's parameter are handled the one way.
function isRange(given) {
    return given !== null && typeof given === "object" && given.from !== undefined;
}

// In the units Box2D wants, which is what literal() does to a plain number.
function rolled(unit, given) {
    RANDOM_USED = true;
    var low = literal(unit, given.from);
    var high = literal(unit, given.to);
    var step = pickNumber(given.step, 0);
    if (step > 0)
        return "rollSteps(" + low + ", " + high + ", " + literal(unit, step) + ")";
    return "rollBetween(" + low + ", " + high + ")";
}

function numberOrRoll(unit, given) {
    return isRange(given) ? rolled(unit, given) : literal(unit, given);
}

// A bare number for somewhere that cannot take an expression. A range reaching
// one of these would quietly become its near end, so it says so instead.
function flatNumber(given, fallback, what) {
    if (isRange(given))
        throw new Error(what + " cannot be a range here.");
    return pickNumber(given, fallback);
}

function writeLines(scene, rule, target) {
    var verb = timerVerbLines(rule);
    if (verb)
        return verb;
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
        value = p.read + (op === "add" ? " + " : " - ") + numberOrRoll(p.unit, rule.value);
    } else {
        value = numberOrRoll(p.unit, rule.value);
    }
    return p.write(value);
}

// What an explosion setting falls back to: the engine's own default, which is
// where the editor takes it from too. A scene stores only what differs from it,
// so an explosion left as it was made carries no radius at all -- and reading
// that as zero made the rule look impossible to write.
function explosionDefault(scene, key, fallback) {
    var actions = (scene.engine && scene.engine.bodyActions) || [];
    for (var i = 0; i < actions.length; ++i) {
        if (actions[i].id !== "explode")
            continue;
        var params = actions[i].params || [];
        for (var p = 0; p < params.length; ++p) {
            if (params[p].key === key)
                return pickNumber(params[p].defaultValue, fallback);
        }
    }
    return fallback;
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
                + numberOrRoll("len", params.x) + ", " + numberOrRoll("len", params.y) + "));"];
    }
    if (rule.action === "pushForceAt") {
        if (!isBody)
            return null;
        var force = "vec(" + numberOrRoll("len", params.impulseX) + ", " + numberOrRoll("len", params.impulseY) + ")";
        var fx = flatNumber(params.offsetX, 0, "Where a push is applied"),
            fy = flatNumber(params.offsetY, 0, "Where a push is applied");
        if (!fx && !fy)
            return ["b2Body_ApplyForceToCenter(" + body + ", " + force + ", true);"];
        return ["b2Body_ApplyForce(" + body + ", " + force + ", b2Add(b2Body_GetWorldCenter("
                + body + "), m(" + short(fx) + ", " + short(fy) + ")), true);"];
    }
    if (rule.action === "resetMass")
        return isBody ? ["b2Body_UpdateMassFromShapes(" + body + ");"] : null;

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
        var radius = pickNumber(settings.radius, explosionDefault(scene, "radius", 200));
        if (!at || !(radius > 0))
            return null;
        var lines = ["{", "    b2ExplosionDef explosion = b2DefaultExplosionDef();",
                     "    explosion.position = " + at + ";",
                     "    explosion.radius = m(" + short(radius) + ");"];
        var falloff = pickNumber(settings.falloff, explosionDefault(scene, "falloff", 100));
        if (falloff)
            lines.push("    explosion.falloff = m(" + short(falloff) + ");");
        if (pickNumber(settings.impulse, 0))
            // Impulse per unit length: the impulse grows with the scale and so does
            // the length under it, so this one number is unchanged by the choice.
            lines.push("    explosion.impulsePerLength = m("
                       + short(pickNumber(settings.impulse, 0) / SCALE) + ");");
        if (pickNumber(settings.maskBits, 0) > 0)
            lines.push("    explosion.maskBits = " + bits64(settings.maskBits, ALL_BITS) + ";");
        lines.push("    b2World_Explode(world, &explosion);");
        lines.push("}");
        return lines;
    }
    if (rule.action === "pushAt") {
        if (target.kind !== "body" && target.kind !== "shape")
            return null;
        var impulse = "vec(" + numberOrRoll("len", params.impulseX) + ", "
                      + numberOrRoll("len", params.impulseY) + ")";
        var ox = flatNumber(params.offsetX, 0, "Where a push is applied"),
            oy = flatNumber(params.offsetY, 0, "Where a push is applied");
        if (!ox && !oy)
            return ["b2Body_ApplyLinearImpulseToCenter(" + body + ", " + impulse + ", true);"];
        return ["b2Body_ApplyLinearImpulse(" + body + ", " + impulse + ", b2Add(b2Body_GetWorldCenter("
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
        RESETS.push(render("objects/pre-solve-reset.js.tmpl", {}));
        out.push(render("objects/pre-solve.js.tmpl", {}));
    }
    if (HELPERS.initState) {
        var starts = [];
        for (var b = 0; b < bodies.length; ++b) {
            var p = bodies[b].position || { x: 0, y: 0 };
            // A list, not a brace-initialised struct: the template reads these
            // back by index, and C++'s spelling of one is not JavaScript's.
            starts.push("[" + NAMES["body:" + b] + ", " + fnum(p.x) + ", " + fnum(p.y) + ", "
                        + fnum(bodies[b].rotation || 0) + "]");
        }
        out.push(render("objects/init-state.js.tmpl", { STARTS: listValue(starts) }));
    }
    for (var index in HELPERS.clones) {
        if (!has(HELPERS.clones, index))
            continue;
        // Built exactly as the original is, keeping no shape ids of its own.
        var kept = USED;
        USED = {};
        var made = bodyCode(bodies[index], "clone", colours);
        USED = kept;
        out.push(render("objects/clone.js.tmpl", { NAME: bodies[index].name, ID: NAMES["body:" + index], BODY: made }));
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
    return render("objects/toolbar.js.tmpl", {
        BUTTONS: own.addControls ? render("objects/buttons.js.tmpl", {}) : "",
        DEBUG_SWITCH: own.debugView ? render("objects/debug-switch.js.tmpl", {}) : "",
    });
}

// The controls are HTML above the canvas; this is what makes them do anything,
// written after the engine is ready so nothing can be pressed too early.
function controlWiring(own) {
    if (!own.addControls && !own.debugView)
        return "";
    return render("objects/control-wiring.js.tmpl", {
        BUTTON_WIRING: own.addControls ? render("objects/button-wiring.js.tmpl", {}) : "",
        DEBUG_WIRING: own.debugView ? render("objects/debug-wiring.js.tmpl", {}) : "",
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
        sensorColor: cssColour(physics.sensorColor, "rgba(5, 201, 54, 1)"),
        // The number itself: the page draws the hatching by hand rather than
        // handing a style to Qt.
        sensorPattern: String(sensorPattern(physics.sensorPattern)),
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
// JavaScript has no float suffix; the name stays so the templates read the same
// as the C++ converter's, which this one is a transliteration of.
function fnum(v) { return num(v); }

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
