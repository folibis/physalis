// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QComboBox>
#include <QStringList>

// A combo box whose entries are switches rather than choices: what the canvas
// draws, one line each, ticked or not. Picking one toggles it and leaves the
// list open -- these are settings, and setting two of them should not mean
// opening the list twice.
//
// It never has a current entry: the box shows what is on rather than the last
// thing clicked.
class ViewLayersCombo : public QComboBox
{
    Q_OBJECT

public:
    explicit ViewLayersCombo(QWidget *parent = nullptr);

    void addLayer(const QString &key, const QString &label, bool on, const QString &tooltip);

    bool isOn(const QString &key) const;
    // Follows a setting changed elsewhere -- in Options, or on loading a file
    // -- without reporting it back as a change.
    void setOn(const QString &key, bool on);

signals:
    void layerToggled(const QString &key, bool on);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    int rowOf(const QString &key) const;
    void toggleRow(int row);
    void refreshText();
};
