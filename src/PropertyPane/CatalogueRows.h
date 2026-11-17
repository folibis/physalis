// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include "PropertyPane.h"
#include "JointTypes.h"

#include <QVariantMap>
#include <functional>
#include <vector>

// Property rows built from what an engine says an object has.
//
// The editor renders the descriptors and keeps the values by key. Which
// properties exist, what they are called, what they mean, what they start at
// and what they may be set to is the engine's alone -- this is the one place
// that turns its answer into a table, and it names none of them.
//
// One row may speak for several objects: it reads the first of the maps and
// writes all of them. That is what a table showing a selection does -- picking
// five shapes and setting the friction used to set it on one of them and leave
// the other four as they were, with nothing on screen saying so.
std::vector<PropertyRow> rowsFromCatalogue(const physics::PropertyList &properties,
                                           const std::vector<QVariantMap *> &values,
                                           const std::function<void()> &changed,
                                           const QString &fallbackSection);

// The rows a run fills in: what the engine can be asked about an object while
// the world exists, and nothing carries otherwise. Read-only, and shown only
// while a run is going -- outside one they would read a flat zero and look
// like measurements.
std::vector<PropertyRow> liveRowsFromCatalogue(const physics::PropertyList &properties,
                                               const QString &fallbackSection);
