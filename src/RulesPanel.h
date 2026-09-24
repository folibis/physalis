// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QPointer>
#include <QVector>
#include <QIcon>
#include <QSet>
#include <QWidget>
#include <functional>

#include "Rule.h"
#include "JointTypes.h"

class CanvasScene;
class ObjectComboBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QFormLayout;
class QToolButton;
class QVBoxLayout;
class QWidget;

struct RuleChoice
{
    QString data;
    QString label;
    QIcon icon;
};

class RulesPanel : public QWidget
{
    Q_OBJECT

public:
    // Parent-only, for Designer promotion; the scene arrives after.
    explicit RulesPanel(QWidget *parent = nullptr);
    RulesPanel(CanvasScene *scene, QWidget *parent = nullptr);

    void setScene(CanvasScene *scene);

    // How many rules are still unfinished. The cards say so themselves, but
    // the panel is one tab of three and may not be the one on top.
    int incompleteCount() const;

signals:
    void incompleteCountChanged(int count);

private:
    void setCollapsed(int index, bool collapsed);

    // Actions share the property dropdown, tagged so they can be told from
    // real properties.
    QVariantMap defaultActionParams(const QString &id) const;
    // The editors for one action's own parameters, stacked into a single
    // widget the value column can hold. Returns a plain label when the action
    // has none, so there is always something to put there.
    QWidget *buildActionParamEditor(int index, int slot, const RuleAction &action,
                                    QWidget *parent);
    static QString actionKey(const QString &id);
    static QString actionIdOf(const QString &key);
    static void setCollapseLook(QToolButton *button, bool collapsed);
    // Why a rule will not run, in a sentence, for the marker's tooltip.
    static QString problemText(Rule::Problem problem);
    // The card's borders and heading. A rule that is switched off goes pale; an
    // unfinished one goes red, whether it is switched off or not, because it
    // would not run either way and that is the part worth noticing.
    static void setCardLook(QWidget *card, QLabel *heading, bool enabled, bool complete);
    // The mark on one When or Then block, so a card with several rows says
    // which of them is the unfinished one.
    static void setRowWarning(QLabel *warning, Rule::Problem problem);
    // Border, heading and marker for one card, after its rule has changed.
    // Touches no widget's lifetime, so it is safe from inside a signal handler
    // where a rebuild would not be.
    void refreshCardLook(int index);
    // A caption can be any name the user types. Shown in full where it fits
    // and cut short where it does not, so the card's width is never decided
    // by how long somebody named a rule.
    static void setHeadingText(QLabel *label, const QString &caption);

    // Which cards are folded, by rule index. Rebuilds recreate every card, so
    // this has to live outside them.
    QSet<int> m_collapsed;

    // One "When" block: the object watched, what is watched on it, and the
    // test. A card holds as many as the rule has conditions.
    struct ConditionRow {
        QWidget *block = nullptr;     // everything belonging to this condition
        QLabel *warning = nullptr;    // shown while this row is the unfinished one
        ObjectComboBox *source = nullptr;
        ObjectComboBox *event = nullptr;
        QComboBox *compare = nullptr;
        QWidget *condition = nullptr;
        QWidget *conditionHolder = nullptr;
    };

    // One "Then/Do" block: what is acted on and what happens to it.
    struct ActionRow {
        QWidget *block = nullptr;
        QLabel *warning = nullptr;
        ObjectComboBox *target = nullptr;
        ObjectComboBox *property = nullptr;
        QComboBox *op = nullptr;
        QWidget *value = nullptr;
        // Where a value is read from, when it is not typed.
        QComboBox *valueMode = nullptr;
        ObjectComboBox *source2 = nullptr;
        ObjectComboBox *sourceProperty = nullptr;
        QWidget *sourceOffset = nullptr;
        QWidget *sourceHolder = nullptr;
        QFormLayout *form = nullptr;
        int sourceRow = -1;
        QWidget *valueHolder = nullptr;
    };

    struct Row {
        QWidget *card = nullptr;
        QWidget *body = nullptr;      // everything under the title, hidden when collapsed
        QToolButton *collapse = nullptr;
        // The caption, and the box that replaces it while it is being renamed.
        QLabel *heading = nullptr;
        QLineEdit *headingEdit = nullptr;
        // Shown beside the caption while the rule is unfinished, and carrying
        // the reason as its tooltip. In the header rather than the body: a
        // folded card hides the body, and that is exactly when the mark has to
        // still be there.
        QLabel *warning = nullptr;
        // All-of or any-of, shown only once there is more than one condition.
        QComboBox *join = nullptr;
        QVector<ConditionRow> conditions;
        QVector<ActionRow> actions;
    };

    void buildUi();
    void connectScene();
    void rebuild();
    void addRule();
    void removeRule(int index);
    // Rules are applied in the order they are listed, so the order is part of
    // what a scene means, not a display preference.
    void moveRule(int from, int to);
    QWidget *buildCard(int index);
    // One When block and one Then/Do block. `slot` is the condition's or the
    // action's place in the rule's own list.
    QWidget *buildConditionBlock(int index, int slot);
    QWidget *buildActionBlock(int index, int slot);
    // The and/or drawn in the gap between two conditions.
    QWidget *buildJoiner(int index, QWidget *parent);
    // The small add/remove button at the end of a When or Then row.
    QToolButton *rowButton(QWidget *parent, const QIcon &icon, const QString &tip,
                           const std::function<void()> &onClick);
    // Adding or removing a row changes the card's shape, so it rebuilds where
    // an ordinary edit only repaints.
    void commitAndRebuild(int index, const Rule &rule, const QString &label);
    void addCondition(int index);
    void removeCondition(int index, int slot);
    void addAction(int index);
    void removeAction(int index, int slot);
    void refreshEvents(int index, int slot);
    void refreshProperties(int index, int slot);
    // Double-clicking a caption turns it into a text box; QLabel has no signal
    // for that, so the panel watches the label itself.
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString captionFor(int index) const;
    void beginRename(int index);
    void finishRename(int index, bool keep);

    void refreshValueEditor(int index, int slot);
    // The same, but after the current signal has finished being delivered:
    // the rebuild deletes the widgets a handler was called from.
    void scheduleValueEditorRefresh(int index, int slot);
    void refreshConditionEditor(int index, int slot);
    void applyWatchChoice(int index, int slot, const QString &chosen);
    const physics::JointParam *describe(const QString &objectName, const QString &key) const;
    bool propertyIsFlag(const QString &objectName, const QString &key) const;
    // A property the engine offers as one of a fixed set rather than as a
    // number. The editor knows nothing about what the choices mean; it shows
    // the labels it was given and stores the index.
    bool propertyIsChoice(const QString &objectName, const QString &key) const;

    QVector<RuleChoice> sourceChoices() const;
    QVector<RuleChoice> watchChoices(const QString &name) const;
    // What the engine says this object can raise, and whether the one it is
    // watching happens *with* something -- see physics::EventType::namesOther.
    QVector<physics::EventType> eventsFor(const QString &name) const;
    bool eventNamesOther(const QString &name, const QString &eventId) const;
    QVector<RuleChoice> targetChoices() const;
    // Objects a value can be read from, and what each of them can be read for.
    QVector<RuleChoice> sourceObjectChoices() const;
    QVector<RuleChoice> readablesOf(const QString &name) const;

    void showEvent(QShowEvent *event) override;

    QVector<RuleChoice> propertiesOf(const QString &name) const;

    void commit(int index, const Rule &rule);

    QPointer<CanvasScene> m_scene;
    QVBoxLayout *m_cards = nullptr;
    QVector<Row> m_rows;

    bool m_building = false;
};
