#include "PropertyPaneFactory.h"
#include "PropertyPane.h"
#include "../ShapeItem.h"
#include "../CanvasScene.h"
#include "PhysicsPropertyPane.h"
#include "JointPropertyPane.h"
#include "ExplosionPropertyPane.h"
#include "RayPropertyPane.h"
#include "SensorPropertyPane.h"

PropertyPaneFactory::PropertyPaneFactory(QObject *parent)
    : QObject(parent)
{
}

PropertyPane *PropertyPaneFactory::paneFor(ShapeItem *item)
{
    const std::type_index key(typeid(*item));
    auto it = m_shapePanes.constFind(key);
    PropertyPane *pane;
    if (it == m_shapePanes.constEnd()) {
        pane = item->makePropertyPane();
        pane->setParent(this);
        m_shapePanes.insert(key, pane);
    } else {
        pane = it.value();
    }
    pane->attach(item);
    return pane;
}

PropertyPane *PropertyPaneFactory::paneForPhysics(CanvasScene *scene)
{
    if (!m_physicsPane)
        m_physicsPane = new PhysicsPropertyPane(this);
    m_physicsPane->attach(scene);
    return m_physicsPane;
}

PropertyPane *PropertyPaneFactory::paneForRay(CanvasScene *scene)
{
    if (!m_rayPane)
        m_rayPane = new RayPropertyPane(this);
    m_rayPane->attach(scene);
    return m_rayPane;
}

PropertyPane *PropertyPaneFactory::paneForSensor(CanvasScene *scene)
{
    if (!m_sensorPane)
        m_sensorPane = new SensorPropertyPane(this);
    m_sensorPane->attach(scene);
    return m_sensorPane;
}

PropertyPane *PropertyPaneFactory::paneForExplosion(CanvasScene *scene)
{
    if (!m_explosionPane)
        m_explosionPane = new ExplosionPropertyPane(this);
    m_explosionPane->attach(scene);
    return m_explosionPane;
}

PropertyPane *PropertyPaneFactory::paneForJoints(CanvasScene *scene)
{
    if (!m_jointsPane)
        m_jointsPane = new JointPropertyPane(this);
    m_jointsPane->attach(scene);
    return m_jointsPane;
}

PropertyPane *PropertyPaneFactory::paneForField(CanvasScene *scene)
{
    if (!m_fieldPane) {
        m_fieldPane = scene->makePropertyPane();
        m_fieldPane->setParent(this);
    }
    m_fieldPane->attach(scene);
    return m_fieldPane;
}
