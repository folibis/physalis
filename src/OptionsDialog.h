#pragma once

#include <QDialog>

#include <memory>
#include <QColor>
#include <QHash>
#include <QVariantMap>
#include "CanvasScene.h"

class QDoubleSpinBox;
class QSpinBox;
QT_BEGIN_NAMESPACE
namespace Ui { class OptionsDialog; }
QT_END_NAMESPACE

namespace SceneExporter { struct ConverterSetting; }

class QCheckBox;
class QToolButton;
class QComboBox;
class QSlider;
class QDoubleSpinBox;
class QLabel;

class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    struct Settings {
        qreal fieldWidth = 1000.0;
        qreal fieldHeight = 1000.0;
        QColor backgroundColor { Qt::white };
        bool showGrid = true;
        qreal gridCellSize = 20.0;
        QColor gridColor { 230, 230, 230 };
        bool snapToGrid = false;
        SnapPoint snapPoint = SnapPoint::Position;
        qreal snapStep = 20.0;
        qreal snapSensitivity = 5.0;

        qreal currentScale = 100.0;
        qreal scaleMin = 10.0;
        qreal scaleMax = 500.0;
        qreal scaleStep = 10.0;

        QColor defaultBorderColor { 100, 170, 220, 204 };
        qreal defaultBorderWidth = 2.0;
        QColor defaultBodyColor { 173, 216, 230, 128 };

        Qt::PenStyle selectionLineStyle = Qt::DotLine;
        qreal selectionLineWidth = 2.0;
        QColor selectionColor { 230, 140, 40 };
        int undoDepth = 50;

        HandleShape handleShape = HandleShape::Square;
        qreal handleSize = 8.0;
        QColor handleColor { 222, 184, 135 };
        qreal handleBorderWidth = 1.0;
        QColor handleBorderColor { 64, 64, 64 };

        QColor bodyDynamicColor { 0x2E, 0x86, 0xC1 };
        QColor bodyStaticColor { 0x27, 0x9E, 0x6A };
        QColor bodyKinematicColor { 0x88, 0x4E, 0xA0 };
        QColor unassignedShapeColor { 0x8C, 0x8C, 0x8C };
        QColor sensorColor { 0x05, 0xC9, 0x36 };
        // A sensor is an area things pass through, not a solid shape, so it is
        // marked out rather than filled in.
        Qt::BrushStyle sensorPattern = Qt::DiagCrossPattern;
        bool sensorFillsBody = false;
        qreal physicsBorderWidth = 2.0;
        int physicsFillAlpha = 90;
        int jointFillAlpha = 170;
        // Percent: how see-through joint anchors are drawn.
        int jointAnchorOpacity = 60;
        Qt::PenStyle physicsSelectionLineStyle = Qt::DotLine;
        qreal physicsSelectionLineWidth = 2.0;
        QColor physicsSelectionColor { 230, 140, 40 };

        // What a *run* shows, set from the toolbar's view list rather than in
        // this dialog, and kept here so it is saved with everything else. The
        // editor draws all of it whatever these say -- see CanvasScene::RunLayer.
        bool sleepShading = true;
        bool runShowGrid = true;
        bool runShowJoints = true;
        bool runShowBodyAxes = true;
        bool runShowRays = true;
        bool runShowExplosions = true;
        bool showBodyAxes = true;
        qreal bodyAxisLength = 40.0;
        qreal bodyAxisWidth = 2.0;
        QColor bodyAxisXColor { 220, 50, 50 };
        QColor bodyAxisYColor { 40, 160, 60 };
        int sleepShiftPercent = 25;
        // Vertex cap for a solid polygon; Box2D's own limit is 8.
        int maxPolygonVertices = 8;
        int simulationStepsPerSecond = 60;
        // Playback pace, set from the toolbar rather than in here.
        qreal simulationSpeed = 1.0;
        // Whether a run opens on a screen of its own; also the toolbar's.
        bool simulationFullScreen = false;

        // How the slingshot's pull line is drawn, for every body: a colour for
        // a light pull and one for a full one, with the shades between.
        QColor shotLightColor { 255, 204, 0 };
        QColor shotFullColor { 220, 30, 30 };
        qreal shotLineWidth = 3.0;
        Qt::PenStyle shotLineStyle = Qt::DotLine;

        QColor jointColor { 0xE8, 0xC4, 0x6A };
        // Per joint kind -- physics::JointVisual cast to int -- rather than
        // per engine's joint type: the five kinds are the same whichever
        // engine is loaded, so one short list covers all of them.
        QHash<int, QColor> jointKindColors;
        QHash<int, JointStyle> jointKindStyles;
        Qt::PenStyle jointSelectionLineStyle = Qt::DotLine;
        qreal jointSelectionLineWidth = 2.0;
        QColor jointSelectionColor { 230, 140, 40 };
        QString simulationEngineName;
        // Which engine a *new* scene is built for. An existing scene keeps the
        // one it was saved with, whatever this says.
        QString defaultEngineName;
        QColor jointOutlineColor { 0x5A, 0x4A, 0x21 };
        qreal jointAnchorRadius = 7.0;
        // How long a sliding joint's axis explosion is drawn when its travel is
        // unlimited. A direction, not a measurement -- like the body axes.
        qreal jointAxisLength = 40.0;
        qreal jointWaistWidth = 3.5;
        qreal jointOutlineWidth = 1.6;

        // Where the export converters live: one folder per converter, each
        // with a manifest.json naming it and an export.js doing the work.
        QString converterPath;
        // What each converter asked to be asked, by converter id. The
        // application stores and renders these without knowing what any of
        // them mean -- the same bargain it has with the engine plugins.
        QHash<QString, QVariantMap> converterSettings;

        qreal pixelsPerMeter = 1000.0;
        bool fieldBoundsSolid = false;
    };

    explicit OptionsDialog(const Settings &current, QWidget *parent = nullptr);
    ~OptionsDialog() override;

    Settings settings() const;

private:

    // What the application had when the dialog opened, so the settings it
    // hands back carry the fields it never shows.
    Settings m_incoming;
    qreal m_currentScale;

    // Every widget in ui/OptionsDialog.ui. Only the colours are kept here:
    // a swatch button holds no colour of its own, it just shows one.
    std::unique_ptr<Ui::OptionsDialog> m_ui;

    void bindSwatch(QToolButton *button, QColor &color, const QString &title);

    // The Slingshot group, built here rather than in the form.
    QDoubleSpinBox *m_shotLineWidth = nullptr;
    QComboBox *m_shotLineStyle = nullptr;
    QColor m_shotLightColor;
    QColor m_shotFullColor;
    // Joint anchors' opacity, a row added to the form's joint drawing group.
    QSlider *m_jointAnchorOpacity = nullptr;
    // Builds the Export tab from whatever converters are under `path`, and
    // removes it again when there are none. Called when the dialog opens and
    // whenever the folder is changed while it is open.
    void rebuildExportTab(const QString &path);

    // One editor on that tab, remembered so its value can be read back.
    struct ConverterField {
        QString converter;
        QString key;
        QString type;
        QWidget *editor = nullptr;
    };
    // The widget a declared setting is edited with, and the value back out of
    // it. A swatch button holds no colour of its own, so that one keeps it as
    // a dynamic property rather than anywhere else.
    QWidget *makeConverterEditor(const SceneExporter::ConverterSetting &setting,
                                 const QVariant &value, QWidget *parent);
    QVariant fieldValue(const ConverterField &field) const;
    QVector<ConverterField> m_converterFields;
    QWidget *m_exportTab = nullptr;
    // What was loaded, so a converter whose folder is missing right now does
    // not lose its settings the next time they are saved.
    QHash<QString, QVariantMap> m_converterSettings;
    void bindSliderValue(QSlider *slider, QLabel *label, const QString &suffix);

    QColor m_gridColor;
    QColor m_backgroundColor;
    QColor m_defaultBorderColor;
    QColor m_defaultBodyColor;
    QColor m_selectionColor;
    QColor m_handleColor;
    QColor m_handleBorderColor;
    QColor m_bodyDynamicColor;
    QColor m_bodyStaticColor;
    QColor m_bodyKinematicColor;
    QColor m_unassignedShapeColor;
    QColor m_sensorColor;
    QColor m_physicsSelectionColor;
    QColor m_bodyAxisXColor;
    QColor m_bodyAxisYColor;
    QColor m_jointColor;
    QColor m_jointOutlineColor;
    QHash<int, QColor> m_jointKindColors;
    QHash<int, JointStyle> m_jointKindStyles;
    QColor m_jointSelectionColor;
};
