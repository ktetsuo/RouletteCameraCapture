#include "stdafx.h"
#include "RouletteCameraCapture.h"

#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QCameraFormat>
#include <QtMultimedia/QVideoFrame>
#include <QtCore/QMutexLocker>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtGui/QPixmap>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

RouletteCameraCapture::RouletteCameraCapture(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);

    m_cameraSelector = new QComboBox(central);
    layout->addWidget(m_cameraSelector);

    m_previewLabel = new QLabel(central);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setText("Initializing camera...");
    layout->addWidget(m_previewLabel, 1);

    setCentralWidget(central);

    m_videoSink = new QVideoSink(this);
    m_captureSession.setVideoSink(m_videoSink);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &RouletteCameraCapture::onVideoFrameChanged);
    connect(m_cameraSelector, &QComboBox::currentIndexChanged, this, &RouletteCameraCapture::onCameraSelectionChanged);

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
    m_cameraDevices = QMediaDevices::videoInputs();

    {
        QSignalBlocker blocker(m_cameraSelector);
        m_cameraSelector->clear();

        for (const QCameraDevice &device : m_cameraDevices)
        {
            m_cameraSelector->addItem(device.description());
        }
    }

    if (m_cameraDevices.isEmpty())
    {
        m_previewLabel->setText("No USB camera found.");
        return;
    }

    const int selectedIndex = findSavedCameraIndex();
    {
        QSignalBlocker blocker(m_cameraSelector);
        m_cameraSelector->setCurrentIndex(selectedIndex);
    }
    startCamera(m_cameraDevices.at(selectedIndex));
}

void RouletteCameraCapture::startCamera(const QCameraDevice &cameraDevice)
{
    if (m_camera != nullptr)
    {
        m_camera->stop();
        m_captureSession.setCamera(nullptr);
        m_camera->deleteLater();
        m_camera = nullptr;
    }

    {
        QMutexLocker locker(&m_frameMutex);
        m_latestFrame = QImage();
    }
    m_previewLabel->setText("Switching camera...");

    m_camera = new QCamera(cameraDevice, this);

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

    m_captureSession.setCamera(m_camera);
    m_camera->start();

    m_currentCameraName = cameraDevice.description();
    m_selectedMaxFrameRate = bestFrameRate;
    m_captureFrameCount = 0;
    m_measuredCaptureFps = 0.0;
    m_captureFpsTimer.restart();
    updateStatusMessage();
}

int RouletteCameraCapture::findSavedCameraIndex() const
{
    QSettings settings("RouletteCameraCapture", "RouletteCameraCapture");
    const QByteArray savedDeviceId = settings.value("camera/deviceId").toByteArray();

    if (savedDeviceId.isEmpty())
    {
        return 0;
    }

    for (int i = 0; i < m_cameraDevices.size(); ++i)
    {
        if (m_cameraDevices.at(i).id() == savedDeviceId)
        {
            return i;
        }
    }

    return 0;
}

void RouletteCameraCapture::saveSelectedCamera(const QCameraDevice &cameraDevice)
{
    QSettings settings("RouletteCameraCapture", "RouletteCameraCapture");
    settings.setValue("camera/deviceId", cameraDevice.id());
}

void RouletteCameraCapture::onCameraSelectionChanged(int index)
{
    if (index < 0 || index >= m_cameraDevices.size())
    {
        return;
    }

    const QCameraDevice cameraDevice = m_cameraDevices.at(index);
    saveSelectedCamera(cameraDevice);
    startCamera(cameraDevice);
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

    bool shouldUpdateStatus = false;

    {
        QMutexLocker locker(&m_frameMutex);
        m_latestFrame = image;
        ++m_captureFrameCount;

        const qint64 elapsedMs = m_captureFpsTimer.elapsed();
        if (elapsedMs >= 1000)
        {
            m_measuredCaptureFps = static_cast<qreal>(m_captureFrameCount) * 1000.0 / static_cast<qreal>(elapsedMs);
            m_captureFrameCount = 0;
            m_captureFpsTimer.restart();
            shouldUpdateStatus = true;
        }
    }

    if (shouldUpdateStatus)
    {
        updateStatusMessage();
    }
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

void RouletteCameraCapture::updateStatusMessage()
{
    statusBar()->showMessage(QString("Camera: %1 | Capture target: 260 fps, selected max format: %2 fps, measured arrival: %3 fps, preview: 10 fps")
        .arg(m_currentCameraName)
        .arg(m_selectedMaxFrameRate, 0, 'f', 1)
        .arg(m_measuredCaptureFps, 0, 'f', 1));
}

