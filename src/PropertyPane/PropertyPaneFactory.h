#pragma once

#include <QObject>
#include <QHash>
#include <typeindex>

class ShapeItem;
class CanvasScene;
class PropertyPane;

class PropertyPaneFactory : public QObject
{
    Q_OBJECT

public:
    explicit PropertyPaneFactory(QObject *parent);

    PropertyPane *paneFor(ShapeItem *item);
    PropertyPane *paneForField(CanvasScene *scene);
    PropertyPane *paneForPhysics(CanvasScene *scene);
    PropertyPane *paneForJoints(CanvasScene *scene);
    PropertyPane *paneForExplosion(CanvasScene *scene);
    PropertyPane *paneForSensor(CanvasScene *scene);
    PropertyPane *paneForRay(CanvasScene *scene);

private:
    QHash<std::type_index, PropertyPane *> m_shapePanes;
    PropertyPane *m_fieldPane = nullptr;
    PropertyPane *m_physicsPane = nullptr;
    PropertyPane *m_jointsPane = nullptr;
    PropertyPane *m_explosionPane = nullptr;
    PropertyPane *m_sensorPane = nullptr;
    PropertyPane *m_rayPane = nullptr;
};
