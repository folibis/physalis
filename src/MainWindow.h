#pragma once

#include <QMainWindow>

#include <memory>
#include <QHash>
#include <QList>
#include "OptionsDialog.h"
#include "EditorMode.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class CanvasScene;
class ShapeItem;
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
    bool clipboardHasShape() const;
    void on_actionCopy_triggered();
    void on_actionPaste_triggered();

private:
    void updateWindowTitle();

public:
    // Set by main() from the generated header, so the build number never
    // reaches the compile graph of anything else.
    void setVersion(const QString &version);

private:
    QString m_version;

private slots:
    void on_canvasView_customContextMenuRequested(const QPoint &pos);
    void onPolygonDrawingChanged(bool drawing);
    void onEditorModeChanged(EditorMode mode);
    void onAddJoint(const QString &typeId);
    void on_actionDeleteJoint_triggered();
    void onJointSelectionChanged();
    void onCreateBody();
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
    CanvasScene *m_scene = nullptr;
    UndoStack *m_undo = nullptr;
    QCheckBox *m_debugViewCheck = nullptr;
    // The log readout, pinned to the canvas corner.
    QLabel *m_logOverlay = nullptr;
    void updateLogOverlay();
public:
    // Reachable from tests: the menu items themselves cannot be clicked
    // without pumping a blocking popup.
    void duplicateShapeForTest(ShapeItem *item) { duplicateShape(item); }
    void flipShapeForTest(ShapeItem *item, bool horizontally) { flipShape(item, horizontally); }
    // Opening a scene is also how a file passed on the command line arrives.
    bool openScene(const QString &path);
    bool saveSceneAs(QString path);

    bool openSceneForTest(const QString &path) { return openScene(path); }
    bool saveSceneForTest() { return onSaveScene(); }
    bool saveSceneAsForTest(const QString &path) { return saveSceneAs(path); }

private:
    void duplicateShape(ShapeItem *item);
    void flipShape(ShapeItem *item, bool horizontally);

    // Toolbar contents, shown and hidden with their editor mode.
    QList<QAction *> m_editModeActions;
    QList<QAction *> m_physicsModeActions;
    QList<QAction *> m_transportActions;

    QString m_scenePath;
    int m_pasteCount = 0;
    QComboBox *m_engineCombo = nullptr;
    SimulationController *m_simulation = nullptr;
    QAction *m_resetScaleAction = nullptr;
    QLabel *m_statusHelpLabel = nullptr;
    QHash<int, QToolButton *> m_modeButtons;
    QComboBox *m_scaleCombo = nullptr;
    bool m_didInitialCenter = false;
};
