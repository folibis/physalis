#include "CanvasScene.h"

#include <QImage>
#include <QPainter>
#include <gtest/gtest.h>

namespace {

int luminance(const QImage &image, int x)
{
    long total = 0;
    for (int y = 0; y < image.height(); ++y)
        total += qGray(image.pixel(x, y));
    return int(total / image.height());
}

int rowLuminance(const QImage &image, int y)
{
    long total = 0;
    for (int x = 0; x < image.width(); ++x)
        total += qGray(image.pixel(x, y));
    return int(total / image.width());
}

} // namespace

// The grid's first line on the left and at the top is dark, but the loops that
// draw the major lines stopped short of the far edges, so the field ended in a
// pale minor line on the right and at the bottom. All four edges are dark now.
TEST(GridEdges, AllFourEdgesAreDark)
{
    CanvasScene scene;
    const QRectF field = scene.sceneRect();
    ASSERT_FALSE(field.isEmpty());

    QImage image(int(field.width()), int(field.height()), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    scene.render(&painter, QRectF(QPointF(), QSizeF(image.size())), field);
    painter.end();

    const int inside = 3;  // a column inside the first cell, off every line
    const int left = luminance(image, 0);
    const int right = luminance(image, image.width() - 1);
    const int top = rowLuminance(image, 0);
    const int bottom = rowLuminance(image, image.height() - 1);

    EXPECT_LT(left, luminance(image, inside)) << "the left edge is a line";
    EXPECT_LT(right, luminance(image, image.width() - 1 - inside)) << "so is the right one";
    EXPECT_LT(top, rowLuminance(image, inside)) << "and the top";
    EXPECT_LT(bottom, rowLuminance(image, image.height() - 1 - inside)) << "and the bottom";
    EXPECT_LE(qAbs(right - left), 12) << "the far edges are as dark as the near ones -- "
                                      << left << " vs " << right;
    EXPECT_LE(qAbs(bottom - top), 12) << top << " vs " << bottom;
}
