// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QPolygonF>
#include <QVector>

namespace ConvexPieces {

// A closed outline cut into convex pieces of at most `maxCorners` corners each,
// together covering exactly what the outline covered. Triangulated by ear
// clipping, then neighbouring pieces merged back wherever the union is still
// convex and within the limit (Hertel-Mehlhorn). Empty when the outline crosses
// itself or encloses no area.
QVector<QPolygonF> split(const QPolygonF &outline, int maxCorners);

} // namespace ConvexPieces
