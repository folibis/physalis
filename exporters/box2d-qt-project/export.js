// Physalis scene -> a standalone Qt + Box2D project.
//
// Everything the application knows about this format is nothing: it hands over
// the scene and a way to read templates and write files, and the rest is here.
//
// There is no C++ in this file. Every line the export produces comes from a
// template: one per generated file, and one under templates/parts/ for each
// thing a scene can contain -- a body, a shape of each kind, a joint of each
// type, a ray, an explosion, a rule. This script's job is to work out what
// goes in the holes: units converted, defaults applied, the editor's
// conventions honoured. To change what the exported code *looks* like, edit a
// template; to change what goes *into* it, edit this.
//
// `scene` carries the saved document (shapes, bodies, joints, rules, rays,
// explosions, world, field) plus three sections the editor works out:
//   scene.simulation  bodies and joints as the engine receives them -- body
//                     transforms resolved, geometry in body-local coordinates.
//                     Saved files keep shapes in scene coordinates, so this is
//                     the part worth reading.
//   scene.engine      what the loaded engine says it offers.
//   scene.settings    the application's own preferences, grouped as stored.
//   scene.converterSettings
//                     what this converter asked for in its manifest, as the
//                     user left it in Options -> Export, defaults applied.

var NEWLINE = String.fromCharCode(10);

// Set up by exportScene, so the part builders can reach them without every one
// of them taking the same three arguments.
var T = null;       // template loader
var PPM = 1000;     // scene units per metre
var MOTION = 0.05;  // 50 / PPM -- how the editor quotes speeds

function exportScene(scene, io) {
    var world = scene.world || {};
    var field = scene.field || {};
    var own = scene.converterSettings || {};

    PPM = world.pixelsPerMeter || 1000;
    // Physalis quotes speeds and accelerations at a reference of 50 px/m so
    // that the pace on screen does not change with the scale. Undone here,
    // once, so the numbers in Scene.cpp are already in Box2D's own units.
    MOTION = 50.0 / PPM;

    // Each template is read once and kept: a scene with two hundred shapes
    // would otherwise read the same file two hundred times.
    var cache = {};
    T = function (name) {
        if (!(name in cache))
            cache[name] = io.read("templates/" + name);
        return cache[name];
    };

    var project = safeName(own.projectName) || "PhysalisScene";

    // An empty prefix path means "let CMake find Qt", which is the right
    // default; a path given means the project configures without being told
    // where Qt is on the command line.
    var prefix = String(own.qtPath || "").trim();
    var qtPrefix = prefix
        ? ('set(CMAKE_PREFIX_PATH "' + prefix.split("\\").join("/") + '" ${CMAKE_PREFIX_PATH})'
           + NEWLINE)
        : "";

    io.write("CMakeLists.txt", fill(T("CMakeLists.txt.tmpl"), {
        PROJECT: project,
        QT_PREFIX: qtPrefix,
        CXX_STANDARD: String(own.cxxStandard || "17"),
        BOX2D_REPO: String(own.box2dRepository
                           || "https://github.com/erincatto/box2d.git").trim(),
        BOX2D_REF: String(own.box2dRef || "v3.1.1").trim(),
    }));
    io.write("main.cpp", fill(T("main.cpp.tmpl"), { TITLE: project }));

    // Copied out unchanged: the runtime is the same whatever the scene is.
    io.write("Scene.h", T("Scene.h.tmpl"));
    io.write("Viewport.h", T("Viewport.h.tmpl"));
    io.write("Viewport.cpp", T("Viewport.cpp.tmpl"));
    io.write("Rules.h", T("Rules.h.tmpl"));
    io.write("Rules.cpp", T("Rules.cpp.tmpl"));

    io.write("Scene.cpp", sceneCpp(scene, world, field, own));

    io.log("Exported " + scene.simulation.bodies.length + " bodies, "
           + scene.simulation.joints.length + " joints and "
           + (scene.rules || []).length + " rules.");
    io.log("Project " + project + ", C++" + String(own.cxxStandard || "17")
           + ", stepping at " + int(own.stepsPerSecond, 60) + " Hz"
           + (own.addControls ? ", with controls." : "."));
    return true;
}

// --- Scene.cpp -------------------------------------------------------------

function sceneCpp(scene, world, field, own) {
    var sim = scene.simulation;
    var colours = shapeColours(scene);
    // Always: a window showing mostly empty field is nobody's idea of a
    // preview. Falls back to the field only for a scene with nothing in it.
    var frame = objectBounds(scene);
    var physics = (scene.settings && scene.settings.Physics) || {};

    var bodies = [];
    for (var i = 0; i < sim.bodies.length; ++i)
        bodies.push(bodyCpp(sim.bodies[i], i, colours));

    var joints = [];
    for (var j = 0; j < sim.joints.length; ++j)
        joints.push(jointCpp(sim.joints[j]));

    return fill(T("Scene.cpp.tmpl"), {
        PIXELS_PER_METER: num(PPM),
        SETTINGS: fill(T("parts/settings.tmpl"), {
            PIXELS_PER_METER: num(PPM),
            SUB_STEP_COUNT: int(world.subStepCount, 4),
            MOTION_SCALE: num(MOTION),
            STEPS_PER_SECOND: int(own.stepsPerSecond, 60),
            SHOW_CONTROLS: bool(own.addControls),
            DEBUG_VIEW: bool(own.debugView),
            AXIS_LENGTH: num(pickNumber(physics.bodyAxisLength, 40)),
            AXIS_WIDTH: num(pickNumber(physics.bodyAxisWidth, 2)),
            AXIS_X_COLOR: physics.bodyAxisXColor || "#ffdc3232",
            AXIS_Y_COLOR: physics.bodyAxisYColor || "#ff28a03c",
            JOINT_COLOR: physics.jointColor || "#ffe8c46a",
            JOINT_ANCHOR_RADIUS: num(pickNumber(physics.jointAnchorRadius, 7)),
            WIDTH: num(frame ? frame.width : (field.width || 2000)),
            HEIGHT: num(frame ? frame.height : (field.height || 2000)),
            VIEW_CENTER_X: num(frame ? frame.centerX : 0),
            VIEW_CENTER_Y: num(frame ? frame.centerY : 0),
            BACKGROUND: field.backgroundColor || "#ffffffff",
        }),
        WORLD: worldCpp(world),
        BODIES: bodies.join(""),
        JOINTS: joints.length ? ("    // --- joints ---" + NEWLINE + joints.join("")) : "",
        FIELD_BOUNDS: fieldBoundsCpp(world, field),
        RAYS: raysCpp(scene),
        EXPLOSIONS: explosionsCpp(scene),
        RULES: rulesCpp(scene),
    });
}

function worldCpp(world) {
    var g = world.gravity || { x: 0, y: 9.81 };
    return fill(T("parts/world.tmpl"), {
        GRAVITY_X: speed(g.x),
        GRAVITY_Y: speed(g.y),
        MAX_LINEAR_SPEED: speed(pickNumber(world.maximumLinearSpeed, 400)),
        MAX_CONTACT_PUSH_SPEED: speed(pickNumber(world.maxContactPushSpeed, 3)),
        RESTITUTION_THRESHOLD: speed(pickNumber(world.restitutionThreshold, 1)),
        HIT_EVENT_THRESHOLD: speed(pickNumber(world.hitEventThreshold, 1)),
        CONTACT_HERTZ: num(pickNumber(world.contactHertz, 30)),
        CONTACT_DAMPING_RATIO: num(pickNumber(world.contactDampingRatio, 10)),
        ENABLE_SLEEP: bool(world.enableSleep),
        ENABLE_CONTINUOUS: bool(world.enableContinuous),
    });
}

// --- bodies and shapes -----------------------------------------------------

function bodyCpp(body, index, colours) {
    var v = body.linearVelocity || { x: 0, y: 0 };
    var shapes = [];
    for (var i = 0; i < body.parts.length; ++i)
        shapes.push(shapeCpp(body.parts[i], index, colours));

    return fill(T("parts/body.tmpl"), {
        NAME: body.name,
        TYPE: bodyType(body.type),
        POSITION_X: num(body.position.x),
        POSITION_Y: num(body.position.y),
        ROTATION: num(body.rotation),
        VELOCITY_X: speed(v.x),
        VELOCITY_Y: speed(v.y),
        ANGULAR_VELOCITY: num(body.angularVelocity),
        LINEAR_DAMPING: num(body.linearDamping),
        ANGULAR_DAMPING: num(body.angularDamping),
        GRAVITY_SCALE: num(body.gravityScale),
        ENABLE_SLEEP: bool(body.enableSleep),
        IS_AWAKE: bool(body.isAwake),
        SLEEP_THRESHOLD: speed(body.sleepThreshold),
        FIXED_ROTATION: bool(body.fixedRotation),
        IS_BULLET: bool(body.isBullet),
        ALLOW_FAST_ROTATION: bool(body.allowFastRotation),
        IS_ENABLED: bool(body.isEnabled),
        SHAPES: shapes.join(""),
    });
}

function shapeCpp(part, bodyIndex, colours) {
    var registration = part.name
        ? fill(T("parts/register.tmpl"), { NAME: part.name, BODY: String(bodyIndex) })
        : "";

    return fill(T("parts/shape.tmpl"), {
        DENSITY: num(part.density),
        FRICTION: num(part.friction),
        RESTITUTION: num(part.restitution),
        ROLLING_RESISTANCE: num(part.rollingResistance),
        TANGENT_SPEED: metres(part.tangentSpeed),
        CATEGORY_BITS: String(part.categoryBits),
        MASK_BITS: String(part.maskBits),
        GROUP_INDEX: String(part.groupIndex),
        IS_SENSOR: bool(part.isSensor),
        SENSOR_EVENTS: bool(part.enableSensorEvents),
        CONTACT_EVENTS: bool(part.enableContactEvents),
        HIT_EVENTS: bool(part.enableHitEvents),
        PRE_SOLVE_EVENTS: bool(part.enablePreSolveEvents),
        GEOMETRY: geometryCpp(part),
        REGISTER: registration,
        DRAWABLE: drawableCpp(part, bodyIndex, colours[part.name] || {}),
    });
}

function geometryCpp(part) {
    var c = part.center || { x: 0, y: 0 };

    if (part.kind === "box") {
        var hw = part.halfExtents.x, hh = part.halfExtents.y;
        var r = Math.max(0, Math.min(part.cornerRadius || 0, Math.min(hw, hh)));
        var rounded = r > 0;
        return fill(T(rounded ? "parts/geometry-box-rounded.tmpl"
                              : "parts/geometry-box.tmpl"), {
            ROTATION: num(part.rotation),
            HALF_WIDTH: metres(rounded ? Math.max(hw - r, 1e-5) : hw),
            HALF_HEIGHT: metres(rounded ? Math.max(hh - r, 1e-5) : hh),
            CENTER_X: num(c.x),
            CENTER_Y: num(c.y),
            CORNER_RADIUS: metres(r),
        });
    }

    if (part.kind === "circle") {
        return fill(T("parts/geometry-circle.tmpl"), {
            CENTER_X: num(c.x),
            CENTER_Y: num(c.y),
            RADIUS: metres(part.radius),
        });
    }

    var points = "";
    var pts = part.points || [];
    for (var i = 0; i < pts.length; ++i)
        points += "                m(" + num(pts[i].x) + ", " + num(pts[i].y) + ")," + NEWLINE;

    if (part.kind === "polygon")
        return fill(T("parts/geometry-polygon.tmpl"), { POINTS: points });

    return fill(T("parts/geometry-chain.tmpl"), {
        POINTS: points,
        LAST_EDGE: part.closed ? "count" : "count - 1",
    });
}

function drawableCpp(part, bodyIndex, colour) {
    var kinds = { box: "Box", circle: "Circle", polygon: "Polygon", chain: "Chain" };

    // A box or a circle is placed by its centre and its own rotation, and the
    // collision shape is built the same way. An outline is not: the editor has
    // already mapped every point into the body's frame, and its `center` is
    // only where the shape's pivot happens to sit. Applying that as well would
    // shift the outline by it and turn it twice.
    var outline = (part.kind === "polygon" || part.kind === "chain");
    var c = outline ? { x: 0, y: 0 } : (part.center || { x: 0, y: 0 });

    var points = "";
    var pts = part.points || [];
    for (var i = 0; i < pts.length; ++i) {
        points += "            drawn.points << QPointF(" + num(pts[i].x) + ", "
                  + num(pts[i].y) + ");" + NEWLINE;
    }

    return fill(T("parts/drawable.tmpl"), {
        BODY: String(bodyIndex),
        NAME: part.name || "",
        KIND: kinds[part.kind] || "Box",
        CENTER_X: num(c.x),
        CENTER_Y: num(c.y),
        ROTATION: num(outline ? 0 : part.rotation),
        HALF_WIDTH: num(part.halfExtents.x),
        HALF_HEIGHT: num(part.halfExtents.y),
        RADIUS: num(part.radius),
        CORNER_RADIUS: num(part.cornerRadius),
        CLOSED: bool(part.closed),
        POINTS: points,
        FILL: colour.fill || "#8055aaff",
        BORDER: colour.border || "#ff404040",
        BORDER_WIDTH: num(pickNumber(colour.borderWidth, 2)),
    });
}

// --- joints ----------------------------------------------------------------

function jointCpp(joint) {
    var p = joint.params || {};
    var anchors = joint.anchors || [];
    var a = anchors.length > 0 ? anchors[0] : { x: 0, y: 0 };
    var b = anchors.length > 1 ? anchors[1] : a;
    var axis = joint.axis || { x: 1, y: 0 };

    var tuning = "";
    if (has(p, "constraintHertz") || has(p, "constraintDampingRatio")) {
        tuning = fill(T("parts/joint-tuning.tmpl"), {
            HERTZ: num(pick(p, "constraintHertz", 60)),
            DAMPING_RATIO: num(pick(p, "constraintDampingRatio", 2)),
        });
    }

    return fill(T("parts/joint.tmpl"), {
        NAME: joint.name,
        TYPE: capitalise(joint.type),
        BODY_A: String(joint.bodyA),
        BODY_B: String(joint.bodyB),
        ANCHOR_A_X: num(a.x),
        ANCHOR_A_Y: num(a.y),
        ANCHOR_B_X: num(b.x),
        ANCHOR_B_Y: num(b.y),
        AXIS_X: num(axis.x),
        AXIS_Y: num(axis.y),
        DEF: jointDefCpp(joint, p),
        TUNING: tuning,
    });
}

function jointDefCpp(joint, p) {
    var type = joint.type;
    var collide = bool(joint.collideConnected);

    if (type === "revolute") {
        // Box2D clamps a revolute limit to just inside a half turn and asserts
        // if handed more, and it asserts on lower > upper. Both are put right
        // here rather than left to a solver that would abort the process.
        var angles = ordered(clampAngle(pick(p, "lowerAngle", 0)),
                             clampAngle(pick(p, "upperAngle", 0)));
        return fill(T("parts/joint-revolute.tmpl"), {
            REFERENCE_ANGLE: num(pick(p, "referenceAngle", 0)),
            TARGET_ANGLE: num(pick(p, "targetAngle", 0)),
            ENABLE_SPRING: bool(p.enableSpring),
            HERTZ: num(pick(p, "hertz", 0)),
            DAMPING_RATIO: num(pick(p, "dampingRatio", 0)),
            ENABLE_LIMIT: bool(p.enableLimit),
            LOWER_ANGLE: num(angles.lower),
            UPPER_ANGLE: num(angles.upper),
            ENABLE_MOTOR: bool(p.enableMotor),
            MAX_MOTOR_TORQUE: num(pick(p, "maxMotorTorque", 0)),
            MOTOR_SPEED: num(pick(p, "motorSpeed", 0)),
            DRAW_SIZE: num(pick(p, "drawSize", 0.25)),
            COLLIDE_CONNECTED: collide,
        });
    }

    if (type === "distance") {
        // Zero length means "however far apart the anchors already are", and
        // zero maxLength means unbounded -- Box2D's own default there is an
        // internal huge value with no public name.
        var length = pick(p, "length", 0);
        if (!(length > 0)) {
            var ends = joint.anchors || [];
            if (ends.length > 1) {
                var dx = ends[1].x - ends[0].x, dy = ends[1].y - ends[0].y;
                length = Math.sqrt(dx * dx + dy * dy);
            }
        }
        var maxLength = pick(p, "maxLength", 0);
        return fill(T("parts/joint-distance.tmpl"), {
            LENGTH: metres(Math.max(length, 0.001)),
            ENABLE_SPRING: bool(p.enableSpring),
            HERTZ: num(pick(p, "hertz", 0)),
            DAMPING_RATIO: num(pick(p, "dampingRatio", 0)),
            ENABLE_LIMIT: bool(p.enableLimit),
            MIN_LENGTH: metres(pick(p, "minLength", 0)),
            MAX_LENGTH: maxLength > 0 ? metres(maxLength) : "100000.0",
            ENABLE_MOTOR: bool(p.enableMotor),
            MAX_MOTOR_FORCE: num(pick(p, "maxMotorForce", 0)),
            MOTOR_SPEED: metres(pick(p, "motorSpeed", 0)),
            COLLIDE_CONNECTED: collide,
        });
    }

    if (type === "weld") {
        return fill(T("parts/joint-weld.tmpl"), {
            REFERENCE_ANGLE: num(pick(p, "referenceAngle", 0)),
            LINEAR_HERTZ: num(pick(p, "linearHertz", 0)),
            ANGULAR_HERTZ: num(pick(p, "angularHertz", 0)),
            LINEAR_DAMPING_RATIO: num(pick(p, "linearDampingRatio", 0)),
            ANGULAR_DAMPING_RATIO: num(pick(p, "angularDampingRatio", 0)),
            COLLIDE_CONNECTED: collide,
        });
    }

    if (type === "prismatic" || type === "wheel") {
        var span = ordered(pick(p, "lowerTranslation", 0), pick(p, "upperTranslation", 0));
        var wheel = type === "wheel";
        var values = {
            ENABLE_SPRING: bool(pick(p, "enableSpring", wheel)),
            HERTZ: num(pick(p, "hertz", wheel ? 1 : 0)),
            DAMPING_RATIO: num(pick(p, "dampingRatio", wheel ? 0.7 : 0)),
            ENABLE_LIMIT: bool(p.enableLimit),
            LOWER_TRANSLATION: metres(span.lower),
            UPPER_TRANSLATION: metres(span.upper),
            ENABLE_MOTOR: bool(p.enableMotor),
            COLLIDE_CONNECTED: collide,
        };
        if (wheel) {
            values.MAX_MOTOR_TORQUE = num(pick(p, "maxMotorTorque", 0));
            // Degrees: the template puts it through rad().
            values.MOTOR_SPEED = num(pick(p, "motorSpeed", 0));
            return fill(T("parts/joint-wheel.tmpl"), values);
        }
        values.REFERENCE_ANGLE = num(pick(p, "referenceAngle", 0));
        values.TARGET_TRANSLATION = metres(pick(p, "targetTranslation", 0));
        values.MAX_MOTOR_FORCE = num(pick(p, "maxMotorForce", 0));
        values.MOTOR_SPEED = metres(pick(p, "motorSpeed", 0));
        return fill(T("parts/joint-prismatic.tmpl"), values);
    }

    if (type === "motor") {
        return fill(T("parts/joint-motor.tmpl"), {
            LINEAR_OFFSET_X: num(pick(p, "linearOffsetX", 0)),
            LINEAR_OFFSET_Y: num(pick(p, "linearOffsetY", 0)),
            ANGULAR_OFFSET: num(pick(p, "angularOffset", 0)),
            MAX_FORCE: num(pick(p, "maxForce", 1)),
            MAX_TORQUE: num(pick(p, "maxTorque", 1)),
            CORRECTION_FACTOR: num(pick(p, "correctionFactor", 0.3)),
            COLLIDE_CONNECTED: collide,
        });
    }

    if (type === "mouse") {
        // The anchor is the target unless one was named outright.
        var tx = pick(p, "targetX", 0), ty = pick(p, "targetY", 0);
        var target = (tx === 0 && ty === 0)
            ? ((joint.anchors || [{ x: 0, y: 0 }])[0])
            : { x: tx, y: ty };
        return fill(T("parts/joint-mouse.tmpl"), {
            TARGET_X: num(target.x),
            TARGET_Y: num(target.y),
            HERTZ: num(pick(p, "hertz", 4)),
            DAMPING_RATIO: num(pick(p, "dampingRatio", 1)),
            MAX_FORCE: num(pick(p, "maxForce", 1)),
            COLLIDE_CONNECTED: collide,
        });
    }

    return T("parts/joint-filter.tmpl");
}

// --- the rest of the scene -------------------------------------------------

// Walls around the editor's field, when the scene asks for them. They are not
// bodies in the document -- the editor makes them when a run starts -- so a
// converter that skipped this would export an open world and let everything
// fall out of it.
function fieldBoundsCpp(world, field) {
    if (!world.solidBounds)
        return "";

    var w = field.width || 2000;
    var h = field.height || 2000;
    var t = 40;                         // the thickness the editor uses
    var left = -w / 2, top = -h / 2;

    var walls = [
        { x: left + w / 2, y: top - t / 2, hw: w / 2, hh: t / 2 },
        { x: left + w / 2, y: top + h + t / 2, hw: w / 2, hh: t / 2 },
        { x: left - t / 2, y: top + h / 2, hw: t / 2, hh: h / 2 },
        { x: left + w + t / 2, y: top + h / 2, hw: t / 2, hh: h / 2 },
    ];

    var out = "    // --- field bounds ---" + NEWLINE;
    for (var i = 0; i < walls.length; ++i) {
        out += fill(T("parts/field-bound.tmpl"), {
            X: num(walls[i].x),
            Y: num(walls[i].y),
            HALF_WIDTH: metres(walls[i].hw),
            HALF_HEIGHT: metres(walls[i].hh),
        });
    }
    return out + NEWLINE;
}

// Rangefinders. They are not in the world -- nothing collides with one -- so
// they are carried as data and cast every step by the rule runner.
function raysCpp(scene) {
    var rays = scene.rays || [];
    if (rays.length === 0)
        return "";

    var out = "    // --- rays ---" + NEWLINE;
    for (var i = 0; i < rays.length; ++i) {
        out += fill(T("parts/ray.tmpl"), {
            NAME: rays[i].name,
            X: num(rays[i].x),
            Y: num(rays[i].y),
            ANGLE: num(rays[i].angle),
            LENGTH: num(rays[i].length),
            // Saved as hex, and wider than a double counts exactly, so it goes
            // out as the literal it came in as.
            MASK_BITS: String(rays[i].maskBits || "ffffffffffffffff"),
        });
    }
    return out + NEWLINE;
}

function explosionsCpp(scene) {
    var explosions = scene.explosions || [];
    if (explosions.length === 0)
        return "";

    var out = "    // --- explosions ---" + NEWLINE;
    for (var i = 0; i < explosions.length; ++i) {
        out += fill(T("parts/explosion.tmpl"), {
            NAME: explosions[i].name,
            X: num(explosions[i].x),
            Y: num(explosions[i].y),
            PARAMS: variantMapCpp("        blast.params", explosions[i].params || {}),
        });
    }
    return out + NEWLINE;
}

// The rules, as rows rather than as code: Rules.cpp reads them, so a scene with
// forty rules is forty rows and not forty branches.
function rulesCpp(scene) {
    var rules = scene.rules || [];
    if (rules.length === 0)
        return "";

    var out = "    // --- rules ---" + NEWLINE;
    for (var i = 0; i < rules.length; ++i) {
        var r = rules[i];

        var condition = r.event
            ? field("rule.event", r.event)
            : field("rule.watch", r.watch || "") + field("rule.compare", r.compare || ">");

        var effect;
        if (r.action) {
            effect = field("rule.action", r.action)
                     + variantMapCpp("        rule.actionParams", r.actionParams || {});
        } else {
            effect = field("rule.property", r.property || "") + field("rule.op", r.op || "set");
            if (r.sourceObject) {
                effect += field("rule.sourceObject", r.sourceObject)
                          + field("rule.sourceProperty", r.sourceProperty || "")
                          + "        rule.sourceOffset = " + num(r.sourceOffset) + ";" + NEWLINE;
            } else {
                effect += "        rule.value = " + variantCpp(r.value) + ";" + NEWLINE;
            }
        }

        var flags = "";
        if (r.enabled === false)
            flags += "        rule.enabled = false;" + NEWLINE;
        if (r.once)
            flags += "        rule.once = true;" + NEWLINE;

        out += fill(T("parts/rule.tmpl"), {
            NAME: r.name || ("rule " + (i + 1)),
            SUBJECT: r.subject || "",
            CONDITION: condition,
            WHEN: variantCpp(r.when),
            TARGET: r.target || "",
            EFFECT: effect,
            FLAGS: flags,
        });
    }
    return out;
}

// One assignment of a string field, for the parts of a rule that vary in
// number rather than in shape.
function field(name, text) {
    return "        " + name + ' = "' + String(text).split('"').join('\\"') + '";' + NEWLINE;
}

// --- odds and ends ---------------------------------------------------------

// How each named shape is painted.
//
// The editor draws a physics scene by what a body *is*, not by the colours a
// shape was given while it was being drawn: dynamic in one colour, static in
// another, kinematic in a third, filled at a set transparency. Those are the
// application's own preferences rather than anything in the scene, which is
// why the converter is handed them. Without them -- an export from a fresh
// install with no settings file yet -- the shapes' own colours are used.
function shapeColours(scene) {
    var byName = {};
    var settings = (scene.settings && scene.settings.Physics) || {};

    var byType = {
        dynamic: settings.bodyDynamicColor,
        static: settings.bodyStaticColor,
        kinematic: settings.bodyKinematicColor,
    };
    var alpha = settings.fillAlpha;
    var width = settings.borderWidth;
    var haveTheme = !!(byType.dynamic || byType.static || byType.kinematic);

    var shapes = scene.shapes || [];
    for (var i = 0; i < shapes.length; ++i) {
        byName[shapes[i].name] = {
            fill: shapes[i].filled ? shapes[i].bodyColor : "#00000000",
            border: shapes[i].borderColor,
            borderWidth: shapes[i].borderWidth,
        };
    }
    if (!haveTheme)
        return byName;

    var bodies = (scene.simulation && scene.simulation.bodies) || [];
    for (var b = 0; b < bodies.length; ++b) {
        var outline = byType[bodies[b].type] || byType.dynamic;
        if (!outline)
            continue;
        var parts = bodies[b].parts || [];
        for (var p = 0; p < parts.length; ++p) {
            if (!parts[p].name)
                continue;
            byName[parts[p].name] = {
                fill: withAlpha(outline, alpha),
                border: outline,
                borderWidth: pickNumber(width, 2),
            };
        }
    }
    return byName;
}

// Qt writes colours as #aarrggbb and reads them back the same way, so putting
// a new alpha on one is a matter of replacing the first pair.
function withAlpha(colour, alpha) {
    var text = String(colour);
    if (alpha === undefined || alpha === null)
        return text;

    var value = Math.max(0, Math.min(255, Math.round(Number(alpha))));
    var hex = value.toString(16);
    if (hex.length < 2)
        hex = "0" + hex;

    if (text.length === 9 && text.charAt(0) === "#")   // #aarrggbb
        return "#" + hex + text.substring(3);
    if (text.length === 7 && text.charAt(0) === "#")   // #rrggbb
        return "#" + hex + text.substring(1);
    return text;
}

// A box around what the scene actually contains, with a small margin.
//
// Measured from real corners, not from a span either side of a centre: an
// outline's points are already in the body's frame and its `center` is only
// where the shape's pivot landed, so treating it as a box around that centre
// puts the edge of the picture somewhere the scene never reaches.
function objectBounds(scene) {
    var bodies = (scene.simulation && scene.simulation.bodies) || [];
    var minX = null, minY = 0, maxX = 0, maxY = 0;

    var see = function (x, y) {
        if (minX === null) {
            minX = maxX = x;
            minY = maxY = y;
            return;
        }
        minX = Math.min(minX, x); maxX = Math.max(maxX, x);
        minY = Math.min(minY, y); maxY = Math.max(maxY, y);
    };

    for (var b = 0; b < bodies.length; ++b) {
        var at = bodies[b].position || { x: 0, y: 0 };
        var bodyAngle = bodies[b].rotation || 0;
        var parts = bodies[b].parts || [];

        for (var p = 0; p < parts.length; ++p) {
            var corners = partCorners(parts[p]);
            for (var i = 0; i < corners.length; ++i) {
                // Body-local to scene: the body's own rotation, then where it
                // stands.
                var w = rotate(corners[i], bodyAngle);
                see(at.x + w.x, at.y + w.y);
            }
        }
    }
    if (minX === null)
        return null;

    // Enough that nothing sits hard against the edge, and enough for a body to
    // move a little before leaving the picture.
    var margin = 40;
    return {
        width: (maxX - minX) + margin * 2,
        height: (maxY - minY) + margin * 2,
        centerX: (minX + maxX) / 2,
        centerY: (minY + maxY) / 2,
    };
}

// The corners of one part, in its body's frame.
function partCorners(part) {
    var c = part.center || { x: 0, y: 0 };

    if (part.kind === "polygon" || part.kind === "chain")
        return part.points || [];   // already in the body's frame

    if (part.kind === "circle") {
        var r = part.radius || 0;
        return [{ x: c.x - r, y: c.y - r }, { x: c.x + r, y: c.y - r },
                { x: c.x + r, y: c.y + r }, { x: c.x - r, y: c.y + r }];
    }

    var hw = part.halfExtents.x || 0;
    var hh = part.halfExtents.y || 0;
    var out = [];
    var signs = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
    for (var i = 0; i < signs.length; ++i) {
        var corner = rotate({ x: signs[i][0] * hw, y: signs[i][1] * hh }, part.rotation || 0);
        out.push({ x: c.x + corner.x, y: c.y + corner.y });
    }
    return out;
}

function rotate(point, degrees) {
    if (!degrees)
        return point;
    var a = degrees * Math.PI / 180.0;
    var cos = Math.cos(a), sin = Math.sin(a);
    return { x: point.x * cos - point.y * sin, y: point.x * sin + point.y * cos };
}

// A QVariant of whichever kind the value actually is: a rule comparing a flag
// against a number would never be true.
function variantCpp(v) {
    if (v === null || v === undefined)
        return "QVariant()";
    if (typeof v === "boolean")
        return "QVariant(" + bool(v) + ")";
    if (typeof v === "number")
        return "QVariant(" + num(v) + ")";
    return 'QVariant(QString("' + String(v).split('"').join('\\"') + '"))';
}

function variantMapCpp(target, map) {
    var out = "";
    for (var key in map) {
        if (Object.prototype.hasOwnProperty.call(map, key))
            out += target + '.insert("' + key + '", ' + variantCpp(map[key]) + ");" + NEWLINE;
    }
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

// Box2D asserts on lower > upper, and an assert in a release solver aborts the
// process. A limit entered backwards means the same span either way round.
function ordered(lower, upper) {
    return lower > upper ? { lower: upper, upper: lower } : { lower: lower, upper: upper };
}

// Box2D clamps a revolute limit to just inside a half turn.
function clampAngle(degrees) { return Math.max(-178.0, Math.min(178.0, degrees || 0)); }

function bodyType(type) {
    if (type === "static") return "b2_staticBody";
    if (type === "kinematic") return "b2_kinematicBody";
    return "b2_dynamicBody";
}

function has(map, key) { return Object.prototype.hasOwnProperty.call(map, key); }
function pick(map, key, fallback) { return has(map, key) ? map[key] : fallback; }
function bool(v) { return v ? "true" : "false"; }

// Scene units to metres, and the editor's reference-scale speeds to Box2D's.
function metres(v) { return num((v || 0) / PPM); }
function speed(v) { return num((v || 0) * MOTION); }

// A number the settings may or may not have; a missing one is not zero.
function pickNumber(v, fallback) {
    var n = Number(v);
    return isFinite(n) && v !== null && v !== undefined && v !== "" ? n : fallback;
}

function int(v, fallback) {
    var n = Math.round(Number(v));
    return isFinite(n) ? String(n) : String(fallback);
}

// A plain decimal, never exponential -- C++ would take 1e-7 happily enough but
// the generated file is meant to be read.
function num(v) {
    var n = Number(v);
    if (!isFinite(n))
        n = 0;
    var text = n.toFixed(6);
    text = text.replace(/0+$/, "");
    if (text.charAt(text.length - 1) === ".")
        text += "0";
    return text;
}

function safeName(name) {
    if (!name)
        return "";
    return String(name).replace(/[^A-Za-z0-9_]/g, "_");
}

function capitalise(s) { return s.charAt(0).toUpperCase() + s.slice(1); }
