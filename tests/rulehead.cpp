#include "CanvasScene.h"
#include "SceneFixtures.h"
#include "MainWindow.h"
#include "RulesPanel.h"
#include "SceneSerializer.h"

#include <QApplication>
#include <gtest/gtest.h>
#include <QFrame>
#include <QLabel>
#include <QTabWidget>
#include <QToolButton>
#include <cstdio>

static const char *kScratch = "";

static void settle()
{
    for (int i = 0; i < 25; ++i)
        QCoreApplication::processEvents();
}

TEST(RuleHead, Behaves)
{
    MainWindow window;
    window.resize(1300, 900);
    window.show();
    settle();

    auto *scene = window.findChild<CanvasScene *>();
    QString error;
    Fixtures::buildCart(scene);
    scene->setEditorMode(EditorMode::Physics);
    settle();

    auto *rules = window.findChild<RulesPanel *>();
    for (QTabWidget *tabs : window.findChildren<QTabWidget *>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("Rule")))
                tabs->setCurrentIndex(i);
    settle();

    // The first rule card, and the pieces of its header.
    QList<QFrame *> cards;
    for (QFrame *f : rules->findChildren<QFrame *>())
        if (f->objectName() == QStringLiteral("ruleCard"))
            cards << f;
    EXPECT_TRUE(!cards.isEmpty()) << "there are cards to test";
    if (cards.isEmpty()) { window.close(); return; }

    QFrame *card = cards.first();
    const QList<QToolButton *> buttons = card->findChildren<QToolButton *>();
    QLabel *heading = nullptr;
    for (QLabel *l : card->findChildren<QLabel *>())
        if (l->text().startsWith(QStringLiteral("Rule ")))
            heading = l;

    EXPECT_TRUE(buttons.size() >= 2) << "has a collapse arrow and a delete button" << " -- " << (QStringLiteral("%1 buttons").arg(buttons.size())).toStdString();

    QToolButton *arrow = nullptr, *del = nullptr;
    for (QToolButton *b : buttons) {
        if (b->objectName() == QStringLiteral("collapseButton")) arrow = b;
        else if (!b->icon().isNull())                            del = b;
    }
    EXPECT_TRUE(arrow != nullptr) << "the arrow is there";
    EXPECT_TRUE(del != nullptr) << "the delete button is there";
    EXPECT_TRUE(heading != nullptr) << "and the title is there" << " -- " << (heading ? heading->text() : QStringLiteral("(none)")).toStdString();
    if (!arrow || !del || !heading) { window.close(); return; }

    const int arrowX = arrow->mapTo(card, QPoint(0, 0)).x();
    const int delX   = del->mapTo(card, QPoint(0, 0)).x();
    const int titleMid = heading->mapTo(card, QPoint(0, 0)).x() + heading->width() / 2;
    const int cardMid  = card->width() / 2;

    EXPECT_TRUE(arrowX < cardMid / 2 && delX < cardMid / 2) << "both buttons are on the left";
    EXPECT_TRUE(arrowX < delX) << "the arrow comes first";
    EXPECT_TRUE(qAbs(titleMid - cardMid) <= 4) << "the title is centred" << " -- " << (QStringLiteral("off by %1 px").arg(qAbs(titleMid - cardMid))).toStdString();

    const int expandedHeight = card->height();
    const QSize expandedIcon = arrow->iconSize();
    EXPECT_TRUE(!arrow->property("collapsed").toBool()) << "starts expanded";
    window.grab().save(QString::fromLatin1(kScratch) + QStringLiteral("rules_expanded.png"));

    arrow->click();
    settle();
    const int collapsedHeight = card->height();
    EXPECT_TRUE(arrow->property("collapsed").toBool()) << "the arrow turns to point right";
    // Both directions must be the same size, or open and closed look unrelated.
    const QSize closedIcon = arrow->iconSize();
    EXPECT_TRUE(closedIcon == expandedIcon) << "open and closed markers are the same size" << " -- " << (QStringLiteral("%1x%2 vs %3x%4").arg(expandedIcon.width()).arg(expandedIcon.height())
              .arg(closedIcon.width()).arg(closedIcon.height())).toStdString();
    EXPECT_TRUE(collapsedHeight < expandedHeight / 2) << "the card shrinks to one line" << " -- " << (QStringLiteral("%1 px -> %2 px").arg(expandedHeight).arg(collapsedHeight)).toStdString();
    EXPECT_TRUE(heading->isVisible()) << "the title survives";
    EXPECT_TRUE(del->isVisible()) << "so does the delete button";
    window.grab().save(QString::fromLatin1(kScratch) + QStringLiteral("rules_collapsed.png"));

    arrow->click();
    settle();
    EXPECT_TRUE(card->height() == expandedHeight) << "expanding restores it" << " -- " << (QStringLiteral("%1 px").arg(card->height())).toStdString();

    window.close();
}
