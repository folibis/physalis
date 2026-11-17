// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

// The clipboard belongs to the machine, not to this program, and on Windows
// only one process may hold it open at a time: while something else has it --
// a clipboard manager, another Qt program, a remote-desktop session -- every
// setText is dropped and read back empty, and reports success either way.
// That is normally over in milliseconds, so this keeps asking for a couple of
// seconds rather than standing aside on the first refusal; a blip used to skip
// the copy and paste tests, which is a hole where a passing test should be.

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QString>
#include <QThread>

inline bool theClipboardAnswers()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    const QString probe = QStringLiteral("physalis-clipboard-probe");
    QElapsedTimer waited;
    waited.start();
    for (;;) {
        clipboard->setText(probe);
        if (clipboard->text() == probe)
            return true;
        if (waited.elapsed() > 2000)
            return false;
        QCoreApplication::processEvents();
        QThread::msleep(50);
    }
}

// A clipboard grabbed by another process between the probe and the copy drops
// what Copy wrote, so the check belongs where the copy is and not only at the
// top of the test. Re-probing is only safe once the clipboard has already come
// back empty, since the probe overwrites whatever Copy managed to put there.
// Red then means Copy is broken; yellow means the machine took the clipboard
// away mid-test. A macro because GTEST_SKIP leaves the function it is written
// in, so a helper could not skip its caller.
#define SKIP_IF_THE_CLIPBOARD_STOPPED_ANSWERING()                                            do {                                                                                         if (QGuiApplication::clipboard()->text().isEmpty() && !theClipboardAnswers())                GTEST_SKIP() << "the system clipboard stopped answering mid-test";                } while (false)
