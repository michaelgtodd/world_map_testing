#pragma once

#include <QWidget>

#include "FlightSettingsPanel.h"
#include "FlightInfoOverlay.h"
#include "NavWidget.h"

namespace rocky { class Application; }
struct FlightSettings;

class EarthViewPane : public QWidget
{
    Q_OBJECT
public:
    EarthViewPane(rocky::Application& app, FlightSettings& settings, QWidget* parent = nullptr);

    FlightSettingsPanel* settingsPanel() { return settingsPanel_; }
    FlightInfoOverlay* infoOverlay() { return infoOverlay_; }
    NavWidget* navWidget() { return navWidget_; }

    void installAncestorFilters();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void repositionOverlays();

    rocky::Application& app_;
    QWidget* rockyWidget_ = nullptr;
    FlightSettingsPanel* settingsPanel_ = nullptr;
    FlightInfoOverlay* infoOverlay_ = nullptr;
    NavWidget* navWidget_ = nullptr;
};
