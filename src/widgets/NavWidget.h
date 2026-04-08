#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QPushButton>

class NavWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NavWidget(QWidget* parent = nullptr)
        : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFixedSize(120, 160);

        QString btnStyle =
            "QPushButton {"
            "  background-color: rgba(10, 15, 30, 200);"
            "  color: #88bbdd;"
            "  border: 1px solid rgba(60, 140, 255, 80);"
            "  font-family: 'Consolas', 'Menlo', monospace;"
            "  font-size: 13px;"
            "  font-weight: bold;"
            "  min-width: 30px; min-height: 30px;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(30, 50, 80, 220);"
            "  color: #aaddff;"
            "}"
            "QPushButton:pressed {"
            "  background-color: rgba(50, 80, 120, 240);"
            "}";
        setStyleSheet(btnStyle);

        auto* grid = new QGridLayout(this);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setSpacing(2);

        // Row 0: rotate up
        auto* rotUp = new QPushButton(QString::fromUtf8("\xE2\x96\xB2")); // triangle up
        rotUp->setToolTip("Tilt up");
        grid->addWidget(rotUp, 0, 1);

        // Row 1: rotate left, home, rotate right
        auto* rotLeft = new QPushButton(QString::fromUtf8("\xE2\x97\x80")); // triangle left
        rotLeft->setToolTip("Rotate left");
        grid->addWidget(rotLeft, 1, 0);

        auto* homeBtn = new QPushButton(QString::fromUtf8("\xE2\x97\x8F")); // circle
        homeBtn->setToolTip("Reset view");
        homeBtn->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(10, 15, 30, 200);"
            "  color: #44aaff;"
            "  border: 1px solid rgba(60, 140, 255, 80);"
            "  border-radius: 15px;"
            "  font-size: 14px;"
            "  min-width: 30px; min-height: 30px;"
            "}"
            "QPushButton:hover { background-color: rgba(30, 50, 80, 220); }");
        grid->addWidget(homeBtn, 1, 1);

        auto* rotRight = new QPushButton(QString::fromUtf8("\xE2\x96\xB6")); // triangle right
        rotRight->setToolTip("Rotate right");
        grid->addWidget(rotRight, 1, 2);

        // Row 2: rotate down
        auto* rotDown = new QPushButton(QString::fromUtf8("\xE2\x96\xBC")); // triangle down
        rotDown->setToolTip("Tilt down");
        grid->addWidget(rotDown, 2, 1);

        // Row 3: zoom in / zoom out
        auto* zoomIn = new QPushButton("+");
        zoomIn->setToolTip("Zoom in");
        zoomIn->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(10, 15, 30, 200);"
            "  color: #66dd88;"
            "  border: 1px solid rgba(60, 200, 100, 80);"
            "  font-size: 18px; font-weight: bold;"
            "  min-width: 30px; min-height: 30px;"
            "}"
            "QPushButton:hover { background-color: rgba(20, 40, 30, 220); }");
        grid->addWidget(zoomIn, 3, 0);

        auto* zoomOut = new QPushButton(QString::fromUtf8("\xE2\x80\x93")); // en-dash
        zoomOut->setToolTip("Zoom out");
        zoomOut->setStyleSheet(
            "QPushButton {"
            "  background-color: rgba(10, 15, 30, 200);"
            "  color: #dd8866;"
            "  border: 1px solid rgba(200, 100, 60, 80);"
            "  font-size: 18px; font-weight: bold;"
            "  min-width: 30px; min-height: 30px;"
            "}"
            "QPushButton:hover { background-color: rgba(40, 20, 10, 220); }");
        grid->addWidget(zoomOut, 3, 2);

        connect(rotUp,    &QPushButton::clicked, this, [this]() { emit rotateRequested(0, -0.05); });
        connect(rotDown,  &QPushButton::clicked, this, [this]() { emit rotateRequested(0,  0.05); });
        connect(rotLeft,  &QPushButton::clicked, this, [this]() { emit rotateRequested(-0.05, 0); });
        connect(rotRight, &QPushButton::clicked, this, [this]() { emit rotateRequested( 0.05, 0); });
        connect(zoomIn,   &QPushButton::clicked, this, [this]() { emit zoomRequested(0, -0.2); });
        connect(zoomOut,  &QPushButton::clicked, this, [this]() { emit zoomRequested(0,  0.2); });
        connect(homeBtn,  &QPushButton::clicked, this, [this]() { emit homeRequested(); });
    }

    void positionOver(const QPoint& topLeft, int yOffset)
    {
        move(topLeft.x() + 12, topLeft.y() + yOffset);
    }

signals:
    void rotateRequested(double dx, double dy);
    void zoomRequested(double dx, double dy);
    void homeRequested();
};
