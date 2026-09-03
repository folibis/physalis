#pragma once

#include <QPointer>
#include <QVector>
#include <QIcon>
#include <QSet>
#include <QWidget>

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

private:
    void setCollapsed(int index, bool collapsed);

    // Actions share the property dropdown, tagged so they can be told from
    // real properties.
    QVariantMap defaultActionParams(const QString &id) const;
    // The editors for one action's own parameters, stacked into a single
    // widget the value column can hold. Returns a plain label when the action
    // has none, so there is always something to put there.
    QWidget *buildActionParamEditor(int index, const Rule &rule, QWidget *parent);
    static QString actionKey(const QString &id);
    static QString actionIdOf(const QString &key);
    static void setCollapseLook(QToolButton *button, bool collapsed);

    // Which cards are folded, by rule index. Rebuilds recreate every card, so
    // this has to live outside them.
    QSet<int> m_collapsed;

    struct Row {
        QWidget *card = nullptr;
        QWidget *body = nullptr;      // everything under the title, hidden when collapsed
        QToolButton *collapse = nullptr;
        // The caption, and the box that replaces it while it is being renamed.
        QLabel *heading = nullptr;
        QLineEdit *headingEdit = nullptr;
        ObjectComboBox *source = nullptr;
        ObjectComboBox *event = nullptr;
        QComboBox *compare = nullptr;
        QWidget *condition = nullptr;
        QWidget *conditionHolder = nullptr;
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
        int valueRow = -1;
        int sourceRow = -1;
        QWidget *valueHolder = nullptr;
    };

    void buildUi();
    void connectScene();
    void rebuild();
    void addRule();
    void removeRule(int index);

    QWidget *buildCard(int index);
    void refreshEvents(int index);
    void refreshProperties(int index);
    // Double-clicking a caption turns it into a text box; QLabel has no signal
    // for that, so the panel watches the label itself.
    bool eventFilter(QObject *watched, QEvent *event) override;
    QString captionFor(int index) const;
    void beginRename(int index);
    void finishRename(int index, bool keep);

    void refreshValueEditor(int index);
    // The same, but after the current signal has finished being delivered:
    // the rebuild deletes the widgets a handler was called from.
    void scheduleValueEditorRefresh(int index);
    void refreshConditionEditor(int index);
    void applyWatchChoice(int index, const QString &chosen);
    const physics::JointParam *describe(const QString &objectName, const QString &key) const;
    bool propertyIsFlag(const QString &objectName, const QString &key) const;
    // A property the engine offers as one of a fixed set rather than as a
    // number. The editor knows nothing about what the choices mean; it shows
    // the labels it was given and stores the index.
    bool propertyIsChoice(const QString &objectName, const QString &key) const;

    QVector<RuleChoice> sourceChoices() const;
    QVector<RuleChoice> watchChoices(const QString &name) const;
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
