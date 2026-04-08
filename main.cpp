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
#include <QMoveEvent>
#include <QShowEvent>

#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>

#include <vsgQt/Window.h>

#include <mutex>
#include <deque>
#include <cmath>

using namespace ROCKY_NAMESPACE;

// ── Data structures ─────────────────────────────────────────────────

enum class WPType { FlyTo, ApproachToHover };

struct Waypoint
{
    int index;
    double lon, lat;
    double terrainAlt;     // ground elevation (meters)
    double flightAlt;      // altitude value (feet)
    bool isMSL;
    double speedKnots;
    WPType type = WPType::FlyTo;

    // Approach-to-hover specific
    double inboundBearing = 0;   // degrees true
    double fafLon = 0, fafLat = 0; // final approach fix position
    double fafAltFt = 0;        // FAF altitude (feet MSL)

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
    std::atomic<int> mode{0}; // 0=FlyTo, 1=ApproachToHover
    // Previous waypoint altitude in feet MSL (for glideslope calc)
    std::atomic<double> prevAltFtMSL{5000.0};
};

static FlightSettings g_settings;

// ── Geodesic helpers ────────────────────────────────────────────────

static constexpr double DEG2RAD = M_PI / 180.0;
static constexpr double RAD2DEG = 180.0 / M_PI;
static constexpr double EARTH_R_NM = 3440.065;
static constexpr double EARTH_R_M = 6371000.0;
static constexpr double FT_TO_M = 0.3048;
static constexpr double M_TO_FT = 3.28084;
static constexpr double NM_TO_FT = 6076.12;
static constexpr double GLIDE_ANGLE_DEG = 3.0;

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

// Compute destination point given start, bearing (deg), and distance (nm)
static void destPoint(double lat1, double lon1, double bearingDeg_, double distNm,
                      double& latOut, double& lonOut)
{
    double d = distNm / EARTH_R_NM; // angular distance
    double brg = bearingDeg_ * DEG2RAD;
    double phi1 = lat1 * DEG2RAD, lam1 = lon1 * DEG2RAD;

    double phi2 = std::asin(std::sin(phi1) * std::cos(d) +
                            std::cos(phi1) * std::sin(d) * std::cos(brg));
    double lam2 = lam1 + std::atan2(std::sin(brg) * std::sin(d) * std::cos(phi1),
                                     std::cos(d) - std::sin(phi1) * std::sin(phi2));

    latOut = phi2 * RAD2DEG;
    lonOut = lam2 * RAD2DEG;
}

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
        double alt = alt1 + f * (alt2 - alt1);

        out.emplace_back(lon, lat, alt);
    }
}

// ── VSG right-click handler ─────────────────────────────────────────

class RightClickHandler : public vsg::Inherit<vsg::Visitor, RightClickHandler>
{
public:
    rocky::Application* app = nullptr;

    void apply(vsg::ButtonPressEvent& event) override
    {
        if (event.button != 3 || !app)
            return;

        // Only capture press in Approach mode (need press point for hover location)
        if (g_settings.mode.load() != 1)
            return;

        pressX_ = event.x;
        pressY_ = event.y;
        pressed_ = true;

        auto result = rocky::pointAtWindowCoords(
            vsg::ref_ptr<vsg::Viewer>(app->viewer.get()),
            event.x, event.y);

        if (result.ok())
        {
            auto wgs84 = result.value().point.transform(SRS::WGS84);
            pressLon_ = wgs84.x;
            pressLat_ = wgs84.y;
            pressTerrainAlt_ = wgs84.z;
            pressValid_ = true;
        }
        else
        {
            pressValid_ = false;
        }
    }

    void apply(vsg::ButtonReleaseEvent& event) override
    {
        if (event.button != 3 || !app)
            return;

        int mode = g_settings.mode.load();

        if (mode == 0)
        {
            handleFlyTo(event);
        }
        else if (pressed_)
        {
            pressed_ = false;
            handleApproach(event);
        }
    }

private:
    bool pressed_ = false;
    int pressX_ = 0, pressY_ = 0;
    double pressLon_ = 0, pressLat_ = 0, pressTerrainAlt_ = 0;
    bool pressValid_ = false;

    void handleFlyTo(vsg::ButtonReleaseEvent& event)
    {
        auto result = rocky::pointAtWindowCoords(
            vsg::ref_ptr<vsg::Viewer>(app->viewer.get()),
            event.x, event.y);

        if (!result.ok()) return;

        auto wgs84 = result.value().point.transform(SRS::WGS84);
        double lon = wgs84.x, lat = wgs84.y, terrainAlt = wgs84.z;
        double flightAlt = g_settings.altitude.load();
        bool isMSL = g_settings.isMSL.load();
        double speed = g_settings.speedKnots.load();
        int idx = g_settings.nextIndex.fetch_add(1);

        double markerAltMSL = isMSL ? flightAlt * FT_TO_M : terrainAlt + flightAlt * FT_TO_M;
        double altFtMSL = markerAltMSL * M_TO_FT;

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

        Waypoint wp{};
        wp.index = idx;
        wp.lon = lon; wp.lat = lat;
        wp.terrainAlt = terrainAlt;
        wp.flightAlt = flightAlt;
        wp.isMSL = isMSL;
        wp.speedKnots = speed;
        wp.type = WPType::FlyTo;
        wp.pointEntity = entity;

        g_wpQueue.push(wp);
        g_settings.prevAltFtMSL.store(altFtMSL);
    }

    void handleApproach(vsg::ButtonReleaseEvent& event)
    {
        if (!pressValid_) return;

        // The hover point is where the user initially clicked (press location)
        double hoverLon = pressLon_;
        double hoverLat = pressLat_;
        double hoverTerrainAlt = pressTerrainAlt_; // meters

        // Hover altitude: 20 ft AGL
        double hoverAltFtMSL = hoverTerrainAlt * M_TO_FT + 20.0;
        double hoverAltM = hoverAltFtMSL * FT_TO_M;

        // Determine inbound bearing from drag direction
        // The drag goes FROM the click point toward the release point.
        // The inbound bearing is the OPPOSITE direction (approaching FROM where you dragged TO).
        double dx = event.x - pressX_;
        double dy = event.y - pressY_;
        double dragDist = std::sqrt(dx * dx + dy * dy);

        double inboundBearing;
        if (dragDist > 15.0) // enough drag to define bearing
        {
            // Pick earth point at release
            auto releaseResult = rocky::pointAtWindowCoords(
                vsg::ref_ptr<vsg::Viewer>(app->viewer.get()),
                event.x, event.y);

            if (releaseResult.ok())
            {
                auto releaseWgs84 = releaseResult.value().point.transform(SRS::WGS84);
                // Bearing from hover point to drag end point
                double dragBearing = bearingDeg(hoverLat, hoverLon,
                                                releaseWgs84.y, releaseWgs84.x);
                // Inbound bearing = approach FROM the direction the user dragged toward
                inboundBearing = dragBearing;
            }
            else
            {
                // Fallback: use screen-space angle
                double screenAngle = std::atan2(-dy, dx) * RAD2DEG; // Qt Y is inverted
                inboundBearing = std::fmod(90.0 - screenAngle + 360.0, 360.0);
            }
        }
        else
        {
            // No meaningful drag - default inbound from north
            inboundBearing = 0.0;
        }

        // Compute FAF: 3-degree glideslope from previous altitude down to hover
        double prevAltFtMSL = g_settings.prevAltFtMSL.load();
        double altToLoseFt = prevAltFtMSL - hoverAltFtMSL;
        if (altToLoseFt < 100.0) altToLoseFt = 100.0; // minimum descent

        // distance = altitude / tan(3°)
        double approachDistFt = altToLoseFt / std::tan(GLIDE_ANGLE_DEG * DEG2RAD);
        double approachDistNm = approachDistFt / NM_TO_FT;

        // FAF is located along the reciprocal of the inbound bearing
        // (i.e., behind the hover point, in the direction the aircraft comes from)
        double recipBearing = std::fmod(inboundBearing + 180.0, 360.0);
        double fafLat, fafLon;
        destPoint(hoverLat, hoverLon, recipBearing, approachDistNm, fafLat, fafLon);

        double speed = g_settings.speedKnots.load();
        int idx = g_settings.nextIndex.fetch_add(1);

        // Create markers: FAF (yellow) and hover point (green)
        entt::entity entity = entt::null;
        app->registry.write([&](entt::registry& r)
        {
            // FAF marker
            auto fafEntity = r.create();
            auto& fafGeom = r.emplace<PointGeometry>(fafEntity);
            fafGeom.srs = SRS::WGS84;
            fafGeom.points.emplace_back(fafLon, fafLat, prevAltFtMSL * FT_TO_M);
            auto& fafStyle = r.emplace<PointStyle>(fafEntity);
            fafStyle.color = Color(1.0f, 0.8f, 0.0f, 1.0f); // yellow
            fafStyle.width = 8.0f;
            fafStyle.antialias = 0.5f;
            fafStyle.depthOffset = 10000.0f;
            r.emplace<Point>(fafEntity, fafGeom, fafStyle);

            // Hover point marker
            entity = r.create();
            auto& geom = r.emplace<PointGeometry>(entity);
            geom.srs = SRS::WGS84;
            geom.points.emplace_back(hoverLon, hoverLat, hoverAltM);
            auto& style = r.emplace<PointStyle>(entity);
            style.color = Color(0.0f, 1.0f, 0.3f, 1.0f); // green
            style.width = 14.0f;
            style.antialias = 0.5f;
            style.depthOffset = 10000.0f;
            r.emplace<Point>(entity, geom, style);

            app->vsgcontext->requestFrame();
        });

        Waypoint wp{};
        wp.index = idx;
        wp.lon = hoverLon; wp.lat = hoverLat;
        wp.terrainAlt = hoverTerrainAlt;
        wp.flightAlt = hoverAltFtMSL;
        wp.isMSL = true; // hover alt is always computed as MSL
        wp.speedKnots = speed;
        wp.type = WPType::ApproachToHover;
        wp.inboundBearing = inboundBearing;
        wp.fafLon = fafLon; wp.fafLat = fafLat;
        wp.fafAltFt = prevAltFtMSL;
        wp.pointEntity = entity;

        g_wpQueue.push(wp);
        g_settings.prevAltFtMSL.store(hoverAltFtMSL);
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

// ── Floating settings panel ─────────────────────────────────────────

class FlightSettingsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FlightSettingsPanel(QWidget* parent = nullptr)
        : QWidget(parent, Qt::FramelessWindowHint | Qt::Tool)
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
            [](double v) { g_settings.altitude.store(v); });
        connect(altTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [](int i) { g_settings.isMSL.store(i == 0); });
        connect(speedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [](double v) { g_settings.speedKnots.store(v); });
        connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int i) {
                g_settings.mode.store(i);
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
    QComboBox* modeCombo_;
    QLabel* altLabel_;
    QDoubleSpinBox* altSpin_;
    QComboBox* altTypeCombo_;
    QDoubleSpinBox* speedSpin_;
    QPushButton* clearBtn_;
    QLabel* approachInfo_;
};

// ── Floating info overlay ───────────────────────────────────────────

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

        settingsPanel_ = new FlightSettingsPanel();
        infoOverlay_ = new FlightInfoOverlay();
        settingsPanel_->show();
        infoOverlay_->show();
    }

    FlightSettingsPanel* settingsPanel() { return settingsPanel_; }
    FlightInfoOverlay* infoOverlay() { return infoOverlay_; }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        repositionOverlays();
    }
    void moveEvent(QMoveEvent* event) override
    {
        QWidget::moveEvent(event);
        repositionOverlays();
    }
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        repositionOverlays();
        if (settingsPanel_) settingsPanel_->show();
        if (infoOverlay_) infoOverlay_->show();
    }
    void hideEvent(QHideEvent* event) override
    {
        QWidget::hideEvent(event);
        if (settingsPanel_) settingsPanel_->hide();
        if (infoOverlay_) infoOverlay_->hide();
    }

private:
    void repositionOverlays()
    {
        if (!rockyWidget_) return;
        QPoint tl = rockyWidget_->mapToGlobal(QPoint(0, 0));
        QPoint tr = rockyWidget_->mapToGlobal(QPoint(rockyWidget_->width(), 0));
        if (settingsPanel_) settingsPanel_->positionOver(tr);
        if (infoOverlay_) infoOverlay_->positionOver(tl);
    }

    rocky::Application& app_;
    vsgQt::Window* rockyWindow_ = nullptr;
    QWidget* rockyWidget_ = nullptr;
    FlightSettingsPanel* settingsPanel_ = nullptr;
    FlightInfoOverlay* infoOverlay_ = nullptr;
};

// ── Flight plan formatter ───────────────────────────────────────────

static QString formatFlightPlan(const std::vector<Waypoint>& wps)
{
    if (wps.empty()) return "(empty flight plan)";

    QString text;
    text += QString("FLIGHT PLAN  (%1 waypoint%2)\n")
        .arg(wps.size()).arg(wps.size() > 1 ? "s" : "");
    text += QString("").fill('=', 72) + "\n\n";

    double totalDist = 0, totalTime = 0;
    QString deg = QString::fromUtf8("\xC2\xB0");

    for (size_t i = 0; i < wps.size(); i++)
    {
        auto& wp = wps[i];
        auto latDir = wp.lat >= 0 ? "N" : "S";
        auto lonDir = wp.lon >= 0 ? "E" : "W";

        if (wp.type == WPType::FlyTo)
        {
            text += QString("  WP%1  FLY TO  %2%3%4  %5%6%7\n")
                .arg(wp.index, 2, 10, QChar('0'))
                .arg(QString::number(std::abs(wp.lat), 'f', 5)).arg(latDir).arg(deg)
                .arg(QString::number(std::abs(wp.lon), 'f', 5)).arg(lonDir).arg(deg);
            text += QString("        ALT %1 ft %2   SPD %3 kts\n")
                .arg(QString::number(wp.flightAlt, 'f', 0))
                .arg(wp.isMSL ? "MSL" : "AGL")
                .arg(QString::number(wp.speedKnots, 'f', 0));
        }
        else
        {
            text += QString("  WP%1  APPROACH TO HOVER  %2%3%4  %5%6%7\n")
                .arg(wp.index, 2, 10, QChar('0'))
                .arg(QString::number(std::abs(wp.lat), 'f', 5)).arg(latDir).arg(deg)
                .arg(QString::number(std::abs(wp.lon), 'f', 5)).arg(lonDir).arg(deg);
            text += QString("        HOVER 20 ft AGL (%1 ft MSL)   SPD %2 kts\n")
                .arg(QString::number(wp.flightAlt, 'f', 0))
                .arg(QString::number(wp.speedKnots, 'f', 0));
            text += QString("        INBOUND %1%2   FAF %3 ft MSL   3%4 GS\n")
                .arg(QString::number(wp.inboundBearing, 'f', 0)).arg(deg)
                .arg(QString::number(wp.fafAltFt, 'f', 0)).arg(deg);

            double approachDist = haversineNm(wp.fafLat, wp.fafLon, wp.lat, wp.lon);
            text += QString("        APPROACH LEG %1 nm\n")
                .arg(QString::number(approachDist, 'f', 1));
        }

        if (i > 0)
        {
            auto& prev = wps[i - 1];
            double fromLat = prev.lat, fromLon = prev.lon;
            double toLat = wp.lat, toLon = wp.lon;

            // For approach waypoints, distance is to the FAF, then FAF to hover
            if (wp.type == WPType::ApproachToHover)
            {
                toLat = wp.fafLat;
                toLon = wp.fafLon;
            }

            double dist = haversineNm(fromLat, fromLon, toLat, toLon);
            double brg = bearingDeg(fromLat, fromLon, toLat, toLon);
            if (wp.type == WPType::ApproachToHover)
                dist += haversineNm(wp.fafLat, wp.fafLon, wp.lat, wp.lon);

            double legTime = (wp.speedKnots > 0) ? dist / wp.speedKnots : 0;
            totalDist += dist;
            totalTime += legTime;

            int mins = static_cast<int>(legTime * 60.0 + 0.5);
            text += QString("        LEG  %1 nm  BRG %2%3  ETE %4 min\n")
                .arg(QString::number(dist, 'f', 1))
                .arg(QString::number(brg, 'f', 0)).arg(deg)
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

    auto* rockyContent = new RockyDockContent(app);
    auto* rockyDock = new ads::CDockWidget(dockManager, "Earth View");
    rockyDock->setWidget(rockyContent);
    rockyDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    dockManager->addDockWidget(ads::CenterDockWidgetArea, rockyDock);

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

    // ── Flight plan state ───────────────────────────────────────────

    std::vector<Waypoint> waypoints;
    entt::entity routeLineEntity = entt::null;

    auto getWpAltMSL = [](const Waypoint& wp) -> double {
        if (wp.type == WPType::ApproachToHover)
            return wp.flightAlt * FT_TO_M; // already MSL
        return wp.isMSL ? wp.flightAlt * FT_TO_M
                        : wp.terrainAlt + wp.flightAlt * FT_TO_M;
    };

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

            constexpr double MAX_SEG_NM = 50.0;

            auto& wp0 = waypoints[0];
            geom.points.emplace_back(wp0.lon, wp0.lat, getWpAltMSL(wp0));

            for (size_t i = 1; i < waypoints.size(); i++)
            {
                auto& prev = waypoints[i - 1];
                auto& cur = waypoints[i];

                double prevAlt = getWpAltMSL(prev);
                double prevLat = prev.lat, prevLon = prev.lon;

                if (cur.type == WPType::ApproachToHover)
                {
                    // Route to FAF first (great-circle at prev altitude)
                    double fafAlt = cur.fafAltFt * FT_TO_M;
                    interpolateGreatCircle(prevLat, prevLon, prevAlt,
                        cur.fafLat, cur.fafLon, fafAlt, MAX_SEG_NM, geom.points);

                    // Then glideslope from FAF down to hover point
                    double hoverAlt = getWpAltMSL(cur);
                    interpolateGreatCircle(cur.fafLat, cur.fafLon, fafAlt,
                        cur.lat, cur.lon, hoverAlt, 5.0, geom.points);
                }
                else
                {
                    double curAlt = getWpAltMSL(cur);
                    interpolateGreatCircle(prevLat, prevLon, prevAlt,
                        cur.lat, cur.lon, curAlt, MAX_SEG_NM, geom.points);
                }
            }

            auto& style = r.emplace<LineStyle>(routeLineEntity);
            style.color = Color(0.2f, 0.8f, 1.0f, 0.9f);
            style.width = 3.0f;
            style.depthOffset = 10000.0f;

            r.emplace<Line>(routeLineEntity, geom, style);
            app.vsgcontext->requestFrame();
        });
    };

    QObject::connect(rockyContent->settingsPanel()->clearButton(), &QPushButton::clicked, [&]() {
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
        g_settings.prevAltFtMSL.store(5000.0);
        mainWindow.statusBar()->showMessage("Flight plan cleared");
    });

    fileMenu->addSeparator();
    fileMenu->addAction("&Clear Flight Plan", [&]() {
        rockyContent->settingsPanel()->clearButton()->click();
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

            rockyContent->infoOverlay()->showWaypoint(wp);

            mainWindow.statusBar()->showMessage(
                QString("WP%1 added (%2) - %3 waypoints in plan")
                    .arg(wp.index, 2, 10, QChar('0'))
                    .arg(typeStr)
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
