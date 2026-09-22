// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#include "CanvasScene.h"
#include "FullScreenView.h"

#include <QGraphicsView>
#include <gtest/gtest.h>

// The second view onto a running scene. It shows the same scene as the main
// one, so it has to repaint the same way: a run's joints, body axes, rays and
// explosions are painted in the scene's foreground, which belongs to no item,
// so nothing marks the ground under them as dirty when a body moves away. The
// main view says FullViewportUpdate in its form; this one said nothing, and
// the joints it had drawn stayed on screen after the bodies had left them.
TEST(FullScreenView, RepaintsTheWholeViewportLikeTheMainOne)
{
    CanvasScene scene;
    FullScreenView view(&scene, nullptr);
    EXPECT_EQ(view.viewportUpdateMode(), QGraphicsView::FullViewportUpdate)
        << "the full-screen view repaints only what items report as dirty, and the run"
           " is painted in the foreground where no item reports anything";
}

// It is a window onto the scene and nothing more: a click during a run must
// not pick anything up or select it.
TEST(FullScreenView, IsNotInteractive)
{
    CanvasScene scene;
    FullScreenView view(&scene, nullptr);
    EXPECT_FALSE(view.isInteractive())
        << "a click in the full-screen view would otherwise pick a shape up mid-run";
    EXPECT_EQ(view.dragMode(), QGraphicsView::ScrollHandDrag) << "dragging pans instead";
}
