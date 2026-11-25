// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "ValueText.h"

#include <QRegularExpression>

namespace ValueText {

namespace {

// A brace and then no space: the editor's own markup carries a style sheet,
// "p, li { white-space: pre-wrap; }", which is not a field.
const QRegularExpression &pattern()
{
    static const QRegularExpression braces(QStringLiteral(R"(\{([^{}<>\s][^{}<>]*)\})"));
    return braces;
}

QString unescaped(QString text)
{
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return text;
}

} // namespace

QString number(double value)
{
    if (value == 0.0 || qAbs(value) >= 1.0)
        return QString::number(value, 'f', 2);
    return QString::number(value, 'g', 3);
}

QString of(const QVariant &value)
{
    switch (value.userType()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QString::number(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return number(value.toDouble());
    default:
        return value.toString();
    }
}

QString field(const Field &field)
{
    if (field.object.isEmpty())
        return QLatin1Char('{') + field.property + QLatin1Char('}');
    return QLatin1Char('{') + field.object + QLatin1Char(':') + field.property + QLatin1Char('}');
}

QString expand(const QString &html, const std::function<QVariant(const Field &)> &read)
{
    QString out;
    qsizetype from = 0;
    auto it = pattern().globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString inside = unescaped(match.captured(1)).trimmed();
        const qsizetype colon = inside.indexOf(QLatin1Char(':'));
        const Field asked = colon < 0 ? Field{ QString(), inside }
                                      : Field{ inside.left(colon).trimmed(),
                                               inside.mid(colon + 1).trimmed() };
        const QVariant value = read(asked);
        out += html.mid(from, match.capturedStart() - from);
        out += value.isValid() ? of(value).toHtmlEscaped() : match.captured(0);
        from = match.capturedEnd();
    }
    out += html.mid(from);
    return out;
}

bool hasFields(const QString &html)
{
    return pattern().match(html).hasMatch();
}

} // namespace ValueText
