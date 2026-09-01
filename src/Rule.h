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
