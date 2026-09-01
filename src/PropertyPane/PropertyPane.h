#pragma once

#include <QObject>
#include <QString>
#include "../EditorMode.h"
#include <QStringList>
#include <QVariant>
#include <functional>
#include <vector>

enum class PropertyFieldType { Numeric, Boolean, String, Color, Slider, Choice };

struct PropertyRow {
    QString label;
    PropertyFieldType type = PropertyFieldType::Numeric;
    std::function<QVariant()> getter;
    std::function<void(const QVariant &)> setter;
    double minValue = -100000.0;
    double maxValue = 100000.0;
    QStringList choices;
    int decimals = -1;
    double step = 0.0;
    QString section = QStringLiteral("Common");
    QString group;
    QVariant defaultValue;
    // The engine's own name for this property, where it has one. Empty for
    // rows the engine knows nothing about, such as a shape's border width.
    // What can be logged is what carries one: the log reads values back from
    // the running world, and this is the only handle it has on them.
    QString key;
    // Shown on hover, over both the name and the editor. Worth filling in for
    // anything whose label alone does not say what it does.
    QString tooltip;

    // Shown but not editable: a value the canvas or the running engine owns.
    // Without this the editor still accepts typing and silently discards it.
    bool readOnly = false;
};

class PropertyPane : public QObject
{
    Q_OBJECT

public:
    explicit PropertyPane(QObject *parent = nullptr) : QObject(parent) {}
    ~PropertyPane() override = default;

    virtual std::vector<PropertyRow> rows(EditorMode mode) const = 0;

    virtual std::vector<PropertyRow> defaultRows(EditorMode mode) const
    {
        Q_UNUSED(mode);
        return {};
    }

    virtual void attach(QObject *target) = 0;

signals:
    void valueChanged();
    void rowsChanged();
};
