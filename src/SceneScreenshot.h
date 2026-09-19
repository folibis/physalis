#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>

class CanvasScene;

// A picture of the scene as it is drawn right now -- background, grid, shapes,
// joints, selection and all -- cut to what the scene holds rather than the
// whole field.
namespace SceneScreenshot {

// Everything the scene holds, with a margin round it; the field itself when
// there is nothing in it.
QRectF contentArea(const CanvasScene *scene);

// Writes the picture to `path`, in the format its extension names: any image
// format Qt can write (drawn at twice the scene's size, so it stays sharp),
// or .svg or .pdf, which keep it as lines. False, with the reason in `error`,
// when the file cannot be written.
bool save(CanvasScene *scene, const QString &path, QString *error);

// "PNG image (*.png)" and the rest, first the one most people want.
QStringList fileFilters();

} // namespace SceneScreenshot
