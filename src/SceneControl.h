// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QObject>
#include <QString>
#include <QVariant>

// Something to work the scene with while it runs: a box to tick, a button to
// press, a slider to pull. No engine knows any of this exists -- it is the
// scene's own, the way its variables are, and rules read and write it the same
// way.
//
// An id and a caption, and they are not the same thing: the id is how a rule
// addresses it and never changes under one, the caption is only what it says
// on the canvas. A caption left empty is the id, so a scene that does not care
// sets one field; a rule may change the caption, which is why identity could
// not be the words on a button.
//
// A slider's bounds are how the control is declared -- its type, in effect --
// and belong to the document. Its value is run state: it starts each run at
// `initial` and goes back there when the run stops, like a variable and like
// every position on the canvas.
struct SceneControl {
    enum class Kind { Checkbox, Button, Slider };

    // How a rule addresses it. Unique within the scene.
    QString id;
    // What it says. Empty means its id.
    QString caption;
    Kind kind = Kind::Checkbox;

    // A slider's, and ignored by the rest.
    double minimum = 0.0;
    double maximum = 100.0;
    double step = 1.0;
    int decimals = 1;

    // Where its value starts. A checkbox's is a bool, a slider's a number; a
    // button has none -- what it has is a press.
    QVariant initial;

    static QString valueProperty() { return QStringLiteral("value"); }
    static QString enabledProperty() { return QStringLiteral("enabled"); }
    // What it says on the canvas. Its name by default, and run state from
    // there: a rule may change the words on a button without changing what
    // every other rule calls it.
    static QString captionProperty() { return QStringLiteral("caption"); }

    // How a rule addresses a control's property: one key made of the control's
    // id and the property, so one pseudo-object carries every control the way
    // one carries every variable.
    //
    // The punctuation between them is this struct's business and nobody
    // else's. It is joined by key() and taken apart by parse(), which accepts
    // every separator the program has ever written -- so changing the one it
    // writes stays a change of spelling and cannot turn a saved scene, or a
    // rule somebody has already written, into something that names nothing.
    struct Address {
        QString control;
        QString property;
        bool isValid() const { return !control.isEmpty() && !property.isEmpty(); }
    };

    // The one it writes is first; the rest are only read. A colon rather than
    // the dot it began with: a dot reads as part of a name, and
    // "Start.caption" looked like one word where "Start:caption" reads as a
    // thing and a property of it.
    static QLatin1String separators() { return QLatin1String(":."); }
    static QLatin1Char separator() { return QLatin1Char(separators().at(0)); }

    static QString key(const QString &control, const QString &property)
    {
        return control + separator() + property;
    }

    static Address parse(const QString &key)
    {
        int at = -1;
        for (const QChar one : separators())
            at = qMax(at, key.lastIndexOf(one));
        if (at <= 0 || at + 1 >= key.size())
            return {};
        return { key.left(at), key.mid(at + 1) };
    }

    // The same address, spelled the way it is written now -- and anything that
    // is not one of a control's three properties left exactly as it came, since
    // every other key in a scene passes through here too.
    static QString canonical(const QString &key)
    {
        const Address address = parse(key);
        if (!address.isValid() || (address.property != valueProperty()
                                   && address.property != enabledProperty()
                                   && address.property != captionProperty())) {
            return key;
        }
        return SceneControl::key(address.control, address.property);
    }

    QString valueKey() const { return key(id, valueProperty()); }
    QString enabledKey() const { return key(id, enabledProperty()); }
    QString captionKey() const { return key(id, captionProperty()); }

    static QString kindName(Kind kind)
    {
        switch (kind) {
        case Kind::Checkbox: return QStringLiteral("checkbox");
        case Kind::Button:   return QStringLiteral("button");
        case Kind::Slider:   return QStringLiteral("slider");
        }
        return QStringLiteral("checkbox");
    }

    static Kind kindFromName(const QString &kind)
    {
        if (kind == QLatin1String("button"))
            return Kind::Button;
        if (kind == QLatin1String("slider"))
            return Kind::Slider;
        return Kind::Checkbox;
    }

    static QString kindLabel(Kind kind)
    {
        switch (kind) {
        case Kind::Checkbox: return QObject::tr("Checkbox");
        case Kind::Button:   return QObject::tr("Button");
        case Kind::Slider:   return QObject::tr("Slider");
        }
        return QObject::tr("Checkbox");
    }

    // What its value is before anything has touched it.
    QVariant startingValue() const
    {
        switch (kind) {
        case Kind::Checkbox: return initial.isValid() ? initial.toBool() : false;
        case Kind::Button:   return false;   // not held
        case Kind::Slider:   return initial.isValid() ? initial.toDouble() : minimum;
        }
        return QVariant();
    }
};
