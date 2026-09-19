#include "CanvasScene.h"
#include "RectangleItem.h"
#include "SceneScreenshot.h"

#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <gtest/gtest.h>

// A picture of the scene, cut to what is in it, in the format the file's
// extension names.

namespace {

void twoShapes(CanvasScene &scene)
{
    auto *a = scene.addRectangle(QPointF(-200, -50));
    a->setRect(QRectF(0, 0, 100, 40));
    auto *b = scene.addRectangle(QPointF(150, 60));
    b->setRect(QRectF(0, 0, 50, 50));
    scene.notifyShapesChanged();
}

} // namespace

TEST(Screenshot, IsCutToTheObjects)
{
    CanvasScene scene;
    twoShapes(scene);
    const QRectF area = SceneScreenshot::contentArea(&scene);
    // From the first shape's left edge to the second's right, and a margin.
    EXPECT_LT(area.left(), -200.0);
    EXPECT_GT(area.left(), -240.0);
    EXPECT_GT(area.right(), 200.0);
    EXPECT_LT(area.right(), 240.0);
    EXPECT_LT(area.width(), scene.sceneRect().width()) << "cut to the objects, not the whole field";
}

TEST(Screenshot, AnEmptySceneIsTheField)
{
    CanvasScene scene;
    EXPECT_EQ(SceneScreenshot::contentArea(&scene), scene.sceneRect());
}

TEST(Screenshot, WritesEachFormat)
{
    CanvasScene scene;
    twoShapes(scene);
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());

    const QRectF area = SceneScreenshot::contentArea(&scene);
    for (const char *name : { "shot.png", "shot.jpg", "shot.svg", "shot.pdf" }) {
        const QString path = folder.filePath(QString::fromLatin1(name));
        QString error;
        EXPECT_TRUE(SceneScreenshot::save(&scene, path, &error)) << name << ": " << error.toStdString();
        EXPECT_GT(QFileInfo(path).size(), 0) << name;
    }
    // Drawn at twice the scene's size.
    const QImage png(folder.filePath(QStringLiteral("shot.png")));
    EXPECT_EQ(png.size(), (area.size() * 2.0).toSize());
}
