/**
 * Rocky + Qt Advanced Docking System Demo
 *
 * Embeds a Rocky Vulkan earth view inside a Qt Advanced Docking System pane,
 * with Qt widgets overlaid on the Rocky view.
 *
 * Right-click on the earth to add waypoints to a flight plan.
 * Configure altitude (MSL/AGL) and speed (knots) before clicking.
 */

#include <rocky/rocky.h>
#include <rocky/vsg/DisplayManager.h>

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QComboBox>
#include <QGroupBox>
#include <QStatusBar>
#include <QMenuBar>
#include <QTimer>
#include <QHeaderView>

#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>

#include <vsgQt/Window.h>

#include <mutex>
#include <deque>
#include <cmath>

using namespace ROCKY_NAMESPACE;

// ── Data structures ─────────────────────────────────────────────────

struct Waypoint
{
    int index;
    double lon, lat;
    double terrainAlt;     // ground elevation at click point (meters)
    double flightAlt;      // pilot-entered altitude value
    bool isMSL;            // true=MSL, false=AGL
    double speedKnots;
    entt::entity pointEntity = entt::null;
};

// Thread-safe queue to pass click data from VSG to Qt
class WaypointQueue
{
public:
    void push(const Waypoint& w)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(w);
    }
    bool pop(Waypoint& w)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        w = queue_.front();
        queue_.pop_front();
        return true;
    }
private:
    std::mutex mutex_;
    std::deque<Waypoint> queue_;
};

static WaypointQueue g_wpQueue;

// Shared settings the event handler reads (written by Qt thread)
struct FlightSettings
{
    std::atomic<double> altitude{5000.0};
    std::atomic<bool> isMSL{true};
    std::atomic<double> speedKnots{250.0};
    std::atomic<int> nextIndex{1};
};

static FlightSettings g_settings;

// ── VSG right-click handler ─────────────────────────────────────────

class RightClickHandler : public vsg::Inherit<vsg::Visitor, RightClickHandler>
{
public:
    rocky::Application* app = nullptr;

    void apply(vsg::ButtonReleaseEvent& event) override
    {
        if (event.button != 3 || !app)
            return;

        auto result = rocky::pointAtWindowCoords(
            vsg::ref_ptr<vsg::Viewer>(app->viewer.get()),
            event.x, event.y);

        if (!result.ok())
            return;

        auto wgs84 = result.value().point.transform(SRS::WGS84);
        double lon = wgs84.x, lat = wgs84.y, terrainAlt = wgs84.z;
        double flightAlt = g_settings.altitude.load();
        bool isMSL = g_settings.isMSL.load();
        double speed = g_settings.speedKnots.load();
        int idx = g_settings.nextIndex.fetch_add(1);

        // Compute display altitude in meters MSL for the point marker
        double markerAltMSL = isMSL ? flightAlt * 0.3048 : terrainAlt + flightAlt * 0.3048;

        // Create a point marker on the globe
        entt::entity entity = entt::null;
        app->registry.write([&](entt::registry& r)
        {
            entity = r.create();

            auto& geom = r.emplace<PointGeometry>(entity);
            geom.srs = SRS::WGS84;
            geom.points.emplace_back(lon, lat, markerAltMSL);

            auto& style = r.emplace<PointStyle>(entity);
            style.color = Color(0.2f, 0.6f, 1.0f, 1.0f); // blue
            style.width = 10.0f;
            style.antialias = 0.5f;
            style.depthOffset = 10000.0f;

            r.emplace<Point>(entity, geom, style);
            app->vsgcontext->requestFrame();
        });

        g_wpQueue.push({ idx, lon, lat, terrainAlt, flightAlt, isMSL, speed, entity });
    }
};

// ── Rocky Qt viewer ─────────────────────────────────────────────────

class RockyQtViewer : public vsg::Inherit<vsgQt::Viewer, RockyQtViewer>
{
public:
    std::function<bool()> frame;
    void render(double) override
    {
        if (continuousUpdate || requests.load() > 0)
            if (!frame())
                if (status->cancel())
                    QApplication::quit();
    }
};

// ── Overlay widget on the Rocky view ────────────────────────────────

class RockyOverlayWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RockyOverlayWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setStyleSheet("background: transparent;");

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        QString labelStyle =
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 180);"
            "  color: #00ff88;"
            "  font-family: monospace;"
            "  font-size: 13px;"
            "  padding: 6px 10px;"
            "  border-radius: 4px;"
            "}";

        instructionLabel_ = new QLabel("Right-click on the earth to add waypoints");
        instructionLabel_->setStyleSheet(labelStyle);
        layout->addWidget(instructionLabel_);

        lastWpLabel_ = new QLabel("");
        lastWpLabel_->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 180);"
            "  color: #ffcc00;"
            "  font-family: monospace;"
            "  font-size: 13px;"
            "  font-weight: bold;"
            "  padding: 8px 12px;"
            "  border-radius: 4px;"
            "  border: 1px solid rgba(255, 204, 0, 100);"
            "}");
        lastWpLabel_->setVisible(false);
        layout->addWidget(lastWpLabel_);
        layout->addStretch();
    }

    void showWaypoint(const Waypoint& wp)
    {
        lastWpLabel_->setText(QString("WP%1: %2%3 %4%5  %6ft %7  %8 kts")
            .arg(wp.index)
            .arg(QString::number(std::abs(wp.lat), 'f', 4))
            .arg(wp.lat >= 0 ? "N" : "S")
            .arg(QString::number(std::abs(wp.lon), 'f', 4))
            .arg(wp.lon >= 0 ? "E" : "W")
            .arg(QString::number(wp.flightAlt, 'f', 0))
            .arg(wp.isMSL ? "MSL" : "AGL")
            .arg(QString::number(wp.speedKnots, 'f', 0)));
        lastWpLabel_->setVisible(true);
    }

private:
    QLabel* instructionLabel_;
    QLabel* lastWpLabel_;
};

// ── Rocky dock content ──────────────────────────────────────────────

class RockyDockContent : public QWidget
{
    Q_OBJECT
public:
    RockyDockContent(rocky::Application& app, QWidget* parent = nullptr)
        : QWidget(parent), app_(app)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        rockyWindow_ = new vsgQt::Window();
        rockyWidget_ = QWidget::createWindowContainer(rockyWindow_);
        layout->addWidget(rockyWidget_);

        rockyWindow_->initializeWindow();
        app_.display.addWindow(rockyWindow_->windowAdapter);

        overlay_ = new RockyOverlayWidget(rockyWidget_);
        overlay_->raise();
    }

    RockyOverlayWidget* overlay() { return overlay_; }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (overlay_ && rockyWidget_)
            overlay_->setGeometry(rockyWidget_->rect());
    }

private:
    rocky::Application& app_;
    vsgQt::Window* rockyWindow_ = nullptr;
    QWidget* rockyWidget_ = nullptr;
    RockyOverlayWidget* overlay_ = nullptr;
};

// ── Helpers ─────────────────────────────────────────────────────────

static double haversineNm(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double R = 3440.065; // earth radius in nautical miles
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * M_PI / 180.0) * std::cos(lat2 * M_PI / 180.0) *
               std::sin(dLon / 2) * std::sin(dLon / 2);
    return R * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

static double bearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double la1 = lat1 * M_PI / 180.0, la2 = lat2 * M_PI / 180.0;
    double y = std::sin(dLon) * std::cos(la2);
    double x = std::cos(la1) * std::sin(la2) - std::sin(la1) * std::cos(la2) * std::cos(dLon);
    double brg = std::atan2(y, x) * 180.0 / M_PI;
    return std::fmod(brg + 360.0, 360.0);
}

// Format the flight plan as a readable text block
static QString formatFlightPlan(const std::vector<Waypoint>& wps)
{
    if (wps.empty()) return "(empty flight plan)";

    QString text;
    text += QString("FLIGHT PLAN  (%1 waypoint%2)\n")
        .arg(wps.size()).arg(wps.size() > 1 ? "s" : "");
    text += QString("").fill('=', 72) + "\n\n";

    double totalDist = 0;
    double totalTime = 0; // hours

    for (size_t i = 0; i < wps.size(); i++)
    {
        auto& wp = wps[i];
        auto latDir = wp.lat >= 0 ? "N" : "S";
        auto lonDir = wp.lon >= 0 ? "E" : "W";

        text += QString("  WP%1  %2%3 %4  %5%6 %7\n")
            .arg(wp.index, 2, 10, QChar('0'))
            .arg(QString::number(std::abs(wp.lat), 'f', 5)).arg(latDir)
            .arg(QString::fromUtf8("\xC2\xB0"))
            .arg(QString::number(std::abs(wp.lon), 'f', 5)).arg(lonDir)
            .arg(QString::fromUtf8("\xC2\xB0"));

        text += QString("        ALT %1 ft %2   SPD %3 kts\n")
            .arg(QString::number(wp.flightAlt, 'f', 0))
            .arg(wp.isMSL ? "MSL" : "AGL")
            .arg(QString::number(wp.speedKnots, 'f', 0));

        if (i > 0)
        {
            auto& prev = wps[i - 1];
            double dist = haversineNm(prev.lat, prev.lon, wp.lat, wp.lon);
            double brg = bearingDeg(prev.lat, prev.lon, wp.lat, wp.lon);
            double legTime = (wp.speedKnots > 0) ? dist / wp.speedKnots : 0;
            totalDist += dist;
            totalTime += legTime;

            int mins = static_cast<int>(legTime * 60.0 + 0.5);

            text += QString("        LEG  %1 nm  BRG %2%3  ETE %4 min\n")
                .arg(QString::number(dist, 'f', 1))
                .arg(QString::number(brg, 'f', 0))
                .arg(QString::fromUtf8("\xC2\xB0"))
                .arg(mins);
        }
        text += "\n";
    }

    text += QString("").fill('-', 72) + "\n";
    int totalMins = static_cast<int>(totalTime * 60.0 + 0.5);
    text += QString("  TOTAL DISTANCE: %1 nm\n").arg(QString::number(totalDist, 'f', 1));
    text += QString("  TOTAL ETE:      %1h %2m\n")
        .arg(totalMins / 60).arg(totalMins % 60, 2, 10, QChar('0'));
    text += QString("  WAYPOINTS:      %1\n").arg(wps.size());

    return text;
}

// ── main ────────────────────────────────────────────────────────────

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

    if (app.mapNode->map->layers().empty())
    {
        auto elev = rocky::TMSElevationLayer::create();
        elev->uri = "https://readymap.org/readymap/tiles/1.0.0/116/";
        app.mapNode->map->add(elev);

        auto img = rocky::TMSImageLayer::create();
        img->uri = "https://readymap.org/readymap/tiles/1.0.0/7";
        app.mapNode->map->add(img);
    }

    auto rightClickHandler = RightClickHandler::create();
    rightClickHandler->app = &app;
    app.viewer->getEventHandlers().push_back(rightClickHandler);

    // ── Qt UI ───────────────────────────────────────────────────────

    QMainWindow mainWindow;
    mainWindow.setWindowTitle("Rocky Flight Planner");
    mainWindow.resize(1400, 850);

    auto* menuBar = mainWindow.menuBar();
    auto* fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("E&xit", &qtApp, &QApplication::quit);
    auto* viewMenu = menuBar->addMenu("&View");
    mainWindow.statusBar()->showMessage("Right-click on the earth to add waypoints");

    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    auto* dockManager = new ads::CDockManager(&mainWindow);

    // ── Earth View (center) ─────────────────────────────────────────

    auto* rockyContent = new RockyDockContent(app);
    auto* rockyDock = new ads::CDockWidget(dockManager, "Earth View");
    rockyDock->setWidget(rockyContent);
    rockyDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    dockManager->addDockWidget(ads::CenterDockWidgetArea, rockyDock);

    // ── Flight Settings (left) ──────────────────────────────────────

    auto* settingsWidget = new QWidget();
    auto* settingsLayout = new QVBoxLayout(settingsWidget);

    auto* altGroup = new QGroupBox("Waypoint Altitude");
    auto* altForm = new QFormLayout(altGroup);

    auto* altSpin = new QDoubleSpinBox();
    altSpin->setRange(0, 60000);
    altSpin->setValue(5000);
    altSpin->setSuffix(" ft");
    altSpin->setDecimals(0);
    altSpin->setSingleStep(500);
    altForm->addRow("Altitude:", altSpin);

    auto* altTypeCombo = new QComboBox();
    altTypeCombo->addItems({"MSL (Mean Sea Level)", "AGL (Above Ground Level)"});
    altForm->addRow("Reference:", altTypeCombo);

    settingsLayout->addWidget(altGroup);

    auto* spdGroup = new QGroupBox("Speed");
    auto* spdForm = new QFormLayout(spdGroup);

    auto* speedSpin = new QDoubleSpinBox();
    speedSpin->setRange(10, 2000);
    speedSpin->setValue(250);
    speedSpin->setSuffix(" kts");
    speedSpin->setDecimals(0);
    speedSpin->setSingleStep(10);
    spdForm->addRow("Speed:", speedSpin);

    settingsLayout->addWidget(spdGroup);

    auto* clearBtn = new QPushButton("Clear Flight Plan");
    settingsLayout->addWidget(clearBtn);
    settingsLayout->addStretch();

    auto* settingsDock = new ads::CDockWidget(dockManager, "Flight Settings");
    settingsDock->setWidget(settingsWidget);
    settingsDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    dockManager->addDockWidget(ads::LeftDockWidgetArea, settingsDock);

    // Sync spinboxes to global settings
    QObject::connect(altSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        [](double v) { g_settings.altitude.store(v); });
    QObject::connect(altTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        [](int i) { g_settings.isMSL.store(i == 0); });
    QObject::connect(speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
        [](double v) { g_settings.speedKnots.store(v); });

    // ── Waypoint Table (right) ──────────────────────────────────────

    auto* wpTree = new QTreeWidget();
    wpTree->setHeaderLabels({"WP", "Lat", "Lon", "Alt (ft)", "Ref", "Speed (kts)"});
    wpTree->setAlternatingRowColors(true);
    wpTree->setRootIsDecorated(false);
    wpTree->header()->setStretchLastSection(true);

    auto* wpDock = new ads::CDockWidget(dockManager, "Waypoints");
    wpDock->setWidget(wpTree);
    dockManager->addDockWidget(ads::RightDockWidgetArea, wpDock);

    // ── Flight Plan Text (bottom) ───────────────────────────────────

    auto* planText = new QTextEdit();
    planText->setReadOnly(true);
    planText->setStyleSheet(
        "QTextEdit {"
        "  background-color: #1a1a2e; color: #e0e0e0;"
        "  font-family: monospace; font-size: 12px;"
        "}");
    planText->setPlainText("(empty flight plan)");

    auto* planDock = new ads::CDockWidget(dockManager, "Flight Plan");
    planDock->setWidget(planText);
    dockManager->addDockWidget(ads::BottomDockWidgetArea, planDock);

    // View menu toggles
    viewMenu->addAction(rockyDock->toggleViewAction());
    viewMenu->addAction(settingsDock->toggleViewAction());
    viewMenu->addAction(wpDock->toggleViewAction());
    viewMenu->addAction(planDock->toggleViewAction());

    // ── Flight plan state ───────────────────────────────────────────

    std::vector<Waypoint> waypoints;
    entt::entity routeLineEntity = entt::null;

    // Lambda to rebuild the route line on the globe
    auto updateRouteLine = [&]()
    {
        app.registry.write([&](entt::registry& r)
        {
            // Destroy old line
            if (routeLineEntity != entt::null && r.valid(routeLineEntity))
                r.destroy(routeLineEntity);
            routeLineEntity = entt::null;

            if (waypoints.size() < 2) return;

            routeLineEntity = r.create();

            auto& geom = r.emplace<LineGeometry>(routeLineEntity);
            geom.topology = LineTopology::Strip;
            geom.srs = SRS::WGS84;

            for (auto& wp : waypoints)
            {
                double altMSL = wp.isMSL
                    ? wp.flightAlt * 0.3048
                    : wp.terrainAlt + wp.flightAlt * 0.3048;
                geom.points.emplace_back(wp.lon, wp.lat, altMSL);
            }

            auto& style = r.emplace<LineStyle>(routeLineEntity);
            style.color = Color(0.2f, 0.8f, 1.0f, 0.9f); // cyan
            style.width = 3.0f;
            style.depthOffset = 10000.0f;
            style.resolution = 1000.0f; // subdivide for great-circle appearance

            r.emplace<Line>(routeLineEntity, geom, style);
            app.vsgcontext->requestFrame();
        });
    };

    // Clear flight plan
    QObject::connect(clearBtn, &QPushButton::clicked, [&]() {
        app.registry.write([&](entt::registry& r) {
            for (auto& wp : waypoints)
                if (wp.pointEntity != entt::null && r.valid(wp.pointEntity))
                    r.destroy(wp.pointEntity);
            if (routeLineEntity != entt::null && r.valid(routeLineEntity))
                r.destroy(routeLineEntity);
            routeLineEntity = entt::null;
            app.vsgcontext->requestFrame();
        });
        waypoints.clear();
        wpTree->clear();
        planText->setPlainText("(empty flight plan)");
        g_settings.nextIndex.store(1);
        mainWindow.statusBar()->showMessage("Flight plan cleared");
    });

    // ── Poll timer ──────────────────────────────────────────────────

    QTimer pollTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, [&]() {
        Waypoint wp;
        bool changed = false;

        while (g_wpQueue.pop(wp))
        {
            waypoints.push_back(wp);
            changed = true;

            auto latDir = wp.lat >= 0 ? "N" : "S";
            auto lonDir = wp.lon >= 0 ? "E" : "W";

            auto* item = new QTreeWidgetItem({
                QString("WP%1").arg(wp.index, 2, 10, QChar('0')),
                QString("%1 %2").arg(QString::number(std::abs(wp.lat), 'f', 5)).arg(latDir),
                QString("%1 %2").arg(QString::number(std::abs(wp.lon), 'f', 5)).arg(lonDir),
                QString::number(wp.flightAlt, 'f', 0),
                wp.isMSL ? "MSL" : "AGL",
                QString::number(wp.speedKnots, 'f', 0)
            });
            wpTree->addTopLevelItem(item);
            wpTree->scrollToBottom();

            rockyContent->overlay()->showWaypoint(wp);

            mainWindow.statusBar()->showMessage(
                QString("WP%1 added - %2 waypoints in plan")
                    .arg(wp.index, 2, 10, QChar('0'))
                    .arg(waypoints.size()));
        }

        if (changed)
        {
            planText->setPlainText(formatFlightPlan(waypoints));
            updateRouteLine();
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

#include "main.moc"
