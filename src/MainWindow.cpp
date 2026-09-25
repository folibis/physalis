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
#include "PropertyPanel.h"
#include "SceneTree.h"
#include "RulesPanel.h"
#include "ShapeStyle.h"
#include "VariablesPanel.h"
#include "UndoStack.h"
#include "RectangleItem.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "Joint.h"
#include "SimulationController.h"
#include "SceneExporter.h"
#include "SceneScreenshot.h"

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
#include <QFileInfo>
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

    m_ui->propertyPanel->setScene(m_scene);
    m_ui->sceneTree->setScene(m_scene);
    m_ui->rulesPanel->setScene(m_scene);
    m_ui->variablesPanel->setScene(m_scene);
    connect(m_ui->rulesPanel, &RulesPanel::incompleteCountChanged, this,
            &MainWindow::showIncompleteRules);
    showIncompleteRules(m_ui->rulesPanel->incompleteCount());

    // Kept, because the transport controls do not exist yet: they are built
    // with the toolbar further down, and what was saved for them is put on
    // them there.
    const OptionsDialog::Settings startup = loadSettingsFromFile();
    applySettings(startup);

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &MainWindow::updatePasteAction);

    m_undo->reset();

    const QRect availableGeometry = QGuiApplication::primaryScreen()->availableGeometry();
    resize(availableGeometry.width() * 0.75, availableGeometry.height() * 0.8);
    m_ui->propertyDock->setMinimumWidth(static_cast<int>(width() * 0.17));
    resizeDocks({m_ui->propertyDock}, {static_cast<int>(width() * 0.25)}, Qt::Horizontal);

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
    for (QAction *action : { static_cast<QAction *>(m_ui->actionAddShape), m_ui->actionAddJoint }) {
        if (auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action)))
            button->setPopupMode(QToolButton::InstantPopup);
    }

    // The dividers travel with their group, so a hidden group leaves no line.
    m_editModeActions << m_ui->actionAddShape << m_ui->actionDelete
                      << m_ui->actionMoveScale << m_ui->actionEditNodes << m_ui->actionRotate
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
                         << m_ui->actionCopy << m_ui->actionPaste << m_ui->actionDelete
                         << m_ui->actionMoveScale << m_ui->actionEditNodes << m_ui->actionRotate
                         << m_ui->actionCreateBody << m_ui->actionDissolveBody
                         << m_ui->actionAddJoint << m_ui->actionDeleteJoint
                         << m_ui->menuAddShape->menuAction();
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
        case SimulationController::State::Stopped:  m_simulation->start();  break;
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
                                     " field as large as it will go. Space holds it and lets"
                                     " it go again, the right arrow advances one frame, and"
                                     " Esc ends the run and comes back here."));
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
    m_scaleCombo->setToolTip(tr("Canvas zoom level -- pick a preset, type a value, or Shift+wheel over the canvas"));
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
    m_resetScaleAction->setToolTip(tr("Reset zoom to 100%"));
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
    modeLabel->setStyleSheet(QStringLiteral("color: #777; padding-right: 10px;"));
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
        "#ModeSwitch { background: #e4e4e4; border: 1px solid #d2d2d2; border-radius: 15px; }"));

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
    // A body flung with the mouse. The canvas knows the gesture; only the run
    // can push anything.
    connect(m_scene, &CanvasScene::shotReleased, m_simulation, &SimulationController::shoot);

    connect(m_scene, &CanvasScene::editorModeChanged, this, &MainWindow::onEditorModeChanged);
    connect(m_scene, &CanvasScene::physicsSelectionChanged, this, &MainWindow::onPhysicsSelectionChanged);
    // Selecting an explosion is a different signal, and Remove depends on it.
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
    connect(m_scene, &CanvasScene::activeItemChanged, this,
            [showProperties](ShapeItem *item) { if (item) showProperties(); });

    connect(m_scene, &CanvasScene::scaleChanged, this, [this](qreal scale) {
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
    m_ui->canvasView->installEventFilter(this);
    placeLogOverlay();
    m_logOverlay->hide();
    connect(m_scene, &CanvasScene::watchesChanged, this, &MainWindow::updateLogOverlay);
    connect(m_simulation, &SimulationController::stateChanged, this, &MainWindow::updateLogOverlay);
    connect(m_simulation, &SimulationController::stepped, this, &MainWindow::updateLogOverlay);
    // The measured rows in the property table read the same values the log
    // does, so they are refreshed on the same beat.
    connect(m_simulation, &SimulationController::stepped,
            m_ui->propertyPanel, &PropertyPanel::refreshValues);
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

    PhysicsBody *body = m_scene->createBodyFromSelection();
    if (!body)
        return;

    // Only area gives mass, so a body made of outlines cannot be dynamic
    // however plainly it was asked for. It is made static instead, and says so
    // -- quietly handing back something that will not move is worse.
    const bool refused = type == physics::BodyType::Dynamic && !problems.isEmpty();
    body->props().type = refused ? physics::BodyType::Static : type;
    body->notifyPropertyChanged();

    if (refused) {
        QMessageBox::warning(
            this, tr("Create Body"),
            tr("%1 was made Static because it can't be a solid, movable body:\n\n%2\n\n"
               "Shapes like these still collide as outlines, which works for walls and ramps,"
               " but an outline has no area and so no mass. Split them into convex pieces to"
               " make the body Dynamic.")
                .arg(body->name(), problems.join(QStringLiteral("\n"))));
    }

    m_scene->notifyEdit(tr("Create %1").arg(body->name()));
    statusBar()->showMessage(tr("Created %1 from %2 shape(s)")
                                 .arg(body->name()).arg(body->shapes().size()), 4000);

    m_ui->propertyPanel->showSection(tr("Body"));
}

void MainWindow::on_actionDissolveBody_triggered()
{
    // One button for whatever is selected: an explosion is deleted outright,
    // a body (sensor or not) is dissolved back into its shapes.
    if (RayItem *ray = m_scene->selectedRay()) {
        const QString name = ray->name();
        m_scene->removeRay(ray);
        m_scene->notifyEdit(tr("Remove %1").arg(name));
        statusBar()->showMessage(tr("Removed %1").arg(name), 4000);
        return;
    }

    if (ExplosionItem *explosion = m_scene->selectedExplosion()) {
        const QString name = explosion->name();
        m_scene->selectExplosion(nullptr);
        m_scene->removeExplosion(explosion);
        m_scene->notifyEdit(tr("Remove %1").arg(name));
        statusBar()->showMessage(tr("Removed %1").arg(name), 4000);
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

bool MainWindow::clipboardHasShape() const
{
    // Asked of the same code paste itself uses, rather than sniffing the JSON.
    QJsonParseError parseError {};
    const QJsonDocument document =
        QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    std::unique_ptr<ShapeItem> shape(SceneSerializer::shapeFromJson(document.object()));
    return shape != nullptr;
}

void MainWindow::updatePasteAction()
{
    if (!m_ui->actionPaste)
        return;
    m_ui->actionPaste->setEnabled(m_scene->editorMode() == EditorMode::Edit
                              && !(m_simulation && m_simulation->isActive())
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
    ShapeItem *item = m_scene->activeItem();
    if (!item)
        return;

    const QJsonDocument document(SceneSerializer::shapeToJson(item));
    QGuiApplication::clipboard()->setText(QString::fromUtf8(document.toJson(QJsonDocument::Compact)));

    m_pasteCount = 0;
    updatePasteAction();
    statusBar()->showMessage(tr("Copied %1").arg(item->name()), 4000);
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

    ShapeItem *shape = SceneSerializer::shapeFromJson(document.object());
    if (!shape)
        return;

    constexpr qreal kPasteOffset = 20.0;
    const qreal offset = kPasteOffset * ++m_pasteCount;
    shape->setPos(shape->pos() + QPointF(offset, offset));

    // Named after it joins the scene, and against what the scene already
    // holds: a name chosen beforehand cannot be checked for collisions, which
    // is how two shapes end up sharing one.
    m_scene->addItem(shape);
    shape->setName(Naming::nextName(shape->typeName(), m_scene->takenNames(shape)));
    m_scene->notifyShapesChanged();
    m_scene->selectShape(shape);
    m_scene->notifyEdit(tr("Paste %1").arg(shape->name()));
    statusBar()->showMessage(tr("Pasted %1").arg(shape->name()), 4000);
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

    QString error;
    if (!SceneSerializer::loadFromFile(m_scene, path, &error)) {
        QMessageBox::warning(this, tr("Load Scene"),
                             tr("Couldn't open %1:\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }

    m_scenePath = path;
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

void MainWindow::duplicateShape(ShapeItem *item)
{
    if (!item)
        return;

    // Through the same JSON the clipboard uses, so a duplicate and a paste
    // cannot drift apart in what they carry.
    ShapeItem *copy = SceneSerializer::shapeFromJson(SceneSerializer::shapeToJson(item));
    if (!copy)
        return;

    constexpr qreal kOffset = 20.0;
    copy->setPos(item->pos() + QPointF(kOffset, kOffset));
    m_scene->addItem(copy);
    // A copy is a new object: its own name, and in no body until grouped.
    copy->setName(Naming::nextName(item->typeName(), m_scene->takenNames(copy)));
    m_scene->notifyShapesChanged();
    m_scene->selectShape(copy);
    m_scene->notifyEdit(tr("Duplicate %1").arg(item->name()));
    statusBar()->showMessage(tr("Duplicated %1 as %2").arg(item->name(), copy->name()), 4000);
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
    if (watched == m_ui->canvasView && event->type() == QEvent::Resize)
        placeLogOverlay();
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::placeLogOverlay()
{
    if (!m_logOverlay)
        return;

    constexpr int kMargin = 8;
    const QSize view = m_ui->canvasView->viewport()->size();
    const QSize own = m_logOverlay->size();
    const bool right = m_logCorner == Qt::TopRightCorner
                       || m_logCorner == Qt::BottomRightCorner;
    const bool bottom = m_logCorner == Qt::BottomLeftCorner
                        || m_logCorner == Qt::BottomRightCorner;
    m_logOverlay->move(right ? qMax(kMargin, view.width() - own.width() - kMargin) : kMargin,
                       bottom ? qMax(kMargin, view.height() - own.height() - kMargin) : kMargin);
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
    placeLogOverlay();
    m_logOverlay->show();
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
    if (key == kLayerGrid)
        m_scene->setRunLayer(RunLayer::Grid, on);
    else if (key == kLayerJoints)
        m_scene->setRunLayer(RunLayer::Joints, on);
    else if (key == kLayerAxes)
        m_scene->setRunLayer(RunLayer::BodyAxes, on);
    else if (key == kLayerRays)
        m_scene->setRunLayer(RunLayer::Rays, on);
    else if (key == kLayerExplosions)
        m_scene->setRunLayer(RunLayer::Explosions, on);
    else if (key == kLayerSleep)
        m_scene->setRunLayer(RunLayer::SleepShading, on);
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
            // The editor was behind it the whole time; give it the keyboard.
            activateWindow();
        }
        return;
    }
    if (m_fullScreen)
        return;

    m_fullScreen = new FullScreenView(m_scene, this);
    // The keys stand in for the transport buttons, which are not on this
    // screen: the same three things they do there.
    connect(m_fullScreen, &FullScreenView::holdRequested, this, [this] {
        if (m_simulation->isRunning())
            m_simulation->pause();
        else
            m_simulation->resume();
    });
    connect(m_fullScreen, &FullScreenView::stepRequested,
            m_simulation, &SimulationController::stepFrame);
    connect(m_fullScreen, &FullScreenView::closeRequested,
            m_simulation, &SimulationController::stop);
    // Whichever screen the editor is on, rather than always the first.
    if (QScreen *screen = windowHandle() ? windowHandle()->screen() : nullptr)
        m_fullScreen->setGeometry(screen->geometry());
    m_fullScreen->showFullScreen();
    m_fullScreen->raise();
    m_fullScreen->activateWindow();
}

void MainWindow::onSimulationStateChanged()
{
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
        onPhysicsSelectionChanged();
        if (m_scene->editorMode() == EditorMode::Edit)
            onActiveItemChanged(m_scene->activeItem());
    }
}

void MainWindow::showIncompleteRules(int count)
{
    const int tab = m_ui->sidePanel->indexOf(m_ui->rulesPanel);
    if (tab < 0)
        return;

    m_ui->sidePanel->setTabText(tab, count > 0 ? tr("Rules (%1)").arg(count) : tr("Rules"));
    m_ui->sidePanel->setTabIcon(tab, count > 0 ? Icons::warning() : QIcon());
    m_ui->sidePanel->setTabToolTip(
        tab,
        count > 0 ? tr("%n rule(s) are unfinished and will not run.", nullptr, count)
                  : QString());
}

void MainWindow::onEditorModeChanged(EditorMode mode)
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
            "QToolButton:hover:!checked { background: #d5d5d5; }"
            "QToolButton:disabled { color: #aaaaaa; background: transparent; }")
            .arg(active ? QStringLiteral("bold") : QStringLiteral("normal"),
                 active ? QStringLiteral("white") : QStringLiteral("#4f4f4f"),
                 active ? accent.name() : QStringLiteral("transparent")));
    }

    for (QAction *action : std::as_const(m_editModeActions))
        action->setVisible(mode == EditorMode::Edit);
    for (QAction *action : std::as_const(m_physicsModeActions))
        action->setVisible(mode == EditorMode::Physics);
    for (QAction *action : std::as_const(m_transportActions))
        action->setVisible(mode != EditorMode::Edit);

    const bool editing = mode == EditorMode::Edit;

    m_ui->menuAddShape->setEnabled(editing);
    m_ui->actionCopy->setEnabled(editing && m_scene->activeItem() != nullptr);
    updatePasteAction();

    if (m_ui->propertyPanel)
        m_ui->propertyPanel->setEditorMode(mode);

    onPhysicsSelectionChanged();
    onJointSelectionChanged();
    if (mode == EditorMode::Edit)
        onActiveItemChanged(m_scene->activeItem());
}

void MainWindow::onActiveItemChanged(ShapeItem *item)
{
    if (!item) {
        m_ui->actionMoveScale->setEnabled(false);
        m_ui->actionMoveScale->setChecked(false);
        m_ui->actionEditNodes->setEnabled(false);
        m_ui->actionEditNodes->setChecked(false);
        m_ui->actionRotate->setEnabled(false);
        m_ui->actionRotate->setChecked(false);
        m_ui->actionDelete->setEnabled(false);
        if (m_ui->actionCopy)
            m_ui->actionCopy->setEnabled(false);
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

    if (m_ui->actionCopy)
        m_ui->actionCopy->setEnabled(geometry);

    m_ui->actionMoveScale->setChecked(mode == ShapeMode::Selected);
    m_ui->actionMoveScale->setEnabled(geometry && mode != ShapeMode::Selected);
    m_ui->actionEditNodes->setChecked(mode == ShapeMode::Editing);
    m_ui->actionEditNodes->setEnabled(geometry && editable && mode != ShapeMode::Editing);
    m_ui->actionRotate->setChecked(mode == ShapeMode::Rotating);
    m_ui->actionRotate->setEnabled(geometry && mode != ShapeMode::Rotating);
    m_ui->actionDelete->setEnabled(true);

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
    // Every entry changes the scene, and nothing does that during a run.
    if (m_simulation && m_simulation->isActive())
        return;

    QMenu menu(this);
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
        menu.exec(m_ui->canvasView->mapToGlobal(pos));
        return;
    }

    if (!item) {
        menu.addMenu(m_ui->menuAddShape);
    } else {
        menu.addAction(tr("Duplicate"), this, [this, item] { duplicateShape(item); });

        menu.addAction(tr("Flip Horizontally"), this, [this, item] { flipShape(item, true); });
        menu.addAction(tr("Flip Vertically"), this, [this, item] { flipShape(item, false); });

        if (item->typeName() == QLatin1String("rectangle")) {
            QAction *toPolygon = menu.addAction(tr("Convert to Polygon"), this,
                                                [this, item] { convertToPolygon(item); });
            toPolygon->setToolTip(tr("Replaces it with the polygon of its four corners, keeping"
                                     " its name, its place in its body and its physics. The"
                                     " corner radius goes: only a rectangle has one."));
            toPolygon->setEnabled(!(m_simulation && m_simulation->isActive()));
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
    }

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

    const QVector<SceneExporter::Converter> converters =
        SceneExporter::discover(m_converterPath);
    if (converters.isEmpty()) {
        QAction *none = menu->addAction(m_converterPath.isEmpty()
                                            ? tr("No converters folder set...")
                                            : tr("No converters in %1").arg(m_converterPath));
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
            // Exported mid-run, it is still the scene that is exported, as it
            // stood when the run started -- not wherever the run has got to.
            m_simulation->withSceneAsStarted([&] {
                converted = SceneExporter::run(converter, m_scene, folder,
                                               settingsAsJson(settingsFilePath()), &error, &written, &log);
            });

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
        OptionsDialog::Settings s = dialog.settings();
        // The engine is the one setting that reaches the open scene, because a
        // scene is built for one. Keeping the scene keeps its engine too.
        if (!adoptEngine(s.defaultEngineName))
            s.defaultEngineName = m_scene->simulationEngineName();
        applySettings(s);
        saveSettingsToFile(s);
    }
}

void MainWindow::onFieldSettingsChanged()
{
    // The edit already applied itself to m_scene; just persist the result.
    saveSettingsToFile(currentSettingsSnapshot());
}

OptionsDialog::Settings MainWindow::currentSettingsSnapshot() const
{
    OptionsDialog::Settings current;
    current.converterPath = m_converterPath;
    current.defaultEngineName = engineForNewScenes();
    current.converterSettings = m_converterSettings;
    current.fieldWidth = m_scene->fieldWidth();
    current.fieldHeight = m_scene->fieldHeight();
    current.backgroundColor = m_scene->backgroundColor();
    current.showGrid = m_scene->showGrid();
    current.gridCellSize = m_scene->gridCellSize();
    current.gridColor = m_scene->gridColor();
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
    current.jointAnchorRadius = m_scene->jointAnchorRadius();
    current.jointAxisLength = m_scene->jointAxisLength();
    current.jointWaistWidth = m_scene->jointWaistWidth();
    current.jointOutlineWidth = m_scene->jointOutlineWidth();
    if (m_simulation) {
        current.simulationStepsPerSecond = m_simulation->stepsPerSecond();
        current.simulationSpeed = m_simulation->speed();
    }
    current.pixelsPerMeter = m_scene->pixelsPerMeter();
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

void MainWindow::applySettings(const OptionsDialog::Settings &s)
{
    m_converterPath = s.converterPath;
    // Only new scenes: what is open was built for the engine it names.
    m_defaultEngineName = s.defaultEngineName;
    m_converterSettings = s.converterSettings;
    m_scene->setFieldSize(s.fieldWidth, s.fieldHeight);
    m_scene->setBackgroundColor(s.backgroundColor);
    m_scene->setShowGrid(s.showGrid);
    m_scene->setGridCellSize(s.gridCellSize);
    m_scene->setGridColor(s.gridColor);
    m_scene->setSnapToGrid(s.snapToGrid);
    m_scene->setSnapPoint(s.snapPoint);
    m_scene->setSnapStep(s.snapStep);
    m_scene->setSnapSensitivity(s.snapSensitivity);
    // Bounds/step before the current value, so it clamps against the
    // settings actually being applied rather than whatever was set before.
    m_scene->setScaleMin(s.scaleMin);
    m_scene->setScaleMax(s.scaleMax);
    m_scene->setScaleStep(s.scaleStep);
    m_scene->setCurrentScale(s.currentScale);
    m_scene->setDefaultShapeStyles(s.shapeStyles);
    m_logFontFamily = s.logFontFamily;
    m_logFontSize = s.logFontSize;
    m_logColor = s.logColor;
    m_logCorner = s.logCorner;
    applyLogStyle();
    m_scene->setBodyColor(physics::BodyType::Dynamic, s.bodyDynamicColor);
    m_scene->setBodyColor(physics::BodyType::Static, s.bodyStaticColor);
    m_scene->setBodyColor(physics::BodyType::Kinematic, s.bodyKinematicColor);
    m_scene->setUnassignedShapeColor(s.unassignedShapeColor);
    m_scene->setSensorColor(s.sensorColor);
    m_scene->setSensorPattern(s.sensorPattern);
    m_scene->setSensorFillsBody(s.sensorFillsBody);
    m_scene->setPhysicsBorderWidth(s.physicsBorderWidth);
    m_scene->setPhysicsFillAlpha(s.physicsFillAlpha);
    m_scene->setJointFillAlpha(s.jointFillAlpha);
    m_scene->setJointAnchorOpacity(s.jointAnchorOpacity);
    m_scene->setPhysicsSelectionLineStyle(s.physicsSelectionLineStyle);
    m_scene->setPhysicsSelectionLineWidth(s.physicsSelectionLineWidth);
    m_scene->setPhysicsSelectionColor(s.physicsSelectionColor);
    m_scene->setRunLayer(CanvasScene::RunLayer::SleepShading, s.sleepShading);
    m_scene->setRunLayer(CanvasScene::RunLayer::Grid, s.runShowGrid);
    m_scene->setRunLayer(CanvasScene::RunLayer::Joints, s.runShowJoints);
    m_scene->setRunLayer(CanvasScene::RunLayer::BodyAxes, s.runShowBodyAxes);
    m_scene->setRunLayer(CanvasScene::RunLayer::Rays, s.runShowRays);
    m_scene->setRunLayer(CanvasScene::RunLayer::Explosions, s.runShowExplosions);
    syncTransportWidgets(s);
    m_scene->setShowBodyAxes(s.showBodyAxes);
    m_scene->setBodyAxisLength(s.bodyAxisLength);
    m_scene->setBodyAxisWidth(s.bodyAxisWidth);
    m_scene->setBodyAxisXColor(s.bodyAxisXColor);
    m_scene->setBodyAxisYColor(s.bodyAxisYColor);
    m_scene->setSleepShiftPercent(s.sleepShiftPercent);
    m_scene->setMaxPolygonVertices(s.maxPolygonVertices);
    m_scene->setJointColor(s.jointColor);
    m_undo->setCapacity(s.undoDepth);
    m_scene->setJointKindColors(s.jointKindColors);
    m_scene->setJointKindStyles(s.jointKindStyles);
    m_scene->setJointSelectionColor(s.jointSelectionColor);
    m_scene->setJointSelectionLineWidth(s.jointSelectionLineWidth);
    m_scene->setJointSelectionLineStyle(s.jointSelectionLineStyle);
    m_scene->setJointOutlineColor(s.jointOutlineColor);
    m_scene->setShotLineColors(s.shotLightColor, s.shotFullColor);
    m_scene->setShotLineWidth(s.shotLineWidth);
    m_scene->setShotLineStyle(s.shotLineStyle);
    m_scene->setJointAnchorRadius(s.jointAnchorRadius);
    m_scene->setJointAxisLength(s.jointAxisLength);
    m_scene->setJointWaistWidth(s.jointWaistWidth);
    m_scene->setJointOutlineWidth(s.jointOutlineWidth);
    if (m_simulation) {
        m_simulation->setStepsPerSecond(s.simulationStepsPerSecond);
        m_simulation->setSpeed(s.simulationSpeed);
    }
    m_scene->setPixelsPerMeter(s.pixelsPerMeter);
    m_scene->setFieldBoundsSolid(s.fieldBoundsSolid);
    m_scene->setSelectionLineStyle(s.selectionLineStyle);
    m_scene->setSelectionLineWidth(s.selectionLineWidth);
    m_scene->setSelectionColor(s.selectionColor);
    m_scene->setHandleShape(s.handleShape);
    m_scene->setHandleSize(s.handleSize);
    m_scene->setHandleColor(s.handleColor);
    m_scene->setHandleBorderWidth(s.handleBorderWidth);
    m_scene->setHandleBorderColor(s.handleBorderColor);

    // Rebuilt so a changed Scale min/max/step shows immediately.
    if (m_ui->propertyPanel)
        m_ui->propertyPanel->setActiveItem(m_scene->activeItem());
}

QString MainWindow::settingsFilePath()
{
    const QString redirect = qEnvironmentVariable("PHYSALIS_SETTINGS");
    return redirect.isEmpty()
               ? QCoreApplication::applicationDirPath() + QStringLiteral("/settings.ini")
               : redirect;
}

OptionsDialog::Settings MainWindow::loadSettingsFromFile() const
{
    OptionsDialog::Settings s; // defaults

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    s.defaultEngineName = settings.value("Engine/default", s.defaultEngineName).toString();
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
    s.jointOutlineWidth = settings.value("jointOutlineWidth", s.jointOutlineWidth).toDouble();

    s.undoDepth = settings.value("undoDepth", s.undoDepth).toInt();
    // An installed copy ships the converters beside the program, so that is
    // where a settings file with nothing to say about them points.
    const QString besideTheProgram =
        QCoreApplication::applicationDirPath() + QStringLiteral("/exporters");
    if (QFileInfo::exists(besideTheProgram))
        s.converterPath = besideTheProgram;
    s.converterPath = settings.value("converterPath", s.converterPath).toString();

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
    settings.setValue("jointOutlineWidth", s.jointOutlineWidth);

    settings.setValue("undoDepth", s.undoDepth);
    settings.setValue("converterPath", s.converterPath);

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
