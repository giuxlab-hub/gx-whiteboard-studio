// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#pragma once
#include <QObject>
#include <QImage>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <functional>
#include <QDateTime>

class RecordingEngine final : public QObject {
public:
    struct AudioInput {
        QString name;
        QString description;
    };
    struct Options {
        QString output;
        QSize size = QSize(1920,1080);
        int fps = 30;
        bool microphoneEnabled = false;
        QString microphoneSource;
        // Test-only: a generated tone. This must never activate the real mic.
        bool syntheticAudio = false;
        QString encoder = "libx264";
        bool convertToMp4 = false;
        int volumePercent = 100;
    };
    explicit RecordingEngine(QObject* parent=nullptr);
    ~RecordingEngine() override;
    static QVector<AudioInput> listMicrophones();
    static bool nvencAvailable();
    bool start(const Options& options, QString* error=nullptr);
    void stop();
    bool active() const;
    bool finishing() const {return stopping_ || conversionPending_;}
    bool muted() const {return muted_;}
    bool usingMicrophone() const {return options_.microphoneEnabled;}
    bool setMuted(bool mute, QString* error=nullptr);
    bool setCaptureVolume(int percent, QString* error=nullptr);
    int captureVolume() const {return volume_;}
    int levelPercent() const {return levelPercent_;}
    int maxLevelPercent() const {return maxLevelPercent_;}
    int frameCount() const {return framesWritten_;}
    int droppedFrames() const {return droppedFrames_;}
    qint64 elapsedMs() const {return elapsed_.isValid()?elapsed_.elapsed():0;}
    QString outputFile() const {return output_;}
    QString lastError() const {return lastError_;}
    QString encoderUsed() const {return encoderUsed_;}
    QString diagnosticPath() const {return output_.isEmpty()?QString():output_+".recording-report.json";}
    QStringList recentWarnings() const {return warnings_;}
    bool abnormalTermination() const {return unexpectedTermination_;}
    static int diskSpaceStatus(qint64 availableBytes); // 0=ok, 1=warn, 2=stop
    void simulateEncoderCrashForTest(); // only invoked by regression tests
    // The owning QMainWindow provides this on the GUI thread; the callback
    // composites only the document and never sees panels or the cursor.
    std::function<QImage(const QSize&)> renderFrame;
    std::function<void()> changed;
    std::function<void(bool success,const QString& message)> completed;
    std::function<void(const QString& message)> warning;
private:
    void writeFrame();
    void readDiagnostics();
    void processFinished(int exitCode,QProcess::ExitStatus status);
    bool controlPulseSourceOutput(const QString& operation,int value,QString* error);
    void setLevelDb(double value);
    void finishMp4();
    void appendWarning(const QString& description);
    void inspectStorage();
    void writeReport(const QString& result,int exitCode,
                     const QString& detail=QString()) const;
    static QString sanitizeDiagnostic(QString text);
    QProcess ffmpeg_;
    QTimer timer_;
    QTimer storageTimer_;
    QElapsedTimer elapsed_;
    Options options_;
    QString output_;
    QString lastError_;
    QString encoderUsed_;
    QString bufferedLog_;
    QStringList warnings_;
    QStringList recentLog_;
    QDateTime startedUtc_;
    qint64 lastObservedBytes_ = 0;
    int staleOutputChecks_ = 0;
    bool unexpectedTermination_ = false;
    bool lowDiskWarningIssued_ = false;
    bool stoppedForLowDisk_ = false;
    bool stoppedForRendererError_ = false;
    qint64 lastPactlPid_ = -1;
    int framesWritten_ = 0;
    int droppedFrames_ = 0;
    int levelPercent_ = 0;
    int maxLevelPercent_ = 0;
    int volume_ = 100;
    bool muted_ = false;
    bool stopping_ = false;
    bool recording_ = false;
    bool conversionPending_ = false;
};
