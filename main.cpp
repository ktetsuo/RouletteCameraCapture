#include "stdafx.h"
#include "RouletteCameraCapture.h"
#include <QtWidgets/QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    RouletteCameraCapture window;
    window.show();
    return app.exec();
}
