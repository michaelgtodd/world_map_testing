#pragma once

#include <QApplication>
#include <vsgQt/Window.h>

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
