#pragma once

#include <QSet>
#include <QString>

// Every shape, body and joint carries a name.
//
// Names started as labels for the property panel, but rules address objects by
// name -- "when ball begins contact with jumper" is stored exactly like that
// -- so a duplicate is no longer cosmetic: it makes a rule ambiguous, silently
// acting on whichever object happens to come first.
//
// All three kinds share one namespace, because a rule can name any of them in
// the same field.
namespace Naming {

// The number a name ends with, or 0 for one that doesn't end in "_<n>".
inline int trailingNumber(const QString &name, const QString &prefix)
{
    if (!name.startsWith(prefix + QLatin1Char('_')))
        return 0;
    bool numeric = false;
    const int value = name.mid(prefix.size() + 1).toInt(&numeric);
    return numeric ? value : 0;
}

inline QString nextName(const QString &prefix, const QSet<QString> &taken = {})
{
    int highest = 0;
    for (const QString &name : taken)
        highest = qMax(highest, trailingNumber(name, prefix));
    return QStringLiteral("%1_%2").arg(prefix).arg(highest + 1);
}

inline QString makeUnique(const QString &desired, const QSet<QString> &taken)
{
    if (desired.isEmpty() || !taken.contains(desired))
        return desired;

    QString stem = desired;
    const int underscore = desired.lastIndexOf(QLatin1Char('_'));
    if (underscore > 0) {
        bool numeric = false;
        desired.mid(underscore + 1).toInt(&numeric);
        if (numeric)
            stem = desired.left(underscore);
    }
    return nextName(stem, taken);
}

} // namespace Naming
