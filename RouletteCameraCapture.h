#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QLabel>
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QMediaCaptureSession>
#include <QtMultimedia/QVideoSink>
#include <QtGui/QImage>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include "ui_RouletteCameraCapture.h"

class RouletteCameraCapture : public QMainWindow
{
    Q_OBJECT

public:
    RouletteCameraCapture(QWidget *parent = nullptr);
    ~RouletteCameraCapture();

private:
    void setupCamera();
    void onVideoFrameChanged(const QVideoFrame &frame);
    void updatePreview();

private:
    Ui::RouletteCameraCaptureClass ui;
    QLabel *m_previewLabel = nullptr;
    QCamera *m_camera = nullptr;
    QMediaCaptureSession m_captureSession;
    QVideoSink *m_videoSink = nullptr;
    QImage m_latestFrame;
    QMutex m_frameMutex;
    QTimer m_previewTimer;
};

