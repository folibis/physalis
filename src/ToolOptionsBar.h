// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QHBoxLayout;
class QLabel;

// One number a tool asks for while it is being set up.
struct ToolOption {
    QString key;
    QString label;
    double value = 0.0;
    double minimum = 0.0;
    double maximum = 1000.0;
    double step = 1.0;
    // Zero makes it a whole number, which is what a count of rows is.
    int decimals = 1;
    QString suffix;
    // Given, the option is one of these rather than a number, and its value is
    // the index of the one picked.
    QStringList choices;
};

// The panel in the corner of the canvas, there while a tool is being set up
// and carrying the numbers that tool was guessing at. Every change re-runs the
// tool, so the canvas shows the answer rather than describing it; Apply keeps
// what is on screen and Cancel puts back what was there before.
class ToolOptionsBar : public QWidget
{
    Q_OBJECT

public:
    explicit ToolOptionsBar(QWidget *parent = nullptr);

    void begin(const QString &title, const QVector<ToolOption> &options);
    double value(const QString &key) const;

signals:
    void valueChanged();
    void applied();
    void cancelled();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    // A plain QWidget draws nothing of its own stylesheet, so the background
    // and border below would be there and invisible without this.
    void paintEvent(QPaintEvent *event) override;

private:
    void clearOptions();

    QLabel *m_title = nullptr;
    // One row per option, rather than one long line: four of them side by side
    // ran most of the way across the canvas.
    QFormLayout *m_form = nullptr;
    QHBoxLayout *m_buttons = nullptr;
    QHash<QString, QDoubleSpinBox *> m_boxes;
    QHash<QString, QComboBox *> m_picks;
};
