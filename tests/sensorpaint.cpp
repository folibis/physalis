#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "PropertyPanel.h"
#include "RectangleItem.h"
#include "ShapeItem.h"

#include <QApplication>
#include <QDir>
#include <QCheckBox>
#include <QLabel>
#include <QImage>
#include <QPainter>
#include <QTableWidget>
#include <gtest/gtest.h>

using namespace physics;

namespace {

void settle()
{
    for (int i = 0; i < 15; ++i)
        QCoreApplication::processEvents();
}

// How much of the shape is painted in something close to this colour.
int countNear(const QImage &image, const QColor &wanted, int tolerance = 40)
{
    int hits = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (c.alpha() < 8)
                continue;
            if (qAbs(c.red() - wanted.red()) <= tolerance
                && qAbs(c.green() - wanted.green()) <= tolerance
                && qAbs(c.blue() - wanted.blue()) <= tolerance)
                ++hits;
        }
    }
    return hits;
}

// The panel puts a label widget in column 0 and the editor in column 1.
QWidget *editorFor(QWidget *panel, const QString &label)
{
    for (QTableWidget *table : panel->findChildren<QTableWidget *>()) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->isRowHidden(row))
                continue;
            QWidget *nameCell = table->cellWidget(row, 0);
            auto *text = nameCell ? nameCell->findChild<QLabel *>() : nullptr;
            if (text && text->text() == label)
                return table->cellWidget(row, 1);
        }
    }
    return nullptr;
}

QImage render(CanvasScene *scene, ShapeItem *shape)
{
    QImage image(120, 120, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(30, 30);
    scene->render(&painter, QRectF(0, 0, 120, 120),
                  shape->sceneBoundingRect().adjusted(-4, -4, 4, 4));
    painter.end();
    return image;
}

}

// Marking a shape as a sensor adds hatching. It must not take the body's
// colour away with it -- the shape is still part of that body.
TEST(SensorPaint, KeepsBodyColour)
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *item = new RectangleItem;
    item->setRect(QRectF(0, 0, 90, 90));
    item->setPos(0, 0);
    item->setName(QStringLiteral("pad"));
    scene->addItem(item);
    scene->notifyShapesChanged();
    scene->selectForPhysics(item, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    body->props().type = BodyType::Static;
    scene->clearPhysicsSelection();
    settle();

    const QColor bodyColour = scene->bodyColor(BodyType::Static);
    const QColor sensorColour = scene->sensorColor();

    const QImage plain = render(scene, item);
    const int bodyPixelsPlain = countNear(plain, bodyColour);
    EXPECT_GT(bodyPixelsPlain, 200) << "an ordinary shape is filled with its body colour";
    EXPECT_LT(countNear(plain, sensorColour), bodyPixelsPlain / 4)
        << "and carries no sensor hatching";

    item->part().isSensor = true;
    item->update();
    settle();

    const QImage sensing = render(scene, item);
    EXPECT_GT(countNear(sensing, sensorColour), 40)
        << "the sensor flag adds hatching";
    EXPECT_GT(countNear(sensing, bodyColour), bodyPixelsPlain / 2)
        << "and the body colour is still underneath it";

    // Side by side, so the difference is one glance rather than two numbers.
    QImage side(260, 120, QImage::Format_ARGB32);
    side.fill(Qt::white);
    QPainter compose(&side);
    compose.drawImage(0, 0, plain);
    compose.drawImage(140, 0, sensing);
    compose.end();
    side.save(QDir(QDir::tempPath()).filePath(QStringLiteral("sensor_paint.png")));

    window.close();
}

// Ticking the Sensor box has to take effect there and then -- not on the next
// click somewhere else in the panel.
TEST(SensorPaint, ChecksBoxAndRepaintsAtOnce)
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);

    auto *item = new RectangleItem;
    item->setRect(QRectF(0, 0, 90, 90));
    item->setName(QStringLiteral("pad"));
    scene->addItem(item);
    scene->notifyShapesChanged();
    scene->selectForPhysics(item, true);
    scene->createBodyFromSelection();
    settle();

    // The panel's own checkbox, found by the label beside it.
    auto *panel = window.findChild<PropertyPanel *>();
    ASSERT_NE(panel, nullptr);
    auto *box = qobject_cast<QCheckBox *>(editorFor(panel, QStringLiteral("Sensor")));
    ASSERT_NE(box, nullptr) << "the Sensor row is on screen";
    EXPECT_NE(editorFor(panel, QStringLiteral("Friction")), nullptr)
        << "a solid shape is offered contact settings";

    const int hatchBefore = countNear(render(scene, item), scene->sensorColor());

    box->setChecked(true);
    settle();

    EXPECT_TRUE(item->part().isSensor);
    EXPECT_GT(countNear(render(scene, item), scene->sensorColor()), hatchBefore + 40)
        << "and what the canvas draws is hatched";
    EXPECT_EQ(editorFor(panel, QStringLiteral("Friction")), nullptr)
        << "and the settings that no longer apply are gone";

    window.close();
}
