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

## Adding new formats

The export formats are small JavaScript plugins, each kept in its own folder
inside the folder set in **Options → Common → Converters folder**. A new format
can be added by placing a new folder there, without changing or rebuilding
Physalis. Each folder holds a short description of the format and the script
that writes it. Physalis gives the script the scene and a way to write files
into the folder you chose, and nothing else.
