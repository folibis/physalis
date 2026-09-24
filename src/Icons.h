// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QIcon>

namespace Icons {

// The application itself, for the title bar and the task bar.
QIcon app();

QIcon newScene();
QIcon loadScene();
QIcon saveScene();
QIcon exportScene();

QIcon add();
QIcon rectangle();
QIcon circle();
QIcon polygon();
QIcon move();
QIcon edit();
QIcon rotate();
QIcon deleteShape();
QIcon resetScale();
QIcon createBody();
QIcon dissolveBody();
QIcon simulate();
QIcon stop();
QIcon pause();
QIcon step();
QIcon resetValue();
QIcon body();
QIcon joint();
QIcon explosion();
QIcon world();
QIcon ray();
// An incomplete rule: the card wears it, and so does the Rules tab.
QIcon warning();
// The scene's own variables, as one object in the rule lists.
QIcon variable();
// The readout of watched values, pinned to the canvas during a run.
QIcon log();

} // namespace Icons
