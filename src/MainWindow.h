// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ruslan Muhlinin. See LICENSE.
#pragma once

#include <QMainWindow>

#include <memory>
#include <QHash>
#include <QVariantMap>
#include <QList>
#include <QStringList>
#include <QPointer>
#include "OptionsDialog.h"
#include "EditorMode.h"

#include <QJsonObject>
#include "ToolOptionsBar.h"

#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QToolBar;
class CanvasScene;
class HelpWindow;
class ShapeItem;
class PhysicsBody;
class Joint;
class QGraphicsView;
class QAction;
class QMenu;
class PropertyPanel;
class SceneTree;
class UndoStack;
class QCheckBox;
class QTabWidget;
class SimulationController;
class QLabel;
class QComboBox;
class QToolButton;
class FullScreenView;
class ViewLayersCombo;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // Closing with unsaved work asks first, the same as New and Open do.
    void closeEvent(QCloseEvent *event) override;

    void showEvent(QShowEvent *event) override;

private slots:
    void on_actionUndo_triggered();
    void on_actionRedo_triggered();
    void on_actionAddRectangle_triggered();
    void on_actionAddCircle_triggered();
    void on_actionAddExplosion_triggered();
    void on_actionAddRay_triggered();
    void on_actionAddPolygon_triggered();
    void onActiveItemChanged(ShapeItem *item);
    void on_actionOptions_triggered();
    // Built fresh each time the menu opens, so a converter dropped into the
    // folder appears without a restart.
    void refreshExportMenu();
    void on_actionAbout_triggered();
    // Where preferences live. Redirectable through PHYSALIS_SETTINGS so a
    // test run cannot read -- or overwrite -- the settings of the installed
    // application sitting in the same folder.
    static QString settingsFilePath();

    bool confirmDiscardChanges(const QString &title);
    void on_actionNewScene_triggered();
    // Wrappers, because connectSlotsByName matches on name alone and these
    // handlers are called from elsewhere too, or return a value.
    void on_actionSaveScene_triggered() { onSaveScene(); }
    void on_actionSaveSceneAs_triggered() { onSaveSceneAs(); }
    void on_actionCreateBody_triggered() { onCreateBody(); }
    void on_actionExit_triggered() { close(); }
    void on_actionLoadScene_triggered();
    bool onSaveScene();
    bool onSaveSceneAs();
    void updateUndoActions();
    void onOriginsToCenterOfMass();
    void addAnchorActions(QMenu *menu, Joint *joint, bool simulating);
    void updatePasteAction();
    void updateCopyAction();
    void updateDeleteAction();
    bool clipboardHasShape() const;
    QVector<ShapeItem *> clipboardShapes() const;
    QVector<ShapeItem *> selectedShapes() const;
    void on_actionCopy_triggered();
    void on_actionPaste_triggered();
    void on_actionSaveScreenshot_triggered();

private:
    void updateWindowTitle();

public:
    // Set by main() from the generated header, so the build number never
    // reaches the compile graph of anything else.
    void setVersion(const QString &version);

private:
    // Where the export converters live. Not a scene property, so unlike the
    // rest of the settings it has nowhere else to be kept.
    QString m_converterPath;
    // Which engine a new scene is built for. An open scene keeps its own.
    QString m_defaultEngineName;
    // The field a new scene is given: its size, its background and its grid.
    // The open scene's own are in the scene, and in the file it came from.
    OptionsDialog::Settings m_fieldDefaults;
    QString engineForNewScenes() const;
    // Points the scene, the simulation, the joint menu and the panels at one
    // engine.
    void useEngine(const QString &name);
    // Moves the editor to another engine, closing the open scene first --
    // asking whether to save it. False if the user decided to keep it.
    bool adoptEngine(const QString &name);
    bool confirmCloseForEngine(const QString &name);
    // What each converter asked to be asked, by converter id.
    QHash<QString, QVariantMap> m_converterSettings;

    QString m_version;

private slots:
    void on_canvasView_customContextMenuRequested(const QPoint &pos);
    void onPolygonDrawingChanged(bool drawing);
    void onEditorModeChanged(EditorMode mode);
    void onAddJoint(const QString &typeId);
    void on_actionDeleteJoint_triggered();
    void onJointSelectionChanged();
    void onCreateBody(bool asStatic = false);
    // A body of the kind asked for, from whatever is selected for physics.
    void createBodyOfType(physics::BodyType type);
    // Adds "Create Static/Kinematic/Dynamic Body" for the loose shapes under
    // the cursor, and says whether it added anything.
    bool addCreateBodyActions(QMenu *menu, const QPointF &scenePos);
    void on_actionDissolveBody_triggered();
    void onPhysicsSelectionChanged();
    void onSimulationStateChanged();
    void updateTransportActions();
    void onFieldSettingsChanged();

private:
    OptionsDialog::Settings loadSettingsFromFile() const;
    void saveSettingsToFile(const OptionsDialog::Settings &s) const;
    void applySettings(const OptionsDialog::Settings &s);
    OptionsDialog::Settings currentSettingsSnapshot() const;

    // Every widget, action and menu from ui/MainWindow.ui.
    std::unique_ptr<Ui::MainWindow> m_ui;
    // Not in the form: filled from the engine at run time.
    QMenu *m_jointTypeMenu = nullptr;
    void rebuildJointMenu();

    QMenu *m_toolsMenu = nullptr;
    QAction *m_toolsAction = nullptr;
    ToolOptionsBar *m_toolOptions = nullptr;
    // Where the shapes the open tool moves stood before it first ran, so each
    // run starts from there instead of compounding on the last one.
    struct ToolSubject {
        QPointer<ShapeItem> shape;
        QPointF pos;
        qreal rotation = 0.0;
    };
    QVector<ToolSubject> m_toolSubjects;
    QVector<QPointer<ShapeItem>> m_toolCreated;
    // The shape the open tool is rewriting, and what it was before: every run
    // starts again from that, and Cancel puts it back.
    QPointer<ShapeItem> m_toolShape;
    QJsonObject m_toolOriginal;
    // A tool that turns one shape into several hides the original instead,
    // since there is nothing left to rewrite in place.
    QPointer<ShapeItem> m_toolHidden;
    std::function<void()> m_toolRun;
    QString m_toolLabel;
    // The side panel's tabs: their icon, the name that became a tooltip, and
    // the settings key that remembers whether the user wants it at all.
    void buildSidePanelTabs();
    void applyVisibleTabs();
    // Only the tab you are on spells its name; the rest keep to their icon.
    void updateSidePanelTabLabels();
    QStringList m_hiddenTabs;

    HelpWindow *m_help = nullptr;

    CanvasScene *m_scene = nullptr;
    UndoStack *m_undo = nullptr;
    // What the canvas draws -- see the kLayer* keys in MainWindow.cpp.
    ViewLayersCombo *m_layers = nullptr;
    void applyLayer(const QString &key, bool on);
    // Puts saved settings onto the transport controls. They are built with the
    // toolbar, after the settings are first applied, so this runs twice at
    // startup -- once with nothing there, once with everything.
    void syncTransportWidgets(const OptionsDialog::Settings &s);
    // Show the run full screen, with the keys standing in for the transport.
    QCheckBox *m_fullScreenCheck = nullptr;
    QPointer<FullScreenView> m_fullScreen;
    void updateFullScreenView();
    // How fast a run plays, next to the transport buttons it belongs with.
    QComboBox *m_speedCombo = nullptr;
    // The Rules tab's own caption, which grows a count and a mark while any
    // rule is unfinished -- the cards say so themselves, but the tab may not be
    // the one on top.
    void showIncompleteRules(int count);

    // The log readout, pinned to the canvas corner.
    QLabel *m_logOverlay = nullptr;
    QLabel *m_caption = nullptr;
    QWidget *m_controls = nullptr;
    QHash<QString, QWidget *> m_controlWidgets;
    void updateLogOverlay();
    // Font, size and colour of that readout, from Options.
    void applyLogStyle();
    QString m_logFontFamily;
    int m_logFontSize = 9;
    QColor m_logColor { 0xaa, 0x00, 0x00 };
    Qt::Corner m_logCorner = Qt::TopLeftCorner;
    // The overlay is a child of the view, so it has to be put back in its
    // corner whenever the view changes size.
    // Whichever view is showing the scene: the editor's, or the full-screen
    // one while that is up. The log, the caption and the controls are its
    // children, so they move house when it changes.
    QGraphicsView *overlayHost() const;
    void moveOverlaysTo(QGraphicsView *host);
    void placeLogOverlay();
    // Words a rule put up, under the log in the top-left corner.
    void updateCaption();
    // The body the scene says to keep in view, put back in the middle after
    // every step of a run.
    void followFocusedBody();
    // What a new scene's field starts as. Everything the *.phys carries in its
    // field block is the scene's own, so Options holds the default for the
    // next scene rather than a setting applied to the one that is open.
    void applyFieldDefaults();
    static void centreViewOn(QGraphicsView *view, const QPointF &at, const QRectF &field);
    // The scene own controls, on the canvas under the caption. Rebuilt when
    // the scene declares different ones; refreshed when their values change.
    void rebuildControls();
    void refreshControls();
    bool eventFilter(QObject *watched, QEvent *event) override;
    // The heading a logged property sits under, worked out again rather than
    // taken from what the watch was named when it was added.
    QString logLabelFor(const CanvasScene::Watch &watch) const;
public:
    // Reachable from tests: the menu items themselves cannot be clicked
    // without pumping a blocking popup.
    void duplicateShapeForTest(ShapeItem *item) { duplicateShape(item); }
    void flipShapeForTest(ShapeItem *item, bool horizontally) { flipShape(item, horizontally); }
    // Opening a scene is also how a file passed on the command line arrives.
    bool openScene(const QString &path);
    bool saveSceneAs(QString path);

    int cloneForTest(int rows, int columns, qreal gapX, qreal gapY)
    {
        return cloneSelection(selectedShapes(), rows, columns, gapX, gapY).size();
    }
    int mirrorForTest(int side, qreal margin)
    {
        return mirrorSelection(selectedShapes(), side, margin).size();
    }
    ShapeItem *circleToChainForTest(ShapeItem *circle, int segments, qreal startAngle = 0.0)
    {
        return m_scene->convertCircleToChain(circle, segments, startAngle);
    }
    QVector<ShapeItem *> toSegmentsForTest(ShapeItem *shape, int segments,
                                           qreal startAngle = 0.0)
    {
        return m_scene->convertToSegments(shape, segments, startAngle);
    }
    void createBodyForTest(bool asStatic) { onCreateBody(asStatic); }
    void startRunForTest();
    // What the Options dialog would hand back, and what it would be given:
    // the dialog itself is modal, so a test cannot work it from the outside.
    void applySettingsForTest(const OptionsDialog::Settings &s) { applySettings(s); }
    OptionsDialog::Settings settingsSnapshotForTest() const { return currentSettingsSnapshot(); }
    void stopRunForTest();
    void setFullScreenForTest(bool on);
    void moveOverlaysForTest(QGraphicsView *host);
    QWidget *overlayHostForTest() const;
    void openSegmentsToolForTest(ShapeItem *shape) { shapeToSegments(shape); }
    void openChainToolForTest(ShapeItem *shape) { circleToChain(shape); }
    void runToolForTest() { runTool(); }
    void applyToolForTest() { applyTool(); }
    void cancelToolForTest() { cancelTool(); }
    bool openSceneForTest(const QString &path) { return openScene(path); }
    bool adoptEngineForTest(const QString &name) { return adoptEngine(name); }
    bool saveSceneForTest() { return onSaveScene(); }
    bool saveSceneAsForTest(const QString &path) { return saveSceneAs(path); }
    // Which of the three candidates the settings live in. Kept apart from
    // asking the environment and the platform for them, so what it decides can
    // be read -- and checked -- without either.
    static QString chooseSettingsFile(const QString &redirect, const QString &configDir,
                                      const QString &besideExecutable);

private:
    void buildAlignMenu();
    void buildToolsMenu();
    void updateAlignActions();

    // A tool runs the moment it is picked and then stays open: the strip under
    // the toolbar carries the numbers it guessed at, every change runs it
    // again from the state it found, and Apply is what finally records it.
    void beginTool(const QString &title, const QString &label,
                   const QVector<ToolOption> &options, std::function<void()> run);
    void placeToolOptions();
    void runTool();
    void undoTool();
    void applyTool();
    void cancelTool();
    bool toolIsOpen() const { return m_toolRun != nullptr; }
    QVector<ShapeItem *> cloneSelection(const QVector<ShapeItem *> &source, int rows, int columns,
                                        qreal gapX, qreal gapY);
    // The side the reflected copy lands on, in the order the panel lists them.
    enum MirrorSide { MirrorRight, MirrorLeft, MirrorTop, MirrorBottom };
    QVector<ShapeItem *> mirrorSelection(const QVector<ShapeItem *> &source, int side,
                                         qreal margin);
    void circleToChain(ShapeItem *item);
    void shapeToSegments(ShapeItem *item);
    void duplicateShape(ShapeItem *item);
    void flipShape(ShapeItem *item, bool horizontally);
    void convertToPolygon(ShapeItem *item);

    // Toolbar contents, shown and hidden with their editor mode.
    QList<QAction *> m_editModeActions;
    QList<QAction *> m_physicsModeActions;
    // Everything that changes the scene or puts another in its place; all off
    // while a run is going (see the constructor).
    QList<QAction *> m_lockedWhileRunning;
    // Of those, the ones nothing else turns back on after a run: nothing about
    // the selection decides them.
    QList<QAction *> m_alwaysOnWhenStopped;
    QList<QAction *> m_transportActions;

    QString m_scenePath;
    // The scenes opened last, newest first, kept across sessions. Five is as
    // many as a menu reads at a glance.
    static constexpr int kRecentFiles = 5;
    QStringList m_recentFiles;
    void rememberRecent(const QString &path);
    void rebuildRecentMenu();
    // The scenes shipped with the program. Nothing there, nothing offered.
    void buildExamplesMenu();
    void buildDocumentationMenu();
    // Where the last screenshot went, so the next one is offered beside it.
    QString m_lastScreenshotPath;
    int m_pasteCount = 0;
    SimulationController *m_simulation = nullptr;
    QAction *m_resetScaleAction = nullptr;
    QLabel *m_statusHelpLabel = nullptr;
    QHash<int, QToolButton *> m_modeButtons;
    QComboBox *m_scaleCombo = nullptr;
    bool m_didInitialCenter = false;
};
