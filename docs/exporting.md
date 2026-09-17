---
title: Exporting
nav_order: 8
---

# Exporting

**File → Export to** turns a scene into a standalone program that runs it
without Physalis.

| Converter | What it writes |
|---|---|
| **Box2D / Qt project** | A CMake project: a Qt window that fetches Box2D, builds the scene and runs it. Rules become code |
| **Planck.js / web page** | A single `index.html` that runs the scene in the browser with Planck.js |

Choose a converter, then the folder to write into. When it finishes, a dialog
lists the files written, or says what went wrong.

## Converter settings

Each converter has its own settings in **Options → Export**, one page per
converter.

**Box2D / Qt project:**
Project name, Add controls, Qt prefix path, Steps per second, Debug view,
Box2D repository, Box2D tag or branch, C++ standard.

**Planck.js / web page:**
Project name, Add controls, Debug view, Steps per second, where Planck.js comes
from, Planck.js URL, version, repository, and tag or branch.

## Where converters come from

Converters are found in the folder set in **Options → Common → Converters
folder**. Each converter is a folder with a `manifest.json` and an
`export.js`, so new ones can be added without rebuilding Physalis.
