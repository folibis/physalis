#include "MainWindow.h"
#include "CanvasScene.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"

#include <QApplication>
#include <QGraphicsView>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

void settle()
{
    for (int i = 0; i < 25; ++i)
        QCoreApplication::processEvents();
}

} // namespace

// A scene drawn away from the middle of the field used to open with the view
// on the field's centre, so its objects could start half out of sight. Opening
// one now puts the middle of what it holds in the middle of the view.
TEST(OpenCentred, ViewCentresOnWhatTheSceneHolds)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("offcentre.phys"));
    {
        CanvasScene source;
        // Room for the view to scroll to them: it stops at the field's edge.
        source.setFieldSize(4000, 4000);
        source.addRectangle(QPointF(260, 140));
        source.addRectangle(QPointF(380, 240));
        QString error;
        ASSERT_TRUE(SceneSerializer::saveToFile(&source, path, &error)) << error.toStdString();
    }

    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();

    ASSERT_TRUE(window.openSceneForTest(path));
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    auto *view = window.findChild<QGraphicsView *>(QStringLiteral("canvasView"));
    ASSERT_TRUE(scene && view);

    const QRectF bounds = scene->contentBounds();
    ASSERT_FALSE(bounds.isNull()) << "the scene has something in it";
    const QPointF centre = view->mapToScene(view->viewport()->rect().center());
    EXPECT_NEAR(centre.x(), bounds.center().x(), 2.0) << "centred across";
    EXPECT_NEAR(centre.y(), bounds.center().y(), 2.0) << "and down";
}
