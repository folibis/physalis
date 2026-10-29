// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "ControlsPanel.h"

#include "CanvasScene.h"
#include "Icons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

ControlsPanel::ControlsPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void ControlsPanel::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *headerBar = new QWidget(this);
    auto *header = new QHBoxLayout(headerBar);
    header->setContentsMargins(6, 4, 6, 4);
    auto *add = new QToolButton(headerBar);
    add->setIcon(Icons::add());
    add->setText(tr("Add"));
    add->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    add->setAutoRaise(true);
    add->setToolTip(tr("Add a control to this scene. It appears on the canvas, under the"
                       " caption, and rules reach it through Controls."));
    connect(add, &QToolButton::clicked, this, &ControlsPanel::addControl);
    header->addStretch();
    header->addWidget(add);
    outer->addWidget(headerBar);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // The panel is a narrow dock and the cards are built to shrink into it.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *host = new QWidget(scroll);
    m_cards = new QVBoxLayout(host);
    m_cards->setContentsMargins(6, 6, 6, 6);
    m_cards->setSpacing(8);
    m_cards->addStretch();
    scroll->setWidget(host);
    outer->addWidget(scroll, 1);
}

void ControlsPanel::setScene(CanvasScene *scene)
{
    if (m_scene)
        disconnect(m_scene, nullptr, this, nullptr);
    m_scene = scene;
    if (m_scene)
        connect(m_scene, &CanvasScene::controlsChanged, this, &ControlsPanel::rebuild);
    rebuild();
}

void ControlsPanel::rebuild()
{
    if (!m_cards)
        return;

    m_building = true;
    // Everything but the stretch at the end.
    while (m_cards->count() > 1) {
        QLayoutItem *item = m_cards->takeAt(0);
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    const QVector<SceneControl> controls = m_scene ? m_scene->controls()
                                                   : QVector<SceneControl>();
    for (int row = 0; row < controls.size(); ++row)
        m_cards->insertWidget(row, buildCard(row, controls.at(row)));
    m_building = false;
}

// One card per control, its fields on rows of their own. A table could not
// hold a slider's four numbers without squeezing them into cells too narrow to
// read, which is what the rule cards already answer by not being a table.
QWidget *ControlsPanel::buildCard(int row, const SceneControl &control)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("controlCard"));
    card->setFrameShape(QFrame::NoFrame);
    card->setStyleSheet(QStringLiteral(
        "QFrame#controlCard { border: 1px solid #c8c8c8; border-radius: 4px;"
        " background: #fcfcfc; }"
        "QWidget#controlCardHeader { background: #ececec;"
        " border-bottom: 1px solid #c8c8c8;"
        " border-top-left-radius: 3px; border-top-right-radius: 3px; }"));

    auto *column = new QVBoxLayout(card);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto *headerBar = new QWidget(card);
    headerBar->setObjectName(QStringLiteral("controlCardHeader"));
    auto *header = new QHBoxLayout(headerBar);
    header->setContentsMargins(6, 3, 4, 3);
    auto *kindIcon = new QLabel(SceneControl::kindLabel(control.kind), headerBar);
    QFont bold = kindIcon->font();
    bold.setBold(true);
    kindIcon->setFont(bold);
    header->addWidget(kindIcon);
    header->addStretch();
    auto *remove = new QToolButton(headerBar);
    remove->setIcon(Icons::deleteShape());
    remove->setAutoRaise(true);
    remove->setToolTip(tr("Remove this control."));
    connect(remove, &QToolButton::clicked, this, [this, row] { removeControl(row); });
    header->addWidget(remove);
    column->addWidget(headerBar);

    auto *form = new QFormLayout;
    form->setContentsMargins(8, 6, 8, 8);
    form->setSpacing(4);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    column->addLayout(form);

    auto *id = new QLineEdit(control.id, card);
    id->setToolTip(tr("How a rule addresses it. Changing it carries every rule that"
                      " names it along."));
    connect(id, &QLineEdit::editingFinished, this, [this, row, id] {
        if (m_building || !m_scene || row >= m_scene->controls().size())
            return;
        SceneControl changed = m_scene->controls().at(row);
        const QString was = changed.id;
        changed.id = m_scene->uniqueControlId(id->text().trimmed(), row);
        if (changed.id == was)
            return;
        commit(row, changed);
        m_scene->renameControlInRules(was, changed.id);
    });
    form->addRow(tr("Id"), id);

    auto *caption = new QLineEdit(control.caption, card);
    caption->setPlaceholderText(control.id);
    caption->setToolTip(tr("What it says on the canvas. Empty is its id, and a rule may"
                           " change it while the scene runs."));
    connect(caption, &QLineEdit::editingFinished, this, [this, row, caption] {
        if (m_building || !m_scene || row >= m_scene->controls().size())
            return;
        SceneControl changed = m_scene->controls().at(row);
        if (changed.caption == caption->text())
            return;
        changed.caption = caption->text();
        commit(row, changed);
    });
    form->addRow(tr("Caption"), caption);

    auto *kind = new QComboBox(card);
    for (const SceneControl::Kind one : { SceneControl::Kind::Checkbox, SceneControl::Kind::Button,
                                          SceneControl::Kind::Slider }) {
        kind->addItem(SceneControl::kindLabel(one), SceneControl::kindName(one));
    }
    kind->setCurrentIndex(kind->findData(SceneControl::kindName(control.kind)));
    connect(kind, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, row, kind](int) {
        if (m_building || !m_scene || row >= m_scene->controls().size())
            return;
        SceneControl changed = m_scene->controls().at(row);
        changed.kind = SceneControl::kindFromName(kind->currentData().toString());
        changed.initial = changed.startingValue();
        commit(row, changed);
    });
    form->addRow(tr("Kind"), kind);

    // A button has nowhere to start from -- what it has is a press -- so it is
    // not offered a row that would lie about having one.
    if (control.kind == SceneControl::Kind::Checkbox) {
        auto *ticked = new QCheckBox(tr("Starts ticked"), card);
        ticked->setChecked(control.startingValue().toBool());
        connect(ticked, &QCheckBox::toggled, this, [this, row](bool on) {
            if (m_building || !m_scene || row >= m_scene->controls().size())
                return;
            SceneControl changed = m_scene->controls().at(row);
            changed.initial = on;
            commit(row, changed);
        });
        form->addRow(QString(), ticked);
    } else if (control.kind == SceneControl::Kind::Slider) {
        const char *labels[] = { QT_TR_NOOP("Starts at"), QT_TR_NOOP("Lowest"),
                                 QT_TR_NOOP("Highest"), QT_TR_NOOP("Step") };
        const double values[] = { control.startingValue().toDouble(), control.minimum,
                                  control.maximum, control.step };
        for (int which = 0; which < 4; ++which) {
            auto *box = new QDoubleSpinBox(card);
            box->setDecimals(control.decimals);
            box->setRange(which == 3 ? 0.0001 : -1e7, 1e7);
            box->setValue(values[which]);
            box->setKeyboardTracking(false);
            connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, row, which](double v) {
                if (m_building || !m_scene || row >= m_scene->controls().size())
                    return;
                SceneControl changed = m_scene->controls().at(row);
                if (which == 0)
                    changed.initial = v;
                else if (which == 1)
                    changed.minimum = v;
                else if (which == 2)
                    changed.maximum = v;
                else
                    changed.step = v;
                // Ends the wrong way round are not a narrow slider but a
                // contradiction -- the rule the joint limits follow.
                if (changed.maximum < changed.minimum) {
                    if (which == 1)
                        changed.maximum = changed.minimum;
                    else if (which == 2)
                        changed.minimum = changed.maximum;
                }
                changed.initial = qBound(changed.minimum, changed.startingValue().toDouble(),
                                         changed.maximum);
                commit(row, changed);
            });
            form->addRow(tr(labels[which]), box);
        }
    }

    return card;
}

void ControlsPanel::commit(int row, const SceneControl &control)
{
    if (!m_scene || row < 0 || row >= m_scene->controls().size())
        return;
    QVector<SceneControl> controls = m_scene->controls();
    controls[row] = control;
    m_scene->setControls(controls);
    m_scene->notifyEdit(tr("Change %1").arg(control.id));
}

void ControlsPanel::addControl()
{
    if (!m_scene)
        return;
    SceneControl control;
    control.id = m_scene->uniqueControlId(QStringLiteral("control"));
    control.kind = SceneControl::Kind::Checkbox;
    control.initial = false;

    QVector<SceneControl> controls = m_scene->controls();
    controls.append(control);
    m_scene->setControls(controls);
    m_scene->notifyEdit(tr("Add %1").arg(control.id));
}

void ControlsPanel::removeControl(int row)
{
    if (!m_scene || row < 0 || row >= m_scene->controls().size())
        return;
    QVector<SceneControl> controls = m_scene->controls();
    const QString name = controls.at(row).id;
    controls.remove(row);
    m_scene->setControls(controls);
    m_scene->notifyEdit(tr("Remove %1").arg(name));
}
