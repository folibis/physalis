#include "Icons.h"

#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QSvgRenderer>

namespace {

// The icons are drawn in their own colours, and those read on a light
// background and a dark one alike -- except the greys, which are ink and
// vanish on a dark panel. On a dark theme they are drawn lighter, at the
// moment of drawing, so switching the theme needs nothing to set its icons
// again.
class ThemedSvgEngine : public QIconEngine
{
public:
    explicit ThemedSvgEngine(const QString &path)
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            m_light = file.readAll();
        m_dark = m_light;
        static const char *const kInks[] = { "#5A5A5A", "#4A5568", "#5A6472", "#5A6875" };
        for (const char *ink : kInks) {
            QColor lifted(QString::fromLatin1(ink));
            lifted.setHsl(lifted.hslHue(), lifted.hslSaturation(), 200);
            m_dark.replace(ink, lifted.name().toLatin1());
        }
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        const qreal dpr = painter->device()->devicePixelRatioF();
        QPixmap drawn = pixmap(rect.size() * dpr, mode, state);
        drawn.setDevicePixelRatio(dpr);
        painter->drawPixmap(rect, drawn);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State) override
    {
        const bool dark = Theme::isDark();
        const quint64 key = (quint64(size.width()) << 32) | (quint64(size.height()) << 8)
                            | (quint64(mode) << 1) | quint64(dark);
        const auto cached = m_cache.constFind(key);
        if (cached != m_cache.constEnd())
            return *cached;

        QPixmap drawn(size);
        drawn.fill(Qt::transparent);
        QSvgRenderer renderer(dark ? m_dark : m_light);
        QPainter painter(&drawn);
        renderer.render(&painter, QRectF(QPointF(0, 0), size));
        painter.end();

        if (mode != QIcon::Normal && mode != QIcon::Active) {
            QStyleOption option;
            option.palette = QApplication::palette();
            drawn = QApplication::style()->generatedIconPixmap(mode, drawn, &option);
        }
        m_cache.insert(key, drawn);
        return drawn;
    }

    QIconEngine *clone() const override { return new ThemedSvgEngine(*this); }
    QString key() const override { return QStringLiteral("PhysalisThemedSvg"); }

private:
    QByteArray m_light;
    QByteArray m_dark;
    QHash<quint64, QPixmap> m_cache;
};

QIcon themed(const QString &path)
{
    return QIcon(new ThemedSvgEngine(path));
}

} // namespace

namespace Icons {

QIcon app() { return QIcon(QStringLiteral(":/app/icon.svg")); }

QIcon newScene() { return themed(QStringLiteral(":/icons/new_scene.svg")); }
QIcon loadScene() { return themed(QStringLiteral(":/icons/load_scene.svg")); }
QIcon saveScene() { return themed(QStringLiteral(":/icons/save_scene.svg")); }
QIcon saveSceneAs() { return themed(QStringLiteral(":/icons/save_as.svg")); }
QIcon exportScene() { return themed(QStringLiteral(":/icons/export.svg")); }
QIcon screenshot() { return themed(QStringLiteral(":/icons/screenshot.svg")); }
QIcon options() { return themed(QStringLiteral(":/icons/options.svg")); }
QIcon exit() { return themed(QStringLiteral(":/icons/exit.svg")); }
QIcon undo() { return themed(QStringLiteral(":/icons/undo.svg")); }
QIcon redo() { return themed(QStringLiteral(":/icons/redo.svg")); }
QIcon copy() { return themed(QStringLiteral(":/icons/copy.svg")); }
QIcon paste() { return themed(QStringLiteral(":/icons/paste.svg")); }
QIcon add() { return themed(QStringLiteral(":/icons/add.svg")); }
QIcon rectangle() { return themed(QStringLiteral(":/icons/rectangle.svg")); }
QIcon circle() { return themed(QStringLiteral(":/icons/circle.svg")); }
QIcon polygon() { return themed(QStringLiteral(":/icons/polygon.svg")); }
QIcon move() { return themed(QStringLiteral(":/icons/move.svg")); }
QIcon edit() { return themed(QStringLiteral(":/icons/edit.svg")); }
QIcon rotate() { return themed(QStringLiteral(":/icons/rotate.svg")); }
QIcon deleteShape() { return themed(QStringLiteral(":/icons/delete.svg")); }
QIcon resetScale() { return themed(QStringLiteral(":/icons/reset_scale.svg")); }
QIcon createBody() { return themed(QStringLiteral(":/icons/create_body.svg")); }
QIcon dissolveBody() { return themed(QStringLiteral(":/icons/dissolve_body.svg")); }
QIcon simulate() { return themed(QStringLiteral(":/icons/simulate.svg")); }
QIcon stop() { return themed(QStringLiteral(":/icons/stop.svg")); }
QIcon pause() { return themed(QStringLiteral(":/icons/pause.svg")); }
QIcon step() { return themed(QStringLiteral(":/icons/step.svg")); }
QIcon resetValue() { return themed(QStringLiteral(":/icons/reset_value.svg")); }
QIcon body() { return themed(QStringLiteral(":/icons/body.svg")); }
QIcon joint() { return themed(QStringLiteral(":/icons/joint.svg")); }
QIcon explosion() { return themed(QStringLiteral(":/icons/explosion.svg")); }
QIcon world() { return themed(QStringLiteral(":/icons/world.svg")); }
QIcon ray() { return themed(QStringLiteral(":/icons/ray.svg")); }
QIcon warning() { return themed(QStringLiteral(":/icons/warning.svg")); }
QIcon variable() { return themed(QStringLiteral(":/icons/variable.svg")); }
QIcon log() { return themed(QStringLiteral(":/icons/log.svg")); }

QIcon properties() { return themed(QStringLiteral(":/icons/properties.svg")); }
QIcon objects() { return themed(QStringLiteral(":/icons/objects.svg")); }
QIcon rules() { return themed(QStringLiteral(":/icons/rules.svg")); }
QIcon plot() { return themed(QStringLiteral(":/icons/plot.svg")); }

QIcon align() { return themed(QStringLiteral(":/icons/align.svg")); }
QIcon alignLeft() { return themed(QStringLiteral(":/icons/align_left.svg")); }
QIcon alignHorizontalCentre() { return themed(QStringLiteral(":/icons/align_hcentre.svg")); }
QIcon alignRight() { return themed(QStringLiteral(":/icons/align_right.svg")); }
QIcon alignTop() { return themed(QStringLiteral(":/icons/align_top.svg")); }
QIcon alignVerticalCentre() { return themed(QStringLiteral(":/icons/align_vcentre.svg")); }
QIcon alignBottom() { return themed(QStringLiteral(":/icons/align_bottom.svg")); }
QIcon distributeAcross() { return themed(QStringLiteral(":/icons/distribute_across.svg")); }
QIcon distributeDown() { return themed(QStringLiteral(":/icons/distribute_down.svg")); }
QIcon flipHorizontal() { return themed(QStringLiteral(":/icons/mirror_across.svg")); }
QIcon flipVertical() { return themed(QStringLiteral(":/icons/mirror_down.svg")); }
QIcon keyboard() { return themed(QStringLiteral(":/icons/keyboard.svg")); }
QIcon tools() { return themed(QStringLiteral(":/icons/tools.svg")); }
QIcon clone() { return themed(QStringLiteral(":/icons/clone.svg")); }
QIcon alignRingOutside() { return themed(QStringLiteral(":/icons/align_ring_out.svg")); }
QIcon alignRingInside() { return themed(QStringLiteral(":/icons/align_ring_in.svg")); }

QIcon value() { return themed(QStringLiteral(":/icons/value.svg")); }
QIcon random() { return themed(QStringLiteral(":/icons/random.svg")); }

} // namespace Icons
