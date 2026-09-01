#pragma once

#include <QWidget>

class QGraphicsView;

class RulerWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Orientation { Horizontal, Vertical };

    static constexpr int kThickness = 26;

    // Parent-only, for Designer promotion; the rest is set after setupUi().
    explicit RulerWidget(QWidget *parent = nullptr);
    RulerWidget(Orientation orientation, QGraphicsView *view, QWidget *parent = nullptr);

    void setOrientation(Orientation orientation);
    void setView(QGraphicsView *view);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Orientation m_orientation = Orientation::Horizontal;
    QGraphicsView *m_view = nullptr;
};
