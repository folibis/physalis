#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "MainWindow.h"
#include "PhysicsBody.h"
#include "SceneSerializer.h"
#include "ShapeItem.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QtTest/QTest>
#include <cstdio>

static void settle()
{
    for (int i = 0; i < 25; ++i)
        QCoreApplication::processEvents();
}

// The editor sitting in the value column of the row with this label.
static QWidget *editorFor(MainWindow &w, const QString &label)
{
    for (QTableWidget *table : w.findChildren<QTableWidget *>()) {
        for (int r = 0; r < table->rowCount(); ++r) {
            QWidget *nameCell = table->cellWidget(r, 0);
            auto *text = nameCell ? nameCell->findChild<QLabel *>() : nullptr;
            if (text && text->text() == label)
                return table->cellWidget(r, 1);
        }
    }
    return nullptr;
}

TEST(ReadOnly, Behaves)
{
    MainWindow window;
    window.resize(1300, 850);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    QString error;
    Fixtures::buildCart(scene);
    settle();

    // Physics tab, click a body.
    scene->setEditorMode(EditorMode::Physics);
    settle();
    ShapeItem *shape = scene->shapes().first();
    scene->selectForPhysics(shape, false);
    settle();

    // Then the Shape tab.
    for (QTabWidget *tabs : window.findChildren<QTabWidget *>()) {
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Shape")))
                tabs->setCurrentIndex(i);
    }
    settle();

    auto *bodyEditor = qobject_cast<QLineEdit *>(editorFor(window, QStringLiteral("Body")));
    EXPECT_TRUE(bodyEditor != nullptr) << "is present";
    if (bodyEditor) {
        EXPECT_TRUE(!bodyEditor->text().isEmpty()) << "shows the body name" << " -- " << (bodyEditor->text()).toStdString();
        EXPECT_TRUE(bodyEditor->isReadOnly()) << "cannot be typed into";
        EXPECT_TRUE(bodyEditor->focusPolicy() == Qt::NoFocus) << "cannot be focused by tabbing";

        // The complaint: typing rubbish used to be accepted by the widget.
        const QString before = bodyEditor->text();
        const QString wasNamed = shape->body() ? shape->body()->name() : QString();
        QTest::keyClicks(bodyEditor, QStringLiteral("rubbish"));
        QTest::keyClick(bodyEditor, Qt::Key_Return);
        settle();
        EXPECT_TRUE(bodyEditor->text() == before) << "typing rubbish leaves the field alone" << " -- " << (bodyEditor->text()).toStdString();
        EXPECT_TRUE(shape->body() && shape->body()->name() == wasNamed) << "and never touches the body" << " -- " << (shape->body() ? shape->body()->name() : QStringLiteral("(none)")).toStdString();
    }

    auto *nameEditor = qobject_cast<QLineEdit *>(editorFor(window, QStringLiteral("Name")));
    EXPECT_TRUE(nameEditor && !nameEditor->isReadOnly()) << "Name accepts input";

    window.close();
}
