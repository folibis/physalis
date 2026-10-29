// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "ToolOptionsBar.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>

ToolOptionsBar::ToolOptionsBar(QWidget *parent)
    : QWidget(parent)
{
    // It floats over the canvas, so it has to carry its own background: a
    // panel with nothing behind it reads as scribble on the scene.
    setObjectName(QStringLiteral("ToolOptionsBar"));
    setStyleSheet(QStringLiteral(
        "#ToolOptionsBar {"
        "  background: #f4f4f4;"
        "  border: 1px solid #a8a8a8;"
        "  border-radius: 6px;"
        "}"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 10, 12, 10);
    outer->setSpacing(8);

    m_title = new QLabel(this);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    outer->addWidget(m_title);

    m_form = new QFormLayout;
    m_form->setContentsMargins(0, 0, 0, 0);
    m_form->setHorizontalSpacing(10);
    m_form->setVerticalSpacing(6);
    m_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    outer->addLayout(m_form);

    m_buttons = new QHBoxLayout;
    m_buttons->setContentsMargins(0, 0, 0, 0);
    outer->addLayout(m_buttons);
}

void ToolOptionsBar::clearOptions()
{
    while (m_form->rowCount() > 0)
        m_form->removeRow(0);
    while (QLayoutItem *item = m_buttons->takeAt(0)) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    m_boxes.clear();
    m_picks.clear();
}

void ToolOptionsBar::begin(const QString &title, const QVector<ToolOption> &options)
{
    clearOptions();
    m_title->setText(title);

    for (const ToolOption &option : options) {
        if (!option.choices.isEmpty()) {
            auto *pick = new QComboBox(this);
            pick->addItems(option.choices);
            pick->setCurrentIndex(qBound(0, int(option.value), option.choices.size() - 1));
            connect(pick, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    &ToolOptionsBar::valueChanged);
            m_form->addRow(option.label, pick);
            m_picks.insert(option.key, pick);
            continue;
        }

        auto *box = new QDoubleSpinBox(this);
        box->setDecimals(option.decimals);
        box->setRange(option.minimum, option.maximum);
        box->setSingleStep(option.step);
        box->setValue(option.value);
        if (!option.suffix.isEmpty())
            box->setSuffix(option.suffix);
        box->setKeyboardTracking(false);   // one re-run per entry, not per digit
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                &ToolOptionsBar::valueChanged);
        m_form->addRow(option.label, box);
        m_boxes.insert(option.key, box);
    }

    auto *apply = new QPushButton(tr("Apply"), this);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    apply->setDefault(true);
    connect(apply, &QPushButton::clicked, this, &ToolOptionsBar::applied);
    connect(cancel, &QPushButton::clicked, this, &ToolOptionsBar::cancelled);
    m_buttons->addStretch(1);
    m_buttons->addWidget(cancel);
    m_buttons->addWidget(apply);

    // So Escape reaches keyPressEvent() without the canvas having to give the
    // focus up first.
    if (!m_boxes.isEmpty())
        m_boxes.constBegin().value()->setFocus();
    else if (!m_picks.isEmpty())
        m_picks.constBegin().value()->setFocus();
    else
        apply->setFocus();
}

double ToolOptionsBar::value(const QString &key) const
{
    const auto box = m_boxes.constFind(key);
    if (box != m_boxes.constEnd())
        return (*box)->value();
    const auto pick = m_picks.constFind(key);
    return pick == m_picks.constEnd() ? 0.0 : (*pick)->currentIndex();
}

void ToolOptionsBar::paintEvent(QPaintEvent *)
{
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void ToolOptionsBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit cancelled();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit applied();
        return;
    }
    QWidget::keyPressEvent(event);
}
