#include "CanvasScene.h"
#include "Joint.h"
#include "PhysicsBody.h"
#include "RectangleItem.h"
#include "SceneExporter.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVariantMap>
#include <gtest/gtest.h>

namespace {

QString contentsOf(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

// The converters that ship with the project, found from this file's own
// location rather than from wherever the tests happen to run.
QString shippedConverters()
{
    QDir dir(QStringLiteral(__FILE__));
    dir.cdUp();
    dir.cdUp();
    return dir.absoluteFilePath(QStringLiteral("exporters"));
}

} // namespace

// A wheel joint with the spring switched off is a roller that runs free -- a
// scissor lift's deck rides on one. Planck says "no spring" with a frequency
// of zero, but its wheel joint then leaves m_sAx and m_sBx unset while the
// spring solver reads them on every step regardless: undefined times zero is
// NaN, and one step later every body joined to that one has NaN for a
// position and the scene is gone before it has drawn a frame. So the export
// has to say zero *and* fill those two in.
TEST(PlanckWheel, Behaves)
{
    CanvasScene scene;

    auto *a = new RectangleItem;
    a->setRect(QRectF(0, 0, 200, 20));
    a->setPos(0, 0);
    scene.addItem(a);
    auto *b = new RectangleItem;
    b->setRect(QRectF(0, 0, 40, 40));
    b->setPos(80, -60);
    scene.addItem(b);
    scene.notifyShapesChanged();
    scene.setEditorMode(EditorMode::Physics);

    scene.selectForPhysics(a, true);
    PhysicsBody *bodyA = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    scene.selectForPhysics(b, true);
    PhysicsBody *bodyB = scene.createBodyFromSelection();
    scene.clearPhysicsSelection();
    ASSERT_TRUE(bodyA && bodyB);

    QVariantMap params;
    params.insert(QStringLiteral("enableSpring"), false);
    // Left deliberately non-zero: with the spring off it must not come out.
    params.insert(QStringLiteral("hertz"), 3.0);
    params.insert(QStringLiteral("dampingRatio"), 0.7);
    Joint *joint = scene.createJoint(QStringLiteral("wheel"), bodyA, bodyB, 1, params);
    ASSERT_TRUE(joint != nullptr);
    joint->params() = params;

    // Held in a named variable: a pointer into the temporary this returns
    // dangles the moment the expression ends.
    const QVector<SceneExporter::Converter> converters =
        SceneExporter::discover(shippedConverters());
    const SceneExporter::Converter *planck = nullptr;
    for (const SceneExporter::Converter &c : converters) {
        if (c.name.contains(QStringLiteral("Planck")))
            planck = &c;
    }
    ASSERT_TRUE(planck != nullptr) << "the Planck.js converter ships with the project";

    QTemporaryDir output;
    ASSERT_TRUE(output.isValid());
    QString error;
    ASSERT_TRUE(SceneExporter::run(*planck, &scene, output.path(), QJsonObject(), &error))
        << error.toStdString();

    const QString generated = contentsOf(output.filePath(QStringLiteral("index.html")));
    const int at = generated.indexOf(QStringLiteral("pl.WheelJoint"));
    ASSERT_GT(at, 0) << "the wheel joint reaches the exported scene";
    const QString emitted = generated.mid(at, 900);

    EXPECT_TRUE(emitted.contains(QStringLiteral("frequencyHz: 0")))
        << "a spring that is off is a frequency of zero, whatever the hertz says"
        << " -- " << emitted.toStdString();
    EXPECT_TRUE(emitted.contains(QStringLiteral("m_sAx = 0"))
                && emitted.contains(QStringLiteral("m_sBx = 0")))
        << "and the fields Planck leaves unset are filled in, or the world goes NaN"
        << " -- " << emitted.toStdString();
}
