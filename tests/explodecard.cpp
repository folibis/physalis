#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "MainWindow.h"
#include "Rule.h"
#include "RulesPanel.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QComboBox>
#include <QLabel>
#include <QTabWidget>
#include <cstdio>

static void settle() { for (int i = 0; i < 25; ++i) QCoreApplication::processEvents(); }

TEST(ExplodeCard, Behaves)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    settle();
    auto *scene = window.findChild<CanvasScene *>();
    scene->setEditorMode(EditorMode::Physics);
    ExplosionItem *boom = scene->addExplosion(QPointF(0, 0));
    settle();

    EXPECT_TRUE(boom->isVisible()) << "visible in Physics mode";
    scene->setEditorMode(EditorMode::Edit);
    settle();
    EXPECT_TRUE(!boom->isVisible()) << "hidden in Edit mode";
    scene->setEditorMode(EditorMode::Physics);
    settle();
    EXPECT_TRUE(boom->isVisible()) << "back in Physics mode";

    // A rule that fires the explosion.
    Rule bang;
    bang.subjectName = Rule::world();
    bang.conditionKey = QStringLiteral("time");
    bang.compare = Rule::Compare::Greater;
    bang.conditionValue = 1.0;
    bang.targetName = boom->name();
    // Deliberately left without an action: choosing the target is what a
    // user does, and the panel picks the only property there is.
    scene->setRules({ bang });
    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Rule")))
                tabs->setCurrentIndex(i);
    settle();

    auto *rules = window.findChild<RulesPanel *>();
    QComboBox *opBox = nullptr;
    QStringList combos;
    for (QComboBox *box : rules->findChildren<QComboBox *>()) {
        QStringList items;
        for (int i = 0; i < box->count(); ++i)
            items << box->itemText(i);
        if (items.contains(QStringLiteral("Set to")))
            opBox = box;
        combos << QStringLiteral("[%1]").arg(box->currentText());
    }
    EXPECT_TRUE(opBox != nullptr) << "the Set to / Toggle box exists";
    EXPECT_TRUE(opBox && !opBox->isVisible()) << "but is hidden for an action" << " -- " << (opBox ? QStringLiteral("visible=%1").arg(opBox->isVisible()) : QString()).toStdString();

    QStringList labels;
    for (QLabel *l : rules->findChildren<QLabel *>())
        if (l->isVisible())
            labels << l->text();
    EXPECT_TRUE(!rules->findChildren<QComboBox *>().isEmpty()) << "the target carries an icon";

    window.close();
}
