#include "OptionsDialog.h"

#include "ui_OptionsDialog.h"

#include "EngineRegistry.h"
#include "JointTypes.h"
#include "SceneExporter.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QFileDialog>
#include <QGuiApplication>
#include <QScreen>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
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

namespace {

// A colour setting is a caption and a small square, and a form layout gives
// each one a whole line of the dialog -- five body colours and eight joint
// types come to thirteen rows of mostly empty space. The pairs are laid out
// across instead, several to a line, which is the same information in a third
// of the height. The form is read and replaced rather than the .ui being
// written this way, so a group whose rows are added in code -- the joint
// types, one per type the engine offers -- is folded the same way.
void compactColorGroup(QGroupBox *group, int perRow = 3)
{
    auto *form = qobject_cast<QFormLayout *>(group->layout());
    if (!form || form->rowCount() < 2)
        return;

    // Only the swatches are folded across. A combo box or a check box beside
    // them wants the width of the group to itself, and squeezing one into a
    // third of it turns its text into "Diagor".
    const auto isSwatch = [](QWidget *w) {
        auto *button = qobject_cast<QToolButton *>(w);
        return button && button->maximumWidth() <= kSwatchWidth;
    };

    QVector<QPair<QWidget *, QWidget *>> swatches;
    QVector<QPair<QWidget *, QWidget *>> rest;
    for (int i = 0; i < form->rowCount(); ++i) {
        QLayoutItem *label = form->itemAt(i, QFormLayout::LabelRole);
        QLayoutItem *field = form->itemAt(i, QFormLayout::FieldRole);
        if (!field || !field->widget())
            return;   // something that is not a plain caption-and-control pair
        QWidget *caption = label ? label->widget() : nullptr;
        (isSwatch(field->widget()) ? swatches : rest).append(qMakePair(caption, field->widget()));
    }
    if (swatches.size() < 2)
        return;

    for (const auto &pair : swatches + rest) {
        if (pair.first)
            pair.first->setParent(nullptr);
        pair.second->setParent(nullptr);
    }
    delete form;

    auto *grid = new QGridLayout(group);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    const int columns = perRow * 2;
    for (int i = 0; i < swatches.size(); ++i) {
        const int row = i / perRow;
        const int column = (i % perRow) * 2;
        if (swatches[i].first) {
            swatches[i].first->setParent(group);
            grid->addWidget(swatches[i].first, row, column);
        }
        swatches[i].second->setParent(group);
        grid->addWidget(swatches[i].second, row, column + 1);
    }
    // Whatever was not a swatch keeps a line of its own, under them.
    int row = (swatches.size() + perRow - 1) / perRow;
    for (const auto &pair : rest) {
        if (pair.first) {
            pair.first->setParent(group);
            grid->addWidget(pair.first, row, 0);
        }
        pair.second->setParent(group);
        grid->addWidget(pair.second, row, 1, 1, columns - 1);
        ++row;
    }
    // The last column takes the slack, so the pairs sit together on the left
    // rather than spreading across the whole width.
    grid->setColumnStretch(columns, 1);
}

} // namespace

OptionsDialog::OptionsDialog(const Settings &current, QWidget *parent)
    : QDialog(parent)
    , m_ui(new Ui::OptionsDialog)
    , m_incoming(current)
    , m_currentScale(current.currentScale)
    , m_gridColor(current.gridColor)
    , m_backgroundColor(current.backgroundColor)
    , m_shapeStyles(current.shapeStyles)
    , m_logColor(current.logColor)
    , m_logCorner(current.logCorner)
    , m_logFontFamily(current.logFontFamily)
    , m_logFontSize(current.logFontSize)
    , m_selectionColor(current.selectionColor)
    , m_handleColor(current.handleColor)
    , m_handleBorderColor(current.handleBorderColor)
    , m_bodyDynamicColor(current.bodyDynamicColor)
    , m_bodyStaticColor(current.bodyStaticColor)
    , m_bodyKinematicColor(current.bodyKinematicColor)
    , m_unassignedShapeColor(current.unassignedShapeColor)
    , m_sensorColor(current.sensorColor)
    , m_physicsSelectionColor(current.physicsSelectionColor)
    , m_bodyAxisXColor(current.bodyAxisXColor)
    , m_bodyAxisYColor(current.bodyAxisYColor)
    , m_jointColor(current.jointColor)
    , m_jointOutlineColor(current.jointOutlineColor)
    , m_jointKindColors(current.jointKindColors)
    , m_jointKindStyles(current.jointKindStyles)
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
    setComboData(m_ui->sensorPattern, { int(Qt::DiagCrossPattern), int(Qt::BDiagPattern),
                                        int(Qt::HorPattern), int(Qt::SolidPattern) });

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
    // The engines found beside the executable. A scene is built for one of
    // them and keeps it, so this only says which a new scene starts with.
    m_ui->defaultEngine->addItems(physics::EngineRegistry::availableEngines());
    m_ui->defaultEngine->setCurrentIndex(
        qMax(0, m_ui->defaultEngine->findText(current.defaultEngineName)));
    for (const Theme::Choice &choice : Theme::available())
        m_ui->theme->addItem(choice.label, choice.key);
    m_ui->theme->setCurrentIndex(qMax(0, m_ui->theme->findData(current.theme)));
    for (const Theme::Choice &choice : Theme::availableControls())
        m_ui->controls->addItem(choice.label, choice.key);
    m_ui->controls->setCurrentIndex(qMax(0, m_ui->controls->findData(current.controls)));
    for (const Units::Entry &entry : Units::all())
        m_ui->rulerUnit->addItem(entry.label, Units::key(entry.unit));
    m_ui->rulerUnit->setCurrentIndex(
        qMax(0, m_ui->rulerUnit->findData(Units::key(current.rulerUnit))));
    // Worn as it is picked, not on OK: a theme and a shape are things you
    // choose by looking at them, and the dialog is the largest thing on the
    // screen to look at. Connected after the two are filled in, so setting
    // them up does not count as a choice.
    const auto wear = [this] {
        Theme::apply(m_ui->theme->currentData().toString(),
                     m_ui->controls->currentData().toString());
    };
    connect(m_ui->theme, qOverload<int>(&QComboBox::currentIndexChanged), this, wear);
    connect(m_ui->controls, qOverload<int>(&QComboBox::currentIndexChanged), this, wear);

    m_converterSettings = current.converterSettings;
    rebuildExportTab();

    selectData(m_ui->selectionLineStyle, current.selectionLineStyle);
    m_ui->selectionLineWidth->setValue(current.selectionLineWidth);
    selectData(m_ui->handleShape, int(current.handleShape));
    selectData(m_ui->sensorPattern, int(current.sensorPattern));
    m_ui->sensorFillsBody->setChecked(current.sensorFillsBody);
    m_ui->handleSize->setValue(current.handleSize);
    m_ui->handleBorderWidth->setValue(current.handleBorderWidth);

    m_ui->maxPolygonVertices->setValue(current.maxPolygonVertices);
    m_ui->simulationStepsPerSecond->setValue(current.simulationStepsPerSecond);
    m_ui->physicsBorderWidth->setValue(current.physicsBorderWidth);
    m_ui->physicsFillAlpha->setValue(current.physicsFillAlpha);
    m_ui->jointFillAlpha->setValue(current.jointFillAlpha);
    m_ui->sleepShiftPercent->setValue(current.sleepShiftPercent);
    m_ui->showBodyAxes->setChecked(current.showBodyAxes);
    m_ui->bodyAxisLength->setValue(current.bodyAxisLength);
    m_ui->bodyAxisWidth->setValue(current.bodyAxisWidth);
    selectData(m_ui->physicsSelectionLineStyle, current.physicsSelectionLineStyle);
    m_ui->physicsSelectionLineWidth->setValue(current.physicsSelectionLineWidth);

    m_ui->jointAnchorRadius->setValue(current.jointAnchorRadius);
    m_ui->jointAxisLength->setValue(current.jointAxisLength);
    m_ui->jointWaistWidth->setValue(current.jointWaistWidth);
    m_ui->springsAsCoils->setChecked(current.springsAsCoils);
    m_ui->springPitch->setValue(current.springPitch);
    m_ui->springWidth->setValue(current.springWidth);
    m_ui->jointOutlineWidth->setValue(current.jointOutlineWidth);
    selectData(m_ui->jointSelectionLineStyle, current.jointSelectionLineStyle);
    m_ui->jointSelectionLineWidth->setValue(current.jointSelectionLineWidth);

    bindSwatch(m_ui->backgroundColorButton, m_backgroundColor, tr("Choose Background Color"));
    bindSwatch(m_ui->gridColorButton, m_gridColor, tr("Choose Grid Color"));
    buildShapeStyleRows();
    bindSwatch(m_ui->selectionColorButton, m_selectionColor, tr("Choose Selection Color"));
    bindSwatch(m_ui->handleColorButton, m_handleColor, tr("Choose Handle Color"));
    bindSwatch(m_ui->handleBorderColorButton, m_handleBorderColor, tr("Choose Handle Border Color"));
    bindSwatch(m_ui->bodyDynamicColorButton, m_bodyDynamicColor, tr("Choose Dynamic Body Color"));
    bindSwatch(m_ui->bodyStaticColorButton, m_bodyStaticColor, tr("Choose Static Body Color"));
    bindSwatch(m_ui->bodyKinematicColorButton, m_bodyKinematicColor, tr("Choose Kinematic Body Color"));
    bindSwatch(m_ui->unassignedShapeColorButton, m_unassignedShapeColor, tr("Choose Unassigned Shape Color"));
    bindSwatch(m_ui->sensorColorButton, m_sensorColor, tr("Choose Sensor Hatching Color"));
    bindSwatch(m_ui->physicsSelectionColorButton, m_physicsSelectionColor, tr("Choose Physics Selection Color"));
    bindSwatch(m_ui->bodyAxisXColorButton, m_bodyAxisXColor, tr("Choose X Axis Color"));
    bindSwatch(m_ui->bodyAxisYColorButton, m_bodyAxisYColor, tr("Choose Y Axis Color"));
    bindSwatch(m_ui->jointColorButton, m_jointColor, tr("Choose Joint Color"));
    bindSwatch(m_ui->jointOutlineColorButton, m_jointOutlineColor, tr("Choose Joint Outline Color"));
    bindSwatch(m_ui->jointSelectionColorButton, m_jointSelectionColor, tr("Choose Joint Selection Color"));

    bindSliderValue(m_ui->physicsFillAlpha, m_ui->physicsFillAlphaLabel, QString());
    bindSliderValue(m_ui->jointFillAlpha, m_ui->jointFillAlphaLabel, QString());

    // Below Fill opacity: how see-through the anchors are, outline and all.
    {
        auto *container = new QWidget(m_ui->jointDrawingGroup);
        auto *row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        m_jointAnchorOpacity = new QSlider(Qt::Horizontal, container);
        m_jointAnchorOpacity->setRange(10, 100);
        m_jointAnchorOpacity->setValue(current.jointAnchorOpacity);
        m_jointAnchorOpacity->setToolTip(tr("How see-through joint anchors are drawn, so the shapes under"
                                            " them stay visible."));
        auto *value = new QLabel(container);
        value->setMinimumWidth(36);
        row->addWidget(m_jointAnchorOpacity, 1);
        row->addWidget(value);
        bindSliderValue(m_jointAnchorOpacity, value, tr("%"));

        int fillRow = -1;
        QFormLayout::ItemRole role;
        m_ui->jointDrawingGroupForm->getWidgetPosition(m_ui->jointFillAlphaContainer, &fillRow, &role);
        m_ui->jointDrawingGroupForm->insertRow(fillRow + 1, tr("Anchor opacity:"), container);
    }
    bindSliderValue(m_ui->sleepShiftPercent, m_ui->sleepShiftPercentLabel, tr("%"));


    buildLogGroup();

    // One row per kind of shape. The group's own rows are dropped first: the
    // form came from the .ui with a single set of controls for every kind.
    buildShapeStyleRows();

    // One row per kind of joint, not per engine's joint type: every engine
    // tags each of its types with one of these five, so the same short list
    // covers whichever engine a scene is built for.
    auto *jointTypeForm = qobject_cast<QFormLayout *>(m_ui->jointTypeColorsGroup->layout());
    {
        for (const physics::JointVisual kind : CanvasScene::jointKinds()) {
            const int id = static_cast<int>(kind);
            if (!m_jointKindColors.value(id).isValid())
                m_jointKindColors.insert(id, CanvasScene::defaultJointKindColor(kind));
            if (!m_jointKindStyles.contains(id))
                m_jointKindStyles.insert(id, CanvasScene::defaultJointKindStyle(kind));

            const QString label = CanvasScene::jointKindLabel(kind);
            auto *button = new QToolButton(m_ui->jointTypeColorsGroup);
            button->setFixedSize(kSwatchWidth, kSwatchHeight);
            button->setStyleSheet(colorSwatchStyle(m_jointKindColors.value(id)));
            button->setToolTip(CanvasScene::jointKindDescription(kind));
            connect(button, &QToolButton::clicked, this, [this, button, id, label] {
                const QColor chosen = QColorDialog::getColor(
                    m_jointKindColors.value(id), this, tr("Choose %1 Color").arg(label),
                    QColorDialog::ShowAlphaChannel);
                if (chosen.isValid()) {
                    m_jointKindColors.insert(id, chosen);
                    button->setStyleSheet(colorSwatchStyle(chosen));
                }
            });

            // How the line between the joint's ends is drawn: the rod it has
            // always been, or a plain stroke.
            auto *style = new QComboBox(m_ui->jointTypeColorsGroup);
            style->setToolTip(CanvasScene::jointKindDescription(kind));
            for (const JointStyle option : CanvasScene::jointStyles())
                style->addItem(CanvasScene::jointStyleLabel(option), static_cast<int>(option));
            style->setCurrentIndex(
                qMax(0, style->findData(static_cast<int>(m_jointKindStyles.value(id)))));
            connect(style, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this, style, id](int) {
                        m_jointKindStyles.insert(
                            id, static_cast<JointStyle>(style->currentData().toInt()));
                    });
            auto *field = new QWidget(m_ui->jointTypeColorsGroup);
            auto *fieldLayout = new QHBoxLayout(field);
            fieldLayout->setContentsMargins(0, 0, 0, 0);
            fieldLayout->setSpacing(6);
            fieldLayout->addWidget(button);
            fieldLayout->addWidget(style, 1);
            jointTypeForm->addRow(tr("%1:").arg(label), field);
        }
    }

    // How the slingshot's pull line looks while aiming. The same for every
    // body -- whether one can be shot, and how hard, stay on the body.
    {
        m_shotLightColor = current.shotLightColor;
        m_shotFullColor = current.shotFullColor;
        auto *group = new QGroupBox(tr("Slingshot"), m_ui->physicsTab);
        auto *form = new QFormLayout(group);

        auto *light = new QToolButton(group);
        light->setFixedSize(kSwatchWidth, kSwatchHeight);
        light->setStyleSheet(colorSwatchStyle(m_shotLightColor));
        bindSwatch(light, m_shotLightColor, tr("Choose Light Pull Color"));
        light->setToolTip(tr("The line's colour with little force in the pull."));
        form->addRow(tr("Light pull color:"), light);

        auto *full = new QToolButton(group);
        full->setFixedSize(kSwatchWidth, kSwatchHeight);
        full->setStyleSheet(colorSwatchStyle(m_shotFullColor));
        bindSwatch(full, m_shotFullColor, tr("Choose Full Pull Color"));
        full->setToolTip(tr("The line's colour at full power. In between, the two are mixed."));
        form->addRow(tr("Full pull color:"), full);

        m_shotLineWidth = new QDoubleSpinBox(group);
        m_shotLineWidth->setRange(0.5, 12.0);
        m_shotLineWidth->setDecimals(1);
        m_shotLineWidth->setSingleStep(0.5);
        m_shotLineWidth->setSuffix(tr(" px"));
        m_shotLineWidth->setValue(current.shotLineWidth);
        form->addRow(tr("Line width:"), m_shotLineWidth);

        m_shotLineStyle = new QComboBox(group);
        m_shotLineStyle->addItem(tr("Dotted"), int(Qt::DotLine));
        m_shotLineStyle->addItem(tr("Dashed"), int(Qt::DashLine));
        m_shotLineStyle->addItem(tr("Solid"), int(Qt::SolidLine));
        m_shotLineStyle->addItem(tr("Dash-dot"), int(Qt::DashDotLine));
        selectData(m_shotLineStyle, int(current.shotLineStyle));
        form->addRow(tr("Line style:"), m_shotLineStyle);

        // Above the spacer that closes the tab.
        m_ui->physicsLayout->insertWidget(m_ui->physicsLayout->count() - 1, group);
        compactColorGroup(group);
    }

    // How a trail looks. The same for every body -- whether one draws at all
    // is the body's own row.
    {
        m_trailColor = current.trailColor;
        auto *group = new QGroupBox(tr("Trail"), m_ui->physicsTab);
        auto *form = new QFormLayout(group);

        auto *color = new QToolButton(group);
        color->setFixedSize(kSwatchWidth, kSwatchHeight);
        color->setStyleSheet(colorSwatchStyle(m_trailColor));
        bindSwatch(color, m_trailColor, tr("Choose Trail Color"));
        color->setToolTip(tr("The colour of the first body's trail. Every other body that draws"
                             " gets its own colour, turned further round the colour wheel."));
        form->addRow(tr("Color:"), color);

        m_trailLineWidth = new QDoubleSpinBox(group);
        m_trailLineWidth->setRange(0.5, 12.0);
        m_trailLineWidth->setDecimals(1);
        m_trailLineWidth->setSingleStep(0.5);
        m_trailLineWidth->setSuffix(tr(" px"));
        m_trailLineWidth->setValue(current.trailLineWidth);
        form->addRow(tr("Line width:"), m_trailLineWidth);

        m_trailLineStyle = new QComboBox(group);
        m_trailLineStyle->addItem(tr("Solid"), int(Qt::SolidLine));
        m_trailLineStyle->addItem(tr("Dashed"), int(Qt::DashLine));
        m_trailLineStyle->addItem(tr("Dotted"), int(Qt::DotLine));
        m_trailLineStyle->addItem(tr("Dash-dot"), int(Qt::DashDotLine));
        selectData(m_trailLineStyle, int(current.trailLineStyle));
        form->addRow(tr("Line style:"), m_trailLineStyle);

        m_ui->physicsLayout->insertWidget(m_ui->physicsLayout->count() - 1, group);
        compactColorGroup(group);
    }

    // Now that every row exists, including the ones added just above.
    for (QGroupBox *group : { m_ui->bodyColorsGroup, m_ui->jointTypeColorsGroup,
                              m_ui->defaultStyleGroup, m_ui->handleStyleGroup,
                              m_ui->bodyAxesGroup, m_ui->jointDrawingGroup,
                              m_ui->springGroup })
        compactColorGroup(group);

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

    // Every page back to what the program ships with. On the left, away from
    // OK and Cancel, and in the colour the rest of the application uses for
    // something going wrong: it throws away every page at once, not the one
    // being looked at, and it closes the dialog to say so.
    auto *reset = m_ui->buttonBox->addButton(tr("Reset to Defaults"),
                                             QDialogButtonBox::ResetRole);
    reset->setObjectName(QStringLiteral("resetToDefaults"));
    reset->setToolTip(tr("Put every setting on every page back to its default. The scene"
                         " itself is not touched."));
    const auto paintReset = [reset] {
        const QColor red = Theme::colors().error;
        reset->setStyleSheet(QStringLiteral(
                                 "QPushButton { background: %1; color: palette(bright-text);"
                                 " border-color: %1; }"
                                 "QPushButton:hover { background: %2; border-color: %2; }"
                                 "QPushButton:pressed { background: %3; border-color: %3; }")
                                 .arg(red.name(), red.lighter(112).name(), red.darker(112).name()));
    };
    paintReset();
    // The theme can change under the dialog -- the two combos above do it as
    // they are picked -- and a colour baked into a sheet does not follow.
    connect(Theme::instance(), &Theme::changed, reset, paintReset);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (!askToReset())
            return;
        m_resetToDefaults = true;
        accept();
    });

    // Sized to what it holds, not to the window it was opened from. Taking a
    // share of the main window made the dialog grow with it -- on a maximised
    // window it opened nearly full height whatever was on the page, and every
    // tab is a scroll area, so it never needed to be that tall.
    //
    // The extra 20px is room the tallest pages were short of: without it the
    // last row of the longest tab sits against the button box.
    // A third taller than it strictly needs: the pages sit better with room
    // under the last group than with it pressed against the button box, and
    // the tallest tabs still scroll.
    constexpr double kHeadroom = 1.32;
    adjustSize();
    QSize wanted(width(), int(height() * kHeadroom));
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        // Never taller or wider than the screen can show, however long the
        // Export tab grows with the converters that are installed.
        wanted = wanted.boundedTo(screen->availableGeometry().size() * 0.85);
    }
    resize(wanted);
}

OptionsDialog::~OptionsDialog() = default;

// Asked rather than undone: nothing here can be put back afterwards, and the
// colours and sizes on the other pages may have been years in the making.
bool OptionsDialog::askToReset()
{
    QMessageBox ask(QMessageBox::Warning, tr("Reset to Defaults"),
                    tr("Put every setting back to its default?"), QMessageBox::NoButton, this);
    ask.setInformativeText(tr("Every page goes, not only this one. The scene and what is drawn"
                              " on it are not touched."));
    QPushButton *go = ask.addButton(tr("Reset"), QMessageBox::AcceptRole);
    ask.setDefaultButton(ask.addButton(QMessageBox::Cancel));
    ask.exec();
    return ask.clickedButton() == go;
}

void OptionsDialog::bindSliderValue(QSlider *slider, QLabel *label, const QString &suffix)
{
    const auto show = [label, suffix](int value) {
        label->setText(QString::number(value) + suffix);
    };
    connect(slider, &QSlider::valueChanged, label, show);
    show(slider->value());
}

void OptionsDialog::buildLogGroup()
{
    auto *group = new QGroupBox(tr("Log"), m_ui->physicsTab);
    auto *form = new QFormLayout(group);

    auto *font = new QFontComboBox(group);
    font->setObjectName(QStringLiteral("logFont"));
    font->setCurrentFont(QFont(m_logFontFamily));
    font->setToolTip(tr("The typeface the readout is drawn in."));
    connect(font, &QFontComboBox::currentFontChanged, this,
            [this](const QFont &f) { m_logFontFamily = f.family(); });
    form->addRow(tr("Font:"), font);

    auto *size = new QSpinBox(group);
    size->setObjectName(QStringLiteral("logFontSize"));
    size->setRange(6, 48);
    size->setValue(m_logFontSize);
    size->setSuffix(tr(" pt"));
    connect(size, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int v) { m_logFontSize = v; });
    form->addRow(tr("Size:"), size);

    auto *corner = new QComboBox(group);
    corner->setObjectName(QStringLiteral("logCorner"));
    corner->addItem(tr("Top left"), int(Qt::TopLeftCorner));
    corner->addItem(tr("Top right"), int(Qt::TopRightCorner));
    corner->addItem(tr("Bottom left"), int(Qt::BottomLeftCorner));
    corner->addItem(tr("Bottom right"), int(Qt::BottomRightCorner));
    corner->setCurrentIndex(corner->findData(int(m_logCorner)));
    connect(corner, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, corner](int) {
                m_logCorner = static_cast<Qt::Corner>(corner->currentData().toInt());
            });
    form->addRow(tr("Corner:"), corner);

    auto *colour = new QToolButton(group);
    colour->setObjectName(QStringLiteral("logColorButton"));
    colour->setFixedSize(kSwatchWidth, kSwatchHeight);
    bindSwatch(colour, m_logColor, tr("Choose Log Colour"));
    form->addRow(tr("Colour:"), colour);

    m_ui->physicsLayout->addWidget(group);
}

void OptionsDialog::buildShapeStyleRows()
{
    auto *form = qobject_cast<QFormLayout *>(m_ui->defaultStyleGroup->layout());
    if (!form)
        return;
    while (form->rowCount() > 0)
        form->removeRow(0);

    auto *heading = new QLabel(tr("Fill, border, width and line, for each kind of shape."),
                               m_ui->defaultStyleGroup);
    heading->setProperty("muted", true);
    heading->setStyleSheet(QStringLiteral("font-size: 11px;"));
    heading->setWordWrap(true);
    form->addRow(heading);

    for (const QString &kind : ShapeStyle::kinds()) {
        if (!m_shapeStyles.contains(kind))
            m_shapeStyles.insert(kind, ShapeStyle::defaultFor(kind));

        auto *row = new QWidget(m_ui->defaultStyleGroup);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        const auto swatch = [this, row, layout, kind](bool border) {
            auto *button = new QToolButton(row);
            button->setFixedSize(kSwatchWidth, kSwatchHeight);
            const ShapeStyle &style = m_shapeStyles[kind];
            button->setStyleSheet(colorSwatchStyle(border ? style.border : style.body));
            button->setToolTip(border ? tr("Border colour") : tr("Fill colour"));
            connect(button, &QToolButton::clicked, this, [this, button, kind, border] {
                ShapeStyle &style = m_shapeStyles[kind];
                const QColor chosen = QColorDialog::getColor(
                    border ? style.border : style.body, this,
                    border ? tr("Choose Border Colour") : tr("Choose Fill Colour"),
                    QColorDialog::ShowAlphaChannel);
                if (!chosen.isValid())
                    return;
                (border ? style.border : style.body) = chosen;
                button->setStyleSheet(colorSwatchStyle(chosen));
                if (border)
                    return;
                if (auto *box = findChild<QSpinBox *>(QStringLiteral("transparency_") + kind)) {
                    QSignalBlocker blocker(box);
                    box->setValue(100 - qRound(chosen.alpha() / 255.0 * 100.0));
                }
            });
            layout->addWidget(button);
        };
        swatch(false);
        swatch(true);

        // The fill's alpha, as the percentage the old single slider showed.
        auto *clear = new QSpinBox(row);
        clear->setObjectName(QStringLiteral("transparency_") + kind);
        clear->setRange(0, 100);
        clear->setSuffix(tr("%"));
        clear->setToolTip(tr("How much of what is behind shows through the fill."));
        clear->setValue(100 - qRound(m_shapeStyles[kind].body.alpha() / 255.0 * 100.0));
        connect(clear, qOverload<int>(&QSpinBox::valueChanged), this,
                [this, kind, layout](int percent) {
                    QColor fill = m_shapeStyles[kind].body;
                    fill.setAlpha(qBound(0, qRound((100 - percent) / 100.0 * 255.0), 255));
                    m_shapeStyles[kind].body = fill;
                    if (auto *button = qobject_cast<QToolButton *>(layout->itemAt(0)->widget()))
                        button->setStyleSheet(colorSwatchStyle(fill));
                });
        layout->addWidget(clear);

        auto *width = new QDoubleSpinBox(row);
        width->setDecimals(1);
        width->setRange(0.0, 100.0);
        width->setSingleStep(0.5);
        width->setValue(m_shapeStyles[kind].borderWidth);
        width->setToolTip(tr("Border width"));
        connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [this, kind](double v) { m_shapeStyles[kind].borderWidth = v; });
        layout->addWidget(width);

        auto *style = new QComboBox(row);
        for (Qt::PenStyle pen : ShapeStyle::penStyles())
            style->addItem(ShapeStyle::penStyleLabel(pen), static_cast<int>(pen));
        style->setCurrentIndex(
            style->findData(static_cast<int>(m_shapeStyles[kind].borderStyle)));
        style->setToolTip(tr("Border line"));
        connect(style, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, kind, style](int) {
                    m_shapeStyles[kind].borderStyle =
                        static_cast<Qt::PenStyle>(style->currentData().toInt());
                });
        layout->addWidget(style, 1);

        form->addRow(ShapeStyle::kindLabel(kind), row);
    }
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

QWidget *OptionsDialog::makeConverterEditor(const SceneExporter::ConverterSetting &setting,
                                            const QVariant &value, QWidget *parent)
{
    const QString &type = setting.type;

    if (type == QLatin1String("bool")) {
        auto *check = new QCheckBox(parent);
        check->setChecked(value.toBool());
        return check;
    }
    if (type == QLatin1String("int")) {
        auto *spin = new QSpinBox(parent);
        // A converter that names no range gets a generous one rather than
        // Qt's default 0..99, which would silently clamp what it asked for.
        const bool ranged = setting.minValue != 0.0 || setting.maxValue != 0.0;
        spin->setRange(ranged ? int(setting.minValue) : -1000000,
                       ranged ? int(setting.maxValue) : 1000000);
        spin->setValue(value.toInt());
        return spin;
    }
    if (type == QLatin1String("double")) {
        auto *spin = new QDoubleSpinBox(parent);
        const bool ranged = setting.minValue != 0.0 || setting.maxValue != 0.0;
        spin->setRange(ranged ? setting.minValue : -1e9, ranged ? setting.maxValue : 1e9);
        spin->setDecimals(setting.decimals);
        spin->setValue(value.toDouble());
        return spin;
    }
    if (type == QLatin1String("choice")) {
        auto *combo = new QComboBox(parent);
        combo->addItems(setting.choices);
        const int at = setting.choices.indexOf(value.toString());
        combo->setCurrentIndex(at >= 0 ? at : 0);
        return combo;
    }
    if (type == QLatin1String("color")) {
        auto *button = new QToolButton(parent);
        button->setMinimumSize(60, 22);
        button->setMaximumSize(60, 22);
        const QColor initial = QColor(value.toString()).isValid() ? QColor(value.toString())
                                                                  : QColor(Qt::white);
        // The button carries the colour itself, so nothing else has to be kept
        // in step with it.
        button->setProperty("chosenColour", initial);
        button->setStyleSheet(colorSwatchStyle(initial));
        connect(button, &QToolButton::clicked, this, [this, button, label = setting.label] {
            const QColor was = button->property("chosenColour").value<QColor>();
            const QColor chosen = QColorDialog::getColor(was, this, label,
                                                         QColorDialog::ShowAlphaChannel);
            if (!chosen.isValid())
                return;
            button->setProperty("chosenColour", chosen);
            button->setStyleSheet(colorSwatchStyle(chosen));
        });
        return button;
    }

    if (type == QLatin1String("path") || type == QLatin1String("file")) {
        // A box and a browse button, the way the converters folder itself is
        // chosen. Typing a path stays possible -- it is often quicker, and it
        // is the only way to enter one that does not exist yet.
        const bool wantsFolder = type == QLatin1String("path");

        auto *holder = new QWidget(parent);
        auto *row = new QHBoxLayout(holder);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);

        auto *edit = new QLineEdit(holder);
        edit->setText(value.toString());
        row->addWidget(edit);

        auto *browse = new QToolButton(holder);
        browse->setText(QStringLiteral("..."));
        row->addWidget(browse);

        connect(browse, &QToolButton::clicked, this,
                [this, edit, wantsFolder, label = setting.label] {
                    const QString chosen =
                        wantsFolder ? QFileDialog::getExistingDirectory(this, label, edit->text())
                                    : QFileDialog::getOpenFileName(this, label, edit->text());
                    if (!chosen.isEmpty())
                        edit->setText(chosen);
                });
        return holder;
    }

    // "string", and anything this version has never heard of: a converter
    // written against a later one still gets a usable box rather than nothing.
    auto *edit = new QLineEdit(parent);
    edit->setText(value.toString());
    return edit;
}

QVariant OptionsDialog::fieldValue(const ConverterField &field) const
{
    // A path is a box beside a button, so the value is one level in.
    if (field.type == QLatin1String("path") || field.type == QLatin1String("file")) {
        auto *edit = field.editor ? field.editor->findChild<QLineEdit *>() : nullptr;
        return edit ? edit->text() : QString();
    }
    if (auto *check = qobject_cast<QCheckBox *>(field.editor))
        return check->isChecked();
    if (auto *spin = qobject_cast<QSpinBox *>(field.editor))
        return spin->value();
    if (auto *spin = qobject_cast<QDoubleSpinBox *>(field.editor))
        return spin->value();
    if (auto *combo = qobject_cast<QComboBox *>(field.editor))
        return combo->currentText();
    if (auto *button = qobject_cast<QToolButton *>(field.editor))
        return button->property("chosenColour").value<QColor>().name(QColor::HexArgb);
    if (auto *edit = qobject_cast<QLineEdit *>(field.editor))
        return edit->text();
    return {};
}

void OptionsDialog::rebuildExportTab()
{
    // Whatever is on the page now goes first: it may belong to converters that
    // are no longer there, and its widgets are about to be deleted.
    for (const ConverterField &field : std::as_const(m_converterFields)) {
        if (!field.editor)
            continue;
        m_converterSettings[field.converter].insert(field.key, fieldValue(field));
    }
    m_converterFields.clear();

    if (m_exportTab) {
        m_ui->tabs->removeTab(m_ui->tabs->indexOf(m_exportTab));
        delete m_exportTab;
        m_exportTab = nullptr;
    }

    const QVector<SceneExporter::Converter> converters = SceneExporter::discover();
    if (converters.isEmpty())
        return;   // nothing to configure, so no tab at all

    m_exportTab = new QWidget(m_ui->tabs);
    auto *outer = new QVBoxLayout(m_exportTab);
    auto *pages = new QTabWidget(m_exportTab);
    outer->addWidget(pages);

    for (const SceneExporter::Converter &converter : converters) {
        auto *page = new QWidget(pages);
        auto *form = new QFormLayout(page);

        if (!converter.description.isEmpty()) {
            auto *about = new QLabel(converter.description, page);
            about->setWordWrap(true);
            about->setProperty("muted", true);
            form->addRow(about);
        }
        if (converter.settings.isEmpty()) {
            auto *none = new QLabel(tr("This converter has nothing to set."), page);
            none->setProperty("muted", true);
            form->addRow(none);
        }

        const QVariantMap stored = m_converterSettings.value(converter.id);
        for (const SceneExporter::ConverterSetting &setting : converter.settings) {
            const QVariant value = stored.contains(setting.key) ? stored.value(setting.key)
                                                                : setting.defaultValue;
            ConverterField field;
            field.converter = converter.id;
            field.key = setting.key;
            field.type = setting.type;
            field.declared = setting.defaultValue;
            field.editor = makeConverterEditor(setting, value, page);
            if (!field.editor)
                continue;
            field.editor->setToolTip(setting.tooltip);
            form->addRow(setting.label, field.editor);
            m_converterFields.append(field);
        }
        pages->addTab(page, converter.name);
    }
    m_ui->tabs->addTab(m_exportTab, tr("Export"));
}

OptionsDialog::Settings OptionsDialog::settings() const
{
    // Starts from what it was given, not from the built-in defaults: the dialog
    // does not show everything the application keeps -- the toolbar's own debug
    // view and playback speed among them -- and pressing OK must not quietly
    // reset what it never asked about.
    Settings s = m_incoming;
    s.converterSettings = m_converterSettings;
    // Only what the user moved off the manifest's default is kept. Writing the
    // defaults down too would mean opening this dialog and pressing OK changed
    // the settings file, and would freeze a default a converter may later
    // revise.
    for (const ConverterField &field : m_converterFields) {
        const QVariant value = fieldValue(field);
        if (value == field.declared)
            s.converterSettings[field.converter].remove(field.key);
        else
            s.converterSettings[field.converter].insert(field.key, value);
    }
    for (auto it = s.converterSettings.begin(); it != s.converterSettings.end();) {
        if (it->isEmpty())
            it = s.converterSettings.erase(it);
        else
            ++it;
    }
    s.bodyDynamicColor = m_bodyDynamicColor;
    s.bodyStaticColor = m_bodyStaticColor;
    s.bodyKinematicColor = m_bodyKinematicColor;
    s.unassignedShapeColor = m_unassignedShapeColor;
    s.sensorColor = m_sensorColor;
    s.sensorFillsBody = m_ui->sensorFillsBody->isChecked();
    s.physicsBorderWidth = m_ui->physicsBorderWidth->value();
    s.physicsFillAlpha = m_ui->physicsFillAlpha->value();
    s.jointFillAlpha = m_ui->jointFillAlpha->value();
    s.jointAnchorOpacity = m_jointAnchorOpacity->value();
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
    s.springsAsCoils = m_ui->springsAsCoils->isChecked();
    s.springPitch = m_ui->springPitch->value();
    s.springWidth = m_ui->springWidth->value();
    s.jointOutlineWidth = m_ui->jointOutlineWidth->value();
    s.undoDepth = m_ui->undoDepth->value();
    s.defaultEngineName = m_ui->defaultEngine->currentText();
    s.theme = m_ui->theme->currentData().toString();
    s.controls = m_ui->controls->currentData().toString();
    s.rulerUnit = Units::fromKey(m_ui->rulerUnit->currentData().toString());
    s.jointKindColors = m_jointKindColors;
    s.jointKindStyles = m_jointKindStyles;
    s.shotLightColor = m_shotLightColor;
    s.shotFullColor = m_shotFullColor;
    s.shotLineWidth = m_shotLineWidth->value();
    s.shotLineStyle = static_cast<Qt::PenStyle>(m_shotLineStyle->currentData().toInt());
    s.trailColor = m_trailColor;
    s.trailLineWidth = m_trailLineWidth->value();
    s.trailLineStyle = static_cast<Qt::PenStyle>(m_trailLineStyle->currentData().toInt());
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
    s.shapeStyles = m_shapeStyles;
    s.logFontFamily = m_logFontFamily;
    s.logFontSize = m_logFontSize;
    s.logColor = m_logColor;
    s.logCorner = m_logCorner;
    s.selectionLineStyle =
        static_cast<Qt::PenStyle>(m_ui->selectionLineStyle->currentData().toInt());
    s.selectionLineWidth = m_ui->selectionLineWidth->value();
    s.selectionColor = m_selectionColor;
    s.handleShape = static_cast<HandleShape>(m_ui->handleShape->currentData().toInt());
    s.sensorPattern = static_cast<Qt::BrushStyle>(m_ui->sensorPattern->currentData().toInt());
    s.handleSize = m_ui->handleSize->value();
    s.handleColor = m_handleColor;
    s.handleBorderWidth = m_ui->handleBorderWidth->value();
    s.handleBorderColor = m_handleBorderColor;
    return s;
}
