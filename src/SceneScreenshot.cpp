#include "SceneScreenshot.h"

#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"

#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QPdfWriter>
#include <QSvgGenerator>

namespace SceneScreenshot {

namespace {

// Room round the objects, so an outline at the edge is not cut in half.
constexpr qreal kMargin = 20.0;
// Raster pictures are drawn at this many pixels per scene unit.
constexpr qreal kRasterScale = 2.0;

void paint(CanvasScene *scene, QPaintDevice *device, const QRectF &area, const QRectF &target)
{
    QPainter painter(device);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    scene->render(&painter, target, area, Qt::KeepAspectRatio);
}

} // namespace

QRectF contentArea(const CanvasScene *scene)
{
    QRectF area;
    for (QGraphicsItem *item : scene->items()) {
        if (item->isVisible())
            area |= item->sceneBoundingRect();
    }
    // Joints are painted over the scene rather than being items of it.
    const qreal ring = scene->jointAnchorRadius();
    for (Joint *joint : scene->joints()) {
        for (Joint::End end : { Joint::End::A, Joint::End::B }) {
            const QPointF at = joint->anchorScenePos(end);
            area |= QRectF(at - QPointF(ring, ring), at + QPointF(ring, ring));
        }
    }
    if (area.isEmpty())
        return scene->sceneRect();
    return area.adjusted(-kMargin, -kMargin, kMargin, kMargin);
}

bool save(CanvasScene *scene, const QString &path, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (!scene)
        return fail(QObject::tr("There is no scene."));

    const QRectF area = contentArea(scene);
    const QString suffix = QFileInfo(path).suffix().toLower();

    if (suffix == QLatin1String("svg")) {
        QSvgGenerator svg;
        svg.setFileName(path);
        svg.setSize(area.size().toSize());
        svg.setViewBox(QRectF(QPointF(0, 0), area.size()));
        svg.setTitle(QFileInfo(path).completeBaseName());
        paint(scene, &svg, area, QRectF(QPointF(0, 0), area.size()));
        if (!QFileInfo::exists(path))
            return fail(QObject::tr("Could not write %1.").arg(path));
        return true;
    }

    if (suffix == QLatin1String("pdf")) {
        QPdfWriter pdf(path);
        pdf.setResolution(72);
        pdf.setPageSize(QPageSize(area.size(), QPageSize::Point));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        pdf.setTitle(QFileInfo(path).completeBaseName());
        paint(scene, &pdf, area, QRectF(QPointF(0, 0), area.size()));
        if (!QFileInfo::exists(path))
            return fail(QObject::tr("Could not write %1.").arg(path));
        return true;
    }

    QImage image((area.size() * kRasterScale).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    paint(scene, &image, area, QRectF(QPointF(0, 0), image.size()));

    QImageWriter writer(path);
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        // No transparency in a JPEG: what was clear comes out white, not black.
        QImage opaque(image.size(), QImage::Format_RGB32);
        opaque.fill(Qt::white);
        QPainter painter(&opaque);
        painter.drawImage(0, 0, image);
        painter.end();
        image = opaque;
        writer.setQuality(95);
    }
    if (!writer.write(image))
        return fail(writer.errorString());
    return true;
}

QStringList fileFilters()
{
    QStringList filters { QObject::tr("PNG image (*.png)") };
    const QStringList preferred { QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp"),
                                  QStringLiteral("tiff") };
    const QList<QByteArray> writable = QImageWriter::supportedImageFormats();
    for (const QString &format : preferred) {
        if (writable.contains(format.toLatin1()))
            filters << QObject::tr("%1 image (*.%2)").arg(format.toUpper(), format);
    }
    filters << QObject::tr("SVG drawing (*.svg)") << QObject::tr("PDF document (*.pdf)");
    return filters;
}

} // namespace SceneScreenshot
