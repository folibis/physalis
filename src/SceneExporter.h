#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

class CanvasScene;

// Turning a scene into somebody else's format.
//
// The application does none of the converting and knows none of the formats.
// It finds converters, lists them, and when one is chosen it hands over the
// whole scene and gets files back. Everything between those two points is
// JavaScript living in the converter's own folder.
//
// A converter is a folder holding:
//   manifest.json   name and description, so the menu can be built without
//                   running anybody's code
//   export.js       exportScene(scene, io) -- the whole of the conversion
//   anything else   templates, README, whatever the converter needs; it can
//                   read them back through io.read()
namespace SceneExporter {

// One setting a converter asks for. The application renders it, stores it and
// hands the value back -- it has no idea what any of them mean, the same way
// it has no idea what a joint parameter means.
struct ConverterSetting {
    QString key;            // how the script asks for it
    QString label;          // what the options page shows
    QString tooltip;
    // "string", "int", "double", "bool", "color" or "choice". Anything else is
    // treated as a string, so a converter asking for something this version
    // has never heard of still gets a usable box.
    QString type = QStringLiteral("string");
    QVariant defaultValue;
    double minValue = 0.0;
    double maxValue = 0.0;  // both zero means "no range given"
    int decimals = 2;
    QStringList choices;    // "choice" only
};

struct Converter {
    QString id;           // the folder's name: stable, and what settings are filed under
    QString name;         // what the menu shows
    QString description;  // the tooltip
    QString folder;       // where it lives, and what io.read() is relative to
    QVector<ConverterSetting> settings;

    // The declared settings with nothing chosen yet.
    QVariantMap defaults() const
    {
        QVariantMap values;
        for (const ConverterSetting &setting : settings)
            values.insert(setting.key, setting.defaultValue);
        return values;
    }
};

// Every converter under `root`, in the order the menu should list them.
// Folders without a readable manifest.json and an export.js are not
// converters and are passed over in silence -- a README sitting beside them
// is not an error.
QVector<Converter> discover(const QString &root);

// Runs one, writing whatever it produces into `outputFolder`. `settings` is
// the application's own preferences, grouped as they are stored -- a converter
// needs them for the things the scene does not carry, such as which colour a
// dynamic body is drawn in. Returns false and fills `error` if the script
// throws, if it is missing exportScene, or if it tries to write outside the
// folder it was given.
// `written` comes back with the files it produced, `log` with whatever the
// script said through io.log() -- both are worth showing, since only the
// converter knows what it did.
bool run(const Converter &converter, const CanvasScene *scene,
         const QString &outputFolder, const QJsonObject &settings,
         QString *error, QStringList *written = nullptr,
         QStringList *log = nullptr);

} // namespace SceneExporter
