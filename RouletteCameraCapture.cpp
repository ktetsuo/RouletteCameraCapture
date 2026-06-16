#include "stdafx.h"
#include "RouletteCameraCapture.h"

#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QCameraDevice>
#include <QtMultimedia/QCameraFormat>
#include <QtMultimedia/QVideoFrame>
#include <QtCore/QMutexLocker>
#include <QtGui/QPixmap>
#include <QtWidgets/QStatusBar>

RouletteCameraCapture::RouletteCameraCapture(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setText("Initializing camera...");
    setCentralWidget(m_previewLabel);

    m_previewTimer.setInterval(100);
    connect(&m_previewTimer, &QTimer::timeout, this, &RouletteCameraCapture::updatePreview);
    m_previewTimer.start();

    setupCamera();
}

RouletteCameraCapture::~RouletteCameraCapture()
{
    if (m_camera != nullptr)
    {
        m_camera->stop();
    }
}

void RouletteCameraCapture::setupCamera()
{
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty())
    {
        m_previewLabel->setText("No USB camera found.");
        return;
    }

    m_camera = new QCamera(cameras.first(), this);

    const QList<QCameraFormat> formats = m_camera->cameraDevice().videoFormats();
    QCameraFormat bestFormat;
    qreal bestFrameRate = 0.0;
    bool foundAtOrAboveTarget = false;

    for (const QCameraFormat &format : formats)
    {
        const qreal maxRate = format.maxFrameRate();

        if (maxRate >= 260.0)
        {
            if (!foundAtOrAboveTarget || maxRate < bestFrameRate)
            {
                bestFormat = format;
                bestFrameRate = maxRate;
                foundAtOrAboveTarget = true;
            }
        }
        else if (!foundAtOrAboveTarget && maxRate > bestFrameRate)
        {
            bestFormat = format;
            bestFrameRate = maxRate;
        }
    }

    if (bestFrameRate > 0.0)
    {
        m_camera->setCameraFormat(bestFormat);
    }

    m_videoSink = new QVideoSink(this);
    m_captureSession.setCamera(m_camera);
    m_captureSession.setVideoSink(m_videoSink);

    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &RouletteCameraCapture::onVideoFrameChanged);

    m_camera->start();

    statusBar()->showMessage(QString("Capture target: 260 fps, actual max format: %1 fps, preview: 10 fps")
        .arg(bestFrameRate, 0, 'f', 1));
}

void RouletteCameraCapture::onVideoFrameChanged(const QVideoFrame &frame)
{
    if (!frame.isValid())
    {
        return;
    }

    const QImage image = frame.toImage();
    if (image.isNull())
    {
        return;
    }

    QMutexLocker locker(&m_frameMutex);
    m_latestFrame = image;
}

void RouletteCameraCapture::updatePreview()
{
    QImage frame;
    {
        QMutexLocker locker(&m_frameMutex);
        frame = m_latestFrame;
    }

    if (frame.isNull())
    {
        return;
    }

    m_previewLabel->setPixmap(QPixmap::fromImage(frame).scaled(
        m_previewLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}

