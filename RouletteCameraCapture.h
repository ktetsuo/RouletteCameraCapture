#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QLabel>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>
#include <QtMultimedia/QCamera>
#include <QtMultimedia/QCameraDevice>
#include <QtMultimedia/QMediaCaptureSession>
#include <QtMultimedia/QMediaRecorder>
#include <QtMultimedia/QVideoSink>
#include <QtGui/QImage>
#include <QtCore/QMutex>
#include <QtCore/QTimer>
#include <QtCore/QList>
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
    void onVideoFrameChanged(const QVideoFrame &frame);
    void updatePreview();
    void updateStatusMessage();

private:
    Ui::RouletteCameraCaptureClass ui;
    QComboBox *m_cameraSelector = nullptr;
    QPushButton *m_startRecordButton = nullptr;
    QPushButton *m_stopRecordButton = nullptr;
    QLabel *m_previewLabel = nullptr;
    QList<QCameraDevice> m_cameraDevices;
    QCamera *m_camera = nullptr;
    QMediaCaptureSession m_captureSession;
    QMediaRecorder *m_recorder = nullptr;
    QVideoSink *m_videoSink = nullptr;
    QImage m_latestFrame;
    QMutex m_frameMutex;
    QTimer m_previewTimer;
    QElapsedTimer m_captureFpsTimer;
    int m_captureFrameCount = 0;
    qreal m_measuredCaptureFps = 0.0;
    qreal m_selectedMaxFrameRate = 0.0;
    qreal m_recordingFrameRate = 0.0;
    QString m_currentCameraName;
    QString m_lastRecordingPath;
    QString m_lastRecorderError;
};

