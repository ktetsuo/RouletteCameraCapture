#include "stdafx.h"
#include "RouletteCameraCapture.h"

#include <QtMultimedia/QMediaDevices>
#include <QtMultimedia/QCameraFormat>
#include <QtMultimedia/QMediaFormat>
#include <QtMultimedia/QVideoFrame>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QMutexLocker>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>
#include <QtGui/QPixmap>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace
{
QString fileExtensionForFormat(QMediaFormat::FileFormat fileFormat)
{
    switch (fileFormat)
    {
    case QMediaFormat::MPEG4:
        return "mp4";
    case QMediaFormat::Matroska:
        return "mkv";
    case QMediaFormat::AVI:
        return "avi";
    case QMediaFormat::QuickTime:
        return "mov";
    case QMediaFormat::Mpeg4Audio:
        return "m4a";
    case QMediaFormat::AAC:
        return "aac";
    case QMediaFormat::WMA:
        return "wma";
    case QMediaFormat::WMV:
        return "wmv";
    default:
        return "mp4";
    }
}
}

RouletteCameraCapture::RouletteCameraCapture(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);

    m_cameraSelector = new QComboBox(central);
    layout->addWidget(m_cameraSelector);

    QHBoxLayout *recordButtonsLayout = new QHBoxLayout();
    m_startRecordButton = new QPushButton("Start Recording", central);
    m_stopRecordButton = new QPushButton("Stop Recording", central);
    m_stopRecordButton->setEnabled(false);
    recordButtonsLayout->addWidget(m_startRecordButton);
    recordButtonsLayout->addWidget(m_stopRecordButton);
    layout->addLayout(recordButtonsLayout);

    m_previewLabel = new QLabel(central);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setText("Initializing camera...");
    layout->addWidget(m_previewLabel, 1);

    setCentralWidget(central);

    m_videoSink = new QVideoSink(this);
    m_recorder = new QMediaRecorder(this);
    m_captureSession.setVideoSink(m_videoSink);
    m_captureSession.setRecorder(m_recorder);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &RouletteCameraCapture::onVideoFrameChanged);
    connect(m_cameraSelector, &QComboBox::currentIndexChanged, this, &RouletteCameraCapture::onCameraSelectionChanged);
    connect(m_startRecordButton, &QPushButton::clicked, this, &RouletteCameraCapture::onStartRecording);
    connect(m_stopRecordButton, &QPushButton::clicked, this, &RouletteCameraCapture::onStopRecording);
    connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState)
        {
            if (m_recorder != nullptr && m_recorder->error() == QMediaRecorder::NoError)
            {
                m_lastRecorderError.clear();
            }
            updateStatusMessage();
        });
    connect(m_recorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &errorString)
        {
            m_lastRecorderError = errorString;
            updateStatusMessage();
        });

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
        m_startRecordButton->setEnabled(false);
        m_stopRecordButton->setEnabled(false);
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
    if (m_recorder != nullptr && m_recorder->recorderState() == QMediaRecorder::RecordingState)
    {
        m_recorder->stop();
    }

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
    m_recordingFrameRate = 0.0;
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
    if (m_recorder == nullptr || m_camera == nullptr)
    {
        return;
    }

    QMediaFormat mediaFormat;
    const QList<QMediaFormat::FileFormat> supportedFileFormats = mediaFormat.supportedFileFormats(QMediaFormat::Encode);

    QMediaFormat::FileFormat selectedFileFormat = QMediaFormat::MPEG4;
    if (!supportedFileFormats.contains(selectedFileFormat))
    {
        if (supportedFileFormats.contains(QMediaFormat::Matroska))
        {
            selectedFileFormat = QMediaFormat::Matroska;
        }
        else if (!supportedFileFormats.isEmpty())
        {
            selectedFileFormat = supportedFileFormats.first();
        }
    }
    mediaFormat.setFileFormat(selectedFileFormat);

    const QList<QMediaFormat::VideoCodec> supportedVideoCodecs = mediaFormat.supportedVideoCodecs(QMediaFormat::Encode);
    if (supportedVideoCodecs.contains(QMediaFormat::VideoCodec::H264))
    {
        mediaFormat.setVideoCodec(QMediaFormat::VideoCodec::H264);
    }
    else if (!supportedVideoCodecs.isEmpty())
    {
        mediaFormat.setVideoCodec(supportedVideoCodecs.first());
    }

    m_recorder->setMediaFormat(mediaFormat);

    qreal safeRecordingFrameRate = 30.0;
    if (m_selectedMaxFrameRate >= 120.0)
    {
        safeRecordingFrameRate = 120.0;
    }
    else if (m_selectedMaxFrameRate >= 60.0)
    {
        safeRecordingFrameRate = 60.0;
    }
    else if (m_selectedMaxFrameRate >= 30.0)
    {
        safeRecordingFrameRate = 30.0;
    }
    else if (m_selectedMaxFrameRate > 1.0)
    {
        safeRecordingFrameRate = m_selectedMaxFrameRate;
    }

    m_recordingFrameRate = safeRecordingFrameRate;
    m_recorder->setVideoFrameRate(m_recordingFrameRate);

    QString moviesDirPath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (moviesDirPath.isEmpty())
    {
        moviesDirPath = QDir::currentPath();
    }

    QDir moviesDir(moviesDirPath);
    if (!moviesDir.exists())
    {
        moviesDir.mkpath(".");
    }

    const QString fileExtension = fileExtensionForFormat(selectedFileFormat);
    m_lastRecordingPath = moviesDir.filePath(
        QString("RouletteCapture_%1.%2").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"), fileExtension));
    m_lastRecorderError.clear();
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_lastRecordingPath));
    m_recorder->record();

    updateStatusMessage();
}

void RouletteCameraCapture::onStopRecording()
{
    if (m_recorder == nullptr)
    {
        return;
    }

    m_recorder->stop();
    m_startRecordButton->setEnabled(!m_cameraDevices.isEmpty());
    m_stopRecordButton->setEnabled(false);
    updateStatusMessage();
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
    const bool isRecording = (m_recorder != nullptr && m_recorder->recorderState() == QMediaRecorder::RecordingState);
    const QString recordingInfo = isRecording
        ? QString("REC: ON")
        : QString("REC: OFF");

    m_startRecordButton->setEnabled(!isRecording && !m_cameraDevices.isEmpty());
    m_stopRecordButton->setEnabled(isRecording);

    QString message = QString("Camera: %1 | Capture target: 260 fps, selected max format: %2 fps, measured arrival: %3 fps, record fps: %4, preview: 10 fps | %5")
        .arg(m_currentCameraName)
        .arg(m_selectedMaxFrameRate, 0, 'f', 1)
        .arg(m_measuredCaptureFps, 0, 'f', 1)
        .arg(m_recordingFrameRate, 0, 'f', 1)
        .arg(recordingInfo);

    if (!m_lastRecordingPath.isEmpty())
    {
        message += QString(" | File: %1").arg(m_lastRecordingPath);
    }

    if (!m_lastRecorderError.isEmpty())
    {
        message += QString(" | Error: %1").arg(m_lastRecorderError);
    }

    statusBar()->showMessage(message);
}

