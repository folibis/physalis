#include "CanvasScene.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "PropertyPane/PhysicsPropertyPane.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"
#include "SimulationController.h"

#include "OptionsDialog.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

// Flinging a body with the mouse during a run: press on it, pull back, let go.
// It is pushed the other way through its centre of mass, harder the further it
// was pulled, up to a limit.

namespace {

PhysicsBody *blockAt(CanvasScene *scene, const QPointF &at, physics::BodyType type)
{
    auto *shape = new RectangleItem;
    shape->setRect(QRectF(0, 0, 40, 40));
    shape->setPos(at);
    scene->addItem(shape);
    scene->notifyShapesChanged();
    scene->setEditorMode(EditorMode::Physics);
    scene->selectForPhysics(shape, true);
    PhysicsBody *body = scene->createBodyFromSelection();
    scene->clearPhysicsSelection();
    if (body)
        body->props().type = type;
    return body;
}

// How far the body travels sideways in half a second after a shot pulled
// this way from its centre.
qreal travelAfterShot(CanvasScene &scene, SimulationController &sim, PhysicsBody *body,
                      const QPointF &pull, bool cancel = false)
{
    sim.start();
    sim.stepFrame();
    const QPointF centre = body->centerOfMassScenePos();
    EXPECT_TRUE(scene.beginShot(centre)) << "a press on the body starts a shot";
    scene.aimShot(centre + pull);
    if (cancel)
        scene.cancelShot();
    else
        scene.releaseShot();
    const qreal startX = body->centerOfMassScenePos().x();
    for (int i = 0; i < 30; ++i)
        sim.stepFrame();
    const qreal travelled = body->centerOfMassScenePos().x() - startX;
    sim.stop();
    return travelled;
}

} // namespace

TEST(Slingshot, PulledBackAndFlungTheOtherWay)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    PhysicsBody *ball = blockAt(&scene, QPointF(0, 0), physics::BodyType::Dynamic);
    ASSERT_TRUE(ball);
    ball->shot().enabled = true;
    ball->shot().fullImpulse = 2.0;
    ball->shot().maxPull = 120.0;

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));
    QObject::connect(&scene, &CanvasScene::shotReleased, &sim, &SimulationController::shoot);

    const qreal half = travelAfterShot(scene, sim, ball, QPointF(-60, 0));
    const qreal full = travelAfterShot(scene, sim, ball, QPointF(-120, 0));
    const qreal beyond = travelAfterShot(scene, sim, ball, QPointF(-400, 0));
    const qreal called = travelAfterShot(scene, sim, ball, QPointF(-120, 0), true);

    EXPECT_GT(half, 5.0) << "pulled left, it goes right -- travelled " << half;
    EXPECT_GT(full, half * 1.5) << "a longer pull is a harder push -- " << half << " then " << full;
    EXPECT_NEAR(beyond, full, qAbs(full) * 0.02)
        << "and past full pull there is no more to give -- " << full << " then " << beyond;
    EXPECT_NEAR(called, 0.0, 0.5) << "a shot called off pushes nothing";
}

TEST(Slingshot, OnlyABodyThatCanBeShotAndOnlyDuringARun)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    PhysicsBody *ball = blockAt(&scene, QPointF(0, 0), physics::BodyType::Dynamic);
    PhysicsBody *wall = blockAt(&scene, QPointF(300, 0), physics::BodyType::Static);
    ASSERT_TRUE(ball && wall);
    wall->shot().enabled = true;

    SimulationController sim(&scene, nullptr);
    sim.setEngineName(QStringLiteral("Box2D"));

    EXPECT_FALSE(scene.beginShot(ball->centerOfMassScenePos())) << "nothing is shot while editing";

    sim.start();
    sim.stepFrame();
    EXPECT_FALSE(scene.beginShot(ball->centerOfMassScenePos()))
        << "a body is not shot unless it is set to be";
    EXPECT_FALSE(scene.beginShot(wall->centerOfMassScenePos()))
        << "and scenery cannot be moved, set or not";

    ball->shot().enabled = true;
    EXPECT_TRUE(scene.beginShot(ball->centerOfMassScenePos())) << "a body set to be shot can be";
    EXPECT_TRUE(scene.isAimingShot());
    sim.stop();
    EXPECT_FALSE(scene.isAimingShot()) << "and the aim goes with the run";
}

TEST(Slingshot, TheWindowPassesTheShotToTheRun)
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    auto *scene = window.findChild<CanvasScene *>();
    auto *sim = window.findChild<SimulationController *>();
    PhysicsBody *ball = blockAt(scene, QPointF(0, 0), physics::BodyType::Dynamic);
    ASSERT_TRUE(ball);
    ball->shot().enabled = true;

    sim->start();
    sim->stepFrame();
    const QPointF centre = ball->centerOfMassScenePos();
    ASSERT_TRUE(scene->beginShot(centre));
    scene->aimShot(centre + QPointF(-150, 0));
    scene->releaseShot();
    const qreal startX = ball->centerOfMassScenePos().x();
    for (int i = 0; i < 30; ++i)
        sim->stepFrame();
    EXPECT_GT(ball->centerOfMassScenePos().x() - startX, 5.0)
        << "released in the window, the body really was pushed";
    sim->stop();
    window.close();
}

TEST(Slingshot, TheSettingsAreTheBodysOwnAndAreSaved)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    PhysicsBody *ball = blockAt(&scene, QPointF(0, 0), physics::BodyType::Dynamic);
    ASSERT_TRUE(ball);

    // The checkbox on the body's pane is what turns it on.
    PhysicsPropertyPane pane;
    pane.attach(&scene);
    scene.selectForPhysics(ball->shapes().first(), true);
    bool found = false;
    for (const PropertyRow &row : pane.rows(EditorMode::Physics)) {
        if (row.label != QStringLiteral("Can Be Shot"))
            continue;
        found = true;
        EXPECT_EQ(row.type, PropertyFieldType::Boolean) << "a checkbox";
        EXPECT_FALSE(row.getter().toBool()) << "off until asked for";
        row.setter(true);
    }
    ASSERT_TRUE(found) << "the body's pane has the checkbox";
    EXPECT_TRUE(ball->shot().enabled) << "and ticking it sets the body";
    ball->shot().fullImpulse = 3.5;
    ball->shot().maxPull = 220.0;

    QString error;
    const QJsonObject saved = SceneSerializer::save(&scene);
    CanvasScene reopened;
    ASSERT_TRUE(SceneSerializer::load(&reopened, saved, &error)) << error.toStdString();
    ASSERT_EQ(reopened.bodies().size(), 1);
    const ShotSettings &shot = reopened.bodies().first()->shot();
    EXPECT_TRUE(shot.enabled) << "saved and read back";
    EXPECT_DOUBLE_EQ(shot.fullImpulse, 3.5);
    EXPECT_DOUBLE_EQ(shot.maxPull, 220.0);
}

// Only a dynamic body can be shot, so only a dynamic body is offered the rows.
TEST(Slingshot, OnlyADynamicBodyIsOfferedTheRows)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    PhysicsBody *body = blockAt(&scene, QPointF(0, 0), physics::BodyType::Static);
    ASSERT_TRUE(body);

    PhysicsPropertyPane pane;
    pane.attach(&scene);
    scene.selectForPhysics(body->shapes().first(), true);

    const auto offered = [&pane] {
        for (const PropertyRow &row : pane.rows(EditorMode::Physics)) {
            if (row.label == QStringLiteral("Can Be Shot") || row.label == QStringLiteral("Max Power")
                || row.label == QStringLiteral("Max Pull"))
                return true;
        }
        return false;
    };

    EXPECT_FALSE(offered()) << "not on a static body";
    body->props().type = physics::BodyType::Kinematic;
    EXPECT_FALSE(offered()) << "nor a kinematic one";
    body->props().type = physics::BodyType::Dynamic;
    EXPECT_TRUE(offered()) << "only on a dynamic one";

    // Changing the type from the pane itself rebuilds it, so the rows come and
    // go without having to select the body again.
    body->props().type = physics::BodyType::Static;
    int rebuilds = 0;
    QObject::connect(&pane, &PropertyPane::rowsChanged, [&rebuilds] { ++rebuilds; });
    for (const PropertyRow &row : pane.rows(EditorMode::Physics)) {
        if (row.label == QStringLiteral("Type"))
            row.setter(2);   // Dynamic
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(body->props().type, physics::BodyType::Dynamic);
    EXPECT_GE(rebuilds, 1) << "the pane is rebuilt after the type changes";
    EXPECT_TRUE(offered());
}

// The power sits on the body, right under the checkbox that turns shooting on.
TEST(Slingshot, MaxPowerIsRightUnderTheCheckbox)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    PhysicsBody *ball = blockAt(&scene, QPointF(0, 0), physics::BodyType::Dynamic);
    ASSERT_TRUE(ball);

    PhysicsPropertyPane pane;
    pane.attach(&scene);
    scene.selectForPhysics(ball->shapes().first(), true);

    QStringList labels;
    for (const PropertyRow &row : pane.rows(EditorMode::Physics))
        labels << row.label;
    const int checkbox = labels.indexOf(QStringLiteral("Can Be Shot"));
    ASSERT_GE(checkbox, 0);
    EXPECT_EQ(labels.value(checkbox + 1), QStringLiteral("Max Power")) << "the next row is the power";

    for (const PropertyRow &row : pane.rows(EditorMode::Physics)) {
        if (row.label == QStringLiteral("Max Power"))
            row.setter(6.0);
    }
    EXPECT_DOUBLE_EQ(ball->shot().fullImpulse, 6.0) << "and it sets this body's push";
}

// How the pull line looks is a setting, the same for every body: saved in the
// settings file, shown in Options, handed to the scene.
TEST(Slingshot, TheLineLookIsASetting)
{
    QTemporaryDir home;
    ASSERT_TRUE(home.isValid());
    const QString path = home.filePath(QStringLiteral("settings.ini"));
    {
        QSettings written(path, QSettings::IniFormat);
        written.beginGroup(QStringLiteral("Physics"));
        written.setValue(QStringLiteral("shotLightColor"), QStringLiteral("#ff00ff00"));
        written.setValue(QStringLiteral("shotFullColor"), QStringLiteral("#ff0000ff"));
        written.setValue(QStringLiteral("shotLineWidth"), 5.5);
        written.setValue(QStringLiteral("shotLineStyle"), static_cast<int>(Qt::DashLine));
        written.endGroup();
    }
    const QByteArray previous = qgetenv("PHYSALIS_SETTINGS");
    qputenv("PHYSALIS_SETTINGS", path.toLocal8Bit());

    {
        MainWindow window;
        auto *scene = window.findChild<CanvasScene *>();
        EXPECT_EQ(scene->shotLightColor(), QColor(0, 255, 0)) << "the saved light-pull colour reaches the scene";
        EXPECT_EQ(scene->shotFullColor(), QColor(0, 0, 255)) << "and the full-pull colour";
        EXPECT_DOUBLE_EQ(scene->shotLineWidth(), 5.5) << "and the width";
        EXPECT_EQ(scene->shotLineStyle(), Qt::DashLine) << "and the style";
    }

    if (previous.isEmpty())
        qunsetenv("PHYSALIS_SETTINGS");
    else
        qputenv("PHYSALIS_SETTINGS", previous);

    // Options shows them and hands back exactly what it was given.
    OptionsDialog::Settings given;
    given.shotLightColor = QColor(1, 2, 3);
    given.shotFullColor = QColor(4, 5, 6);
    given.shotLineWidth = 2.5;
    given.shotLineStyle = Qt::SolidLine;
    OptionsDialog dialog(given);
    const OptionsDialog::Settings back = dialog.settings();
    EXPECT_EQ(back.shotLightColor, QColor(1, 2, 3)) << "the colours are in Options";
    EXPECT_EQ(back.shotFullColor, QColor(4, 5, 6));
    EXPECT_DOUBLE_EQ(back.shotLineWidth, 2.5) << "the width";
    EXPECT_EQ(back.shotLineStyle, Qt::SolidLine) << "the style";
}

// A body that can be shot looks it: a dotted outline just inside its own.
TEST(Slingshot, ABodyThatCanBeShotLooksIt)
{
    CanvasScene scene;
    scene.setSimulationEngineName(QStringLiteral("Box2D"));
    PhysicsBody *ball = blockAt(&scene, QPointF(0, 0), physics::BodyType::Dynamic);
    ASSERT_TRUE(ball);

    const auto render = [&scene] {
        QImage image(120, 120, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        scene.render(&painter, QRectF(0, 0, 120, 120), QRectF(-40, -40, 120, 120));
        painter.end();
        return image;
    };

    const QImage plain = render();
    ball->shot().enabled = true;
    const QImage shootable = render();
    EXPECT_NE(plain, shootable) << "ticking Can Be Shot changes how the body is drawn";

    // The difference is inside the body, not around it.
    int changedInside = 0;
    int changedOutside = 0;
    for (int y = 0; y < plain.height(); ++y) {
        for (int x = 0; x < plain.width(); ++x) {
            if (plain.pixel(x, y) == shootable.pixel(x, y))
                continue;
            const bool inside = x > 42 && x < 78 && y > 42 && y < 78;   // the 40x40 block, less its border
            (inside ? changedInside : changedOutside) += 1;
        }
    }
    EXPECT_GT(changedInside, 0) << "the extra outline sits inside the body";
    EXPECT_EQ(changedOutside, 0) << "and nothing changes outside it";

    ball->props().type = physics::BodyType::Static;
    EXPECT_EQ(render(), [&] {
        ball->shot().enabled = false;
        QImage staticPlain = render();
        ball->shot().enabled = true;
        return staticPlain;
    }()) << "a static body cannot be shot, so it is not marked, whatever the box says";
}
