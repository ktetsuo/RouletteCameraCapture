#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_RouletteCameraCapture.h"

class RouletteCameraCapture : public QMainWindow
{
    Q_OBJECT

public:
    RouletteCameraCapture(QWidget *parent = nullptr);
    ~RouletteCameraCapture();

private:
    Ui::RouletteCameraCaptureClass ui;
};

