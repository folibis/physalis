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

#include <QCheckBox>
#include <algorithm>

#include <QComboBox>
#include <QDoubleSpinBox>
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

} // namespace

void RulesPanel::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // The whole strip is green, not just a badge behind the words.
    auto *headerBar = new QWidget(this);
    headerBar->setObjectName(QStringLiteral("rulesHeader"));
    headerBar->setStyleSheet(QStringLiteral(
        "QWidget#rulesHeader { background: #05C936; }"   // the icon's leaf green
        "QWidget#rulesHeader QLabel { color: #000000; font-weight: bold; background: transparent; }"
        "QWidget#rulesHeader QToolButton { border: none; background: transparent; }"));

    auto *header = new QHBoxLayout(headerBar);
    header->setContentsMargins(6, 4, 6, 4);

    auto *add = new QToolButton(headerBar);
    // The plus is drawn in the same green as the bar, so it has to be
    // recoloured or it disappears into it.
    add->setIcon(tintedIcon(Icons::add(), QColor(0, 0, 0), 16));
    add->setToolTip(tr("Add a rule"));
    add->setAutoRaise(true);
    connect(add, &QToolButton::clicked, this, &RulesPanel::addRule);

    auto *title = new QLabel(tr("Add rule"), headerBar);

    // A spacer opposite the button, so the caption is centred on the bar
    // rather than on what is left beside it.
    auto *balance = new QWidget(headerBar);
    balance->setFixedWidth(add->sizeHint().width());
    header->addWidget(balance);
    header->addStretch();
    header->addWidget(title);
    header->addStretch();
    header->addWidget(add);
    layout->addWidget(headerBar);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
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
        refreshEvents(i);
        refreshConditionEditor(i);
        refreshProperties(i);
        refreshValueEditor(i);
    }
}

QWidget *RulesPanel::buildCard(int index)
{
    const Rule rule = m_scene->rules().at(index);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("ruleCard"));
    card->setFrameShape(QFrame::NoFrame);
    // Drawn here rather than left to the style: QFrame::StyledPanel gave some
    // cards no top border at all, and the panel behind is the same grey.
    card->setStyleSheet(QStringLiteral(
        "QFrame#ruleCard { border: 1px solid #c9c9c9; border-radius: 4px;"
        " background: #fcfcfc; }"));
    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(8, 6, 8, 8);
    outer->setSpacing(4);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(2);

    auto *collapse = new QToolButton(card);
    collapse->setObjectName(QStringLiteral("collapseButton"));
    collapse->setAutoRaise(true);
    collapse->setStyleSheet(QStringLiteral(
        "QToolButton#collapseButton { border: none; background: transparent; }"));
    setCollapseLook(collapse, m_collapsed.contains(index));
    titleRow->addWidget(collapse);

    auto *remove = new QToolButton(card);
    remove->setIcon(Icons::deleteShape());
    remove->setToolTip(tr("Remove this rule"));
    remove->setAutoRaise(true);
    connect(remove, &QToolButton::clicked, this, [this, index] { removeRule(index); });
    titleRow->addWidget(remove);

    titleRow->addStretch();
    auto *heading = new QLabel(captionFor(index), card);
    heading->setStyleSheet(QStringLiteral("font-weight: bold; color: #6f6f6f;"));
    heading->setToolTip(tr("Double-click to rename this rule."));
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

    // Balances the two buttons, so the title sits centred on the card rather
    // than centred on what is left over beside them.
    auto *balance = new QWidget(card);
    balance->setFixedWidth(collapse->sizeHint().width() + remove->sizeHint().width() + 2);
    titleRow->addWidget(balance);

    outer->addLayout(titleRow);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    Row row;
    row.card = card;

    // --- when: a condition over one object's properties -------------------
    row.source = new ObjectComboBox([this] { return sourceChoices(); }, card);
    row.source->setEmptyText(tr("(no objects yet)"));
    row.source->selectData(rule.subjectName);
    connect(row.source, &QComboBox::currentIndexChanged, this, [this, index](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        updated.subjectName = m_rows[index].source->currentData().toString();
        updated.conditionKey.clear();
        updated.eventId.clear();
        commit(index, updated);
        refreshEvents(index);
        refreshConditionEditor(index);
        refreshProperties(index);
        scheduleValueEditorRefresh(index);
    });
    form->addRow(tr("When:"), row.source);

    // What to watch on it -- hidden for a touch, which is not a property.
    row.event = new ObjectComboBox(
        [this, index] {
            return watchChoices(index < m_scene->rules().size()
                                    ? m_scene->rules().at(index).subjectName
                                    : QString());
        },
        card);
    row.event->setEmptyText(tr("(nothing readable)"));
    connect(row.event, &QComboBox::currentIndexChanged, this, [this, index](int) {
        if (m_building)
            return;
        applyWatchChoice(index, m_rows[index].event->currentData().toString());
        refreshEvents(index);
        refreshConditionEditor(index);
    });
    form->addRow(QString(), row.event);

    auto *testRow = new QHBoxLayout;
    testRow->setSpacing(4);

    row.compare = new QComboBox(card);
    row.compare->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row.compare->setMinimumContentsLength(8);
    row.compare->addItem(tr("is greater than"), Rule::compareName(Rule::Compare::Greater));
    row.compare->addItem(tr("is less than"), Rule::compareName(Rule::Compare::Less));
    row.compare->addItem(tr("is at least"), Rule::compareName(Rule::Compare::GreaterEqual));
    row.compare->addItem(tr("is at most"), Rule::compareName(Rule::Compare::LessEqual));
    row.compare->addItem(tr("equals"), Rule::compareName(Rule::Compare::Equal));
    row.compare->addItem(tr("differs from"), Rule::compareName(Rule::Compare::NotEqual));
    row.compare->setCurrentIndex(row.compare->findData(Rule::compareName(rule.compare)));
    row.compare->setToolTip(tr("The action runs when this becomes true, not for as long"
                                 " as it stays true."));
    connect(row.compare, &QComboBox::currentIndexChanged, this, [this, index](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        updated.compare =
            Rule::compareFromName(m_rows[index].compare->currentData().toString());
        updated.conditionValue = QVariant();
        commit(index, updated);
        refreshEvents(index);
        refreshConditionEditor(index);
    });
    testRow->addWidget(row.compare, 0);

    row.conditionHolder = new QWidget(card);
    auto *condLayout = new QHBoxLayout(row.conditionHolder);
    condLayout->setContentsMargins(0, 0, 0, 0);
    testRow->addWidget(row.conditionHolder, 1);
    row.conditionHolder->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    form->addRow(QString(), testRow);

    // --- then ------------------------------------------------------------
    row.target = new ObjectComboBox([this] { return targetChoices(); }, card);
    row.target->selectData(rule.targetName);
    connect(row.target, &QComboBox::currentIndexChanged, this, [this, index](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        updated.targetName = m_rows[index].target->currentData().toString();
        updated.propertyKey.clear(); // a different object has different properties
        commit(index, updated);
        refreshProperties(index);
        scheduleValueEditorRefresh(index);
    });
    form->addRow(tr("Then change:"), row.target);

    row.property = new ObjectComboBox(
        [this, index] {
            return propertiesOf(index < m_scene->rules().size()
                                    ? m_scene->rules().at(index).targetName
                                    : QString());
        },
        card);
    row.property->setEmptyText(tr("(nothing changeable)"));
    connect(row.property, &QComboBox::currentIndexChanged, this, [this, index](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        const QString chosen = m_rows[index].property->currentData().toString();

        // The same dropdown offers properties and actions; which one it is
        // decides whether the rule writes a value or performs something.
        const QString action = actionIdOf(chosen);
        updated.actionId = action;
        if (action.isEmpty()) {
            updated.propertyKey = chosen;
            updated.actionParams.clear();
        } else {
            updated.propertyKey.clear();
            // An action's settings are its own, and a rule that carried none
            // performed it with every number at zero -- a blast of radius
            // nothing. Start them where the engine says they should start.
            updated.actionParams = defaultActionParams(action);
        }
        commit(index, updated);
        m_rows[index].op->setVisible(action.isEmpty());
        scheduleValueEditorRefresh(index);
    });
    form->addRow(QString(), row.property);

    // --- what to do -------------------------------------------------------
    auto *doRow = new QHBoxLayout;
    doRow->setSpacing(4);

    row.op = new QComboBox(card);
    row.op->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row.op->setMinimumContentsLength(6);
    row.op->addItem(tr("Set to"), static_cast<int>(Rule::Op::Set));
    row.op->addItem(tr("Toggle"), static_cast<int>(Rule::Op::Toggle));
    row.op->addItem(tr("Negate"), static_cast<int>(Rule::Op::Negate));
    row.op->addItem(tr("Add"), static_cast<int>(Rule::Op::Add));
    row.op->setCurrentIndex(row.op->findData(static_cast<int>(rule.op)));
    row.op->setToolTip(tr("Negate flips the sign, which is how a motor reverses at a limit."));
    connect(row.op, &QComboBox::currentIndexChanged, this, [this, index](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        updated.op = static_cast<Rule::Op>(m_rows[index].op->currentData().toInt());
        commit(index, updated);
        scheduleValueEditorRefresh(index);
    });
    doRow->addWidget(row.op);

    // What the number is: typed here, or read off another object each time the
    // rule fires. Which of the two editors is shown follows from this.
    row.valueMode = new QComboBox(card);
    row.valueMode->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row.valueMode->setMinimumContentsLength(6);
    row.valueMode->addItem(tr("Value"), false);
    row.valueMode->addItem(tr("Property"), true);
    row.valueMode->setToolTip(tr("A number you type, or one taken from another "
                                 "object while the rule runs."));
    connect(row.valueMode, &QComboBox::currentIndexChanged, this, [this, index](int) {
        if (m_building)
            return;
        Rule updated = m_scene->rules().at(index);
        if (m_rows[index].valueMode->currentData().toBool()) {
            // Property with nothing picked would leave the rule half-set and
            // silently skipped, so the first candidate is filled in at once.
            if (updated.sourceObject.isEmpty()) {
                const QVector<RuleChoice> objects = sourceObjectChoices();
                // The object the rule already watches is the likely one to read
                // from -- "when the ray sees something, go to where it struck".
                const auto watched =
                    std::find_if(objects.begin(), objects.end(),
                                 [&](const RuleChoice &c) {
                                     return c.data == updated.subjectName;
                                 });
                if (watched != objects.end())
                    updated.sourceObject = watched->data;
                else if (!objects.isEmpty())
                    updated.sourceObject = objects.first().data;
            }
            const QVector<RuleChoice> properties = readablesOf(updated.sourceObject);
            if (updated.sourceProperty.isEmpty() && !properties.isEmpty())
                updated.sourceProperty = properties.first().data;
        } else {
            updated.sourceObject.clear();
            updated.sourceProperty.clear();
        }
        commit(index, updated);
        scheduleValueEditorRefresh(index);
    });
    doRow->addWidget(row.valueMode);

    doRow->addStretch(1);
    form->addRow(tr("Do:"), doRow);

    // The editor gets its own line. Sharing the Do row with the operation and
    // the mode picker squeezed it down to nothing on a card this narrow.
    row.valueHolder = new QWidget(card);
    auto *holderLayout = new QHBoxLayout(row.valueHolder);
    holderLayout->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Value:"), row.valueHolder);
    row.valueRow = form->rowCount() - 1;

    // Where the number comes from, on its own line. Four controls crammed into
    // the Do row left the picker 39 px wide and effectively invisible.
    row.sourceHolder = new QWidget(card);
    // A form, not a row: three controls side by side leave each of them about
    // 38 px wide on a card this narrow.
    auto *sourceLayout = new QFormLayout(row.sourceHolder);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(4);
    sourceLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    sourceLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Value from:"), row.sourceHolder);
    row.sourceRow = form->rowCount() - 1;
    row.form = form;

    auto *body = new QWidget(card);
    body->setLayout(form);
    body->setVisible(!m_collapsed.contains(index));
    outer->addWidget(body);

    row.body = body;
    row.collapse = collapse;
    row.heading = heading;
    row.headingEdit = rename;
    connect(collapse, &QToolButton::clicked, this, [this, index] { setCollapsed(index, !m_collapsed.contains(index)); });
    m_rows.append(row);
    return card;
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

void RulesPanel::refreshEvents(int index)
{
    if (index < 0 || index >= m_rows.size())
        return;
    Rule rule = m_scene->rules().at(index);

    const QString wanted = rule.isEvent() ? kEventPrefix + rule.eventId
                                          : rule.conditionKey;
    m_rows[index].event->selectData(wanted);

    const QString shown = m_rows[index].event->currentData().toString();
    if (shown != wanted && !shown.isEmpty()) {
        applyWatchChoice(index, shown);
        rule = m_scene->rules().at(index);
    }
    m_rows[index].compare->setVisible(!rule.isEvent());
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
        choices.append({kEventPrefix + QStringLiteral("rayDetects"), tr("detects")});
        choices.append({QStringLiteral("distance"), tr("Distance")});
        choices.append({QStringLiteral("hit"), tr("Hit")});
        choices.append({QStringLiteral("hitX"), tr("Hit X")});
        choices.append({QStringLiteral("hitY"), tr("Hit Y")});
        return choices;
    }

    // The run has no events: two numbers that climb as it goes, and whatever
    // the engine says its world can still be asked about.
    if (name == Rule::world()) {
        choices.append({QStringLiteral("time"), tr("Elapsed Time (s)")});
        choices.append({QStringLiteral("frame"), tr("Frame")});
        addReadable(engine->worldProperties());
        return choices;
    }

    for (Joint *joint : m_scene->joints()) {
        if (joint->name() != name)
            continue;
        for (const physics::JointType &type : engine->jointTypes()) {
            if (type.id == joint->typeId())
                addEvents(type.events);
        }
        addReadable(engine->jointReadables(joint->typeId()));
        return choices;
    }
    for (PhysicsBody *body : m_scene->bodies()) {
        if (body->name() != name)
            continue;
        addEvents(engine->bodyEvents());
        addReadable(engine->bodyProperties());
        return choices;
    }
    for (ShapeItem *shape : m_scene->shapes()) {
        if (shape->name() != name)
            continue;
        addEvents(engine->shapeEvents());
        addReadable(engine->shapeProperties());
        return choices;
    }
    return choices;
}

void RulesPanel::refreshConditionEditor(int index)
{
    if (index < 0 || index >= m_rows.size())
        return;
    Row &row = m_rows[index];
    const Rule rule = m_scene->rules().at(index);

    delete row.condition;
    row.condition = nullptr;

    const bool wasBuilding = m_building;
    m_building = true;

    if (rule.isEvent()) {
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
        combo->setToolTip(tr("Which object, or anything."));
        combo->selectData(rule.conditionValue.toString());
        connect(combo, &QComboBox::currentIndexChanged, this, [this, index, combo](int) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.conditionValue = combo->currentData().toString();
            commit(index, updated);
        });
        row.condition = combo;
    } else if (propertyIsChoice(rule.subjectName, rule.conditionKey)) {
        // Compared as its index, so "is Dynamic" is a comparison the same way
        // any other is -- but chosen by name.
        const physics::JointParam *param = describe(rule.subjectName, rule.conditionKey);
        auto *combo = new QComboBox(row.conditionHolder);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        combo->addItems(param->choices);
        combo->setToolTip(param->tooltip);
        combo->setCurrentIndex(qBound(0, rule.conditionValue.toInt(),
                                      param->choices.size() - 1));
        connect(combo, &QComboBox::currentIndexChanged, this, [this, index](int at) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.conditionValue = at;
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
        spin->setValue(rule.conditionValue.toDouble());
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.conditionValue = v;
            commit(index, updated);
        });
        row.condition = spin;
    }

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
            return result;
        }
    }
    for (ShapeItem *shape : m_scene->shapes()) {
        if (shape->name() == name) {
            addSettable(engine->shapeProperties(), false);
            addActions();
            return result;
        }
    }

    if (name == Rule::otherObject()) {
        addSettable(engine->shapeProperties(), false);
        addActions();
    } else if (name == Rule::otherObjectBody()) {
        addSettable(engine->bodyProperties(), false);
        addActions();
    }
    return result;
}

QVariantMap RulesPanel::defaultActionParams(const QString &id) const
{
    QVariantMap params;
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

QWidget *RulesPanel::buildActionParamEditor(int index, const Rule &rule,
                                            QWidget *parent)
{
    auto engine = physics::EngineRegistry::create(
        m_scene ? m_scene->simulationEngineName() : QString());

    QVector<physics::JointParam> params;
    QString description;
    if (engine) {
        for (const physics::ActionType &action :
             engine->bodyActions() + engine->jointActions()) {
            if (action.id != rule.actionId)
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
        const QVariant current = rule.actionParams.value(param.key, param.defaultValue);

        if (param.type == physics::ParamType::Bool) {
            auto *check = new QCheckBox(holder);
            check->setChecked(current.toBool());
            check->setToolTip(param.tooltip);
            connect(check, &QCheckBox::toggled, this, [this, index, key = param.key](bool on) {
                if (m_building)
                    return;
                Rule updated = m_scene->rules().at(index);
                updated.actionParams.insert(key, on);
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
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, index, key = param.key](double v) {
                    if (m_building)
                        return;
                    Rule updated = m_scene->rules().at(index);
                    updated.actionParams.insert(key, v);
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

void RulesPanel::refreshProperties(int index)
{
    if (index < 0 || index >= m_rows.size())
        return;

    const Rule rule = m_scene->rules().at(index);
    const QString stored = rule.isAction() ? actionKey(rule.actionId) : rule.propertyKey;
    m_rows[index].property->selectData(stored);

    // Same fallback problem as the watch list: what is shown has to be what
    // is stored, or the first click on the entry already displayed does
    // nothing at all.
    const QString shown = m_rows[index].property->currentData().toString();
    QString action = rule.actionId;
    if (shown != stored && !shown.isEmpty()) {
        Rule updated = rule;
        action = actionIdOf(shown);
        updated.actionId = action;
        updated.actionParams = action.isEmpty() ? QVariantMap()
                                                : defaultActionParams(action);
        updated.propertyKey = action.isEmpty() ? shown : QString();
        commit(index, updated);
    }

    // An action has no Set/Toggle/Negate/Add -- performing it is the whole
    // thing. Decided after the fallback above, or an action picked for the
    // rule on its behalf would leave the operation box behind.
    m_rows[index].op->setVisible(action.isEmpty());
}

void RulesPanel::applyWatchChoice(int index, const QString &chosen)
{
    if (index < 0 || index >= m_scene->rules().size())
        return;

    Rule updated = m_scene->rules().at(index);
    if (chosen.startsWith(kEventPrefix)) {
        updated.eventId = chosen.mid(kEventPrefix.size());
        updated.conditionKey.clear();
        if (updated.conditionValue.typeId() != QMetaType::QString)
            updated.conditionValue = QString();
    } else {
        updated.conditionKey = chosen;
        if (updated.isEvent()) {
            updated.eventId.clear();
            updated.compare = Rule::Compare::Greater;
            updated.conditionValue = 0.0;
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
    row.heading->setText(captionFor(index));
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

void RulesPanel::scheduleValueEditorRefresh(int index)
{
    // Rebuilding from inside a combo box's own currentIndexChanged deletes that
    // combo box while Qt is still delivering the signal, which took the whole
    // application down. Queued, the handler returns first.
    QMetaObject::invokeMethod(
        this, [this, index] { refreshValueEditor(index); }, Qt::QueuedConnection);
}

void RulesPanel::refreshValueEditor(int index)
{
    if (index < 0 || index >= m_rows.size())
        return;
    Row &row = m_rows[index];
    const Rule rule = m_scene->rules().at(index);

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

    if (rule.isAction() && m_scene->explosionNamed(rule.targetName)) {
        // Aimed at an explosion, the settings belong to that object -- it is
        // placed, sized and tuned on the canvas, and the rule only says when.
        auto *label = new QLabel(tr("(set on the object)"), row.valueHolder);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        label->setStyleSheet(QStringLiteral("color: #8f8f8f;"));
        row.value = label;
    } else if (rule.isAction()) {
        // Aimed at anything else there is no object to carry them, so the
        // action's own parameters are edited here. Without this they stayed at
        // whatever they were seeded with and the rule could not be tuned.
        row.value = buildActionParamEditor(index, rule, row.valueHolder);
    } else if (!Rule::usesValue(rule.op)) {
        auto *label = new QLabel(tr("(current value)"), row.valueHolder);
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        label->setStyleSheet(QStringLiteral("color: #8f8f8f;"));
        row.value = label;
    } else if (propertyIsFlag(rule.targetName, rule.propertyKey)) {
        auto *check = new QCheckBox(row.valueHolder);
        check->setChecked(rule.value.toBool());
        connect(check, &QCheckBox::toggled, this, [this, index](bool on) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.value = on;
            commit(index, updated);
        });
        row.value = check;
    } else if (propertyIsChoice(rule.targetName, rule.propertyKey)) {
        auto *combo = new QComboBox(row.valueHolder);
        combo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        const physics::JointParam *param = describe(rule.targetName, rule.propertyKey);
        combo->addItems(param->choices);
        combo->setToolTip(param->tooltip);
        combo->setCurrentIndex(qBound(0, rule.value.toInt(), param->choices.size() - 1));
        connect(combo, &QComboBox::currentIndexChanged, this, [this, index](int at) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.value = at;
            commit(index, updated);
        });
        row.value = combo;
    } else {
        auto *spin = new QDoubleSpinBox(row.valueHolder);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        spin->setMinimumWidth(70);
        // Range, precision and step as the engine declared them.
        if (const physics::JointParam *p = describe(rule.targetName, rule.propertyKey)) {
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
                if (joint->name() != rule.targetName)
                    continue;
                for (const physics::JointType &type : engine->jointTypes()) {
                    if (type.id != joint->typeId())
                        continue;
                    for (const physics::JointParam &param : type.params) {
                        if (param.key != rule.propertyKey)
                            continue;
                        spin->setRange(param.minValue, param.maxValue);
                        spin->setDecimals(param.decimals);
                        spin->setSingleStep(param.step);
                        spin->setToolTip(param.tooltip);
                    }
                }
            }
        }
        spin->setValue(rule.value.toDouble());
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.value = v;
            commit(index, updated);
        });
        row.value = spin;
    }

    row.valueHolder->layout()->addWidget(row.value);

    // Only a number-valued rule can take its value from elsewhere, so the mode
    // picker is offered there and nowhere else.
    const bool canSource = !rule.isAction() && Rule::usesValue(rule.op)
                           && !propertyIsFlag(rule.targetName, rule.propertyKey);
    const bool sourced = canSource && rule.usesSource();
    if (row.valueMode) {
        row.valueMode->setVisible(canSource);
        row.valueMode->setCurrentIndex(row.valueMode->findData(sourced));
    }
    if (row.form)
        row.form->setRowVisible(row.sourceRow, sourced);
    // The two are alternatives, so the typed editor goes away entirely rather
    // than sitting there greyed out next to the object that replaced it.
    if (row.form)
        row.form->setRowVisible(row.valueRow, !sourced);

    if (sourced) {
        auto *layout = qobject_cast<QFormLayout *>(row.sourceHolder->layout());

        auto *from = new ObjectComboBox([this] { return sourceObjectChoices(); },
                                        row.sourceHolder);
        from->setMinimumContentsLength(10);
        from->selectData(rule.sourceObject);
        from->setToolTip(tr("Which object the value is read from."));
        connect(from, &QComboBox::currentIndexChanged, this, [this, index](int) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.sourceObject = m_rows[index].source2->currentData().toString();
            // Another object has other properties, so the old key cannot just
            // be carried across.
            const QVector<RuleChoice> properties = readablesOf(updated.sourceObject);
            updated.sourceProperty = properties.isEmpty() ? QString()
                                                          : properties.first().data;
            commit(index, updated);
            scheduleValueEditorRefresh(index);
        });
        row.source2 = from;
        layout->addRow(tr("object"), from);

        auto *what = new ObjectComboBox(
            [this, index] {
                return readablesOf(index < m_scene->rules().size()
                                       ? m_scene->rules().at(index).sourceObject
                                       : QString());
            },
            row.sourceHolder);
        what->setMinimumContentsLength(10);
        what->selectData(rule.sourceProperty);
        what->setToolTip(tr("Which of its properties to read."));
        connect(what, &QComboBox::currentIndexChanged, this, [this, index](int) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.sourceProperty = m_rows[index].sourceProperty->currentData().toString();
            commit(index, updated);
        });
        row.sourceProperty = what;
        layout->addRow(tr("property"), what);

        auto *offset = new QDoubleSpinBox(row.sourceHolder);
        offset->setRange(-1e6, 1e6);
        offset->setDecimals(1);
        offset->setSingleStep(10.0);
        offset->setMinimumWidth(70);
        offset->setValue(rule.sourceOffset);
        offset->setToolTip(tr("Added to whatever that property reads. Zero to "
                              "take it as it comes."));
        connect(offset, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
            if (m_building)
                return;
            Rule updated = m_scene->rules().at(index);
            updated.sourceOffset = v;
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
}

void RulesPanel::addRule()
{
    if (!m_scene)
        return;

    Rule rule;
    if (!m_scene->bodies().isEmpty()) {
        rule.subjectName = m_scene->bodies().first()->name();
        rule.targetName = rule.subjectName;
    }
    if (!m_scene->joints().isEmpty()) {
        rule.subjectName = m_scene->joints().first()->name();
        rule.targetName = rule.subjectName;
    }

    QVector<Rule> rules = m_scene->rules();
    rules.append(rule);
    m_scene->setRules(rules);
    m_scene->notifyEdit(tr("Add rule"));
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
    for (Row &row : m_rows) {
        row.source->refill();
        row.event->refill();
        row.target->refill();
        row.property->refill();
        if (row.event->isVisible())
            row.event->refill();
    }
}
