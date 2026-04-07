/**
 * Rocky + Qt Advanced Docking System Demo
 *
 * Embeds a Rocky Vulkan earth view inside a Qt Advanced Docking System pane,
 * with Qt widgets overlaid on the Rocky view.
 *
 * Right-click on the earth to add waypoints to a flight plan.
 * Configure altitude (MSL/AGL) and speed (knots) via the overlay HUD.
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
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QComboBox>
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
    double flightAlt;      // pilot-entered altitude value (feet)
    bool isMSL;            // true=MSL, false=AGL
    double speedKnots;
    entt::entity pointEntity = entt::null;
};

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

struct FlightSettings
{
    std::atomic<double> altitude{5000.0};
    std::atomic<bool> isMSL{true};
    std::atomic<double> speedKnots{250.0};
    std::atomic<int> nextIndex{1};
};

static FlightSettings g_settings;

// ── Geodesic helpers ────────────────────────────────────────────────

static constexpr double DEG2RAD = M_PI / 180.0;
static constexpr double RAD2DEG = 180.0 / M_PI;
static constexpr double EARTH_R_NM = 3440.065;

static double haversineNm(double lat1, double lon1, double lat2, double lon2)
{
    double dLat = (lat2 - lat1) * DEG2RAD;
    double dLon = (lon2 - lon1) * DEG2RAD;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * DEG2RAD) * std::cos(lat2 * DEG2RAD) *
               std::sin(dLon / 2) * std::sin(dLon / 2);
    return EARTH_R_NM * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

static double bearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    double dLon = (lon2 - lon1) * DEG2RAD;
    double la1 = lat1 * DEG2RAD, la2 = lat2 * DEG2RAD;
    double y = std::sin(dLon) * std::cos(la2);
    double x = std::cos(la1) * std::sin(la2) - std::sin(la1) * std::cos(la2) * std::cos(dLon);
    return std::fmod(std::atan2(y, x) * RAD2DEG + 360.0, 360.0);
}

// Interpolate along a great-circle arc between two lat/lon points.
// Returns intermediate points (excluding start, including end).
static void interpolateGreatCircle(
    double lat1, double lon1, double alt1,
    double lat2, double lon2, double alt2,
    double maxSegmentNm,
    std::vector<glm::dvec3>& out)
{
    double distNm = haversineNm(lat1, lon1, lat2, lon2);
    int segments = std::max(1, static_cast<int>(std::ceil(distNm / maxSegmentNm)));

    double phi1 = lat1 * DEG2RAD, lam1 = lon1 * DEG2RAD;
    double phi2 = lat2 * DEG2RAD, lam2 = lon2 * DEG2RAD;

    // Angular distance on the unit sphere
    double d = 2.0 * std::asin(std::sqrt(
        std::pow(std::sin((phi2 - phi1) / 2), 2) +
        std::cos(phi1) * std::cos(phi2) * std::pow(std::sin((lam2 - lam1) / 2), 2)));

    if (d < 1e-12)
    {
        out.emplace_back(lon2, lat2, alt2);
        return;
    }

    for (int i = 1; i <= segments; i++)
    {
        double f = static_cast<double>(i) / segments;
        double A = std::sin((1.0 - f) * d) / std::sin(d);
        double B = std::sin(f * d) / std::sin(d);

        double x = A * std::cos(phi1) * std::cos(lam1) + B * std::cos(phi2) * std::cos(lam2);
        double y = A * std::cos(phi1) * std::sin(lam1) + B * std::cos(phi2) * std::sin(lam2);
        double z = A * std::sin(phi1) + B * std::sin(phi2);

        double lat = std::atan2(z, std::sqrt(x * x + y * y)) * RAD2DEG;
        double lon = std::atan2(y, x) * RAD2DEG;
        double alt = alt1 + f * (alt2 - alt1); // linear altitude interpolation

        out.emplace_back(lon, lat, alt);
    }
}

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

        double markerAltMSL = isMSL ? flightAlt * 0.3048 : terrainAlt + flightAlt * 0.3048;

        entt::entity entity = entt::null;
        app->registry.write([&](entt::registry& r)
        {
            entity = r.create();

            auto& geom = r.emplace<PointGeometry>(entity);
            geom.srs = SRS::WGS84;
            geom.points.emplace_back(lon, lat, markerAltMSL);

            auto& style = r.emplace<PointStyle>(entity);
            style.color = Color(0.2f, 0.6f, 1.0f, 1.0f);
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

// ── HUD overlay: flight settings + last waypoint ────────────────────

class FlightHUD : public QWidget
{
    Q_OBJECT
public:
    explicit FlightHUD(QWidget* parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setStyleSheet("background: transparent;");

        auto* outerLayout = new QVBoxLayout(this);
        outerLayout->setContentsMargins(0, 0, 0, 0);

        // ── Top-left: instructions + last WP ────────────────────────
        auto* topRow = new QHBoxLayout();
        topRow->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        auto* infoCol = new QVBoxLayout();
        infoCol->setContentsMargins(12, 12, 12, 0);
        infoCol->setSpacing(6);

        QString infoStyle =
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 180);"
            "  color: #00ff88;"
            "  font-family: 'Consolas', 'Menlo', monospace;"
            "  font-size: 12px;"
            "  padding: 5px 8px;"
            "  border-radius: 3px;"
            "}";

        instructionLabel_ = new QLabel("Right-click earth to add waypoints");
        instructionLabel_->setStyleSheet(infoStyle);
        infoCol->addWidget(instructionLabel_);

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
        infoCol->addWidget(lastWpLabel_);
        infoCol->addStretch();

        topRow->addLayout(infoCol);
        topRow->addStretch();

        // ── Top-right: flight settings panel ────────────────────────
        auto* settingsPanel = new QWidget();
        settingsPanel->setFixedWidth(240);
        settingsPanel->setStyleSheet(
            "QWidget#settingsPanel {"
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
        settingsPanel->setObjectName("settingsPanel");

        auto* sLayout = new QVBoxLayout(settingsPanel);
        sLayout->setContentsMargins(10, 8, 10, 8);
        sLayout->setSpacing(4);

        auto* titleLabel = new QLabel("FLIGHT SETTINGS");
        titleLabel->setStyleSheet(
            "QLabel { color: #4488cc; font-weight: bold; font-size: 11px;"
            "  letter-spacing: 2px; background: transparent; }");
        titleLabel->setAlignment(Qt::AlignCenter);
        sLayout->addWidget(titleLabel);

        auto* sep1 = new QLabel("");
        sep1->setFixedHeight(1);
        sep1->setStyleSheet("QLabel { background: rgba(60, 140, 255, 60); }");
        sLayout->addWidget(sep1);

        auto* altLabel = new QLabel("ALTITUDE");
        sLayout->addWidget(altLabel);
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

        sLayout->addSpacing(4);

        auto* spdLabel = new QLabel("SPEED");
        sLayout->addWidget(spdLabel);
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

        auto* settingsContainer = new QVBoxLayout();
        settingsContainer->setContentsMargins(0, 12, 12, 0);
        settingsContainer->addWidget(settingsPanel);
        settingsContainer->addStretch();

        topRow->addLayout(settingsContainer);
        outerLayout->addLayout(topRow);
        outerLayout->addStretch();

        // Connect spinboxes to global settings
        connect(altSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [](double v) { g_settings.altitude.store(v); });
        connect(altTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [](int i) { g_settings.isMSL.store(i == 0); });
        connect(speedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [](double v) { g_settings.speedKnots.store(v); });
    }

    void showWaypoint(const Waypoint& wp)
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
        lastWpLabel_->setVisible(true);
    }

    QPushButton* clearButton() { return clearBtn_; }

protected:
    // Let mouse events pass through to the Rocky view, except on interactive widgets
    bool event(QEvent* e) override
    {
        if (e->type() == QEvent::MouseButtonPress ||
            e->type() == QEvent::MouseButtonRelease ||
            e->type() == QEvent::MouseMove ||
            e->type() == QEvent::Wheel)
        {
            // Check if the event is over an interactive child widget
            auto* me = static_cast<QMouseEvent*>(e);
            QWidget* child = childAt(me->pos());
            if (child && (qobject_cast<QDoubleSpinBox*>(child) ||
                          qobject_cast<QComboBox*>(child) ||
                          qobject_cast<QPushButton*>(child) ||
                          qobject_cast<QAbstractSpinBox*>(child)))
            {
                return QWidget::event(e); // handle normally
            }
            // Otherwise pass through to Rocky view underneath
            e->ignore();
            return false;
        }
        return QWidget::event(e);
    }

private:
    QLabel* instructionLabel_;
    QLabel* lastWpLabel_;
    QDoubleSpinBox* altSpin_;
    QComboBox* altTypeCombo_;
    QDoubleSpinBox* speedSpin_;
    QPushButton* clearBtn_;
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

        hud_ = new FlightHUD(rockyWidget_);
        hud_->raise();
    }

    FlightHUD* hud() { return hud_; }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (hud_ && rockyWidget_)
            hud_->setGeometry(rockyWidget_->rect());
    }

private:
    rocky::Application& app_;
    vsgQt::Window* rockyWindow_ = nullptr;
    QWidget* rockyWidget_ = nullptr;
    FlightHUD* hud_ = nullptr;
};

// ── Flight plan formatter ───────────────────────────────────────────

static QString formatFlightPlan(const std::vector<Waypoint>& wps)
{
    if (wps.empty()) return "(empty flight plan)";

    QString text;
    text += QString("FLIGHT PLAN  (%1 waypoint%2)\n")
        .arg(wps.size()).arg(wps.size() > 1 ? "s" : "");
    text += QString("").fill('=', 72) + "\n\n";

    double totalDist = 0;
    double totalTime = 0;

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
    mainWindow.resize(1600, 900);

    auto* menuBar = mainWindow.menuBar();
    auto* fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("E&xit", &qtApp, &QApplication::quit);
    auto* viewMenu = menuBar->addMenu("&View");
    mainWindow.statusBar()->showMessage("Right-click on the earth to add waypoints");

    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    auto* dockManager = new ads::CDockManager(&mainWindow);

    // ── Earth View (center) — dominant area ─────────────────────────

    auto* rockyContent = new RockyDockContent(app);
    auto* rockyDock = new ads::CDockWidget(dockManager, "Earth View");
    rockyDock->setWidget(rockyContent);
    rockyDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    auto* centerArea = dockManager->addDockWidget(ads::CenterDockWidgetArea, rockyDock);

    // ── Waypoint Table (right, narrow) ──────────────────────────────

    auto* wpTree = new QTreeWidget();
    wpTree->setHeaderLabels({"WP", "Lat", "Lon", "Alt", "Ref", "Kts"});
    wpTree->setAlternatingRowColors(true);
    wpTree->setRootIsDecorated(false);
    wpTree->header()->setStretchLastSection(true);
    wpTree->setStyleSheet("QTreeWidget { font-family: monospace; font-size: 11px; }");

    auto* wpDock = new ads::CDockWidget(dockManager, "Waypoints");
    wpDock->setWidget(wpTree);
    auto* rightArea = dockManager->addDockWidget(ads::RightDockWidgetArea, wpDock);

    // ── Flight Plan Text (bottom, shorter) ──────────────────────────

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

    // Keep the right panel narrow so the earth view is dominant
    wpDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    wpTree->setMinimumWidth(280);
    wpTree->setMaximumWidth(400);

    // View menu toggles
    viewMenu->addAction(rockyDock->toggleViewAction());
    viewMenu->addAction(wpDock->toggleViewAction());
    viewMenu->addAction(planDock->toggleViewAction());

    // ── Flight plan state ───────────────────────────────────────────

    std::vector<Waypoint> waypoints;
    entt::entity routeLineEntity = entt::null;

    // Build route line with great-circle interpolation
    auto updateRouteLine = [&]()
    {
        app.registry.write([&](entt::registry& r)
        {
            if (routeLineEntity != entt::null && r.valid(routeLineEntity))
                r.destroy(routeLineEntity);
            routeLineEntity = entt::null;

            if (waypoints.size() < 2) return;

            routeLineEntity = r.create();

            auto& geom = r.emplace<LineGeometry>(routeLineEntity);
            geom.topology = LineTopology::Strip;
            geom.srs = SRS::WGS84;

            // First waypoint
            auto& wp0 = waypoints[0];
            double alt0 = wp0.isMSL ? wp0.flightAlt * 0.3048
                                    : wp0.terrainAlt + wp0.flightAlt * 0.3048;
            geom.points.emplace_back(wp0.lon, wp0.lat, alt0);

            // Interpolate great-circle segments between consecutive waypoints
            // Max 50nm per segment keeps arcs smooth on the globe
            constexpr double MAX_SEG_NM = 50.0;
            for (size_t i = 1; i < waypoints.size(); i++)
            {
                auto& prev = waypoints[i - 1];
                auto& cur = waypoints[i];
                double prevAlt = prev.isMSL ? prev.flightAlt * 0.3048
                                            : prev.terrainAlt + prev.flightAlt * 0.3048;
                double curAlt = cur.isMSL ? cur.flightAlt * 0.3048
                                          : cur.terrainAlt + cur.flightAlt * 0.3048;

                interpolateGreatCircle(
                    prev.lat, prev.lon, prevAlt,
                    cur.lat, cur.lon, curAlt,
                    MAX_SEG_NM, geom.points);
            }

            auto& style = r.emplace<LineStyle>(routeLineEntity);
            style.color = Color(0.2f, 0.8f, 1.0f, 0.9f);
            style.width = 3.0f;
            style.depthOffset = 10000.0f;

            r.emplace<Line>(routeLineEntity, geom, style);
            app.vsgcontext->requestFrame();
        });
    };

    // Clear flight plan
    QObject::connect(rockyContent->hud()->clearButton(), &QPushButton::clicked, [&]() {
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

    fileMenu->addSeparator();
    fileMenu->addAction("&Clear Flight Plan", [&]() {
        rockyContent->hud()->clearButton()->click();
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
                QString("%1%2").arg(QString::number(std::abs(wp.lat), 'f', 4)).arg(latDir),
                QString("%1%2").arg(QString::number(std::abs(wp.lon), 'f', 4)).arg(lonDir),
                QString::number(wp.flightAlt, 'f', 0),
                wp.isMSL ? "MSL" : "AGL",
                QString::number(wp.speedKnots, 'f', 0)
            });
            wpTree->addTopLevelItem(item);
            wpTree->scrollToBottom();

            rockyContent->hud()->showWaypoint(wp);

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
