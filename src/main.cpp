/**
 * Rocky + Qt Advanced Docking System Demo
 *
 * Right-click: add FlyTo waypoints or Approach-to-Hover goals.
 * In Approach mode, click-and-drag to set hover point + inbound bearing.
 * A 3-degree glideslope is computed from the previous waypoint's altitude
 * down to 20 ft AGL at the hover point.
 */

#include <rocky/rocky.h>
#include <rocky/vsg/DisplayManager.h>

#include <fstream>

#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStatusBar>
#include <QMenuBar>
#include <QTimer>
#include <QHeaderView>

#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>

#include "RockyViewer.h"
#include "Waypoint.h"
#include "Geodesy.h"
#include "FlightPlan.h"
#include "WaypointHandler.h"
#include "widgets/EarthViewPane.h"

using namespace ROCKY_NAMESPACE;

int main(int argc, char** argv)
{
    try {

    QApplication qtApp(argc, argv);
    qtApp.setApplicationName("Rocky Flight Planner");

    auto viewer = RockyQtViewer::create();
    rocky::Application app(viewer, argc, argv);
    app.renderContinuously = true;
    viewer->frame = [&app]() { return app.frame(); };
    viewer->continuousUpdate = true;
    viewer->setInterval(8);
    app.vsgcontext->devicePixelRatio = []() { return 1.0; };
    rocky::Log()->set_level(rocky::log::level::info);

    // Load map configuration from JSON file
    {
        std::string mapFile = "data/default.map.json";
        // Check relative to executable, then relative to source dir
        for (auto& candidate : { mapFile, std::string(PROJECT_SOURCE_DIR "/data/default.map.json") })
        {
            std::ifstream f(candidate);
            if (f.good())
            {
                std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                auto r = app.mapNode->from_json(json, app.vsgcontext->io);
                if (r.failed())
                    fprintf(stderr, "Warning: failed to load map file %s\n", candidate.c_str());
                else
                    break;
            }
        }
    }

    // Sky and atmosphere
    if (app.skyNode)
    {
        app.skyNode->setShowAtmosphere(true);
        app.skyNode->setDateTime(rocky::DateTime(2026, 6, 21, 16.0)); // summer afternoon
        if (app.skyNode->ambient)
            app.skyNode->ambient->color.set(0.06f, 0.06f, 0.08f);
    }

    // Stack-allocated shared state
    FlightSettings settings;
    WaypointQueue wpQueue;

    auto rightClickHandler = RightClickHandler::create();
    rightClickHandler->app = &app;
    rightClickHandler->settings = &settings;
    rightClickHandler->wpQueue = &wpQueue;
    app.viewer->getEventHandlers().push_back(rightClickHandler);

    FlightPlan flightPlan(settings);

    QMainWindow mainWindow;
    mainWindow.setWindowTitle("Rocky Flight Planner");
    mainWindow.resize(1600, 900);

    auto* menuBar = mainWindow.menuBar();
    auto* fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("E&xit", &qtApp, &QApplication::quit);
    auto* viewMenu = menuBar->addMenu("&View");
    mainWindow.statusBar()->showMessage("Right-click to add waypoints | Approach mode: click+drag for bearing");

    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    auto* dockManager = new ads::CDockManager(&mainWindow);

    auto* earthPane = new EarthViewPane(app, settings);
    auto* rockyDock = new ads::CDockWidget(dockManager, "Earth View");
    rockyDock->setWidget(earthPane);
    rockyDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    dockManager->addDockWidget(ads::CenterDockWidgetArea, rockyDock);

    // Rebind Ctrl+Left-drag to rotate
    vsg::ref_ptr<MapManipulator> manip;
    for (auto& handler : app.viewer->getEventHandlers())
    {
        manip = vsg::ref_ptr<MapManipulator>(dynamic_cast<MapManipulator*>(handler.get()));
        if (manip) break;
    }
    if (manip)
    {
        earthPane->setManipulator(manip);
    }

    // Connect nav widget to manipulator
    if (manip)
    {
        auto manipRef = manip;
        auto* nav = earthPane->navWidget();
        QObject::connect(nav, &NavWidget::rotateRequested, [manipRef, &app](double dx, double dy) {
            manipRef->rotate(dx, dy);
            app.vsgcontext->requestFrame();
        });
        QObject::connect(nav, &NavWidget::zoomRequested, [manipRef, &app](double dx, double dy) {
            double scale = 1.0 + dy;
            manipRef->setDistance(manipRef->distance() * scale);
            app.vsgcontext->requestFrame();
        });
        QObject::connect(nav, &NavWidget::homeRequested, [manipRef, &app]() {
            manipRef->home();
            app.vsgcontext->requestFrame();
        });
    }

    auto* wpTree = new QTreeWidget();
    wpTree->setHeaderLabels({"WP", "Type", "Lat", "Lon", "Alt", "Spd"});
    wpTree->setAlternatingRowColors(true);
    wpTree->setRootIsDecorated(false);
    wpTree->header()->setStretchLastSection(true);
    wpTree->setStyleSheet("QTreeWidget { font-family: monospace; font-size: 11px; }");

    auto* wpDock = new ads::CDockWidget(dockManager, "Waypoints");
    wpDock->setWidget(wpTree);
    dockManager->addDockWidget(ads::RightDockWidgetArea, wpDock);

    auto* planText = new QTextEdit();
    planText->setReadOnly(true);
    planText->setStyleSheet(
        "QTextEdit {"
        "  background-color: #1a1a2e; color: #e0e0e0;"
        "  font-family: 'Consolas', 'Menlo', monospace; font-size: 12px;"
        "}");
    planText->setPlainText("(empty flight plan)");

    auto* planDock = new ads::CDockWidget(dockManager, "Flight Plan");
    planDock->setWidget(planText);
    dockManager->addDockWidget(ads::BottomDockWidgetArea, planDock);

    wpDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    wpTree->setMinimumWidth(280);
    wpTree->setMaximumWidth(420);

    viewMenu->addAction(rockyDock->toggleViewAction());
    viewMenu->addAction(wpDock->toggleViewAction());
    viewMenu->addAction(planDock->toggleViewAction());

    // Clear button
    QObject::connect(earthPane->settingsPanel()->clearButton(), &QPushButton::clicked, [&]() {
        flightPlan.clear(app);
        wpTree->clear();
        planText->setPlainText("(empty flight plan)");
        mainWindow.statusBar()->showMessage("Flight plan cleared");
    });

    fileMenu->addSeparator();
    fileMenu->addAction("&Clear Flight Plan", [&]() {
        earthPane->settingsPanel()->clearButton()->click();
    });

    // Poll timer - pops from WaypointQueue, adds to FlightPlan
    QTimer pollTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, [&]() {
        Waypoint wp;
        bool changed = false;

        while (wpQueue.pop(wp))
        {
            flightPlan.addWaypoint(wp);
            changed = true;

            QString typeStr = (wp.type == WPType::FlyTo) ? "FLY" : "APCH";
            auto latDir = wp.lat >= 0 ? "N" : "S";
            auto lonDir = wp.lon >= 0 ? "E" : "W";

            QString altStr;
            if (wp.type == WPType::ApproachToHover)
                altStr = QString("20 AGL");
            else
                altStr = QString("%1 %2").arg(QString::number(wp.flightAlt, 'f', 0)).arg(wp.isMSL ? "M" : "A");

            auto* item = new QTreeWidgetItem({
                QString("WP%1").arg(wp.index, 2, 10, QChar('0')),
                typeStr,
                QString("%1%2").arg(QString::number(std::abs(wp.lat), 'f', 4)).arg(latDir),
                QString("%1%2").arg(QString::number(std::abs(wp.lon), 'f', 4)).arg(lonDir),
                altStr,
                QString::number(wp.speedKnots, 'f', 0)
            });
            wpTree->addTopLevelItem(item);
            wpTree->scrollToBottom();

            earthPane->infoOverlay()->showWaypoint(wp);

            mainWindow.statusBar()->showMessage(
                QString("WP%1 added (%2) - %3 waypoints in plan")
                    .arg(wp.index, 2, 10, QChar('0'))
                    .arg(typeStr)
                    .arg(flightPlan.waypointCount()));
        }

        if (changed)
        {
            planText->setPlainText(flightPlan.formatText());
            flightPlan.updateRouteLine(app);
        }
    });
    pollTimer.start(50);

    mainWindow.show();
    return qtApp.exec();

    } catch (const vsg::Exception& e) {
        fprintf(stderr, "vsg::Exception: %s (VkResult=%d)\n", e.message.c_str(), e.result);
        return 1;
    } catch (const std::exception& e) {
        fprintf(stderr, "std::exception: %s\n", e.what());
        return 1;
    } catch (...) {
        fprintf(stderr, "Unknown exception caught\n");
        return 1;
    }
}
