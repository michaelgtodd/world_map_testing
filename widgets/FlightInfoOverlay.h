#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QString>

#include "../Waypoint.h"

class FlightInfoOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit FlightInfoOverlay(QWidget* parent = nullptr)
        : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setStyleSheet("background: transparent;");

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        QString infoStyle =
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 180);"
            "  color: #00ff88;"
            "  font-family: 'Consolas', 'Menlo', monospace;"
            "  font-size: 12px;"
            "  padding: 5px 8px;"
            "  border-radius: 3px;"
            "}";

        instructionLabel_ = new QLabel("Right-click to add waypoints");
        instructionLabel_->setStyleSheet(infoStyle);
        layout->addWidget(instructionLabel_);

        lastWpLabel_ = new QLabel("");
        lastWpLabel_->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 180);"
            "  color: #ffcc00;"
            "  font-family: 'Consolas', 'Menlo', monospace;"
            "  font-size: 12px;"
            "  padding: 5px 8px;"
            "  border-radius: 3px;"
            "  border: 1px solid rgba(255, 204, 0, 80);"
            "}");
        lastWpLabel_->setVisible(false);
        layout->addWidget(lastWpLabel_);

        adjustSize();
    }

    void showWaypoint(const Waypoint& wp)
    {
        if (wp.type == WPType::FlyTo)
        {
            lastWpLabel_->setText(QString("WP%1  %2%3 %4%5  %6ft %7  %8kts")
                .arg(wp.index, 2, 10, QChar('0'))
                .arg(QString::number(std::abs(wp.lat), 'f', 4))
                .arg(wp.lat >= 0 ? "N" : "S")
                .arg(QString::number(std::abs(wp.lon), 'f', 4))
                .arg(wp.lon >= 0 ? "E" : "W")
                .arg(QString::number(wp.flightAlt, 'f', 0))
                .arg(wp.isMSL ? "MSL" : "AGL")
                .arg(QString::number(wp.speedKnots, 'f', 0)));
        }
        else
        {
            lastWpLabel_->setText(QString("WP%1  APCH %2%3  IBD %4%5  20ft AGL  %6kts")
                .arg(wp.index, 2, 10, QChar('0'))
                .arg(QString::number(std::abs(wp.lat), 'f', 4))
                .arg(wp.lat >= 0 ? "N" : "S")
                .arg(QString::number(wp.inboundBearing, 'f', 0))
                .arg(QString::fromUtf8("\xC2\xB0"))
                .arg(QString::number(wp.speedKnots, 'f', 0)));
        }
        lastWpLabel_->setVisible(true);
        adjustSize();
    }

    void positionOver(const QPoint& topLeft)
    {
        move(topLeft.x() + 12, topLeft.y() + 12);
    }

private:
    QLabel* instructionLabel_;
    QLabel* lastWpLabel_;
};
