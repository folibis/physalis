# Themes

How the application's own widgets look, read when Physalis starts. Two
independent choices in Options -> Common: a **colour scheme** (`schemes/*.json`,
ten colours each) and a **style** (`styles/*.json`, the shape and density of the
controls), over one shared `base.qss`.

**The full reference is [docs/themes.md](../docs/themes.md)** -- the fields, the
placeholders, and a worked example of each. It is the same page Help ->
Documentation shows.

Three things worth knowing before editing anything here:

- **A file's name is its key.** `nord.json` is the scheme the settings file
  calls `nord`; nothing inside the file can disagree about which scheme it is.
- **Put your own files beside your settings, not here**
  (`%LOCALAPPDATA%\Physalis\themes` or `~/.config/Physalis/themes`). That folder
  is read after this one, so a file named after one of these redraws it, and
  nothing is lost when Physalis is updated.
- **The canvas is not part of this.** The field's background, the grid and the
  colours bodies and joints are drawn in belong to the scene and to Options.
