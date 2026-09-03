#include "MainWindow.h"

#include "AboutDialog.h"


#include "ui_MainWindow.h"
#include "CanvasScene.h"
#include "ExplosionItem.h"
#include "RayItem.h"
#include "Rule.h"
#include "ShapeItem.h"
#include "RulerWidget.h"
#include "PropertyPanel.h"
#include "SceneTree.h"
#include "RulesPanel.h"
#include "UndoStack.h"
#include "RectangleItem.h"
#include "CircleItem.h"
#include "PhysicsBody.h"
#include "Joint.h"
#include "SimulationController.h"
#include "SceneExporter.h"

#include <QJsonObject>
#include <QJsonValue>
#include "SceneSerializer.h"
#include "Naming.h"
#include "EngineRegistry.h"
#include "Icons.h"

#include <QGraphicsView>
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

    applySettings(loadSettingsFromFile());

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

    m_simulation = new SimulationController(m_scene, this);

    if (auto engine = physics::EngineRegistry::create(m_simulation->engineName())) {
        for (const physics::JointType &type : engine->jointTypes()) {
            QAction *action = m_jointTypeMenu->addAction(type.label);
            action->setToolTip(type.description);
            const QString typeId = type.id;
            connect(action, &QAction::triggered, this, [this, typeId] { onAddJoint(typeId); });
        }
    }

    m_transportActions << toolBar->actions().constLast();

    m_engineCombo = new QComboBox(this);
    m_engineCombo->addItems(physics::EngineRegistry::availableEngines());
    m_engineCombo->setCurrentText(m_simulation->engineName());
    m_engineCombo->setToolTip(tr("Which physics engine to simulate with"));
    connect(m_engineCombo, &QComboBox::currentTextChanged, this,
            [this](const QString &name) {
                m_simulation->setEngineName(name);
                m_scene->setSimulationEngineName(name);
            });
    m_scene->setSimulationEngineName(m_simulation->engineName());
    // addWidget() returns the QWidgetAction wrapping the combo -- hiding that
    // is what removes it; hiding the combo would leave a hole.
    m_transportActions << toolBar->addWidget(m_engineCombo);

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

    m_debugViewCheck = new QCheckBox(tr("Debug View"), toolBar);
    m_debugViewCheck->setChecked(m_scene->debugView());
    m_debugViewCheck->setToolTip(tr("While a simulation runs, draw the axis cross at each body's"
                                      " centre of mass, shift its color to show whether the"
                                      " solver still has it awake, and draw the joints. Off, a run"
                                      " shows just the shapes in their plain body colors. Outside"
                                      " a run nothing changes."));
    m_transportActions << toolBar->addWidget(m_debugViewCheck);
    connect(m_debugViewCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_scene->setDebugView(on);
        saveSettingsToFile(currentSettingsSnapshot());
    });

    m_transportActions << m_ui->actionSimulate << m_ui->actionStep << m_ui->actionStop;

    connect(m_scene, &CanvasScene::selectedJointChanged, this, &MainWindow::onJointSelectionChanged);
    connect(m_scene, &CanvasScene::jointsChanged, this, &MainWindow::onJointSelectionChanged);
    connect(m_simulation, &SimulationController::stateChanged,
            this, &MainWindow::onSimulationStateChanged);

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
    connect(m_scaleCombo, &QComboBox::activated, this, [this](int index) {
        QString text = m_scaleCombo->itemText(index);
        text.remove(QLatin1Char('%'));
        m_scene->setCurrentScale(text.toDouble());
    });
    toolBar->addWidget(m_scaleCombo);

    m_resetScaleAction = toolBar->addAction(Icons::resetScale(), tr("Reset Zoom"));
    m_resetScaleAction->setToolTip(tr("Reset zoom to 100%"));
    connect(m_resetScaleAction, &QAction::triggered, this, [this] { m_scene->setCurrentScale(100.0); });
    squareButton(toolBar, m_resetScaleAction);

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
    m_logOverlay->setStyleSheet(QStringLiteral(
        "#LogOverlay { background: rgba(255,255,255,190); border: 1px solid #c8c8c8;"
        " border-radius: 4px; padding: 4px 8px; color: #1E6FD9; }"));
    m_logOverlay->move(8, 8);
    m_logOverlay->hide();
    connect(m_scene, &CanvasScene::watchesChanged, this, &MainWindow::updateLogOverlay);
    connect(m_simulation, &SimulationController::stateChanged, this, &MainWindow::updateLogOverlay);
    connect(m_simulation, &SimulationController::stepped, this, &MainWindow::updateLogOverlay);
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
               " • Ctrl+Enter to close it • Escape to cancel"));
    } else {
        onActiveItemChanged(m_scene->activeItem());
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

    if (bodies.size() != 2) {
        QMessageBox::information(
            this, tr("Add Joint"),
            tr("A joint connects two bodies, and %1 %2 selected.\n\n"
               "Click a shape to select its body, then Ctrl+Click a shape of the other one.")
                .arg(bodies.size()).arg(bodies.size() == 1 ? tr("is") : tr("are")));
        return;
    }

    Joint *joint = m_scene->createJoint(type.id, bodies.at(0), bodies.at(1),
                                        type.anchorCount, type.defaultValues());
    if (!joint)
        return;

    m_scene->selectJoint(joint);
    m_scene->notifyEdit(tr("Add %1").arg(joint->name()));
    statusBar()->showMessage(tr("Added %1 between %2 and %3")
                                 .arg(joint->name(), bodies.at(0)->name(), bodies.at(1)->name()), 4000);
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

void MainWindow::onCreateBody()
{
    const QStringList problems = m_scene->solidBodyProblems(m_scene->physicsSelection());

    PhysicsBody *body = m_scene->createBodyFromSelection();
    if (!body)
        return;

    if (!problems.isEmpty()) {
        body->props().type = physics::BodyType::Static;
        body->notifyPropertyChanged();

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
            tr("Click a shape to select it • Ctrl+Click to add more • Double-click or Create Body"
               " groups them into a body, which is what Simulate runs"));
    } else if (picked == 0) {
        m_statusHelpLabel->setText(
            tr("Click a shape to select it • Ctrl+Click to add more"
               " • Create Body groups them into one rigid body"));
    } else if (PhysicsBody *body = m_scene->commonSelectedBody()) {
        m_statusHelpLabel->setText(
            m_scene->selectionIsWholeBody()
                ? tr("%1 shape(s) selected — all of %2 • Add Joint connects it to another body"
                     " • Dissolve Body breaks it up").arg(picked).arg(body->name())
                : tr("%1 of %2's shapes selected • Create Body splits them into a body of their"
                     " own • Dissolve Body breaks up %2").arg(picked).arg(body->name()));
    } else {
        m_statusHelpLabel->setText(
            tr("%1 shape(s) selected, not yet in a body"
               " • Create Body groups them into one").arg(picked));
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
    // A test drives the window itself and has nobody to answer a dialog, so it
    // is told apart by the settings override it always runs with.
    if (!qEnvironmentVariableIsSet("PHYSALIS_SETTINGS")
        && !confirmDiscardChanges(tr("Quit"))) {
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
    m_undo->reset(); // the opened file is the new starting point
    updateWindowTitle();
    m_ui->propertyPanel->setActiveItem(nullptr);
    statusBar()->showMessage(tr("Opened %1").arg(QDir::toNativeSeparators(path)), 4000);
    return true;
}

bool MainWindow::onSaveScene()
{
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
            shown = value.typeId() == QMetaType::Bool
                        ? (value.toBool() ? tr("true") : tr("false"))
                        : QString::number(value.toDouble(), 'f', 2);
        }
        // "@world" is an internal handle, not something to show a reader.
        const QString who = watch.objectName == Rule::world() ? tr("World")
                                                              : watch.objectName;
        lines << tr("%1 · %2   %3").arg(who, watch.label, shown);
    }
    m_logOverlay->setText(lines.join(QChar::LineFeed));
    m_logOverlay->adjustSize();
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

void MainWindow::onSimulationStateChanged()
{
    const bool active = m_simulation->isActive();

    updateTransportActions();
    updateUndoActions();
    m_engineCombo->setEnabled(!active);

    m_ui->menuAddShape->setEnabled(!active && m_scene->editorMode() == EditorMode::Edit);
    for (EditorMode mode : EditorModes::kAll) {
        if (QToolButton *button = m_modeButtons.value(static_cast<int>(mode)))
            button->setEnabled(!active);
    }

    if (active) {
        const QStringList skipped = m_simulation->skippedBodies();
        if (skipped.isEmpty()) {
            onPhysicsSelectionChanged();
        } else {
            m_statusHelpLabel->setText(
                tr("%1 • Skipped (no mass): %2 • Stop restores the starting positions")
                    .arg(m_simulation->isRunning() ? tr("Running") : tr("Paused"),
                         skipped.join(QStringLiteral(", "))));
        }
        for (QAction *action : std::as_const(m_editModeActions))
            action->setEnabled(false);
        m_ui->actionCreateBody->setEnabled(false);
        m_ui->actionDissolveBody->setEnabled(false);
        m_ui->actionCopy->setEnabled(false);
        m_ui->actionPaste->setEnabled(false);
    } else {
        onPhysicsSelectionChanged();
        if (m_scene->editorMode() == EditorMode::Edit)
            onActiveItemChanged(m_scene->activeItem());
    }
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
            tr("Drag a node to move it (Shift ignores the grid) • Ctrl+Click to select multiple"
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

void MainWindow::on_canvasView_customContextMenuRequested(const QPoint &pos)
{
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
            const bool converted =
                SceneExporter::run(converter, m_scene, folder,
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
        const OptionsDialog::Settings s = dialog.settings();
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
    current.defaultBorderColor = m_scene->defaultBorderColor();
    current.defaultBorderWidth = m_scene->defaultBorderWidth();
    current.defaultBodyColor = m_scene->defaultBodyColor();
    current.bodyDynamicColor = m_scene->bodyColor(physics::BodyType::Dynamic);
    current.bodyStaticColor = m_scene->bodyColor(physics::BodyType::Static);
    current.bodyKinematicColor = m_scene->bodyColor(physics::BodyType::Kinematic);
    current.unassignedShapeColor = m_scene->unassignedShapeColor();
    current.physicsBorderWidth = m_scene->physicsBorderWidth();
    current.physicsFillAlpha = m_scene->physicsFillAlpha();
    current.physicsSelectionLineStyle = m_scene->physicsSelectionLineStyle();
    current.physicsSelectionLineWidth = m_scene->physicsSelectionLineWidth();
    current.physicsSelectionColor = m_scene->physicsSelectionColor();
    current.debugView = m_scene->debugView();
    current.showBodyAxes = m_scene->showBodyAxes();
    current.bodyAxisLength = m_scene->bodyAxisLength();
    current.bodyAxisWidth = m_scene->bodyAxisWidth();
    current.bodyAxisXColor = m_scene->bodyAxisXColor();
    current.bodyAxisYColor = m_scene->bodyAxisYColor();
    current.sleepShiftPercent = m_scene->sleepShiftPercent();
    current.maxPolygonVertices = m_scene->maxPolygonVertices();
    current.jointColor = m_scene->jointColor();
    current.undoDepth = m_undo->capacity();
    current.jointTypeColors = m_scene->jointTypeColors();
    current.jointSelectionColor = m_scene->jointSelectionColor();
    current.jointSelectionLineWidth = m_scene->jointSelectionLineWidth();
    current.jointSelectionLineStyle = m_scene->jointSelectionLineStyle();
    current.simulationEngineName = m_scene->simulationEngineName();
    current.jointOutlineColor = m_scene->jointOutlineColor();
    current.jointAnchorRadius = m_scene->jointAnchorRadius();
    current.jointAxisLength = m_scene->jointAxisLength();
    current.jointWaistWidth = m_scene->jointWaistWidth();
    current.jointOutlineWidth = m_scene->jointOutlineWidth();
    if (m_simulation)
        current.simulationStepsPerSecond = m_simulation->stepsPerSecond();
    current.gravity = m_scene->gravity();
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
    m_scene->setDefaultBorderColor(s.defaultBorderColor);
    m_scene->setDefaultBorderWidth(s.defaultBorderWidth);
    m_scene->setDefaultBodyColor(s.defaultBodyColor);
    m_scene->setBodyColor(physics::BodyType::Dynamic, s.bodyDynamicColor);
    m_scene->setBodyColor(physics::BodyType::Static, s.bodyStaticColor);
    m_scene->setBodyColor(physics::BodyType::Kinematic, s.bodyKinematicColor);
    m_scene->setUnassignedShapeColor(s.unassignedShapeColor);
    m_scene->setPhysicsBorderWidth(s.physicsBorderWidth);
    m_scene->setPhysicsFillAlpha(s.physicsFillAlpha);
    m_scene->setPhysicsSelectionLineStyle(s.physicsSelectionLineStyle);
    m_scene->setPhysicsSelectionLineWidth(s.physicsSelectionLineWidth);
    m_scene->setPhysicsSelectionColor(s.physicsSelectionColor);
    m_scene->setDebugView(s.debugView);
    if (m_debugViewCheck) {
        // Blocked: the box is catching up with the setting, not being toggled,
        // and signalling would write the file back again.
        const QSignalBlocker blocker(m_debugViewCheck);
        m_debugViewCheck->setChecked(s.debugView);
    }
    m_scene->setShowBodyAxes(s.showBodyAxes);
    m_scene->setBodyAxisLength(s.bodyAxisLength);
    m_scene->setBodyAxisWidth(s.bodyAxisWidth);
    m_scene->setBodyAxisXColor(s.bodyAxisXColor);
    m_scene->setBodyAxisYColor(s.bodyAxisYColor);
    m_scene->setSleepShiftPercent(s.sleepShiftPercent);
    m_scene->setMaxPolygonVertices(s.maxPolygonVertices);
    m_scene->setJointColor(s.jointColor);
    m_undo->setCapacity(s.undoDepth);
    m_scene->setJointTypeColors(s.jointTypeColors);
    m_scene->setJointSelectionColor(s.jointSelectionColor);
    m_scene->setJointSelectionLineWidth(s.jointSelectionLineWidth);
    m_scene->setJointSelectionLineStyle(s.jointSelectionLineStyle);
    m_scene->setJointOutlineColor(s.jointOutlineColor);
    m_scene->setJointAnchorRadius(s.jointAnchorRadius);
    m_scene->setJointAxisLength(s.jointAxisLength);
    m_scene->setJointWaistWidth(s.jointWaistWidth);
    m_scene->setJointOutlineWidth(s.jointOutlineWidth);
    if (m_simulation)
        m_simulation->setStepsPerSecond(s.simulationStepsPerSecond);
    m_scene->setGravity(s.gravity);
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
    s.gravity = QPointF(settings.value("gravityX", s.gravity.x()).toDouble(),
                        settings.value("gravityY", s.gravity.y()).toDouble());
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
    s.physicsBorderWidth = settings.value("borderWidth", s.physicsBorderWidth).toDouble();
    s.physicsFillAlpha = settings.value("fillAlpha", s.physicsFillAlpha).toInt();
    s.physicsSelectionLineStyle = static_cast<Qt::PenStyle>(
        settings.value("selectionLineStyle", static_cast<int>(s.physicsSelectionLineStyle)).toInt());
    s.physicsSelectionLineWidth = settings.value("selectionLineWidth", s.physicsSelectionLineWidth).toDouble();
    s.physicsSelectionColor = QColor(settings.value("selectionColor",
                                                    s.physicsSelectionColor.name(QColor::HexArgb)).toString());
    s.debugView = settings.value("debugView", s.debugView).toBool();
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

    settings.beginGroup("jointTypeColors");
    for (const QString &key : settings.childKeys()) {
        const QColor color(settings.value(key).toString());
        if (color.isValid())
            s.jointTypeColors.insert(key, color);
    }
    settings.endGroup();
    s.simulationStepsPerSecond =
        settings.value("stepsPerSecond", s.simulationStepsPerSecond).toInt();
    settings.endGroup();

    settings.beginGroup("Shapes");
    s.defaultBorderColor = QColor(settings.value("borderColor", s.defaultBorderColor.name(QColor::HexArgb)).toString());
    s.defaultBorderWidth = settings.value("borderWidth", s.defaultBorderWidth).toDouble();
    s.defaultBodyColor = QColor(settings.value("bodyColor", s.defaultBodyColor.name(QColor::HexArgb)).toString());
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
    settings.setValue("gravityX", s.gravity.x());
    settings.setValue("gravityY", s.gravity.y());
    settings.setValue("pixelsPerMeter", s.pixelsPerMeter);
    settings.setValue("solidBounds", s.fieldBoundsSolid);
    settings.setValue("bodyDynamicColor", s.bodyDynamicColor.name(QColor::HexArgb));
    settings.setValue("bodyStaticColor", s.bodyStaticColor.name(QColor::HexArgb));
    settings.setValue("bodyKinematicColor", s.bodyKinematicColor.name(QColor::HexArgb));
    settings.setValue("unassignedShapeColor", s.unassignedShapeColor.name(QColor::HexArgb));
    settings.setValue("borderWidth", s.physicsBorderWidth);
    settings.setValue("fillAlpha", s.physicsFillAlpha);
    settings.setValue("selectionLineStyle", static_cast<int>(s.physicsSelectionLineStyle));
    settings.setValue("selectionLineWidth", s.physicsSelectionLineWidth);
    settings.setValue("selectionColor", s.physicsSelectionColor.name(QColor::HexArgb));
    settings.setValue("debugView", s.debugView);
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

    settings.beginGroup("jointTypeColors");
    settings.remove(QString()); // drop ids that are no longer offered
    for (auto it = s.jointTypeColors.constBegin(); it != s.jointTypeColors.constEnd(); ++it) {
        if (it.value().isValid())
            settings.setValue(it.key(), it.value().name(QColor::HexArgb));
    }
    settings.endGroup();
    settings.setValue("stepsPerSecond", s.simulationStepsPerSecond);
    settings.endGroup();

    settings.beginGroup("Shapes");
    settings.setValue("borderColor", s.defaultBorderColor.name(QColor::HexArgb));
    settings.setValue("borderWidth", s.defaultBorderWidth);
    settings.setValue("bodyColor", s.defaultBodyColor.name(QColor::HexArgb));
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
