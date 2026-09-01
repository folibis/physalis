#pragma once

#include <QGraphicsScene>
#include <QPolygonF>
#include <QSet>
#include <QHash>
#include <QVector>
#include <QStringList>
#include "ShapeItem.h"
#include "EditorMode.h"
#include "Rule.h"
#include "PhysicsTypes.h"
#include "JointTypes.h"

class RectangleItem;
class CircleItem;
class PolygonItem;
class QKeyEvent;
class QGraphicsSceneWheelEvent;
class PropertyPane;
class PhysicsBody;
class Joint;

enum class SnapPoint {
    Position, // the shape's top-left corner (pos() + rect.topLeft()); always visible
    Origin    // the shape's pivot point; only drawn/visible in Rotating mode
};

enum class HandleShape { Circle, Square };

class ExplosionItem;
class RayItem;

class CanvasScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit CanvasScene(QObject *parent = nullptr);

    RectangleItem *addRectangle(const QPointF &scenePos = QPointF(0, 0));
    CircleItem *addCircle(const QPointF &scenePos = QPointF(0, 0));

    void startPolygonDrawing();
    bool isPolygonDrawing() const { return m_polygonDrawing; }

    ShapeItem *activeItem() const { return m_active; }

    QVector<ShapeItem *> shapes() const;

    // Named points, used as somewhere for a position-based rule action to
    // happen. They are not shapes and never reach the physics world.
    QVector<ExplosionItem *> explosions() const { return m_explosions; }
    ExplosionItem *addExplosion(const QPointF &scenePos = QPointF(0, 0));
    void removeExplosion(ExplosionItem *explosion);
    ExplosionItem *explosionNamed(const QString &name) const;
    void selectExplosion(ExplosionItem *explosion);

    // Rangefinders. Like explosions they never enter the physics world; the
    // engine is only asked to measure along them.
    QVector<RayItem *> rays() const { return m_rays; }
    RayItem *addRay(const QPointF &scenePos = QPointF(0, 0));
    void removeRay(RayItem *ray);
    RayItem *rayNamed(const QString &name) const;
    void selectRay(RayItem *ray);
    RayItem *selectedRay() const { return m_selectedRay; }

    ExplosionItem *selectedExplosion() const { return m_selectedExplosion; }

    // Every name in use, across shapes, bodies and joints alike -- they share
    // one namespace because a rule can name any of them in the same field.
    QSet<QString> takenNames(const QObject *except = nullptr) const;
    QString uniqueName(const QString &desired, const QObject *except = nullptr) const;

    void selectShape(ShapeItem *shape);

    // Edit mode keeps one active shape -- the one carrying the handles and
    // filling the property pane -- and any number picked alongside it with
    // Shift or Ctrl. Moving or turning the active one takes the whole set.
    const QVector<ShapeItem *> &editSelection() const { return m_editSelection; }
    void addToEditSelection(ShapeItem *shape);
    void clearEditSelection();

    // The pivot a multi-shape selection turns about. It starts at the centre
    // of everything picked and can be dragged anywhere; changing the selection
    // puts it back at the centre.
    QPointF editSelectionOrigin() const { return m_groupOrigin; }
    void setEditSelectionOrigin(const QPointF &scenePos);

    // What the whole selection covers, in scene coordinates. The group's box
    // and its corner handles are drawn on this.
    QRectF editSelectionBounds() const;
    // The frame drawn round that, held clear of the shapes' own outlines so
    // the two do not sit on top of each other and read as one thick line.
    QRectF editSelectionBox() const;
    // The lead plus everything picked alongside it.
    QVector<ShapeItem *> selectedShapes() const;
    // True while the selection is being turned rather than moved -- which is
    // what decides between showing handles and showing the pivot.
    bool editSelectionRotating() const;

    void setEditorMode(EditorMode mode);
    EditorMode editorMode() const { return m_editorMode; }

    bool geometryEditingAllowed() const { return m_editorMode == EditorMode::Edit; }

    bool movingShape() const { return m_dragMode == DragMode::Move; }

    // --- Physics-mode selection and bodies ----------------------------
    // Physics mode has its own selection, separate from Edit mode's single
    const QVector<ShapeItem *> &physicsSelection() const { return m_physicsSelection; }
    bool isSelectedForPhysics(ShapeItem *shape) const { return m_physicsSelection.contains(shape); }
    void selectForPhysics(ShapeItem *shape, bool additive = false);
    void clearPhysicsSelection();

    const QVector<PhysicsBody *> &bodies() const { return m_bodies; }

    PhysicsBody *createBodyFromSelection();

    PhysicsBody *createEmptyBody();

    void pruneEmptyBodies();

    // --- rules ------------------------------------------------------------
    // What the scene does while it runs, as data.
    const QVector<Rule> &rules() const { return m_rules; }
    QVector<Rule> &rules() { return m_rules; }
    void setRules(const QVector<Rule> &rules);
    void renameInRules(const QString &previous, const QString &current);
    void notifyRulesChanged() { emit rulesChanged(); }

    void clearContents();
    void destroyBody(PhysicsBody *body);

    // --- Joints -------------------------------------------------------
    // Joints are constraints between two bodies. The scene owns them, but
    // knows nothing about what any given type does: see Joint.
    const QVector<Joint *> &joints() const { return m_joints; }

    QString simulationEngineName() const { return m_simulationEngineName; }
    void setSimulationEngineName(const QString &name);

    Joint *createJoint(const QString &typeId, PhysicsBody *bodyA, PhysicsBody *bodyB,
                       int anchorCount, const QVariantMap &defaultParams);
    void destroyJoint(Joint *joint);

    Joint *selectedJoint() const { return m_selectedJoint; }
    void selectJoint(Joint *joint);

    static constexpr int kJointShaft = -1;
    // The joint under `scenePos`, or nullptr.
    Joint *jointAt(const QPointF &scenePos, int *end = nullptr) const;

    // --- Joint appearance ---------------------------------------------
    // The knobs are the anchors, and are the only part that can be dragged.
    void setJointColor(const QColor &color);

    void setJointSelectionColor(const QColor &color);
    QColor jointSelectionColor() const { return m_jointSelectionColor; }
    void setJointSelectionLineWidth(qreal width);
    qreal jointSelectionLineWidth() const { return m_jointSelectionLineWidth; }
    void setJointSelectionLineStyle(Qt::PenStyle style);
    Qt::PenStyle jointSelectionLineStyle() const { return m_jointSelectionLineStyle; }

    void setJointTypeColor(const QString &typeId, const QColor &color);
    QColor jointTypeColor(const QString &typeId) const;
    // How the engine says a joint of this type should be drawn.
    physics::JointVisual jointVisual(const QString &typeId) const;
    // The engine whose catalogue describes this scene: the one set for
    // simulation, or the default when none has been set yet.
    QString describingEngineName() const;
    QHash<QString, QColor> jointTypeColors() const { return m_jointTypeColors; }
    void setJointTypeColors(const QHash<QString, QColor> &colors);
    QColor jointColor() const { return m_jointColor; }

    void setJointAnchorRadius(qreal radius);
    qreal jointAnchorRadius() const { return m_jointAnchorRadius; }

    // The pinched middle running between the two anchors.
    void setJointAxisLength(qreal length);
    qreal jointAxisLength() const { return m_jointAxisLength; }

    void setJointWaistWidth(qreal width);
    qreal jointWaistWidth() const { return m_jointWaistWidth; }

    void setJointOutlineWidth(qreal width);
    qreal jointOutlineWidth() const { return m_jointOutlineWidth; }

    void setJointOutlineColor(const QColor &color);
    QColor jointOutlineColor() const { return m_jointOutlineColor; }

    PhysicsBody *commonSelectedBody() const;

    bool selectionIsWholeBody() const;

    PropertyPane *makePropertyPane() const;

    void setFieldSize(qreal width, qreal height);
    qreal fieldWidth() const { return m_fieldWidth; }
    qreal fieldHeight() const { return m_fieldHeight; }

    // --- Physics (the field acts as the simulation's world) -----------
    // Gravitational acceleration in m/s^2.
    void setGravity(const QPointF &gravity);
    QPointF gravity() const { return m_world.gravity; }

    // How many scene units make up one simulated meter.
    void setPixelsPerMeter(qreal pixelsPerMeter);
    qreal pixelsPerMeter() const { return m_world.pixelsPerMeter; }

    // Whether the field's own edges act as static walls.
    void setFieldBoundsSolid(bool solid);
    bool fieldBoundsSolid() const { return m_fieldBoundsSolid; }


    const physics::WorldDesc &world() const { return m_world; }
    physics::WorldDesc &world() { return m_world; }
    void notifyFieldPropertyChanged() { emit fieldPropertyChanged(); }
    void notifyShapesChanged() { emit shapesChanged(); }

    void notifyEdit(const QString &label, const QString &mergeKey = QString())
    {
        emit editCommitted(label, mergeKey);
    }

    physics::WorldDesc toWorldDesc() const;

    // --- Physics-mode appearance --------------------------------------
    void setBodyColor(physics::BodyType type, const QColor &color);
    QColor bodyColor(physics::BodyType type) const;
    // Sensors are pass-through, and look nothing like something solid.
    QColor sensorColor() const { return m_sensorColor; }
    void setSensorColor(const QColor &color) { m_sensorColor = color; update(); }

    void setUnassignedShapeColor(const QColor &color);
    QColor unassignedShapeColor() const { return m_unassignedShapeColor; }

    void setPhysicsBorderWidth(qreal width);
    qreal physicsBorderWidth() const { return m_physicsBorderWidth; }

    void setPhysicsFillAlpha(int alpha);
    int physicsFillAlpha() const { return m_physicsFillAlpha; }

    void setPhysicsSelectionLineStyle(Qt::PenStyle style);
    Qt::PenStyle physicsSelectionLineStyle() const { return m_physicsSelectionLineStyle; }

    void setPhysicsSelectionLineWidth(qreal width);
    qreal physicsSelectionLineWidth() const { return m_physicsSelectionLineWidth; }

    void setPhysicsSelectionColor(const QColor &color);
    QColor physicsSelectionColor() const { return m_physicsSelectionColor; }

    // --- The log ------------------------------------------------------
    // Properties pinned to the readout in the canvas corner. Identified the
    // same way a rule identifies one -- object name plus the engine's key --
    // so a logged value survives selecting something else, and can be read
    // out of the running world.
    struct Watch
    {
        QString objectName;
        QString propertyKey;
        QString label;
        bool operator==(const Watch &other) const
        {
            return objectName == other.objectName && propertyKey == other.propertyKey;
        }
    };
    const QVector<Watch> &watches() const { return m_watches; }
    void addWatch(const Watch &watch);
    void removeWatch(const QString &objectName, const QString &propertyKey);
    bool isWatched(const QString &objectName, const QString &propertyKey) const;
    void clearWatches();
    void setWatches(const QVector<Watch> &watches);
    // Reads a logged property the engine knows nothing about -- a shape's own
    // geometry. The solver moves the shape, so these follow a run too.
    QVariant readSceneValue(const QString &objectName, const QString &key) const;

    void setDebugView(bool on);
    bool debugView() const { return m_debugView; }

    void setShowBodyAxes(bool show);
    bool showBodyAxes() const { return m_showBodyAxes; }

    void setBodyAxisLength(qreal length);
    qreal bodyAxisLength() const { return m_bodyAxisLength; }

    void setBodyAxisWidth(qreal width);
    qreal bodyAxisWidth() const { return m_bodyAxisWidth; }

    void setBodyAxisXColor(const QColor &color);
    QColor bodyAxisXColor() const { return m_bodyAxisXColor; }

    void setBodyAxisYColor(const QColor &color);
    QColor bodyAxisYColor() const { return m_bodyAxisYColor; }

    void setMaxPolygonVertices(int count);
    int maxPolygonVertices() const { return m_maxPolygonVertices; }

    QStringList solidBodyProblems(const QVector<ShapeItem *> &shapes) const;

    void setSleepShiftPercent(int percent);
    int sleepShiftPercent() const { return m_sleepShiftPercent; }

    void setSimulationRunning(bool running);
    bool simulationRunning() const { return m_simulationRunning; }

    bool selectionAllowed() const { return !m_simulationRunning; }

    void setShowGrid(bool show);
    bool showGrid() const { return m_showGrid; }

    void setGridCellSize(qreal size);
    qreal gridCellSize() const { return m_gridCellSize; }

    void setGridColor(const QColor &color);
    QColor gridColor() const { return m_gridColor; }

    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_backgroundColor; }

    void setSnapToGrid(bool snap);
    bool snapToGrid() const { return m_snapToGrid; }

    // Which point of the shape gets pulled onto the grid while moving.
    void setSnapPoint(SnapPoint point);
    SnapPoint snapPoint() const { return m_snapPoint; }

    // The snap increment, in scene units.
    void setSnapStep(qreal step);
    qreal snapStep() const { return m_snapStep; }

    // Maximum distance (scene units) from a snap-step multiple at which the
    // snap point snaps to it; beyond that, movement on that axis is free.
    void setSnapSensitivity(qreal sensitivity);
    qreal snapSensitivity() const { return m_snapSensitivity; }

    void setDefaultBorderColor(const QColor &color);
    QColor defaultBorderColor() const { return m_defaultBorderColor; }

    void setDefaultBorderWidth(qreal width);
    qreal defaultBorderWidth() const { return m_defaultBorderWidth; }

    void setDefaultBodyColor(const QColor &color);
    QColor defaultBodyColor() const { return m_defaultBodyColor; }

    void setSelectionLineStyle(Qt::PenStyle style);
    Qt::PenStyle selectionLineStyle() const { return m_selectionLineStyle; }

    void setSelectionLineWidth(qreal width);
    qreal selectionLineWidth() const { return m_selectionLineWidth; }

    void setSelectionColor(const QColor &color);
    QColor selectionColor() const { return m_selectionColor; }

    void setHandleShape(HandleShape shape);
    HandleShape handleShape() const { return m_handleShape; }

    void setHandleSize(qreal size);
    qreal handleSize() const { return m_handleSize; }

    void setHandleColor(const QColor &color);
    QColor handleColor() const { return m_handleColor; }

    void setHandleBorderWidth(qreal width);
    qreal handleBorderWidth() const { return m_handleBorderWidth; }

    void setHandleBorderColor(const QColor &color);
    QColor handleBorderColor() const { return m_handleBorderColor; }

    void setCurrentScale(qreal scale);
    qreal currentScale() const { return m_currentScale; }

    void setScaleMin(qreal value);
    qreal scaleMin() const { return m_scaleMin; }

    void setScaleMax(qreal value);
    qreal scaleMax() const { return m_scaleMax; }

    void setScaleStep(qreal value);
    qreal scaleStep() const { return m_scaleStep; }

public slots:
    void switchActiveToSelected();
    void switchActiveToEditing();
    void switchActiveToRotating();
    void deleteActiveItem();

signals:
    void editorModeChanged(EditorMode mode);
    void physicsSelectionChanged();
    void editSelectionChanged();
    void jointsChanged();
    void selectedJointChanged(Joint *joint);
    void createBodyRequested();
    void bodiesChanged();

    void simulationRunningChanged(bool running);
    void watchesChanged();
    void rulesChanged();

    // See notifyEdit(). The undo stack is the only thing that listens.
    void editCommitted(const QString &label, const QString &mergeKey);

    void shapesChanged();
    void explosionsChanged();
    void raysChanged();
    void selectedRayChanged(RayItem *ray);
    void selectedExplosionChanged(ExplosionItem *explosion);
    void activeItemChanged(ShapeItem *item);
    void polygonDrawingChanged(bool drawing);
    void scaleChanged(qreal scale);
    void fieldPropertyChanged();

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QGraphicsSceneWheelEvent *event) override;

private:
    enum class DragMode { None, Move, Scale, Rotate, Origin, EditNode, PanField,
                          // The pivot of a multi-shape selection, which
                          // belongs to no single shape.
                          GroupOrigin,
                          // A corner of the box drawn round a multi-shape
                          // selection, which scales the whole set.
                          GroupScale,
                          // Physics mode: drags a whole body, not one shape of
                          // it, and without Edit mode's handles.
                          MoveBody };

    void activate(ShapeItem *item);
    void deactivate();

    QPointF snapScenePoint(const QPointF &scenePoint) const;

    bool m_snapSuspended = false;

    void finishPolygonDrawing(bool closed);
    void cancelPolygonDrawing();

    void setNodeSelection(const QSet<int> &indices);

    // Invoked on Enter in Editing mode with exactly two nodes selected.
    void handleEditModeEnter();

    EditorMode m_editorMode = EditorMode::Edit;

    QVector<ShapeItem *> m_physicsSelection;

    // The shapes picked alongside m_active, and where they stood when the
    // current drag began, so each one follows the lead exactly.
    QVector<ShapeItem *> m_editSelection;
    QVector<QPointF> m_groupStartPositions;
    QVector<qreal> m_groupStartRotations;
    QPointF m_groupLeadStart;
    qreal m_groupLeadStartRotation = 0.0;
    void beginGroupDrag();

    QPointF m_groupOrigin;
    // Cleared whenever the selection changes, so the pivot returns to the
    // centre rather than staying where it was put for an older set of shapes.
    bool m_groupOriginPlaced = false;
    void refreshGroupOrigin();
    bool groupOriginHandleContains(const QPointF &scenePos) const;

    // The four corners of the group box, and what a scale started from.
    QVector<QPointF> groupHandlePoints() const;
    int groupHandleAt(const QPointF &scenePos) const;
    QPointF m_groupScaleAnchor;
    QPointF m_groupScaleStart;
    QVector<QRectF> m_groupScaleStartRects;
    QVector<QPointF> m_groupScaleStartOrigins;
    QVector<QPointF> m_groupScaleStartOriginScene;
    void beginGroupScale(int handle);
    void applyGroupScale(qreal factor);

    // A press inside a multi-shape selection drags the whole set, so the shape
    // under the cursor is remembered instead: a click that turns out not to be
    // a drag collapses the selection onto it, the way a click normally would.
    ShapeItem *m_groupClickCandidate = nullptr;
    QPointF m_pressScenePos;
    // Set between a shape leaving a body and the queued prune that follows.
    bool m_prunePending = false;
    QVector<Rule> m_rules;
    // Owned; bodies and joints are deleted with the scene.
    QVector<PhysicsBody *> m_bodies;
    QVector<Joint *> m_joints;
    Joint *m_selectedJoint = nullptr;
    QString m_simulationEngineName;

    // Anchor drag: which joint is being dragged and which of its ends.
    Joint *m_draggedJoint = nullptr;
    int m_draggedJointEnd = 0;

    QColor m_jointColor { 0xE8, 0xC4, 0x6A };
    QHash<QString, QColor> m_jointTypeColors;
    // Filled from the engine on first use; cleared when the engine changes.
    mutable QHash<QString, physics::JointVisual> m_jointVisuals;
    mutable QString m_jointVisualsEngine;
    QColor m_jointSelectionColor { 230, 140, 40 };
    qreal m_jointSelectionLineWidth = 2.0;
    Qt::PenStyle m_jointSelectionLineStyle = Qt::DotLine;
    QColor m_jointOutlineColor { 0x5A, 0x4A, 0x21 };
    qreal m_jointAnchorRadius = 7.0;
    qreal m_jointAxisLength = 40.0;
    qreal m_jointWaistWidth = 3.5;
    qreal m_jointOutlineWidth = 1.6;

    ShapeItem *m_active = nullptr;

    DragMode m_dragMode = DragMode::None;
    HandleId m_activeHandle = HandleId::None;
    QPointF m_lastScenePos;

    QPoint m_panLastScreenPos;

    QSet<int> m_selectedNodes;
    int m_editNodeIndex = -1;
    QHash<int, QPointF> m_editDragNodeStart;

    QPointF m_moveDragVirtualPos;

    // What a Physics-mode drag is carrying, and where each piece started.
    QVector<ShapeItem *> m_bodyDragShapes;
    QVector<QPointF> m_bodyDragStartPositions;
    QString m_bodyDragLabel;

    QPointF m_dragOriginScene;
    qreal m_rotateStartAngle = 0.0;
    qreal m_itemStartRotation = 0.0;

    qreal m_fieldWidth = 2000.0;
    qreal m_fieldHeight = 2000.0;
    bool m_showGrid = true;
    qreal m_gridCellSize = 20.0;
    QColor m_gridColor = QColor(230, 230, 230);
    QColor m_backgroundColor = Qt::white;
    bool m_snapToGrid = false;

    QColor m_sensorColor { 0x05, 0xC9, 0x36 };
    QVector<ExplosionItem *> m_explosions;
    QVector<RayItem *> m_rays;
    RayItem *m_selectedRay = nullptr;
    RayItem *m_draggedRay = nullptr;
    ExplosionItem *m_selectedExplosion = nullptr;
    ExplosionItem *m_draggedExplosion = nullptr;
    physics::WorldDesc m_world;
    bool m_fieldBoundsSolid = false;

    QColor m_bodyDynamicColor { 0x2E, 0x86, 0xC1 };   // blue
    QColor m_bodyStaticColor { 0x27, 0x9E, 0x6A };    // green
    QColor m_bodyKinematicColor { 0x88, 0x4E, 0xA0 }; // purple
    QColor m_unassignedShapeColor { 0x8C, 0x8C, 0x8C };
    qreal m_physicsBorderWidth = 2.0;
    int m_physicsFillAlpha = 90;
    Qt::PenStyle m_physicsSelectionLineStyle = Qt::DotLine;
    qreal m_physicsSelectionLineWidth = 2.0;
    QColor m_physicsSelectionColor { 230, 140, 40 };
    QVector<Watch> m_watches;
    bool m_debugView = true;
    bool m_showBodyAxes = true;
    qreal m_bodyAxisLength = 40.0;
    qreal m_bodyAxisWidth = 2.0;
    QColor m_bodyAxisXColor { 220, 50, 50 };   // red, conventionally X
    QColor m_bodyAxisYColor { 40, 160, 60 };   // green, conventionally Y
    int m_sleepShiftPercent = 25;
    int m_maxPolygonVertices = 8;
    bool m_simulationRunning = false;

    SnapPoint m_snapPoint = SnapPoint::Position;
    qreal m_snapStep = 20.0;
    qreal m_snapSensitivity = 5.0;

    QColor m_defaultBorderColor { 100, 170, 220, 204 };
    qreal m_defaultBorderWidth = 2.0;
    QColor m_defaultBodyColor { 173, 216, 230, 128 };

    Qt::PenStyle m_selectionLineStyle = Qt::DotLine;
    qreal m_selectionLineWidth = 2.0;
    QColor m_selectionColor { 230, 140, 40 };

    HandleShape m_handleShape = HandleShape::Square;
    qreal m_handleSize = 8.0;
    QColor m_handleColor { 222, 184, 135 };
    qreal m_handleBorderWidth = 1.0;
    QColor m_handleBorderColor { 64, 64, 64 };

    qreal m_currentScale = 100.0;
    qreal m_scaleMin = 10.0;
    qreal m_scaleMax = 500.0;
    qreal m_scaleStep = 10.0;

    bool m_polygonDrawing = false;
    QPolygonF m_polygonScenePoints;
    QPointF m_polygonCursorScenePos;
};
