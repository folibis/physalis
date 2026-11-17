#include "AssetFolder.h"
#include "HelpWindow.h"
#include "MainWindow.h"

#include "AboutDialog.h"


#include "ui_MainWindow.h"
#include "CanvasScene.h"
#include "FullScreenView.h"
#include "ViewLayersCombo.h"
#include "ExplosionItem.h"
#include "RayItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "RulerWidget.h"
#include "PlotPanel.h"
#include "PropertyPanel.h"
#include "SceneTree.h"
#include "RulesPanel.h"
#include "ShapeStyle.h"
#include "ControlsPanel.h"
#include "SceneControl.h"
#include "VariablesPanel.h"
#include "UndoStack.h"
#include "RectangleItem.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "Joint.h"
#include "SimulationController.h"
#include "SceneExporter.h"
#include "SceneScreenshot.h"
#include "Theme.h"

#include <QTextDocument>
#include <cmath>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QJsonArray>
#include <QStringList>
#include <QJsonObject>
#include <QJsonValue>
#include "SceneSerializer.h"
#include "Naming.h"
#include "EngineRegistry.h"
#include "Icons.h"

#include <QGraphicsView>
#include <QWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QClipboard>

#include <memory>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QCloseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QAction>
#include <QKeySequence>
#include <QPainter>
#include <QIcon>
#include <QWidget>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDockWidget>
#include <QTabWidget>
#include <QSettings>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QStatusBar>
#include <QLabel>
#include <QTransform>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QActionGroup>
#include <QShowEvent>

namespace {

// The view layers, by the names their settings are stored under.
const QString kLayerGrid = QStringLiteral("grid");
const QString kLayerJoints = QStringLiteral("joints");
const QString kLayerAxes = QStringLiteral("bodyAxes");
const QString kLayerRays = QStringLiteral("rays");
const QString kLayerExplosions = QStringLiteral("explosions");
const QString kLayerSleep = QStringLiteral("sleepShading");
const QString kLayerSensors = QStringLiteral("sensors");
const QString kLayerTrails = QStringLiteral("trails");

constexpr int kIconSize = 22;
constexpr int kButtonSize = 30;

QToolButton *squareButton(QToolBar *toolBar, QAction *action)
{
    auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action));
    if (button)
        button->setFixedSize(kButtonSize, kButtonSize);
    return button;
}

} // namespace

namespace {

// x2, x3 -- and the halves written as fractions, which read better on a
// toolbar than 0.5 does.
QString speedLabel(double factor)
{
    if (qFuzzyCompare(factor, 0.25))
        return QStringLiteral("×¼");
    if (qFuzzyCompare(factor, 0.5))
        return QStringLiteral("×½");
    return QStringLiteral("×%1").arg(factor, 0, 'g', 3);
}

QAction *separatorBefore(QToolBar *bar, QAction *anchor)
{
    const QList<QAction *> actions = bar->actions();
    const int index = actions.indexOf(anchor);
    if (index <= 0)
        return nullptr;
    QAction *previous = actions.at(index - 1);
    return previous->isSeparator() ? previous : nullptr;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ui(new Ui::MainWindow)
{
    // First: applySettings() below reaches into widgets the form owns.
    // setupUi() ends in connectSlotsByName(), which binds the on_<object>_
    // <signal>() slots to their actions.
    m_ui->setupUi(this);

    setAcceptDrops(true);

    // Here rather than in the form: uic would build plain icons, and these
    // have to follow the theme.
    const std::pair<QAction *, QIcon> actionIcons[] = {
        { m_ui->actionNewScene, Icons::newScene() },
        { m_ui->actionLoadScene, Icons::loadScene() },
        { m_ui->actionSaveScene, Icons::saveScene() },
        { m_ui->actionSaveSceneAs, Icons::saveSceneAs() },
        { m_ui->actionOptions, Icons::options() },
        { m_ui->actionExit, Icons::exit() },
        { m_ui->actionUndo, Icons::undo() },
        { m_ui->actionRedo, Icons::redo() },
        { m_ui->actionCopy, Icons::copy() },
        { m_ui->actionPaste, Icons::paste() },
        { m_ui->actionSaveScreenshot, Icons::screenshot() },
        { m_ui->actionAddRectangle, Icons::rectangle() },
        { m_ui->actionAddCircle, Icons::circle() },
        { m_ui->actionAddPolygon, Icons::polygon() },
        { m_ui->actionAddShape, Icons::add() },
        { m_ui->actionDelete, Icons::deleteShape() },
        { m_ui->actionMoveScale, Icons::move() },
        { m_ui->actionEditNodes, Icons::edit() },
        { m_ui->actionRotate, Icons::rotate() },
        { m_ui->actionAddRay, Icons::ray() },
        { m_ui->actionAddExplosion, Icons::explosion() },
        { m_ui->actionCreateBody, Icons::createBody() },
        { m_ui->actionDissolveBody, Icons::dissolveBody() },
        { m_ui->actionAddJoint, Icons::add() },
        { m_ui->actionDeleteJoint, Icons::deleteShape() },
        { m_ui->actionSimulate, Icons::simulate() },
        { m_ui->actionStep, Icons::step() },
        { m_ui->actionStop, Icons::stop() },
    };
    for (const auto &[action, icon] : actionIcons)
        action->setIcon(icon);

    m_scene = new CanvasScene(this);
    m_undo = new UndoStack(m_scene, this);
    connect(m_scene, &CanvasScene::editCommitted, this,
            [this](const QString &label, const QString &mergeKey) {
                m_undo->push(label, mergeKey);
            });

    m_ui->canvasView->setScene(m_scene);
    m_ui->rulerCorner->setFixedSize(RulerWidget::kThickness, RulerWidget::kThickness);
    m_ui->topRuler->setOrientation(RulerWidget::Orientation::Horizontal);
    m_ui->topRuler->setView(m_ui->canvasView);
    m_ui->leftRuler->setOrientation(RulerWidget::Orientation::Vertical);
    m_ui->leftRuler->setView(m_ui->canvasView);
    // Picked on either ruler, kept for both and written down: the two show
    // one scene and a window with its edges disagreeing would be unreadable.
    for (RulerWidget *ruler : { m_ui->topRuler, m_ui->leftRuler }) {
        connect(ruler, &RulerWidget::unitPicked, this, [this](Units::Unit unit) {
            if (m_ui->topRuler->unit() == unit)
                return;
            applyRulerUnit(unit);
            OptionsDialog::Settings s = currentSettingsSnapshot();
            saveSettingsToFile(s);
        });
    }

    buildSidePanelTabs();
    m_ui->propertyPanel->setScene(m_scene);
    m_ui->sceneTree->setScene(m_scene);
    m_ui->rulesPanel->setScene(m_scene);
    m_ui->variablesPanel->setScene(m_scene);
    m_ui->controlsPanel->setScene(m_scene);
    connect(m_ui->rulesPanel, &RulesPanel::incompleteCountChanged, this,
            &MainWindow::showIncompleteRules);
    // Following a rule back to what it names: the scene selects it, and the
    // panel that shows a selection comes to the front.
    connect(m_ui->rulesPanel, &RulesPanel::revealRequested, this, [this](const QString &name) {
        if (m_scene->revealObjectNamed(name))
            m_ui->sidePanel->setCurrentWidget(m_ui->propertyPanel);
    });
    showIncompleteRules(m_ui->rulesPanel->incompleteCount());

    // Kept, because the transport controls do not exist yet: they are built
    // with the toolbar further down, and what was saved for them is put on
    // them there.
    const OptionsDialog::Settings startup = loadSettingsFromFile();
    applySettings(startup);
    // The scene the window opens with is a new one, so it starts with them.
    applyFieldDefaults();

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &MainWindow::updatePasteAction);

    m_undo->reset();

    const QRect availableGeometry = QGuiApplication::primaryScreen()->availableGeometry();
    resize(availableGeometry.width() * 0.75, availableGeometry.height() * 0.8);
    // The extra 30 px is for the panel's own furniture -- the tab strip's
    // icons and the plot's left gutter -- which a plain fraction of the window
    // does not account for.
    m_ui->propertyDock->setMinimumWidth(static_cast<int>(width() * 0.17) + 60);
    resizeDocks({m_ui->propertyDock}, {static_cast<int>(width() * 0.25) + 60}, Qt::Horizontal);

    QToolBar *toolBar = m_ui->toolBar;
    toolBar->setIconSize(QSize(kIconSize, kIconSize));
    toolBar->layout()->setSpacing(4);
    for (QAction *action : toolBar->actions())
        squareButton(toolBar, action);

    // Not in the form: filled from the engine, and a menu with no place in the
    // menu bar cannot be a top-level child of the window -- uic reads one as
    // another central widget and it replaces the canvas.
    m_jointTypeMenu = new QMenu(tr("Add Joint"), this);
    m_jointTypeMenu->setObjectName(QStringLiteral("menuJointType"));

    m_ui->actionAddShape->setMenu(m_ui->menuAddShape);
    // Rebuilt as the menu opens rather than at startup, so a converter added
    // to the folder is there the moment it is looked for.
    m_ui->menuExport->setIcon(Icons::exportScene());
    connect(m_ui->menuExport, &QMenu::aboutToShow, this, &MainWindow::refreshExportMenu);
    m_ui->actionAddJoint->setMenu(m_jointTypeMenu);
    for (QAction *action : { static_cast<QAction *>(m_ui->actionAddShape), m_ui->actionAddJoint,
                             m_ui->actionAlign }) {
        if (auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action)))
            button->setPopupMode(QToolButton::InstantPopup);
    }

    buildAlignMenu();
    buildToolsMenu();
    rebuildRecentMenu();
    buildExamplesMenu();
    buildDocumentationMenu();

    // The dividers travel with their group, so a hidden group leaves no line.
    // Not actionDelete: a ray, a blast and a body are picked in Physics mode
    // and Delete has to reach those too. Hiding a QAction disables it as well,
    // so listing it here left the key dead the moment the mode changed.
    m_editModeActions << m_ui->actionAddShape
                      << m_ui->actionMoveScale << m_ui->actionEditNodes << m_ui->actionRotate
                      << m_ui->actionAlign << m_toolsAction
                      << separatorBefore(toolBar, m_ui->actionMoveScale)
                      << separatorBefore(toolBar, m_ui->actionCreateBody);
    // Listed in the order they sit on the bar, so this reads like the toolbar.
    m_physicsModeActions << m_ui->actionCreateBody << m_ui->actionAddExplosion
                         << m_ui->actionAddRay
                         << m_ui->actionDissolveBody
                         << m_ui->actionAddJoint << m_ui->actionDeleteJoint
                         << separatorBefore(toolBar, m_ui->actionAddJoint);
    m_editModeActions.removeAll(nullptr);
    m_physicsModeActions.removeAll(nullptr);

    // A run moves everything, and Stop puts it all back: whatever was added,
    // deleted, opened or saved in between would be a change to a moment of the
    // run, or would pull the scene out from under it. So all of it is off
    // until Stop -- and whichever code would turn one back on mid-run, a
    // selection handler or the undo stack, is overruled on the spot.
    m_alwaysOnWhenStopped = { m_ui->actionNewScene, m_ui->actionLoadScene, m_ui->actionOptions,
                              m_ui->actionAddShape, m_ui->actionAddRectangle,
                              m_ui->actionAddCircle, m_ui->actionAddPolygon,
                              m_ui->actionAddRay, m_ui->actionAddExplosion };
    m_lockedWhileRunning = m_alwaysOnWhenStopped;
    m_lockedWhileRunning << m_ui->actionSaveScene << m_ui->actionSaveSceneAs
                         << m_ui->actionUndo << m_ui->actionRedo
                         << m_ui->actionCopy << m_ui->actionPaste << m_ui->actionDuplicate
                         << m_ui->actionBringToFront << m_ui->actionBringForward
                         << m_ui->actionSendBackward << m_ui->actionSendToBack
                         << m_ui->actionDelete
                         << m_ui->actionMoveScale << m_ui->actionEditNodes << m_ui->actionRotate
                         << m_ui->actionAlign << m_ui->menuAlign->menuAction()
                         << m_ui->actionCreateBody << m_ui->actionDissolveBody
                         << m_ui->actionAddJoint << m_ui->actionDeleteJoint
                         << m_ui->menuAddShape->menuAction() << m_toolsAction;
    for (QAction *action : std::as_const(m_lockedWhileRunning)) {
        connect(action, &QAction::changed, this, [this, action] {
            if (action->isEnabled() && m_simulation && m_simulation->isActive())
                action->setEnabled(false);
        });
    }

    m_simulation = new SimulationController(m_scene, this);

    rebuildJointMenu();

    m_transportActions << toolBar->actions().constLast();

    // No engine chooser here: engines differ in what they offer -- their
    // joints, and half their properties -- so a scene is built for one and
    // keeps it. Which one a new scene gets is an Option; which one an opened
    // scene gets is in the file.
    useEngine(engineForNewScenes());

    toolBar->addAction(m_ui->actionSimulate);
    toolBar->addAction(m_ui->actionStep);
    toolBar->addAction(m_ui->actionStop);
    for (QAction *action : { m_ui->actionSimulate, m_ui->actionStep, m_ui->actionStop })
        squareButton(toolBar, action);
    connect(m_ui->actionSimulate, &QAction::triggered, this, [this] {
        switch (m_simulation->state()) {
        case SimulationController::State::Running:  m_simulation->pause();  break;
        case SimulationController::State::Stepping: m_simulation->resume(); break;
        case SimulationController::State::Stopped:
            m_simulation->start();
            // Keys reach whatever has the focus, and after clicking the
            // toolbar that is the toolbar. A rule watching the keyboard was
            // never given a chance.
            m_ui->canvasView->setFocus(Qt::OtherFocusReason);
            break;
        }
    });
    connect(m_ui->actionStep, &QAction::triggered, m_simulation, &SimulationController::stepFrame);
    connect(m_ui->actionStop, &QAction::triggered, m_simulation, &SimulationController::stop);

    m_speedCombo = new QComboBox(toolBar);
    m_speedCombo->setToolTip(tr("How fast the run plays against the clock: ×2 covers two seconds"
                                " of the world in one of ours, ×½ covers half. The step the solver"
                                " takes is the same either way -- this changes the pace it is"
                                " watched at, not the physics."));
    for (double factor : { 0.25, 0.5, 1.0, 2.0, 3.0, 4.0, 8.0 })
        m_speedCombo->addItem(speedLabel(factor), factor);
    m_speedCombo->setCurrentIndex(m_speedCombo->findData(1.0));
    m_transportActions << toolBar->addWidget(m_speedCombo);
    connect(m_speedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        const double factor = m_speedCombo->itemData(index).toDouble();
        m_simulation->setSpeed(factor);
        saveSettingsToFile(currentSettingsSnapshot());
    });

    // What a run shows. None of it touches the editor: joints and rays are
    // being placed there, and a switch that hid them would make them unusable.
    using RunLayer = CanvasScene::RunLayer;
    m_layers = new ViewLayersCombo(toolBar);
    m_layers->addLayer(kLayerGrid, tr("Grid"), m_scene->runLayer(RunLayer::Grid),
                       tr("The ruled background, once a run starts. While editing it is"
                          " always drawn -- that is what Show Grid in Options is for."));
    m_layers->addLayer(kLayerJoints, tr("Joints"), m_scene->runLayer(RunLayer::Joints),
                       tr("The joints and their anchors once a run starts, and the travel"
                          " a sliding joint is limited to."));
    m_layers->addLayer(kLayerAxes, tr("Body axes"), m_scene->runLayer(RunLayer::BodyAxes),
                       tr("The cross at each body's centre of mass, once a run starts."));
    m_layers->addLayer(kLayerRays, tr("Rays"), m_scene->runLayer(RunLayer::Rays),
                       tr("The rays and what they are looking at, once a run starts."));
    m_layers->addLayer(kLayerExplosions, tr("Explosions"),
                       m_scene->runLayer(RunLayer::Explosions),
                       tr("Where a blast goes off and how far it reaches, once a run"
                          " starts."));
    m_layers->addLayer(kLayerSensors, tr("Sensors"), m_scene->runLayer(RunLayer::Sensors),
                       tr("The pass-through shapes rules watch, once a run starts. They are"
                          " always drawn while editing -- that is when they are placed."));
    m_layers->addLayer(kLayerTrails, tr("Trails"), m_scene->runLayer(RunLayer::Trails),
                       tr("The line each body marked Draws a Trail leaves behind it."));
    m_layers->addLayer(kLayerSleep, tr("Sleep shading"),
                       m_scene->runLayer(RunLayer::SleepShading),
                       tr("Tint each body by whether the solver still has it awake. There"
                          " is nothing to tint outside a run."));
    m_transportActions << toolBar->addWidget(m_layers);
    connect(m_layers, &ViewLayersCombo::layerToggled, this,
            [this](const QString &key, bool on) {
                applyLayer(key, on);
                saveSettingsToFile(currentSettingsSnapshot());
            });

    m_fullScreenCheck = new QCheckBox(tr("Full Screen"), toolBar);
    m_fullScreenCheck->setToolTip(tr("Run the simulation on a screen of its own, with the"
                                     " field as large as it will go. Esc ends the run and"
                                     " comes back here; every other key goes to the scene."));
    m_transportActions << toolBar->addWidget(m_fullScreenCheck);
    connect(m_fullScreenCheck, &QCheckBox::toggled, this, [this](bool) {
        updateFullScreenView();
        saveSettingsToFile(currentSettingsSnapshot());
    });

    m_transportActions << m_ui->actionSimulate << m_ui->actionStep << m_ui->actionStop;

    connect(m_scene, &CanvasScene::selectedJointChanged, this, &MainWindow::onJointSelectionChanged);
    connect(m_scene, &CanvasScene::jointsChanged, this, &MainWindow::onJointSelectionChanged);
    connect(m_simulation, &SimulationController::stateChanged,
            this, &MainWindow::onSimulationStateChanged);

    // Now that they exist, the transport controls catch up with what was saved.
    syncTransportWidgets(startup);

    toolBar->addSeparator();

    m_scaleCombo = new QComboBox(this);
    m_scaleCombo->setEditable(true);
    m_scaleCombo->setInsertPolicy(QComboBox::NoInsert);
    m_scaleCombo->setToolTip(tr("Canvas zoom level -- pick a preset, type a value,"
                                " or Ctrl+wheel over the canvas"));
    m_scaleCombo->setMinimumWidth(72);
    for (int preset : {25, 50, 75, 100, 150, 200, 300, 400, 500})
        m_scaleCombo->addItem(tr("%1%").arg(preset));
    m_scaleCombo->setCurrentText(tr("%1%").arg(qRound(m_scene->currentScale())));
    connect(m_scaleCombo->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        QString text = m_scaleCombo->currentText();
        text.remove(QLatin1Char('%'));
        bool ok = false;
        const double value = text.toDouble(&ok);
        if (ok)
            m_scene->setCurrentScale(value);
        else
            m_scaleCombo->setCurrentText(tr("%1%").arg(qRound(m_scene->currentScale())));
    });
    connect(m_scaleCombo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        QString text = m_scaleCombo->itemText(index);
        text.remove(QLatin1Char('%'));
        m_scene->setCurrentScale(text.toDouble());
    });
    toolBar->addWidget(m_scaleCombo);

    m_resetScaleAction = toolBar->addAction(Icons::resetScale(), tr("Reset Zoom"));
    m_resetScaleAction->setToolTip(tr("Reset zoom to 100% (Ctrl+0)"));
    m_resetScaleAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    addAction(m_resetScaleAction); // a toolbar action is not reached by its key on its own
    connect(m_resetScaleAction, &QAction::triggered, this, [this] { m_scene->setCurrentScale(100.0); });
    squareButton(toolBar, m_resetScaleAction);

    // Taking a picture of the scene belongs with what is being looked at
    // rather than with saving the file, so it sits after the zoom.
    toolBar->addAction(m_ui->actionSaveScreenshot);
    squareButton(toolBar, m_ui->actionSaveScreenshot);

    auto *toolBarSpacer = new QWidget(toolBar);
    toolBarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(toolBarSpacer);

    auto *modeLabel = new QLabel(tr("Mode"), toolBar);
    modeLabel->setProperty("muted", true);
    modeLabel->setStyleSheet(QStringLiteral("padding-right: 10px;"));
    toolBar->addWidget(modeLabel);

    // A widget added straight to a toolbar is stretched to its full height, so
    // the track needs a wrapper whose layout carries the margins.
    auto *modeWrapper = new QWidget(toolBar);
    auto *modeWrapperLayout = new QHBoxLayout(modeWrapper);
    modeWrapperLayout->setContentsMargins(0, 6, 14, 6);

    auto *modeSwitch = new QWidget(modeWrapper);
    modeSwitch->setObjectName(QStringLiteral("ModeSwitch"));
    // The ID selector matters: an unqualified rule would cascade into the child
    // buttons and fight the per-button styling in onEditorModeChanged().
    modeSwitch->setStyleSheet(QStringLiteral(
        "#ModeSwitch { background: palette(midlight); border: 1px solid palette(mid);"
        " border-radius: 15px; }"));

    auto *modeLayout = new QHBoxLayout(modeSwitch);
    modeLayout->setContentsMargins(3, 3, 3, 3);
    modeLayout->setSpacing(2);

    for (EditorMode mode : EditorModes::kAll) {
        auto *button = new QToolButton(modeSwitch);
        button->setText(EditorModes::name(mode));
        button->setToolTip(EditorModes::tooltip(mode));
        button->setCheckable(true);
        button->setAutoRaise(false);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setCursor(Qt::PointingHandCursor);
        button->setEnabled(true);
        connect(button, &QToolButton::clicked, this, [this, mode] { m_scene->setEditorMode(mode); });
        m_modeButtons.insert(static_cast<int>(mode), button);
        modeLayout->addWidget(button);
    }
    modeWrapperLayout->addWidget(modeSwitch);
    toolBar->addWidget(modeWrapper);

    connect(m_undo, &UndoStack::changed, this, &MainWindow::updateUndoActions);
    connect(m_undo, &UndoStack::changed, this, &MainWindow::updateWindowTitle);
    m_scene->setLiveValueProvider([this](const QString &object, const QString &key) -> QVariant {
        if (!m_simulation)
            return {};
        return m_simulation->initialValue(object, key);
    });
    connect(m_scene, &CanvasScene::shapesChanged, this, &MainWindow::updateCopyAction);
    // Off by default: a field of labels is in the way of the drawing most of
    // the time. Remembered between runs of the program, since somebody who
    // wants them wants them every time.
    m_ui->actionShowObjectNames->setChecked(
        QSettings(settingsFilePath(), QSettings::IniFormat)
            .value(QStringLiteral("Panels/showObjectNames"), false)
            .toBool());
    m_scene->setObjectNamesShown(m_ui->actionShowObjectNames->isChecked());
    connect(m_ui->actionShowObjectNames, &QAction::toggled, this, [this](bool on) {
        m_scene->setObjectNamesShown(on);
        if (CanvasScene *run = m_simulation->runScene())
            run->setObjectNamesShown(on);
        QSettings(settingsFilePath(), QSettings::IniFormat)
            .setValue(QStringLiteral("Panels/showObjectNames"), on);
    });
    connect(m_scene, &CanvasScene::editorModeChanged, this, &MainWindow::onEditorModeChanged);
    connect(Theme::instance(), &Theme::changed, this,
            [this] { styleModeButtons(m_scene->editorMode()); });
    connect(m_scene, &CanvasScene::physicsSelectionChanged, this, &MainWindow::onPhysicsSelectionChanged);
    // Selecting an explosion is a different signal, and Remove depends on it.
    connect(m_scene, &CanvasScene::selectedRayChanged, this, [this](RayItem *) {
        updateCopyAction();
        updateDeleteAction();
    });
    connect(m_scene, &CanvasScene::selectedExplosionChanged, this,
            [this](ExplosionItem *) {
                updateCopyAction();
                updateDeleteAction();
            });
    connect(m_scene, &CanvasScene::selectedExplosionChanged, this,
            [this] { onPhysicsSelectionChanged(); });
    connect(m_scene, &CanvasScene::selectedRayChanged, this,
            [this] { onPhysicsSelectionChanged(); });
    connect(m_scene, &CanvasScene::createBodyRequested, this, &MainWindow::onCreateBody);
    connect(m_scene, &CanvasScene::bodiesChanged, this, &MainWindow::onPhysicsSelectionChanged);
    connect(m_scene, &CanvasScene::activeItemChanged, this, &MainWindow::onActiveItemChanged);
    // Picking a second shape does not change which one is active, so the
    // toolbar has to be told separately -- its wording depends on how many
    // shapes are in hand.
    connect(m_scene, &CanvasScene::editSelectionChanged, this,
            [this] { onActiveItemChanged(m_scene->activeItem()); });
    connect(m_scene, &CanvasScene::polygonDrawingChanged, this, &MainWindow::onPolygonDrawingChanged);
    connect(m_scene, &CanvasScene::activeItemChanged, m_ui->propertyPanel, &PropertyPanel::setActiveItem);
    connect(m_ui->propertyPanel, &PropertyPanel::fieldSettingsChanged, this, &MainWindow::onFieldSettingsChanged);

    connect(m_ui->actionMoveScale, &QAction::triggered, m_scene, &CanvasScene::switchActiveToSelected);
    connect(m_ui->actionEditNodes, &QAction::triggered, m_scene, &CanvasScene::switchActiveToEditing);
    connect(m_ui->actionRotate, &QAction::triggered, m_scene, &CanvasScene::switchActiveToRotating);
    connect(m_ui->actionDelete, &QAction::triggered, m_scene, &CanvasScene::deleteActiveItem);

    const auto showProperties = [this] {
        if (m_ui->sceneTree && m_ui->sceneTree->isDrivingSelection())
            return;
        m_ui->sidePanel->setCurrentWidget(m_ui->propertyPanel);
    };
    connect(m_ui->sceneTree, &SceneTree::propertiesRequested, this,
            [this] { m_ui->sidePanel->setCurrentWidget(m_ui->propertyPanel); });
    connect(m_scene, &CanvasScene::selectedJointChanged, this,
            [showProperties](Joint *joint) { if (joint) showProperties(); });
    connect(m_scene, &CanvasScene::physicsSelectionChanged, this,
            [this, showProperties] {
                if (!m_scene->physicsSelection().isEmpty())
                    showProperties();
            });
    connect(m_scene, &CanvasScene::selectedRayChanged, this,
            [showProperties](RayItem *ray) { if (ray) showProperties(); });
    connect(m_scene, &CanvasScene::selectedExplosionChanged, this,
            [showProperties](ExplosionItem *explosion) { if (explosion) showProperties(); });
    connect(m_scene, &CanvasScene::activeItemChanged, this,
            [showProperties](ShapeItem *item) { if (item) showProperties(); });

    connect(m_scene, &CanvasScene::scaleChanged, this, [this](qreal scale) {
        if (CanvasScene *run = m_simulation->runScene())
            run->setCurrentScale(scale);
        const qreal factor = scale / 100.0;
        m_ui->canvasView->setTransform(QTransform::fromScale(factor, factor));
        m_scaleCombo->setCurrentText(tr("%1%").arg(qRound(scale)));
    });
    m_ui->canvasView->setTransform(
        QTransform::fromScale(m_scene->currentScale() / 100.0, m_scene->currentScale() / 100.0));

    // Pinned to the canvas rather than drawn into the scene, so it stays the
    // same size whatever the zoom and never scrolls away.
    m_logOverlay = new QLabel(m_ui->canvasView);
    m_logOverlay->setObjectName(QStringLiteral("LogOverlay"));
    m_logOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    applyLogStyle();
    // Beside the log, and above it in stacking order: a caption is what the
    // scene wants read.
    m_caption = new QLabel(m_ui->canvasView);
    m_caption->setObjectName(QStringLiteral("SceneCaption"));
    m_caption->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_caption->setTextFormat(Qt::RichText);
    m_caption->setWordWrap(true);
    m_caption->setMaximumWidth(420);
    // The padding is a real margin and not a stylesheet one: heightForWidth()
    // counts contents margins and knows nothing about what a stylesheet adds,
    // so padding put there came out of the text's height instead of being
    // added to it -- and the last line was cut off.
    m_caption->setContentsMargins(9, 7, 9, 7);
    // It and the controls stand on the field, whose colour is the scene's and
    // not the theme's, behind a pale backing of their own -- so they keep a
    // light palette whatever the application is wearing.
    m_caption->setPalette(Theme::lightPalette());
    m_caption->setStyleSheet(QStringLiteral(
        "#SceneCaption { background: rgba(255, 255, 255, 200); border: 1px solid #b4b4b4;"
        " border-radius: 4px; }"));
    m_caption->hide();
    connect(m_scene, &CanvasScene::captionChanged, this, &MainWindow::updateCaption);

    m_controls = new QWidget(m_ui->canvasView);
    m_controls->setObjectName(QStringLiteral("SceneControls"));
    auto *controlColumn = new QVBoxLayout(m_controls);
    controlColumn->setContentsMargins(9, 7, 9, 7);
    controlColumn->setSpacing(4);
    m_controls->setPalette(Theme::lightPalette());
    m_controls->setStyleSheet(QStringLiteral(
        "#SceneControls { background: rgba(255, 255, 255, 200); border: 1px solid #b4b4b4;"
        " border-radius: 4px; }"));
    m_controls->hide();
    connect(m_scene, &CanvasScene::controlsChanged, this, &MainWindow::rebuildControls);
    connect(m_scene, &CanvasScene::controlValuesChanged, this, &MainWindow::refreshControls);

    m_ui->canvasView->installEventFilter(this);
    m_ui->canvasView->viewport()->installEventFilter(this);
    placeLogOverlay();
    m_logOverlay->hide();
    connect(m_scene, &CanvasScene::watchesChanged, this, &MainWindow::updateLogOverlay);
    connect(m_simulation, &SimulationController::stateChanged, this, &MainWindow::updateLogOverlay);
    connect(m_simulation, &SimulationController::stepped, this, &MainWindow::updateLogOverlay);
    connect(m_simulation, &SimulationController::stepped, this, &MainWindow::followFocusedBody);
    // Centred as soon as it is chosen, rather than at the next step: a run
    // that is paused would otherwise leave it where it was.
    connect(m_scene, &CanvasScene::focusedBodyChanged, this, &MainWindow::followFocusedBody);
    m_simulation->setRunSceneSetup([this](CanvasScene *run) {
        applySceneLook(run, currentSettingsSnapshot());
        run->setObjectNamesShown(m_scene->objectNamesShown());
        connect(run, &CanvasScene::captionChanged, this, &MainWindow::updateCaption);
        connect(run, &CanvasScene::controlValuesChanged, this, &MainWindow::refreshControls);
        connect(run, &CanvasScene::focusedBodyChanged, this, &MainWindow::followFocusedBody);
        connect(run, &CanvasScene::scaleChanged, m_scene, &CanvasScene::setCurrentScale);
    });
    // The measured rows in the property table read the same values the log
    // does, so they are refreshed on the same beat.
    connect(m_simulation, &SimulationController::stepped,
            m_ui->propertyPanel, &PropertyPanel::refreshValues);
    // The plot is fed by the run, not by this beat -- a sample a step is taken
    // in the controller. All that happens here is a repaint, and only while
    // the tab is the one showing.
    m_ui->plotPanel->attach(m_scene, m_simulation);
    const auto redrawPlot = [this] {
        if (m_ui->sidePanel->currentWidget() == m_ui->plotPanel)
            m_ui->plotPanel->refresh();
    };
    connect(m_simulation, &SimulationController::stepped, this, redrawPlot);
    connect(m_simulation, &SimulationController::stateChanged, this, redrawPlot);
    connect(m_scene, &CanvasScene::watchesChanged, this, redrawPlot);
    connect(m_ui->sidePanel, &QTabWidget::currentChanged, this,
            [this](int) { m_ui->plotPanel->refresh(); });
    updateLogOverlay();

    m_statusHelpLabel = new QLabel(this);
    statusBar()->addWidget(m_statusHelpLabel, 1);

    // Both are only wired to change signals, which do not fire until something
    // happens, so the initial state has to be applied by hand.
    onEditorModeChanged(m_scene->editorMode());
    onSimulationStateChanged();

    updatePasteAction();
    updateUndoActions();
    updateWindowTitle();
}

MainWindow::~MainWindow() = default;

// One scene file at a time: opening a second would close the first, and a
// drop of twenty would ask twenty times whether to save. Empty for anything
// else dragged over, which is then left for whatever else wants it.
QString MainWindow::sceneFileAmong(const QList<QUrl> &urls)
{
    if (urls.size() != 1 || !urls.first().isLocalFile())
        return {};
    const QString path = urls.first().toLocalFile();
    return QFileInfo(path).suffix().compare(QLatin1String("phys"), Qt::CaseInsensitive) == 0
               ? path
               : QString();
}

// The three events ask the same question of the same list, so they share it.
static QList<QUrl> urlsOf(const QDropEvent *event)
{
    const QMimeData *data = event->mimeData();
    return data && data->hasUrls() ? data->urls() : QList<QUrl>();
}

// What the drop does once it is ours. Separate from dropEvent because Qt
// delivers a drop through its own drag manager and not through sendEvent, so
// this is the furthest in a test can reach.
bool MainWindow::openDroppedUrls(const QList<QUrl> &urls)
{
    const QString path = sceneFileAmong(urls);
    if (path.isEmpty())
        return false;

    // Refused mid-run: stopping somebody's simulation to load a file they
    // dropped on the way past is not what a drop asks for.
    if (m_simulation && m_simulation->isActive()) {
        statusBar()->showMessage(tr("Stop the simulation before opening a scene"), 4000);
        return false;
    }
    return openScene(path);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (sceneFileAmong(urlsOf(event)).isEmpty())
        return;
    event->acceptProposedAction();
}

// Accepted again on every move, or the drop is refused however the enter went.
void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (sceneFileAmong(urlsOf(event)).isEmpty())
        return;
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = urlsOf(event);
    if (sceneFileAmong(urls).isEmpty())
        return;
    event->acceptProposedAction();
    openDroppedUrls(urls);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_didInitialCenter) {
        m_didInitialCenter = true;
        m_ui->canvasView->centerOn(0, 0);
    }
}

void MainWindow::on_actionUndo_triggered()
{
    m_undo->undo();
}

void MainWindow::on_actionRedo_triggered()
{
    m_undo->redo();
}

void MainWindow::on_actionAddRectangle_triggered()
{
    const QPointF center = m_ui->canvasView->mapToScene(m_ui->canvasView->viewport()->rect().center());
    ShapeItem *shape = m_scene->addRectangle(center);
    m_scene->notifyEdit(tr("Add %1").arg(shape->name()));
}

void MainWindow::on_actionAddRay_triggered()
{
    const QPointF center = m_ui->canvasView->mapToScene(m_ui->canvasView->viewport()->rect().center());
    RayItem *ray = m_scene->addRay(center);
    m_scene->notifyEdit(tr("Add %1").arg(ray->name()));
}

void MainWindow::on_actionAddExplosion_triggered()
{
    const QPointF center = m_ui->canvasView->mapToScene(m_ui->canvasView->viewport()->rect().center());
    ExplosionItem *explosion = m_scene->addExplosion(center);
    m_scene->notifyEdit(tr("Add %1").arg(explosion->name()));
}

void MainWindow::on_actionAddCircle_triggered()
{
    const QPointF center = m_ui->canvasView->mapToScene(m_ui->canvasView->viewport()->rect().center());
    ShapeItem *shape = m_scene->addCircle(center);
    m_scene->notifyEdit(tr("Add %1").arg(shape->name()));
}

void MainWindow::on_actionAddPolygon_triggered()
{
    m_scene->startPolygonDrawing();
    m_ui->canvasView->setFocus();
}

void MainWindow::onPolygonDrawingChanged(bool drawing)
{
    if (drawing) {
        m_statusHelpLabel->setText(
            tr("Click to add points • Enter to finish as an open shape"
               " • Shift+Enter to close it • Escape to cancel"));
    } else {
        onActiveItemChanged(m_scene->activeItem());
    }
}

QString MainWindow::engineForNewScenes() const
{
    const QStringList available = physics::EngineRegistry::availableEngines();
    if (available.contains(m_defaultEngineName))
        return m_defaultEngineName;
    // Nothing chosen yet, or what was chosen is no longer installed.
    return available.value(0);
}

void MainWindow::useEngine(const QString &name)
{
    // Taking up the engine already in use is nothing to do. A file naming the
    // engine this window is on came through here on its way open and had every
    // panel that asks the catalogue build itself a second time.
    if (!name.isEmpty() && m_scene->simulationEngineName() == name
        && m_simulation->engineName() == name) {
        return;
    }

    m_scene->setSimulationEngineName(name);
    m_simulation->setEngineName(name);
    // Everything the engine has a say in, filled again from the new one: the
    // joints it offers, the property rows it publishes, and the conditions and
    // actions a rule can be written from.
    rebuildJointMenu();
    if (m_ui->propertyPanel)
        m_ui->propertyPanel->setActiveItem(m_scene->activeItem());
    m_scene->notifyRulesChanged();
    updateTransportActions();
}

bool MainWindow::confirmCloseForEngine(const QString &name)
{
    // A test drives the window itself and has nobody to answer a dialog, the
    // same as confirmDiscardChanges.
    if (qEnvironmentVariableIsSet("PHYSALIS_SETTINGS"))
        return true;

    const QString current = m_scene->simulationEngineName();
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Change Engine"));
    box.setText(tr("This scene is built for %1.").arg(current));
    box.setInformativeText(
        tr("Its joints, and some of its settings, belong to that engine, so switching to"
           " %1 closes the scene and starts an empty one.").arg(name));

    QPushButton *save = nullptr;
    if (!m_undo->isClean())
        save = box.addButton(tr("Save and Close"), QMessageBox::AcceptRole);
    QPushButton *close = box.addButton(tr("Close Scene"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(save ? save : close);
    box.exec();

    if (save && box.clickedButton() == save)
        return onSaveScene();   // false if the save failed, or Save As was cancelled
    return box.clickedButton() == close;
}

bool MainWindow::adoptEngine(const QString &name)
{
    if (name.isEmpty() || name == m_scene->simulationEngineName())
        return true;

    // An engine change is not a change of mind about the open scene: its joint
    // types belong to the engine it was built for and mean nothing to another
    // one. An empty scene has nothing to lose, so it is switched in silence.
    const bool empty = m_scene->shapes().isEmpty() && m_scene->joints().isEmpty()
                       && m_scene->rules().isEmpty();
    if (!empty) {
        if (!confirmCloseForEngine(name))
            return false;
        m_simulation->stop();
        m_scene->clearContents();
        m_scene->setEditorMode(EditorMode::Edit);
        m_scenePath.clear();
        m_undo->reset();
        updateWindowTitle();
    }

    useEngine(name);
    return true;
}

void MainWindow::rebuildJointMenu()
{
    m_jointTypeMenu->clear();
    if (auto engine = physics::EngineRegistry::create(m_simulation->engineName())) {
        for (const physics::JointType &type : engine->jointTypes()) {
            QAction *action = m_jointTypeMenu->addAction(type.label);
            action->setToolTip(type.description);
            const QString typeId = type.id;
            connect(action, &QAction::triggered, this, [this, typeId] { onAddJoint(typeId); });
        }
    }
}

void MainWindow::onAddJoint(const QString &typeId)
{
    auto engine = physics::EngineRegistry::create(m_simulation->engineName());
    if (!engine)
        return;

    physics::JointType type;
    bool found = false;
    for (const physics::JointType &candidate : engine->jointTypes()) {
        if (candidate.id == typeId) {
            type = candidate;
            found = true;
            break;
        }
    }
    if (!found)
        return;

    QVector<PhysicsBody *> bodies;
    for (ShapeItem *shape : m_scene->physicsSelection()) {
        if (PhysicsBody *body = shape->body()) {
            if (!bodies.contains(body))
                bodies.append(body);
        }
    }

    // How many bodies this kind of joint joins is the engine's to say. Most
    // hold one thing to another; one holds a body to a point in the world and
    // has no second body to ask for.
    const int wanted = qBound(1, type.bodyCount, 2);
    if (bodies.size() != wanted) {
        QMessageBox::information(
            this, tr("Add Joint"),
            wanted == 1
                ? tr("%1 holds one body to a point in the world, and %2 %3 selected.\n\n"
                     "Click a shape to select its body.")
                      .arg(type.label)
                      .arg(bodies.size())
                      .arg(bodies.size() == 1 ? tr("is") : tr("are"))
                : tr("A joint connects two bodies, and %1 %2 selected.\n\n"
                     "Click a shape to select its body, then Shift+Click a shape of the"
                     " other one.")
                      .arg(bodies.size())
                      .arg(bodies.size() == 1 ? tr("is") : tr("are")));
        return;
    }

    PhysicsBody *second = wanted > 1 ? bodies.at(1) : nullptr;
    Joint *joint = m_scene->createJoint(type.id, bodies.at(0), second,
                                        type.anchorCount, type.defaultValues());
    if (!joint)
        return;

    m_scene->selectJoint(joint);
    m_scene->notifyEdit(tr("Add %1").arg(joint->name()));
    statusBar()->showMessage(
        second ? tr("Added %1 between %2 and %3")
                     .arg(joint->name(), bodies.at(0)->name(), second->name())
               : tr("Added %1 on %2 -- move its target with a rule")
                     .arg(joint->name(), bodies.at(0)->name()),
        4000);
}

void MainWindow::on_actionDeleteJoint_triggered()
{
    if (Joint *joint = m_scene->selectedJoint()) {
        const QString name = joint->name();
        m_scene->destroyJoint(joint);
        m_scene->notifyEdit(tr("Delete %1").arg(name));
        statusBar()->showMessage(tr("Removed %1").arg(name), 4000);
    }
}

void MainWindow::onJointSelectionChanged()
{
    const bool physicsMode = m_scene->editorMode() == EditorMode::Physics;
    const bool simulating = m_simulation && m_simulation->isActive();
    if (m_ui->actionAddJoint)
        m_ui->actionAddJoint->setEnabled(physicsMode && !simulating && !m_jointTypeMenu->isEmpty());
    if (m_ui->actionDeleteJoint)
        m_ui->actionDeleteJoint->setEnabled(physicsMode && !simulating
                                        && m_scene->selectedJoint() != nullptr);

    // Safe to call back into: it only reads the scene.
    onPhysicsSelectionChanged();
}

void MainWindow::onCreateBody(bool asStatic)
{
    // The two the canvas offers by itself: a plain double-click means the body
    // that moves, Ctrl means the one that does not.
    createBodyOfType(asStatic ? physics::BodyType::Static : physics::BodyType::Dynamic);
}

void MainWindow::createBodyOfType(physics::BodyType type)
{
    const QStringList problems = m_scene->solidBodyProblems(m_scene->physicsSelection());
    // Alongside a filled shape such an outline goes unnoticed: the body moves,
    // and the outline is only edges with no share of its mass.
    QStringList splittable;
    for (ShapeItem *shape : m_scene->physicsSelection()) {
        if (m_scene->canSplitIntoConvexPieces(shape))
            splittable << shape->name();
    }
    const QString splitHint =
        splittable.isEmpty()
            ? QString()
            : tr(" %1 can only be an outline: right-click it and choose Split into Convex"
                 " Pieces to make it solid.")
                  .arg(splittable.join(QStringLiteral(", ")));

    PhysicsBody *body = m_scene->createBodyFromSelection();
    if (!body)
        return;

    // Only area gives mass, and a body without mass is dropped at the start of
    // every run. The scene has already defaulted such a body to Kinematic --
    // the useful kind, driven rather than pushed -- and that is a default and
    // not a rule: the Body panel will set it Dynamic afterwards, and a body
    // gains area the moment a filled shape joins it. So the type asked for is
    // honoured wherever it can be, and where the default stood in, the status
    // bar says so rather than a dialog refusing the request.
    const bool defaulted = type == physics::BodyType::Dynamic
                           && body->props().type == physics::BodyType::Kinematic;
    if (!defaulted) {
        body->props().type = type;
    }
    body->notifyPropertyChanged();

    m_scene->notifyEdit(tr("Create %1").arg(body->name()));
    if (defaulted) {
        statusBar()->showMessage(
            problems.isEmpty()
                ? tr("Created %1 as Kinematic — an outline has no area, so no mass."
                     " The Body panel will make it Dynamic.")
                      .arg(body->name())
                : tr("Created %1 as Kinematic — %2. The Body panel will make it Dynamic.")
                      .arg(body->name(), problems.join(QStringLiteral("; ")))
                      + splitHint,
            8000);
    } else {
        statusBar()->showMessage(tr("Created %1 from %2 shape(s).")
                                         .arg(body->name()).arg(body->shapes().size())
                                     + splitHint,
                                 splitHint.isEmpty() ? 4000 : 8000);
    }

    m_ui->propertyPanel->showSection(tr("Body"));
}

void MainWindow::on_actionDissolveBody_triggered()
{
    // One button for whatever is selected: an explosion is deleted outright,
    // a body (sensor or not) is dissolved back into its shapes.
    const QVector<RayItem *> rays = m_scene->raySelection();
    const QVector<ExplosionItem *> blasts = m_scene->explosionSelection();
    if (!rays.isEmpty() || !blasts.isEmpty()) {
        const int count = int(rays.size() + blasts.size());
        const QString what = count == 1
                                 ? (rays.isEmpty() ? blasts.first()->name() : rays.first()->name())
                                 : tr("%n object(s)", nullptr, count);
        for (RayItem *ray : rays)
            m_scene->removeRay(ray);
        for (ExplosionItem *explosion : blasts)
            m_scene->removeExplosion(explosion);
        m_scene->notifyEdit(tr("Remove %1").arg(what));
        statusBar()->showMessage(tr("Removed %1").arg(what), 4000);
        return;
    }

    if (PhysicsBody *body = m_scene->commonSelectedBody()) {
        const QString name = body->name();
        m_scene->destroyBody(body);
        m_scene->notifyEdit(tr("Dissolve %1").arg(name));
        statusBar()->showMessage(tr("Dissolved %1").arg(name), 4000);
    }
}

void MainWindow::onPhysicsSelectionChanged()
{
    const bool physicsMode = m_scene->editorMode() != EditorMode::Edit;
    const int picked = m_scene->physicsSelection().size();

    const bool simulating = m_simulation && m_simulation->isActive();
    m_ui->actionCreateBody->setEnabled(physicsMode && picked > 0 && !simulating
                                   && !m_scene->selectionIsWholeBody());
    m_ui->actionDissolveBody->setEnabled(
        physicsMode && !simulating
        && (m_scene->commonSelectedBody() != nullptr
            || m_scene->selectedExplosion() != nullptr
            || m_scene->selectedRay() != nullptr));
    updateCopyAction();
    updateTransportActions();

    if (!physicsMode)
        return;

    if (simulating) {
        m_statusHelpLabel->setText(
            m_simulation->isRunning()
                ? tr("Running • Pause holds it"
                     " • Stop puts every shape back where it started")
                : tr("Held • Step advances one frame • Simulate resumes"
                     " • Stop puts every shape back where it started"));
        return;
    }

    if (Joint *joint = m_scene->selectedJoint()) {
        m_statusHelpLabel->setText(
            tr("%1 selected • Drag either end point to move it"
               " • Delete Joint removes it • Click elsewhere to go back to bodies")
                .arg(joint->name()));
    } else if (m_scene->bodies().isEmpty() && picked == 0) {
        m_statusHelpLabel->setText(
            tr("Click a shape to select it • Shift+Click to add more"
               " • Double-click makes a dynamic body, Ctrl+double-click a static one"
               " • Right-click to choose the kind"));
    } else if (picked == 0) {
        m_statusHelpLabel->setText(
            tr("Click a shape to select it • Shift+Click to add more"
               " • Double-click makes a dynamic body, Ctrl+double-click a static one"
               " • Right-click to choose the kind"));
    } else if (PhysicsBody *body = m_scene->commonSelectedBody()) {
        m_statusHelpLabel->setText(
            m_scene->selectionIsWholeBody()
                ? tr("%1 shape(s) selected — all of %2 • Add Joint connects it to another body"
                     " • Dissolve Body breaks it up").arg(picked).arg(body->name())
                : tr("%1 of %2's shapes selected • Create Body splits them into a body of their"
                     " own • Dissolve Body breaks up %2").arg(picked).arg(body->name()));
    } else {
        m_statusHelpLabel->setText(
            tr("%1 shape(s) selected, not yet in a body • Create Body groups them into one"
               " • Double-click makes it dynamic, Ctrl+double-click static"
               " • Right-click to choose the kind").arg(picked));
    }
}

namespace {
const char *kSceneFilter =
    QT_TR_NOOP("Physalis Scene (*.phys);;Older Scenes (*.scene *.scene.json *.json);;All Files (*)");
}

// What the clipboard holds, as shapes nobody owns yet -- the caller takes
// them. A copy writes an array even for a single shape; a bare shape object is
// what an older build wrote, and is still read.
QVector<ShapeItem *> MainWindow::clipboardShapes() const
{
    QVector<ShapeItem *> shapes;
    QJsonParseError parseError {};
    const QJsonDocument document =
        QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return shapes;

    const QJsonObject root = document.object();
    const QJsonArray array = root.value(QStringLiteral("shapes")).toArray();
    if (array.isEmpty()) {
        if (ShapeItem *one = SceneSerializer::shapeFromJson(root))
            shapes.append(one);
        return shapes;
    }
    for (const QJsonValue &value : array) {
        if (ShapeItem *shape = SceneSerializer::shapeFromJson(value.toObject()))
            shapes.append(shape);
    }
    return shapes;
}

bool MainWindow::clipboardHasShape() const
{
    // Asked of the same code paste itself uses, rather than sniffing the JSON.
    const QVector<ShapeItem *> shapes = clipboardShapes();
    qDeleteAll(shapes);
    if (!shapes.isEmpty())
        return true;

    // A ray and a blast are made by the scene, so there is nothing to build
    // and throw away here -- only the arrays to look for.
    QJsonParseError parseError {};
    const QJsonDocument document =
        QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonObject root = document.object();
    return !root.value(QStringLiteral("rays")).toArray().isEmpty()
           || !root.value(QStringLiteral("explosions")).toArray().isEmpty();
}

// Copy takes whatever is picked, so what is picked is what decides it: a
// shape in Edit mode, a ray or a blast outside it. Said in one place because
// three handlers used to set it and none of them knew about the other two.
// Delete takes whatever is picked, so what is picked is what decides it: the
// active shape in Edit mode, the ray or the blast selected outside it. Said in
// one place because three handlers used to set it and none knew about the
// others -- which is how a selected ray ended up with Delete greyed out.
void MainWindow::updateDeleteAction()
{
    if (!m_ui->actionDelete)
        return;
    const bool running = m_simulation && m_simulation->isActive();
    const bool want = !running
                      && (m_scene->activeItem() || m_scene->selectedRay()
                          || m_scene->selectedExplosion());
    m_ui->actionDelete->setEnabled(want);
}

void MainWindow::updateCopyAction()
{
    if (!m_ui->actionCopy)
        return;
    const bool running = m_simulation && m_simulation->isActive();
    // The same question Copy itself asks. Reading activeItem() here asked
    // Edit mode's selection whatever the mode was, so picking a mechanism in
    // Physics mode left Copy greyed out and Ctrl+C did nothing.
    m_ui->actionCopy->setEnabled(!running
                                 && (!selectedShapes().isEmpty() || m_scene->selectedRay()
                                     || m_scene->selectedExplosion()));
    // Duplicate carries shapes and the mechanism around them, and nothing
    // else, so it asks a narrower question than Copy's.
    if (m_ui->actionDuplicate)
        m_ui->actionDuplicate->setEnabled(!running && !selectedShapes().isEmpty());
    if (m_ui->actionSelectAll)
        m_ui->actionSelectAll->setEnabled(!m_scene->shapes().isEmpty());
    // Stacking needs something picked and more than one shape to order it
    // against; asking the scene whether the move would change anything would
    // mean working it out twice per selection change.
    const bool canRestack = !running && !selectedShapes().isEmpty()
                            && m_scene->shapes().size() > 1;
    for (QAction *action : { m_ui->actionBringToFront, m_ui->actionBringForward,
                             m_ui->actionSendBackward, m_ui->actionSendToBack }) {
        if (action)
            action->setEnabled(canRestack);
    }
}

void MainWindow::updatePasteAction()
{
    if (!m_ui->actionPaste)
        return;
    // Not Edit mode's alone any more: a ray and a blast are picked outside
    // it, and a scene holds all three whichever mode is showing.
    m_ui->actionPaste->setEnabled(!(m_simulation && m_simulation->isActive())
                                  && clipboardHasShape());
}

void MainWindow::addAnchorActions(QMenu *menu, Joint *joint, bool simulating)
{
    const auto moveTo = [this, joint](Joint::End end, PhysicsBody *body) {
        if (!body)
            return;
        joint->setAnchorScenePos(end, body->centerOfMassScenePos());
        m_scene->update();
        m_scene->notifyEdit(tr("Center %1 on %2").arg(joint->name(), body->name()));
    };

    PhysicsBody *bodyA = joint->bodyA();
    PhysicsBody *bodyB = joint->bodyB();

    if (joint->anchorCount() == 1) {
        // One shared pin, so the choice is which body it turns about.
        if (bodyA) {
            QAction *a = menu->addAction(tr("Move Anchor to %1 Center").arg(bodyA->name()),
                                         this, [moveTo, bodyA] { moveTo(Joint::End::A, bodyA); });
            a->setEnabled(!simulating);
        }
        if (bodyB) {
            QAction *b = menu->addAction(tr("Move Anchor to %1 Center").arg(bodyB->name()),
                                         this, [moveTo, bodyB] { moveTo(Joint::End::A, bodyB); });
            b->setEnabled(!simulating);
        }
        return;
    }

    // Two anchors: each end has exactly one sensible destination.
    if (bodyA) {
        QAction *a = menu->addAction(tr("Move Anchor A to %1 Center").arg(bodyA->name()),
                                     this, [moveTo, bodyA] { moveTo(Joint::End::A, bodyA); });
        a->setEnabled(!simulating);
    }
    if (bodyB) {
        QAction *b = menu->addAction(tr("Move Anchor B to %1 Center").arg(bodyB->name()),
                                     this, [moveTo, bodyB] { moveTo(Joint::End::B, bodyB); });
        b->setEnabled(!simulating);
    }
    if (bodyA && bodyB) {
        QAction *both = menu->addAction(tr("Move Both Anchors to Their Centers"), this,
                                        [moveTo, bodyA, bodyB] {
                                            moveTo(Joint::End::A, bodyA);
                                            moveTo(Joint::End::B, bodyB);
                                        });
        both->setEnabled(!simulating);
    }
}

void MainWindow::onOriginsToCenterOfMass()
{
    QVector<PhysicsBody *> bodies;
    for (ShapeItem *shape : m_scene->physicsSelection()) {
        if (PhysicsBody *body = shape->body()) {
            if (!bodies.contains(body))
                bodies.append(body);
        }
    }

    if (bodies.isEmpty()) {
        statusBar()->showMessage(tr("Select a shape that belongs to a body first"), 4000);
        return;
    }

    int moved = 0;
    for (PhysicsBody *body : std::as_const(bodies)) {
        // One point per body, in scene coordinates, mapped into each shape's
        // own frame. setOrigin() compensates the position, so this changes
        // what the shapes turn about without moving them.
        const QPointF centre = body->centerOfMassScenePos();
        for (ShapeItem *shape : body->shapes()) {
            shape->setOrigin(shape->mapFromScene(centre));
            ++moved;
        }
    }

    m_scene->notifyEdit(tr("Center origins on mass"));
    statusBar()->showMessage(tr("Moved %1 origin(s) onto the centre of mass of %2 body(s)")
                                 .arg(moved).arg(bodies.size()), 4000);
}

void MainWindow::updateUndoActions()
{
    if (!m_ui->actionUndo || !m_ui->actionRedo || !m_undo)
        return;

    const bool running = m_simulation && m_simulation->isActive();
    m_ui->actionUndo->setEnabled(m_undo->canUndo() && !running);
    m_ui->actionRedo->setEnabled(m_undo->canRedo() && !running);

    const QString undoLabel = m_undo->undoLabel();
    const QString redoLabel = m_undo->redoLabel();
    m_ui->actionUndo->setText(undoLabel.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(undoLabel));
    m_ui->actionRedo->setText(redoLabel.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(redoLabel));

    // Same answer as the star in the title: with nothing changed there is
    // nothing to write. Save As stays open -- saving a copy of an untouched
    // scene is a reasonable thing to ask for. Neither while a run is going:
    // it moves everything, and what it would save is a moment of the run.
    if (m_ui->actionSaveScene)
        m_ui->actionSaveScene->setEnabled(!m_undo->isClean() && !running);
    if (m_ui->actionSaveSceneAs)
        m_ui->actionSaveSceneAs->setEnabled(!running);
}

void MainWindow::on_actionCopy_triggered()
{
    // Whatever is picked, of whatever kind: shapes in Edit mode, and the ray
    // or the blast that is selected outside it. They go into the same object
    // the file uses, so one clipboard carries any of them.
    const QVector<ShapeItem *> picked = selectedShapes();
    const QVector<RayItem *> rays = m_scene->raySelection();
    const QVector<ExplosionItem *> blasts = m_scene->explosionSelection();
    if (picked.isEmpty() && rays.isEmpty() && blasts.isEmpty())
        return;

    int jointsLeftOut = 0;
    QJsonObject root;
    if (!picked.isEmpty()) {
        // The whole mechanism, not the shapes on their own: the bodies they
        // belong to and the joints between those bodies travel with them.
        root = SceneSerializer::fragmentToJson(m_scene, picked, &jointsLeftOut);
    }
    if (!rays.isEmpty()) {
        QJsonArray written;
        for (RayItem *ray : rays)
            written.append(SceneSerializer::rayToJson(ray));
        root.insert(QStringLiteral("rays"), written);
    }
    if (!blasts.isEmpty()) {
        QJsonArray written;
        for (ExplosionItem *explosion : blasts)
            written.append(SceneSerializer::explosionToJson(explosion));
        root.insert(QStringLiteral("explosions"), written);
    }
    const QJsonDocument document(root);
    QGuiApplication::clipboard()->setText(QString::fromUtf8(document.toJson(QJsonDocument::Compact)));

    const int count = int(picked.size() + rays.size() + blasts.size());
    QString what;
    if (count > 1) {
        what = picked.isEmpty() ? tr("%n object(s)", nullptr, count)
                                : tr("%n shapes", nullptr, int(picked.size()));
    } else if (!picked.isEmpty()) {
        what = picked.first()->name();
    } else if (!rays.isEmpty()) {
        what = rays.first()->name();
    } else {
        what = blasts.first()->name();
    }
    m_pasteCount = 0;
    updatePasteAction();
    statusBar()->showMessage(
        jointsLeftOut == 0
            ? tr("Copied %1").arg(what)
            : tr("Copied %1 — %n joint(s) left out, held to a body that was not picked",
                 nullptr, jointsLeftOut).arg(what),
        jointsLeftOut == 0 ? 4000 : 6000);
}

void MainWindow::on_actionSaveScreenshot_triggered()
{
    // Beside the scene's own file, named after it, unless a picture has been
    // saved already -- then where that one went.
    QString suggested = m_lastScreenshotPath;
    if (suggested.isEmpty()) {
        const QFileInfo scene(m_scenePath);
        suggested = m_scenePath.isEmpty() ? QStringLiteral("scene.png")
                                          : scene.absolutePath() + QLatin1Char('/') + scene.completeBaseName()
                                                + QStringLiteral(".png");
    }
    QString chosenFilter;
    QString path = QFileDialog::getSaveFileName(this, tr("Save Screenshot"), suggested,
                                                SceneScreenshot::fileFilters().join(QStringLiteral(";;")),
                                                &chosenFilter);
    if (path.isEmpty())
        return;
    // A name typed without an extension takes the chosen format's.
    if (QFileInfo(path).suffix().isEmpty()) {
        const int star = chosenFilter.indexOf(QStringLiteral("*."));
        path += star >= 0 ? chosenFilter.mid(star + 1).chopped(1) : QStringLiteral(".png");
    }

    QString error;
    if (!SceneScreenshot::save(m_scene, path, &error)) {
        QMessageBox::warning(this, tr("Save Screenshot"),
                             tr("Couldn't save %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    m_lastScreenshotPath = path;
    statusBar()->showMessage(tr("Saved a screenshot to %1").arg(QDir::toNativeSeparators(path)), 4000);
}

void MainWindow::on_actionPaste_triggered()
{
    QJsonParseError parseError {};
    const QJsonDocument document =
        QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return; // clipboard holds something else entirely; not an error

    const QJsonObject root = document.object();
    const bool haveShapes = !root.value(QStringLiteral("shapes")).toArray().isEmpty();
    const QJsonArray rays = root.value(QStringLiteral("rays")).toArray();
    const QJsonArray explosions = root.value(QStringLiteral("explosions")).toArray();
    if (!haveShapes && rays.isEmpty() && explosions.isEmpty())
        return;

    // The same offset for every one of them, so a group pasted keeps the
    // arrangement it was copied in.
    constexpr qreal kPasteOffset = 20.0;
    const qreal offset = kPasteOffset * ++m_pasteCount;

    // Everything pasted ends up picked, of whichever kinds were on the
    // clipboard -- it used to keep only the last of each, so pasting four
    // blasts left one selected and the other three to be found by hand.
    QVector<RayItem *> madeRays;
    QVector<ExplosionItem *> madeBlasts;
    for (const QJsonValue &value : rays) {
        QJsonObject one = value.toObject();
        one.insert(QStringLiteral("x"), one.value(QStringLiteral("x")).toDouble() + offset);
        one.insert(QStringLiteral("y"), one.value(QStringLiteral("y")).toDouble() + offset);
        if (RayItem *ray = SceneSerializer::rayFromJson(m_scene, one)) {
            ray->setName(Naming::nextName(QStringLiteral("ray"), m_scene->takenNames()));
            madeRays.append(ray);
        }
    }
    for (const QJsonValue &value : explosions) {
        QJsonObject one = value.toObject();
        one.insert(QStringLiteral("x"), one.value(QStringLiteral("x")).toDouble() + offset);
        one.insert(QStringLiteral("y"), one.value(QStringLiteral("y")).toDouble() + offset);
        if (ExplosionItem *blast = SceneSerializer::explosionFromJson(m_scene, one)) {
            blast->setName(Naming::nextName(QStringLiteral("explosion"), m_scene->takenNames()));
            madeBlasts.append(blast);
        }
    }
    m_scene->clearCanvasSelection();
    m_scene->selectRays(madeRays, false);
    m_scene->selectExplosions(madeBlasts, false);
    const int made = int(madeRays.size() + madeBlasts.size());
    if (!haveShapes) {
        const QString last = made == 1 ? (madeRays.isEmpty() ? madeBlasts.first()->name()
                                                             : madeRays.first()->name())
                                       : tr("%n objects", nullptr, made);
        m_scene->notifyEdit(tr("Paste %1").arg(last));
        statusBar()->showMessage(tr("Pasted %1").arg(last), 4000);
        return;
    }

    // Through the fragment reader, so a paste rebuilds the bodies and the
    // joints between them rather than laying down loose shapes.
    const QVector<ShapeItem *> pasted =
        SceneSerializer::fragmentFromJson(m_scene, root, QPointF(offset, offset));
    if (pasted.isEmpty())
        return;

    selectPasted(pasted);

    const QString what = pasted.size() == 1 ? pasted.first()->name()
                                            : tr("%n shapes", nullptr, int(pasted.size()));
    m_scene->notifyEdit(tr("Paste %1").arg(what));
    statusBar()->showMessage(tr("Pasted %1").arg(what), 4000);
}

void MainWindow::on_actionDuplicate_triggered()
{
    duplicateShapes(selectedShapes());
}

void MainWindow::on_actionSelectAll_triggered()
{
    m_scene->selectAllShapes();
}

void MainWindow::restack(CanvasScene::Restack where)
{
    const QVector<ShapeItem *> picked = selectedShapes();
    if (picked.isEmpty() || !m_scene->restackShapes(picked, where))
        return;

    const QString what = picked.size() == 1 ? picked.first()->name()
                                            : tr("%n shapes", nullptr, int(picked.size()));
    m_scene->notifyEdit(tr("Restack %1").arg(what));
}

void MainWindow::on_actionBringToFront_triggered() { restack(CanvasScene::Restack::ToFront); }
void MainWindow::on_actionBringForward_triggered() { restack(CanvasScene::Restack::Forward); }
void MainWindow::on_actionSendBackward_triggered() { restack(CanvasScene::Restack::Backward); }
void MainWindow::on_actionSendToBack_triggered() { restack(CanvasScene::Restack::ToBack); }

// Paste, Select All and the view commands belong to the canvas, not to
// whatever happens to be under the pointer, so both modes' menus end with the
// same block. The empty-canvas menu used to offer Add and nothing else.
void MainWindow::addCanvasActions(QMenu *menu)
{
    if (!menu->isEmpty())
        menu->addSeparator();
    menu->addAction(m_ui->actionPaste);
    menu->addAction(m_ui->actionSelectAll);
    menu->addSeparator();
    menu->addAction(m_ui->actionFitToField);
    if (m_resetScaleAction)
        menu->addAction(m_resetScaleAction);
    menu->addAction(m_ui->actionShowObjectNames);
}

// The field, not the view's own scene rect: a run widens that to hold a
// focused body that has left the field, and fitting to it would zoom out to
// frame empty space.
void MainWindow::on_actionFitToField_triggered()
{
    QGraphicsView *view = m_ui->canvasView;
    const QRectF field(-m_scene->fieldWidth() / 2.0, -m_scene->fieldHeight() / 2.0,
                       m_scene->fieldWidth(), m_scene->fieldHeight());
    const QSize port = view->viewport()->size();
    if (field.isEmpty() || port.isEmpty())
        return;

    // A little short of flush, so the field's own edge is visible rather than
    // sitting exactly on the frame.
    constexpr qreal kRoom = 0.97;
    const qreal factor = qMin(port.width() / field.width(), port.height() / field.height());
    m_scene->setCurrentScale(factor * kRoom * 100.0);
    view->centerOn(field.center());
}

// What a paste or a duplicate leaves picked. In Physics mode that is the whole
// of each body, so the copy can be dragged or jointed straight away; in Edit
// mode it is the shapes themselves.
void MainWindow::selectPasted(const QVector<ShapeItem *> &made)
{
    if (made.isEmpty())
        return;

    m_scene->selectShapes(made, false);
}

bool MainWindow::confirmDiscardChanges(const QString &title)
{
    if (!m_undo || m_undo->isClean())
        return true;

    // A test drives the window itself and has nobody to answer a dialog, so it
    // is told apart by the settings override it always runs with. Any caller
    // that would otherwise pop a modal here is silently allowed to proceed.
    if (qEnvironmentVariableIsSet("PHYSALIS_SETTINGS"))
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, title,
        tr("The scene has unsaved changes.\n\nSave them before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return onSaveScene(); // false if the save failed, or Save As was cancelled
    return true;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmDiscardChanges(tr("Quit"))) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::on_actionNewScene_triggered()
{
    if (!confirmDiscardChanges(tr("New Scene")))
        return;

    // A run holds engine state built from the shapes about to be deleted.
    m_simulation->stop();
    m_scene->clearContents();
    applyFieldDefaults();
    // An empty scene has nothing to group into bodies or hang joints on, so
    // Physics mode has nothing to show and nothing to do. Drawing is where a
    // scene starts.
    m_scene->setEditorMode(EditorMode::Edit);
    useEngine(engineForNewScenes());
    m_scenePath.clear();
    m_undo->reset();
    updateWindowTitle();
}

void MainWindow::on_actionLoadScene_triggered()
{
    // Before the file dialog: being told afterwards wastes the choice.
    if (!confirmDiscardChanges(tr("Load Scene")))
        return;

    const QString path = QFileDialog::getOpenFileName(this, tr("Load Scene"), QString(), tr(kSceneFilter));
    if (path.isEmpty())
        return;

    openScene(path);
}

bool MainWindow::openScene(const QString &path)
{
    m_simulation->stop();
    applyFieldDefaults();

    QString error;
    if (!SceneSerializer::loadFromFile(m_scene, path, &error)) {
        // The same reason the unsaved-changes prompt stands aside under a test:
        // nobody is there to dismiss a modal, and a recent entry that no longer
        // opens is exactly what a test drives this with.
        if (!qEnvironmentVariableIsSet("PHYSALIS_SETTINGS")) {
            QMessageBox::warning(this, tr("Load Scene"),
                                 tr("Couldn't open %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        }
        return false;
    }

    m_scenePath = path;
    rememberRecent(path);
    // The file said which engine it was built for, and the loader refused it
    // if that one is missing; everything else follows from the scene.
    useEngine(m_scene->simulationEngineName());
    m_undo->reset(); // the opened file is the new starting point
    updateWindowTitle();
    m_ui->propertyPanel->setActiveItem(nullptr);
    statusBar()->showMessage(tr("Opened %1").arg(QDir::toNativeSeparators(path)), 4000);

    // Queued: a file named on the command line is opened straight after
    // show(), before the layout has given the view its real size, and centring
    // against the size it has then lands off to one side.
    QMetaObject::invokeMethod(this, [this] {
        const QRectF bounds = m_scene->contentBounds();
        m_ui->canvasView->centerOn(bounds.isNull() ? QPointF(0, 0) : bounds.center());
    }, Qt::QueuedConnection);
    return true;
}

bool MainWindow::onSaveScene()
{
    // Only a prompt about closing the scene gets here mid-run -- the actions
    // are off -- so the run can end: stopping puts the scene back as it was,
    // which is what gets saved.
    if (m_simulation->isActive())
        m_simulation->stop();
    if (m_scenePath.isEmpty())
        return onSaveSceneAs();

    QString error;
    if (!SceneSerializer::saveToFile(m_scene, m_scenePath, &error)) {
        QMessageBox::warning(this, tr("Save Scene"),
                             tr("Couldn't save %1:\n%2").arg(QDir::toNativeSeparators(m_scenePath), error));
        return false;
    }
    m_undo->markClean();
    // Saving is as much "this is the scene I am working on" as opening is, and
    // a scene saved for the first time has never been opened at all -- so
    // without this it could not be reached from Open Recent until it had been
    // opened the long way round once.
    rememberRecent(m_scenePath);
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(m_scenePath)), 4000);
    return true;
}

bool MainWindow::onSaveSceneAs()
{
    return saveSceneAs(QFileDialog::getSaveFileName(this, tr("Save Scene As"), m_scenePath,
                                                    tr(kSceneFilter)));
}

bool MainWindow::saveSceneAs(QString path)
{
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".phys");

    m_scenePath = path;
    updateWindowTitle();
    return onSaveScene();
}

// The shapes an Edit-mode command acts on: everything picked, with the one
// carrying the handles first.
QVector<ShapeItem *> MainWindow::selectedShapes() const
{
    // Whichever selection the mode actually keeps. Reading the edit selection
    // in Physics mode gave back whatever had last been picked in Edit mode --
    // so Copy there copied something that had not been on screen for a while.
    if (m_scene->editorMode() != EditorMode::Edit)
        return m_scene->physicsSelection();

    QVector<ShapeItem *> picked;
    if (ShapeItem *active = m_scene->activeItem())
        picked.append(active);
    for (ShapeItem *other : m_scene->editSelection()) {
        if (!picked.contains(other))
            picked.append(other);
    }
    return picked;
}

void MainWindow::duplicateShape(ShapeItem *item)
{
    if (!item)
        return;

    // Asked of one shape, but a shape that is part of a selection stands for
    // the whole of it -- the same as copy does.
    QVector<ShapeItem *> source = selectedShapes();
    if (!source.contains(item))
        source = {item};
    duplicateShapes(source);
}

void MainWindow::duplicateShapes(const QVector<ShapeItem *> &source)
{
    if (source.isEmpty())
        return;

    constexpr qreal kOffset = 20.0;
    // Through the same fragment the clipboard uses, so a duplicate and a
    // paste cannot drift apart in what they carry -- which now includes the
    // bodies the shapes belong to and the joints between those bodies.
    int jointsLeftOut = 0;
    const QJsonObject fragment = SceneSerializer::fragmentToJson(m_scene, source, &jointsLeftOut);
    const QVector<ShapeItem *> copies =
        SceneSerializer::fragmentFromJson(m_scene, fragment, QPointF(kOffset, kOffset));
    if (copies.isEmpty())
        return;

    selectPasted(copies);

    const QString what = copies.size() == 1
                             ? source.first()->name()
                             : tr("%n shapes", nullptr, int(copies.size()));
    m_scene->notifyEdit(tr("Duplicate %1").arg(what));
    statusBar()->showMessage(
        jointsLeftOut == 0
            ? (copies.size() == 1
                   ? tr("Duplicated %1 as %2").arg(what, copies.first()->name())
                   : tr("Duplicated %1").arg(what))
            : tr("Duplicated %1 — %n joint(s) left out, held to a body that was not picked",
                 nullptr, jointsLeftOut).arg(what),
        jointsLeftOut == 0 ? 4000 : 6000);
}

void MainWindow::convertToPolygon(ShapeItem *item)
{
    if (!item)
        return;

    const QString name = item->name();          // read before the shape is gone
    const bool hadRadius = item->cornerRadius() > 0.0;

    ShapeItem *polygon = m_scene->convertToPolygon(item);
    if (!polygon) {
        statusBar()->showMessage(tr("%1 is not a rectangle").arg(name), 4000);
        return;
    }

    m_scene->notifyEdit(tr("Convert %1 to a polygon").arg(name));
    statusBar()->showMessage(
        hadRadius ? tr("%1 is a polygon now — its corners are square, a polygon has no radius")
                        .arg(polygon->name())
                  : tr("%1 is a polygon now — drag its nodes to reshape it").arg(polygon->name()),
        6000);
}

void MainWindow::flipShape(ShapeItem *item, bool horizontally)
{
    if (!item)
        return;

    // Mirrored about the origin, which is what the shape turns and scales
    // about -- mirroring about the rect instead would shift the shape.
    const QPointF origin = item->origin();
    const auto mirror = [origin, horizontally](const QPointF &p) {
        return horizontally ? QPointF(2.0 * origin.x() - p.x(), p.y())
                            : QPointF(p.x(), 2.0 * origin.y() - p.y());
    };

    if (item->supportsNodeEditing() && item->nodeCount() > 0) {
        for (int i = 0; i < item->nodeCount(); ++i)
            item->moveNode(i, mirror(item->nodePosition(i)));
    } else {
        const QRectF r = item->rect();
        const QPointF a = mirror(r.topLeft());
        const QPointF b = mirror(r.bottomRight());
        item->setRect(QRectF(a, b).normalized());
    }

    // A mirrored shape turns the other way, or the flip would be undone by
    // whatever rotation it already carries.
    item->setRotation(-item->rotation());

    m_scene->notifyEdit(horizontally ? tr("Flip %1 horizontally").arg(item->name())
                                     : tr("Flip %1 vertically").arg(item->name()));
}

QString MainWindow::logLabelFor(const CanvasScene::Watch &watch) const
{
    // What the row is called is asked of the engine every time it is drawn,
    // never remembered: the catalogue is where a property's name lives, and an
    // entry made before a label changed -- or before the name carried its
    // heading -- would otherwise keep saying the old thing. A joint has a
    // Spring, a Limit and a Motor each with a switch called "Enabled", so
    // without the heading the row names none of the three. What the file
    // carries stands only where the catalogue has nothing to say.
    // A variable is named by the scene rather than by a catalogue, and the
    // name is the label: there is nothing else it could be called.
    if (watch.objectName == Rule::variables()) {
        const SceneVariable *variable = m_scene->variableNamed(watch.propertyKey);
        return variable ? variable->name : watch.label;
    }

    auto engine = physics::EngineRegistry::create(m_scene->simulationEngineName());
    if (!engine)
        return watch.label;

    const auto named = [&watch](const physics::PropertyList &properties, QString *label) {
        for (const physics::JointParam &property : properties) {
            if (property.key != watch.propertyKey)
                continue;
            *label = property.section.isEmpty()
                         ? property.label
                         : tr("%1 · %2").arg(property.section, property.label);
            return true;
        }
        return false;
    };

    QString label;
    for (Joint *joint : m_scene->joints()) {
        if (joint->name() != watch.objectName)
            continue;
        for (const physics::JointType &type : engine->jointTypes()) {
            if (type.id == joint->typeId() && named(type.params, &label))
                return label;
        }
        if (named(engine->jointReadables(joint->typeId()), &label))
            return label;
    }
    if (watch.objectName == Rule::world() && named(engine->worldProperties(), &label))
        return label;
    // A body's and a shape's properties share no keys, so whichever has it is
    // the one the row is about.
    if (named(engine->bodyProperties(), &label) || named(engine->shapeProperties(), &label))
        return label;
    return watch.label;
}

// Two decimals for most things, but a light body's energy or mass is a few
// thousandths, and two decimals made it read as zero.
static QString formatLogNumber(double v)
{
    if (v == 0.0 || qAbs(v) >= 1.0)
        return QString::number(v, 'f', 2);
    return QString::number(v, 'g', 3);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_ui->canvasView && event->type() == QEvent::Resize) {
        placeLogOverlay();
        placeToolOptions();
        updateCaption();
    }
    // The panel is a child of the view, so a press that reaches the viewport
    // landed somewhere else -- which means the user has moved on, and the tool
    // they did not keep is called off.
    if (watched == m_ui->canvasView->viewport() && event->type() == QEvent::MouseButtonPress)
        cancelTool();
    return QMainWindow::eventFilter(watched, event);
}

// Under the log, in the top-left corner, whatever corner the log is in: the
// caption is the scene talking to whoever is watching it, and the top-left is
// where reading starts.
// The scene's controls, laid out under the caption in the same corner. They
// are the scene talking to whoever is working it, so they go where the reading
// happens -- and they are widgets rather than something drawn into the scene,
// so they keep their size whatever the zoom and can actually be clicked.
CanvasScene *MainWindow::shownScene() const
{
    return m_simulation && m_simulation->runScene() ? m_simulation->runScene() : m_scene;
}

void MainWindow::rebuildControls()
{
    if (!m_controls)
        return;

    m_controlWidgets.clear();
    qDeleteAll(m_controls->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
    auto *column = qobject_cast<QVBoxLayout *>(m_controls->layout());
    if (!column)
        return;

    for (const SceneControl &control : shownScene()->controls()) {
        const QString name = control.id;
        const QString shown = shownScene()->controlCaption(name);
        QWidget *widget = nullptr;

        switch (control.kind) {
        case SceneControl::Kind::Checkbox: {
            auto *box = new QCheckBox(shown, m_controls);
            connect(box, &QCheckBox::toggled, this, [this, name](bool on) {
                shownScene()->setControlValue(name, on);
            });
            widget = box;
            break;
        }
        case SceneControl::Kind::Button: {
            auto *button = new QPushButton(shown, m_controls);
            // Down and up, not "clicked": a rule may want to drive something
            // for exactly as long as the button is held.
            connect(button, &QPushButton::pressed, this, [this, name] {
                shownScene()->setControlValue(name, true);
            });
            connect(button, &QPushButton::released, this, [this, name] {
                shownScene()->setControlValue(name, false);
            });
            widget = button;
            break;
        }
        case SceneControl::Kind::Slider: {
            auto *holder = new QWidget(m_controls);
            auto *row = new QHBoxLayout(holder);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(6);
            auto *says = new QLabel(shown, holder);
            says->setObjectName(QStringLiteral("says"));
            row->addWidget(says);
            auto *spin = new QDoubleSpinBox(holder);
            spin->setObjectName(QStringLiteral("value"));
            spin->setDecimals(control.decimals);
            spin->setRange(control.minimum, control.maximum);
            spin->setSingleStep(control.step);
            spin->setKeyboardTracking(false);
            auto *slider = new QSlider(Qt::Horizontal, holder);
            slider->setObjectName(QStringLiteral("slider"));
            // A slider counts in whole numbers, so it counts in steps and the
            // step is what a number means here.
            const int steps = qMax(1, qRound((control.maximum - control.minimum)
                                             / qMax(0.0001, control.step)));
            slider->setRange(0, steps);
            row->addWidget(slider, 1);
            row->addWidget(spin);

            connect(slider, &QSlider::valueChanged, this, [this, name, control](int at) {
                shownScene()->setControlValue(name, control.minimum + at * control.step);
            });
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, name](double v) {
                shownScene()->setControlValue(name, v);
            });
            widget = holder;
            break;
        }
        }

        if (!widget)
            continue;
        widget->setToolTip(tr("Rules reach it as %1").arg(name));
        column->addWidget(widget);
        m_controlWidgets.insert(name, widget);
    }

    refreshControls();
}

// What they are set to now. The widgets are told without answering back, or a
// rule moving a slider would be answered by the slider moving it again.
void MainWindow::refreshControls()
{
    if (!m_controls)
        return;

    // Only while a run is going. They are how a running scene is worked --
    // outside one there is nothing for a press to reach, and a slider left
    // standing over a scene being edited is just something in the way.
    const bool running = m_simulation && m_simulation->isActive();
    m_controls->setVisible(running && !shownScene()->controls().isEmpty());
    if (!m_controls->isVisible())
        return;

    for (const SceneControl &control : shownScene()->controls()) {
        QWidget *widget = m_controlWidgets.value(control.id);
        if (!widget)
            continue;
        widget->setEnabled(shownScene()->controlEnabled(control.id));
        const QVariant value = shownScene()->controlValue(control.id);
        const QString says = shownScene()->controlCaption(control.id);

        if (auto *box = qobject_cast<QCheckBox *>(widget)) {
            const QSignalBlocker quiet(box);
            box->setChecked(value.toBool());
            box->setText(says);
        } else if (auto *button = qobject_cast<QPushButton *>(widget)) {
            // A press is a moment, not a state to put back; what it says is
            // not, and a rule may have changed it.
            button->setText(says);
        } else {
            if (auto *label = widget->findChild<QLabel *>(QStringLiteral("says")))
                label->setText(says);
            auto *slider = widget->findChild<QSlider *>(QStringLiteral("slider"));
            auto *spin = widget->findChild<QDoubleSpinBox *>(QStringLiteral("value"));
            if (!slider || !spin)
                continue;
            const QSignalBlocker quietSlider(slider);
            const QSignalBlocker quietSpin(spin);
            spin->setValue(value.toDouble());
            slider->setValue(qRound((value.toDouble() - control.minimum)
                                    / qMax(0.0001, control.step)));
        }
    }

    m_controls->adjustSize();
    constexpr int kMargin = 8;
    int top = kMargin;
    if (m_caption && m_caption->isVisible())
        top = m_caption->y() + m_caption->height() + kMargin;
    else if (m_logOverlay && m_logOverlay->isVisible() && m_logCorner == Qt::TopLeftCorner)
        top = m_logOverlay->y() + m_logOverlay->height() + kMargin;
    m_controls->move(kMargin, top);
    m_controls->raise();
}

// The scene moves around the body rather than the body across the screen.
// Only while a run is going: editing is when panning is the user's own.
void MainWindow::followFocusedBody()
{
    if (!m_simulation || !m_simulation->isActive())
        return;
    CanvasScene *run = shownScene();
    PhysicsBody *body = run->focusedBody();
    if (!body)
        return;
    centreViewOn(m_ui->canvasView, body->originScenePos(), run->sceneRect());
}

// centerOn will not scroll past the scene's rectangle, so a body that has left
// the field would slide off the screen with nothing following it. The view is
// given a rectangle of its own, big enough to hold both, rather than the
// scene's being stretched -- the field is what the scene says it is.
void MainWindow::centreViewOn(QGraphicsView *view, const QPointF &at, const QRectF &field)
{
    if (!view)
        return;
    const QRectF seen = view->mapToScene(view->viewport()->rect()).boundingRect();
    QRectF reach = field.united(QRectF(at, at).adjusted(-seen.width(), -seen.height(),
                                                        seen.width(), seen.height()));
    view->setSceneRect(reach);
    view->centerOn(at);
}

void MainWindow::startRunForTest()
{
    m_simulation->start();
}

void MainWindow::stopRunForTest()
{
    m_simulation->stop();
}

void MainWindow::updateCaption()
{
    if (!m_caption)
        return;

    const bool show = shownScene()->captionShown() && !shownScene()->caption().isEmpty();
    m_caption->setVisible(show);
    if (!show)
        return;

    m_caption->setText(shownScene()->caption());

    // A wrapped label cannot size itself: its hint is the width the text would
    // want on one line, so adjustSize() gave a box too narrow for what it then
    // wrapped and the last words were cut off. And measured from the text
    // rather than asked of the label, because QLabel::heightForWidth()
    // under-reports rich text -- it returned 64 for a caption whose document
    // needed 56 inside margins of 14.
    //
    // Sized once at the widest it may be, to learn what the frame and the
    // margins cost, then to what the words actually want: a line and a half
    // of text was taking the full width of the box it was allowed, which is a
    // lot of white space to put over somebody's scene.
    constexpr int kMargin = 8;
    const int room = overlayHost()->viewport()->width() - 2 * kMargin;
    const int widest = qMax(120, qMin(m_caption->maximumWidth(), room));

    m_caption->resize(widest, 100);
    const int chromeWide = m_caption->width() - m_caption->contentsRect().width();
    const int chromeTall = m_caption->height() - m_caption->contentsRect().height();

    QTextDocument wrapped;
    wrapped.setDefaultFont(m_caption->font());
    wrapped.setHtml(shownScene()->caption());
    // What it would take on the lines it chooses for itself, which for text
    // with no break in it is one line and for text with breaks is the longest
    // of them.
    wrapped.setTextWidth(-1);
    const int wanted = int(std::ceil(wrapped.idealWidth()));
    const int text = qBound(60, wanted, widest - chromeWide);

    wrapped.setTextWidth(text);
    m_caption->resize(text + chromeWide,
                      int(std::ceil(wrapped.size().height())) + chromeTall);

    const bool under = m_logCorner == Qt::TopLeftCorner && m_logOverlay
                       && m_logOverlay->isVisible();
    m_caption->move(kMargin, under ? m_logOverlay->y() + m_logOverlay->height() + kMargin
                                   : kMargin);
    m_caption->raise();
    refreshControls();
}

void MainWindow::moveOverlaysForTest(QGraphicsView *host)
{
    moveOverlaysTo(host);
}

void MainWindow::setFullScreenForTest(bool on)
{
    if (m_fullScreenCheck)
        m_fullScreenCheck->setChecked(on);
}

QWidget *MainWindow::overlayHostForTest() const
{
    return overlayHost();
}

QGraphicsView *MainWindow::overlayHost() const
{
    return m_fullScreen ? static_cast<QGraphicsView *>(m_fullScreen) : m_ui->canvasView;
}

// They are children of a view, and a full-screen run is a different view --
// so on the way in and out they are handed over, rather than left behind on a
// window nobody is looking at.
void MainWindow::moveOverlaysTo(QGraphicsView *host)
{
    if (!host)
        return;
    for (QWidget *overlay : { static_cast<QWidget *>(m_logOverlay),
                              static_cast<QWidget *>(m_caption),
                              static_cast<QWidget *>(m_controls) }) {
        if (!overlay || overlay->parentWidget() == host)
            continue;
        const bool wasShown = overlay->isVisible();
        overlay->setParent(host);
        overlay->setVisible(wasShown);
    }
    updateLogOverlay();
    updateCaption();
    refreshControls();
}

void MainWindow::placeLogOverlay()
{
    if (!m_logOverlay)
        return;

    constexpr int kMargin = 8;
    const QSize view = overlayHost()->viewport()->size();
    const QSize own = m_logOverlay->size();
    const bool right = m_logCorner == Qt::TopRightCorner
                       || m_logCorner == Qt::BottomRightCorner;
    const bool bottom = m_logCorner == Qt::BottomLeftCorner
                        || m_logCorner == Qt::BottomRightCorner;
    m_logOverlay->move(right ? qMax(kMargin, view.width() - own.width() - kMargin) : kMargin,
                       bottom ? qMax(kMargin, view.height() - own.height() - kMargin) : kMargin);

    // The caption sits under the log, so it moves whenever the log does -- and
    // the log moves on every line it gains. Left to place itself once, it was
    // overlapped the moment a watch was added.
    updateCaption();
    refreshControls();
}

void MainWindow::applyLogStyle()
{
    if (!m_logOverlay)
        return;

    m_logOverlay->setStyleSheet(
        QStringLiteral("#LogOverlay { background: rgba(255,255,255,190);"
                       " border: 1px solid #c8c8c8; border-radius: 4px;"
                       " padding: 4px 8px; color: %1; }").arg(m_logColor.name()));

    QFont font = m_logFontFamily.isEmpty() ? m_logOverlay->font() : QFont(m_logFontFamily);
    font.setPointSize(m_logFontSize);
    m_logOverlay->setFont(font);
    m_logOverlay->adjustSize();
    placeLogOverlay();
}

// The tabs carry an icon and keep their name in a tooltip: five words across
// a narrow panel left no room for any of them, and the names are only needed
// while you are learning which is which.
void MainWindow::buildSidePanelTabs()
{
    QTabWidget *tabs = m_ui->sidePanel;
    const struct { QWidget *page; QIcon icon; } wanted[] = {
        {m_ui->propertyPanel, Icons::properties()},
        {m_ui->sceneTree, Icons::objects()},
        {m_ui->rulesPanel, Icons::rules()},
        {m_ui->variablesPanel, Icons::variable()},
        {m_ui->controlsPanel, Icons::keyboard()},
        {m_ui->plotPanel, Icons::plot()},
    };
    for (const auto &one : wanted) {
        const int at = tabs->indexOf(one.page);
        if (at < 0)
            continue;
        const QString name = tabs->tabText(at);
        one.page->setProperty("tabName", name);
        one.page->setProperty("tabIcon", QVariant::fromValue(one.icon));
        tabs->setTabIcon(at, one.icon);
        tabs->setTabText(at, QString());
        tabs->setTabToolTip(at, name);
    }
    tabs->setIconSize(QSize(20, 20));
    // Tabs of this width are icons, not labels, and the sheet gives them a
    // label's padding unless it is told otherwise.
    tabs->tabBar()->setProperty("iconsOnly", true);

    // View says which panels to keep. Built when it opens rather than once,
    // so it cannot drift from what is actually on the strip.
    connect(m_ui->menuView, &QMenu::aboutToShow, this, [this] {
        m_ui->menuView->clear();
        for (QWidget *page : { static_cast<QWidget *>(m_ui->propertyPanel),
                               static_cast<QWidget *>(m_ui->sceneTree),
                               static_cast<QWidget *>(m_ui->rulesPanel),
                               static_cast<QWidget *>(m_ui->variablesPanel),
                               static_cast<QWidget *>(m_ui->controlsPanel),
                               static_cast<QWidget *>(m_ui->plotPanel) }) {
            const QString name = page->property("tabName").toString();
            if (name.isEmpty())
                continue;
            QAction *action = m_ui->menuView->addAction(name);
            action->setCheckable(true);
            action->setChecked(!m_hiddenTabs.contains(name));
            connect(action, &QAction::toggled, this, [this, name](bool on) {
                if (on)
                    m_hiddenTabs.removeAll(name);
                else if (!m_hiddenTabs.contains(name))
                    m_hiddenTabs.append(name);
                applyVisibleTabs();
            });
        }
        m_ui->menuView->addSeparator();
        m_ui->menuView->addAction(m_ui->actionFitToField);
        if (m_resetScaleAction)
            m_ui->menuView->addAction(m_resetScaleAction);
    });

    m_hiddenTabs = QSettings(settingsFilePath(), QSettings::IniFormat)
                       .value(QStringLiteral("Panels/hidden"))
                       .toStringList();
    if (!m_hiddenTabs.isEmpty())
        applyVisibleTabs();

    connect(tabs, &QTabWidget::currentChanged, this,
            [this](int) { updateSidePanelTabLabels(); });
    updateSidePanelTabLabels();
}

// Rebuilt rather than hidden: QTabWidget has no "hide one tab", so the ones
// that are not wanted are taken out and put back in their old order.
// The tab you are looking at says what it is; the others are icons only. Five
// names across a narrow panel left no room for any of them, and one name is
// all that is ever being read.
void MainWindow::updateSidePanelTabLabels()
{
    QTabWidget *tabs = m_ui->sidePanel;
    for (int at = 0; at < tabs->count(); ++at) {
        const QString name = tabs->widget(at)->property("tabName").toString();
        tabs->setTabText(at, at == tabs->currentIndex() ? name : QString());
    }
}

void MainWindow::applyVisibleTabs()
{
    QSettings(settingsFilePath(), QSettings::IniFormat)
        .setValue(QStringLiteral("Panels/hidden"), m_hiddenTabs);

    QTabWidget *tabs = m_ui->sidePanel;
    QWidget *was = tabs->currentWidget();
    const QVector<QWidget *> order = { m_ui->propertyPanel, m_ui->sceneTree, m_ui->rulesPanel,
                                       m_ui->variablesPanel, m_ui->controlsPanel,
                                       m_ui->plotPanel };
    for (QWidget *page : order) {
        const int at = tabs->indexOf(page);
        if (at >= 0)
            tabs->removeTab(at);
    }
    for (QWidget *page : order) {
        const QString name = page->property("tabName").toString();
        if (m_hiddenTabs.contains(name)) {
            page->hide();
            continue;
        }
        const int at = tabs->addTab(page, QString());
        tabs->setTabIcon(at, page->property("tabIcon").value<QIcon>());
        tabs->setTabToolTip(at, name);
    }
    // Never leave none of them: a panel with no tabs cannot be got back.
    if (tabs->count() == 0) {
        m_hiddenTabs.removeAll(m_ui->propertyPanel->property("tabName").toString());
        applyVisibleTabs();
        return;
    }
    if (was && tabs->indexOf(was) >= 0)
        tabs->setCurrentWidget(was);
    updateSidePanelTabLabels();
}

void MainWindow::updateLogOverlay()
{
    if (!m_logOverlay)
        return;

    // Only while a run is going: outside one the values are simply what the
    // property table already shows, and the readout would sit over the canvas
    // saying nothing new.
    const QVector<CanvasScene::Watch> watches = m_scene->watches();
    if (watches.isEmpty() || !m_simulation || !m_simulation->isActive()) {
        m_logOverlay->hide();
        updateCaption(); // the caption moves up into the space
        return;
    }

    QStringList lines;
    lines.reserve(watches.size());
    for (const CanvasScene::Watch &watch : watches) {
        // While a run owns the world the engine is the only place the live
        // value exists; outside one there is nothing to read, so the row is
        // shown waiting rather than with a stale number.
        QVariant value;
        if (m_simulation && m_simulation->isActive())
            value = m_simulation->readValue(watch.objectName, watch.propertyKey);
        // A shape's own geometry is not the engine's to report, and it is
        // readable whether or not a run is going.
        if (!value.isValid())
            value = m_scene->readSceneValue(watch.objectName, watch.propertyKey);

        QString shown = tr("--");
        if (value.isValid()) {
            shown = value.userType() == QMetaType::Bool
                        ? (value.toBool() ? tr("true") : tr("false"))
                        : formatLogNumber(value.toDouble());
        }
        // "@world" is an internal handle, not something to show a reader.
        const QString who = watch.objectName == Rule::world() ? tr("World")
                                                              : watch.objectName;
        lines << tr("%1 · %2   %3").arg(who, logLabelFor(watch), shown);
    }
    m_logOverlay->setText(lines.join(QChar::LineFeed));
    m_logOverlay->adjustSize();
    // Shown before it is placed: placing it moves the caption under it, and
    // that has to know whether there is a log there at all.
    m_logOverlay->show();
    placeLogOverlay();
    m_logOverlay->raise();
}

void MainWindow::setVersion(const QString &version)
{
    m_version = version;
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    const QString name = m_scenePath.isEmpty() ? tr("Untitled")
                                               : QFileInfo(m_scenePath).fileName();
    // A scene that was never saved can still have unsaved work in it, so the
    // star belongs on "Untitled" as much as on a file name.
    const QString star = (m_undo && !m_undo->isClean()) ? QStringLiteral("*") : QString();

    setWindowTitle(m_version.isEmpty()
                       ? tr("%1%2 - Physalis").arg(name, star)
                       : tr("%1%2 - Physalis %3").arg(name, star, m_version));
}

void MainWindow::updateTransportActions()
{
    if (!m_ui->actionSimulate || !m_simulation)
        return;

    const bool active = m_simulation->isActive();
    const bool running = m_simulation->isRunning();
    const bool haveBodies = !m_scene->bodies().isEmpty();

    // Stepping is only offered when time is not already advancing.
    m_ui->actionSimulate->setIcon(running ? Icons::pause() : Icons::simulate());
    m_ui->actionSimulate->setText(running ? tr("Pause") : tr("Simulate"));
    m_ui->actionSimulate->setToolTip(running ? tr("Hold the simulation where it is")
                                         : tr("Run the simulation continuously"));

    m_ui->actionSimulate->setEnabled(active || haveBodies);
    m_ui->actionStep->setEnabled(!running && (active || haveBodies));
    m_ui->actionStop->setEnabled(active);
}

void MainWindow::syncTransportWidgets(const OptionsDialog::Settings &s)
{
    // The controls catching up with the settings, not being used: none of this
    // reports a change back, or startup would write the file again for every
    // switch it touched. Called once the toolbar exists as well as from
    // applySettings, because at startup it does not exist yet.
    if (m_layers) {
        m_layers->setOn(kLayerGrid, s.runShowGrid);
        m_layers->setOn(kLayerJoints, s.runShowJoints);
        m_layers->setOn(kLayerAxes, s.runShowBodyAxes);
        m_layers->setOn(kLayerRays, s.runShowRays);
        m_layers->setOn(kLayerSensors, s.runShowSensors);
        m_layers->setOn(kLayerTrails, s.runShowTrails);
        m_layers->setOn(kLayerExplosions, s.runShowExplosions);
        m_layers->setOn(kLayerSleep, s.sleepShading);
    }
    if (m_fullScreenCheck) {
        const QSignalBlocker blocker(m_fullScreenCheck);
        m_fullScreenCheck->setChecked(s.simulationFullScreen);
    }
    if (m_speedCombo) {
        const QSignalBlocker blocker(m_speedCombo);
        const int index = m_speedCombo->findData(s.simulationSpeed);
        if (index >= 0)
            m_speedCombo->setCurrentIndex(index);
    }
    if (m_simulation) {
        // Blocked: at startup this runs while the window is still being built,
        // and the controller's stateChanged reaches handlers whose widgets do
        // not exist yet. The speed alone changes nothing anyone is showing.
        const QSignalBlocker blocker(m_simulation);
        m_simulation->setSpeed(s.simulationSpeed);
    }
}

void MainWindow::applyLayer(const QString &key, bool on)
{
    using RunLayer = CanvasScene::RunLayer;
    static const QHash<QString, RunLayer> layers {
        { kLayerGrid, RunLayer::Grid }, { kLayerJoints, RunLayer::Joints },
        { kLayerAxes, RunLayer::BodyAxes }, { kLayerRays, RunLayer::Rays },
        { kLayerExplosions, RunLayer::Explosions }, { kLayerSleep, RunLayer::SleepShading },
        { kLayerSensors, RunLayer::Sensors }, { kLayerTrails, RunLayer::Trails },
    };
    const auto layer = layers.constFind(key);
    if (layer == layers.constEnd())
        return;
    m_scene->setRunLayer(*layer, on);
    if (CanvasScene *run = m_simulation ? m_simulation->runScene() : nullptr)
        run->setRunLayer(*layer, on);
}

void MainWindow::updateFullScreenView()
{
    const bool wanted = m_simulation && m_simulation->isActive() && m_fullScreenCheck
                        && m_fullScreenCheck->isChecked();
    if (!wanted) {
        if (m_fullScreen) {
            m_fullScreen->close();
            m_fullScreen->deleteLater();
            m_fullScreen = nullptr;
            moveOverlaysTo(m_ui->canvasView);
            // The editor was behind it the whole time; give it the keyboard.
            activateWindow();
        }
        return;
    }
    if (m_fullScreen)
        return;

    m_fullScreen = new FullScreenView(shownScene(), this);
    // The keys stand in for the transport buttons, which are not on this
    // screen: the same three things they do there.
    connect(m_fullScreen, &FullScreenView::closeRequested,
            m_simulation, &SimulationController::stop);
    // It has its own view onto the same scene, so it does its own following.
    connect(m_simulation, &SimulationController::stepped,
            m_fullScreen, &FullScreenView::followFocusedBody);
    m_fullScreen->followFocusedBody();
    // Whichever screen the editor is on, rather than always the first.
    if (QScreen *screen = windowHandle() ? windowHandle()->screen() : nullptr)
        m_fullScreen->setGeometry(screen->geometry());
    m_fullScreen->showFullScreen();
    m_fullScreen->raise();
    m_fullScreen->activateWindow();
    // Keys reach whatever has the focus, and a window that has just been shown
    // does not have it until it is asked for.
    m_fullScreen->setFocus(Qt::OtherFocusReason);
    moveOverlaysTo(m_fullScreen);
}

void MainWindow::onSimulationStateChanged()
{
    CanvasScene *shown = shownScene();
    if (m_ui->canvasView->scene() != shown) {
        m_ui->canvasView->setScene(shown);
        m_ui->canvasView->setSceneRect(QRectF());
        if (m_fullScreen && shown != m_scene)
            m_fullScreen->setScene(shown);
        // Not rebuilt: the copy has the same controls, and a widget torn down
        // under a press loses its release.
        updateCaption();
    }
    refreshControls();

    const bool active = m_simulation->isActive();

    updateFullScreenView();
    updateTransportActions();
    updateUndoActions();

    m_ui->menuAddShape->setEnabled(!active && m_scene->editorMode() == EditorMode::Edit);
    for (EditorMode mode : EditorModes::kAll) {
        if (QToolButton *button = m_modeButtons.value(static_cast<int>(mode)))
            button->setEnabled(!active);
    }

    if (active) {
        const QStringList problems = m_simulation->problems();
        const QStringList skipped = m_simulation->skippedBodies();
        if (!problems.isEmpty()) {
            // What the solver could not do, which is worth more than the
            // running/paused it replaces.
            m_statusHelpLabel->setText(problems.join(QStringLiteral("  •  ")));
        } else if (skipped.isEmpty()) {
            onPhysicsSelectionChanged();
        } else {
            m_statusHelpLabel->setText(
                tr("%1 • Skipped (no mass): %2 • Stop restores the starting positions")
                    .arg(m_simulation->isRunning() ? tr("Running") : tr("Paused"),
                         skipped.join(QStringLiteral(", "))));
        }
        for (QAction *action : std::as_const(m_lockedWhileRunning))
            action->setEnabled(false);
    } else {
        for (QAction *action : std::as_const(m_alwaysOnWhenStopped))
            action->setEnabled(true);
        // Through the joint handler, which ends by calling the physics one:
        // Add Joint and Delete Joint are set nowhere else, so stopping a run
        // used to leave them greyed out until the editor mode was switched and
        // back. Selecting bodies did not bring them round, which is what made
        // it look like it happened only sometimes.
        onJointSelectionChanged();
        if (m_scene->editorMode() == EditorMode::Edit)
            onActiveItemChanged(m_scene->activeItem());
    }
}

void MainWindow::showIncompleteRules(int count)
{
    const int tab = m_ui->sidePanel->indexOf(m_ui->rulesPanel);
    if (tab < 0)
        return;

    // The tabs carry icons, not names, so an unfinished rule swaps the icon
    // for the warning rather than writing a count into a label that is not
    // there -- and says how many in the tooltip, which is where the name went.
    const QString name = m_ui->rulesPanel->property("tabName").toString();
    m_ui->sidePanel->setTabIcon(tab, count > 0 ? Icons::warning() : Icons::rules());
    updateSidePanelTabLabels();
    m_ui->sidePanel->setTabToolTip(
        tab,
        count > 0 ? tr("%1 — %n rule(s) unfinished and will not run.", nullptr, count).arg(name)
                  : name);
}

void MainWindow::onEditorModeChanged(EditorMode mode)
{
    styleModeButtons(mode);

    for (QAction *action : std::as_const(m_editModeActions))
        action->setVisible(mode == EditorMode::Edit);
    for (QAction *action : std::as_const(m_physicsModeActions))
        action->setVisible(mode == EditorMode::Physics);
    for (QAction *action : std::as_const(m_transportActions))
        action->setVisible(mode != EditorMode::Edit);

    const bool editing = mode == EditorMode::Edit;

    m_ui->menuAddShape->setEnabled(editing);
    updateAlignActions();
    updateCopyAction();
    updatePasteAction();

    if (m_ui->propertyPanel)
        m_ui->propertyPanel->setEditorMode(mode);

    onPhysicsSelectionChanged();
    onJointSelectionChanged();
    if (mode == EditorMode::Edit)
        onActiveItemChanged(m_scene->activeItem());
}

void MainWindow::styleModeButtons(EditorMode mode)
{
    for (EditorMode buttonMode : EditorModes::kAll) {
        QToolButton *button = m_modeButtons.value(static_cast<int>(buttonMode));
        if (!button)
            continue;

        const bool active = buttonMode == mode;
        const QColor accent = EditorModes::accent(buttonMode);

        button->setChecked(active);

        button->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  border: none;"
            "  border-radius: 12px;"
            "  padding: 5px 18px;"
            "  font-weight: %1;"
            "  color: %2;"
            "  background: %3;"
            "}"
            "QToolButton:hover:!checked { background: palette(mid); }"
            "QToolButton:disabled { color: %4; background: transparent; }")
            .arg(active ? QStringLiteral("bold") : QStringLiteral("normal"),
                 active ? QStringLiteral("white") : QStringLiteral("palette(window-text)"),
                 active ? accent.name() : QStringLiteral("transparent"),
                 Theme::colors().faintText.name()));
    }
}

// Lining shapes up: one button with the six edges under it, used from the
// toolbar and from the Edit menu. The scene does the work -- it owns the
// selection, and the move is an edit like any other.

// The strip under the toolbar, empty and hidden until a tool opens it.
void MainWindow::buildToolsMenu()
{
    m_toolsMenu = new QMenu(tr("Tools"), this);
    m_toolsMenu->setIcon(Icons::tools());
    m_toolsAction = new QAction(Icons::tools(), tr("Tools"), this);
    m_toolsAction->setMenu(m_toolsMenu);
    m_toolsAction->setToolTip(tr("Tools that work on the selection"));

    auto *clone = m_toolsMenu->addAction(Icons::clone(), tr("&Clone…"));
    connect(clone, &QAction::triggered, this, [this] {
        const QVector<ShapeItem *> source = selectedShapes();
        if (source.isEmpty())
            return;
        QRectF all;
        for (ShapeItem *shape : source) {
            const QRectF box = shape->mapToScene(shape->rect()).boundingRect();
            all = all.isNull() ? box : all.united(box);
        }
        const QVector<ToolOption> options {
            { QStringLiteral("rows"), tr("Rows"), 1.0, 1.0, 200.0, 1.0, 0, {}, {} },
            { QStringLiteral("columns"), tr("Columns"), 2.0, 1.0, 200.0, 1.0, 0, {}, {} },
            { QStringLiteral("gapX"), tr("Gap across"), 20.0, -10000.0, 10000.0, 5.0, 1,
              {}, {} },
            { QStringLiteral("gapY"), tr("Gap down"), 20.0, -10000.0, 10000.0, 5.0, 1,
              {}, {} },
        };
        beginTool(tr("Clone"), tr("Clone %1").arg(source.size() == 1
                                                      ? source.first()->name()
                                                      : tr("%n shapes", nullptr, source.size())),
                  options, [this, source] {
                      m_toolCreated.clear();
                      const QVector<ShapeItem *> made = cloneSelection(
                          source, int(m_toolOptions->value(QStringLiteral("rows"))),
                          int(m_toolOptions->value(QStringLiteral("columns"))),
                          m_toolOptions->value(QStringLiteral("gapX")),
                          m_toolOptions->value(QStringLiteral("gapY")));
                      for (ShapeItem *shape : made)
                          m_toolCreated.append(shape);
                  });
    });

    auto *mirror = m_toolsMenu->addAction(Icons::flipHorizontal(), tr("&Mirror…"));
    connect(mirror, &QAction::triggered, this, [this] {
        const QVector<ShapeItem *> source = selectedShapes();
        if (source.isEmpty())
            return;
        QVector<ToolOption> options {
            { QStringLiteral("side"), tr("Side"), 0.0, 0.0, 3.0, 1.0, 0, {}, {} },
            { QStringLiteral("margin"), tr("Margin"), 20.0, -10000.0, 10000.0, 5.0, 1,
              {}, {} },
        };
        // In the order MirrorSide takes them.
        options[0].choices =
            QStringList { tr("Right"), tr("Left"), tr("Top"), tr("Bottom") };
        beginTool(tr("Mirror"), tr("Mirror"), options, [this, source] {
            m_toolCreated.clear();
            const QVector<ShapeItem *> made =
                mirrorSelection(source, int(m_toolOptions->value(QStringLiteral("side"))),
                                m_toolOptions->value(QStringLiteral("margin")));
            for (ShapeItem *shape : made)
                m_toolCreated.append(shape);
        });
    });

    if (auto *toolBar = m_ui->toolBar) {
        // Straight after Align, which is the other thing on the bar that works
        // on whatever is picked.
        const QList<QAction *> actions = toolBar->actions();
        const int at = actions.indexOf(m_ui->actionAlign);
        toolBar->insertAction(at >= 0 && at + 1 < actions.size() ? actions.at(at + 1) : nullptr,
                              m_toolsAction);
        if (auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(m_toolsAction)))
            button->setPopupMode(QToolButton::InstantPopup);
    }

    // On the canvas rather than on the toolbar: it belongs to what is being
    // drawn, and a bar across the top pushes the whole scene down.
    m_toolOptions = new ToolOptionsBar(m_ui->canvasView);
    m_toolOptions->hide();

    connect(m_toolOptions, &ToolOptionsBar::valueChanged, this, &MainWindow::runTool);
    connect(m_toolOptions, &ToolOptionsBar::applied, this, &MainWindow::applyTool);
    connect(m_toolOptions, &ToolOptionsBar::cancelled, this, &MainWindow::cancelTool);
}

// Bottom left of the canvas, clear of the edges -- the same corner rule the
// log overlay follows.
void MainWindow::placeToolOptions()
{
    if (!m_toolOptions || m_toolOptions->isHidden())
        return;
    constexpr int kMargin = 10;
    const QSize view = m_ui->canvasView->viewport()->size();
    m_toolOptions->adjustSize();
    m_toolOptions->move(kMargin,
                        qMax(kMargin, view.height() - m_toolOptions->height() - kMargin));
}

void MainWindow::beginTool(const QString &title, const QString &label,
                           const QVector<ToolOption> &options, std::function<void()> run)
{
    cancelTool();

    m_toolLabel = label;
    m_toolRun = std::move(run);
    m_toolSubjects.clear();
    m_toolCreated.clear();
    for (ShapeItem *shape : selectedShapes())
        m_toolSubjects.append({ shape, shape->pos(), shape->rotation() });

    m_toolOptions->begin(title, options);
    m_toolOptions->show();
    m_toolOptions->raise();
    placeToolOptions();
    runTool();
}

// Back to what the tool found, so the next run is not laid on top of the last.
void MainWindow::undoTool()
{
    for (const QPointer<ShapeItem> &shape : std::as_const(m_toolCreated)) {
        if (shape)
            m_scene->destroyShape(shape.data());
    }
    m_toolCreated.clear();

    for (const ToolSubject &subject : std::as_const(m_toolSubjects)) {
        if (!subject.shape)
            continue;
        subject.shape->setPos(subject.pos);
        subject.shape->setRotation(subject.rotation);
    }
}

void MainWindow::runTool()
{
    if (!m_toolRun)
        return;

    // Every run but the kept one is a guess, and the undo stack is not shown
    // guesses.
    m_scene->muteEdits(true);
    undoTool();
    m_toolRun();
    m_scene->muteEdits(false);
    m_scene->update();
}

void MainWindow::applyTool()
{
    if (!m_toolRun)
        return;
    const QString label = m_toolLabel;
    m_toolRun = nullptr;
    m_toolSubjects.clear();
    m_toolCreated.clear();
    m_toolShape = nullptr;
    m_toolOriginal = QJsonObject();
    if (m_toolHidden)
        m_scene->destroyShape(m_toolHidden.data());
    m_toolHidden = nullptr;
    m_toolOptions->hide();
    m_scene->notifyShapesChanged();
    m_scene->notifyEdit(label);
}

void MainWindow::cancelTool()
{
    if (!m_toolRun)
        return;
    m_scene->muteEdits(true);
    undoTool();
    m_scene->muteEdits(false);
    m_toolRun = nullptr;
    m_toolSubjects.clear();
    m_toolCreated.clear();
    if (m_toolShape && !m_toolOriginal.isEmpty())
        m_scene->replaceShape(m_toolShape.data(), m_toolOriginal);
    m_toolShape = nullptr;
    m_toolOriginal = QJsonObject();
    if (m_toolHidden)
        m_toolHidden->setVisible(true);
    m_toolHidden = nullptr;
    m_toolOptions->hide();
    m_scene->notifyShapesChanged();
    m_scene->update();
}

// A grid of copies of whatever is picked, the original standing in for row and
// column nought. Spacing is the gap left between neighbours, so it is read off
// the whole selection's box rather than off each shape.

// A reflected copy of the selection on the side asked for, the margin being
// the distance between the two boxes. The mirror line sits half a margin past
// that edge, so the copy lands a whole margin away however far each shape in
// the selection is from the edge.


// A circle turned into the chain that runs round it: the links are the sides
// of the polygon inscribed in it, so each one ends exactly where the next
// begins and the corners sit on the circle that was drawn.
// A circle redrawn as the run of segments round its rim -- the same kind of
// conversion a rectangle's Convert to Polygon is, and nothing to do with
// bodies or joints. The shape keeps its name, its body and its physics.

// A circle or a rectangle taken apart into the lines its outline is made of.
// Unlike Convert to Chain, which leaves one shape, this leaves one per side:
// each can be a body of its own, which is what joints connect.
void MainWindow::shapeToSegments(ShapeItem *item)
{
    if (!item)
        return;
    const bool round = item->typeName() == QLatin1String("circle");
    if (!round && item->typeName() != QLatin1String("rectangle"))
        return;

    QVector<ToolOption> options {
        { QStringLiteral("segments"), round ? tr("Segments") : tr("Per side"),
          round ? 12.0 : 1.0, round ? 3.0 : 1.0, 400.0, 1.0, 0, {}, {} },
    };
    if (round) {
        options.append({ QStringLiteral("angle"), tr("Start angle"), 0.0, -360.0, 360.0,
                         5.0, 1,
                         QStringLiteral("\u00b0"), {} });
    }

    // Out of sight while the segments stand in for it, back if the tool is
    // called off, and gone once it is kept.
    item->setVisible(false);
    m_toolHidden = item;

    beginTool(tr("Convert to Segments"), tr("Convert %1 to segments").arg(item->name()), options,
              [this, round] {
                  if (!m_toolHidden)
                      return;
                  const QVector<ShapeItem *> made = m_scene->convertToSegments(
                      m_toolHidden.data(),
                      int(m_toolOptions->value(QStringLiteral("segments"))),
                      round ? m_toolOptions->value(QStringLiteral("angle")) : 0.0);
                  for (ShapeItem *segment : made)
                      m_toolCreated.append(segment);
              });
}

void MainWindow::circleToChain(ShapeItem *item)
{
    if (!item || item->typeName() != QLatin1String("circle"))
        return;

    // What it was, so every re-run starts from the circle rather than from
    // the polygon the last one left, and Cancel can put it back.
    m_toolOriginal = SceneSerializer::shapeToJson(item);
    m_toolShape = item;

    const QVector<ToolOption> options {
        { QStringLiteral("segments"), tr("Segments"), 12.0, 3.0, 400.0, 1.0, 0, {}, {} },
        { QStringLiteral("angle"), tr("Start angle"), 0.0, -360.0, 360.0, 5.0, 1,
          QStringLiteral("°"), {} },
    };
    beginTool(tr("Circle to Chain"), tr("Convert %1 to a chain").arg(item->name()), options,
              [this] {
                  if (!m_toolShape)
                      return;
                  ShapeItem *was = m_toolShape.data();
                  ShapeItem *now = m_scene->replaceShape(was, m_toolOriginal);
                  m_toolShape = now ? now : was;
                  if (!m_toolShape)
                      return;
                  if (ShapeItem *chain = m_scene->convertCircleToChain(
                          m_toolShape.data(),
                          int(m_toolOptions->value(QStringLiteral("segments"))),
                          m_toolOptions->value(QStringLiteral("angle"))))
                      m_toolShape = chain;
              });
}

// Every re-run starts from the polygon as it was, so a smaller corner limit
// cuts the original rather than the pieces the last run left.
void MainWindow::splitIntoConvexPieces(ShapeItem *item)
{
    if (!m_scene->canSplitIntoConvexPieces(item))
        return;

    m_toolOriginal = SceneSerializer::shapeToJson(item);
    m_toolShape = item;

    const int cap = m_scene->maxPolygonVertices();
    const QVector<ToolOption> options {
        { QStringLiteral("corners"), tr("Corners per piece"), qreal(cap), 3.0, qreal(cap), 1.0, 0,
          {}, {} },
    };
    beginTool(tr("Split into Convex Pieces"), tr("Split %1 into convex pieces").arg(item->name()),
              options, [this] {
                  if (!m_toolShape)
                      return;
                  ShapeItem *was = m_toolShape.data();
                  ShapeItem *now = m_scene->replaceShape(was, m_toolOriginal);
                  m_toolShape = now ? now : was;
                  if (!m_toolShape)
                      return;
                  const QVector<ShapeItem *> pieces = m_scene->splitIntoConvexPieces(
                      m_toolShape.data(), int(m_toolOptions->value(QStringLiteral("corners"))));
                  if (pieces.isEmpty()) {
                      statusBar()->showMessage(
                          tr("%1 crosses itself, so it has no inside to cut up.")
                              .arg(m_toolShape->name()), 6000);
                      return;
                  }
                  m_toolShape = pieces.first();
                  for (int i = 1; i < pieces.size(); ++i)
                      m_toolCreated.append(pieces.at(i));
                  selectPasted(pieces);
                  statusBar()->showMessage(tr("%n convex piece(s)", nullptr, int(pieces.size())),
                                           4000);
              });
}

QVector<ShapeItem *> MainWindow::mirrorSelection(const QVector<ShapeItem *> &source, int side,
                                                 qreal margin)
{
    QVector<ShapeItem *> made;
    if (source.isEmpty())
        return made;

    QRectF all;
    for (ShapeItem *shape : source) {
        const QRectF box = shape->mapToScene(shape->rect()).boundingRect();
        all = all.isNull() ? box : all.united(box);
    }
    // Right, Left, Top, Bottom -- the order the panel lists them in.
    const bool horizontally = side == MirrorRight || side == MirrorLeft;
    const bool ahead = side == MirrorRight || side == MirrorBottom;
    const qreal edge = side == MirrorRight    ? all.right()
                       : side == MirrorLeft   ? all.left()
                       : side == MirrorTop    ? all.top()
                                              : all.bottom();
    const qreal axis = edge + (ahead ? margin / 2.0 : -margin / 2.0);

    for (ShapeItem *one : source) {
        ShapeItem *copy = SceneSerializer::shapeFromJson(SceneSerializer::shapeToJson(one));
        if (!copy)
            continue;
        m_scene->addItem(copy);
        copy->setName(Naming::nextName(one->typeName(), m_scene->takenNames(copy)));
        flipShape(copy, horizontally);

        // Measured after the flip, since flipping is about the shape's own
        // origin and moves whatever it covers.
        const QPointF was = one->mapToScene(one->rect()).boundingRect().center();
        const QPointF now = copy->mapToScene(copy->rect()).boundingRect().center();
        const QPointF wanted = horizontally ? QPointF(2.0 * axis - was.x(), was.y())
                                            : QPointF(was.x(), 2.0 * axis - was.y());
        copy->setPos(copy->pos() + (wanted - now));
        made.append(copy);
    }
    return made;
}

QVector<ShapeItem *> MainWindow::cloneSelection(const QVector<ShapeItem *> &source, int rows,
                                                int columns, qreal gapX, qreal gapY)
{
    QVector<ShapeItem *> made;
    if (source.isEmpty() || rows < 1 || columns < 1)
        return made;

    QRectF all;
    for (ShapeItem *shape : source) {
        const QRectF box = shape->mapToScene(shape->rect()).boundingRect();
        all = all.isNull() ? box : all.united(box);
    }
    const qreal pitchX = all.width() + gapX;
    const qreal pitchY = all.height() + gapY;

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (row == 0 && column == 0)
                continue;
            const QPointF offset(column * pitchX, row * pitchY);
            for (ShapeItem *one : source) {
                // Through the same JSON the clipboard uses, so a clone and a
                // paste cannot drift apart in what they carry.
                ShapeItem *copy = SceneSerializer::shapeFromJson(SceneSerializer::shapeToJson(one));
                if (!copy)
                    continue;
                copy->setPos(one->pos() + offset);
                m_scene->addItem(copy);
                copy->setName(Naming::nextName(one->typeName(), m_scene->takenNames(copy)));
                made.append(copy);
            }
        }
    }
    return made;
}

void MainWindow::buildAlignMenu()
{
    struct Entry {
        CanvasScene::Align edge;
        QString text;
        QIcon icon;
    };
    const QVector<Entry> entries {
        { CanvasScene::Align::Left, tr("Align &Left"), Icons::alignLeft() },
        { CanvasScene::Align::HorizontalCentre, tr("Align &Centres Across"),
          Icons::alignHorizontalCentre() },
        { CanvasScene::Align::Right, tr("Align &Right"), Icons::alignRight() },
        { CanvasScene::Align::Top, tr("Align &Top"), Icons::alignTop() },
        { CanvasScene::Align::VerticalCentre, tr("Align C&entres Down"),
          Icons::alignVerticalCentre() },
        { CanvasScene::Align::Bottom, tr("Align &Bottom"), Icons::alignBottom() },
        { CanvasScene::Align::DistributeAcross, tr("&Distribute Across"),
          Icons::distributeAcross() },
        { CanvasScene::Align::DistributeDown, tr("D&istribute Down"),
          Icons::distributeDown() },
        { CanvasScene::Align::RingOutside, tr("Ring &Outside"), Icons::alignRingOutside() },
        { CanvasScene::Align::RingInside, tr("Ring &Inside"), Icons::alignRingInside() },
    };

    for (int i = 0; i < entries.size(); ++i) {
        const Entry &entry = entries.at(i);
        auto *action = m_ui->menuAlign->addAction(entry.icon, entry.text);
        const CanvasScene::Align edge = entry.edge;
        const QString text = entry.text;
        connect(action, &QAction::triggered, this, [this, edge, text] {
            const bool ring =
                edge == CanvasScene::Align::RingOutside || edge == CanvasScene::Align::RingInside;
            const bool spread = edge == CanvasScene::Align::DistributeAcross
                                || edge == CanvasScene::Align::DistributeDown;
            if (!ring && !spread) {
                m_scene->alignSelection(edge);
                return;
            }
            // Spacing opens on the gap that leaves the selection spanning what
            // it spans now, so Apply without touching anything is the plain
            // "space these evenly" everyone expects.
            const qreal start =
                spread ? m_scene->evenGap(edge == CanvasScene::Align::DistributeAcross) : 0.0;
            const QVector<ToolOption> options {
                { QStringLiteral("gap"), tr("Gap"), start, -10000.0, 10000.0, 1.0, 1,
                  {}, {} },
            };
            QString title = text;
            title.remove(QLatin1Char('&'));
            beginTool(title, title, options, [this, edge] {
                m_scene->alignSelection(edge, m_toolOptions->value(QStringLiteral("gap")));
            });
        });
        if (i == 2 || i == 5 || i == 7)
            m_ui->menuAlign->addSeparator();   // across, down, evenly, round
    }

    m_ui->actionAlign->setIcon(Icons::align());
    m_ui->actionAlign->setMenu(m_ui->menuAlign);
    m_ui->menuAlign->setIcon(Icons::align());
    m_ui->actionAlign->setEnabled(false);
    m_ui->menuAlign->setEnabled(false);
}

// The scenes opened last, newest first and without repeats. Kept beside the
// rest of the settings so they survive the application closing.
void MainWindow::rememberRecent(const QString &path)
{
    const QString full = QFileInfo(path).absoluteFilePath();
    if (full.isEmpty())
        return;
    m_recentFiles.removeAll(full);
    m_recentFiles.prepend(full);
    while (m_recentFiles.size() > kRecentFiles)
        m_recentFiles.removeLast();

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Files/recent"), m_recentFiles);
    rebuildRecentMenu();
}

// The examples are read from beside the program, the same place the export
// converters come from. A copy built from source has them there too, and one
// without them shows no menu at all -- an "Examples" that opens onto nothing
// is worse than no entry.
void MainWindow::buildExamplesMenu()
{
    if (!m_ui->menuExamples)
        return;

    m_ui->menuExamples->clear();
    const QFileInfoList found =
        QDir(assetFolder(QStringLiteral("examples")))
            .entryInfoList({QStringLiteral("*.phys")}, QDir::Files, QDir::Name);
    if (found.isEmpty()) {
        m_ui->menuExamples->menuAction()->setVisible(false);
        return;
    }

    m_ui->menuExamples->menuAction()->setVisible(true);
    for (const QFileInfo &file : found) {
        // By its file name, tidied: "ball_catcher" reads as "Ball catcher".
        QString label = file.completeBaseName();
        label.replace(QLatin1Char('_'), QLatin1Char(' '));
        if (!label.isEmpty())
            label[0] = label[0].toUpper();

        const QString path = file.absoluteFilePath();
        QAction *action = m_ui->menuExamples->addAction(label);
        action->setStatusTip(QDir::toNativeSeparators(path));
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] {
            if (confirmDiscardChanges(tr("Load Scene")))
                openScene(path);
        });
    }
}

// The same rule as the examples: the pages ship beside the program, and a copy
// without them says nothing rather than offering an empty menu.
void MainWindow::buildDocumentationMenu()
{
    if (!m_ui->menuDocumentation)
        return;

    m_ui->menuDocumentation->clear();
    const QString folder = HelpWindow::documentationFolder();
    if (folder.isEmpty()) {
        m_ui->menuDocumentation->menuAction()->setVisible(false);
        return;
    }

    m_ui->menuDocumentation->menuAction()->setVisible(true);
    for (const QFileInfo &file : QDir(folder).entryInfoList({QStringLiteral("*.md")},
                                                            QDir::Files, QDir::Name)) {
        // Named by the page's own title where it has one, so the menu reads
        // the way the documentation does.
        QFile source(file.absoluteFilePath());
        QString title = file.completeBaseName();
        if (source.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&source);
            if (in.readLine().trimmed() == QLatin1String("---")) {
                while (!in.atEnd()) {
                    const QString line = in.readLine();
                    if (line.trimmed() == QLatin1String("---"))
                        break;
                    if (line.startsWith(QLatin1String("title:")))
                        title = line.mid(6).trimmed();
                }
            }
        }

        const QString name = file.fileName();
        connect(m_ui->menuDocumentation->addAction(title), &QAction::triggered, this,
                [this, name] {
                    if (!m_help)
                        m_help = new HelpWindow(this);
                    m_help->showPage(name);
                });
    }
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_ui->menuRecent)
        return;
    if (m_recentFiles.isEmpty()) {
        QSettings settings(settingsFilePath(), QSettings::IniFormat);
        m_recentFiles = settings.value(QStringLiteral("Files/recent")).toStringList();
        while (m_recentFiles.size() > kRecentFiles)
            m_recentFiles.removeLast();
    }

    m_ui->menuRecent->clear();
    m_ui->menuRecent->setEnabled(!m_recentFiles.isEmpty());
    for (int i = 0; i < m_recentFiles.size(); ++i) {
        const QString path = m_recentFiles.at(i);
        // Numbered, so the first four can be reached without reading: the name
        // alone is what tells two scenes apart, and the whole path is the hint.
        auto *action = m_ui->menuRecent->addAction(
            tr("&%1  %2").arg(i + 1).arg(QFileInfo(path).fileName()));
        action->setStatusTip(QDir::toNativeSeparators(path));
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] {
            if (!confirmDiscardChanges(tr("Load Scene")))
                return;
            if (openScene(path))
                return;
            // Gone, or no longer readable: it has no business being offered again.
            m_recentFiles.removeAll(path);
            QSettings settings(settingsFilePath(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Files/recent"), m_recentFiles);
            rebuildRecentMenu();
        });
    }
}

// Aligning needs two shapes to line up against each other, so the button
// follows the selection rather than the mode: it was set only when the mode
// changed, and picking a second shape left it off.
void MainWindow::updateAlignActions()
{
    const bool can = m_scene->canAlignSelection();
    m_ui->actionAlign->setEnabled(can);
    m_ui->menuAlign->setEnabled(can);

    // Aligning needs two shapes to line up against each other; cloning needs
    // only the one. Said here because nothing else ever turned Tools back on,
    // so one run left it greyed out for good.
    if (m_toolsAction) {
        const bool running = m_simulation && m_simulation->isActive();
        m_toolsAction->setEnabled(!running && m_scene->editorMode() == EditorMode::Edit
                                  && m_scene->activeItem() != nullptr);
    }
}

void MainWindow::onActiveItemChanged(ShapeItem *item)
{
    updateAlignActions();
    if (!item) {
        m_ui->actionMoveScale->setEnabled(false);
        m_ui->actionMoveScale->setChecked(false);
        m_ui->actionEditNodes->setEnabled(false);
        m_ui->actionEditNodes->setChecked(false);
        m_ui->actionRotate->setEnabled(false);
        m_ui->actionRotate->setChecked(false);
        updateDeleteAction();
        updateCopyAction();
        m_statusHelpLabel->setText(m_scene->geometryEditingAllowed()
            ? tr("Click a shape to select it • Add → Rectangle/Circle/Polygon to create a new shape")
            : tr("Click a shape to set its physical properties"
                 " • With nothing selected the panel shows the world's"));
        return;
    }

    const ShapeMode mode = item->mode();
    const bool editable = item->supportsNodeEditing();
    // All three change geometry -- see CanvasScene::geometryEditingAllowed().
    const bool geometry = m_scene->geometryEditingAllowed();

    updateCopyAction();
    updateDeleteAction();

    m_ui->actionMoveScale->setChecked(mode == ShapeMode::Selected);
    m_ui->actionMoveScale->setEnabled(geometry && mode != ShapeMode::Selected);
    m_ui->actionEditNodes->setChecked(mode == ShapeMode::Editing);
    m_ui->actionEditNodes->setEnabled(geometry && editable && mode != ShapeMode::Editing);
    m_ui->actionRotate->setChecked(mode == ShapeMode::Rotating);
    m_ui->actionRotate->setEnabled(geometry && mode != ShapeMode::Rotating);

    if (!geometry) {
        m_statusHelpLabel->setText(
            tr("Drag with the left mouse button to move • Set the shape's physical properties"
               " in the panel • Click elsewhere to deselect"));
    } else if (mode == ShapeMode::Rotating) {
        m_statusHelpLabel->setText(
            tr("Drag with the left mouse button to rotate around the origin • Drag the green point"
               " to move the origin (Shift ignores the grid) • Double-click or Move/Scale to go"
               " back • Delete to remove"));
    } else if (mode == ShapeMode::Editing) {
        m_statusHelpLabel->setText(
            tr("Drag a node to move it (Shift ignores the grid) • Shift+Click to select multiple"
               " nodes • Enter to add a node between 2 adjacent, or close an open shape"
               " from its endpoints • Delete to remove selected nodes"));
    } else {
        m_statusHelpLabel->setText(
            m_scene->editSelection().isEmpty()
                ? tr("Drag to move, or a handle to resize — hold Shift to ignore the grid"
                     " • Shift+Click another shape to move, resize and rotate them together"
                     " • Double-click or Rotate to rotate%1 • Delete to remove"
                     " • Click elsewhere to deselect")
                      .arg(editable ? tr(" • Edit to edit nodes") : QString())
                : tr("%n shapes — drag to move them, a corner to resize them together,"
                     " Rotate to turn them about a pivot you can drag"
                     " • Shift+Click to add or drop one • Delete removes them all",
                     nullptr, int(m_scene->editSelection().size()) + 1));
    }
}

bool MainWindow::addCreateBodyActions(QMenu *menu, const QPointF &scenePos)
{
    ShapeItem *loose = m_scene->looseShapeAt(scenePos);
    if (!loose)
        return false;

    // Right-clicking something outside the selection means that shape, the way
    // it does everywhere else; right-clicking one already in it keeps the rest.
    if (!m_scene->isSelectedForPhysics(loose))
        m_scene->selectForPhysics(loose);

    // Only shapes with no body of their own: the entries make a body, and one
    // already in a body would be taken out of it without the menu saying so.
    int count = 0;
    for (ShapeItem *shape : m_scene->physicsSelection())
        count += shape->body() ? 0 : 1;
    if (count == 0 || count != m_scene->physicsSelection().size())
        return false;

    const auto entry = [this, menu, count](const QString &label, physics::BodyType type,
                                           const QString &tip) {
        QAction *action = menu->addAction(
            count == 1 ? label : tr("%1 from %n Shapes", nullptr, count).arg(label),
            this, [this, type] { createBodyOfType(type); });
        action->setToolTip(tip);
    };

    entry(tr("Create Static Body"), physics::BodyType::Static,
          tr("Never moves, whatever hits it: the ground, a wall, a ramp."));
    entry(tr("Create Kinematic Body"), physics::BodyType::Kinematic,
          tr("Moved by a velocity you set, and pushed by nothing: a lift, a conveyor."));
    entry(tr("Create Dynamic Body"), physics::BodyType::Dynamic,
          tr("Falls, collides and is pushed around like a real object."));
    return true;
}

void MainWindow::on_canvasView_customContextMenuRequested(const QPoint &pos)
{
    QMenu menu(this);

    // Most entries change the scene and nothing does that during a run -- but
    // the view commands do not, and framing the field or reading the names is
    // exactly what a run is watched with, so they are still offered.
    if (m_simulation && m_simulation->isActive()) {
        addCanvasActions(&menu);
        menu.exec(m_ui->canvasView->mapToGlobal(pos));
        return;
    }

    ShapeItem *item = m_scene->activeItem();

    if (m_scene->editorMode() != EditorMode::Edit) {
        const bool simulating = m_simulation && m_simulation->isActive();

        const QPointF scenePos = m_ui->canvasView->mapToScene(pos);
        Joint *joint = m_scene->jointAt(scenePos);
        if (!joint)
            joint = m_scene->selectedJoint();
        else if (!simulating)
            m_scene->selectJoint(joint);

        if (joint && joint->anchorCount() > 0) {
            addAnchorActions(&menu, joint, simulating);
            menu.addSeparator();
        }

        // A shape not yet in a body takes no part in a run at all. Offered
        // here by kind, so the body it becomes is chosen outright rather than
        // made and then changed in the property table.
        if (!joint && addCreateBodyActions(&menu, scenePos))
            menu.addSeparator();

        QAction *centreAction =
            menu.addAction(tr("Move Origins to Center of Mass"), this,
                           &MainWindow::onOriginsToCenterOfMass);
        centreAction->setEnabled(!m_scene->physicsSelection().isEmpty()
                                 && !(m_simulation && m_simulation->isActive()));
        centreAction->setToolTip(tr("Moves every origin in each selected body onto that body's"
                                      " centre of mass."));
        addCanvasActions(&menu);
        menu.exec(m_ui->canvasView->mapToGlobal(pos));
        return;
    }

    if (!item) {
        menu.addMenu(m_ui->menuAddShape);
        addCanvasActions(&menu);
        menu.exec(m_ui->canvasView->mapToGlobal(pos));
        return;
    } else {
        menu.addAction(tr("Duplicate"), this, [this, item] { duplicateShape(item); });

        menu.addAction(tr("Flip Horizontally"), this, [this, item] { flipShape(item, true); });
        menu.addAction(tr("Flip Vertically"), this, [this, item] { flipShape(item, false); });

        if (item->typeName() == QLatin1String("circle")) {
            QAction *toChain = menu.addAction(tr("Convert to Chain…"), this,
                                              [this, item] { circleToChain(item); });
            toChain->setToolTip(tr("Replaces it with a closed chain of links round its rim,"
                                   " each one its own body, jointed to its neighbours."));
            toChain->setEnabled(!(m_simulation && m_simulation->isActive()));
        }

        if (item->typeName() == QLatin1String("circle")
            || item->typeName() == QLatin1String("rectangle")) {
            QAction *toSegments = menu.addAction(tr("Convert to Segments…"), this,
                                                 [this, item] { shapeToSegments(item); });
            toSegments->setToolTip(tr("Replaces it with one line per side of its outline, each"
                                      " its own shape and in no body -- so they can be made"
                                      " into bodies and jointed together."));
            toSegments->setEnabled(!(m_simulation && m_simulation->isActive()));
        }

        if (item->typeName() == QLatin1String("rectangle")) {
            QAction *toPolygon = menu.addAction(tr("Convert to Polygon"), this,
                                                [this, item] { convertToPolygon(item); });
            toPolygon->setToolTip(tr("Replaces it with the polygon of its four corners, keeping"
                                     " its name, its place in its body and its physics. The"
                                     " corner radius goes: only a rectangle has one."));
            toPolygon->setEnabled(!(m_simulation && m_simulation->isActive()));
        }

        if (m_scene->canSplitIntoConvexPieces(item)) {
            QAction *split = menu.addAction(tr("Split into Convex Pieces…"), this,
                                            [this, item] { splitIntoConvexPieces(item); });
            split->setToolTip(tr("Cuts the outline into convex pieces within the point limit,"
                                 " so it can be solid and belong to a body that moves. The"
                                 " pieces stay in its body and the first keeps its name."));
            split->setEnabled(!(m_simulation && m_simulation->isActive()));
        }

        menu.addSeparator();
        menu.addAction(m_ui->actionDelete);
        menu.addSeparator();

        menu.addAction(tr("Move to Center"), this, [item] {
            item->setPos(-item->rect().center());
        });

        QMenu *originMenu = menu.addMenu(tr("Move Origin To"));
        struct OriginSpot { const char *label; qreal fx, fy; };
        // Fractions of the shape's own rect, so this works whatever its size.
        static const OriginSpot spots[] = {
            { QT_TR_NOOP("Center"),              0.5, 0.5 },
            { QT_TR_NOOP("Top"),                 0.5, 0.0 },
            { QT_TR_NOOP("Bottom"),              0.5, 1.0 },
            { QT_TR_NOOP("Left"),                0.0, 0.5 },
            { QT_TR_NOOP("Right"),               1.0, 0.5 },
            { QT_TR_NOOP("Top-Left Corner"),     0.0, 0.0 },
            { QT_TR_NOOP("Top-Right Corner"),    1.0, 0.0 },
            { QT_TR_NOOP("Bottom-Left Corner"),  0.0, 1.0 },
            { QT_TR_NOOP("Bottom-Right Corner"), 1.0, 1.0 },
        };
        for (const OriginSpot &spot : spots) {
            const QString label = tr(spot.label);
            const qreal fx = spot.fx;
            const qreal fy = spot.fy;
            originMenu->addAction(label, this, [this, item, label, fx, fy] {
                const QRectF r = item->rect();
                item->setOrigin(QPointF(r.left() + r.width() * fx,
                                        r.top() + r.height() * fy));
                m_scene->notifyEdit(tr("Move %1 origin to %2").arg(item->name(), label));
            });
        }

        menu.addSeparator();

        QAction *snapToGridAction = menu.addAction(tr("Snap to Grid"));
        snapToGridAction->setCheckable(true);
        snapToGridAction->setChecked(m_scene->snapToGrid());
        connect(snapToGridAction, &QAction::triggered, this, [this](bool checked) {
            m_scene->setSnapToGrid(checked);
            saveSettingsToFile(currentSettingsSnapshot());
        });

        QMenu *snapMenu = menu.addMenu(tr("Snap To"));
        auto *snapGroup = new QActionGroup(snapMenu);
        snapGroup->setExclusive(true);

        QAction *snapPositionAction = snapMenu->addAction(tr("Position (Top-Left Corner)"));
        snapPositionAction->setCheckable(true);
        snapPositionAction->setChecked(m_scene->snapPoint() == SnapPoint::Position);
        snapGroup->addAction(snapPositionAction);
        connect(snapPositionAction, &QAction::triggered, this, [this] {
            m_scene->setSnapPoint(SnapPoint::Position);
            saveSettingsToFile(currentSettingsSnapshot());
        });

        QAction *snapOriginAction = snapMenu->addAction(tr("Origin Point"));
        snapOriginAction->setCheckable(true);
        snapOriginAction->setChecked(m_scene->snapPoint() == SnapPoint::Origin);
        snapGroup->addAction(snapOriginAction);
        connect(snapOriginAction, &QAction::triggered, this, [this] {
            m_scene->setSnapPoint(SnapPoint::Origin);
            saveSettingsToFile(currentSettingsSnapshot());
        });

        menu.addSeparator();

        menu.addAction(tr("Reset Rotation"), this, [item] {
            // Un-rotate first, so the origin lands on the untransformed centre.
            item->setRotation(0);
            item->setOrigin(item->rect().center());
        });

        menu.addSeparator();
        menu.addAction(m_ui->actionBringToFront);
        menu.addAction(m_ui->actionBringForward);
        menu.addAction(m_ui->actionSendBackward);
        menu.addAction(m_ui->actionSendToBack);
    }

    addCanvasActions(&menu);
    menu.exec(m_ui->canvasView->mapToGlobal(pos));
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog dialog(m_version, this);
    dialog.exec();
}

// The whole preferences file, as it is stored: every group, every key. A
// converter needs the parts the scene itself does not carry -- which colour a
// dynamic body is drawn in, and so on -- and reading the lot means the app
// does not have to decide in advance which of them matter to somebody else's
// format.
// One QSettings group, and everything under it, as nested objects. Groups
// nest -- a converter's own settings live under Export/<id> -- so this has to
// recurse or a script would be handed keys with slashes in them.
static QJsonObject groupAsJson(QSettings &settings)
{
    QJsonObject values;
    for (const QString &key : settings.childKeys())
        values.insert(key, QJsonValue::fromVariant(settings.value(key)));
    for (const QString &group : settings.childGroups()) {
        settings.beginGroup(group);
        values.insert(group, groupAsJson(settings));
        settings.endGroup();
    }
    return values;
}

// The whole preferences file, as it is stored. A converter needs the parts the
// scene itself does not carry -- which colour a dynamic body is drawn in, and
// so on -- and reading the lot means the app does not have to decide in
// advance which of them matter to somebody else's format.
static QJsonObject settingsAsJson(const QString &path)
{
    QSettings settings(path, QSettings::IniFormat);
    return groupAsJson(settings);
}

void MainWindow::refreshExportMenu()
{
    QMenu *menu = m_ui->menuExport;
    menu->clear();

    const QVector<SceneExporter::Converter> converters = SceneExporter::discover();
    if (converters.isEmpty()) {
        QAction *none = menu->addAction(tr("No converters installed..."));
        none->setEnabled(false);
        return;
    }

    for (const SceneExporter::Converter &converter : converters) {
        QAction *action = menu->addAction(converter.name);
        action->setToolTip(converter.description);
        action->setStatusTip(converter.description);
        connect(action, &QAction::triggered, this, [this, converter] {
            const QString folder = QFileDialog::getExistingDirectory(
                this, tr("Export %1 into").arg(converter.name));
            if (folder.isEmpty())
                return;

            QString error;
            QStringList written;
            QStringList log;
            bool converted = false;
            converted = SceneExporter::run(converter, m_scene, folder,
                                           settingsAsJson(settingsFilePath()), &error, &written, &log);

            // Whatever the converter had to say for itself, under whichever
            // message it gets. It is the only thing that knows what it did.
            const QString said = log.join(QLatin1Char('\n'));

            if (!converted) {
                QMessageBox failed(this);
                failed.setIcon(QMessageBox::Critical);
                failed.setWindowTitle(tr("Export failed"));
                failed.setText(tr("%1 could not convert this scene.").arg(converter.name));
                failed.setInformativeText(error);
                if (!said.isEmpty())
                    failed.setDetailedText(said);
                failed.exec();
                return;
            }

            QMessageBox done(this);
            done.setIcon(QMessageBox::Information);
            done.setWindowTitle(tr("Export finished"));
            done.setText(tr("%1 wrote %n file(s).", nullptr, written.size())
                             .arg(converter.name));
            done.setInformativeText(folder);
            // The file list belongs behind Show Details: it is worth having,
            // and a converter that writes thirty files should not fill the
            // screen with them.
            QStringList details = written;
            if (!said.isEmpty())
                details << QString() << said;
            done.setDetailedText(details.join(QLatin1Char('\n')));
            done.exec();
        });
    }
    menu->setToolTipsVisible(true);
}

void MainWindow::on_actionOptions_triggered()
{
    OptionsDialog dialog(currentSettingsSnapshot(), this);
    if (dialog.exec() == QDialog::Accepted) {
        // Reset closes the dialog, so the pages are never read: what they show
        // is what is being thrown away.
        OptionsDialog::Settings s = dialog.resetToDefaults() ? defaultSettings()
                                                             : dialog.settings();
        // The engine is the one setting that reaches the open scene, because a
        // scene is built for one. Keeping the scene keeps its engine too.
        if (!adoptEngine(s.defaultEngineName))
            s.defaultEngineName = m_scene->simulationEngineName();
        applySettings(s);
        saveSettingsToFile(s);
    }
}

// The same read as always, over a file with nothing in it. A default has one
// home -- the Settings struct, and the few the read above works out for itself
// -- and a second list of them here would drift from it the first time one
// changed.
OptionsDialog::Settings MainWindow::defaultSettings() const
{
    QTemporaryDir empty;
    return loadSettingsFromFile(empty.filePath(QStringLiteral("settings.ini")));
}

void MainWindow::onFieldSettingsChanged()
{
    // The edit already applied itself to m_scene; just persist the result.
    saveSettingsToFile(currentSettingsSnapshot());
}

OptionsDialog::Settings MainWindow::currentSettingsSnapshot() const
{
    OptionsDialog::Settings current;
    current.defaultEngineName = engineForNewScenes();
    current.theme = Theme::current();
    current.controls = Theme::currentControls();
    current.converterSettings = m_converterSettings;
    // The field belongs to the scene and travels in its file; what Options
    // holds is what the next new scene starts with.
    current.fieldWidth = m_fieldDefaults.fieldWidth;
    current.fieldHeight = m_fieldDefaults.fieldHeight;
    current.backgroundColor = m_fieldDefaults.backgroundColor;
    current.showGrid = m_fieldDefaults.showGrid;
    current.gridCellSize = m_fieldDefaults.gridCellSize;
    current.gridColor = m_fieldDefaults.gridColor;
    current.snapToGrid = m_scene->snapToGrid();
    current.snapPoint = m_scene->snapPoint();
    current.snapStep = m_scene->snapStep();
    current.snapSensitivity = m_scene->snapSensitivity();
    current.currentScale = m_scene->currentScale();
    current.scaleMin = m_scene->scaleMin();
    current.scaleMax = m_scene->scaleMax();
    current.scaleStep = m_scene->scaleStep();
    current.shapeStyles = m_scene->defaultShapeStyles();
    current.logFontFamily = m_logFontFamily;
    current.logFontSize = m_logFontSize;
    current.logColor = m_logColor;
    current.logCorner = m_logCorner;
    current.bodyDynamicColor = m_scene->bodyColor(physics::BodyType::Dynamic);
    current.bodyStaticColor = m_scene->bodyColor(physics::BodyType::Static);
    current.bodyKinematicColor = m_scene->bodyColor(physics::BodyType::Kinematic);
    current.unassignedShapeColor = m_scene->unassignedShapeColor();
    current.sensorColor = m_scene->sensorColor();
    current.sensorPattern = m_scene->sensorPattern();
    current.sensorFillsBody = m_scene->sensorFillsBody();
    current.physicsBorderWidth = m_scene->physicsBorderWidth();
    current.physicsFillAlpha = m_scene->physicsFillAlpha();
    current.jointFillAlpha = m_scene->jointFillAlpha();
    current.jointAnchorOpacity = m_scene->jointAnchorOpacity();
    current.physicsSelectionLineStyle = m_scene->physicsSelectionLineStyle();
    current.physicsSelectionLineWidth = m_scene->physicsSelectionLineWidth();
    current.physicsSelectionColor = m_scene->physicsSelectionColor();
    current.sleepShading = m_scene->runLayer(CanvasScene::RunLayer::SleepShading);
    current.runShowGrid = m_scene->runLayer(CanvasScene::RunLayer::Grid);
    current.runShowJoints = m_scene->runLayer(CanvasScene::RunLayer::Joints);
    current.runShowBodyAxes = m_scene->runLayer(CanvasScene::RunLayer::BodyAxes);
    current.runShowRays = m_scene->runLayer(CanvasScene::RunLayer::Rays);
    current.runShowSensors = m_scene->runLayer(CanvasScene::RunLayer::Sensors);
    current.runShowTrails = m_scene->runLayer(CanvasScene::RunLayer::Trails);
    current.runShowExplosions = m_scene->runLayer(CanvasScene::RunLayer::Explosions);
    if (m_fullScreenCheck)
        current.simulationFullScreen = m_fullScreenCheck->isChecked();
    current.showBodyAxes = m_scene->showBodyAxes();
    current.bodyAxisLength = m_scene->bodyAxisLength();
    current.bodyAxisWidth = m_scene->bodyAxisWidth();
    current.bodyAxisXColor = m_scene->bodyAxisXColor();
    current.bodyAxisYColor = m_scene->bodyAxisYColor();
    current.sleepShiftPercent = m_scene->sleepShiftPercent();
    current.maxPolygonVertices = m_scene->maxPolygonVertices();
    current.jointColor = m_scene->jointColor();
    current.undoDepth = m_undo->capacity();
    current.jointKindColors = m_scene->jointKindColors();
    current.jointKindStyles = m_scene->jointKindStyles();
    current.jointSelectionColor = m_scene->jointSelectionColor();
    current.jointSelectionLineWidth = m_scene->jointSelectionLineWidth();
    current.jointSelectionLineStyle = m_scene->jointSelectionLineStyle();
    current.simulationEngineName = m_scene->simulationEngineName();
    current.jointOutlineColor = m_scene->jointOutlineColor();
    current.shotLightColor = m_scene->shotLightColor();
    current.shotFullColor = m_scene->shotFullColor();
    current.shotLineWidth = m_scene->shotLineWidth();
    current.shotLineStyle = m_scene->shotLineStyle();
    current.trailColor = m_scene->trailColor();
    current.trailLineWidth = m_scene->trailLineWidth();
    current.trailLineStyle = m_scene->trailLineStyle();
    current.jointAnchorRadius = m_scene->jointAnchorRadius();
    current.jointAxisLength = m_scene->jointAxisLength();
    current.jointWaistWidth = m_scene->jointWaistWidth();
    current.springsAsCoils = m_scene->springsAsCoils();
    current.springPitch = m_scene->springPitch();
    current.springWidth = m_scene->springWidth();
    current.jointOutlineWidth = m_scene->jointOutlineWidth();
    if (m_simulation) {
        current.simulationStepsPerSecond = m_simulation->stepsPerSecond();
        current.simulationSpeed = m_simulation->speed();
    }
    current.pixelsPerMeter = m_scene->pixelsPerMeter();
    current.rulerUnit = m_ui->topRuler->unit();
    current.fieldBoundsSolid = m_scene->fieldBoundsSolid();
    current.selectionLineStyle = m_scene->selectionLineStyle();
    current.selectionLineWidth = m_scene->selectionLineWidth();
    current.selectionColor = m_scene->selectionColor();
    current.handleShape = m_scene->handleShape();
    current.handleSize = m_scene->handleSize();
    current.handleColor = m_scene->handleColor();
    current.handleBorderWidth = m_scene->handleBorderWidth();
    current.handleBorderColor = m_scene->handleBorderColor();
    return current;
}

void MainWindow::applyFieldDefaults()
{
    m_scene->setFieldSize(m_fieldDefaults.fieldWidth, m_fieldDefaults.fieldHeight);
    m_scene->setBackgroundColor(m_fieldDefaults.backgroundColor);
    m_scene->setShowGrid(m_fieldDefaults.showGrid);
    m_scene->setGridCellSize(m_fieldDefaults.gridCellSize);
    m_scene->setGridColor(m_fieldDefaults.gridColor);
}

void MainWindow::applyRulerUnit(Units::Unit unit)
{
    m_ui->topRuler->setUnit(unit);
    m_ui->leftRuler->setUnit(unit);
}

void MainWindow::applySceneLook(CanvasScene *scene, const OptionsDialog::Settings &s)
{
    scene->setSnapToGrid(s.snapToGrid);
    scene->setSnapPoint(s.snapPoint);
    scene->setSnapStep(s.snapStep);
    scene->setSnapSensitivity(s.snapSensitivity);
    // Bounds/step before the current value, so it clamps against the
    // settings actually being applied rather than whatever was set before.
    scene->setScaleMin(s.scaleMin);
    scene->setScaleMax(s.scaleMax);
    scene->setScaleStep(s.scaleStep);
    scene->setCurrentScale(s.currentScale);
    scene->setDefaultShapeStyles(s.shapeStyles);
    scene->setBodyColor(physics::BodyType::Dynamic, s.bodyDynamicColor);
    scene->setBodyColor(physics::BodyType::Static, s.bodyStaticColor);
    scene->setBodyColor(physics::BodyType::Kinematic, s.bodyKinematicColor);
    scene->setUnassignedShapeColor(s.unassignedShapeColor);
    scene->setSensorColor(s.sensorColor);
    scene->setSensorPattern(s.sensorPattern);
    scene->setSensorFillsBody(s.sensorFillsBody);
    scene->setPhysicsBorderWidth(s.physicsBorderWidth);
    scene->setPhysicsFillAlpha(s.physicsFillAlpha);
    scene->setJointFillAlpha(s.jointFillAlpha);
    scene->setJointAnchorOpacity(s.jointAnchorOpacity);
    scene->setPhysicsSelectionLineStyle(s.physicsSelectionLineStyle);
    scene->setPhysicsSelectionLineWidth(s.physicsSelectionLineWidth);
    scene->setPhysicsSelectionColor(s.physicsSelectionColor);
    scene->setRunLayer(CanvasScene::RunLayer::SleepShading, s.sleepShading);
    scene->setRunLayer(CanvasScene::RunLayer::Grid, s.runShowGrid);
    scene->setRunLayer(CanvasScene::RunLayer::Joints, s.runShowJoints);
    scene->setRunLayer(CanvasScene::RunLayer::BodyAxes, s.runShowBodyAxes);
    scene->setRunLayer(CanvasScene::RunLayer::Rays, s.runShowRays);
    scene->setRunLayer(CanvasScene::RunLayer::Sensors, s.runShowSensors);
    scene->setRunLayer(CanvasScene::RunLayer::Trails, s.runShowTrails);
    scene->setRunLayer(CanvasScene::RunLayer::Explosions, s.runShowExplosions);
    scene->setShowBodyAxes(s.showBodyAxes);
    scene->setBodyAxisLength(s.bodyAxisLength);
    scene->setBodyAxisWidth(s.bodyAxisWidth);
    scene->setBodyAxisXColor(s.bodyAxisXColor);
    scene->setBodyAxisYColor(s.bodyAxisYColor);
    scene->setSleepShiftPercent(s.sleepShiftPercent);
    scene->setMaxPolygonVertices(s.maxPolygonVertices);
    scene->setJointColor(s.jointColor);
    scene->setJointKindColors(s.jointKindColors);
    scene->setJointKindStyles(s.jointKindStyles);
    scene->setJointSelectionColor(s.jointSelectionColor);
    scene->setJointSelectionLineWidth(s.jointSelectionLineWidth);
    scene->setJointSelectionLineStyle(s.jointSelectionLineStyle);
    scene->setJointOutlineColor(s.jointOutlineColor);
    scene->setShotLineColors(s.shotLightColor, s.shotFullColor);
    scene->setShotLineWidth(s.shotLineWidth);
    scene->setShotLineStyle(s.shotLineStyle);
    scene->setTrailColor(s.trailColor);
    scene->setTrailLineWidth(s.trailLineWidth);
    scene->setTrailLineStyle(s.trailLineStyle);
    scene->setJointAnchorRadius(s.jointAnchorRadius);
    scene->setJointAxisLength(s.jointAxisLength);
    scene->setJointWaistWidth(s.jointWaistWidth);
    scene->setSpringsAsCoils(s.springsAsCoils);
    scene->setSpringPitch(s.springPitch);
    scene->setSpringWidth(s.springWidth);
    scene->setJointOutlineWidth(s.jointOutlineWidth);
    scene->setSelectionLineStyle(s.selectionLineStyle);
    scene->setSelectionLineWidth(s.selectionLineWidth);
    scene->setSelectionColor(s.selectionColor);
    scene->setHandleShape(s.handleShape);
    scene->setHandleSize(s.handleSize);
    scene->setHandleColor(s.handleColor);
    scene->setHandleBorderWidth(s.handleBorderWidth);
    scene->setHandleBorderColor(s.handleBorderColor);
}

void MainWindow::applySettings(const OptionsDialog::Settings &s)
{
    // Only new scenes: what is open was built for the engine it names.
    m_defaultEngineName = s.defaultEngineName;
    Theme::apply(s.theme, s.controls);
    m_converterSettings = s.converterSettings;
    // Kept for the next new scene. Changing it does not repaint the scene
    // that is open: that one's field came from its own file, and is saved
    // back to it.
    m_fieldDefaults.fieldWidth = s.fieldWidth;
    m_fieldDefaults.fieldHeight = s.fieldHeight;
    m_fieldDefaults.backgroundColor = s.backgroundColor;
    m_fieldDefaults.showGrid = s.showGrid;
    m_fieldDefaults.gridCellSize = s.gridCellSize;
    m_fieldDefaults.gridColor = s.gridColor;
    applySceneLook(m_scene, s);
    m_logFontFamily = s.logFontFamily;
    m_logFontSize = s.logFontSize;
    m_logColor = s.logColor;
    m_logCorner = s.logCorner;
    applyLogStyle();
    syncTransportWidgets(s);
    m_undo->setCapacity(s.undoDepth);
    if (m_simulation) {
        m_simulation->setStepsPerSecond(s.simulationStepsPerSecond);
        m_simulation->setSpeed(s.simulationSpeed);
    }
    m_scene->setPixelsPerMeter(s.pixelsPerMeter);
    applyRulerUnit(s.rulerUnit);
    m_scene->setFieldBoundsSolid(s.fieldBoundsSolid);

    // Rebuilt so a changed Scale min/max/step shows immediately.
    if (m_ui->propertyPanel)
        m_ui->propertyPanel->setActiveItem(m_scene->activeItem());
}

QString MainWindow::chooseSettingsFile(const QString &redirect, const QString &configDir,
                                       const QString &besideExecutable)
{
    if (!redirect.isEmpty())
        return redirect;
    if (configDir.isEmpty())
        return besideExecutable; // nowhere better to go
    return configDir + QStringLiteral("/settings.ini");
}

QString MainWindow::settingsFilePath()
{
    const QString redirect = qEnvironmentVariable("PHYSALIS_SETTINGS");
    if (!redirect.isEmpty())
        return redirect;

    // Worked out once: every setting read or written asks for this, and the
    // carrying over below is a one-off.
    static const QString chosen = [] {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/settings.ini");

        // Where the platform keeps a user's own settings, which is what Qt
        // answers here. They used to go beside the executable, and that is a
        // place an installed program cannot write -- Program Files on Windows,
        // <libdir>/physalis on Linux -- so every setting was dropped on exit
        // and only a build run from its own folder ever kept any.
        QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (!dir.isEmpty() && !QDir().mkpath(dir))
            dir.clear();

        const QString own = chooseSettingsFile(QString(), dir, beside);
        // Carried over once, so a copy that has been run from its own folder
        // keeps what it had when its settings move here.
        if (own != beside && !QFileInfo::exists(own) && QFileInfo::exists(beside))
            QFile::copy(beside, own);
        return own;
    }();
    return chosen;
}

OptionsDialog::Settings MainWindow::loadSettingsFromFile(const QString &path) const
{
    OptionsDialog::Settings s; // defaults

    QSettings settings(path.isEmpty() ? settingsFilePath() : path, QSettings::IniFormat);
    s.defaultEngineName = settings.value("Engine/default", s.defaultEngineName).toString();
    s.theme = settings.value("Appearance/theme", s.theme).toString();
    s.controls = settings.value("Appearance/controls", s.controls).toString();
    settings.beginGroup("Field");
    s.fieldWidth = settings.value("width", s.fieldWidth).toDouble();
    s.fieldHeight = settings.value("height", s.fieldHeight).toDouble();
    s.backgroundColor = QColor(settings.value("backgroundColor", s.backgroundColor.name(QColor::HexArgb)).toString());
    s.showGrid = settings.value("showGrid", s.showGrid).toBool();
    s.gridCellSize = settings.value("cellSize", s.gridCellSize).toDouble();
    s.gridColor = QColor(settings.value("gridColor", s.gridColor.name()).toString());
    s.snapToGrid = settings.value("snapToGrid", s.snapToGrid).toBool();
    s.snapPoint = static_cast<SnapPoint>(settings.value("snapPoint", static_cast<int>(s.snapPoint)).toInt());
    s.snapStep = settings.value("snapStep", s.snapStep).toDouble();
    s.snapSensitivity = settings.value("snapSensitivity", s.snapSensitivity).toDouble();
    settings.endGroup();

    settings.beginGroup("Scale");
    // The current zoom is deliberately not restored.
    s.scaleMin = settings.value("min", s.scaleMin).toDouble();
    s.scaleMax = settings.value("max", s.scaleMax).toDouble();
    s.scaleStep = settings.value("step", s.scaleStep).toDouble();
    settings.endGroup();

    settings.beginGroup("Physics");
    s.pixelsPerMeter = settings.value("pixelsPerMeter", s.pixelsPerMeter).toDouble();
    s.rulerUnit = Units::fromKey(settings.value("rulerUnit", Units::key(s.rulerUnit)).toString(),
                                 s.rulerUnit);
    s.fieldBoundsSolid = settings.value("solidBounds", s.fieldBoundsSolid).toBool();
    s.bodyDynamicColor = QColor(settings.value("bodyDynamicColor",
                                               s.bodyDynamicColor.name(QColor::HexArgb)).toString());
    s.bodyStaticColor = QColor(settings.value("bodyStaticColor",
                                              s.bodyStaticColor.name(QColor::HexArgb)).toString());
    s.bodyKinematicColor = QColor(settings.value("bodyKinematicColor",
                                                 s.bodyKinematicColor.name(QColor::HexArgb)).toString());
    s.unassignedShapeColor = QColor(settings.value("unassignedShapeColor",
                                                   s.unassignedShapeColor.name(QColor::HexArgb)).toString());
    s.sensorColor = QColor(settings.value("sensorColor",
                                          s.sensorColor.name(QColor::HexArgb)).toString());
    s.sensorPattern = static_cast<Qt::BrushStyle>(
        settings.value("sensorPattern", static_cast<int>(s.sensorPattern)).toInt());
    s.sensorFillsBody = settings.value("sensorFillsBody", s.sensorFillsBody).toBool();
    s.physicsBorderWidth = settings.value("borderWidth", s.physicsBorderWidth).toDouble();
    s.logFontFamily = settings.value("logFont", s.logFontFamily).toString();
    s.logFontSize = settings.value("logFontSize", s.logFontSize).toInt();
    s.logColor = QColor(settings.value("logColor", s.logColor.name(QColor::HexArgb)).toString());
    s.logCorner = static_cast<Qt::Corner>(settings.value("logCorner", int(s.logCorner)).toInt());
    s.physicsFillAlpha = settings.value("fillAlpha", s.physicsFillAlpha).toInt();
    s.jointFillAlpha = settings.value("jointFillAlpha", s.jointFillAlpha).toInt();
    s.jointAnchorOpacity = settings.value("jointAnchorOpacity", s.jointAnchorOpacity).toInt();
    s.physicsSelectionLineStyle = static_cast<Qt::PenStyle>(
        settings.value("selectionLineStyle", static_cast<int>(s.physicsSelectionLineStyle)).toInt());
    s.physicsSelectionLineWidth = settings.value("selectionLineWidth", s.physicsSelectionLineWidth).toDouble();
    s.physicsSelectionColor = QColor(settings.value("selectionColor",
                                                    s.physicsSelectionColor.name(QColor::HexArgb)).toString());
    // "debugView" is what the one switch these grew out of was called; the
    // key stays so a saved setting is not lost.
    s.sleepShading = settings.value("debugView", s.sleepShading).toBool();
    s.runShowGrid = settings.value("runShowGrid", s.runShowGrid).toBool();
    s.runShowJoints = settings.value("runShowJoints", s.runShowJoints).toBool();
    s.runShowBodyAxes = settings.value("runShowBodyAxes", s.runShowBodyAxes).toBool();
    s.runShowRays = settings.value("runShowRays", s.runShowRays).toBool();
    s.runShowSensors = settings.value("runShowSensors", s.runShowSensors).toBool();
    s.runShowTrails = settings.value("runShowTrails", s.runShowTrails).toBool();
    s.runShowExplosions =
        settings.value("runShowExplosions", s.runShowExplosions).toBool();
    s.showBodyAxes = settings.value("showBodyAxes", s.showBodyAxes).toBool();
    s.bodyAxisLength = settings.value("bodyAxisLength", s.bodyAxisLength).toDouble();
    s.bodyAxisWidth = settings.value("bodyAxisWidth", s.bodyAxisWidth).toDouble();
    s.bodyAxisXColor = QColor(settings.value("bodyAxisXColor",
                                             s.bodyAxisXColor.name(QColor::HexArgb)).toString());
    s.bodyAxisYColor = QColor(settings.value("bodyAxisYColor",
                                             s.bodyAxisYColor.name(QColor::HexArgb)).toString());
    s.sleepShiftPercent = settings.value("sleepShiftPercent", s.sleepShiftPercent).toInt();
    s.maxPolygonVertices = settings.value("maxPolygonVertices", s.maxPolygonVertices).toInt();
    s.jointColor = QColor(settings.value("jointColor", s.jointColor.name(QColor::HexArgb)).toString());
    s.jointOutlineColor =
        QColor(settings.value("jointOutlineColor", s.jointOutlineColor.name(QColor::HexArgb)).toString());
    s.jointAnchorRadius = settings.value("jointAnchorRadius", s.jointAnchorRadius).toDouble();
    s.jointAxisLength = settings.value("jointAxisLength", s.jointAxisLength).toDouble();
    s.jointWaistWidth = settings.value("jointWaistWidth", s.jointWaistWidth).toDouble();
    s.springsAsCoils = settings.value("springsAsCoils", s.springsAsCoils).toBool();
    s.springPitch = settings.value("springPitch", s.springPitch).toDouble();
    s.springWidth = settings.value("springWidth", s.springWidth).toDouble();
    s.jointOutlineWidth = settings.value("jointOutlineWidth", s.jointOutlineWidth).toDouble();

    s.undoDepth = settings.value("undoDepth", s.undoDepth).toInt();
    // One group per converter, keyed by its folder name. Read back whole
    // rather than by declared key: a converter that is not installed right now
    // still keeps whatever was set for it.
    settings.beginGroup("Export");
    for (const QString &converter : settings.childGroups()) {
        settings.beginGroup(converter);
        QVariantMap values;
        for (const QString &key : settings.childKeys())
            values.insert(key, settings.value(key));
        settings.endGroup();
        s.converterSettings.insert(converter, values);
    }
    settings.endGroup();
    s.jointSelectionColor = QColor(settings.value("jointSelectionColor",
        s.jointSelectionColor.name(QColor::HexArgb)).toString());
    s.jointSelectionLineWidth =
        settings.value("jointSelectionLineWidth", s.jointSelectionLineWidth).toDouble();
    s.jointSelectionLineStyle = static_cast<Qt::PenStyle>(
        settings.value("jointSelectionLineStyle",
                       static_cast<int>(s.jointSelectionLineStyle)).toInt());

    // One colour and one line style per kind of joint, under the kind's own
    // name. What an older settings file had per Box2D joint type is dropped:
    // the kinds it would map to are what the defaults already give.
    for (const physics::JointVisual kind : CanvasScene::jointKinds()) {
        const QString key = CanvasScene::jointKindKey(kind);
        const QColor color(settings.value(QStringLiteral("jointKindColors/") + key).toString());
        if (color.isValid())
            s.jointKindColors.insert(static_cast<int>(kind), color);
        const QVariant style = settings.value(QStringLiteral("jointKindStyles/") + key);
        if (style.isValid())
            s.jointKindStyles.insert(static_cast<int>(kind),
                                     static_cast<JointStyle>(style.toInt()));
    }
    s.simulationStepsPerSecond =
        settings.value("stepsPerSecond", s.simulationStepsPerSecond).toInt();
    s.simulationSpeed = settings.value("simulationSpeed", s.simulationSpeed).toDouble();
    s.simulationFullScreen =
        settings.value("fullScreen", s.simulationFullScreen).toBool();
    const QColor shotLight(settings.value("shotLightColor").toString());
    if (shotLight.isValid())
        s.shotLightColor = shotLight;
    const QColor shotFull(settings.value("shotFullColor").toString());
    if (shotFull.isValid())
        s.shotFullColor = shotFull;
    s.shotLineWidth = settings.value("shotLineWidth", s.shotLineWidth).toDouble();
    s.shotLineStyle = static_cast<Qt::PenStyle>(
        settings.value("shotLineStyle", static_cast<int>(s.shotLineStyle)).toInt());
    const QColor trail(settings.value("trailColor").toString());
    if (trail.isValid())
        s.trailColor = trail;
    s.trailLineWidth = settings.value("trailLineWidth", s.trailLineWidth).toDouble();
    s.trailLineStyle = static_cast<Qt::PenStyle>(
        settings.value("trailLineStyle", static_cast<int>(s.trailLineStyle)).toInt());
    settings.endGroup();

    settings.beginGroup("Shapes");
    // The single default these grew out of, used to seed every kind the first
    // time a scene is opened after the upgrade.
    const QColor oldBody(settings.value("bodyColor").toString());
    const QColor oldBorder(settings.value("borderColor").toString());
    const double oldWidth = settings.value("borderWidth", -1.0).toDouble();
    for (const QString &kind : ShapeStyle::kinds()) {
        ShapeStyle style = ShapeStyle::defaultFor(kind);
        if (oldBody.isValid())
            style.body = oldBody;
        if (oldBorder.isValid())
            style.border = oldBorder;
        if (oldWidth >= 0.0)
            style.borderWidth = oldWidth;
        const QString group = QStringLiteral("shapeStyles/") + kind + QLatin1Char('/');
        style.body = QColor(settings.value(group + QStringLiteral("body"),
                                           style.body.name(QColor::HexArgb)).toString());
        style.border = QColor(settings.value(group + QStringLiteral("border"),
                                             style.border.name(QColor::HexArgb)).toString());
        style.borderWidth = settings.value(group + QStringLiteral("width"),
                                           style.borderWidth).toDouble();
        style.borderStyle = ShapeStyle::penStyleFromName(
            settings.value(group + QStringLiteral("line"),
                           ShapeStyle::penStyleName(style.borderStyle)).toString());
        s.shapeStyles.insert(kind, style);
    }
    s.selectionLineStyle = static_cast<Qt::PenStyle>(settings.value("selectionLineStyle", static_cast<int>(s.selectionLineStyle)).toInt());
    s.selectionLineWidth = settings.value("selectionLineWidth", s.selectionLineWidth).toDouble();
    s.selectionColor = QColor(settings.value("selectionColor", s.selectionColor.name(QColor::HexArgb)).toString());
    s.handleShape = static_cast<HandleShape>(settings.value("handleShape", static_cast<int>(s.handleShape)).toInt());
    s.handleSize = settings.value("handleSize", s.handleSize).toDouble();
    s.handleColor = QColor(settings.value("handleColor", s.handleColor.name(QColor::HexArgb)).toString());
    s.handleBorderWidth = settings.value("handleBorderWidth", s.handleBorderWidth).toDouble();
    s.handleBorderColor = QColor(settings.value("handleBorderColor", s.handleBorderColor.name(QColor::HexArgb)).toString());
    settings.endGroup();

    return s;
}

void MainWindow::saveSettingsToFile(const OptionsDialog::Settings &s) const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue("Engine/default", s.defaultEngineName);
    settings.setValue("Appearance/theme", s.theme);
    settings.setValue("Appearance/controls", s.controls);
    settings.beginGroup("Field");
    settings.setValue("width", s.fieldWidth);
    settings.setValue("height", s.fieldHeight);
    settings.setValue("backgroundColor", s.backgroundColor.name(QColor::HexArgb));
    settings.setValue("showGrid", s.showGrid);
    settings.setValue("cellSize", s.gridCellSize);
    settings.setValue("gridColor", s.gridColor.name());
    settings.setValue("snapToGrid", s.snapToGrid);
    settings.setValue("snapPoint", static_cast<int>(s.snapPoint));
    settings.setValue("snapStep", s.snapStep);
    settings.setValue("snapSensitivity", s.snapSensitivity);
    settings.endGroup();

    settings.beginGroup("Scale");
    settings.remove("current"); // no longer restored; see loadSettingsFromFile()
    settings.setValue("min", s.scaleMin);
    settings.setValue("max", s.scaleMax);
    settings.setValue("step", s.scaleStep);
    settings.endGroup();

    settings.beginGroup("Physics");
    settings.setValue("pixelsPerMeter", s.pixelsPerMeter);
    settings.setValue("rulerUnit", Units::key(s.rulerUnit));
    settings.setValue("solidBounds", s.fieldBoundsSolid);
    settings.setValue("bodyDynamicColor", s.bodyDynamicColor.name(QColor::HexArgb));
    settings.setValue("bodyStaticColor", s.bodyStaticColor.name(QColor::HexArgb));
    settings.setValue("bodyKinematicColor", s.bodyKinematicColor.name(QColor::HexArgb));
    settings.setValue("unassignedShapeColor", s.unassignedShapeColor.name(QColor::HexArgb));
    settings.setValue("sensorColor", s.sensorColor.name(QColor::HexArgb));
    settings.setValue("sensorPattern", static_cast<int>(s.sensorPattern));
    settings.setValue("sensorFillsBody", s.sensorFillsBody);
    settings.setValue("borderWidth", s.physicsBorderWidth);
    settings.setValue("logFont", s.logFontFamily);
    settings.setValue("logFontSize", s.logFontSize);
    settings.setValue("logColor", s.logColor.name(QColor::HexArgb));
    settings.setValue("logCorner", int(s.logCorner));
    settings.setValue("fillAlpha", s.physicsFillAlpha);
    settings.setValue("jointFillAlpha", s.jointFillAlpha);
    settings.setValue("jointAnchorOpacity", s.jointAnchorOpacity);
    settings.setValue("selectionLineStyle", static_cast<int>(s.physicsSelectionLineStyle));
    settings.setValue("selectionLineWidth", s.physicsSelectionLineWidth);
    settings.setValue("selectionColor", s.physicsSelectionColor.name(QColor::HexArgb));
    settings.setValue("debugView", s.sleepShading);
    settings.setValue("runShowGrid", s.runShowGrid);
    settings.setValue("runShowJoints", s.runShowJoints);
    settings.setValue("runShowBodyAxes", s.runShowBodyAxes);
    settings.setValue("runShowRays", s.runShowRays);
    settings.setValue("runShowSensors", s.runShowSensors);
    settings.setValue("runShowTrails", s.runShowTrails);
    settings.setValue("runShowExplosions", s.runShowExplosions);
    settings.setValue("showBodyAxes", s.showBodyAxes);
    settings.setValue("bodyAxisLength", s.bodyAxisLength);
    settings.setValue("bodyAxisWidth", s.bodyAxisWidth);
    settings.setValue("bodyAxisXColor", s.bodyAxisXColor.name(QColor::HexArgb));
    settings.setValue("bodyAxisYColor", s.bodyAxisYColor.name(QColor::HexArgb));
    settings.setValue("sleepShiftPercent", s.sleepShiftPercent);
    settings.setValue("maxPolygonVertices", s.maxPolygonVertices);
    settings.setValue("jointColor", s.jointColor.name(QColor::HexArgb));
    settings.setValue("jointOutlineColor", s.jointOutlineColor.name(QColor::HexArgb));
    settings.setValue("jointAnchorRadius", s.jointAnchorRadius);
    settings.setValue("jointAxisLength", s.jointAxisLength);
    settings.setValue("jointWaistWidth", s.jointWaistWidth);
    settings.setValue("springsAsCoils", s.springsAsCoils);
    settings.setValue("springPitch", s.springPitch);
    settings.setValue("springWidth", s.springWidth);
    settings.setValue("jointOutlineWidth", s.jointOutlineWidth);

    settings.setValue("undoDepth", s.undoDepth);

    settings.beginGroup("Export");
    settings.remove(QString());   // converters that have gone leave nothing behind
    for (auto it = s.converterSettings.constBegin();
         it != s.converterSettings.constEnd(); ++it) {
        settings.beginGroup(it.key());
        for (auto value = it.value().constBegin(); value != it.value().constEnd(); ++value)
            settings.setValue(value.key(), value.value());
        settings.endGroup();
    }
    settings.endGroup();
    settings.setValue("jointSelectionColor", s.jointSelectionColor.name(QColor::HexArgb));
    settings.setValue("jointSelectionLineWidth", s.jointSelectionLineWidth);
    settings.setValue("jointSelectionLineStyle", static_cast<int>(s.jointSelectionLineStyle));

    settings.remove(QStringLiteral("jointTypeColors")); // colours are per kind now
    for (const physics::JointVisual kind : CanvasScene::jointKinds()) {
        const QString key = CanvasScene::jointKindKey(kind);
        const QColor color = s.jointKindColors.value(static_cast<int>(kind));
        if (color.isValid())
            settings.setValue(QStringLiteral("jointKindColors/") + key,
                              color.name(QColor::HexArgb));
        settings.setValue(QStringLiteral("jointKindStyles/") + key,
                          static_cast<int>(s.jointKindStyles.value(
                              static_cast<int>(kind), CanvasScene::defaultJointKindStyle(kind))));
    }
    settings.setValue("stepsPerSecond", s.simulationStepsPerSecond);
    settings.setValue("simulationSpeed", s.simulationSpeed);
    settings.setValue("fullScreen", s.simulationFullScreen);
    settings.setValue("shotLightColor", s.shotLightColor.name(QColor::HexArgb));
    settings.setValue("shotFullColor", s.shotFullColor.name(QColor::HexArgb));
    settings.setValue("shotLineWidth", s.shotLineWidth);
    settings.setValue("shotLineStyle", static_cast<int>(s.shotLineStyle));
    settings.setValue("trailColor", s.trailColor.name(QColor::HexArgb));
    settings.setValue("trailLineWidth", s.trailLineWidth);
    settings.setValue("trailLineStyle", static_cast<int>(s.trailLineStyle));
    settings.endGroup();

    settings.beginGroup("Shapes");
    for (auto it = s.shapeStyles.constBegin(); it != s.shapeStyles.constEnd(); ++it) {
        const QString group = QStringLiteral("shapeStyles/") + it.key() + QLatin1Char('/');
        settings.setValue(group + QStringLiteral("body"), it->body.name(QColor::HexArgb));
        settings.setValue(group + QStringLiteral("border"), it->border.name(QColor::HexArgb));
        settings.setValue(group + QStringLiteral("width"), it->borderWidth);
        settings.setValue(group + QStringLiteral("line"),
                          ShapeStyle::penStyleName(it->borderStyle));
    }
    settings.setValue("selectionLineStyle", static_cast<int>(s.selectionLineStyle));
    settings.setValue("selectionLineWidth", s.selectionLineWidth);
    settings.setValue("selectionColor", s.selectionColor.name(QColor::HexArgb));
    settings.setValue("handleShape", static_cast<int>(s.handleShape));
    settings.setValue("handleSize", s.handleSize);
    settings.setValue("handleColor", s.handleColor.name(QColor::HexArgb));
    settings.setValue("handleBorderWidth", s.handleBorderWidth);
    settings.setValue("handleBorderColor", s.handleBorderColor.name(QColor::HexArgb));
    settings.endGroup();
}
