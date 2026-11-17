---
title: Colour schemes and styles
nav_order: 10
---

# Colour schemes and styles

How the application itself looks is two separate choices in
**File → Options… → Common**, and either goes with any of the other:

- a **colour scheme** -- Light, Dark, Nord, Gruvbox, Solarized and so on, or
  **System** to follow the desktop;
- a **style** -- Rounded, Sharp, Fluent, Material or Minimal: the shape and
  density of the controls, how a button is filled, how a field shows that it
  has focus. **System** here means the platform's own controls, with nothing of
  ours laid over them.

Neither is built into the program. Both are plain files that Physalis reads
when it starts, so **adding a colour scheme or a style is adding a file** -- no
rebuilding, no settings to edit.

This covers the windows, panels, menus and dialogs only. The **field** -- its
background, its grid, and the colours bodies and joints are drawn in -- belongs
to the scene and to **Options → Common** and **Options → Physics**, and is not
touched by any of this.

## Where the files live

Physalis reads three folders, in this order. A file in a later one replaces a
file of the same name in an earlier one:

1. the `themes` folder installed with Physalis -- beside the program on Windows
   and macOS, `/usr/share/physalis/themes` from a Linux package;
2. a `themes` folder beside your own settings file:
   `%LOCALAPPDATA%\Physalis\themes` on Windows, `~/.config/Physalis/themes` on
   Linux;
3. whatever the `PHYSALIS_THEMES` environment variable names.

**Put your own files in (2).** The installed folder may be overwritten the next
time you update Physalis, and on Windows it is usually somewhere you cannot
write to anyway.

Because a later folder wins, a file in (2) named after one that ships -- say
`nord.json` -- **redraws that scheme** rather than appearing beside it. That is
how you adjust a scheme you almost like without copying it under a new name.

The folder looks like this:

```
themes/
    base.qss                 the style sheet every style starts from
    schemes/
        nord.json            one colour scheme
        ...
    styles/
        sharp.json           one control style
        snippets/
            underline-tabs.qss
```

**A file's name is its name.** `nord.json` is the scheme stored in your
settings as `nord`. Renaming the file is renaming the scheme, and anything that
was using it falls back to System.

The files are read **when Physalis starts**, so restart it after adding or
changing one.

## A colour scheme

Ten colours, and everything else -- the shades for hovering, for disabled text,
for a selected tab, for the border of an unfinished rule -- is worked out from
them. That is why a scheme is this short:

```json
{
    "label": "Nord",
    "order": 110,
    "dark": true,
    "window": "#3B4252",
    "surface": "#2E3440",
    "base": "#2E3440",
    "alternate": "#434C5E",
    "text": "#ECEFF4",
    "muted": "#A3ABB9",
    "border": "#56607A",
    "soft": "#4C566A",
    "accent": "#88C0D0",
    "accentText": "#2E3440"
}
```

### Fields

| Field | What it is |
| --- | --- |
| `label` | What **Options** shows in the list. Without one, the file's name is used. |
| `order` | Where it sits in that list. A scheme without one goes after the others, in alphabetical order. |
| `dark` | Whether this is a dark scheme. The arrows and the checkbox tick are drawn in light ink when it is true, dark ink when it is false, and several of the worked-out shades follow it. |
| `window` | Panels and dialogs: the colour most of the window is. |
| `surface` | The bars around them -- the toolbar, the menu bar, the status bar. |
| `base` | Fields, lists and cards: what you type into and read from. |
| `alternate` | Every other row of a list or table. |
| `text` | What is written, on `window` and on `base` alike. |
| `muted` | Notes, hints and secondary labels. |
| `border` | The edge of a field or a button. |
| `soft` | Dividers, and borders that should not be noticed. |
| `accent` | The selection, a checked tool button, the tab you are on, a link. |
| `accentText` | What is written **on** the accent -- so it must read against it. |

All ten colours are needed. A scheme missing one is left out of the list
rather than shown half-dressed. Any form Qt understands will do: `#RRGGBB`,
`#AARRGGBB`, or a name such as `midnightblue`.

Two more are optional. `error` and `errorText` are the red a rule is marked in
when it is unfinished. Left out, a dark scheme gets `#E06C75` and `#F28B82`, a
light one `#D9534F` and `#A33A35` -- a warning that reads as a warning in any
palette.

### Example: your own colour scheme

1. Make the folder `%LOCALAPPDATA%\Physalis\themes\schemes`
   (`~/.config/Physalis/themes/schemes` on Linux).
2. Put a file in it called `midnight.json`:

   ```json
   {
       "label": "Midnight",
       "dark": true,
       "window": "#11131A",
       "surface": "#0B0D12",
       "base": "#0B0D12",
       "alternate": "#1B1F2A",
       "text": "#D7DCE8",
       "muted": "#7C879C",
       "border": "#2E3444",
       "soft": "#1B1F2A",
       "accent": "#FFB454",
       "accentText": "#11131A"
   }
   ```

3. Restart Physalis and choose **Midnight** in **Options → Common → Colour
   scheme**. The whole window follows at once, before you press OK.

The quickest way to a scheme you like is to copy one of the installed files
next to your own and change the ten values. Keep `accentText` readable on
`accent` -- it is what a selected row is written in -- and keep `text` well
clear of `window`.

## A style

A style is the shape of the controls, apart from their colour. Most of it is a
handful of numbers that are put into the shared style sheet; the rest is
whatever rules that style wants of its own.

```json
{
    "label": "Sharp",
    "order": 20,
    "radius": {
        "field": 0, "small": 0, "check": 0, "handle": 0,
        "card": 0, "scroll": 0, "spin": 0
    },
    "padding": { "field": "1px 5px", "button": "3px 10px" },
    "include": ["underline-tabs.qss"],
    "sheet": [
        "QPushButton { background: palette(base); border: 1px solid @borderStrong; }",
        "QGroupBox { border: 1px solid @borderStrong; }"
    ]
}
```

### Fields

| Field | What it is |
| --- | --- |
| `label`, `order` | As for a colour scheme. |
| `radius` | Corner radii in pixels, one per part. Each key fills a placeholder: `field` fills `@rfield`, `card` fills `@rcard`, and so on. |
| `padding` | The space inside a field and inside a button, written as a CSS padding -- `"5px 16px"`. `field` fills `@padfield` and `button` fills `@padbutton`. |
| `include` | Files in `styles/snippets/` to lay down before this style's own rules, for a run of rules that two styles share. Optional. |
| `sheet` | This style's own rules, as one string or as a list of lines. Optional. |

The rules in `sheet` come **after** the shared ones, so where both say
something about the same control, this style wins. They are ordinary
[Qt style sheets](https://doc.qt.io/qt-6/stylesheet-syntax.html) -- the same
language as CSS, with Qt's own widget names as selectors.

`radius.card` is also what the panels that paint their own cards ask for, so it
reaches a little further than the style sheet does.

**Give every placeholder a value.** The shared sheet uses all seven radii and
both paddings, and one left out means Qt throws away the whole rule it appeared
in -- usually something small and hard to spot. Physalis prints a message
naming what was missing, so start from a copy of an installed style rather than
from an empty file.

### The placeholders

Anything written `@name` in `base.qss` or in a style's `sheet` is filled in
before the sheet is used.

From the style itself:

| Placeholder | From |
| --- | --- |
| `@rfield`, `@rsmall`, `@rcheck`, `@rhandle`, `@rcard`, `@rscroll`, `@rspin` | the matching key in `radius`, in pixels |
| `@padfield`, `@padbutton` | the matching key in `padding`, as written |

From the colour scheme in use -- these are the shades worked out from its ten
colours, so a style never names a colour of its own:

| Placeholder | What it is |
| --- | --- |
| `@surface` | the toolbar, menu bar and status bar |
| `@hover` | under the pointer |
| `@accent` | the accent, lifted towards white on a dark scheme |
| `@accentSoft` | a selected tab, a checked tool button |
| `@accentHover` | a default button under the pointer |
| `@borderStrong` | the edge of a field the pointer is over |
| `@muted` | notes and secondary labels |
| `@faint` | the same, on something switched off |
| `@tipBase`, `@tipText` | a tooltip and its text |
| `@ink` | `light` or `dark`: which set of arrow images to use |
| `@tick` | the same for the checkbox tick, chosen to read on the accent |

Everything else should be written as `palette(window)`, `palette(base)`,
`palette(highlight)` and so on. That way a widget that has been handed a
palette of its own -- the caption and the scene's controls, which stand on the
field rather than on a panel -- is drawn in that one instead of in the
application's.

### Example: your own style

A style that is only a different corner radius is a name in a list and little
else, so change the padding and a control or two as well:

1. Make the folder `%LOCALAPPDATA%\Physalis\themes\styles`.
2. Put a file in it called `soft.json`:

   ```json
   {
       "label": "Soft",
       "radius": {
           "field": 10, "small": 9, "check": 5, "handle": 9,
           "card": 16, "scroll": 5, "spin": 10
       },
       "padding": { "field": "6px 12px", "button": "8px 20px" },
       "sheet": [
           "QPushButton { font-weight: 600; min-height: 20px; }",
           "QGroupBox { border: none; background: palette(base);",
           "            margin-top: 16px; padding: 12px 10px 10px 10px; }",
           "QLineEdit, QAbstractSpinBox, QComboBox {",
           "            background: palette(alternate-base); border-color: transparent; }",
           "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus {",
           "            background: palette(base); border-color: palette(highlight); }"
       ]
   }
   ```

3. Restart Physalis and choose **Soft** in **Options → Common → Style**. It
   works with every colour scheme, because it names no colour of its own.

## When something does not appear

- **A scheme or style is missing from the list.** One of its colours is missing
  or misspelled, or the file is not valid JSON. Physalis says which file and
  which field on the console -- start it from a terminal to see the message.
- **Most of it works but one control looks wrong.** A placeholder nothing fills
  was used, and Qt dropped the rule it was in. The message names the style and
  the placeholder.
- **The file changed and nothing happened.** The files are read when Physalis
  starts. Restart it.
- **Two files with the same name.** The one in the folder read later wins --
  see [Where the files live](#where-the-files-live). This is deliberate: it is
  how you redraw a scheme that ships.
