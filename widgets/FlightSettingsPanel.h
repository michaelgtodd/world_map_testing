#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QComboBox>

#include "../Waypoint.h"

class FlightSettingsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FlightSettingsPanel(FlightSettings& settings, QWidget* parent = nullptr)
        : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool), settings_(settings)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFixedWidth(240);

        setStyleSheet(
            "QWidget#panel {"
            "  background-color: rgba(10, 15, 30, 210);"
            "  border: 1px solid rgba(60, 140, 255, 120);"
            "  border-radius: 6px;"
            "}"
            "QLabel {"
            "  background: transparent;"
            "  color: #8899bb;"
            "  font-family: 'Consolas', 'Menlo', monospace;"
            "  font-size: 11px;"
            "}"
            "QDoubleSpinBox, QComboBox {"
            "  background-color: rgba(20, 25, 45, 230);"
            "  color: #00ccff;"
            "  border: 1px solid rgba(60, 140, 255, 80);"
            "  border-radius: 3px;"
            "  padding: 3px 6px;"
            "  font-family: 'Consolas', 'Menlo', monospace;"
            "  font-size: 12px;"
            "  font-weight: bold;"
            "}"
            "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
            "  width: 16px;"
            "  background: rgba(40, 60, 100, 200);"
            "  border: 1px solid rgba(60, 140, 255, 60);"
            "}"
            "QPushButton {"
            "  background-color: rgba(60, 30, 30, 200);"
            "  color: #ff6666;"
            "  border: 1px solid rgba(255, 80, 80, 100);"
            "  border-radius: 3px;"
            "  padding: 4px 8px;"
            "  font-family: 'Consolas', 'Menlo', monospace;"
            "  font-size: 11px;"
            "}"
            "QPushButton:hover {"
            "  background-color: rgba(100, 40, 40, 220);"
            "}");

        auto* panel = new QWidget(this);
        panel->setObjectName("panel");
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(panel);

        auto* sLayout = new QVBoxLayout(panel);
        sLayout->setContentsMargins(10, 8, 10, 8);
        sLayout->setSpacing(4);

        auto* titleLabel = new QLabel("FLIGHT SETTINGS");
        titleLabel->setStyleSheet(
            "QLabel { color: #4488cc; font-weight: bold; font-size: 11px;"
            "  letter-spacing: 2px; }");
        titleLabel->setAlignment(Qt::AlignCenter);
        sLayout->addWidget(titleLabel);

        auto* sep = new QLabel("");
        sep->setFixedHeight(1);
        sep->setStyleSheet("QLabel { background: rgba(60, 140, 255, 60); }");
        sLayout->addWidget(sep);

        // Mode selector
        sLayout->addWidget(new QLabel("MODE"));
        modeCombo_ = new QComboBox();
        modeCombo_->addItems({"FLY TO", "APPROACH TO HOVER"});
        sLayout->addWidget(modeCombo_);

        sLayout->addSpacing(2);

        // Altitude (for FlyTo mode)
        altLabel_ = new QLabel("ALTITUDE");
        sLayout->addWidget(altLabel_);
        altSpin_ = new QDoubleSpinBox();
        altSpin_->setRange(0, 60000);
        altSpin_->setValue(5000);
        altSpin_->setSuffix(" ft");
        altSpin_->setDecimals(0);
        altSpin_->setSingleStep(500);
        altSpin_->setButtonSymbols(QDoubleSpinBox::PlusMinus);
        sLayout->addWidget(altSpin_);

        altTypeCombo_ = new QComboBox();
        altTypeCombo_->addItems({"MSL", "AGL"});
        sLayout->addWidget(altTypeCombo_);

        // Approach info label (shown in approach mode)
        approachInfo_ = new QLabel("Click + drag on earth\nDrag = inbound bearing\n3° GS to 20ft AGL");
        approachInfo_->setStyleSheet(
            "QLabel { color: #44cc88; font-size: 10px; padding: 4px; }");
        approachInfo_->setWordWrap(true);
        approachInfo_->setVisible(false);
        sLayout->addWidget(approachInfo_);

        sLayout->addSpacing(4);
        sLayout->addWidget(new QLabel("SPEED"));
        speedSpin_ = new QDoubleSpinBox();
        speedSpin_->setRange(10, 2000);
        speedSpin_->setValue(250);
        speedSpin_->setSuffix(" kts");
        speedSpin_->setDecimals(0);
        speedSpin_->setSingleStep(10);
        speedSpin_->setButtonSymbols(QDoubleSpinBox::PlusMinus);
        sLayout->addWidget(speedSpin_);

        sLayout->addSpacing(6);
        clearBtn_ = new QPushButton("CLEAR PLAN");
        sLayout->addWidget(clearBtn_);

        connect(altSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double v) { settings_.altitude.store(v); });
        connect(altTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int i) { settings_.isMSL.store(i == 0); });
        connect(speedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double v) { settings_.speedKnots.store(v); });
        connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int i) {
                settings_.mode.store(i);
                bool isFlyTo = (i == 0);
                altLabel_->setVisible(isFlyTo);
                altSpin_->setVisible(isFlyTo);
                altTypeCombo_->setVisible(isFlyTo);
                approachInfo_->setVisible(!isFlyTo);
                adjustSize();
            });

        adjustSize();
    }

    QPushButton* clearButton() { return clearBtn_; }

    void positionOver(const QPoint& topRight)
    {
        move(topRight.x() - width() - 12, topRight.y() + 12);
    }

private:
    FlightSettings& settings_;
    QComboBox* modeCombo_;
    QLabel* altLabel_;
    QDoubleSpinBox* altSpin_;
    QComboBox* altTypeCombo_;
    QDoubleSpinBox* speedSpin_;
    QPushButton* clearBtn_;
    QLabel* approachInfo_;
};
