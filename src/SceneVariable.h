// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include "JointTypes.h"

#include <QObject>
#include <QString>
#include <QVariant>

// A value the scene carries that no engine knows about: a score, a count of
// lives, a flag saying which way a lift is going. Rules read and write them the
// same way they read and write anything else, and the log shows them.
//
// They belong to the scene rather than to a body, so they are addressed as
// properties of one pseudo-object -- Rule::variables() -- the way elapsed time
// and the frame count are properties of the world. That keeps one entry in the
// object lists however many variables a scene has.
//
// A variable is run state: it starts each run at `initial` and goes back there
// when the run stops, like every position on the canvas.
struct SceneVariable {
    enum class Type { Bool, Integer, Double };

    QString name;
    Type type = Type::Double;
    QVariant initial;

    static QString typeName(Type type)
    {
        switch (type) {
        case Type::Bool:    return QStringLiteral("bool");
        case Type::Integer: return QStringLiteral("int");
        case Type::Double:  return QStringLiteral("double");
        }
        return QStringLiteral("double");
    }

    static Type typeFromName(const QString &name)
    {
        if (name == QLatin1String("bool"))
            return Type::Bool;
        if (name == QLatin1String("int"))
            return Type::Integer;
        return Type::Double;
    }

    static QString typeLabel(Type type)
    {
        switch (type) {
        case Type::Bool:    return QObject::tr("Boolean");
        case Type::Integer: return QObject::tr("Integer");
        case Type::Double:  return QObject::tr("Double");
        }
        return QObject::tr("Double");
    }

    static QVector<Type> types() { return { Type::Bool, Type::Integer, Type::Double }; }

    // What an empty one of this type holds, and what a value coerced to it
    // becomes -- so a variable switched from a number to a flag does not keep
    // a double in it.
    static QVariant coerce(Type type, const QVariant &value)
    {
        switch (type) {
        case Type::Bool:    return value.toBool();
        case Type::Integer: return value.toInt();
        case Type::Double:  return value.toDouble();
        }
        return value;
    }

    QVariant value() const { return coerce(type, initial); }

    // The variable as a property, which is how the rule cards and the property
    // tables are built -- they ask what a property is and render it, and have
    // no idea that this one is not an engine's.
    physics::JointParam describe() const
    {
        physics::JointParam p;
        p.key = name;
        p.label = name;
        p.defaultValue = value();
        p.stored = true;
        p.liveReadable = true;
        p.liveSettable = true;
        switch (type) {
        case Type::Bool:
            p.type = physics::ParamType::Bool;
            p.tooltip = QObject::tr("A true/false variable of this scene.");
            break;
        case Type::Integer:
            p.type = physics::ParamType::Integer;
            p.minValue = -1e9;
            p.maxValue = 1e9;
            p.decimals = 0;
            p.step = 1.0;
            p.tooltip = QObject::tr("A whole-number variable of this scene.");
            break;
        case Type::Double:
            p.type = physics::ParamType::Real;
            p.minValue = -1e9;
            p.maxValue = 1e9;
            p.decimals = 3;
            p.step = 0.1;
            p.tooltip = QObject::tr("A number variable of this scene.");
            break;
        }
        return p;
    }
};
