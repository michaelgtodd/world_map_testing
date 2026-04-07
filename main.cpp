/**
 * Rocky + Qt Advanced Docking System Demo
 *
 * Embeds a Rocky Vulkan earth view inside a Qt Advanced Docking System pane,
 * with Qt widgets overlaid on the Rocky view.
 *
 * Right-click on the earth to place a marker and display lat/lon.
 */

#include <rocky/rocky.h>
#include <rocky/vsg/DisplayManager.h>

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QComboBox>
#include <QStatusBar>
#include <QMenuBar>
#include <QTimer>

#include <DockManager.h>
#include <DockWidget.h>
#include <DockAreaWidget.h>

#include <vsgQt/Window.h>

#include <mutex>
#include <deque>
#include <sstream>
#include <iomanip>

using namespace ROCKY_NAMESPACE;

// Struct to pass marker data from VSG thread to Qt thread
struct MarkerInfo
{
    double lon;
    double lat;
    double alt;
    entt::entity entity;
};

// Thread-safe queue for markers created on the VSG/render thread
class MarkerQueue
{
public:
    void push(const MarkerInfo& m)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(m);
    }

    bool pop(MarkerInfo& m)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        m = queue_.front();
        queue_.pop_front();
        return true;
    }

private:
    std::mutex mutex_;
    std::deque<MarkerInfo> queue_;
};

// Global marker queue for the VSG event handler
static MarkerQueue g_markerQueue;

// VSG event handler that intercepts right-clicks and places markers
class RightClickHandler : public vsg::Inherit<vsg::Visitor, RightClickHandler>
{
public:
    rocky::Application* app = nullptr;
    int markerCount = 0;

    void apply(vsg::ButtonReleaseEvent& event) override
    {
        // Button 3 = right mouse button in VSG
        if (event.button != 3 || !app)
            return;

        int x = event.x;
        int y = event.y;

        // Find the view under the mouse and pick the earth surface
        auto result = rocky::pointAtWindowCoords(
            vsg::ref_ptr<vsg::Viewer>(app->viewer.get()),
            x, y);

        if (result.ok())
        {
            auto geoPoint = result.value().point;

            // Transform to WGS84 lat/lon if needed
            auto wgs84Point = geoPoint.transform(SRS::WGS84);
            double lon = wgs84Point.x;
            double lat = wgs84Point.y;
            double alt = wgs84Point.z;

            // Create a point marker entity in the Rocky ECS
            entt::entity entity = entt::null;
            app->registry.write([&](entt::registry& r)
            {
                entity = r.create();

                // Single-point geometry at the clicked location
                auto& geometry = r.emplace<PointGeometry>(entity);
                geometry.srs = SRS::WGS84;
                geometry.points.emplace_back(lon, lat, alt + 100.0); // slight offset above terrain

                // Style: bright marker
                auto& style = r.emplace<PointStyle>(entity);
                style.color = Color(1.0f, 0.2f, 0.2f, 1.0f); // red
                style.width = 12.0f;
                style.antialias = 0.5f;
                style.depthOffset = 10000.0f;

                r.emplace<Point>(entity, geometry, style);

                app->vsgcontext->requestFrame();
            });

            markerCount++;

            // Queue the marker info for the Qt thread to pick up
            g_markerQueue.push({ lon, lat, alt, entity });
        }
    }
};

// Custom viewer that bridges Rocky's frame loop with Qt's event loop
class RockyQtViewer : public vsg::Inherit<vsgQt::Viewer, RockyQtViewer>
{
public:
    void render(double simTime) override
    {
        if (continuousUpdate || requests.load() > 0)
        {
            if (frame() == false)
            {
                if (status->cancel())
                {
                    QApplication::quit();
                }
            }
        }
    }

    std::function<bool()> frame;
};

// Transparent overlay widget on top of the Rocky view
class RockyOverlayWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RockyOverlayWidget(QWidget* parent = nullptr)
        : QWidget(parent)
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

        // Instruction label
        instructionLabel_ = new QLabel("Right-click on the earth to place a marker");
        instructionLabel_->setStyleSheet(labelStyle);
        layout->addWidget(instructionLabel_);

        // Last-clicked coordinate display
        coordLabel_ = new QLabel("");
        coordLabel_->setStyleSheet(
            "QLabel {"
            "  background-color: rgba(0, 0, 0, 180);"
            "  color: #ffcc00;"
            "  font-family: monospace;"
            "  font-size: 14px;"
            "  font-weight: bold;"
            "  padding: 8px 12px;"
            "  border-radius: 4px;"
            "  border: 1px solid rgba(255, 204, 0, 100);"
            "}");
        coordLabel_->setVisible(false);
        layout->addWidget(coordLabel_);

        // Marker count
        countLabel_ = new QLabel("");
        countLabel_->setStyleSheet(labelStyle);
        countLabel_->setVisible(false);
        layout->addWidget(countLabel_);

        layout->addStretch();
    }

    void showMarker(double lon, double lat, double alt, int count)
    {
        auto latDir = lat >= 0 ? "N" : "S";
        auto lonDir = lon >= 0 ? "E" : "W";

        coordLabel_->setText(QString("Last: %1%2 %3, %4%5 %6  Alt: %7m")
            .arg(QString::number(std::abs(lat), 'f', 6))
            .arg(latDir)
            .arg(QString::fromUtf8("\xC2\xB0"))
            .arg(QString::number(std::abs(lon), 'f', 6))
            .arg(lonDir)
            .arg(QString::fromUtf8("\xC2\xB0"))
            .arg(QString::number(alt, 'f', 1)));
        coordLabel_->setVisible(true);

        countLabel_->setText(QString("Markers placed: %1").arg(count));
        countLabel_->setVisible(true);
    }

private:
    QLabel* instructionLabel_;
    QLabel* coordLabel_;
    QLabel* countLabel_;
};

// Container widget that hosts the Rocky view with an overlay
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

        // Create the Rocky/Vulkan window
        rockyWindow_ = new vsgQt::Window();
        rockyWidget_ = QWidget::createWindowContainer(rockyWindow_);
        layout->addWidget(rockyWidget_);

        // Must call initializeWindow() AFTER createWindowContainer()
        rockyWindow_->initializeWindow();
        app_.display.addWindow(rockyWindow_->windowAdapter);

        // Create overlay widget on top of the Rocky view
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

int main(int argc, char** argv)
{
    try {
    QApplication qtApp(argc, argv);
    qtApp.setApplicationName("Rocky Qt Docking Demo");

    // Create the Rocky viewer and application
    auto viewer = RockyQtViewer::create();
    rocky::Application app(viewer, argc, argv);
    app.renderContinuously = true;

    viewer->frame = [&app]() { return app.frame(); };
    viewer->continuousUpdate = true;
    viewer->setInterval(8);

    app.vsgcontext->devicePixelRatio = []() { return 1.0; };
    rocky::Log()->set_level(rocky::log::level::info);

    // Add map layers
    if (app.mapNode->map->layers().empty())
    {
        auto elevationLayer = rocky::TMSElevationLayer::create();
        elevationLayer->uri = "https://readymap.org/readymap/tiles/1.0.0/116/";
        app.mapNode->map->add(elevationLayer);

        auto imageLayer = rocky::TMSImageLayer::create();
        imageLayer->uri = "https://readymap.org/readymap/tiles/1.0.0/7";
        app.mapNode->map->add(imageLayer);
    }

    // Install the right-click event handler on the viewer
    auto rightClickHandler = RightClickHandler::create();
    rightClickHandler->app = &app;
    app.viewer->getEventHandlers().push_back(rightClickHandler);

    // Set up the main window
    QMainWindow mainWindow;
    mainWindow.setWindowTitle("Rocky + Qt Advanced Docking System");
    mainWindow.resize(1280, 800);

    auto* menuBar = mainWindow.menuBar();
    auto* fileMenu = menuBar->addMenu("&File");
    fileMenu->addAction("E&xit", &qtApp, &QApplication::quit);
    auto* viewMenu = menuBar->addMenu("&View");

    mainWindow.statusBar()->showMessage("Right-click on the earth to place markers");

    // Dock Manager
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager* dockManager = new ads::CDockManager(&mainWindow);

    // --- Dock 1: Rocky Earth View ---
    auto* rockyContent = new RockyDockContent(app);
    auto* rockyDock = new ads::CDockWidget(dockManager, "Earth View");
    rockyDock->setWidget(rockyContent);
    rockyDock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContent);
    dockManager->addDockWidget(ads::CenterDockWidgetArea, rockyDock);

    // --- Dock 2: Marker List ---
    auto* markerTree = new QTreeWidget();
    markerTree->setHeaderLabels({"#", "Latitude", "Longitude", "Altitude (m)"});
    markerTree->setAlternatingRowColors(true);
    markerTree->setRootIsDecorated(false);
    markerTree->setColumnWidth(0, 40);
    markerTree->setColumnWidth(1, 130);
    markerTree->setColumnWidth(2, 130);

    auto* markerDock = new ads::CDockWidget(dockManager, "Markers");
    markerDock->setWidget(markerTree);
    dockManager->addDockWidget(ads::RightDockWidgetArea, markerDock);

    // --- Dock 3: Log Output ---
    auto* logOutput = new QTextEdit();
    logOutput->setReadOnly(true);
    logOutput->setStyleSheet(
        "QTextEdit { background-color: #1e1e1e; color: #dcdcdc; font-family: monospace; font-size: 12px; }");
    logOutput->append("[INFO] Application started");
    logOutput->append("[INFO] Right-click on the earth to place markers");

    auto* logDock = new ads::CDockWidget(dockManager, "Output Log");
    logDock->setWidget(logOutput);
    dockManager->addDockWidget(ads::BottomDockWidgetArea, logDock);

    // View menu toggles
    viewMenu->addAction(rockyDock->toggleViewAction());
    viewMenu->addAction(markerDock->toggleViewAction());
    viewMenu->addAction(logDock->toggleViewAction());

    // Clear markers action
    int markerCount = 0;
    fileMenu->addSeparator();
    fileMenu->addAction("&Clear Markers", [&]() {
        app.registry.write([&](entt::registry& r) {
            for (int i = 0; i < markerTree->topLevelItemCount(); i++)
            {
                auto entityId = markerTree->topLevelItem(i)->data(0, Qt::UserRole).toUInt();
                auto entity = static_cast<entt::entity>(entityId);
                if (r.valid(entity))
                    r.destroy(entity);
            }
            app.vsgcontext->requestFrame();
        });
        markerTree->clear();
        markerCount = 0;
        logOutput->append("[INFO] All markers cleared");
        mainWindow.statusBar()->showMessage("Markers cleared");
    });

    // Timer to poll the marker queue and update Qt widgets
    QTimer pollTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, [&]() {
        MarkerInfo m;
        while (g_markerQueue.pop(m))
        {
            markerCount++;

            auto latDir = m.lat >= 0 ? "N" : "S";
            auto lonDir = m.lon >= 0 ? "E" : "W";

            // Add to marker list
            auto* item = new QTreeWidgetItem({
                QString::number(markerCount),
                QString("%1 %2").arg(QString::number(std::abs(m.lat), 'f', 6)).arg(latDir),
                QString("%1 %2").arg(QString::number(std::abs(m.lon), 'f', 6)).arg(lonDir),
                QString::number(m.alt, 'f', 1)
            });
            item->setData(0, Qt::UserRole, static_cast<unsigned int>(m.entity));
            markerTree->addTopLevelItem(item);
            markerTree->scrollToBottom();

            // Update overlay
            rockyContent->overlay()->showMarker(m.lon, m.lat, m.alt, markerCount);

            // Log it
            logOutput->append(QString("[MARKER %1] %2%3, %4%5  Alt: %6m")
                .arg(markerCount)
                .arg(QString::number(std::abs(m.lat), 'f', 6)).arg(latDir)
                .arg(QString::number(std::abs(m.lon), 'f', 6)).arg(lonDir)
                .arg(QString::number(m.alt, 'f', 1)));

            // Status bar
            mainWindow.statusBar()->showMessage(
                QString("Marker %1 placed at %2%3, %4%5")
                    .arg(markerCount)
                    .arg(QString::number(std::abs(m.lat), 'f', 4)).arg(latDir)
                    .arg(QString::number(std::abs(m.lon), 'f', 4)).arg(lonDir));
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
