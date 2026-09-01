#pragma once

#include "PropertyPane.h"

#include <QPair>
#include <QRectF>
#include <QVector>
#include "../ShapeItem.h"

#include <QPointer>

class ShapePropertyPaneBase : public PropertyPane
{
public:
    explicit ShapePropertyPaneBase(QObject *parent = nullptr) : PropertyPane(parent) {}

    std::vector<PropertyRow> rows(EditorMode mode) const override;
    std::vector<PropertyRow> defaultRows(EditorMode mode) const override;
    void attach(QObject *target) override;

private:
    std::vector<PropertyRow> geometryRows(ShapeItem *item) const;

protected:
    virtual std::vector<PropertyRow> extraRows(ShapeItem *item) const { Q_UNUSED(item); return {}; }

    virtual std::vector<PropertyRow> sizeRows(ShapeItem *item, const QString &section) const;
    virtual QVector<QPair<QString, QVariant>> sizeDefaults(const QRectF &created) const;

    virtual std::vector<PropertyRow> extraDefaultRows(ShapeItem *item) const { Q_UNUSED(item); return {}; }

    QPointer<ShapeItem> m_item;

private:
    void onItemPropertyChanged();

    bool m_lastOriginRowsVisible = false;
};
