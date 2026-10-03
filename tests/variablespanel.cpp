// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "Rule.h"
#include "SceneVariable.h"
#include "VariablesPanel.h"

#include <QCoreApplication>
#include <QTableWidget>
#include "MainWindow.h"
#include <QAction>
#include <QDir>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace {

QTableWidget *tableOf(VariablesPanel *panel)
{
    const auto tables = panel->findChildren<QTableWidget *>();
    return tables.isEmpty() ? nullptr : tables.first();
}

SceneVariable made(const char *name, SceneVariable::Type type, const QVariant &initial)
{
    SceneVariable variable;
    variable.name = QString::fromLatin1(name);
    variable.type = type;
    variable.initial = initial;
    return variable;
}

} // namespace

// Every cell of this tab holds a widget, so a click never reaches the table and
// each widget carries its own row as a property instead. That property is what
// the right-click menu reads to know which variable it is offering, so a widget
// left behind from an earlier layout with an older row makes the menu describe
// the wrong variable -- offering to take a variable out of the log that was
// never in it, and to add one that already is.
TEST(VariablesPanel, EveryCellWidgetKnowsTheRowItIsActuallyIn)
{
    CanvasScene scene;
    VariablesPanel panel(nullptr);
    panel.setScene(&scene);

    QTableWidget *table = tableOf(&panel);
    ASSERT_NE(table, nullptr) << "the panel has no table";

    const auto checkRows = [&](const char *when) {
        ASSERT_EQ(table->rowCount(), scene.variables().size()) << when;
        for (int row = 0; row < table->rowCount(); ++row) {
            for (int column = 0; column < table->columnCount(); ++column) {
                QWidget *cell = table->cellWidget(row, column);
                ASSERT_NE(cell, nullptr)
                    << when << ": row " << row << " column " << column << " has no widget";
                EXPECT_EQ(cell->property("variableRow").toInt(), row)
                    << when << ": the widget in row " << row << " column " << column
                    << " still thinks it is in another row";
            }
        }
    };

    scene.setVariables({ made("score", SceneVariable::Type::Integer, 0),
                         made("lives", SceneVariable::Type::Integer, 3) });
    QCoreApplication::processEvents();
    checkRows("two variables");

    // A third arriving, which is what rebuilds the table.
    scene.setVariables({ made("score", SceneVariable::Type::Integer, 0),
                         made("lives", SceneVariable::Type::Integer, 3),
                         made("clock", SceneVariable::Type::Timer, 0) });
    QCoreApplication::processEvents();
    checkRows("after one was added");

    // And one taken away from the middle, which shifts every row below it.
    scene.setVariables({ made("score", SceneVariable::Type::Integer, 0),
                         made("clock", SceneVariable::Type::Timer, 0) });
    QCoreApplication::processEvents();
    checkRows("after one was removed");
}

// The log is kept by name, so the offer made for a row has to be the one that
// row's own variable needs -- whatever else is in the list and whatever order
// they are in.
TEST(VariablesPanel, TheLogOfferFollowsTheVariableAndNotThePosition)
{
    CanvasScene scene;
    VariablesPanel panel(nullptr);
    panel.setScene(&scene);

    // A timer first and a logged variable under it, which is the arrangement
    // the offer was reported wrong in.
    scene.setVariables({ made("clock", SceneVariable::Type::Timer, 0),
                         made("score", SceneVariable::Type::Integer, 0) });
    scene.addWatch({ Rule::variables(), QStringLiteral("score"), QStringLiteral("score") });
    QCoreApplication::processEvents();

    EXPECT_FALSE(scene.isWatched(Rule::variables(), QStringLiteral("clock")))
        << "the timer was reported as logged though nothing logged it";
    EXPECT_TRUE(scene.isWatched(Rule::variables(), QStringLiteral("score")))
        << "the logged variable was reported as not logged";

    QTableWidget *table = tableOf(&panel);
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    // Which variable each row is, as the menu works it out: the row a cell
    // widget says it is in, looked up in the scene.
    for (int row = 0; row < table->rowCount(); ++row) {
        QWidget *cell = table->cellWidget(row, 0);
        ASSERT_NE(cell, nullptr);
        const int said = cell->property("variableRow").toInt();
        ASSERT_GE(said, 0);
        ASSERT_LT(said, scene.variables().size());
        EXPECT_EQ(scene.variables().at(said).name, scene.variables().at(row).name)
            << "row " << row << " would offer the menu for another variable";
    }
}


// What the log shows is saved with the scene, so putting a variable on it is a
// change: the Save button has to come on and closing has to offer to keep it.
// It did neither -- the watch was added and nothing recorded an edit.
TEST(VariablesPanel, PuttingAVariableOnTheLogIsAChange)
{
    MainWindow window;
    auto *scene = window.findChild<CanvasScene *>();
    ASSERT_NE(scene, nullptr);
    auto *save = window.findChild<QAction *>(QStringLiteral("actionSaveScene"));
    ASSERT_NE(save, nullptr);

    SceneVariable score;
    score.name = QStringLiteral("score");
    score.type = SceneVariable::Type::Integer;
    scene->setVariables({ score });
    scene->notifyEdit(QStringLiteral("Add variable"));

    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    ASSERT_TRUE(window.saveSceneAsForTest(QDir(folder.path()).filePath(
        QStringLiteral("watched.phys"))));
    ASSERT_FALSE(save->isEnabled()) << "the scene was just saved and Save is still on";

    scene->addWatch({ Rule::variables(), score.name, score.name });
    EXPECT_TRUE(save->isEnabled())
        << "a variable was put on the log and the scene does not count as changed";

    ASSERT_TRUE(window.saveSceneForTest());
    ASSERT_FALSE(save->isEnabled());

    scene->removeWatch(Rule::variables(), score.name);
    EXPECT_TRUE(save->isEnabled())
        << "a variable was taken off the log and the scene does not count as changed";
}

