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
    , m_defaultBorderColor(current.defaultBorderColor)
    , m_defaultBodyColor(current.defaultBodyColor)
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

    m_ui->converterPath->setText(current.converterPath);
    m_converterSettings = current.converterSettings;
    connect(m_ui->converterPathBrowse, &QToolButton::clicked, this, [this] {
        const QString chosen = QFileDialog::getExistingDirectory(
            this, tr("Converters folder"), m_ui->converterPath->text());
        if (!chosen.isEmpty())
            m_ui->converterPath->setText(chosen);
    });
    // Point it somewhere else and the converters there are asked what they
    // want, without closing the dialog first.
    connect(m_ui->converterPath, &QLineEdit::editingFinished, this, [this] {
        rebuildExportTab(m_ui->converterPath->text());
    });
    rebuildExportTab(current.converterPath);

    m_ui->defaultBorderWidth->setValue(current.defaultBorderWidth);
    m_ui->defaultTransparency->setValue(qRound((1.0 - current.defaultBodyColor.alphaF()) * 100.0));
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
    bindSwatch(m_ui->sensorColorButton, m_sensorColor, tr("Choose Sensor Hatching Color"));
    bindSwatch(m_ui->physicsSelectionColorButton, m_physicsSelectionColor, tr("Choose Physics Selection Color"));
    bindSwatch(m_ui->bodyAxisXColorButton, m_bodyAxisXColor, tr("Choose X Axis Color"));
    bindSwatch(m_ui->bodyAxisYColorButton, m_bodyAxisYColor, tr("Choose Y Axis Color"));
    bindSwatch(m_ui->jointColorButton, m_jointColor, tr("Choose Joint Color"));
    bindSwatch(m_ui->jointOutlineColorButton, m_jointOutlineColor, tr("Choose Joint Outline Color"));
    bindSwatch(m_ui->jointSelectionColorButton, m_jointSelectionColor, tr("Choose Joint Selection Color"));

    bindSliderValue(m_ui->defaultTransparency, m_ui->defaultTransparencyLabel, tr("%"));
    bindSliderValue(m_ui->physicsFillAlpha, m_ui->physicsFillAlphaLabel, QString());
    bindSliderValue(m_ui->jointFillAlpha, m_ui->jointFillAlphaLabel, QString());
    bindSliderValue(m_ui->sleepShiftPercent, m_ui->sleepShiftPercentLabel, tr("%"));

    connect(m_ui->defaultTransparency, &QSlider::valueChanged, this, [this](int percent) {
        m_defaultBodyColor.setAlphaF(1.0 - percent / 100.0);
        m_ui->defaultBodyColorButton->setStyleSheet(colorSwatchStyle(m_defaultBodyColor));
    });

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

    // Now that every row exists, including the ones added just above.
    for (QGroupBox *group : { m_ui->bodyColorsGroup, m_ui->jointTypeColorsGroup,
                              m_ui->defaultStyleGroup, m_ui->handleStyleGroup,
                              m_ui->bodyAxesGroup, m_ui->jointDrawingGroup })
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

void OptionsDialog::rebuildExportTab(const QString &path)
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

    const QVector<SceneExporter::Converter> converters = SceneExporter::discover(path);
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
            about->setStyleSheet(QStringLiteral("color: #6f6f6f;"));
            form->addRow(about);
        }
        if (converter.settings.isEmpty()) {
            auto *none = new QLabel(tr("This converter has nothing to set."), page);
            none->setStyleSheet(QStringLiteral("color: #8f8f8f;"));
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
    for (const ConverterField &field : m_converterFields)
        s.converterSettings[field.converter].insert(field.key, fieldValue(field));
    s.bodyDynamicColor = m_bodyDynamicColor;
    s.bodyStaticColor = m_bodyStaticColor;
    s.bodyKinematicColor = m_bodyKinematicColor;
    s.unassignedShapeColor = m_unassignedShapeColor;
    s.sensorColor = m_sensorColor;
    s.sensorFillsBody = m_ui->sensorFillsBody->isChecked();
    s.physicsBorderWidth = m_ui->physicsBorderWidth->value();
    s.physicsFillAlpha = m_ui->physicsFillAlpha->value();
    s.jointFillAlpha = m_ui->jointFillAlpha->value();
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
    s.converterPath = m_ui->converterPath->text().trimmed();
    s.defaultEngineName = m_ui->defaultEngine->currentText();
    s.jointKindColors = m_jointKindColors;
    s.jointKindStyles = m_jointKindStyles;
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
    s.sensorPattern = static_cast<Qt::BrushStyle>(m_ui->sensorPattern->currentData().toInt());
    s.handleSize = m_ui->handleSize->value();
    s.handleColor = m_handleColor;
    s.handleBorderWidth = m_ui->handleBorderWidth->value();
    s.handleBorderColor = m_handleBorderColor;
    return s;
}
