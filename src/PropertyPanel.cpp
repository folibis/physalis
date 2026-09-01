#include "PropertyPanel.h"
#include "ShapeItem.h"
#include "CanvasScene.h"
#include "Joint.h"
#include "RayItem.h"
#include "Rule.h"
#include "PhysicsBody.h"
#include "PropertyPane/PropertyPaneFactory.h"
#include "Icons.h"

#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPalette>
#include <QToolButton>
#include <QColorDialog>
#include <QLabel>
#include <QMenu>
#include <QSlider>
#include <QComboBox>
#include <QFont>
#include <QSizePolicy>
#include <QColor>
#include <QHash>
#include <QTabWidget>

PropertyPanel::PropertyPanel(QWidget *parent)
    : QWidget(parent)
    , m_factory(new PropertyPaneFactory(this))
{
    m_title = new QLabel(this);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_title);
    layout->addWidget(m_tabs);

    setEditorMode(m_mode);
    setActiveItem(nullptr);
}

void PropertyPanel::setEditorMode(EditorMode mode)
{
    m_mode = mode;

    updateActivePane();
}

void PropertyPanel::setTitle(const QString &subject)
{
    m_title->setText(tr("%1 Properties").arg(subject));
    m_title->setStyleSheet(QStringLiteral("font-weight: bold; padding: 2px; color: %1;")
                               .arg(EditorModes::accent(m_mode).name()));
}

void PropertyPanel::fitNameColumn(QTableWidget *table)
{
    int width = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (QWidget *cell = table->cellWidget(row, 0))
            width = qMax(width, cell->sizeHint().width());
    }
    if (width > 0)
        table->setColumnWidth(0, width + 6);
}

QTableWidget *PropertyPanel::tableForSection(const QString &section)
{
    auto it = m_sectionTables.constFind(section);
    if (it != m_sectionTables.constEnd())
        return it.value();

    auto *table = new QTableWidget(0, 2, m_tabs);
    table->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->verticalHeader()->setVisible(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);

    m_sectionTables.insert(section, table);
    m_tabs->addTab(table, section);
    return table;
}

void PropertyPanel::addGroupHeader(QTableWidget *table, const QString &title)
{
    const int tableRow = table->rowCount();
    table->insertRow(tableRow);

    auto *item = new QTableWidgetItem(title);
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
    item->setFlags(Qt::NoItemFlags);
    item->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    const QPalette palette = table->palette();
    item->setBackground(palette.color(QPalette::Window).darker(105));
    item->setForeground(palette.color(QPalette::WindowText));

    table->setItem(tableRow, 0, item);
    table->setSpan(tableRow, 0, 1, 2);
}

void PropertyPanel::addRow(QTableWidget *table, const PropertyRow &row)
{
    const int tableRow = table->rowCount();
    table->insertRow(tableRow);

    auto *nameCell = new QWidget(table);
    auto *nameLayout = new QHBoxLayout(nameCell);
    nameLayout->setContentsMargins(4, 0, 2, 0);
    nameLayout->setSpacing(4);

    auto *nameLabel = new QLabel(row.label, nameCell);
    nameLayout->addWidget(nameLabel);
    // Only a property the engine can name can be logged, since the log reads
    // values back out of the running world.
    if (!row.key.isEmpty()) {
        // On the whole cell, not the label: a label is only as wide as its
        // text, so a menu on it answers on "Rotation" and nowhere else in the
        // column -- which reads as no menu at all.
        nameCell->setContextMenuPolicy(Qt::CustomContextMenu);
        nameLabel->setContextMenuPolicy(Qt::CustomContextMenu);
        const QString key = row.key;
        const QString section = row.section;
        const QString label = row.label;
        const auto popUp = [this, key, section, label, nameLabel](QWidget *from,
                                                                  const QPoint &pos) {
            Row entry;
            entry.key = key;
            entry.section = section;
            entry.label = label;
            entry.nameLabel = nameLabel;
            showRowMenu(entry, from->mapToGlobal(pos));
        };
        connect(nameCell, &QWidget::customContextMenuRequested, this,
                [popUp, nameCell](const QPoint &pos) { popUp(nameCell, pos); });
        connect(nameLabel, &QWidget::customContextMenuRequested, this,
                [popUp, nameLabel](const QPoint &pos) { popUp(nameLabel, pos); });
    }
    nameLayout->addStretch();

    QToolButton *resetButton = nullptr;
    if (row.defaultValue.isValid()) {
        const QString defaultText = row.defaultValue.typeId() == QMetaType::Bool
                                        ? (row.defaultValue.toBool() ? tr("on") : tr("off"))
                                        : row.defaultValue.toString();

        resetButton = new QToolButton(nameCell);
        resetButton->setIcon(Icons::resetValue());
        resetButton->setIconSize(QSize(15, 15));
        resetButton->setAutoRaise(true);
        resetButton->setCursor(Qt::PointingHandCursor);
        resetButton->setToolTip(tr("Reset to default (%1)").arg(defaultText));

        QSizePolicy policy = resetButton->sizePolicy();
        policy.setRetainSizeWhenHidden(true);
        resetButton->setSizePolicy(policy);
        resetButton->hide(); // shown once the value differs; see updateModifiedMarks()

        const QVariant defaultValue = row.defaultValue;
        const std::function<void(const QVariant &)> resetSetter = row.setter;
        connect(resetButton, &QToolButton::clicked, this, [this, resetSetter, defaultValue] {
            resetSetter(defaultValue);
            refreshValues();
            if (!m_item)
                emit fieldSettingsChanged();
        });
        nameLayout->addWidget(resetButton);
    }

    table->setCellWidget(tableRow, 0, nameCell);

    const QString editLabel = row.label;
    const std::function<void(const QVariant &)> rawSetter = row.setter;
    const std::function<void(const QVariant &)> setter =
        [this, rawSetter, editLabel](const QVariant &value) {
            rawSetter(value);
            reportEdit(editLabel);
        };

    QWidget *editor = nullptr;
    QWidget *cellWidget = nullptr;
    switch (row.type) {
    case PropertyFieldType::Numeric: {
        auto *spin = new QDoubleSpinBox(table);
        spin->setRange(row.minValue, row.maxValue);
        const bool fineGrained = (row.maxValue - row.minValue) <= 2.0;
        spin->setDecimals(row.decimals >= 0 ? row.decimals : (fineGrained ? 2 : 1));
        spin->setSingleStep(row.step > 0.0 ? row.step : (fineGrained ? 0.05 : 1.0));
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, setter](double v) {
            if (m_updating)
                return;
            setter(v);
            updateModifiedMarks();
            if (!m_item)
                emit fieldSettingsChanged();
        });
        editor = spin;
        break;
    }
    case PropertyFieldType::Boolean: {
        auto *check = new QCheckBox(table);
        connect(check, &QCheckBox::toggled, this, [this, setter](bool v) {
            if (m_updating)
                return;
            setter(v);
            updateModifiedMarks();
            if (!m_item)
                emit fieldSettingsChanged();
        });
        editor = check;
        break;
    }
    case PropertyFieldType::String: {
        auto *edit = new QLineEdit(table);
        connect(edit, &QLineEdit::editingFinished, this, [this, setter, edit] {
            if (m_updating)
                return;
            setter(edit->text());
            updateModifiedMarks();
            if (!m_item)
                emit fieldSettingsChanged();
        });
        editor = edit;
        break;
    }
    case PropertyFieldType::Slider: {
        auto *container = new QWidget(table);
        auto *hbox = new QHBoxLayout(container);
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(6);

        auto *valueLabel = new QLabel(container);
        valueLabel->setMinimumWidth(24);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        auto *slider = new QSlider(Qt::Horizontal, container);
        slider->setRange(static_cast<int>(row.minValue), static_cast<int>(row.maxValue));
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(qMax(1, static_cast<int>((row.maxValue - row.minValue) / 10.0)));

        hbox->addWidget(valueLabel);
        hbox->addWidget(slider, 1);

        connect(slider, &QSlider::valueChanged, this, [this, setter, valueLabel](int v) {
            valueLabel->setText(QString::number(v));
            if (m_updating)
                return;
            setter(v);
            updateModifiedMarks();
            if (!m_item)
                emit fieldSettingsChanged();
        });

        editor = slider;
        cellWidget = container;
        break;
    }
    case PropertyFieldType::Choice: {
        auto *combo = new QComboBox(table);
        combo->addItems(row.choices);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, setter](int index) {
            if (m_updating)
                return;
            setter(index);
            updateModifiedMarks();
            if (!m_item)
                emit fieldSettingsChanged();
        });
        editor = combo;
        break;
    }
    case PropertyFieldType::Color: {
        auto *button = new QToolButton(table);
        const std::function<QVariant()> getter = row.getter;
        connect(button, &QToolButton::clicked, this, [this, setter, getter] {
            const QColor chosen = QColorDialog::getColor(getter().value<QColor>(), this,
                                                           tr("Choose Color"), QColorDialog::ShowAlphaChannel);
            if (chosen.isValid()) {
                setter(chosen);
                updateModifiedMarks();
                if (!m_item)
                    emit fieldSettingsChanged();
            }
        });
        editor = button;
        break;
    }
    }

    if (row.readOnly && editor) {
        // Left visible and selectable, so the value can still be read and
        // copied, but it cannot be typed into or focused by tabbing.
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(editor)) {
            spin->setReadOnly(true);
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        } else if (auto *line = qobject_cast<QLineEdit *>(editor)) {
            line->setReadOnly(true);
        } else {
            editor->setEnabled(false);
        }
        editor->setFocusPolicy(Qt::NoFocus);
        QPalette muted = editor->palette();
        muted.setColor(QPalette::Base, editor->palette().color(QPalette::Window));
        muted.setColor(QPalette::Text, editor->palette().color(QPalette::Disabled, QPalette::Text));
        editor->setPalette(muted);
    }

    if (!row.tooltip.isEmpty()) {
        nameCell->setToolTip(row.tooltip);
        if (editor)
            editor->setToolTip(row.tooltip);
    }

    table->setCellWidget(tableRow, 1, cellWidget ? cellWidget : editor);
    m_rows.push_back({row.type, editor, row.getter, nameLabel, resetButton, row.defaultValue,
                      row.key, row.section, row.label});
}

void PropertyPanel::rebuildRows()
{
    const QString openSection = m_tabs->currentIndex() >= 0
                                    ? m_tabs->tabText(m_tabs->currentIndex())
                                    : QString();

    m_tabs->clear();
    qDeleteAll(m_sectionTables);
    m_sectionTables.clear();
    m_rows.clear();

    if (m_activePane) {
        QHash<QString, QVariant> defaults;
        for (const PropertyRow &row : m_activePane->defaultRows(m_mode))
            defaults.insert(row.label, row.getter ? row.getter() : QVariant());

        QHash<QTableWidget *, QString> openGroup;
        for (PropertyRow row : m_activePane->rows(m_mode)) {
            row.defaultValue = defaults.value(row.label);
            QTableWidget *table = tableForSection(row.section);
            if (!row.group.isEmpty() && openGroup.value(table) != row.group) {
                addGroupHeader(table, row.group);
                openGroup.insert(table, row.group);
            }
            addRow(table, row);
        }
    }

    for (QTableWidget *table : std::as_const(m_sectionTables))
        fitNameColumn(table);

    if (!openSection.isEmpty()) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (m_tabs->tabText(i) == openSection) {
                m_tabs->setCurrentIndex(i);
                break;
            }
        }
    }

    refreshValues();
    updateWatchMarks();
}

void PropertyPanel::setActivePane(PropertyPane *pane)
{
    if (m_activePane != pane) {
        if (m_activePane)
            disconnect(m_activePane, nullptr, this, nullptr);
        m_activePane = pane;
        if (m_activePane) {
            connect(m_activePane, &PropertyPane::valueChanged, this, &PropertyPanel::refreshValues);
            connect(m_activePane, &PropertyPane::rowsChanged, this, &PropertyPanel::rebuildRows);
        }
    }
    rebuildRows();
}

void PropertyPanel::reportEdit(const QString &label)
{
    if (!m_scene || m_updating)
        return;

    const QString key = QStringLiteral("property|%1|%2")
                            .arg(reinterpret_cast<quintptr>(subject()))
                            .arg(label);
    m_scene->notifyEdit(tr("Change %1").arg(label), key);
}

QString PropertyPanel::objectNameFor(const QString &section) const
{
    if (!m_scene)
        return m_item ? m_item->name() : QString();

    if (section == QLatin1String("Joint")) {
        if (Joint *joint = m_scene->selectedJoint())
            return joint->name();
        return QString();
    }
    if (section == QLatin1String("Body")) {
        if (PhysicsBody *body = m_scene->commonSelectedBody())
            return body->name();
        return QString();
    }
    // The World rows belong to the run, which is not an object in the scene.
    if (section == QLatin1String("World"))
        return Rule::world();

    if (section == QLatin1String("Ray")) {
        if (RayItem *ray = m_scene->selectedRay())
            return ray->name();
        return QString();
    }

    if (section == QLatin1String("Shape") || section == QLatin1String("Collision")) {
        if (!m_scene->physicsSelection().isEmpty())
            return m_scene->physicsSelection().first()->name();
        return QString();
    }
    return m_item ? m_item->name() : QString();
}

void PropertyPanel::showRowMenu(const Row &row, const QPoint &globalPos)
{
    if (!m_scene)
        return;
    const QString object = objectNameFor(row.section);
    if (object.isEmpty())
        return;

    QMenu menu;
    const bool watched = m_scene->isWatched(object, row.key);
    if (watched) {
        menu.addAction(tr("Remove from Log"), this, [this, object, row] {
            m_scene->removeWatch(object, row.key);
        });
    } else {
        menu.addAction(tr("Add to Log"), this, [this, object, row] {
            CanvasScene::Watch watch;
            watch.objectName = object;
            watch.propertyKey = row.key;
            watch.label = row.label;
            m_scene->addWatch(watch);
        });
    }
    if (!m_scene->watches().isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Clear Log"), this, [this] { m_scene->clearWatches(); });
    }
    menu.exec(globalPos);
}

void PropertyPanel::updateWatchMarks()
{
    if (!m_scene)
        return;
    for (const Row &row : m_rows) {
        if (!row.nameLabel || row.key.isEmpty())
            continue;
        const QString object = objectNameFor(row.section);
        const bool watched = !object.isEmpty() && m_scene->isWatched(object, row.key);
        // Blue and bold: the same property can be in the log or not, and the
        // table is the only place that says which.
        row.nameLabel->setStyleSheet(watched
                                         ? QStringLiteral("color: #1E6FD9; font-weight: bold;")
                                         : QString());
    }
}

void *PropertyPanel::subject() const
{
    if (!m_scene)
        return m_item;
    if (Joint *joint = m_scene->selectedJoint())
        return joint;
    if (m_mode == EditorMode::Edit)
        return m_item;
    if (!m_scene->physicsSelection().isEmpty())
        return m_scene->physicsSelection().first();
    return nullptr; // the field pane, which edits the scene itself
}

void PropertyPanel::updateEditable()
{
    setEnabled(!m_scene || m_scene->selectionAllowed());
}

void PropertyPanel::setScene(CanvasScene *scene)
{
    m_scene = scene;
    if (!m_scene)
        return;

    connect(m_scene, &CanvasScene::simulationRunningChanged, this,
            [this] { updateEditable(); });
    updateEditable();

    connect(m_scene, &CanvasScene::watchesChanged, this, &PropertyPanel::updateWatchMarks);
    connect(m_scene, &CanvasScene::physicsSelectionChanged, this, &PropertyPanel::updateActivePane);
    connect(m_scene, &CanvasScene::selectedJointChanged, this, &PropertyPanel::updateActivePane);
    connect(m_scene, &CanvasScene::selectedExplosionChanged, this, &PropertyPanel::updateActivePane);
    connect(m_scene, &CanvasScene::selectedRayChanged, this, &PropertyPanel::updateActivePane);
    updateActivePane();
}

void PropertyPanel::showSection(const QString &section)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabText(i) == section) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
}

void PropertyPanel::setActiveItem(ShapeItem *item)
{
    m_item = item;
    updateEditable();
    updateActivePane();
}

void PropertyPanel::updateActivePane()
{
    if (!m_scene) {
        setTitle(EditorModes::name(m_mode));
        setActivePane(m_item ? m_factory->paneFor(m_item) : nullptr);
        return;
    }

    if (m_mode != EditorMode::Edit) {
        if (m_scene->selectedRay()) {
            setTitle(tr("Ray"));
            setActivePane(m_factory->paneForRay(m_scene));
        } else if (m_scene->selectedExplosion()) {
            setTitle(tr("Point"));
            setActivePane(m_factory->paneForExplosion(m_scene));
        } else if (m_scene->selectedJoint()) {
            setTitle(tr("Joint"));
            setActivePane(m_factory->paneForJoints(m_scene));
        } else {
            setTitle(EditorModes::name(m_mode));
            // A sensor is an area, not a body with a shape on it, and
            // nearly nothing in the physics vocabulary applies to one.
            const auto &picked = m_scene->physicsSelection();
            const bool sensor = !picked.isEmpty() && picked.first()->body()
                                && picked.first()->part().isSensor;
            if (sensor)
                setTitle(tr("Sensor"));
            setActivePane(picked.isEmpty()
                              ? m_factory->paneForField(m_scene)
                              : (sensor ? m_factory->paneForSensor(m_scene)
                                        : m_factory->paneForPhysics(m_scene)));
        }
        return;
    }

    setTitle(EditorModes::name(m_mode));
    setActivePane(m_item ? m_factory->paneFor(m_item) : m_factory->paneForField(m_scene));
}

namespace {

bool differsFromDefault(const QVariant &value, const QVariant &def)
{
    if (!def.isValid())
        return false;

    switch (def.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float:
        return qAbs(value.toDouble() - def.toDouble()) > 1e-6;
    case QMetaType::Bool:
        return value.toBool() != def.toBool();
    case QMetaType::QColor:
        return value.value<QColor>() != def.value<QColor>();
    default:
        return value.toString() != def.toString();
    }
}

} // namespace

void PropertyPanel::updateModifiedMarks()
{
    for (const Row &row : m_rows) {
        if (!row.nameLabel)
            continue;

        const bool modified = differsFromDefault(row.getter(), row.defaultValue);

        QFont font = row.nameLabel->font();
        font.setBold(modified);
        row.nameLabel->setFont(font);

        if (row.resetButton)
            row.resetButton->setVisible(modified);
    }
}

void PropertyPanel::refreshValues()
{
    updateModifiedMarks();

    m_updating = true;
    for (const Row &row : m_rows) {
        const QVariant value = row.getter();

        switch (row.type) {
        case PropertyFieldType::Numeric:
            static_cast<QDoubleSpinBox *>(row.editor)->setValue(value.toDouble());
            break;
        case PropertyFieldType::Boolean:
            static_cast<QCheckBox *>(row.editor)->setChecked(value.toBool());
            break;
        case PropertyFieldType::String:
            static_cast<QLineEdit *>(row.editor)->setText(value.toString());
            break;
        case PropertyFieldType::Slider:
            static_cast<QSlider *>(row.editor)->setValue(value.toInt());
            break;
        case PropertyFieldType::Choice:
            static_cast<QComboBox *>(row.editor)->setCurrentIndex(value.toInt());
            break;
        case PropertyFieldType::Color: {
            const QColor c = value.value<QColor>();
            static_cast<QToolButton *>(row.editor)
                ->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(c.name(QColor::HexArgb)));
            break;
        }
        }
    }
    m_updating = false;
}
