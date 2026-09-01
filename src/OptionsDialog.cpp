#include "OptionsDialog.h"

#include "ui_OptionsDialog.h"

#include "EngineRegistry.h"
#include "JointTypes.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>

namespace {

QString colorSwatchStyle(const QColor &color)
{
    return QString("background-color: %1; border: 1px solid #555;").arg(color.name(QColor::HexArgb));
}

constexpr int kSwatchWidth = 60;
constexpr int kSwatchHeight = 22;

void setComboData(QComboBox *combo, std::initializer_list<int> values)
{
    int i = 0;
    for (int value : values)
        combo->setItemData(i++, value);
}

void selectData(QComboBox *combo, int value)
{
    combo->setCurrentIndex(qMax(0, combo->findData(value)));
}

} // namespace

OptionsDialog::OptionsDialog(const Settings &current, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::OptionsDialog)
    , m_currentScale(current.currentScale)
    , m_gridColor(current.gridColor)
    , m_backgroundColor(current.backgroundColor)
    , m_defaultBorderColor(current.defaultBorderColor)
    , m_defaultBodyColor(current.defaultBodyColor)
    , m_selectionColor(current.selectionColor)
    , m_handleColor(current.handleColor)
    , m_handleBorderColor(current.handleBorderColor)
    , m_bodyDynamicColor(current.bodyDynamicColor)
    , m_bodyStaticColor(current.bodyStaticColor)
    , m_bodyKinematicColor(current.bodyKinematicColor)
    , m_unassignedShapeColor(current.unassignedShapeColor)
    , m_physicsSelectionColor(current.physicsSelectionColor)
    , m_bodyAxisXColor(current.bodyAxisXColor)
    , m_bodyAxisYColor(current.bodyAxisYColor)
    , m_jointColor(current.jointColor)
    , m_jointOutlineColor(current.jointOutlineColor)
    , m_jointTypeColors(current.jointTypeColors)
    , m_jointSelectionColor(current.jointSelectionColor)
{
    m_ui->setupUi(this);

    // The form carries each combo's visible text; the value behind each entry
    // is set here, because it is an enum this dialog reads back in settings().
    const std::initializer_list<int> penStyles = { Qt::DotLine, Qt::DashLine,
                                                   Qt::SolidLine, Qt::DashDotLine };
    for (QComboBox *combo : { m_ui->selectionLineStyle, m_ui->physicsSelectionLineStyle,
                              m_ui->jointSelectionLineStyle })
        setComboData(combo, penStyles);
    setComboData(m_ui->snapPoint, { int(SnapPoint::Position), int(SnapPoint::Origin) });
    setComboData(m_ui->handleShape, { int(HandleShape::Square), int(HandleShape::Circle) });

    m_ui->fieldWidth->setValue(current.fieldWidth);
    m_ui->fieldHeight->setValue(current.fieldHeight);
    m_ui->scaleMin->setValue(current.scaleMin);
    m_ui->scaleMax->setValue(current.scaleMax);
    m_ui->scaleStep->setValue(current.scaleStep);
    m_ui->showGrid->setChecked(current.showGrid);
    m_ui->cellSize->setValue(current.gridCellSize);
    m_ui->snapToGrid->setChecked(current.snapToGrid);
    selectData(m_ui->snapPoint, int(current.snapPoint));
    m_ui->snapStep->setValue(current.snapStep);
    m_ui->undoDepth->setValue(current.undoDepth);

    m_ui->defaultBorderWidth->setValue(current.defaultBorderWidth);
    m_ui->defaultTransparency->setValue(qRound((1.0 - current.defaultBodyColor.alphaF()) * 100.0));
    selectData(m_ui->selectionLineStyle, current.selectionLineStyle);
    m_ui->selectionLineWidth->setValue(current.selectionLineWidth);
    selectData(m_ui->handleShape, int(current.handleShape));
    m_ui->handleSize->setValue(current.handleSize);
    m_ui->handleBorderWidth->setValue(current.handleBorderWidth);

    m_ui->maxPolygonVertices->setValue(current.maxPolygonVertices);
    m_ui->simulationStepsPerSecond->setValue(current.simulationStepsPerSecond);
    m_ui->physicsBorderWidth->setValue(current.physicsBorderWidth);
    m_ui->physicsFillAlpha->setValue(current.physicsFillAlpha);
    m_ui->sleepShiftPercent->setValue(current.sleepShiftPercent);
    m_ui->showBodyAxes->setChecked(current.showBodyAxes);
    m_ui->bodyAxisLength->setValue(current.bodyAxisLength);
    m_ui->bodyAxisWidth->setValue(current.bodyAxisWidth);
    selectData(m_ui->physicsSelectionLineStyle, current.physicsSelectionLineStyle);
    m_ui->physicsSelectionLineWidth->setValue(current.physicsSelectionLineWidth);

    m_ui->jointAnchorRadius->setValue(current.jointAnchorRadius);
    m_ui->jointAxisLength->setValue(current.jointAxisLength);
    m_ui->jointWaistWidth->setValue(current.jointWaistWidth);
    m_ui->jointOutlineWidth->setValue(current.jointOutlineWidth);
    selectData(m_ui->jointSelectionLineStyle, current.jointSelectionLineStyle);
    m_ui->jointSelectionLineWidth->setValue(current.jointSelectionLineWidth);

    bindSwatch(m_ui->backgroundColorButton, m_backgroundColor, tr("Choose Background Color"));
    bindSwatch(m_ui->gridColorButton, m_gridColor, tr("Choose Grid Color"));
    bindSwatch(m_ui->defaultBorderColorButton, m_defaultBorderColor, tr("Choose Border Color"));
    bindSwatch(m_ui->defaultBodyColorButton, m_defaultBodyColor, tr("Choose Body Color"));
    bindSwatch(m_ui->selectionColorButton, m_selectionColor, tr("Choose Selection Color"));
    bindSwatch(m_ui->handleColorButton, m_handleColor, tr("Choose Handle Color"));
    bindSwatch(m_ui->handleBorderColorButton, m_handleBorderColor, tr("Choose Handle Border Color"));
    bindSwatch(m_ui->bodyDynamicColorButton, m_bodyDynamicColor, tr("Choose Dynamic Body Color"));
    bindSwatch(m_ui->bodyStaticColorButton, m_bodyStaticColor, tr("Choose Static Body Color"));
    bindSwatch(m_ui->bodyKinematicColorButton, m_bodyKinematicColor, tr("Choose Kinematic Body Color"));
    bindSwatch(m_ui->unassignedShapeColorButton, m_unassignedShapeColor, tr("Choose Unassigned Shape Color"));
    bindSwatch(m_ui->physicsSelectionColorButton, m_physicsSelectionColor, tr("Choose Physics Selection Color"));
    bindSwatch(m_ui->bodyAxisXColorButton, m_bodyAxisXColor, tr("Choose X Axis Color"));
    bindSwatch(m_ui->bodyAxisYColorButton, m_bodyAxisYColor, tr("Choose Y Axis Color"));
    bindSwatch(m_ui->jointColorButton, m_jointColor, tr("Choose Joint Color"));
    bindSwatch(m_ui->jointOutlineColorButton, m_jointOutlineColor, tr("Choose Joint Outline Color"));
    bindSwatch(m_ui->jointSelectionColorButton, m_jointSelectionColor, tr("Choose Joint Selection Color"));

    bindSliderValue(m_ui->defaultTransparency, m_ui->defaultTransparencyLabel, tr("%"));
    bindSliderValue(m_ui->physicsFillAlpha, m_ui->physicsFillAlphaLabel, QString());
    bindSliderValue(m_ui->sleepShiftPercent, m_ui->sleepShiftPercentLabel, tr("%"));

    connect(m_ui->defaultTransparency, &QSlider::valueChanged, this, [this](int percent) {
        m_defaultBodyColor.setAlphaF(1.0 - percent / 100.0);
        m_ui->defaultBodyColorButton->setStyleSheet(colorSwatchStyle(m_defaultBodyColor));
    });

    // One row per joint type the engine offers, so a new backend joint gets a
    // colour setting without this dialog knowing the type exists.
    auto *jointTypeForm = qobject_cast<QFormLayout *>(m_ui->jointTypeColorsGroup->layout());
    if (auto engine = physics::EngineRegistry::create(current.simulationEngineName)) {
        for (const physics::JointType &type : engine->jointTypes()) {
            const QString id = type.id;
            if (!m_jointTypeColors.value(id).isValid())
                m_jointTypeColors.insert(id, type.color);

            auto *button = new QToolButton(m_ui->jointTypeColorsGroup);
            button->setFixedSize(kSwatchWidth, kSwatchHeight);
            button->setStyleSheet(colorSwatchStyle(m_jointTypeColors.value(id)));
            button->setToolTip(type.description);
            connect(button, &QToolButton::clicked, this, [this, button, id, label = type.label] {
                const QColor chosen = QColorDialog::getColor(
                    m_jointTypeColors.value(id), this, tr("Choose %1 Color").arg(label),
                    QColorDialog::ShowAlphaChannel);
                if (chosen.isValid()) {
                    m_jointTypeColors.insert(id, chosen);
                    button->setStyleSheet(colorSwatchStyle(chosen));
                }
            });
            jointTypeForm->addRow(tr("%1:").arg(type.label), button);
        }
    }

    // Capped at half the snap step, the largest value that still leaves a free
    // zone between snap points.
    const auto capSensitivity = [this](double step) {
        const double previous = m_ui->snapSensitivity->value();
        m_ui->snapSensitivity->setRange(0.0, step / 2.0);
        if (previous > 0.0)
            m_ui->snapSensitivity->setValue(qMin(previous, step / 2.0));
    };
    connect(m_ui->snapStep, qOverload<double>(&QDoubleSpinBox::valueChanged), this, capSensitivity);
    capSensitivity(m_ui->snapStep->value());
    m_ui->snapSensitivity->setValue(qMin(current.snapSensitivity, m_ui->snapSensitivity->maximum()));

    if (parent) {
        resize(parent->width() * 0.44, parent->height() * 0.935);
    } else {
        adjustSize();
        resize(width() * 1.1, height() * 1.1);
    }
}

OptionsDialog::~OptionsDialog() = default;

void OptionsDialog::bindSliderValue(QSlider *slider, QLabel *label, const QString &suffix)
{
    const auto show = [label, suffix](int value) {
        label->setText(QString::number(value) + suffix);
    };
    connect(slider, &QSlider::valueChanged, label, show);
    show(slider->value());
}

void OptionsDialog::bindSwatch(QToolButton *button, QColor &color, const QString &title)
{
    button->setStyleSheet(colorSwatchStyle(color));
    connect(button, &QToolButton::clicked, this, [this, button, &color, title] {
        const QColor chosen = QColorDialog::getColor(color, this, title,
                                                     QColorDialog::ShowAlphaChannel);
        if (chosen.isValid()) {
            color = chosen;
            button->setStyleSheet(colorSwatchStyle(color));
        }
    });
}

OptionsDialog::Settings OptionsDialog::settings() const
{
    Settings s;
    s.bodyDynamicColor = m_bodyDynamicColor;
    s.bodyStaticColor = m_bodyStaticColor;
    s.bodyKinematicColor = m_bodyKinematicColor;
    s.unassignedShapeColor = m_unassignedShapeColor;
    s.physicsBorderWidth = m_ui->physicsBorderWidth->value();
    s.physicsFillAlpha = m_ui->physicsFillAlpha->value();
    s.physicsSelectionLineStyle =
        static_cast<Qt::PenStyle>(m_ui->physicsSelectionLineStyle->currentData().toInt());
    s.physicsSelectionLineWidth = m_ui->physicsSelectionLineWidth->value();
    s.physicsSelectionColor = m_physicsSelectionColor;
    s.showBodyAxes = m_ui->showBodyAxes->isChecked();
    s.bodyAxisLength = m_ui->bodyAxisLength->value();
    s.bodyAxisWidth = m_ui->bodyAxisWidth->value();
    s.bodyAxisXColor = m_bodyAxisXColor;
    s.bodyAxisYColor = m_bodyAxisYColor;
    s.sleepShiftPercent = m_ui->sleepShiftPercent->value();
    s.maxPolygonVertices = m_ui->maxPolygonVertices->value();
    s.simulationStepsPerSecond = m_ui->simulationStepsPerSecond->value();
    s.jointColor = m_jointColor;
    s.jointOutlineColor = m_jointOutlineColor;
    s.jointAnchorRadius = m_ui->jointAnchorRadius->value();
    s.jointAxisLength = m_ui->jointAxisLength->value();
    s.jointWaistWidth = m_ui->jointWaistWidth->value();
    s.jointOutlineWidth = m_ui->jointOutlineWidth->value();
    s.undoDepth = m_ui->undoDepth->value();
    s.jointTypeColors = m_jointTypeColors;
    s.jointSelectionLineStyle =
        static_cast<Qt::PenStyle>(m_ui->jointSelectionLineStyle->currentData().toInt());
    s.jointSelectionLineWidth = m_ui->jointSelectionLineWidth->value();
    s.jointSelectionColor = m_jointSelectionColor;
    s.fieldWidth = m_ui->fieldWidth->value();
    s.fieldHeight = m_ui->fieldHeight->value();
    s.backgroundColor = m_backgroundColor;
    s.showGrid = m_ui->showGrid->isChecked();
    s.gridCellSize = m_ui->cellSize->value();
    s.gridColor = m_gridColor;
    s.snapToGrid = m_ui->snapToGrid->isChecked();
    s.snapPoint = static_cast<SnapPoint>(m_ui->snapPoint->currentData().toInt());
    s.snapStep = m_ui->snapStep->value();
    s.snapSensitivity = m_ui->snapSensitivity->value();
    s.currentScale = m_currentScale;
    s.scaleMin = m_ui->scaleMin->value();
    s.scaleMax = m_ui->scaleMax->value();
    s.scaleStep = m_ui->scaleStep->value();
    s.defaultBorderColor = m_defaultBorderColor;
    s.defaultBorderWidth = m_ui->defaultBorderWidth->value();
    s.defaultBodyColor = m_defaultBodyColor;
    s.selectionLineStyle =
        static_cast<Qt::PenStyle>(m_ui->selectionLineStyle->currentData().toInt());
    s.selectionLineWidth = m_ui->selectionLineWidth->value();
    s.selectionColor = m_selectionColor;
    s.handleShape = static_cast<HandleShape>(m_ui->handleShape->currentData().toInt());
    s.handleSize = m_ui->handleSize->value();
    s.handleColor = m_handleColor;
    s.handleBorderWidth = m_ui->handleBorderWidth->value();
    s.handleBorderColor = m_handleBorderColor;
    return s;
}
