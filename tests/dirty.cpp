#include "CanvasScene.h"
#include "UndoStack.h"
#include "RectangleItem.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <cstdio>

static void addShape(CanvasScene *scene, const char *name)
{
    auto *r = new RectangleItem;
    r->setRect(QRectF(0, 0, 40, 40));
    r->setName(QString::fromLatin1(name));
    scene->addItem(r);
    scene->notifyShapesChanged();
}

TEST(Dirty, Behaves)
{
    CanvasScene scene;
    UndoStack undo(&scene);

    EXPECT_TRUE(undo.isClean()) << "a fresh scene is clean";

    addShape(&scene, "a");
    undo.push(QStringLiteral("Add a"));
    EXPECT_TRUE(!undo.isClean()) << "after an edit it is dirty";

    undo.markClean();
    EXPECT_TRUE(undo.isClean()) << "after saving it is clean again";

    addShape(&scene, "b");
    undo.push(QStringLiteral("Add b"));
    EXPECT_TRUE(!undo.isClean()) << "another edit makes it dirty";

    undo.undo();
    EXPECT_TRUE(undo.isClean()) << "undoing back to the saved state is clean again";

    undo.redo();
    EXPECT_TRUE(!undo.isClean()) << "redoing forward of it is dirty";

    // A run of property edits merges into one entry, so the cursor doesn't
    // move -- the scene still has to count as changed.
    undo.undo();                                   // back to the saved state
    undo.markClean();
    addShape(&scene, "c");
    undo.push(QStringLiteral("Edit c"), QStringLiteral("prop:c"));
    undo.markClean();                              // pretend a save here
    addShape(&scene, "d");
    undo.push(QStringLiteral("Edit c"), QStringLiteral("prop:c"));  // merges in place
    EXPECT_TRUE(!undo.isClean()) << "an edit merged into the saved entry is dirty";

    // Saving, undoing, then editing puts the saved state on a branch that no
    // longer exists.
    UndoStack other(&scene);
    addShape(&scene, "e");
    other.push(QStringLiteral("Add e"));
    addShape(&scene, "f");
    other.push(QStringLiteral("Add f"));
    other.markClean();
    other.undo();
    addShape(&scene, "g");
    other.push(QStringLiteral("Add g"));
    EXPECT_TRUE(!other.isClean()) << "saved state discarded by a new branch is dirty";

    // Once the saved state falls out of the history we can no longer prove
    // the scene matches the file.
    UndoStack small(&scene);
    small.setCapacity(3);
    small.markClean();
    for (int i = 0; i < 6; ++i) {
        addShape(&scene, "x");
        small.push(QStringLiteral("Add %1").arg(i));
    }
    EXPECT_TRUE(!small.isClean()) << "saved state trimmed off the history is dirty";
}
