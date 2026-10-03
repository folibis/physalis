// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "VariablesPanel.h"

#include "CanvasScene.h"
#include "Icons.h"
#include "Rule.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kName = 0;
constexpr int kType = 1;
constexpr int kValue = 2;

// A cell widget carries the row it belongs to, since a click lands on it and
// never reaches the table that would otherwise say which row was meant.
const char *kRowProperty = "variableRow";
} // namespace

VariablesPanel::VariablesPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void VariablesPanel::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(4);

    auto *tools = new QHBoxLayout;
    tools->setSpacing(2);

    auto *add = new QToolButton(this);
    add->setIcon(Icons::add());
    add->setText(tr("Add"));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setAutoRaise(true);
    add->setToolTip(tr("Add a variable to this scene."));
    connect(add, &QToolButton::clicked, this, &VariablesPanel::addVariable);
    tools->addWidget(add);

    tools->addStretch();
    outer->addLayout(tools);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("Name"), tr("Type"), tr("Value") });
    m_table->verticalHeader()->setVisible(false);
    // No selection: every cell holds a widget, so a highlighted row shows only
    // in the gaps between them and in whatever the widgets do not cover --
    // which reads as one row being a different colour for no reason. Nothing
    // needs it either; the menu acts on the row that was clicked.
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
    // Not ResizeToContents: every cell here holds a widget, and that mode sizes
    // a column to its *item*, which is empty -- so the type box came out reading
    // "In" and the value box had no room for a number. The width is taken from
    // what the widgets ask for instead, once they are there.
    m_table->horizontalHeader()->setSectionResizeMode(kType, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kValue, QHeaderView::Fixed);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &where) {
        const int row = m_table->rowAt(where.y());
        if (row >= 0)
            showMenu(row, m_table->viewport()->mapToGlobal(where));
    });
    outer->addWidget(m_table, 1);
}

void VariablesPanel::setScene(CanvasScene *scene)
{
    if (m_scene)
        m_scene->disconnect(this);

    m_scene = scene;
    if (m_scene) {
        connect(m_scene, &CanvasScene::variablesChanged, this, &VariablesPanel::rebuild);
        // Editing them mid-run would change what the run is counting with.
        connect(m_scene, &CanvasScene::simulationRunningChanged, this,
                [this](bool running) { setEnabled(!running); });
    }
    rebuild();
}

int VariablesPanel::rowOf(const QObject *widget)
{
    return widget ? widget->property(kRowProperty).toInt() : -1;
}

void VariablesPanel::adopt(QWidget *widget, int row)
{
    widget->setProperty(kRowProperty, row);
    widget->installEventFilter(this);
    widget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(widget, &QWidget::customContextMenuRequested, this,
            [this, widget](const QPoint &where) {
        showMenu(rowOf(widget), widget->mapToGlobal(where));
    });
}

bool VariablesPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        const int row = rowOf(watched);
        if (row >= 0 && qobject_cast<QLabel *>(watched)) {
            beginRename(row);
            return true;
        }
    } else if (event->type() == QEvent::KeyPress) {
        if (auto *edit = qobject_cast<QLineEdit *>(watched)) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) {
                finishRename(rowOf(edit), false);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VariablesPanel::rebuild()
{
    if (!m_table)
        return;

    m_building = true;
    m_table->clearContents();
    m_rows.clear();
    m_table->setRowCount(m_scene ? m_scene->variables().size() : 0);
    m_rows.resize(m_table->rowCount());
    if (m_scene) {
        for (int row = 0; row < m_scene->variables().size(); ++row)
            fillRow(row, m_scene->variables().at(row));
    }
    fitColumns();
    m_building = false;
}

// What the boxes in a column actually need, which the table cannot work out for
// itself: a cell widget is not the cell's item, and sizing to the item leaves
// every one of them squashed.
void VariablesPanel::fitColumns()
{
    if (!m_table)
        return;
    constexpr int kPadding = 10;
    for (int column : { kType, kValue }) {
        int wanted = 0;
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (QWidget *cell = m_table->cellWidget(row, column))
                wanted = qMax(wanted, cell->sizeHint().width());
        }
        // Room for the heading as well, so a table with no rows still reads.
        const QString heading = m_table->horizontalHeaderItem(column)
                                    ? m_table->horizontalHeaderItem(column)->text()
                                    : QString();
        wanted = qMax(wanted, m_table->fontMetrics().horizontalAdvance(heading) + kPadding);
        m_table->setColumnWidth(column, wanted + kPadding);
    }
}

void VariablesPanel::fillRow(int row, const SceneVariable &variable)
{
    // The caption and the box that replaces it while it is being renamed --
    // the same arrangement a rule card's title uses.
    auto *holder = new QWidget(m_table);
    auto *layout = new QHBoxLayout(holder);
    layout->setContentsMargins(4, 0, 0, 0);
    layout->setSpacing(0);

    auto *name = new QLabel(variable.name, holder);
    name->setObjectName(QStringLiteral("variableName"));
    name->setToolTip(tr("What rules and the log call it.\n\nDouble-click to rename."));
    layout->addWidget(name);
    layout->addStretch();

    auto *rename = new QLineEdit(holder);
    // Named so it can be told from the boxes a spin box keeps inside itself.
    rename->setObjectName(QStringLiteral("variableRename"));
    rename->setVisible(false);
    connect(rename, &QLineEdit::editingFinished, this, [this, row] { finishRename(row, true); });
    layout->addWidget(rename);

    adopt(holder, row);
    adopt(name, row);
    rename->setProperty(kRowProperty, row);
    rename->installEventFilter(this);

    m_rows[row].name = name;
    m_rows[row].rename = rename;
    m_table->setCellWidget(row, kName, holder);

    auto *type = new QComboBox(m_table);
    for (SceneVariable::Type candidate : SceneVariable::types())
        type->addItem(SceneVariable::typeLabel(candidate), SceneVariable::typeName(candidate));
    type->setCurrentIndex(type->findData(SceneVariable::typeName(variable.type)));
    type->setToolTip(tr("What kind of value it holds."));
    connect(type, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, row, type](int) {
        if (m_building || !m_scene || row >= m_scene->variables().size())
            return;
        SceneVariable updated = m_scene->variables().at(row);
        updated.type = SceneVariable::typeFromName(type->currentData().toString());
        // Kept to the new type, so a flag does not go on holding a decimal.
        updated.initial = SceneVariable::coerce(updated.type, updated.initial);
        commit(row, updated);
        // The value editor belongs to the type, so it is replaced -- queued,
        // since doing it here would delete the box mid-signal.
        QMetaObject::invokeMethod(this, [this, row] {
            if (m_scene && row < m_scene->variables().size())
                setValueEditor(row, m_scene->variables().at(row));
        }, Qt::QueuedConnection);
    });
    adopt(type, row);
    m_table->setCellWidget(row, kType, type);

    setValueEditor(row, variable);
}

void VariablesPanel::setValueEditor(int row, const SceneVariable &variable)
{
    const bool wasBuilding = m_building;
    m_building = true;

    QWidget *editor = nullptr;
    switch (variable.type) {
    case SceneVariable::Type::Bool: {
        auto *check = new QCheckBox(m_table);
        check->setChecked(variable.value().toBool());
        connect(check, &QCheckBox::toggled, this, [this, row](bool on) {
            if (m_building || !m_scene || row >= m_scene->variables().size())
                return;
            SceneVariable updated = m_scene->variables().at(row);
            updated.initial = on;
            commit(row, updated);
        });
        editor = check;
        break;
    }
    case SceneVariable::Type::Integer: {
        auto *spin = new QSpinBox(m_table);
        spin->setRange(-1000000000, 1000000000);
        spin->setValue(variable.value().toInt());
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this, row](int v) {
            if (m_building || !m_scene || row >= m_scene->variables().size())
                return;
            SceneVariable updated = m_scene->variables().at(row);
            updated.initial = v;
            commit(row, updated);
        });
        editor = spin;
        break;
    }
    case SceneVariable::Type::Timer: {
        // Milliseconds, and never below zero. The suffix is what tells a
        // reader the 3000 in the cell is three seconds.
        auto *spin = new QSpinBox(m_table);
        spin->setRange(0, 1000000000);
        spin->setSingleStep(100);
        spin->setSuffix(tr(" ms"));
        spin->setValue(variable.value().toInt());
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this, row](int v) {
            if (m_building || !m_scene || row >= m_scene->variables().size())
                return;
            SceneVariable updated = m_scene->variables().at(row);
            updated.initial = v;
            commit(row, updated);
        });
        editor = spin;
        break;
    }
    case SceneVariable::Type::Double: {
        auto *spin = new QDoubleSpinBox(m_table);
        spin->setRange(-1e9, 1e9);
        spin->setDecimals(3);
        spin->setValue(variable.value().toDouble());
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, row](double v) {
            if (m_building || !m_scene || row >= m_scene->variables().size())
                return;
            SceneVariable updated = m_scene->variables().at(row);
            updated.initial = v;
            commit(row, updated);
        });
        editor = spin;
        break;
    }
    }

    adopt(editor, row);
    m_table->setCellWidget(row, kValue, editor);
    m_building = wasBuilding;
}

void VariablesPanel::beginRename(int row)
{
    if (row < 0 || row >= m_rows.size() || !m_scene || row >= m_scene->variables().size())
        return;
    RowWidgets &widgets = m_rows[row];
    if (!widgets.name || !widgets.rename)
        return;

    widgets.rename->setText(m_scene->variables().at(row).name);
    widgets.name->setVisible(false);
    widgets.rename->setVisible(true);
    widgets.rename->selectAll();
    widgets.rename->setFocus();
}

void VariablesPanel::finishRename(int row, bool keep)
{
    if (row < 0 || row >= m_rows.size() || !m_scene || row >= m_scene->variables().size())
        return;
    RowWidgets &widgets = m_rows[row];
    if (!widgets.name || !widgets.rename || !widgets.rename->isVisible())
        return;

    widgets.rename->setVisible(false);
    widgets.name->setVisible(true);

    SceneVariable updated = m_scene->variables().at(row);
    const QString wanted = keep ? widgets.rename->text().trimmed() : QString();
    if (wanted.isEmpty() || wanted == updated.name)
        return;

    const QString previous = updated.name;
    updated.name = m_scene->uniqueVariableName(wanted, row);
    commit(row, updated);
    widgets.name->setText(updated.name);
    // Rules and log rows naming it by the old key follow, the way they follow
    // any other rename -- otherwise every rule using it would quietly go
    // unfinished the moment it was renamed.
    m_scene->renameVariableInRules(previous, updated.name);
}

void VariablesPanel::commit(int row, const SceneVariable &variable)
{
    if (!m_scene || row < 0 || row >= m_scene->variables().size())
        return;

    QVector<SceneVariable> updated = m_scene->variables();
    updated[row] = variable;
    // Written straight in rather than through setVariables(), which would emit
    // and rebuild the very editor the change came from.
    m_scene->replaceVariables(updated);
    m_scene->notifyEdit(tr("Edit variable"), QStringLiteral("variable:%1").arg(row));
}

void VariablesPanel::addVariable()
{
    if (!m_scene)
        return;

    SceneVariable variable;
    variable.name = m_scene->uniqueVariableName(tr("variable"));
    variable.type = SceneVariable::Type::Double;
    variable.initial = 0.0;

    QVector<SceneVariable> updated = m_scene->variables();
    updated.append(variable);
    m_scene->setVariables(updated);
    m_scene->notifyEdit(tr("Add variable"));
    // Straight into renaming it: the name it was given is a placeholder.
    beginRename(updated.size() - 1);
}

void VariablesPanel::removeVariable(int row)
{
    if (!m_scene || row < 0 || row >= m_scene->variables().size())
        return;

    const QString gone = m_scene->variables().at(row).name;
    QVector<SceneVariable> updated = m_scene->variables();
    updated.remove(row);
    m_scene->setVariables(updated);
    // Anything logging it has nothing left to read.
    m_scene->removeWatch(Rule::variables(), gone);
    m_scene->notifyEdit(tr("Remove variable"));
}

void VariablesPanel::toggleLog(int row)
{
    if (!m_scene || row < 0 || row >= m_scene->variables().size())
        return;

    const SceneVariable variable = m_scene->variables().at(row);
    if (m_scene->isWatched(Rule::variables(), variable.name))
        m_scene->removeWatch(Rule::variables(), variable.name);
    else
        m_scene->addWatch({ Rule::variables(), variable.name, variable.name });
}

void VariablesPanel::showMenu(int row, const QPoint &globalPos)
{
    if (!m_scene || row < 0 || row >= m_scene->variables().size())
        return;

    const SceneVariable variable = m_scene->variables().at(row);

    QMenu menu(this);
    menu.addAction(tr("Rename"), this, [this, row] { beginRename(row); });

    // The same offer the property tables make, so a variable is watched during
    // a run exactly as a body's speed is. Every entry is given the row the menu
    // was opened for rather than reading the selection: the two are not always
    // the same row, and an entry that says one thing and does another is worse
    // than no entry at all.
    const bool watched = m_scene->isWatched(Rule::variables(), variable.name);
    menu.addAction(watched ? tr("Remove from Log") : tr("Add to Log"),
                   this, [this, row] { toggleLog(row); });
    menu.addSeparator();
    menu.addAction(tr("Remove Variable"), this, [this, row] { removeVariable(row); });
    menu.exec(globalPos);
}
