#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QLabel>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraDevice>
#include <QtMultimedia/QMediaCaptureSession>
#include <QtMultimedia/QVideoSink>
#include <QtSerialPort/QSerialPort>
#include <QtGui/QImage>
#include <QtCore/QRecursiveMutex>
#include <QtCore/QTimer>
#include <QtCore/QList>
#include <QtCore/QQueue>
#include <QtCore/QElapsedTimer>
#include <QtCore/QString>
#include "ui_RouletteCameraCapture.h"

/// @brief ルーレット制御装置の制御状態
enum class ControlState
{
    IDLE,             // 待機状態 (0)
    ACCELERATING,     // 加速中 (1)
    WAITING_TRIGGER1, // トリガーセンサー1待ち (2)
    WAITING_TRIGGER2, // トリガーセンサー2待ち (3)
    TARGETING,        // 目標位置に向けて制御中 (4) ← 撮影対象
    DECELERATING,     // 減速中 (5)
};

class RouletteCameraCapture : public QMainWindow
{
    Q_OBJECT

public:
    RouletteCameraCapture(QWidget *parent = nullptr);
    ~RouletteCameraCapture();

    signals:
    void serialTriggerChanged(bool active);
    void bufferFull();

private:
    void setupCamera();
    void startCamera(const QCameraDevice &cameraDevice);
    int findSavedCameraIndex() const;
    void saveSelectedCamera(const QCameraDevice &cameraDevice);
    QString findSavedSerialPort() const;
    void saveSelectedSerialPort(const QString &portName);
    void onCameraSelectionChanged(int index);
    void onStartRecording();
    void onStopRecording();
    void onSaveBuffer();
    void onSeekBufferStart();
    void onPlayPauseBuffer();
    void onStepBackward();
    void onStepForward();
    void onPlaybackTick();
    void onPlaybackSpeedChanged(int index);
    void onPlaybackSliderChanged(int value);
    void refreshSerialPorts();
    void onToggleSerialPort();
    void onSerialReadyRead();
    void onSerialTriggerChanged(bool active);
    void onSendSerialByButton();
    void onSendSerialByEnter();
    void sendSerialLine(bool clearInputAfterSend);
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
    QComboBox *m_serialPortSelector = nullptr;
    QComboBox *m_baudRateSelector = nullptr;
    QPushButton *m_refreshSerialPortsButton = nullptr;
    QPushButton *m_toggleSerialPortButton = nullptr;
    QLineEdit *m_serialSendEdit = nullptr;
    QPushButton *m_serialSendButton = nullptr;
    QPushButton *m_startRecordButton = nullptr;
    QPushButton *m_stopRecordButton = nullptr;
    QPushButton *m_saveBufferButton = nullptr;
    QPushButton *m_seekStartButton = nullptr;
    QPushButton *m_playPauseBufferButton = nullptr;
    QPushButton *m_stepBackwardButton = nullptr;
    QPushButton *m_stepForwardButton = nullptr;
    QComboBox *m_playbackSpeedSelector = nullptr;
    QSlider *m_playbackPositionSlider = nullptr;
    QLabel *m_previewLabel = nullptr;
    QLabel *m_playbackTimestampLabel = nullptr;
    QList<QCameraDevice> m_cameraDevices;
    QCamera *m_camera = nullptr;
    QMediaCaptureSession m_captureSession;
    QVideoSink *m_videoSink = nullptr;
    QSerialPort *m_serialPort = nullptr;
    QString m_serialRxPending;
    QQueue<QString> m_serialReceivedLines;
    int m_maxSerialReceivedLines = 512;
    int m_lastControlState = -1;
    QImage m_latestFrame;
    QRecursiveMutex m_frameMutex;
    QTimer m_previewTimer;
    QElapsedTimer m_captureFpsTimer;
    QElapsedTimer m_bufferTimer;
    int m_captureFrameCount = 0;
    qreal m_measuredCaptureFps = 0.0;
    qreal m_selectedMaxFrameRate = 0.0;
    QString m_currentCameraName;
    bool m_isBuffering = false;
    bool m_isBufferFull = false;
    bool m_isPlaybackActive = false;
    bool m_isPlaybackPaused = false;
    int m_playbackIndex = 0;
    int m_bufferDurationMs = 750;
    int m_preTriggerDurationMs = 250;
    qint64 m_recordingStartTimestampMs = -1;
    qint64 m_triggerTimestampMs = -1;
    QQueue<BufferedFrame> m_liveFrameBuffer;
    QQueue<BufferedFrame> m_frameBuffer;
    QTimer m_playbackTimer;
};

