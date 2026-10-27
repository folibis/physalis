box2d3wasm
==================

Turns a Physalis scene into one web page that runs it: index.html, a canvas,
and Box2D compiled to WebAssembly.

What comes out
--------------

    index.html          the whole page -- the scene, the rules, the drawing,
                        and (by default) the engine itself

Where the engine comes from is a setting. Built into the page, the wasm is
carried inside index.html as base64 and the page opens straight from disk;
beside the page, it is one box2d3wasm.js next to it; from a CDN, the page
imports it, which needs a server and a network.

index.html reads like the Box2D manual, in JavaScript:

    createWorld()   b2CreateWorld, then every body, shape and joint, each id
                    kept in a variable named after the object in the editor
                    (body_1, circle_1, revolute_2 ...). A def field is written
                    only when it is not already Box2D's default.

    step(dt)        b2World_Step, then what the scene's rules say, as ordinary
                    if statements calling Box2D. A rule on an event -- two
                    shapes starting to touch, a sensor being entered -- is a
                    loop over Box2D's own event arrays, and is carried out on
                    every step the event is raised, as it is in the editor. A
                    rule on a value acts on the step the value first crosses,
                    which is what the bool beside it remembers.

    the page        what the scene carries that Box2D knows nothing about: the
                    body the scene keeps in view, which the view follows; the
                    log of watched values, painted on the canvas in the corner
                    the editor keeps it in; the caption a rule puts up; the
                    scene's own controls, as real HTML over the canvas, which
                    the rules read and write through a `controls` object; and
                    the keys a rule watches, named as the editor names them
                    ("Enter", "Up", "Ctrl+A") by chordOf(). A key the scene
                    uses is left to the scene -- the page's own Space, R and D
                    stand aside for it.

    drawing         canvas 2D, shape by shape, in the editor's own colours:
                    sensors hatched, joints and body axes drawn the way the
                    editor draws them, and a slingshot for bodies that can be
                    flung.

Contact events: Box2D leaves them off, so a shape that an event rule watches
is given enableContactEvents -- the editor switches it on for those shapes when
a run starts, and without it the rule never fires.

Nothing of the editor comes along: no scene description, no rules as data.

Units
-----

Box2D's tolerances are lengths fixed in metres: 5 mm of slop, a contact made at
four times that. The editor shrinks them with b2SetLengthUnitsPerMeter, which
box2d3wasm does not expose -- so the page picks its metre instead, the one at
which that fixed 2 cm is exactly the contact margin the scene asked for.
Lengths, speeds and gravity are divided by that number rather than by the
scene's own scale, and density by its square, so every mass, force and motion
comes out the same as it is in the editor.

Settings
--------

Options -> Export -> box2d3wasm:

    Project name          the <title> of the page
    Engine                built into the page, beside it, or from a CDN
    CDN address           the module to import when it comes from a CDN
    Add controls          a strip with Start, Pause and Reset above the canvas
    Debug view            starts with joints and centres of mass shown, and a
                          switch for them
    Pan and zoom          whether dragging and the wheel move the view
    Steps per second      how often the page steps the world
    Sub-steps             how many solver iterations each step takes

Opening it
----------

Built into the page, index.html opens from disk. Otherwise it needs a server,
since a browser refuses to fetch anything beside a file:// page:

    python tools/view_exports.py        (from the Physalis sources)
    python -m http.server               (from the folder itself)

What is not carried
-------------------

A rule Box2D has nothing for stays in step() as a comment saying so, and the
export says which in its message at the end. A joint that holds a body to a
point in the world (a mouse joint) is not exported.
