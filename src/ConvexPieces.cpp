// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "ConvexPieces.h"

#include <QHash>
#include <QLineF>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace ConvexPieces {

namespace {

qreal cross(const QPointF &o, const QPointF &a, const QPointF &b)
{
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

qreal signedArea(const QPolygonF &points)
{
    qreal twice = 0.0;
    for (int i = 0; i < points.size(); ++i) {
        const QPointF &a = points[i];
        const QPointF &b = points[(i + 1) % points.size()];
        twice += a.x() * b.y() - b.x() * a.y();
    }
    return twice / 2.0;
}

// Repeated points and corners that are not corners: both make ears of no area.
QPolygonF cleaned(const QPolygonF &outline, qreal eps)
{
    QPolygonF points = outline;
    bool changed = true;
    while (changed && points.size() >= 3) {
        changed = false;
        for (int i = 0; i < points.size() && points.size() >= 3; ++i) {
            const int n = points.size();
            const QPointF &prev = points[(i + n - 1) % n];
            const QPointF &here = points[i];
            const QPointF &next = points[(i + 1) % n];
            if (QLineF(prev, here).length() < 1e-9 || qAbs(cross(prev, here, next)) <= eps) {
                points.remove(i);
                changed = true;
                --i;
            }
        }
    }
    return points;
}

bool segmentsCross(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d)
{
    const qreal d1 = cross(c, d, a);
    const qreal d2 = cross(c, d, b);
    const qreal d3 = cross(a, b, c);
    const qreal d4 = cross(a, b, d);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0))
           && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

bool crossesItself(const QPolygonF &points)
{
    const int n = points.size();
    for (int i = 0; i < n; ++i) {
        for (int j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1)
                continue;
            if (segmentsCross(points[i], points[(i + 1) % n], points[j], points[(j + 1) % n]))
                return true;
        }
    }
    return false;
}

bool insideOrOn(const QPointF &p, const QPointF &a, const QPointF &b, const QPointF &c, qreal eps)
{
    return cross(a, b, p) >= -eps && cross(b, c, p) >= -eps && cross(c, a, p) >= -eps;
}

// Triangles as indices into `points`, which run anticlockwise.
QVector<QVector<int>> earClip(const QPolygonF &points, qreal eps)
{
    QVector<int> left;
    for (int i = 0; i < points.size(); ++i)
        left.append(i);

    // The ear with the widest smallest angle, not the first one found: the
    // first one fans every triangle out of one corner, and slivers merge badly
    // and make thin pieces, which are what a solver handles worst.
    const auto smallestAngle = [&points](int p, int c, int q) {
        const QPointF corners[3] { points[p], points[c], points[q] };
        qreal smallest = M_PI;
        for (int i = 0; i < 3; ++i) {
            const QPointF u = corners[(i + 1) % 3] - corners[i];
            const QPointF v = corners[(i + 2) % 3] - corners[i];
            const qreal angle = std::atan2(qAbs(u.x() * v.y() - u.y() * v.x()),
                                           u.x() * v.x() + u.y() * v.y());
            smallest = qMin(smallest, angle);
        }
        return smallest;
    };

    QVector<QVector<int>> triangles;
    while (left.size() > 3) {
        int best = -1;
        qreal bestAngle = -1.0;
        const int n = left.size();
        for (int k = 0; k < n; ++k) {
            const int p = left[(k + n - 1) % n];
            const int c = left[k];
            const int q = left[(k + 1) % n];
            if (cross(points[p], points[c], points[q]) <= eps)
                continue;
            bool empty = true;
            for (int v : std::as_const(left)) {
                if (v == p || v == c || v == q)
                    continue;
                if (insideOrOn(points[v], points[p], points[c], points[q], eps)) {
                    empty = false;
                    break;
                }
            }
            if (!empty)
                continue;
            const qreal angle = smallestAngle(p, c, q);
            if (angle > bestAngle) {
                bestAngle = angle;
                best = k;
            }
        }
        if (best < 0)
            return {};
        triangles.append({ left[(best + n - 1) % n], left[best], left[(best + 1) % n] });
        left.remove(best);
    }
    triangles.append(left);
    return triangles;
}

bool isConvex(const QVector<int> &piece, const QPolygonF &points, qreal eps)
{
    const int n = piece.size();
    for (int i = 0; i < n; ++i) {
        if (cross(points[piece[(i + n - 1) % n]], points[piece[i]], points[piece[(i + 1) % n]]) < -eps)
            return false;
    }
    return true;
}

// Corners that stay corners once the straight-through ones are dropped.
int cornerCount(const QVector<int> &piece, const QPolygonF &points, qreal eps)
{
    const int n = piece.size();
    int corners = 0;
    for (int i = 0; i < n; ++i) {
        if (cross(points[piece[(i + n - 1) % n]], points[piece[i]], points[piece[(i + 1) % n]]) > eps)
            ++corners;
    }
    return corners;
}

// The union of two pieces that share the edge a->b in `first` and b->a in
// `second`, or an empty list when they share no edge.
QVector<int> joined(const QVector<int> &first, const QVector<int> &second)
{
    const int n = first.size();
    const int m = second.size();
    for (int i = 0; i < n; ++i) {
        const int a = first[i];
        const int b = first[(i + 1) % n];
        for (int j = 0; j < m; ++j) {
            if (second[j] != b || second[(j + 1) % m] != a)
                continue;
            QVector<int> merged;
            merged.reserve(n + m - 2);
            for (int k = 1; k <= n; ++k)
                merged.append(first[(i + k) % n]);
            for (int k = 2; k < m; ++k)
                merged.append(second[(j + k) % m]);
            return merged;
        }
    }
    return {};
}

} // namespace

QVector<QPolygonF> split(const QPolygonF &outline, int maxCorners)
{
    maxCorners = qMax(3, maxCorners);

    QPolygonF points = outline;
    if (points.size() > 1 && points.first() == points.last())
        points.removeLast();

    const QRectF bounds = points.boundingRect();
    const qreal size = qMax(bounds.width(), bounds.height());
    if (size <= 0.0)
        return {};
    // A cross product is an area, so the tolerance scales with the square.
    const qreal eps = size * size * 1e-9;

    points = cleaned(points, eps);
    if (points.size() < 3 || crossesItself(points))
        return {};
    if (signedArea(points) < 0.0)
        std::reverse(points.begin(), points.end());

    QVector<QVector<int>> pieces = earClip(points, eps);
    if (pieces.isEmpty())
        return {};

    // The longest cut goes first: taking the long ones out leaves the short
    // ones round the edges, which keeps the pieces fat rather than thin.
    const qint64 count = points.size();
    for (;;) {
        QHash<qint64, int> owner;
        for (int i = 0; i < pieces.size(); ++i) {
            const QVector<int> &piece = pieces[i];
            for (int k = 0; k < piece.size(); ++k)
                owner.insert(piece[k] * count + piece[(k + 1) % piece.size()], i);
        }

        int keep = -1;
        int drop = -1;
        qreal longest = -1.0;
        QVector<int> best;
        for (int i = 0; i < pieces.size(); ++i) {
            const QVector<int> &piece = pieces[i];
            for (int k = 0; k < piece.size(); ++k) {
                const int a = piece[k];
                const int b = piece[(k + 1) % piece.size()];
                const int j = owner.value(b * count + a, -1);
                if (j <= i)
                    continue;
                const qreal length = QLineF(points[a], points[b]).length();
                if (length <= longest)
                    continue;
                const QVector<int> together = joined(piece, pieces[j]);
                if (together.isEmpty() || !isConvex(together, points, eps)
                    || cornerCount(together, points, eps) > maxCorners)
                    continue;
                keep = i;
                drop = j;
                longest = length;
                best = together;
            }
        }
        if (keep < 0)
            break;
        pieces[keep] = best;
        pieces.remove(drop);
    }

    QVector<QPolygonF> result;
    result.reserve(pieces.size());
    for (const QVector<int> &piece : std::as_const(pieces)) {
        QPolygonF polygon;
        for (int index : piece)
            polygon << points[index];
        polygon = cleaned(polygon, eps);
        if (polygon.size() >= 3)
            result.append(polygon);
    }
    return result;
}

} // namespace ConvexPieces
