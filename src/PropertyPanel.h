#pragma once

#include <QWidget>
#include <QHash>
#include <QPointer>

class QTableWidgetItem;
#include <QVariant>
#include <vector>

#include "PropertyPane/PropertyPane.h"
#include "EditorMode.h"

class QTableWidget;
class QTabWidget;
class QLabel;
class QToolButton;
class ShapeItem;
class CanvasScene;
class PropertyPaneFactory;

class PropertyPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertyPanel(QWidget *parent = nullptr);

    void setScene(CanvasScene *scene);

public slots:
    void setActiveItem(ShapeItem *item);
    void setEditorMode(EditorMode mode);
    void showSection(const QString &section);
    void refreshValues();

signals:
    void fieldSettingsChanged();

private:
    struct Row {
        PropertyFieldType type;
        QWidget *editor;
        std::function<QVariant()> getter;
        QLabel *nameLabel = nullptr;
        QToolButton *resetButton = nullptr;
        QVariant defaultValue;
        // What the log needs to name this property later.
        QString key;
        QString section;
        QString label;
    };

    void setActivePane(PropertyPane *pane);
    void updateActivePane();
    void updateEditable();
    // Turns a row's setter call into an undo step; see the wrapper in addRow().
    void reportEdit(const QString &label);
    void *subject() const;
    // Which object a row belongs to. A physics table carries the body's rows
    // and the shape's at once, so the section decides, not the selection.
    QString objectNameFor(const QString &section) const;
    void showRowMenu(const Row &row, const QPoint &globalPos);
    // Paints a logged row's name in the log colour.
    void updateWatchMarks();
    void setTitle(const QString &subject);
    void rebuildRows();
    void updateModifiedMarks();
    void addRow(QTableWidget *table, const PropertyRow &row);
    void addGroupHeader(QTableWidget *table, const QString &title);
    QTableWidget *tableForSection(const QString &section);
    void fitNameColumn(QTableWidget *table);

    QTabWidget *m_tabs = nullptr;
    QHash<QString, QTableWidget *> m_sectionTables;
    QLabel *m_title = nullptr;
    EditorMode m_mode = EditorMode::Edit;
    QPointer<ShapeItem> m_item;
    CanvasScene *m_scene = nullptr;
    PropertyPaneFactory *m_factory;
    PropertyPane *m_activePane = nullptr;
    std::vector<Row> m_rows;
    bool m_updating = false; // guards against editor->setter->refresh feedback loops
};
