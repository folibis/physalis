// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QString>
#include <Qt>
#include <QSet>
#include <QPair>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <functional>

// How a reading is measured against the value written beside it.
enum class RuleCompare {
    Equal, NotEqual, Greater, Less, GreaterEqual, LessEqual,
    // Rounded to a whole number, a non-zero multiple of the value: with 5,
    // true at 5, 10, 15... -- so a rule on the frame fires every fifth one.
    Multiple,
    // The two that look at the step before as well: true only on the step the
    // reading became the value, or stopped being it. Both need the reading to
    // have actually moved, so a value sitting on the target goes on being
    // ignored. They compare exactly, which makes them worth using on things
    // that step between settled values -- awake, sleeping, a body type, a
    // count of what is in a sensor -- rather than on a position, which can
    // pass through a number without ever landing on it.
    ChangedTo,
    ChangedFrom,
};

// What an action does to the property it names. Add and Subtract move the
// value the property already has, rather than replacing it -- a score counted
// up, a life taken away, a motor wound on a notch at a time.
// The last four belong to a timer variable and carry no value: performing one
// is the whole effect, the way an action is. Start counts on from where the
// timer stands, Pause leaves it there, Stop puts it back to what the scene
// declared, and Reset does that without stopping it -- which is how a timer
// that measures the gap between two events is wound back on each one.
enum class RuleOp {
    Set, Toggle, Negate, Add, Subtract,
    TimerStart, TimerPause, TimerStop, TimerReset,
};

// A number a rule uses -- the value it writes, or one of an action's parameters.
// It is either the number itself, or a range to take a fresh one from every time
// the rule fires: a map of `from`, `to` and `step`. A step of zero means
// anywhere between the two; anything else means whole steps of it, so 1 rolls
// whole numbers and 10 rolls tens.
//
// A map rather than a second field beside every number, because there are a
// great many numbers -- a value, and every parameter of every action -- and
// they all have to work the same way or the reader has to remember which ones
// can be random.
namespace RuleNumber {

inline bool isRange(const QVariant &value)
{
    return value.userType() == QMetaType::QVariantMap
           && value.toMap().contains(QStringLiteral("from"));
}

inline qreal from(const QVariant &value) { return value.toMap().value(QStringLiteral("from")).toDouble(); }
inline qreal to(const QVariant &value) { return value.toMap().value(QStringLiteral("to")).toDouble(); }
inline qreal step(const QVariant &value) { return value.toMap().value(QStringLiteral("step")).toDouble(); }

inline QVariant range(qreal from, qreal to, qreal step)
{
    QVariantMap made;
    made.insert(QStringLiteral("from"), from);
    made.insert(QStringLiteral("to"), to);
    if (step > 0.0)
        made.insert(QStringLiteral("step"), step);
    return made;
}

// What to show where one number can be shown: the near end of a range, or the
// value itself.
inline qreal shown(const QVariant &value)
{
    return isRange(value) ? from(value) : value.toDouble();
}

} // namespace RuleNumber

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

    // True for the two comparisons that need the step before to answer.
    bool watchesChange() const
    {
        return !isEvent()
               && (compare == RuleCompare::ChangedTo || compare == RuleCompare::ChangedFrom);
    }
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
    // Either the number itself, or a range to take a fresh one from -- see
    // RuleNumber. The same is true of every entry in actionParams, so a value
    // and a parameter are written and rolled the one way.
    QVariant value;
    bool usesRange() const { return !usesSource() && RuleNumber::isRange(value); }
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

    // The scene's own variables, addressed as properties of one object the way
    // time and the frame count are properties of the world -- so a scene with
    // twenty of them still puts one entry in the object lists.
    static QString variables() { return QStringLiteral("@variables"); }

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

    // The keyboard, as something a rule can watch. No engine knows a key has
    // been pressed, so this is the application's own, and it is one object
    // carrying every key the way the variables are one object carrying every
    // variable. Which key is the "other object" the event happened with.
    static QString keyboard() { return QStringLiteral("@keyboard"); }

    // The scene's own controls -- a box to tick, a button, a slider -- as one
    // object carrying all of them, the way the variables are one object. Each
    // control contributes two properties, "<name>.value" and "<name>.enabled",
    // and the three events below name which control they happened to.
    static QString controls() { return QStringLiteral("@controls"); }
    static QString controlChangedEvent() { return QStringLiteral("@controlChanged"); }
    static QString controlPressedEvent() { return QStringLiteral("@controlPressed"); }
    static QString controlReleasedEvent() { return QStringLiteral("@controlReleased"); }
    static QString keyPressedEvent() { return QStringLiteral("@keyPressed"); }
    static QString keyReleasedEvent() { return QStringLiteral("@keyReleased"); }
    // Raised on every step the key is down, where a press and a release happen
    // once each. A thruster burns for as long as the key is held, and holding
    // a key is not a thing that happens once.
    static QString keyHeldEvent() { return QStringLiteral("@keyHeld"); }

    // What a rule may name. Plain keys only: a rule asking for Ctrl+S would be
    // asking for Save. Stored by name rather than by Qt's number so a file
    // written today still reads in a build with a different Qt.
    static QVector<QPair<QString, int>> offerableKeys()
    {
        QVector<QPair<QString, int>> keys;
        for (int c = Qt::Key_A; c <= Qt::Key_Z; ++c)
            keys.append({ QString(QChar(c)), c });
        for (int d = Qt::Key_0; d <= Qt::Key_9; ++d)
            keys.append({ QString(QChar(d)), d });
        keys.append({ QStringLiteral("Left"), Qt::Key_Left });
        keys.append({ QStringLiteral("Right"), Qt::Key_Right });
        keys.append({ QStringLiteral("Up"), Qt::Key_Up });
        keys.append({ QStringLiteral("Down"), Qt::Key_Down });
        keys.append({ QStringLiteral("Space"), Qt::Key_Space });
        keys.append({ QStringLiteral("Enter"), Qt::Key_Return });
        keys.append({ QStringLiteral("Tab"), Qt::Key_Tab });
        keys.append({ QStringLiteral("Backspace"), Qt::Key_Backspace });
        keys.append({ QStringLiteral("Delete"), Qt::Key_Delete });

        // Every one of them. The run used to keep Space for holding itself and
        // Right for stepping, which took them away from every scene that
        // wanted them -- and a scene that does want them can say so: holding
        // and ending a run are rule actions like any other. The buttons are
        // still there for anyone who has not bound a key.
        return keys;
    }

    static int keyFromName(const QString &name)
    {
        for (const auto &key : offerableKeys()) {
            if (key.first.compare(name, Qt::CaseInsensitive) == 0)
                return key.second;
        }
        return 0;
    }

    // A key with its modifiers, written the way a menu writes one. Stored and
    // compared as this text, so a rule asking for Ctrl+A is not fired by a
    // bare A -- and a scene written today still reads in a build with a
    // different Qt, which a raw number would not survive.
    static QString chordName(int key, Qt::KeyboardModifiers held)
    {
        const QString base = nameOfKey(key);
        if (base.isEmpty())
            return QString();
        QString chord;
        if (held & Qt::ControlModifier)
            chord += QStringLiteral("Ctrl+");
        if (held & Qt::ShiftModifier)
            chord += QStringLiteral("Shift+");
        if (held & Qt::AltModifier)
            chord += QStringLiteral("Alt+");
        return chord + base;
    }

    // The key out of such a chord, and the modifiers out of it.
    static QString keyOfChord(const QString &chord)
    {
        const int last = chord.lastIndexOf(QLatin1Char('+'));
        return last < 0 ? chord : chord.mid(last + 1);
    }

    static Qt::KeyboardModifiers modifiersOfChord(const QString &chord)
    {
        Qt::KeyboardModifiers held;
        if (chord.contains(QLatin1String("Ctrl+")))
            held |= Qt::ControlModifier;
        if (chord.contains(QLatin1String("Shift+")))
            held |= Qt::ShiftModifier;
        if (chord.contains(QLatin1String("Alt+")))
            held |= Qt::AltModifier;
        return held;
    }

    static QString nameOfKey(int key)
    {
        // The keypad has an Enter of its own, and nobody pressing it means
        // anything different by it.
        if (key == Qt::Key_Enter)
            key = Qt::Key_Return;
        for (const auto &known : offerableKeys()) {
            if (known.second == key)
                return known.first;
        }
        return QString();
    }

    // An action on a body, the application's own: put it back where it stood
    // when the run started, facing the same way, and stop it moving.
    // An action on the world, the application's own: put some words on the
    // canvas, under the log. No engine has anything to say about text. The
    // caption lasts as long as the run does, like every other thing a rule
    // does to a scene.
    static QString showCaptionAction() { return QStringLiteral("@showCaption"); }
    static QString captionTextParam() { return QStringLiteral("text"); }
    static QString captionShownParam() { return QStringLiteral("shown"); }

    static QString initStateAction() { return QStringLiteral("@initState"); }

    // An action on a body, the application's own: the view follows this one
    // from now on. Where a camera points is nothing an engine has an opinion
    // about. Its one parameter turns following off again, the way the
    // caption's "shown" takes a caption down -- and like the caption, it is
    // the run's: stopping puts back whatever the scene itself says.
    static QString keepInViewAction() { return QStringLiteral("@keepInView"); }
    static QString keepInViewParam() { return QStringLiteral("follow"); }
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

    static bool usesValue(Op op)
    {
        return op == Op::Set || op == Op::Add || op == Op::Subtract;
    }

    // An op only a timer has. The card offers these in place of Toggle and
    // Negate once the property it writes is one, and shows no value beside
    // them.
    static bool isTimerVerb(Op op)
    {
        return op == Op::TimerStart || op == Op::TimerPause
               || op == Op::TimerStop || op == Op::TimerReset;
    }

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
        case Compare::ChangedTo:       return QStringLiteral("->");
        case Compare::ChangedFrom:     return QStringLiteral("<-");
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
        if (name == QLatin1String("->"))               return Compare::ChangedTo;
        if (name == QLatin1String("<-"))               return Compare::ChangedFrom;
        return Compare::Greater;
    }

    static QString opName(Op op)
    {
        switch (op) {
        case Op::Set:    return QStringLiteral("set");
        case Op::Toggle: return QStringLiteral("toggle");
        case Op::Negate: return QStringLiteral("negate");
        case Op::Add:    return QStringLiteral("add");
        case Op::Subtract: return QStringLiteral("subtract");
        case Op::TimerStart: return QStringLiteral("timerStart");
        case Op::TimerPause: return QStringLiteral("timerPause");
        case Op::TimerStop:  return QStringLiteral("timerStop");
        case Op::TimerReset: return QStringLiteral("timerReset");
        }
        return QStringLiteral("set");
    }

    static Op opFromName(const QString &name)
    {
        if (name == QLatin1String("toggle")) return Op::Toggle;
        if (name == QLatin1String("negate")) return Op::Negate;
        if (name == QLatin1String("add"))    return Op::Add;
        if (name == QLatin1String("subtract")) return Op::Subtract;
        if (name == QLatin1String("timerStart")) return Op::TimerStart;
        if (name == QLatin1String("timerPause")) return Op::TimerPause;
        if (name == QLatin1String("timerStop"))  return Op::TimerStop;
        if (name == QLatin1String("timerReset")) return Op::TimerReset;
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
        // A property the engine no longer offers. A scene outlives the engine it
        // was drawn for: upgrade the plugin and a rule can go on naming something
        // that has gone, looking finished and doing nothing at all.
        UnknownProperty,
    };

    // Whether an object still has a property by that name. The rule cannot
    // answer it -- only the engine's catalogue knows -- so it is handed in.
    using KnownProperty = std::function<bool(const QString &object, const QString &key)>;

    static Problem conditionProblem(const RuleCondition &condition,
                                    const KnownProperty &known = {})
    {
        if (condition.subjectName.isEmpty())
            return Problem::NoSubject;
        // A value comparison needs something to read; an event does not.
        if (!condition.isEvent() && condition.conditionKey.isEmpty())
            return Problem::NoCondition;
        if (!condition.isEvent() && known
            && !known(condition.subjectName, condition.conditionKey)) {
            return Problem::UnknownProperty;
        }
        return Problem::None;
    }

    static Problem actionProblem(const RuleAction &action, const KnownProperty &known = {})
    {
        if (action.targetName.isEmpty())
            return Problem::NoTarget;
        if (!action.isAction()) {
            if (action.propertyKey.isEmpty())
                return Problem::NoProperty;
            if (known && !known(action.targetName, action.propertyKey))
                return Problem::UnknownProperty;
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
    Problem problem(const KnownProperty &known = {}) const
    {
        if (conditions.isEmpty())
            return Problem::NoConditions;
        for (const RuleCondition &condition : conditions) {
            const Problem found = conditionProblem(condition, known);
            if (found != Problem::None)
                return found;
        }
        if (actions.isEmpty())
            return Problem::NoActions;
        for (const RuleAction &action : actions) {
            const Problem found = actionProblem(action, known);
            if (found != Problem::None)
                return found;
        }
        return Problem::None;
    }

    bool isValid(const KnownProperty &known = {}) const
    {
        return problem(known) == Problem::None;
    }
};
