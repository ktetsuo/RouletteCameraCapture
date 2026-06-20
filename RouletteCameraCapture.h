#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QLabel>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraDevice>
#include <QtMultimedia/QMediaCaptureSession>
#include <QtMultimedia/QVideoSink>
#include <QtGui/QImage>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtCore/QList>
#include <QtCore/QQueue>
#include <QtCore/QElapsedTimer>
#include <QtCore/QString>
#include "ui_RouletteCameraCapture.h"

class RouletteCameraCapture : public QMainWindow
{
    Q_OBJECT

public:
    RouletteCameraCapture(QWidget *parent = nullptr);
    ~RouletteCameraCapture();

private:
    void setupCamera();
    void startCamera(const QCameraDevice &cameraDevice);
    int findSavedCameraIndex() const;
    void saveSelectedCamera(const QCameraDevice &cameraDevice);
    void onCameraSelectionChanged(int index);
    void onStartRecording();
    void onStopRecording();
    void onSaveBuffer();
    void onPlayBuffer();
    void onPauseBuffer();
    void onStepBackward();
    void onStepForward();
    void onPlaybackTick();
    void onPlaybackSpeedChanged(int index);
    void onVideoFrameChanged(const QVideoFrame &frame);
    void updatePreview();
    void updateStatusMessage();
    void showBufferedFrameAt(int index);
    int playbackIntervalMs() const;

private:
    struct BufferedFrame
    {
        qint64 timestampMs;
        QImage image;
    };

    Ui::RouletteCameraCaptureClass ui;
    QComboBox *m_cameraSelector = nullptr;
    QPushButton *m_startRecordButton = nullptr;
    QPushButton *m_stopRecordButton = nullptr;
    QPushButton *m_saveBufferButton = nullptr;
    QPushButton *m_playBufferButton = nullptr;
    QPushButton *m_pauseBufferButton = nullptr;
    QPushButton *m_stepBackwardButton = nullptr;
    QPushButton *m_stepForwardButton = nullptr;
    QComboBox *m_playbackSpeedSelector = nullptr;
    QLabel *m_previewLabel = nullptr;
    QList<QCameraDevice> m_cameraDevices;
    QCamera *m_camera = nullptr;
    QMediaCaptureSession m_captureSession;
    QVideoSink *m_videoSink = nullptr;
    QImage m_latestFrame;
    QMutex m_frameMutex;
    QTimer m_previewTimer;
    QElapsedTimer m_captureFpsTimer;
    QElapsedTimer m_bufferTimer;
    int m_captureFrameCount = 0;
    qreal m_measuredCaptureFps = 0.0;
    qreal m_selectedMaxFrameRate = 0.0;
    QString m_currentCameraName;
    bool m_isBuffering = false;
    bool m_isPlaybackActive = false;
    bool m_isPlaybackPaused = false;
    int m_playbackIndex = 0;
    int m_bufferDurationMs = 1000;
    QQueue<BufferedFrame> m_frameBuffer;
    QTimer m_playbackTimer;
};

