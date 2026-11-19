// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QString>
#include <QVariant>

#include <functional>

// How a value is written for a person to read, and where a caption asks for
// one. The log and a caption say the same number the same way, and the
// grammar of a field -- "{score}", "{ball:angle}" -- is written down here and
// nowhere else, the way a control's address is SceneControl's.
namespace ValueText {

// Two decimals for most things, but a light body's energy or mass is a few
// thousandths, and two decimals made it read as zero.
QString number(double value);

// A whole number as one, a flag as true or false, anything else as number().
QString of(const QVariant &value);

// One field of a caption. A bare name has no object: it is a variable, or
// failing that something the world answers to. Otherwise it is a property of
// a named object -- a body, a shape, a joint, a ray, or a control by its id.
struct Field {
    QString object;
    QString property;
};

QString field(const Field &field);

// The caption with every field it names replaced by what `read` answers. A
// field nothing answers to is left as written, so a misspelt one shows on the
// canvas rather than vanishing. `html` is rich text: names are read with its
// escapes undone, and values go in escaped.
QString expand(const QString &html, const std::function<QVariant(const Field &)> &read);

bool hasFields(const QString &html);

} // namespace ValueText
