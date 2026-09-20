// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

struct Rule {
    // What the card is called. Empty means the panel numbers it instead.
    QString name;

    // --- the condition ----------------------------------------------------
    QString subjectName;    // the object watched
    QString conditionKey;   // a readable property of it; ignored for touching

    enum class Compare {
        Equal, NotEqual, Greater, Less, GreaterEqual, LessEqual,
        // Rounded to a whole number, a non-zero multiple of the value: with 5,
        // true at 5, 10, 15... -- so a rule on the frame fires every fifth one.
        Multiple,
    };
    Compare compare = Compare::Greater;
    QVariant conditionValue;

    QString eventId;

    // --- the action -------------------------------------------------------
    QString targetName;
    QString propertyKey;

    // When set, the rule performs this engine action on targetName instead of
    // writing propertyKey. The parameters belong to the action.
    QString actionId;
    QVariantMap actionParams;
    bool isAction() const { return !actionId.isEmpty(); }

    // Where the value comes from. With sourceObject empty the rule uses the
    // literal `value` below; otherwise it reads that object's property when it
    // fires and adds sourceOffset. That is how one object follows another.
    QString sourceObject;
    QString sourceProperty;
    qreal sourceOffset = 0.0;
    bool usesSource() const { return !sourceObject.isEmpty() && !sourceProperty.isEmpty(); }

    enum class Op { Set, Toggle, Negate, Add };
    Op op = Op::Set;
    QVariant value;

    bool enabled = true;
    bool once = false;

    static QString otherObject() { return QStringLiteral("@other"); }

    static QString otherObjectBody() { return QStringLiteral("@otherBody"); }

    // The run itself, as something a rule can watch: how long it has been
    // going and how many steps it has taken.
    static QString world() { return QStringLiteral("@world"); }

    // Two actions the *application* performs, not the engine: no physics
    // engine knows a run is being watched, let alone how to end one. They are
    // offered on the world because that is what a rule names when it means the
    // simulation itself. Ending it puts the scene back where it started, the
    // way the Stop button does; holding it leaves everything where it stands,
    // and Step still goes on from there.
    static QString stopRunAction() { return QStringLiteral("@stopRun"); }

    // Raised on a body, by the application, when a rule is about to remove it.
    // A rule answering it is carried out instead of the removal, and the body
    // stays in the run; with no answer, it is removed. The other object is the
    // one whose rule did the removing.
    static QString aboutToBeRemovedEvent() { return QStringLiteral("@aboutToBeRemoved"); }

    // Raised on the world, by the application, once as a run starts and before
    // its first step: where a rule sets things up.
    static QString runStartedEvent() { return QStringLiteral("@runStarted"); }

    // An action on a body, the application's own: put it back where it stood
    // when the run started, facing the same way, and stop it moving.
    static QString initStateAction() { return QStringLiteral("@initState"); }
    static QString holdRunAction() { return QStringLiteral("@holdRun"); }

    // An action on a body, the application's own: a new body just like it --
    // every body and shape property -- placed at the X and Y in actionParams.
    // It is cloned as the run found it, so a body already removed can still be
    // cloned. The clone lasts as long as the run does.
    static QString cloneAction() { return QStringLiteral("@clone"); }
    static QString cloneXParam() { return QStringLiteral("x"); }
    static QString cloneYParam() { return QStringLiteral("y"); }
    bool isRunAction() const
    {
        return actionId == stopRunAction() || actionId == holdRunAction();
    }

    bool isEvent() const { return !eventId.isEmpty(); }

    static bool usesValue(Op op) { return op == Op::Set || op == Op::Add; }

    static QString compareName(Compare compare)
    {
        switch (compare) {
        case Compare::Equal:           return QStringLiteral("=");
        case Compare::NotEqual:        return QStringLiteral("!=");
        case Compare::Greater:         return QStringLiteral(">");
        case Compare::Less:            return QStringLiteral("<");
        case Compare::GreaterEqual:    return QStringLiteral(">=");
        case Compare::LessEqual:       return QStringLiteral("<=");
        case Compare::Multiple:        return QStringLiteral("%");
        }
        return QStringLiteral(">");
    }

    static Compare compareFromName(const QString &name)
    {
        if (name == QLatin1String("="))                return Compare::Equal;
        if (name == QLatin1String("!="))               return Compare::NotEqual;
        if (name == QLatin1String("<"))                return Compare::Less;
        if (name == QLatin1String(">="))               return Compare::GreaterEqual;
        if (name == QLatin1String("<="))               return Compare::LessEqual;
        if (name == QLatin1String("%"))                return Compare::Multiple;
        return Compare::Greater;
    }

    static QString opName(Op op)
    {
        switch (op) {
        case Op::Set:    return QStringLiteral("set");
        case Op::Toggle: return QStringLiteral("toggle");
        case Op::Negate: return QStringLiteral("negate");
        case Op::Add:    return QStringLiteral("add");
        }
        return QStringLiteral("set");
    }

    static Op opFromName(const QString &name)
    {
        if (name == QLatin1String("toggle")) return Op::Toggle;
        if (name == QLatin1String("negate")) return Op::Negate;
        if (name == QLatin1String("add"))    return Op::Add;
        return Op::Set;
    }

    bool isValid() const
    {
        if (subjectName.isEmpty() || targetName.isEmpty())
            return false;
        if (isAction())
            return isEvent() || !conditionKey.isEmpty();
        if (propertyKey.isEmpty())
            return false;
        // Set and Add need something to set -- either a typed value or an
        // object to read one from. Without either the action still runs and
        // writes a null, which reads as false or as zero, so a rule left
        // half-filled silently switches things off.
        if (usesValue(op) && !value.isValid() && !usesSource())
            return false;
        // A value comparison needs something to read; an event does not.
        return isEvent() || !conditionKey.isEmpty();
    }
};
