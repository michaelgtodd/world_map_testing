#include "EarthViewPane.h"

#include <QVBoxLayout>
#include <QEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QShowEvent>
#include <QHideEvent>

#include <rocky/rocky.h>
#include <rocky/vsg/DisplayManager.h>
#include <vsgQt/Window.h>

using namespace ROCKY_NAMESPACE;

EarthViewPane::EarthViewPane(rocky::Application& app, FlightSettings& settings, QWidget* parent)
    : QWidget(parent), app_(app)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* rockyWindow = new vsgQt::Window();
    rockyWidget_ = QWidget::createWindowContainer(rockyWindow);
    rockyWidget_->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(rockyWidget_);

    rockyWindow->initializeWindow();
    app_.display.addWindow(rockyWindow->windowAdapter);

    settingsPanel_ = new FlightSettingsPanel(settings);
    infoOverlay_ = new FlightInfoOverlay();
    navWidget_ = new NavWidget();
    settingsPanel_->show();
    infoOverlay_->show();
    navWidget_->show();
}

void EarthViewPane::installAncestorFilters()
{
    QWidget* w = parentWidget();
    while (w)
    {
        w->installEventFilter(this);
        w = w->parentWidget();
    }
}

bool EarthViewPane::eventFilter(QObject* obj, QEvent* event)
{
    auto type = event->type();
    if (type == QEvent::Move || type == QEvent::Resize ||
        type == QEvent::WindowStateChange || type == QEvent::LayoutRequest)
    {
        repositionOverlays();
    }
    return QWidget::eventFilter(obj, event);
}

void EarthViewPane::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    repositionOverlays();
}

void EarthViewPane::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    repositionOverlays();
}

void EarthViewPane::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    installAncestorFilters();
    repositionOverlays();
    if (settingsPanel_) settingsPanel_->show();
    if (infoOverlay_) infoOverlay_->show();
    if (navWidget_) navWidget_->show();
}

void EarthViewPane::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (settingsPanel_) settingsPanel_->hide();
    if (infoOverlay_) infoOverlay_->hide();
    if (navWidget_) navWidget_->hide();
}

void EarthViewPane::repositionOverlays()
{
    if (!rockyWidget_) return;
    QPoint tl = rockyWidget_->mapToGlobal(QPoint(0, 0));
    QPoint tr = rockyWidget_->mapToGlobal(QPoint(rockyWidget_->width(), 0));
    if (settingsPanel_) settingsPanel_->positionOver(tr);
    if (infoOverlay_) infoOverlay_->positionOver(tl);
    if (navWidget_) navWidget_->positionOver(tl, 80);
}
