// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>

// Where the program's own read-only files live -- the example scenes, the
// documentation. Windows keeps them beside the executable, and so does a build
// tree; a Linux package puts the program under <libdir>/physalis and its data
// under <datadir>/physalis, as a package is expected to. Asking here rather
// than hard-coding one of them is what lets both work from the same code.
inline QString assetFolder(const QString &name)
{
    const QDir beside(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        beside.absoluteFilePath(name),
        // <prefix>/lib/physalis/Physalis -> <prefix>/share/physalis/<name>
        beside.absoluteFilePath(QStringLiteral("../../share/physalis/") + name),
        // <prefix>/bin/physalis -> <prefix>/share/physalis/<name>
        beside.absoluteFilePath(QStringLiteral("../share/physalis/") + name),
        // A build tree: the program sits in build/, the folders beside the
        // sources one level up. Found rather than copied, so the scene a menu
        // opens is the one in the repository and editing it edits that.
        beside.absoluteFilePath(QStringLiteral("../") + name),
        beside.absoluteFilePath(QStringLiteral("../../") + name),
    };
    for (const QString &path : candidates) {
        const QDir folder(path);
        if (folder.exists())
            return folder.absolutePath();
    }
    return QString();
}
