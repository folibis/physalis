---
title: Exporting
nav_order: 8
---

# Exporting

A scene can be exported as a standalone program that runs it without
Physalis. The program builds the same bodies, joints and settings, and your
rules become ordinary program code. This is useful when a scene should become
part of a game or an application, or be shown to someone who does not have
Physalis.

To export, choose **File → Export to** and pick a format. Then choose the folder
to write the result to. When the export finishes, a message lists the files
that were written or explains what went wrong.

## Available formats

- **Box2D / Qt project** writes a C++ project: a small Qt application that
  downloads Box2D, builds the scene and runs it in a window. Open the project
  in Qt Creator, or build it with CMake, like any other C++ project.
- **Planck.js / web page** writes a single `index.html` file that runs the
  scene in a web browser, using Planck.js, a JavaScript version of Box2D.
  Open the file in a browser, or put it on any website.

## Export settings

Each format has its own settings in **Options → Export**, one page per format.
For example:

- the name of the project or page;
- whether the result has buttons to start, pause and restart the simulation;
- whether helpers such as joints and centres of mass are drawn;
- how many simulation steps are calculated per second;
- which version of Box2D or Planck.js to use, and where to get it from.

## Writing your own exporter

An exporter (also called a converter) is a folder containing two files:

- **`manifest.json`** describes the exporter: its name, a description, and the
  settings it wants to show in **Options → Export**.
- **`export.js`** is the script that does the work. It is written in plain
  JavaScript and defines one function, `exportScene(scene, io)`.

The folder may also hold anything else the exporter needs, such as templates
of the files it writes. Place the folder inside the **Converters folder** set in
**Options → Common**. It appears in **File → Export to** the next time the menu
is opened, without restarting Physalis. The folder's name identifies the
exporter, and its settings are stored under that name.

Physalis reads the manifest to build the menu and the options page, but it
never runs a script until you actually choose that exporter.

### manifest.json

```json
{
    "name": "Bodies as CSV",
    "description": "A table of the scene's bodies, one row per body.",
    "settings": [
        {
            "key": "separator",
            "label": "Separator",
            "type": "choice",
            "choices": [",", ";", "tab"],
            "default": ",",
            "tooltip": "What goes between the columns."
        },
        {
            "key": "includeStatic",
            "label": "Include static bodies",
            "type": "bool",
            "default": true
        }
    ]
}
```

- **`name`** is what the **Export to** menu shows, and **`description`** is
  its tooltip.
- **`settings`** is optional. Each entry becomes one row on the exporter's page
  in **Options → Export**:
  - `key`: the name the script uses to read the value;
  - `label` and `tooltip`: what the options page shows;
  - `type`: one of `string`, `int`, `double`, `bool`, `color`, `choice`, `path`
    or `file`. An unknown type is shown as a text box;
  - `default`: the value used until the user changes it;
  - `min` and `max` for numbers, `decimals` for `double`, and `choices` for
    `choice`.

### export.js

When the user exports, Physalis runs `export.js` and calls
`exportScene(scene, io)` once. The export succeeds if the function returns
normally and has written at least one file. It fails if the function throws an
error or writes nothing. In that case the user sees the error message and the
line it came from.

**`io`** is the script's only way to reach files:

- `io.write(path, text)` writes a text file into the folder the user chose.
  `path` is relative to that folder and may contain sub-folders, such as
  `"src/main.cpp"`; missing folders are created. Writing outside the chosen
  folder is refused.
- `io.read(path)` reads a text file from the exporter's own folder, such as a
  template. Reading outside that folder is refused.
- `io.log(message)` adds a line to the message shown when the export
  finishes. Use it to tell the user what was done, or what could not be
  converted.

`console.log(...)` also works, and prints to the application's debug output
while you develop the exporter.

**`scene`** is the whole scene as a JavaScript object. It contains everything a
`.phys` file holds, plus a few additions:

- `scene.field`: the field's `width`, `height`, `backgroundColor`, `showGrid`,
  `gridCellSize` and `gridColor`.
- `scene.world`: `pixelsPerMeter`, `solidBounds`, and `physics`, the world's
  engine settings such as gravity.
- `scene.shapes`, `scene.bodies`, `scene.joints`, `scene.rules`,
  `scene.rays` and `scene.explosions`: the objects as saved in the file.
- `scene.simulation`: the bodies and joints **exactly as the physics engine
  receives them**. This is usually the easier part to work from:
  - `simulation.bodies[]`: `name`, `type` (`"static"`, `"kinematic"` or
    `"dynamic"`), `position` (`{x, y}`), `rotation` (degrees), `centerOfMass`,
    `isEnabled`, `physics` (the body's properties), and `parts[]`, its shapes.
    Each part has a `name`, a `kind` (`"box"`, `"circle"`, `"polygon"` or
    `"chain"`), its size (`halfExtents`, `radius` or `points`), `center` and
    `rotation` relative to the body, and `physics` (the shape's properties).
  - `simulation.joints[]`: `name`, `type`, `bodyA` and `bodyB` (indexes into
    `simulation.bodies`), `anchors`, `axis`, `collideConnected` and
    `params`.
- `scene.engine`: the description of the physics engine the scene uses, which
  lists every body, shape and world property, joint type, event and action,
  with each one's `key`, `label`, `default`, `min`, `max` and more.
- `scene.converterSettings`: this exporter's own settings, keyed by the `key`
  in the manifest, with defaults already filled in.
- `scene.settings`: the application's options, such as the colours bodies are
  drawn in.

Things to keep in mind:

- **Units.** Positions and sizes are in field units (pixels), with Y pointing
  down, and angles are in degrees. Divide by `scene.world.pixelsPerMeter` to
  get metres.
- **Only changed properties are stored.** A body's or shape's `physics` object
  holds only the values that differ from the engine's defaults. Look the
  others up in `scene.engine`, as the example below does.

### Example: bodies as a CSV table

This exporter writes `bodies.csv`, one line per body, with its name, type,
position, rotation and the density of its first shape. Together with the
manifest above, it is a complete, working exporter.

```js
// The value of a property: the stored one if the user changed it, and
// otherwise the engine's default.
function valueOf(values, key, catalogue) {
    if (values && key in values)
        return values[key];
    for (var i = 0; i < catalogue.length; ++i) {
        if (catalogue[i].key === key)
            return catalogue[i]["default"];
    }
    return undefined;
}

function exportScene(scene, io) {
    var options = scene.converterSettings;
    var sep = options.separator === "tab" ? "\t" : options.separator;
    var shapeCatalogue = scene.engine.shapeProperties;

    var lines = [["name", "type", "x", "y", "rotation", "density"].join(sep)];
    var bodies = scene.simulation.bodies;
    for (var i = 0; i < bodies.length; ++i) {
        var body = bodies[i];
        if (body.type === "static" && !options.includeStatic)
            continue;
        var firstPart = body.parts[0];
        var density = valueOf(firstPart.physics, "density", shapeCatalogue);
        lines.push([body.name, body.type,
                    body.position.x.toFixed(1), body.position.y.toFixed(1),
                    body.rotation.toFixed(1), density].join(sep));
    }

    io.write("bodies.csv", lines.join("\n") + "\n");
    io.log("Wrote " + (lines.length - 1) + " bodies.");
}
```

To try it, create a folder named `bodies-csv` in the Converters folder, save
the manifest and the script in it as `manifest.json` and `export.js`, and
choose **File → Export to → Bodies as CSV**.

For larger examples, look at the two exporters that come with Physalis, in the
`box2d-qt-project` and `planck-js` folders. They read templates with
`io.read`, turn the rules into code, and write several files.
