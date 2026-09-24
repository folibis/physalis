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

    m_remove = new QToolButton(this);
    m_remove->setIcon(Icons::deleteShape());
    m_remove->setText(tr("Remove"));
    m_remove->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_remove->setAutoRaise(true);
    m_remove->setToolTip(tr("Remove the selected variable. Rules naming it will"
                            " say they are unfinished."));
    connect(m_remove, &QToolButton::clicked, this, &VariablesPanel::removeSelected);
    tools->addWidget(m_remove);

    // The log is where a variable is actually watched counting, so the offer
    // belongs on the toolbar rather than only behind a right-click.
    m_log = new QToolButton(this);
    m_log->setIcon(Icons::log());
    m_log->setText(tr("Add to Log"));
    m_log->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_log->setAutoRaise(true);
    connect(m_log, &QToolButton::clicked, this, &VariablesPanel::toggleLog);
    tools->addWidget(m_log);

    tools->addStretch();
    outer->addLayout(tools);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("Name"), tr("Type"), tr("Value") });
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kType, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kValue, QHeaderView::ResizeToContents);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &where) {
        const int row = m_table->rowAt(where.y());
        if (row >= 0)
            showMenu(row, m_table->viewport()->mapToGlobal(where));
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &VariablesPanel::syncTools);
    outer->addWidget(m_table, 1);

    syncTools();
}

void VariablesPanel::setScene(CanvasScene *scene)
{
    if (m_scene)
        m_scene->disconnect(this);

    m_scene = scene;
    if (m_scene) {
        connect(m_scene, &CanvasScene::variablesChanged, this, &VariablesPanel::rebuild);
        // The toolbar says "Add to Log" or "Remove from Log", so it follows
        // the log as well as the selection.
        connect(m_scene, &CanvasScene::watchesChanged, this, &VariablesPanel::syncTools);
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

int VariablesPanel::selectedRow() const
{
    const int row = m_table ? m_table->currentRow() : -1;
    if (!m_scene || row < 0 || row >= m_scene->variables().size())
        return -1;
    return row;
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
    if (event->type() == QEvent::MouseButtonPress) {
        // A cell widget covers its cell, so the table never learns which row
        // was clicked and the selection would never move off the first one.
        const int row = rowOf(watched);
        if (row >= 0 && m_table && row != m_table->currentRow())
            m_table->setCurrentCell(row, kName);
    } else if (event->type() == QEvent::MouseButtonDblClick) {
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
    m_building = false;
    // Something selected from the start, or the toolbar sits greyed out until
    // a row is clicked and the offer to log one looks unavailable.
    if (m_table->currentRow() < 0 && m_table->rowCount() > 0)
        m_table->setCurrentCell(0, kName);
    syncTools();
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
    m_table->setCurrentCell(updated.size() - 1, kName);
    // Straight into renaming it: the name it was given is a placeholder.
    beginRename(updated.size() - 1);
}

void VariablesPanel::removeSelected()
{
    const int row = selectedRow();
    if (row < 0)
        return;

    const QString gone = m_scene->variables().at(row).name;
    QVector<SceneVariable> updated = m_scene->variables();
    updated.remove(row);
    m_scene->setVariables(updated);
    // Anything logging it has nothing left to read.
    m_scene->removeWatch(Rule::variables(), gone);
    m_scene->notifyEdit(tr("Remove variable"));
}

void VariablesPanel::toggleLog()
{
    const int row = selectedRow();
    if (row < 0)
        return;

    const SceneVariable variable = m_scene->variables().at(row);
    if (m_scene->isWatched(Rule::variables(), variable.name))
        m_scene->removeWatch(Rule::variables(), variable.name);
    else
        m_scene->addWatch({ Rule::variables(), variable.name, variable.name });
    syncTools();
}

void VariablesPanel::syncTools()
{
    const int row = selectedRow();
    const bool has = row >= 0;
    if (m_remove)
        m_remove->setEnabled(has);
    if (!m_log)
        return;

    m_log->setEnabled(has);
    const bool watched =
        has && m_scene->isWatched(Rule::variables(), m_scene->variables().at(row).name);
    m_log->setText(watched ? tr("Remove from Log") : tr("Add to Log"));
    m_log->setToolTip(watched
                          ? tr("Stop showing this variable in the log during a run.")
                          : tr("Show this variable in the log while the scene runs."));
}

void VariablesPanel::showMenu(int row, const QPoint &globalPos)
{
    if (!m_scene || row < 0 || row >= m_scene->variables().size())
        return;

    m_table->setCurrentCell(row, kName);
    const SceneVariable variable = m_scene->variables().at(row);

    QMenu menu(this);
    menu.addAction(tr("Rename"), this, [this, row] { beginRename(row); });

    // The same offer the property tables make, so a variable is watched during
    // a run exactly as a body's speed is.
    const bool watched = m_scene->isWatched(Rule::variables(), variable.name);
    menu.addAction(watched ? tr("Remove from Log") : tr("Add to Log"),
                   this, &VariablesPanel::toggleLog);
    menu.addSeparator();
    menu.addAction(tr("Remove Variable"), this, &VariablesPanel::removeSelected);
    menu.exec(globalPos);
}
