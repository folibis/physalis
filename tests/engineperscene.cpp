#include "CanvasScene.h"
#include "EngineRegistry.h"
#include "RectangleItem.h"
#include "SceneSerializer.h"

#include <gtest/gtest.h>

// A scene is built for one engine and keeps it: the engine's name is saved
// with the scene, and a scene naming an engine that is not installed is
// refused outright rather than loaded half-understood.
TEST(EnginePerScene, TheFileNamesTheEngineItWasBuiltFor)
{
    CanvasScene source;
    source.setSimulationEngineName(QStringLiteral("Chipmunk2D"));
    source.addRectangle(QPointF(0, 0));
    const QJsonObject document = SceneSerializer::save(&source);
    EXPECT_EQ(document.value("engine").toString(), QStringLiteral("Chipmunk2D"))
        << "the engine is written into the file";

    CanvasScene opened;
    opened.setSimulationEngineName(QStringLiteral("Box2D"));
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&opened, document, &error)) << error.toStdString();
    EXPECT_EQ(opened.simulationEngineName(), QStringLiteral("Chipmunk2D"))
        << "and opening the scene takes it up, whatever was loaded before";
}

TEST(EnginePerScene, AMissingEngineStopsTheLoad)
{
    CanvasScene source;
    source.setSimulationEngineName(QStringLiteral("Chipmunk2D"));
    source.addRectangle(QPointF(0, 0));
    QJsonObject document = SceneSerializer::save(&source);
    document.insert("engine", QStringLiteral("NoSuchEngine"));

    CanvasScene opened;
    opened.addRectangle(QPointF(500, 500));
    opened.setSimulationEngineName(QStringLiteral("Box2D"));

    QString error;
    EXPECT_FALSE(SceneSerializer::load(&opened, document, &error)) << "the load is refused";
    EXPECT_TRUE(error.contains(QStringLiteral("NoSuchEngine")))
        << "and says which engine is missing -- " << error.toStdString();
    EXPECT_EQ(opened.shapes().size(), 1)
        << "what was open is untouched, rather than half replaced";
    EXPECT_EQ(opened.simulationEngineName(), QStringLiteral("Box2D"));
}

// A scene written before engines were named in the file keeps whatever the
// editor is set to, rather than being refused.
TEST(EnginePerScene, AnOlderFileWithoutOneStillOpens)
{
    CanvasScene source;
    source.addRectangle(QPointF(0, 0));
    QJsonObject document = SceneSerializer::save(&source);
    document.remove("engine");

    CanvasScene opened;
    opened.setSimulationEngineName(QStringLiteral("Chipmunk2D"));
    QString error;
    ASSERT_TRUE(SceneSerializer::load(&opened, document, &error)) << error.toStdString();
    EXPECT_EQ(opened.simulationEngineName(), QStringLiteral("Chipmunk2D"));
}
