#include "stdafx.h"
#include "RouletteCameraCapture.h"

#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QCameraFormat>
#include <QtMultimedia/QVideoFrame>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QMutexLocker>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtCore/QDebug>
#include <QtGui/QPixmap>
#include <QtWidgets/QHBoxLayout>
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

    QHBoxLayout *recordButtonsLayout = new QHBoxLayout();
    m_startRecordButton = new QPushButton("Start Buffer", central);
    m_stopRecordButton = new QPushButton("Stop Buffer", central);
    m_saveBufferButton = new QPushButton("Save Buffer", central);
    m_stopRecordButton->setEnabled(false);
    recordButtonsLayout->addWidget(m_startRecordButton);
    recordButtonsLayout->addWidget(m_stopRecordButton);
    recordButtonsLayout->addWidget(m_saveBufferButton);
    layout->addLayout(recordButtonsLayout);

    QHBoxLayout *playbackLayout = new QHBoxLayout();
    m_playBufferButton = new QPushButton("Play", central);
    m_pauseBufferButton = new QPushButton("Pause", central);
    m_stepBackwardButton = new QPushButton("<", central);
    m_stepForwardButton = new QPushButton(">", central);
    m_playbackSpeedSelector = new QComboBox(central);
    m_playbackSpeedSelector->addItem("1/1", 1);
    m_playbackSpeedSelector->addItem("1/2", 2);
    m_playbackSpeedSelector->addItem("1/4", 4);
    m_playbackSpeedSelector->addItem("1/8", 8);
    m_playbackSpeedSelector->addItem("1/16", 16);
    playbackLayout->addWidget(m_playBufferButton);
    playbackLayout->addWidget(m_pauseBufferButton);
    playbackLayout->addWidget(m_stepBackwardButton);
    playbackLayout->addWidget(m_stepForwardButton);
    playbackLayout->addWidget(m_playbackSpeedSelector);
    layout->addLayout(playbackLayout);

    m_playbackPositionSlider = new QSlider(Qt::Horizontal, central);
    m_playbackPositionSlider->setRange(0, 0);
    layout->addWidget(m_playbackPositionSlider);

    m_previewLabel = new QLabel(central);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setText("Initializing camera...");
    layout->addWidget(m_previewLabel, 1);

    setCentralWidget(central);

    m_videoSink = new QVideoSink(this);
    m_captureSession.setVideoSink(m_videoSink);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &RouletteCameraCapture::onVideoFrameChanged);
    connect(m_cameraSelector, &QComboBox::currentIndexChanged, this, &RouletteCameraCapture::onCameraSelectionChanged);
    connect(m_startRecordButton, &QPushButton::clicked, this, &RouletteCameraCapture::onStartRecording);
    connect(m_stopRecordButton, &QPushButton::clicked, this, &RouletteCameraCapture::onStopRecording);
    connect(m_saveBufferButton, &QPushButton::clicked, this, &RouletteCameraCapture::onSaveBuffer);
    connect(m_playBufferButton, &QPushButton::clicked, this, &RouletteCameraCapture::onPlayBuffer);
    connect(m_pauseBufferButton, &QPushButton::clicked, this, &RouletteCameraCapture::onPauseBuffer);
    connect(m_stepBackwardButton, &QPushButton::clicked, this, &RouletteCameraCapture::onStepBackward);
    connect(m_stepForwardButton, &QPushButton::clicked, this, &RouletteCameraCapture::onStepForward);
    connect(m_playbackSpeedSelector, &QComboBox::currentIndexChanged, this, &RouletteCameraCapture::onPlaybackSpeedChanged);
    connect(m_playbackPositionSlider, &QSlider::valueChanged, this, &RouletteCameraCapture::onPlaybackSliderChanged);

    m_previewTimer.setInterval(100);
    connect(&m_previewTimer, &QTimer::timeout, this, &RouletteCameraCapture::updatePreview);
    m_previewTimer.start();

    m_playbackTimer.setInterval(playbackIntervalMs());
    connect(&m_playbackTimer, &QTimer::timeout, this, &RouletteCameraCapture::onPlaybackTick);

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
        m_startRecordButton->setEnabled(false);
        m_stopRecordButton->setEnabled(false);
        m_saveBufferButton->setEnabled(false);
        m_playBufferButton->setEnabled(false);
        m_pauseBufferButton->setEnabled(false);
        m_stepBackwardButton->setEnabled(false);
        m_stepForwardButton->setEnabled(false);
        m_playbackSpeedSelector->setEnabled(false);
        m_playbackPositionSlider->setEnabled(false);
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
    m_isBuffering = false;
    m_isPlaybackActive = false;
    m_isPlaybackPaused = false;
    m_playbackTimer.stop();
    m_playbackIndex = 0;

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
    {
        QMutexLocker locker(&m_frameMutex);
        m_frameBuffer.clear();
    }
    {
        QSignalBlocker blocker(m_playbackPositionSlider);
        m_playbackPositionSlider->setRange(0, 0);
        m_playbackPositionSlider->setValue(0);
    }
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

void RouletteCameraCapture::onStartRecording()
{
    if (m_camera == nullptr)
    {
        return;
    }

    m_playbackTimer.stop();
    m_isPlaybackActive = false;
    m_isPlaybackPaused = false;
    m_playbackIndex = 0;

    {
        QMutexLocker locker(&m_frameMutex);
        m_frameBuffer.clear();
    }
    {
        QSignalBlocker blocker(m_playbackPositionSlider);
        m_playbackPositionSlider->setRange(0, 0);
        m_playbackPositionSlider->setValue(0);
    }

    m_isBuffering = true;
    m_bufferTimer.restart();

    qDebug().noquote() << QString("BufferStart | Camera=%1 | DurationMs=%2")
        .arg(m_currentCameraName)
        .arg(m_bufferDurationMs);

    updateStatusMessage();
}

void RouletteCameraCapture::onStopRecording()
{
    m_isBuffering = false;

    int bufferedFrameCount = 0;
    qint64 bufferedDurationMs = 0;
    {
        QMutexLocker locker(&m_frameMutex);
        bufferedFrameCount = m_frameBuffer.size();
        if (!m_frameBuffer.isEmpty())
        {
            bufferedDurationMs = m_frameBuffer.back().timestampMs - m_frameBuffer.front().timestampMs;
        }
    }

    qDebug().noquote() << QString("BufferStop | Frames=%1 | DurationMs=%2")
        .arg(bufferedFrameCount)
        .arg(bufferedDurationMs);

    updateStatusMessage();
}

void RouletteCameraCapture::onSaveBuffer()
{
    QQueue<BufferedFrame> framesToSave;
    {
        QMutexLocker locker(&m_frameMutex);
        framesToSave = m_frameBuffer;
    }

    if (framesToSave.isEmpty())
    {
        statusBar()->showMessage("No buffered frames to save.");
        qDebug().noquote() << "BufferSave | No frames";
        return;
    }

    QString baseDirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDirPath.isEmpty())
    {
        baseDirPath = QDir::currentPath();
    }

    QDir baseDir(baseDirPath);
    if (!baseDir.exists() && !baseDir.mkpath("."))
    {
        statusBar()->showMessage("Failed to create output directory.");
        qDebug().noquote() << QString("BufferSave | Failed to create base directory: %1").arg(baseDirPath);
        return;
    }

    const QString sessionDirName = QString("Buffer_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
    if (!baseDir.mkdir(sessionDirName))
    {
        statusBar()->showMessage("Failed to create buffer session directory.");
        qDebug().noquote() << QString("BufferSave | Failed to create session directory: %1").arg(sessionDirName);
        return;
    }

    const QString sessionDirPath = baseDir.filePath(sessionDirName);
    QDir sessionDir(sessionDirPath);

    int savedCount = 0;
    int frameIndex = 0;
    for (const BufferedFrame &bufferedFrame : framesToSave)
    {
        const QString fileName = QString("frame_%1_%2.jpg")
            .arg(frameIndex, 6, 10, QChar('0'))
            .arg(bufferedFrame.timestampMs, 6, 10, QChar('0'));
        if (bufferedFrame.image.save(sessionDir.filePath(fileName), "JPG", 90))
        {
            ++savedCount;
        }
        ++frameIndex;
    }

    statusBar()->showMessage(QString("Saved %1/%2 frames to %3")
        .arg(savedCount)
        .arg(framesToSave.size())
        .arg(sessionDirPath));

    qDebug().noquote() << QString("BufferSave | Saved=%1/%2 | Dir=%3")
        .arg(savedCount)
        .arg(framesToSave.size())
        .arg(sessionDirPath);
}

void RouletteCameraCapture::onPlayBuffer()
{
    if (m_isBuffering)
    {
        return;
    }

    int frameCount = 0;
    {
        QMutexLocker locker(&m_frameMutex);
        frameCount = m_frameBuffer.size();
    }

    if (frameCount <= 0)
    {
        return;
    }

    if (m_playbackIndex < 0 || m_playbackIndex >= frameCount)
    {
        m_playbackIndex = 0;
    }

    m_isPlaybackActive = true;
    m_isPlaybackPaused = false;
    m_playbackTimer.setInterval(playbackIntervalMs());
    m_playbackTimer.start();
    showBufferedFrameAt(m_playbackIndex);
    updateStatusMessage();
}

void RouletteCameraCapture::onPauseBuffer()
{
    if (!m_isPlaybackActive)
    {
        return;
    }

    m_playbackTimer.stop();
    m_isPlaybackPaused = true;
    updateStatusMessage();
}

void RouletteCameraCapture::onStepBackward()
{
    int frameCount = 0;
    {
        QMutexLocker locker(&m_frameMutex);
        frameCount = m_frameBuffer.size();
    }

    if (frameCount <= 0)
    {
        return;
    }

    m_playbackTimer.stop();
    m_isPlaybackActive = true;
    m_isPlaybackPaused = true;
    m_playbackIndex = qMax(0, m_playbackIndex - 1);
    showBufferedFrameAt(m_playbackIndex);
    updateStatusMessage();
}

void RouletteCameraCapture::onStepForward()
{
    int frameCount = 0;
    {
        QMutexLocker locker(&m_frameMutex);
        frameCount = m_frameBuffer.size();
    }

    if (frameCount <= 0)
    {
        return;
    }

    m_playbackTimer.stop();
    m_isPlaybackActive = true;
    m_isPlaybackPaused = true;
    m_playbackIndex = qMin(frameCount - 1, m_playbackIndex + 1);
    showBufferedFrameAt(m_playbackIndex);
    updateStatusMessage();
}

void RouletteCameraCapture::onPlaybackTick()
{
    if (!m_isPlaybackActive || m_isPlaybackPaused)
    {
        return;
    }

    int frameCount = 0;
    {
        QMutexLocker locker(&m_frameMutex);
        frameCount = m_frameBuffer.size();
    }

    if (frameCount <= 0)
    {
        m_playbackTimer.stop();
        m_isPlaybackActive = false;
        updateStatusMessage();
        return;
    }

    if (m_playbackIndex >= frameCount - 1)
    {
        m_playbackTimer.stop();
        m_isPlaybackPaused = true;
        updateStatusMessage();
        return;
    }

    ++m_playbackIndex;
    showBufferedFrameAt(m_playbackIndex);
    updateStatusMessage();
}

void RouletteCameraCapture::onPlaybackSpeedChanged(int)
{
    m_playbackTimer.setInterval(playbackIntervalMs());
    updateStatusMessage();
}

void RouletteCameraCapture::onPlaybackSliderChanged(int value)
{
    int frameCount = 0;
    {
        QMutexLocker locker(&m_frameMutex);
        frameCount = m_frameBuffer.size();
    }

    if (frameCount <= 0)
    {
        return;
    }

    m_playbackTimer.stop();
    m_isPlaybackActive = true;
    m_isPlaybackPaused = true;
    m_playbackIndex = qBound(0, value, frameCount - 1);
    showBufferedFrameAt(m_playbackIndex);
    updateStatusMessage();
}

void RouletteCameraCapture::showBufferedFrameAt(int index)
{
    QImage frame;
    {
        QMutexLocker locker(&m_frameMutex);
        if (index < 0 || index >= m_frameBuffer.size())
        {
            return;
        }
        frame = m_frameBuffer.at(index).image;
    }

    if (frame.isNull())
    {
        return;
    }

    m_playbackIndex = index;
    {
        QSignalBlocker blocker(m_playbackPositionSlider);
        m_playbackPositionSlider->setValue(index);
    }

    m_previewLabel->setPixmap(QPixmap::fromImage(frame).scaled(
        m_previewLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
}

int RouletteCameraCapture::playbackIntervalMs() const
{
    int speedDivisor = 1;
    if (m_playbackSpeedSelector != nullptr)
    {
        speedDivisor = m_playbackSpeedSelector->currentData().toInt();
        if (speedDivisor <= 0)
        {
            speedDivisor = 1;
        }
    }

    qreal baseFps = 30.0;
    if (m_measuredCaptureFps > 1.0)
    {
        baseFps = m_measuredCaptureFps;
    }
    else if (m_selectedMaxFrameRate > 1.0)
    {
        baseFps = m_selectedMaxFrameRate;
    }

    const qreal playbackFps = qMax<qreal>(1.0, baseFps / static_cast<qreal>(speedDivisor));
    return qMax(1, static_cast<int>(1000.0 / playbackFps));
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

        if (m_isBuffering)
        {
            const qint64 nowMs = m_bufferTimer.elapsed();
            m_frameBuffer.enqueue({ nowMs, image });

            while (!m_frameBuffer.isEmpty() && (nowMs - m_frameBuffer.head().timestampMs) > m_bufferDurationMs)
            {
                m_frameBuffer.dequeue();
            }
        }

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
    if (m_isPlaybackActive)
    {
        return;
    }

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
    int bufferedFrameCount = 0;
    qint64 bufferedDurationMs = 0;
    bool hasBufferedFrames = false;
    {
        QMutexLocker locker(&m_frameMutex);
        bufferedFrameCount = m_frameBuffer.size();
        hasBufferedFrames = !m_frameBuffer.isEmpty();
        if (hasBufferedFrames)
        {
            bufferedDurationMs = m_frameBuffer.back().timestampMs - m_frameBuffer.front().timestampMs;
        }
    }

    const bool hasCamera = !m_cameraDevices.isEmpty();
    const bool canPlayback = hasBufferedFrames && !m_isBuffering;
    const QString modeInfo = m_isBuffering
        ? QString("BUF")
        : (m_isPlaybackActive ? (m_isPlaybackPaused ? QString("PLAY:PAUSE") : QString("PLAY")) : QString("LIVE"));

    m_startRecordButton->setEnabled(!m_isBuffering && hasCamera);
    m_stopRecordButton->setEnabled(m_isBuffering);
    m_saveBufferButton->setEnabled(canPlayback);
    m_playBufferButton->setEnabled(canPlayback);
    m_pauseBufferButton->setEnabled(m_isPlaybackActive);
    m_stepBackwardButton->setEnabled(canPlayback);
    m_stepForwardButton->setEnabled(canPlayback);
    m_playbackSpeedSelector->setEnabled(canPlayback);
    m_playbackPositionSlider->setEnabled(canPlayback);

    const int sliderMax = hasBufferedFrames ? (bufferedFrameCount - 1) : 0;
    const int clampedIndex = qBound(0, m_playbackIndex, sliderMax);
    m_playbackIndex = clampedIndex;
    {
        QSignalBlocker blocker(m_playbackPositionSlider);
        m_playbackPositionSlider->setRange(0, sliderMax);
        m_playbackPositionSlider->setValue(clampedIndex);
    }

    const QString speedText = (m_playbackSpeedSelector != nullptr) ? m_playbackSpeedSelector->currentText() : QString("1/1");
    const QString shortMessage = QString("%1 | cap:%2fps | buf:%3f/%4ms | idx:%5 | spd:%6")
        .arg(modeInfo)
        .arg(m_measuredCaptureFps, 0, 'f', 1)
        .arg(bufferedFrameCount)
        .arg(bufferedDurationMs)
        .arg(m_playbackIndex)
        .arg(speedText);

    statusBar()->showMessage(shortMessage);

    qDebug().noquote() << QString("StatusDetail | Camera=%1 | SelectedMax=%2 | Measured=%3 | Buffering=%4 | PlaybackActive=%5 | PlaybackPaused=%6 | Frames=%7 | DurationMs=%8 | Index=%9 | Speed=%10")
        .arg(m_currentCameraName)
        .arg(m_selectedMaxFrameRate, 0, 'f', 1)
        .arg(m_measuredCaptureFps, 0, 'f', 1)
        .arg(m_isBuffering ? QString("ON") : QString("OFF"))
        .arg(m_isPlaybackActive ? QString("ON") : QString("OFF"))
        .arg(m_isPlaybackPaused ? QString("ON") : QString("OFF"))
        .arg(bufferedFrameCount)
        .arg(bufferedDurationMs)
        .arg(m_playbackIndex)
        .arg(speedText);
}

