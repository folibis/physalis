#include "CatalogueRows.h"

std::vector<PropertyRow> liveRowsFromCatalogue(const physics::PropertyList &properties,
                                               const QString &fallbackSection)
{
    std::vector<PropertyRow> rows;
    for (const physics::JointParam &property : properties) {
        if (property.stored || !property.liveReadable)
            continue;
        PropertyRow row;
        row.label = property.label;
        row.key = property.key;
        row.tooltip = property.tooltip;
        row.section = property.section.isEmpty() ? fallbackSection : property.section;
        row.type = property.type == physics::ParamType::Bool ? PropertyFieldType::Boolean
                                                             : PropertyFieldType::Numeric;
        row.decimals = property.decimals;
        row.minValue = property.minValue;
        row.maxValue = property.maxValue;
        row.getter = [] { return QVariant(); };   // the engine answers while running
        row.setter = [](const QVariant &) {};
        row.readOnly = true;
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<PropertyRow> rowsFromCatalogue(const physics::PropertyList &properties,
                                           QVariantMap *values,
                                           const std::function<void()> &changed,
                                           const QString &fallbackSection)
{
    std::vector<PropertyRow> rows;
    if (!values)
        return rows;

    for (const physics::JointParam &property : properties) {
        // Only what belongs to the object. The rest of the catalogue is
        // readouts and one-shot pushes, which a run answers and a scene does
        // not carry.
        if (!property.stored)
            continue;

        PropertyRow row;
        row.label = property.label;
        row.key = property.key;
        row.tooltip = property.tooltip;
        row.section = property.section.isEmpty() ? fallbackSection : property.section;
        row.defaultValue = property.defaultValue;
        row.minValue = property.minValue;
        row.maxValue = property.maxValue;
        row.decimals = property.decimals;
        row.step = property.step;
        row.choices = property.choices;

        switch (property.type) {
        case physics::ParamType::Bool:    row.type = PropertyFieldType::Boolean; break;
        case physics::ParamType::Choice:  row.type = PropertyFieldType::Choice;  break;
        case physics::ParamType::Integer: row.type = PropertyFieldType::Numeric;
                                          row.decimals = 0;                      break;
        case physics::ParamType::Real:    row.type = PropertyFieldType::Numeric; break;
        }

        // A value the object never had is whatever the engine said it starts
        // as, so nothing has to be written into every object up front -- and a
        // property an engine drops leaves nothing behind.
        const QString key = property.key;
        const QVariant fallback = property.defaultValue;
        row.getter = [values, key, fallback] { return values->value(key, fallback); };
        row.setter = [values, key, changed](const QVariant &v) {
            values->insert(key, v);
            if (changed)
                changed();
        };
        rows.push_back(std::move(row));
    }
    return rows;
}
