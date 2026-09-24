// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>

// How a reading is measured against the value written beside it.
enum class RuleCompare {
    Equal, NotEqual, Greater, Less, GreaterEqual, LessEqual,
    // Rounded to a whole number, a non-zero multiple of the value: with 5,
    // true at 5, 10, 15... -- so a rule on the frame fires every fifth one.
    Multiple,
};

// What an action does to the property it names.
enum class RuleOp { Set, Toggle, Negate, Add };

// One thing that has to be so. A rule holds a list of them: either an event
// somebody raised this step, or a reading measured against a value.
struct RuleCondition {
    QString subjectName;    // the object watched
    QString conditionKey;   // a readable property of it; ignored for an event

    RuleCompare compare = RuleCompare::Greater;
    // For a reading, what it is measured against. For an event, which other
    // object it has to have happened with -- empty meaning any.
    QVariant conditionValue;

    QString eventId;

    bool isEvent() const { return !eventId.isEmpty(); }
};

// One thing the rule does when it fires. A rule holds a list of them, carried
// out in order, so a single condition can set a property, perform an action and
// stop the run without being written three times over.
struct RuleAction {
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

    RuleOp op = RuleOp::Set;
    QVariant value;
};

struct Rule {
    // The enums are named outside the struct so a condition and an action can
    // carry one, but Rule::Compare and Rule::Op are what the rest of the
    // application spells them as.
    using Compare = RuleCompare;
    using Op = RuleOp;

    // What the card is called. Empty means the panel numbers it instead.
    QString name;

    // How several conditions are read together. One joiner for the whole card
    // rather than brackets: "all of these" and "any of these" cover what a rule
    // is for, and a card that can nest them stops being readable at a glance.
    enum class Join { All, Any };
    Join join = Join::All;

    // At least one of each for the rule to do anything. Both are lists so that
    // the common case -- one condition, one action -- reads exactly as it
    // always did, and the rest is the same card with more rows.
    QVector<RuleCondition> conditions { RuleCondition() };
    QVector<RuleAction> actions { RuleAction() };

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

    static bool isRunAction(const RuleAction &action)
    {
        return action.actionId == stopRunAction() || action.actionId == holdRunAction();
    }

    // True when any of the conditions is an event rather than a reading. An
    // event is momentary -- raised on one step and gone -- which is worth
    // knowing before joining one to anything else with "all of".
    bool isEvent() const
    {
        for (const RuleCondition &condition : conditions) {
            if (condition.isEvent())
                return true;
        }
        return false;
    }

    static bool usesValue(Op op) { return op == Op::Set || op == Op::Add; }

    static QString joinName(Join join)
    {
        return join == Join::Any ? QStringLiteral("any") : QStringLiteral("all");
    }

    static Join joinFromName(const QString &name)
    {
        return name == QLatin1String("any") ? Join::Any : Join::All;
    }

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

    // What a rule is still missing, in the order the card reads: the first
    // blank going down it. A rule is kept and saved whatever this says -- an
    // unfinished one that vanished on save took the work with it -- and the
    // panel marks the card instead. It simply does not run.
    enum class Problem {
        None,
        NoConditions, // every condition row was taken away
        NoSubject,    // nothing being watched
        NoCondition,  // neither an event nor a property to compare
        NoActions,    // every action row was taken away
        NoTarget,     // nothing to act on
        NoProperty,   // nothing on the target to write
        NoValue,      // Set or Add with neither a typed value nor a source
    };

    static Problem conditionProblem(const RuleCondition &condition)
    {
        if (condition.subjectName.isEmpty())
            return Problem::NoSubject;
        // A value comparison needs something to read; an event does not.
        if (!condition.isEvent() && condition.conditionKey.isEmpty())
            return Problem::NoCondition;
        return Problem::None;
    }

    static Problem actionProblem(const RuleAction &action)
    {
        if (action.targetName.isEmpty())
            return Problem::NoTarget;
        if (!action.isAction()) {
            if (action.propertyKey.isEmpty())
                return Problem::NoProperty;
            // Set and Add need something to set -- either a typed value or an
            // object to read one from. Without either the action still runs and
            // writes a null, which reads as false or as zero, so a rule left
            // half-filled silently switches things off.
            if (usesValue(action.op) && !action.value.isValid() && !action.usesSource())
                return Problem::NoValue;
        }
        return Problem::None;
    }

    // The first thing missing anywhere on the card: every condition in order,
    // then every action. The panel marks the row it came from as well, so the
    // card's own mark is the summary rather than the whole story.
    Problem problem() const
    {
        if (conditions.isEmpty())
            return Problem::NoConditions;
        for (const RuleCondition &condition : conditions) {
            const Problem found = conditionProblem(condition);
            if (found != Problem::None)
                return found;
        }
        if (actions.isEmpty())
            return Problem::NoActions;
        for (const RuleAction &action : actions) {
            const Problem found = actionProblem(action);
            if (found != Problem::None)
                return found;
        }
        return Problem::None;
    }

    bool isValid() const { return problem() == Problem::None; }
};
