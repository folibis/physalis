#include "RulesPanel.h"

#include "CanvasScene.h"
#include "Joint.h"
#include "ExplosionItem.h"
#include "PhysicsBody.h"
#include "ShapeItem.h"
#include "Icons.h"
#include "RayItem.h"
#include "ObjectIcons.h"
#include "EngineRegistry.h"

#include <QApplication>
#include <QCheckBox>
#include <QMouseEvent>
#include <algorithm>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScrollArea>
#include <QSizePolicy>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {

const QString kEventPrefix = QStringLiteral("@event:");

// QFormLayout::setRowVisible arrived in Qt 6.4, and this tree still builds
// against Qt 5.15. Hiding both items of the row is what it does underneath.
void setFormRowVisible(QFormLayout *form, int row, bool on)
{
    if (!form || row < 0 || row >= form->rowCount())
        return;
    for (QFormLayout::ItemRole role : { QFormLayout::LabelRole, QFormLayout::FieldRole }) {
        if (QLayoutItem *item = form->itemAt(row, role)) {
            if (QWidget *widget = item->widget())
                widget->setVisible(on);
        }
    }
}

} // namespace

class ObjectComboBox : public QComboBox
{
public:
    using Source = std::function<QVector<RuleChoice>()>;

    ObjectComboBox(Source source, QWidget *parent)
        : QComboBox(parent)
        , m_source(std::move(source))
    {
        setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        setMinimumContentsLength(10);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }

    // Keeps the chosen entry selected by data, not index: a position is not
    // stable across a rename or a deletion.
    void refill()
    {
        const QString chosen = currentData().toString();
        const QSignalBlocker blocker(this);
        clear();
        for (const auto &choice : m_source())
            addItem(choice.icon, choice.label, choice.data);
        if (count() == 0)
            addItem(m_emptyText, QString());
        setCurrentIndex(qMax(0, findData(chosen)));
    }

    void selectData(const QString &data)
    {
        refill();
        const QSignalBlocker blocker(this);
        setCurrentIndex(qMax(0, findData(data)));
    }

    void setEmptyText(const QString &text) { m_emptyText = text; }

    void showPopup() override
    {
        refill();
        QComboBox::showPopup();
    }

private:
    Source m_source;
    QString m_emptyText = QObject::tr("(nothing to choose)");
};


RulesPanel::RulesPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

RulesPanel::RulesPanel(CanvasScene *scene, QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    setScene(scene);
}

void RulesPanel::setScene(CanvasScene *scene)
{
    if (m_scene == scene)
        return;
    m_scene = scene;
    connectScene();
    rebuild();
}

namespace {

// Recolours an icon's opaque pixels, keeping its shape.
QIcon tintedIcon(const QIcon &icon, const QColor &colour, int size)
{
    QPixmap pm = icon.pixmap(size, size);
    QPainter painter(&pm);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pm.rect(), colour);
    painter.end();
    return QIcon(pm);
}

// Painted rather than typed, for the same reason the collapse marker is: the
// arrow glyphs differ wildly between fonts and some render as a box.
//
// It is drawn with a shaft, and the collapse marker is a bare triangle. Side
// by side in the same header at the same size, the outline is the only thing
// that tells them apart -- two plain triangles read as the same button.
QIcon arrowIcon(bool up)
{
    const int side = 14;
    QPixmap pm(side, side);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x50, 0x50, 0x50));

    QPolygonF head;
    if (up) {
        head << QPointF(7, 2) << QPointF(12, 7.5) << QPointF(2, 7.5);
        painter.drawPolygon(head);
        painter.drawRect(QRectF(5.5, 7.0, 3.0, 5.0));
    } else {
        head << QPointF(2, 6.5) << QPointF(12, 6.5) << QPointF(7, 12);
        painter.drawPolygon(head);
        painter.drawRect(QRectF(5.5, 2.0, 3.0, 5.0));
    }
    painter.end();

    // A rule at either end has nowhere to go that way, and the button says so
    // by being disabled. Qt's own faded version of a flat grey glyph is close
    // enough to the live one to be missed, so the off state is drawn.
    QPixmap off(side, side);
    off.fill(Qt::transparent);
    QPainter faded(&off);
    faded.setRenderHint(QPainter::Antialiasing, true);
    faded.setPen(Qt::NoPen);
    faded.setBrush(QColor(0xc4, 0xc4, 0xc4));
    if (up) {
        faded.drawPolygon(QPolygonF({QPointF(7, 2), QPointF(12, 7.5), QPointF(2, 7.5)}));
        faded.drawRect(QRectF(5.5, 7.0, 3.0, 5.0));
    } else {
        faded.drawPolygon(QPolygonF({QPointF(2, 6.5), QPointF(12, 6.5), QPointF(7, 12)}));
        faded.drawRect(QRectF(5.5, 2.0, 3.0, 5.0));
    }
    faded.end();

    QIcon icon(pm);
    icon.addPixmap(off, QIcon::Disabled);
    return icon;
}

} // namespace

void RulesPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);   // as the property panel has them
    layout->setSpacing(4);

    // The same header every pane has -- a bold title on the left -- rather
    // than a strip of its own. The colour is the one the Edit mode button and
    // the app icon are drawn in, so the panel is recognisably the same family.
    const QColor accent = EditorModes::accent(EditorMode::Edit);

    auto *headerBar = new QWidget(this);
    auto *header = new QHBoxLayout(headerBar);
    header->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel(tr("Rules"), headerBar);
    title->setStyleSheet(QStringLiteral("font-weight: bold; padding: 2px; color: %1;")
                             .arg(accent.name()));

    auto *add = new QToolButton(headerBar);
    // Drawn in the title's colour, so the two read as one heading.
    add->setIcon(tintedIcon(Icons::add(), accent, 16));
    add->setToolTip(tr("Add a rule"));
    add->setAutoRaise(true);
    connect(add, &QToolButton::clicked, this, &RulesPanel::addRule);

    header->addWidget(title);
    header->addStretch();
    header->addWidget(add);
    layout->addWidget(headerBar);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // The panel is a narrow dock and the cards are built to shrink into it.
    // Left on, a card that momentarily wants a few pixels more puts a bar
    // across the bottom and takes the height away from every card at once.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *host = new QWidget(scroll);
    m_cards = new QVBoxLayout(host);
    m_cards->setContentsMargins(6, 6, 6, 6);
    m_cards->setSpacing(8);
    m_cards->addStretch();
    scroll->setWidget(host);
    layout->addWidget(scroll, 1);

}

void RulesPanel::connectScene()
{
    if (m_scene) {
        connect(m_scene, &CanvasScene::rulesChanged, this, &RulesPanel::rebuild);
        connect(m_scene, &CanvasScene::bodiesChanged, this, &RulesPanel::rebuild);
        connect(m_scene, &CanvasScene::jointsChanged, this, &RulesPanel::rebuild);
        // Editing rules mid-run would change the thing being watched.
        connect(m_scene, &CanvasScene::simulationRunningChanged, this,
                [this](bool running) { setEnabled(!running); });
        setEnabled(m_scene->selectionAllowed());
    }
}

void RulesPanel::rebuild()
{
    if (!m_scene)
        return;

    m_building = true;
    for (const Row &row : std::as_const(m_rows))
        delete row.card;
    m_rows.clear();

    for (int i = 0; i < m_scene->rules().size(); ++i) {
        QWidget *card = buildCard(i);
        m_cards->insertWidget(m_cards->count() - 1, card); // before the stretch
    }
    m_building = false;

    for (int i = 0; i < m_rows.size(); ++i) {
        for (int slot = 0; slot < m_rows[i].conditions.size(); ++slot) {
            refreshEvents(i, slot);
            refreshConditionEditor(i, slot);
        }
        for (int slot = 0; slot < m_rows[i].actions.size(); ++slot) {
            refreshProperties(i, slot);
            refreshValueEditor(i, slot);
        }
        // Last, because the four above can settle a rule's own fields -- a
        // watch that no longer exists, a value editor that fills in a default
        // -- and the card has to show what it ended up as.
        refreshCardLook(i);
    }

    emit incompleteCountChanged(incompleteCount());
}

QWidget *RulesPanel::buildCard(int index)
{
    const Rule rule = m_scene->rules().at(index);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("ruleCard"));
    card->setFrameShape(QFrame::NoFrame);
    // Drawn here rather than left to the style: QFrame::StyledPanel gave some
    // cards no top border at all, and the panel behind is the same grey.
    // Styled through the helper, so the switched-off look and the live one
    // are described in one place.
    setCardLook(card, nullptr, rule.enabled, rule.isValid());
    // No margins of its own: the header band has to reach the card's edges,
    // so the padding belongs to the header and the body separately.
    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(2);

    // Whether the rule runs at all. Off, it is still here and still saved --
    // the simulation simply passes over it. Trying something without a rule in
    // the way should not mean deleting the rule and writing it again.
    auto *enabled = new QCheckBox(card);
    enabled->setObjectName(QStringLiteral("ruleEnabled"));
    enabled->setChecked(rule.enabled);
    enabled->setToolTip(tr("Whether this rule runs. Unticked it is kept and saved,"
                           " and the simulation passes over it."));
    titleRow->addWidget(enabled);

    auto *collapse = new QToolButton(card);
    collapse->setObjectName(QStringLiteral("collapseButton"));
    collapse->setAutoRaise(true);
    collapse->setStyleSheet(QStringLiteral(
        "QToolButton#collapseButton { border: none; background: transparent; }"));
    setCollapseLook(collapse, m_collapsed.contains(index));
    titleRow->addWidget(collapse);

    auto *remove = new QToolButton(card);
    remove->setObjectName(QStringLiteral("removeRuleButton"));
    remove->setIcon(Icons::deleteShape());
    remove->setToolTip(tr("Remove this rule"));
    remove->setAutoRaise(true);
    // removeRule() rebuilds the whole panel and deletes this very button --
    // doing that while the click signal is still on the stack is the crash
    // documented at the top of CLAUDE.md. Queue it so the signal returns first.
    connect(remove, &QToolButton::clicked, this, [this, index] {
        QMetaObject::invokeMethod(this, [this, index] { removeRule(index); },
                                  Qt::QueuedConnection);
    });
    titleRow->addWidget(remove);

    titleRow->addStretch();

    // Sits immediately left of the caption while the rule is unfinished. Inside
    // the stretches, so the caption and the mark centre as one and the header's
    // balancing widget is unaffected.
    auto *warning = new QLabel(card);
    warning->setObjectName(QStringLiteral("ruleWarning"));
    warning->setPixmap(Icons::warning().pixmap(14, 14));
    warning->setVisible(false);
    titleRow->addWidget(warning);

    auto *heading = new QLabel(card);
    heading->setStyleSheet(QStringLiteral("font-weight: bold; color: #6f6f6f;"));
    heading->setAlignment(Qt::AlignCenter);
    // The caption gives up width before the buttons either side of it do, so
    // a narrow dock does not grow a scrollbar -- but never all of it. Ignored,
    // the layout squeezed it to nothing: the name vanished, and with it the
    // only place to double-click to rename the rule.
    heading->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    heading->setMinimumWidth(40);
    setHeadingText(heading, captionFor(index));
    heading->installEventFilter(this);
    heading->setProperty("ruleIndex", index);
    titleRow->addWidget(heading);

    // Sits in the caption's place while the name is being typed.
    auto *rename = new QLineEdit(card);
    rename->setVisible(false);
    rename->setAlignment(Qt::AlignHCenter);
    rename->setProperty("ruleIndex", index);
    rename->installEventFilter(this);
    connect(rename, &QLineEdit::editingFinished, this, [this, index] {
        finishRename(index, true);
    });
    titleRow->addWidget(rename);

    titleRow->addStretch();

    // Rules are applied in the order they are listed, so moving one is an
    // edit like any other. Two buttons rather than a drag: the card is a form
    // full of controls, and there is nowhere on it to grab that does not
    // already belong to one of them.
    const int lastIndex = m_scene->rules().size() - 1;

    auto *moveUp = new QToolButton(card);
    moveUp->setObjectName(QStringLiteral("moveRuleUp"));
    moveUp->setIcon(arrowIcon(true));
    moveUp->setToolTip(tr("Move this rule up. Rules are applied in the order"
                          " they are listed."));
    moveUp->setAutoRaise(true);
    moveUp->setEnabled(index > 0);
    // Moving rebuilds every card, this button included, and doing that while
    // its own click is still on the stack is the crash at the top of CLAUDE.md.
    connect(moveUp, &QToolButton::clicked, this, [this, index] {
        QMetaObject::invokeMethod(this, [this, index] { moveRule(index, index - 1); },
                                  Qt::QueuedConnection);
    });

    auto *moveDown = new QToolButton(card);
    moveDown->setObjectName(QStringLiteral("moveRuleDown"));
    moveDown->setIcon(arrowIcon(false));
    moveDown->setToolTip(tr("Move this rule down."));
    moveDown->setAutoRaise(true);
    moveDown->setEnabled(index < lastIndex);
    connect(moveDown, &QToolButton::clicked, this, [this, index] {
        QMetaObject::invokeMethod(this, [this, index] { moveRule(index, index + 1); },
                                  Qt::QueuedConnection);
    });

    // Balances the left pair against the right, so the title sits centred on
    // the card rather than on what is left over between them.
    auto *balance = new QWidget(card);
    balance->setFixedWidth(qMax(0, enabled->sizeHint().width()
                                       + collapse->sizeHint().width()
                                       + remove->sizeHint().width()
                                       - moveUp->sizeHint().width()
                                       - moveDown->sizeHint().width()));
    titleRow->addWidget(balance);
    titleRow->addWidget(moveUp);
    titleRow->addWidget(moveDown);

    auto *headerBand = new QWidget(card);
    headerBand->setObjectName(QStringLiteral("ruleCardHeader"));
    headerBand->setLayout(titleRow);
    titleRow->setContentsMargins(6, 3, 6, 3);
    outer->addWidget(headerBand);

    // Now the caption exists, so the pale look can reach it too.
    const Rule::Problem problem = rule.problem();
    setCardLook(card, heading, rule.enabled, problem == Rule::Problem::None);
    if (problem != Rule::Problem::None) {
        warning->setVisible(true);
        warning->setToolTip(problemText(problem));
    }
    connect(enabled, &QCheckBox::toggled, this, [this, index](bool on) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        if (updated.enabled == on)
            return;
        updated.enabled = on;
        // commit() repaints the card. Only the look changes, so there is
        // nothing to rebuild -- and rebuilding here would delete the box while
        // its own signal is still being delivered.
        commit(index, updated);
    });

    Row row;
    row.card = card;
    row.warning = warning;
    row.collapse = collapse;
    row.heading = heading;
    row.headingEdit = rename;

    auto *body = new QWidget(card);
    auto *stack = new QVBoxLayout(body);
    stack->setContentsMargins(8, 6, 8, 8);
    stack->setSpacing(6);
    body->setVisible(!m_collapsed.contains(index));
    outer->addWidget(body);
    row.body = body;

    // The row has to be in place before the blocks are built: each of them
    // stores its own widgets into it as it goes.
    m_rows.append(row);

    for (int slot = 0; slot < rule.conditions.size(); ++slot) {
        // The joiner is drawn in the gap it applies to, so the card reads down
        // as a sentence: this, and this, and this.
        if (slot > 0)
            stack->addWidget(buildJoiner(index, body));
        stack->addWidget(buildConditionBlock(index, slot));
    }

    for (int slot = 0; slot < rule.actions.size(); ++slot)
        stack->addWidget(buildActionBlock(index, slot));

    connect(collapse, &QToolButton::clicked, this,
            [this, index] { setCollapsed(index, !m_collapsed.contains(index)); });
    return card;
}

// The word between two conditions. One joiner belongs to the whole rule, not
// to the gap it is drawn in: "a and b or c" without brackets reads two ways,
// and a card that can be written ambiguously is worse than one that cannot.
// Every gap therefore shows the same word, and setting one sets them all.
QWidget *RulesPanel::buildJoiner(int index, QWidget *parent)
{
    const Rule rule = m_scene->rules().at(index);

    auto *holder = new QWidget(parent);
    auto *layout = new QHBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto *combo = new QComboBox(holder);
    combo->addItem(tr("and"), Rule::joinName(Rule::Join::All));
    combo->addItem(tr("or"), Rule::joinName(Rule::Join::Any));
    combo->setCurrentIndex(combo->findData(Rule::joinName(rule.join)));
    combo->setToolTip(tr("Whether every condition has to be true, or any one of"
                         " them. It is the same choice all the way down the card."
                         "\n\nAn event happens on a single step, so joining two"
                         " events with \"and\" asks for both on the same step."));
    connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, index, combo](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        const Rule::Join picked = Rule::joinFromName(combo->currentData().toString());
        if (updated.join == picked)
            return;
        updated.join = picked;
        // Every other gap shows the same word and has to follow.
        commitAndRebuild(index, updated, tr("Edit rule"));
    });

    layout->addWidget(combo, 0);
    layout->addStretch(1);
    return holder;
}

// The small button at the end of a When or Then row: one more of these, or
// this one gone. Rebuilding deletes the button, which cannot happen while its
// own click is still on the stack, so both are queued.
QToolButton *RulesPanel::rowButton(QWidget *parent, const QIcon &icon, const QString &tip,
                                   const std::function<void()> &onClick)
{
    auto *button = new QToolButton(parent);
    button->setIcon(icon);
    button->setIconSize(QSize(12, 12));
    button->setAutoRaise(true);
    button->setToolTip(tip);
    button->setFixedWidth(20);
    connect(button, &QToolButton::clicked, this, [this, onClick] {
        QMetaObject::invokeMethod(this, [onClick] { onClick(); }, Qt::QueuedConnection);
    });
    return button;
}

QWidget *RulesPanel::buildConditionBlock(int index, int slot)
{
    const Rule rule = m_scene->rules().at(index);
    const RuleCondition condition = rule.conditions.at(slot);

    auto *block = new QWidget(m_rows[index].body);
    auto *stack = new QVBoxLayout(block);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(2);

    ConditionRow cond;
    cond.block = block;

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // A row stays on its line. Letting the fields wrap under their labels was
    // what put "Set to" and the number on a line of their own, which reads as
    // two separate things rather than one instruction -- so the captions are
    // kept short ("Then:", "Do:") and the controls give up width instead.
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    stack->addLayout(form);

    cond.source = new ObjectComboBox([this] { return sourceChoices(); }, block);
    cond.source->setEmptyText(tr("(no objects yet)"));
    cond.source->selectData(condition.subjectName);
    connect(cond.source, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, index, slot](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        RuleCondition &c = updated.conditions[slot];
        c.subjectName = m_rows[index].conditions[slot].source->currentData().toString();
        c.conditionKey.clear();
        c.eventId.clear();
        commit(index, updated);
        refreshEvents(index, slot);
        refreshConditionEditor(index, slot);
        // The value editors offer to read from whatever the rule watches, so
        // they follow a change of subject too.
        for (int a = 0; a < m_rows[index].actions.size(); ++a) {
            refreshProperties(index, a);
            scheduleValueEditorRefresh(index, a);
        }
    });
    // The first one carries the caption and the button that adds another; the
    // ones below it carry a button to take themselves away again. The mark for
    // the row sits on the same line, so it is beside the field it is about.
    auto *whenRow = new QHBoxLayout;
    whenRow->setSpacing(4);
    whenRow->addWidget(cond.source, 1);

    cond.warning = new QLabel(block);
    cond.warning->setPixmap(Icons::warning().pixmap(12, 12));
    cond.warning->setVisible(false);
    whenRow->addWidget(cond.warning);

    if (slot == 0) {
        whenRow->addWidget(rowButton(block, Icons::add(), tr("Add another condition"),
                                     [this, index] { addCondition(index); }));
    } else {
        whenRow->addWidget(rowButton(block, Icons::deleteShape(), tr("Remove this condition"),
                                     [this, index, slot] { removeCondition(index, slot); }));
    }
    // Each block has its own form, and a form sizes its label column to what
    // is in it -- so a block whose labels are all blank would start its fields
    // hard against the left edge while the first block's began past "When:".
    // The width is held the same across them by hand.
    auto *whenLabel = new QLabel(slot == 0 ? tr("When:") : QString(), block);
    whenLabel->setMinimumWidth(QFontMetrics(whenLabel->font()).horizontalAdvance(tr("When:")));
    form->addRow(whenLabel, whenRow);

    // What to watch on it -- hidden for a touch, which is not a property.
    cond.event = new ObjectComboBox(
        [this, index, slot] {
            const QVector<Rule> &rules = m_scene->rules();
            if (index >= rules.size() || slot >= rules.at(index).conditions.size())
                return watchChoices(QString());
            return watchChoices(rules.at(index).conditions.at(slot).subjectName);
        },
        block);
    cond.event->setEmptyText(tr("(nothing readable)"));
    connect(cond.event, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, index, slot](int) {
        if (m_building)
            return;
        applyWatchChoice(index, slot,
                         m_rows[index].conditions[slot].event->currentData().toString());
        refreshEvents(index, slot);
        refreshConditionEditor(index, slot);
    });
    form->addRow(QString(), cond.event);

    auto *testRow = new QHBoxLayout;
    testRow->setSpacing(4);

    cond.compare = new QComboBox(block);
    cond.compare->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cond.compare->setMinimumContentsLength(8);
    cond.compare->addItem(tr("is greater than"), Rule::compareName(Rule::Compare::Greater));
    cond.compare->addItem(tr("is less than"), Rule::compareName(Rule::Compare::Less));
    cond.compare->addItem(tr("is at least"), Rule::compareName(Rule::Compare::GreaterEqual));
    cond.compare->addItem(tr("is at most"), Rule::compareName(Rule::Compare::LessEqual));
    cond.compare->addItem(tr("equals"), Rule::compareName(Rule::Compare::Equal));
    cond.compare->addItem(tr("differs from"), Rule::compareName(Rule::Compare::NotEqual));
    cond.compare->addItem(tr("is a multiple of"), Rule::compareName(Rule::Compare::Multiple));
    cond.compare->setCurrentIndex(cond.compare->findData(Rule::compareName(condition.compare)));
    cond.compare->setToolTip(tr("The actions run when the rule becomes true, not for as"
                                " long as it stays true."));
    connect(cond.compare, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, index, slot](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        RuleCondition &c = updated.conditions[slot];
        c.compare = Rule::compareFromName(
            m_rows[index].conditions[slot].compare->currentData().toString());
        c.conditionValue = QVariant();
        commit(index, updated);
        refreshEvents(index, slot);
        refreshConditionEditor(index, slot);
    });
    testRow->addWidget(cond.compare, 0);

    cond.conditionHolder = new QWidget(block);
    auto *condLayout = new QHBoxLayout(cond.conditionHolder);
    condLayout->setContentsMargins(0, 0, 0, 0);
    testRow->addWidget(cond.conditionHolder, 1);
    cond.conditionHolder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    form->addRow(QString(), testRow);

    m_rows[index].conditions.append(cond);
    return block;
}

QWidget *RulesPanel::buildActionBlock(int index, int slot)
{
    const Rule rule = m_scene->rules().at(index);
    const RuleAction action = rule.actions.at(slot);

    auto *block = new QWidget(m_rows[index].body);
    auto *stack = new QVBoxLayout(block);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(2);

    ActionRow act;
    act.block = block;

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    stack->addLayout(form);

    act.target = new ObjectComboBox([this] { return targetChoices(); }, block);
    act.target->selectData(action.targetName);
    connect(act.target, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, index, slot](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        const QString picked = m_rows[index].actions[slot].target->currentData().toString();
        if (picked == updated.actions.at(slot).targetName)
            return; // the list was rebuilt under it; the rule has not changed
        updated.actions[slot].targetName = picked;
        // a different object has different properties
        updated.actions[slot].propertyKey.clear();
        commit(index, updated);
        refreshProperties(index, slot);
        scheduleValueEditorRefresh(index, slot);
    });
    auto *thenRow = new QHBoxLayout;
    thenRow->setSpacing(4);
    thenRow->addWidget(act.target, 1);

    act.warning = new QLabel(block);
    act.warning->setPixmap(Icons::warning().pixmap(12, 12));
    act.warning->setVisible(false);
    thenRow->addWidget(act.warning);

    if (slot == 0) {
        thenRow->addWidget(rowButton(block, Icons::add(), tr("Add another action"),
                                     [this, index] { addAction(index); }));
    } else {
        thenRow->addWidget(rowButton(block, Icons::deleteShape(), tr("Remove this action"),
                                     [this, index, slot] { removeAction(index, slot); }));
    }
    form->addRow(tr("Then:"), thenRow);

    act.property = new ObjectComboBox(
        [this, index, slot] {
            const QVector<Rule> &rules = m_scene->rules();
            if (index >= rules.size() || slot >= rules.at(index).actions.size())
                return propertiesOf(QString());
            return propertiesOf(rules.at(index).actions.at(slot).targetName);
        },
        block);
    act.property->setEmptyText(tr("(nothing changeable)"));
    connect(act.property, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, index, slot](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        const QString chosen = m_rows[index].actions[slot].property->currentData().toString();

        // The same dropdown offers properties and actions; which one it is
        // decides whether the rule writes a value or performs something.
        const QString performed = actionIdOf(chosen);
        RuleAction &a = updated.actions[slot];
        a.actionId = performed;
        if (performed.isEmpty()) {
            a.propertyKey = chosen;
            a.actionParams.clear();
        } else {
            a.propertyKey.clear();
            // An action's settings are its own, and a rule that carried none
            // performed it with every number at zero -- a blast of radius
            // nothing. Start them where the engine says they should start.
            a.actionParams = defaultActionParams(performed);
        }
        commit(index, updated);
        m_rows[index].actions[slot].op->setVisible(performed.isEmpty());
        scheduleValueEditorRefresh(index, slot);
    });
    form->addRow(QString(), act.property);

    // --- what to do -------------------------------------------------------
    auto *doRow = new QHBoxLayout;
    doRow->setSpacing(4);

    act.op = new QComboBox(block);
    act.op->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    act.op->setMinimumContentsLength(4);
    // Never Ignored: that lets the layout squeeze it to nothing, and then the
    // choice between Set, Toggle, Negate and Add is simply gone from the card.
    // It keeps the width its shortest entry needs; the number beside it is the
    // part that gives way.
    act.op->addItem(tr("Set to"), static_cast<int>(Rule::Op::Set));
    act.op->addItem(tr("Toggle"), static_cast<int>(Rule::Op::Toggle));
    act.op->addItem(tr("Negate"), static_cast<int>(Rule::Op::Negate));
    act.op->addItem(tr("Add"), static_cast<int>(Rule::Op::Add));
    act.op->setCurrentIndex(act.op->findData(static_cast<int>(action.op)));
    act.op->setToolTip(tr("Negate flips the sign, which is how a motor reverses at a limit."));
    connect(act.op, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, index, slot](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        updated.actions[slot].op =
            static_cast<Rule::Op>(m_rows[index].actions[slot].op->currentData().toInt());
        commit(index, updated);
        scheduleValueEditorRefresh(index, slot);
    });
    doRow->addWidget(act.op);

    // What the number is: typed here, or read off another object each time the
    // rule fires. Which of the two editors is shown follows from this.
    act.valueMode = new QComboBox(block);
    act.valueMode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    act.valueMode->setMinimumContentsLength(4);
    act.valueMode->addItem(tr("Value"), false);
    act.valueMode->addItem(tr("Property"), true);
    act.valueMode->setToolTip(tr("A number you type, or one taken from another "
                                 "object while the rule runs."));
    connect(act.valueMode, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, index, slot](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        RuleAction &a = updated.actions[slot];
        if (m_rows[index].actions[slot].valueMode->currentData().toBool()) {
            // Property with nothing picked would leave the rule half-set and
            // silently skipped, so the first candidate is filled in at once.
            if (a.sourceObject.isEmpty()) {
                const QVector<RuleChoice> objects = sourceObjectChoices();
                // The object the rule already watches is the likely one to read
                // from -- "when the ray sees something, go to where it struck".
                const QString watched = updated.conditions.isEmpty()
                                            ? QString()
                                            : updated.conditions.first().subjectName;
                const auto found =
                    std::find_if(objects.begin(), objects.end(),
                                 [&](const RuleChoice &c) { return c.data == watched; });
                if (found != objects.end())
                    a.sourceObject = found->data;
                else if (!objects.isEmpty())
                    a.sourceObject = objects.first().data;
            }
            const QVector<RuleChoice> properties = readablesOf(a.sourceObject);
            if (a.sourceProperty.isEmpty() && !properties.isEmpty())
                a.sourceProperty = properties.first().data;
        } else {
            a.sourceObject.clear();
            a.sourceProperty.clear();
        }
        commit(index, updated);
        scheduleValueEditorRefresh(index, slot);
    });
    doRow->addWidget(act.valueMode);

    // The number reads as part of the sentence -- "Set to 12.5" -- so it sits
    // on the same line as the operation it belongs to, and takes whatever
    // width is left over.
    act.valueHolder = new QWidget(block);
    auto *holderLayout = new QHBoxLayout(act.valueHolder);
    holderLayout->setContentsMargins(0, 0, 0, 0);
    doRow->addWidget(act.valueHolder, 1);
    form->addRow(tr("Do:"), doRow);

    // Where the number comes from, on its own line. Four controls crammed into
    // the Do row left the picker 39 px wide and effectively invisible.
    act.sourceHolder = new QWidget(block);
    // A form, not a row: three controls side by side leave each of them about
    // 38 px wide on a card this narrow.
    auto *sourceLayout = new QFormLayout(act.sourceHolder);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(4);
    sourceLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    sourceLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Value from:"), act.sourceHolder);
    act.sourceRow = form->rowCount() - 1;
    act.form = form;

    m_rows[index].actions.append(act);
    return block;
}

QVector<RuleChoice> RulesPanel::sourceChoices() const
{
    QVector<RuleChoice> choices;
    if (!m_scene)
        return choices;
    // The world itself, so a rule can fire on how long it has been running.
    choices.append({Rule::world(), tr("World"), Icons::world()});
    for (RayItem *ray : m_scene->rays())
        choices.append({ray->name(), ray->name(), Icons::ray()});
    for (Joint *joint : m_scene->joints())
        choices.append({joint->name(), joint->name(),
                        ObjectIcons::forJoint(m_scene->jointTypeColor(joint->typeId()))});
    for (PhysicsBody *body : m_scene->bodies())
        choices.append({body->name(), body->name(),
                        ObjectIcons::forBody(m_scene->bodyColor(body->props().type))});
    for (ShapeItem *shape : m_scene->shapes())
        choices.append({shape->name(), shape->name(), ObjectIcons::forShape(shape)});
    return choices;
}

QVector<RuleChoice> RulesPanel::sourceObjectChoices() const
{
    QVector<RuleChoice> choices;
    if (!m_scene)
        return choices;

    choices.append({Rule::world(), tr("World"), Icons::world()});
    for (RayItem *ray : m_scene->rays())
        choices.append({ray->name(), ray->name(), Icons::ray()});
    for (Joint *joint : m_scene->joints())
        choices.append({joint->name(), joint->name(),
                        ObjectIcons::forJoint(m_scene->jointTypeColor(joint->typeId()))});
    for (PhysicsBody *body : m_scene->bodies())
        choices.append({body->name(), body->name(),
                        ObjectIcons::forBody(m_scene->bodyColor(body->props().type))});
    for (ShapeItem *shape : m_scene->shapes())
        choices.append({shape->name(), shape->name(), ObjectIcons::forShape(shape)});
    return choices;
}

QVector<RuleChoice> RulesPanel::readablesOf(const QString &name) const
{
    // Whatever that object can be asked for -- the same list the condition
    // side offers, minus the events, which are not values.
    QVector<RuleChoice> choices;
    for (const RuleChoice &choice : watchChoices(name)) {
        if (!choice.data.startsWith(QStringLiteral("@event:")))
            choices.append(choice);
    }
    return choices;
}

QVector<RuleChoice> RulesPanel::targetChoices() const
{
    QVector<RuleChoice> choices;
    if (!m_scene)
        return choices;
    choices.append({Rule::otherObject(), tr("the shape that touched it")});
    choices.append({Rule::otherObjectBody(), tr("the body that touched it")});
    // The world is a target as well as a subject: gravity and the solver's own
    // thresholds can be changed while it runs.
    choices.append({Rule::world(), tr("World"), Icons::world()});
    for (Joint *joint : m_scene->joints())
        choices.append({joint->name(), joint->name(),
                        ObjectIcons::forJoint(m_scene->jointTypeColor(joint->typeId()))});
    for (PhysicsBody *body : m_scene->bodies())
        choices.append({body->name(), body->name(),
                        ObjectIcons::forBody(m_scene->bodyColor(body->props().type))});
    for (ShapeItem *shape : m_scene->shapes())
        choices.append({shape->name(), shape->name(), ObjectIcons::forShape(shape)});
    for (ExplosionItem *explosion : m_scene->explosions())
        choices.append({explosion->name(), explosion->name(), Icons::explosion()});
    return choices;
}

void RulesPanel::refreshEvents(int index, int slot)
{
    if (index < 0 || index >= m_rows.size() || slot < 0
        || slot >= m_rows[index].conditions.size()
        || slot >= m_scene->rules().at(index).conditions.size()) {
        return;
    }
    RuleCondition condition = m_scene->rules().at(index).conditions.at(slot);
    ConditionRow &row = m_rows[index].conditions[slot];

    const QString wanted = condition.isEvent() ? kEventPrefix + condition.eventId
                                               : condition.conditionKey;
    row.event->selectData(wanted);

    const QString shown = row.event->currentData().toString();
    if (shown != wanted && !shown.isEmpty()) {
        applyWatchChoice(index, slot, shown);
        condition = m_scene->rules().at(index).conditions.at(slot);
    }
    row.compare->setVisible(!condition.isEvent());
}

QVector<physics::EventType> RulesPanel::eventsFor(const QString &name) const
{
    if (!m_scene)
        return {};

    // The ray's own event is the application's, not an engine's: a ray is
    // something the editor casts, and what it saw is named the same way a
    // touch is.
    if (m_scene->rayNamed(name)) {
        return { {QStringLiteral("rayDetects"), tr("detects"),
                  tr("Raised while the ray is looking at the named shape."), true} };
    }

    auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName());
    if (!engine)
        return {};
    // The run starting is the application's to report, and it happens with
    // nothing: there is no other object to pick.
    if (name == Rule::world()) {
        physics::EventType started;
        started.id = Rule::runStartedEvent();
        started.label = tr("starting simulation");
        started.description = tr("Raised once as the simulation starts, before its first step -- where a rule"
                                 " sets things up.");
        started.namesOther = false;
        return { started };
    }

    for (Joint *joint : m_scene->joints()) {
        if (joint->name() != name)
            continue;
        for (const physics::JointType &type : engine->jointTypes()) {
            if (type.id == joint->typeId())
                return type.events;
        }
        return {};
    }
    // Anything that can be removed can be about to be removed: the
    // application's own event, not one an engine reports.
    physics::EventType aboutToBeRemoved;
    aboutToBeRemoved.id = Rule::aboutToBeRemovedEvent();
    aboutToBeRemoved.label = tr("to be removed");
    aboutToBeRemoved.description = tr("Raised when a rule is about to remove this object. A rule that answers it is"
                                      " carried out instead, and the object stays in the run; with no answer, it"
                                      " is removed.");
    aboutToBeRemoved.namesOther = false;
    for (PhysicsBody *body : m_scene->bodies()) {
        if (body->name() == name)
            return engine->bodyEvents() << aboutToBeRemoved;
    }
    for (ShapeItem *shape : m_scene->shapes()) {
        if (shape->name() == name)
            return engine->shapeEvents() << aboutToBeRemoved;
    }
    return {};
}

bool RulesPanel::eventNamesOther(const QString &name, const QString &eventId) const
{
    for (const physics::EventType &event : eventsFor(name)) {
        if (event.id == eventId)
            return event.namesOther;
    }
    return true; // an event nothing published: leave the chooser alone
}

QVector<RuleChoice> RulesPanel::watchChoices(const QString &name) const
{
    QVector<RuleChoice> choices;
    if (!m_scene)
        return choices;

    auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName());
    if (!engine)
        return choices;

    const auto addEvents = [&choices](const QVector<physics::EventType> &events) {
        for (const physics::EventType &e : events)
            choices.append({kEventPrefix + e.id, e.label});
    };
    const auto addReadable = [&choices](const physics::PropertyList &properties) {
        for (const physics::JointParam &p : properties) {
            if (p.liveReadable)
                choices.append({p.key, p.label});
        }
    };

    if (m_scene->rayNamed(name)) {
        // Naming what it sees, so a rule can single out one shape rather than
        // firing on whatever happens to be in the way.
        addEvents(eventsFor(name));
        choices.append({QStringLiteral("distance"), tr("Distance")});
        choices.append({QStringLiteral("hit"), tr("Hit")});
        choices.append({QStringLiteral("hitX"), tr("Hit X")});
        choices.append({QStringLiteral("hitY"), tr("Hit Y")});
        return choices;
    }

    // The run has no events: two numbers that climb as it goes, and whatever
    // the engine says its world can still be asked about.
    if (name == Rule::world()) {
        addEvents(eventsFor(name));
        choices.append({QStringLiteral("time"), tr("Elapsed Time (s)")});
        choices.append({QStringLiteral("frame"), tr("Frame")});
        addReadable(engine->worldProperties());
        return choices;
    }

    for (Joint *joint : m_scene->joints()) {
        if (joint->name() != name)
            continue;
        addEvents(eventsFor(name));
        addReadable(engine->jointReadables(joint->typeId()));
        return choices;
    }
    for (PhysicsBody *body : m_scene->bodies()) {
        if (body->name() != name)
            continue;
        addEvents(eventsFor(name));
        addReadable(engine->bodyProperties());
        return choices;
    }
    for (ShapeItem *shape : m_scene->shapes()) {
        if (shape->name() != name)
            continue;
        addEvents(eventsFor(name));
        addReadable(engine->shapeProperties());
        return choices;
    }
    return choices;
}

void RulesPanel::refreshConditionEditor(int index, int slot)
{
    if (index < 0 || index >= m_rows.size() || slot < 0
        || slot >= m_rows[index].conditions.size()
        || slot >= m_scene->rules().at(index).conditions.size()) {
        return;
    }
    ConditionRow &row = m_rows[index].conditions[slot];
    const RuleCondition condition = m_scene->rules().at(index).conditions.at(slot);

    delete row.condition;
    row.condition = nullptr;

    const bool wasBuilding = m_building;
    m_building = true;

    // What can sensibly be asked of the thing being watched. A flag is true or
    // false: "is greater than 0.5" is not a question about it, and offering
    // the ordering tests is how a rule ends up written that way.
    const bool watchingFlag =
        !condition.isEvent() && propertyIsFlag(condition.subjectName, condition.conditionKey);
    if (row.compare) {
        row.compare->clear();
        if (!watchingFlag) {
            row.compare->addItem(tr("is greater than"),
                                 Rule::compareName(Rule::Compare::Greater));
            row.compare->addItem(tr("is less than"), Rule::compareName(Rule::Compare::Less));
            row.compare->addItem(tr("is at least"),
                                 Rule::compareName(Rule::Compare::GreaterEqual));
            row.compare->addItem(tr("is at most"), Rule::compareName(Rule::Compare::LessEqual));
            row.compare->addItem(tr("equals"), Rule::compareName(Rule::Compare::Equal));
            row.compare->addItem(tr("differs from"),
                                 Rule::compareName(Rule::Compare::NotEqual));
            row.compare->addItem(tr("is a multiple of"),
                                 Rule::compareName(Rule::Compare::Multiple));
        } else {
            row.compare->addItem(tr("is"), Rule::compareName(Rule::Compare::Equal));
            row.compare->addItem(tr("is not"), Rule::compareName(Rule::Compare::NotEqual));
        }
        int at = row.compare->findData(Rule::compareName(condition.compare));
        if (at < 0) {
            // The test it carried is not one that can be asked any more --
            // switched from a number to a flag. Equals is the one that always
            // means something.
            at = row.compare->findData(Rule::compareName(Rule::Compare::Equal));
            Rule updated = m_scene->rules().at(index);
            updated.conditions[slot].compare = Rule::Compare::Equal;
            commit(index, updated);
        }
        row.compare->setCurrentIndex(qMax(0, at));
    }

    if (condition.isEvent() && !eventNamesOther(condition.subjectName, condition.eventId)) {
        // Nothing to choose. A joint arriving at its limit, a body coming to
        // rest -- these happen to one object, and offering "or anything" here
        // only invited the question of what the objects had to do with it.
    } else if (condition.isEvent()) {
        auto *combo = new ObjectComboBox(
            [this] {
                QVector<RuleChoice> choices;
                choices.append({QString(), tr("anything")});
                for (ShapeItem *shape : m_scene->shapes())
                    choices.append({shape->name(), shape->name(),
                                    ObjectIcons::forShape(shape)});
                return choices;
            },
            row.conditionHolder);
        combo->setObjectName(QStringLiteral("conditionOther"));
        combo->setToolTip(tr("Which object, or anything."));
        combo->selectData(condition.conditionValue.toString());
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index, slot, combo](int) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.conditions[slot].conditionValue = combo->currentData().toString();
            commit(index, updated);
        });
        row.condition = combo;
    } else if (propertyIsChoice(condition.subjectName, condition.conditionKey)) {
        // Compared as its index, so "is Dynamic" is a comparison the same way
        // any other is -- but chosen by name.
        const physics::JointParam *param = describe(condition.subjectName, condition.conditionKey);
        auto *combo = new QComboBox(row.conditionHolder);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->addItems(param->choices);
        combo->setToolTip(param->tooltip);
        combo->setCurrentIndex(qBound(0, condition.conditionValue.toInt(),
                                      param->choices.size() - 1));
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index, slot](int at) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.conditions[slot].conditionValue = at;
            commit(index, updated);
        });
        row.condition = combo;
    } else if (watchingFlag) {
        // True or false, chosen by name. A spin box here is what let a switch
        // be tested against 0.5.
        auto *combo = new QComboBox(row.conditionHolder);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->addItem(tr("true"), true);
        combo->addItem(tr("false"), false);
        combo->setCurrentIndex(condition.conditionValue.toBool() ? 0 : 1);
        if (const physics::JointParam *param = describe(condition.subjectName, condition.conditionKey))
            combo->setToolTip(param->tooltip);
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index, slot, combo](int) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.conditions[slot].conditionValue = combo->currentData().toBool();
            commit(index, updated);
        });
        row.condition = combo;
    } else {
        auto *spin = new QDoubleSpinBox(row.conditionHolder);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        spin->setMinimumWidth(70);
        spin->setRange(-1e7, 1e7);
        spin->setDecimals(1);
        spin->setSingleStep(10.0);
        spin->setValue(condition.conditionValue.toDouble());
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, index, slot](double v) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.conditions[slot].conditionValue = v;
            commit(index, updated);
        });
        row.condition = spin;
    }

    // There may be nothing to add: an event that names no second object asks
    // nothing further, and the holder stays empty.
    if (row.condition)
        row.conditionHolder->layout()->addWidget(row.condition);
    m_building = wasBuilding;
}

QVector<RuleChoice> RulesPanel::propertiesOf(const QString &name) const
{
    QVector<RuleChoice> result;
    if (!m_scene)
        return result;

    auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName());
    if (!engine)
        return result;

    const auto addSettable = [&result](const physics::PropertyList &properties,
                                       bool qualify) {
        for (const physics::JointParam &p : properties) {
            if (!p.liveSettable)
                continue;
            result.append({p.key,
                           (qualify && !p.section.isEmpty() && p.section != p.label)
                               ? QStringLiteral("%1 · %2").arg(p.section, p.label)
                               : p.label});
        }
    };

    if (name == Rule::world()) {
        addSettable(engine->worldProperties(), false);
        // The run itself, which is the application's to end or hold.
        result.append({actionKey(Rule::stopRunAction()), tr("Stop the simulation")});
        result.append({actionKey(Rule::holdRunAction()), tr("Hold the simulation")});
        return result;
    }

    for (Joint *joint : m_scene->joints()) {
        if (joint->name() != name)
            continue;
        for (const physics::JointType &type : engine->jointTypes()) {
            if (type.id == joint->typeId())
                addSettable(type.params, true);
        }
        // Not everything settable on a joint is one of its type's creation
        // parameters -- some belong to any joint at all, and arrive with the
        // readables.
        addSettable(engine->jointReadables(joint->typeId()), false);
        for (const physics::ActionType &action : engine->jointActions())
            result.append({actionKey(action.id), action.label});
        return result;
    }
    const auto addActions = [&result, &engine] {
        for (const physics::ActionType &action : engine->bodyActions())
            result.append({actionKey(action.id), action.label});
    };
    // Putting a body back as the run found it is the application's to do: no
    // engine knows where a run started.
    const auto addInitState = [&result] {
        result.append({actionKey(Rule::initStateAction()), tr("Init state")});
        result.append({actionKey(Rule::cloneAction()), tr("Clone")});
    };

    if (m_scene->explosionNamed(name)) {
        addActions();   // its only property is the blast, and that is the action
        return result;
    }

    // Bodies and shapes get the engine's body actions as well as its
    // properties -- the same dropdown offers both, and a rule that names a
    // shape performs the action on the body that owns it. Without this the
    // actions existed and were reachable only by editing a scene file.
    for (PhysicsBody *body : m_scene->bodies()) {
        if (body->name() == name) {
            addSettable(engine->bodyProperties(), false);
            addActions();
            addInitState();
            return result;
        }
    }
    for (ShapeItem *shape : m_scene->shapes()) {
        if (shape->name() == name) {
            addSettable(engine->shapeProperties(), false);
            addActions();
            addInitState();
            return result;
        }
    }

    if (name == Rule::otherObject()) {
        addSettable(engine->shapeProperties(), false);
        addActions();
        addInitState();
    } else if (name == Rule::otherObjectBody()) {
        addSettable(engine->bodyProperties(), false);
        addActions();
        addInitState();
    }
    return result;
}

// The settings of the actions the application performs itself.
static physics::ActionType applicationAction(const QString &id)
{
    physics::ActionType action;
    if (id != Rule::cloneAction())
        return action;
    action.id = id;
    action.description = QObject::tr("Makes a new body just like this one, as the run found it,"
                                     " with its origin at X and Y. It works on a body already"
                                     " removed, and lasts until the run ends.");
    for (const auto &[key, label] : {std::pair { Rule::cloneXParam(), QObject::tr("X") },
                                     std::pair { Rule::cloneYParam(), QObject::tr("Y") }}) {
        physics::JointParam param;
        param.key = key;
        param.label = label;
        param.defaultValue = 0.0;
        param.minValue = -1e7;
        param.maxValue = 1e7;
        param.decimals = 1;
        param.step = 10.0;
        action.params.append(param);
    }
    return action;
}

QVariantMap RulesPanel::defaultActionParams(const QString &id) const
{
    QVariantMap params;
    for (const physics::JointParam &param : applicationAction(id).params)
        params.insert(param.key, param.defaultValue);
    if (!params.isEmpty())
        return params;
    auto engine = physics::EngineRegistry::create(
        m_scene ? m_scene->simulationEngineName() : QString());
    if (!engine)
        return params;
    for (const physics::ActionType &action : engine->bodyActions() + engine->jointActions()) {
        if (action.id != id)
            continue;
        for (const physics::JointParam &param : action.params)
            params.insert(param.key, param.defaultValue);
    }
    return params;
}

QWidget *RulesPanel::buildActionParamEditor(int index, int slot, const RuleAction &ruleAction,
                                            QWidget *parent)
{
    auto engine = physics::EngineRegistry::create(
        m_scene ? m_scene->simulationEngineName() : QString());

    const physics::ActionType ownAction = applicationAction(ruleAction.actionId);
    QVector<physics::JointParam> params = ownAction.params;
    QString description = ownAction.description;
    if (engine && params.isEmpty()) {
        for (const physics::ActionType &action :
             engine->bodyActions() + engine->jointActions()) {
            if (action.id != ruleAction.actionId)
                continue;
            params = action.params;
            description = action.description;
        }
    }

    if (params.isEmpty()) {
        // Breaking a joint or removing a body takes no settings: doing it is
        // the whole of it.
        auto *label = new QLabel(tr("(nothing to set)"), parent);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        label->setStyleSheet(QStringLiteral("color: #8f8f8f;"));
        label->setToolTip(description);
        return label;
    }

    auto *holder = new QWidget(parent);
    auto *form = new QFormLayout(holder);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(2);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    holder->setToolTip(description);

    for (const physics::JointParam &param : params) {
        const QVariant current = ruleAction.actionParams.value(param.key, param.defaultValue);

        if (param.type == physics::ParamType::Bool) {
            auto *check = new QCheckBox(holder);
            check->setChecked(current.toBool());
            check->setToolTip(param.tooltip);
            connect(check, &QCheckBox::toggled, this, [this, index, slot, key = param.key](bool on) {
                if (m_building)
                    return;
                Rule updated = m_scene->rules().at(index);
                updated.actions[slot].actionParams.insert(key, on);
                commit(index, updated);
            });
            form->addRow(param.label, check);
            continue;
        }

        auto *spin = new QDoubleSpinBox(holder);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        spin->setMinimumWidth(70);
        spin->setRange(param.minValue, param.maxValue);
        spin->setDecimals(param.decimals);
        spin->setSingleStep(param.step);
        spin->setToolTip(param.tooltip);
        spin->setValue(current.toDouble());
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, index, slot, key = param.key](double v) {
                    if (m_building)
                        return;
                    Rule updated = m_scene->rules().at(index);
                    updated.actions[slot].actionParams.insert(key, v);
                    commit(index, updated);
                });
        form->addRow(param.label, spin);
    }
    return holder;
}

QString RulesPanel::actionKey(const QString &id)
{
    return QStringLiteral("@action:") + id;
}

QString RulesPanel::actionIdOf(const QString &key)
{
    const QString prefix = QStringLiteral("@action:");
    return key.startsWith(prefix) ? key.mid(prefix.size()) : QString();
}

void RulesPanel::refreshProperties(int index, int slot)
{
    if (index < 0 || index >= m_rows.size() || slot < 0
        || slot >= m_rows[index].actions.size()
        || slot >= m_scene->rules().at(index).actions.size()) {
        return;
    }

    const RuleAction action = m_scene->rules().at(index).actions.at(slot);
    ActionRow &row = m_rows[index].actions[slot];
    const QString stored = action.isAction() ? actionKey(action.actionId) : action.propertyKey;
    row.property->selectData(stored);

    // Same fallback problem as the watch list: what is shown has to be what
    // is stored, or the first click on the entry already displayed does
    // nothing at all.
    //
    // Only ever to fill a blank, though. A rule that already names a property
    // keeps it even when the box cannot show it -- the list is built from the
    // target's joint type, and a moment where that is not resolvable yet used
    // to rewrite the rule to whatever happened to be on top and save it that
    // way. Silently, and permanently: "stop the motor" became "set the solver
    // damping" and nothing ever stopped.
    const QString shown = row.property->currentData().toString();
    QString performed = action.actionId;
    if (shown != stored && !shown.isEmpty() && stored.isEmpty()) {
        Rule updated = m_scene->rules().at(index);
        RuleAction &a = updated.actions[slot];
        performed = actionIdOf(shown);
        a.actionId = performed;
        a.actionParams = performed.isEmpty() ? QVariantMap()
                                             : defaultActionParams(performed);
        a.propertyKey = performed.isEmpty() ? shown : QString();
        commit(index, updated);
    }

    // An action has no Set/Toggle/Negate/Add -- performing it is the whole
    // thing. Decided after the fallback above, or an action picked for the
    // rule on its behalf would leave the operation box behind.
    row.op->setVisible(performed.isEmpty());
}

void RulesPanel::applyWatchChoice(int index, int slot, const QString &chosen)
{
    if (index < 0 || index >= m_scene->rules().size())
        return;

    Rule updated = m_scene->rules().at(index);
    if (slot < 0 || slot >= updated.conditions.size())
        return;

    RuleCondition &condition = updated.conditions[slot];
    if (chosen.startsWith(kEventPrefix)) {
        condition.eventId = chosen.mid(kEventPrefix.size());
        condition.conditionKey.clear();
        if (condition.conditionValue.userType() != QMetaType::QString
            || !eventNamesOther(condition.subjectName, condition.eventId))
            condition.conditionValue = QString();
    } else {
        condition.conditionKey = chosen;
        if (condition.isEvent()) {
            condition.eventId.clear();
            condition.compare = Rule::Compare::Greater;
            condition.conditionValue = 0.0;
        }
    }
    commit(index, updated);
}

QString RulesPanel::captionFor(int index) const
{
    if (m_scene && index >= 0 && index < m_scene->rules().size()) {
        const QString given = m_scene->rules().at(index).name;
        if (!given.isEmpty())
            return given;
    }
    return tr("Rule %1").arg(index + 1);
}

void RulesPanel::beginRename(int index)
{
    if (index < 0 || index >= m_rows.size())
        return;
    Row &row = m_rows[index];
    if (!row.heading || !row.headingEdit)
        return;

    // The number is a placeholder, not a name: renaming starts from empty so
    // "Rule 3" is not what gets saved by pressing Enter.
    row.headingEdit->setText(m_scene ? m_scene->rules().at(index).name : QString());
    row.headingEdit->setPlaceholderText(captionFor(index));
    row.heading->setVisible(false);
    row.headingEdit->setVisible(true);
    row.headingEdit->setFocus(Qt::MouseFocusReason);
    row.headingEdit->selectAll();
}

void RulesPanel::finishRename(int index, bool keep)
{
    if (index < 0 || index >= m_rows.size())
        return;
    Row &row = m_rows[index];
    if (!row.heading || !row.headingEdit || !row.headingEdit->isVisible())
        return;

    if (keep && m_scene && index < m_scene->rules().size()) {
        Rule updated = m_scene->rules().at(index);
        const QString typed = row.headingEdit->text().trimmed();
        if (typed != updated.name) {
            updated.name = typed;   // empty puts the numbering back
            commit(index, updated);
        }
    }

    row.headingEdit->setVisible(false);
    setHeadingText(row.heading, captionFor(index));
    row.heading->setVisible(true);
}

bool RulesPanel::eventFilter(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    const QVariant which = widget ? widget->property("ruleIndex") : QVariant();
    if (which.isValid()) {
        const int index = which.toInt();
        if (event->type() == QEvent::MouseButtonDblClick
            && qobject_cast<QLabel *>(watched)) {
            beginRename(index);
            return true;
        }
        if (event->type() == QEvent::KeyPress && qobject_cast<QLineEdit *>(watched)) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Escape) {
                finishRename(index, false);
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut && qobject_cast<QLineEdit *>(watched)) {
            finishRename(index, true);
            return false;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RulesPanel::moveRule(int from, int to)
{
    if (!m_scene)
        return;
    QVector<Rule> rules = m_scene->rules();
    if (from < 0 || from >= rules.size() || to < 0 || to >= rules.size() || from == to)
        return;

    const QString name = captionFor(from);

    // Folded state belongs to the card, not to the slot it happens to sit in.
    QVector<bool> folded(rules.size());
    for (int i = 0; i < rules.size(); ++i)
        folded[i] = m_collapsed.contains(i);
    folded.move(from, to);
    m_collapsed.clear();
    for (int i = 0; i < folded.size(); ++i) {
        if (folded[i])
            m_collapsed.insert(i);
    }

    rules.move(from, to);
    m_scene->setRules(rules);        // rebuilds the panel through rulesChanged
    // The order is part of what the scene means -- rules are applied in it --
    // so it is an edit, and undoable like any other.
    m_scene->notifyEdit(tr("Move %1").arg(name));
}

void RulesPanel::scheduleValueEditorRefresh(int index, int slot)
{
    // Rebuilding from inside a combo box's own currentIndexChanged deletes that
    // combo box while Qt is still delivering the signal, which took the whole
    // application down. Queued, the handler returns first.
    QMetaObject::invokeMethod(
        this, [this, index, slot] { refreshValueEditor(index, slot); }, Qt::QueuedConnection);
}

void RulesPanel::refreshValueEditor(int index, int slot)
{
    if (index < 0 || index >= m_rows.size() || slot < 0
        || slot >= m_rows[index].actions.size()
        || slot >= m_scene->rules().at(index).actions.size()) {
        return;
    }
    ActionRow &row = m_rows[index].actions[slot];
    const RuleAction action = m_scene->rules().at(index).actions.at(slot);

    delete row.value;
    row.value = nullptr;

    // The source controls are rebuilt from scratch too, or picking an object
    // would stack a second set on top of the first.
    if (row.sourceHolder) {
        qDeleteAll(row.sourceHolder->findChildren<QWidget *>(
            QString(), Qt::FindDirectChildrenOnly));
        row.source2 = nullptr;
        row.sourceProperty = nullptr;
        row.sourceOffset = nullptr;
    }

    const bool wasBuilding = m_building;
    m_building = true;

    // A control showing a number nobody typed is a lie: the rule carries no
    // value at all. One without a value is dropped as half-filled -- never
    // written to the file, never run -- so "set the motor to 0" could not be
    // written by leaving the box on the 0 it already showed. Whatever the
    // editor ends up displaying is what the rule holds from here on. Written
    // straight into the scene rather than through commit(): making a shown
    // default real is not an edit the user made, and it has no business in
    // the undo history as one.
    const auto seedShownValue = [this, index, slot](const QVariant &shown) {
        if (index >= m_scene->rules().size()
            || slot >= m_scene->rules().at(index).actions.size()
            || m_scene->rules().at(index).actions.at(slot).value.isValid()) {
            return;
        }
        m_scene->rules()[index].actions[slot].value = shown;
    };

    if (action.isAction() && m_scene->explosionNamed(action.targetName)) {
        // Aimed at an explosion, the settings belong to that object -- it is
        // placed, sized and tuned on the canvas, and the rule only says when.
        auto *label = new QLabel(tr("(set on the object)"), row.valueHolder);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        label->setStyleSheet(QStringLiteral("color: #8f8f8f;"));
        row.value = label;
    } else if (action.isAction()) {
        // Aimed at anything else there is no object to carry them, so the
        // action's own parameters are edited here. Without this they stayed at
        // whatever they were seeded with and the rule could not be tuned.
        row.value = buildActionParamEditor(index, slot, action, row.valueHolder);
    } else if (!Rule::usesValue(action.op)) {
        auto *label = new QLabel(tr("(current value)"), row.valueHolder);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        label->setStyleSheet(QStringLiteral("color: #8f8f8f;"));
        row.value = label;
    } else if (propertyIsFlag(action.targetName, action.propertyKey)) {
        auto *check = new QCheckBox(row.valueHolder);
        check->setChecked(action.value.toBool());
        seedShownValue(check->isChecked());
        connect(check, &QCheckBox::toggled, this, [this, index, slot](bool on) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.actions[slot].value = on;
            commit(index, updated);
        });
        row.value = check;
    } else if (propertyIsChoice(action.targetName, action.propertyKey)) {
        auto *combo = new QComboBox(row.valueHolder);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        const physics::JointParam *param = describe(action.targetName, action.propertyKey);
        combo->addItems(param->choices);
        combo->setToolTip(param->tooltip);
        combo->setCurrentIndex(qBound(0, action.value.toInt(), param->choices.size() - 1));
        seedShownValue(combo->currentIndex());
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index, slot](int at) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.actions[slot].value = at;
            commit(index, updated);
        });
        row.value = combo;
    } else {
        auto *spin = new QDoubleSpinBox(row.valueHolder);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        spin->setMinimumWidth(48);
        // Range, precision and step as the engine declared them.
        if (const physics::JointParam *p = describe(action.targetName, action.propertyKey)) {
            spin->setRange(p->minValue, p->maxValue);
            spin->setDecimals(p->decimals);
            spin->setSingleStep(p->step);
            spin->setToolTip(p->tooltip);
        } else {
            spin->setRange(-1e6, 1e6);
            spin->setDecimals(1);
            spin->setSingleStep(10.0);
        }
        auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName());
        if (engine) {
            for (Joint *joint : m_scene->joints()) {
                if (joint->name() != action.targetName)
                    continue;
                for (const physics::JointType &type : engine->jointTypes()) {
                    if (type.id != joint->typeId())
                        continue;
                    for (const physics::JointParam &param : type.params) {
                        if (param.key != action.propertyKey)
                            continue;
                        spin->setRange(param.minValue, param.maxValue);
                        spin->setDecimals(param.decimals);
                        spin->setSingleStep(param.step);
                        spin->setToolTip(param.tooltip);
                    }
                }
            }
        }
        spin->setValue(action.value.toDouble());
        // Read back rather than assumed: the range the engine declared may not
        // reach zero, and the box has already clamped into it.
        seedShownValue(spin->value());
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, index, slot](double v) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.actions[slot].value = v;
            commit(index, updated);
        });
        row.value = spin;
    }

    row.valueHolder->layout()->addWidget(row.value);

    // Only a number-valued rule can take its value from elsewhere, so the mode
    // picker is offered there and nowhere else.
    const bool canSource = !action.isAction() && Rule::usesValue(action.op)
                           && !propertyIsFlag(action.targetName, action.propertyKey);
    const bool sourced = canSource && action.usesSource();
    if (row.valueMode) {
        row.valueMode->setVisible(canSource);
        row.valueMode->setCurrentIndex(row.valueMode->findData(sourced));
    }
    setFormRowVisible(row.form, row.sourceRow, sourced);
    // The two are alternatives, so the typed editor goes away entirely rather
    // than sitting there greyed out next to the object that replaced it.
    row.valueHolder->setVisible(!sourced);

    if (sourced) {
        auto *layout = qobject_cast<QFormLayout *>(row.sourceHolder->layout());

        auto *from = new ObjectComboBox([this] { return sourceObjectChoices(); },
                                        row.sourceHolder);
        from->setMinimumContentsLength(10);
        from->selectData(action.sourceObject);
        from->setToolTip(tr("Which object the value is read from."));
        connect(from, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index, slot](int) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.actions[slot].sourceObject = m_rows[index].actions[slot].source2->currentData().toString();
            // Another object has other properties, so the old key cannot just
            // be carried across.
            const QVector<RuleChoice> properties =
                readablesOf(updated.actions[slot].sourceObject);
            updated.actions[slot].sourceProperty = properties.isEmpty() ? QString()
                                                          : properties.first().data;
            commit(index, updated);
            scheduleValueEditorRefresh(index, slot);
        });
        row.source2 = from;
        layout->addRow(tr("object"), from);

        auto *what = new ObjectComboBox(
            [this, index, slot] {
                const QVector<Rule> &rules = m_scene->rules();
                if (index >= rules.size() || slot >= rules.at(index).actions.size())
                    return readablesOf(QString());
                return readablesOf(rules.at(index).actions.at(slot).sourceObject);
            },
            row.sourceHolder);
        what->setMinimumContentsLength(10);
        what->selectData(action.sourceProperty);
        what->setToolTip(tr("Which of its properties to read."));
        connect(what, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, index, slot](int) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.actions[slot].sourceProperty = m_rows[index].actions[slot].sourceProperty->currentData().toString();
            commit(index, updated);
        });
        row.sourceProperty = what;
        layout->addRow(tr("property"), what);

        auto *offset = new QDoubleSpinBox(row.sourceHolder);
        offset->setRange(-1e6, 1e6);
        offset->setDecimals(1);
        offset->setSingleStep(10.0);
        offset->setMinimumWidth(70);
        offset->setValue(action.sourceOffset);
        offset->setToolTip(tr("Added to whatever that property reads. Zero to "
                              "take it as it comes."));
        connect(offset, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, index, slot](double v) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.actions[slot].sourceOffset = v;
            commit(index, updated);
        });
        row.sourceOffset = offset;
        layout->addRow(tr("offset"), offset);
    }

    m_building = wasBuilding;
}

const physics::JointParam *RulesPanel::describe(const QString &objectName,
                                                const QString &key) const
{
    // Cached in a static so the pointer stays valid for the caller; the
    // lists are small and rebuilt per call.
    static physics::JointParam found;
    if (!m_scene)
        return nullptr;

    auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName());
    if (!engine)
        return nullptr;

    physics::PropertyList candidates;
    bool matched = false;
    if (objectName == Rule::world()) {
        candidates = engine->worldProperties();
        matched = true;
    }
    for (Joint *joint : m_scene->joints()) {
        if (joint->name() != objectName)
            continue;
        for (const physics::JointType &type : engine->jointTypes()) {
            if (type.id != joint->typeId())
                continue;
            candidates = type.params;
            for (const physics::JointParam &p : engine->jointReadables(type.id))
                candidates.append(p);
        }
        matched = true;
    }
    if (!matched) {
        for (PhysicsBody *body : m_scene->bodies()) {
            if (body->name() == objectName) {
                candidates = engine->bodyProperties();
                matched = true;
            }
        }
    }
    if (!matched) {
        for (ShapeItem *shape : m_scene->shapes()) {
            if (shape->name() == objectName) {
                candidates = engine->shapeProperties();
                matched = true;
            }
        }
    }
    if (!matched) {
        if (objectName == Rule::otherObject())
            candidates = engine->shapeProperties();
        else if (objectName == Rule::otherObjectBody())
            candidates = engine->bodyProperties();
    }

    for (const physics::JointParam &p : candidates) {
        if (p.key == key) {
            found = p;
            return &found;
        }
    }
    return nullptr;
}

bool RulesPanel::propertyIsFlag(const QString &objectName, const QString &key) const
{
    const physics::JointParam *param = describe(objectName, key);
    return param && param->type == physics::ParamType::Bool;
}

bool RulesPanel::propertyIsChoice(const QString &objectName, const QString &key) const
{
    const physics::JointParam *param = describe(objectName, key);
    return param && param->type == physics::ParamType::Choice && !param->choices.isEmpty();
}

void RulesPanel::commit(int index, const Rule &rule)
{
    if (!m_scene || index < 0 || index >= m_scene->rules().size())
        return;

    m_scene->rules()[index] = rule;
    // One undo step per rule rather than per keystroke: the merge key names
    // the rule being edited, so a run of changes to it collapses into one.
    m_scene->notifyEdit(tr("Edit rule"), QStringLiteral("rule:%1").arg(index));
    // No rebuild follows an edit, so the card has to be repainted here or a
    // rule finished by the last field left would keep its warning.
    refreshCardLook(index);
    emit incompleteCountChanged(incompleteCount());
}

void RulesPanel::addRule()
{
    if (!m_scene)
        return;

    Rule rule;
    if (!m_scene->bodies().isEmpty()) {
        rule.conditions[0].subjectName = m_scene->bodies().first()->name();
        rule.actions[0].targetName = rule.conditions[0].subjectName;
    }
    if (!m_scene->joints().isEmpty()) {
        rule.conditions[0].subjectName = m_scene->joints().first()->name();
        rule.actions[0].targetName = rule.conditions[0].subjectName;
    }

    QVector<Rule> rules = m_scene->rules();
    rules.append(rule);
    m_scene->setRules(rules);
    m_scene->notifyEdit(tr("Add rule"));
}

// A second When, or a second Do. The new one is seeded from the one above it,
// since a rule with two conditions is usually watching the same object twice
// -- and an empty row would only mark the card unfinished the moment it
// appeared.
void RulesPanel::addCondition(int index)
{
    if (!m_scene || index < 0 || index >= m_scene->rules().size())
        return;

    Rule updated = m_scene->rules().at(index);
    RuleCondition seed;
    if (!updated.conditions.isEmpty()) {
        seed.subjectName = updated.conditions.last().subjectName;
        seed.conditionKey = updated.conditions.last().conditionKey;
        seed.compare = updated.conditions.last().compare;
    }
    updated.conditions.append(seed);
    commitAndRebuild(index, updated, tr("Add condition"));
}

void RulesPanel::removeCondition(int index, int slot)
{
    if (!m_scene || index < 0 || index >= m_scene->rules().size())
        return;

    Rule updated = m_scene->rules().at(index);
    // Never down to nothing: a rule with no condition could not say when, and
    // the card would have no When at all to fill back in.
    if (slot < 0 || slot >= updated.conditions.size() || updated.conditions.size() <= 1)
        return;

    updated.conditions.remove(slot);
    commitAndRebuild(index, updated, tr("Remove condition"));
}

void RulesPanel::addAction(int index)
{
    if (!m_scene || index < 0 || index >= m_scene->rules().size())
        return;

    Rule updated = m_scene->rules().at(index);
    RuleAction seed;
    if (!updated.actions.isEmpty())
        seed.targetName = updated.actions.last().targetName;
    else if (!updated.conditions.isEmpty())
        seed.targetName = updated.conditions.first().subjectName;
    updated.actions.append(seed);
    commitAndRebuild(index, updated, tr("Add action"));
}

void RulesPanel::removeAction(int index, int slot)
{
    if (!m_scene || index < 0 || index >= m_scene->rules().size())
        return;

    Rule updated = m_scene->rules().at(index);
    if (slot < 0 || slot >= updated.actions.size() || updated.actions.size() <= 1)
        return;

    updated.actions.remove(slot);
    commitAndRebuild(index, updated, tr("Remove action"));
}

// Adding or taking away a row changes how many blocks the card has, so unlike
// an ordinary edit this one has to rebuild the panel. Its own undo step, too:
// merging it with the edits either side would make one step undo both the row
// and whatever was typed into it.
void RulesPanel::commitAndRebuild(int index, const Rule &rule, const QString &label)
{
    m_scene->rules()[index] = rule;
    m_scene->notifyEdit(label);
    m_scene->notifyRulesChanged();
}

QString RulesPanel::problemText(Rule::Problem problem)
{
    switch (problem) {
    case Rule::Problem::None:
        return QString();
    case Rule::Problem::NoConditions:
        return tr("This rule does not run: it has no condition left to fire on.");
    case Rule::Problem::NoActions:
        return tr("This rule does not run: it has nothing left to do.");
    case Rule::Problem::NoSubject:
        return tr("This rule does not run: nothing is being watched.");
    case Rule::Problem::NoCondition:
        return tr("This rule does not run: pick an event to watch for, or a"
                  " property to compare.");
    case Rule::Problem::NoTarget:
        return tr("This rule does not run: nothing is being acted on.");
    case Rule::Problem::NoProperty:
        return tr("This rule does not run: pick what to change on the target.");
    case Rule::Problem::NoValue:
        return tr("This rule does not run: it has nothing to set. Type a value,"
                  " or read one from another object.");
    }
    return QString();
}

void RulesPanel::setCardLook(QWidget *card, QLabel *heading, bool enabled, bool complete)
{
    // Four looks over two questions. Complete or not decides the colour, since
    // an unfinished rule is the one that wants attention; switched on or off
    // decides whether the border is solid, which is the weaker signal of the
    // two and so the quieter mark.
    QString border;
    QString background;
    QString band;
    QString bandBorder;
    QString captionColor;
    if (complete) {
        border = enabled ? QStringLiteral("1px solid #c9c9c9")
                         : QStringLiteral("1px dashed #cfcfcf");
        background = enabled ? QStringLiteral("#f4f4f4") : QStringLiteral("#fafafa");
        // A band across the top, darker than the card, so where one rule ends
        // and the next begins is obvious down a column.
        band = enabled ? QStringLiteral("#dcdcdc") : QStringLiteral("#eeeeee");
        bandBorder = enabled ? QStringLiteral("#c9c9c9") : QStringLiteral("#dcdcdc");
        captionColor = enabled ? QStringLiteral("#6f6f6f") : QStringLiteral("#b0b0b0");
    } else {
        border = enabled ? QStringLiteral("1px solid #d9534f")
                         : QStringLiteral("1px dashed #e0a3a0");
        background = enabled ? QStringLiteral("#fdf5f5") : QStringLiteral("#fdf9f9");
        band = enabled ? QStringLiteral("#f3c9c6") : QStringLiteral("#f6e2e0");
        bandBorder = enabled ? QStringLiteral("#d9534f") : QStringLiteral("#e0a3a0");
        captionColor = enabled ? QStringLiteral("#a33a35") : QStringLiteral("#c48f8c");
    }

    card->setStyleSheet(
        QStringLiteral("QFrame#ruleCard { border: %1; border-radius: 4px;"
                       " background: %2; }"
                       "QWidget#ruleCardHeader { background: %3;"
                       " border-bottom: 1px solid %4;"
                       " border-top-left-radius: 3px; border-top-right-radius: 3px; }")
            .arg(border, background, band, bandBorder));
    if (heading) {
        heading->setStyleSheet(
            QStringLiteral("font-weight: bold; color: %1;").arg(captionColor));
    }
}

void RulesPanel::refreshCardLook(int index)
{
    if (!m_scene || index < 0 || index >= m_rows.size()
        || index >= m_scene->rules().size())
        return;

    const Row &row = m_rows.at(index);
    if (!row.card)
        return;

    const Rule &rule = m_scene->rules().at(index);
    const Rule::Problem problem = rule.problem();
    setCardLook(row.card, row.heading, rule.enabled, problem == Rule::Problem::None);
    if (row.warning) {
        row.warning->setVisible(problem != Rule::Problem::None);
        row.warning->setToolTip(problemText(problem));
    }

    // And the row it came from, so a card with several Whens says which of
    // them is the one still to be filled in.
    for (int slot = 0; slot < row.conditions.size() && slot < rule.conditions.size(); ++slot)
        setRowWarning(row.conditions.at(slot).warning,
                      Rule::conditionProblem(rule.conditions.at(slot)));
    for (int slot = 0; slot < row.actions.size() && slot < rule.actions.size(); ++slot)
        setRowWarning(row.actions.at(slot).warning,
                      Rule::actionProblem(rule.actions.at(slot)));
}

void RulesPanel::setRowWarning(QLabel *warning, Rule::Problem problem)
{
    if (!warning)
        return; // a card with one of each has no per-row heading to mark
    warning->setVisible(problem != Rule::Problem::None);
    warning->setToolTip(problemText(problem));
}

int RulesPanel::incompleteCount() const
{
    if (!m_scene)
        return 0;
    int count = 0;
    for (const Rule &rule : m_scene->rules()) {
        if (!rule.isValid())
            ++count;
    }
    return count;
}

void RulesPanel::setHeadingText(QLabel *label, const QString &caption)
{
    // Cut to a width a docked panel can hold. Left to itself the label reports
    // the whole name as its minimum, and that becomes a width the card can
    // never go below: one long name would push the fields off the right-hand
    // edge with nothing to scroll them back. A fixed bound rather than the
    // width it happens to have -- re-eliding as it is resized changes what it
    // asks for, which resizes it again, and it never settles.
    constexpr int kCaptionWidth = 100;
    label->setProperty("fullCaption", caption);
    const QFontMetrics metrics(label->font());
    const QString shown = metrics.elidedText(caption, Qt::ElideRight, kCaptionWidth);
    label->setText(shown);
    label->setToolTip(shown == caption
                          ? tr("Double-click to rename this rule.")
                          : tr("%1\n\nDouble-click to rename this rule.").arg(caption));
}

void RulesPanel::setCollapseLook(QToolButton *button, bool collapsed)
{
    // Both directions are the same triangle, so open and closed match. Typed
    // glyphs cannot manage that: U+25BC is far larger than U+25B6.
    const int side = 14;
    const qreal dpr = button->devicePixelRatioF();
    QPixmap pm(QSize(side, side) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x90, 0x90, 0x90));

    QPolygonF triangle;
    if (collapsed)
        triangle << QPointF(5, 3) << QPointF(10, 7) << QPointF(5, 11);
    else
        triangle << QPointF(3, 5) << QPointF(11, 5) << QPointF(7, 10);
    painter.drawPolygon(triangle);
    painter.end();

    button->setIcon(QIcon(pm));
    button->setIconSize(QSize(side, side));
    button->setProperty("collapsed", collapsed);
    button->setToolTip(collapsed ? tr("Expand this rule") : tr("Collapse this rule"));
}

void RulesPanel::setCollapsed(int index, bool collapsed)
{
    if (collapsed)
        m_collapsed.insert(index);
    else
        m_collapsed.remove(index);

    if (index < 0 || index >= m_rows.size())
        return;

    // Toggled in place rather than through rebuild(), which would recreate
    // every card and lose whatever the user was part-way through editing.
    const Row &row = m_rows.at(index);
    if (row.body)
        row.body->setVisible(!collapsed);
    if (row.collapse)
        setCollapseLook(row.collapse, collapsed);
}

void RulesPanel::removeRule(int index)
{
    if (!m_scene || index < 0 || index >= m_scene->rules().size())
        return;

    QSet<int> shifted;
    for (int collapsed : std::as_const(m_collapsed)) {
        if (collapsed < index)
            shifted.insert(collapsed);
        else if (collapsed > index)
            shifted.insert(collapsed - 1);
    }
    m_collapsed = shifted;

    QVector<Rule> rules = m_scene->rules();
    rules.remove(index);
    m_scene->setRules(rules);
    m_scene->notifyEdit(tr("Remove rule"));
}

void RulesPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Every block of every card: the lists are built from what the scene holds
    // now, and it may have gained or lost objects while the tab was hidden.
    for (Row &row : m_rows) {
        for (ConditionRow &condition : row.conditions) {
            condition.source->refill();
            condition.event->refill();
        }
        for (ActionRow &action : row.actions) {
            action.target->refill();
            action.property->refill();
            if (action.source2)
                action.source2->refill();
            if (action.sourceProperty)
                action.sourceProperty->refill();
        }
    }
}
