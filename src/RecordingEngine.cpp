// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#include "RecordingEngine.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDebug>
#include <QImage>
#include <QSaveFile>
#include <QDateTime>
#include <cmath>
#include <algorithm>
#include <memory>
#include <QRegularExpression>

RecordingEngine::RecordingEngine(QObject* parent): QObject(parent) {
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_,&QTimer::timeout,this,[this] {writeFrame();});
    storageTimer_.setInterval(2500);
    connect(&storageTimer_,&QTimer::timeout,this,[this] {inspectStorage();});
    ffmpeg_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&ffmpeg_,&QProcess::readyReadStandardError,
            this,[this] {readDiagnostics();});
    connect(&ffmpeg_,&QProcess::errorOccurred,this,
            [this](QProcess::ProcessError error) {
        if (error==QProcess::FailedToStart ||
            error==QProcess::Crashed) {
            lastError_=QString("FFmpeg: %1").arg(ffmpeg_.errorString());
            if (changed) changed();
        }
    });
    connect(&ffmpeg_,&QProcess::finished,this,
            [this](int code,QProcess::ExitStatus status) {
        processFinished(code,status);
    });
}
RecordingEngine::~RecordingEngine() {
    completed={};
    changed={};
    warning={};
    renderFrame={};
    timer_.stop();
    storageTimer_.stop();
    ffmpeg_.disconnect(this);
    if (ffmpeg_.state()!=QProcess::NotRunning) {
        ffmpeg_.closeWriteChannel();
        if (!ffmpeg_.waitForFinished(1500)) {
            ffmpeg_.kill();
            ffmpeg_.waitForFinished(1500);
        }
    }
}
QVector<RecordingEngine::AudioInput> RecordingEngine::listMicrophones() {
    QVector<AudioInput> microphones;
    const QString pactl=QStandardPaths::findExecutable("pactl");
    if (pactl.isEmpty()) return microphones;
    QProcess proc;
    proc.start(pactl,{"--format=json","list","sources"});
    if (!proc.waitForFinished(2500) || proc.exitCode()!=0)
        return microphones;
    const auto doc=QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!doc.isArray()) return microphones;
    for (const auto& value : doc.array()) {
        const auto object=value.toObject();
        const QString name=object.value("name").toString();
        const QString description=object.value("description").toString(name);
        if (name.isEmpty() || name.endsWith(".monitor")) continue;
        microphones.push_back({name,description});
    }
    return microphones;
}
bool RecordingEngine::nvencAvailable() {
    static int cached=-1;
    if (cached>=0) return cached==1;
    const QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) return false;
    QProcess test;
    test.start(ffmpeg,{"-hide_banner","-nostdin","-loglevel","error",
       "-f","lavfi","-i","color=c=black:s=160x90:r=5",
       "-frames:v","2","-c:v","h264_nvenc","-f","null","-"});
    cached=(test.waitForFinished(3000) && test.exitCode()==0)?1:0;
    return cached==1;
}
bool RecordingEngine::start(const Options& options,QString* error) {
    if (recording_ || stopping_ || conversionPending_ ||
        ffmpeg_.state()!=QProcess::NotRunning) {
        if (error) *error="Recording is already active.";
        return false;
    }
    const QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) {
        if (error) *error="FFmpeg executable not found.";
        return false;
    }
    const auto width=options.size.width(),height=options.size.height();
    if (width<320 || height<180 || width>1920 || height>1080 ||
        (width%2)!=0 || (height%2)!=0 ||
        options.fps<1 || options.fps>30 ||
        !renderFrame) {
        if (error) *error="Invalid recording profile or renderer.";
        return false;
    }
    if (options.microphoneEnabled && options.syntheticAudio) {
        if (error) *error="Select real microphone or synthetic audio, not both.";
        return false;
    }
    if (options.microphoneEnabled && options.microphoneSource.isEmpty()) {
        if (error) *error="Select a PulseAudio microphone first.";
        return false;
    }
    const QFileInfo file(options.output);
    if (file.absoluteFilePath().isEmpty() ||
        file.suffix().compare("mkv",Qt::CaseInsensitive)!=0 ||
        !QDir(file.absolutePath()).exists() ||
        file.exists()) {
        if (error) *error="Choose an unused .mkv filename in an existing directory.";
        return false;
    }
    const QStorageInfo storage(file.absolutePath());
    if (!storage.isValid() || storage.isReadOnly() ||
        storage.bytesAvailable()<150LL*1024*1024) {
        if (error) *error="Output storage is read-only or has less than 150 MB available.";
        return false;
    }
    options_=options;
    output_=file.absoluteFilePath();
    volume_=std::clamp(options.volumePercent,0,150);
    muted_=false;
    droppedFrames_=0;framesWritten_=0;levelPercent_=0;maxLevelPercent_=0;
    conversionPending_=false;
    lastError_.clear();bufferedLog_.clear();
    warnings_.clear();recentLog_.clear();
    startedUtc_=QDateTime::currentDateTimeUtc();
    unexpectedTermination_=false;
    lowDiskWarningIssued_=false;
    stoppedForLowDisk_=false;
    stoppedForRendererError_=false;
    staleOutputChecks_=0;
    lastObservedBytes_=0;
    stopping_=false;
    const bool hasAudio=options.microphoneEnabled || options.syntheticAudio;
    encoderUsed_=(options.encoder=="h264_nvenc" && nvencAvailable())
        ? "h264_nvenc" : "libx264";
    QStringList args={
       "-hide_banner","-nostdin","-loglevel","info","-y",
       "-thread_queue_size","256",
       "-f","rawvideo","-pixel_format","bgra",
       "-video_size",QString("%1x%2").arg(width).arg(height),
       "-framerate",QString::number(options.fps),"-i","pipe:0"
    };
    if (options.syntheticAudio) {
        args << "-thread_queue_size" << "256"
             << "-re" << "-f" << "lavfi" << "-i"
             << "sine=frequency=440:sample_rate=48000";
    } else if (options.microphoneEnabled) {
        args << "-thread_queue_size" << "256"
             << "-f" << "pulse"
             << "-sample_rate" << "48000"
             << "-channels" << "2"
             << "-i" << options.microphoneSource;
    }
    args << "-map" << "0:v:0";
    if (hasAudio) args << "-map" << "1:a:0";
    args << "-c:v" << encoderUsed_ << "-pix_fmt" << "yuv420p";
    if (encoderUsed_=="h264_nvenc")
        args << "-preset" << "p4" << "-b:v" << "8M";
    else
        args << "-preset" << "veryfast" << "-crf" << "23"
             << "-tune" << "zerolatency";
    if (hasAudio) {
        // Print peak/RMS levels to stderr; samples remain inside FFmpeg.
        // No separate microphone capture process and no audio in .gxwb.
        args << "-af"
             << "astats=metadata=1:reset=1,ametadata=print:key=lavfi.astats.Overall.RMS_level"
             << "-c:a" << "aac" << "-ar" << "48000" << "-b:a" << "128k"
             << "-shortest";
    }
    args << "-f" << "matroska" << output_;
    ffmpeg_.setProgram(ffmpeg);
    ffmpeg_.setArguments(args);
    ffmpeg_.start(QIODevice::ReadWrite);
    if (!ffmpeg_.waitForStarted(2000)) {
        lastError_=ffmpeg_.errorString();
        if (error) *error=lastError_;
        return false;
    }
    recording_=true;
    elapsed_.start();
    timer_.start(std::max(1,1000/options.fps));
    storageTimer_.start();
    // Prime first frame now rather than waiting for the timer cycle.
    writeFrame();
    if (options.microphoneEnabled) {
        // Source-output may appear after FFmpeg initializes PulseAudio.
        // Retry without ever changing the system-wide microphone volume.
        auto* volumeSetup=new QTimer(this);
        volumeSetup->setInterval(400);
        connect(volumeSetup,&QTimer::timeout,this,
                [this,volumeSetup,attempts=0]() mutable {
            if (!active() || !options_.microphoneEnabled) {
                volumeSetup->stop();
                volumeSetup->deleteLater();
                return;
            }
            QString ignored;
            if (setCaptureVolume(volume_,&ignored) || ++attempts>=8) {
                volumeSetup->stop();
                volumeSetup->deleteLater();
            }
        });
        QTimer::singleShot(550,volumeSetup,[volumeSetup] {
            volumeSetup->start();
        });
    }
    if (changed) changed();
    return true;
}
bool RecordingEngine::active() const {
    return recording_ && !stopping_ &&
           ffmpeg_.state()!=QProcess::NotRunning;
}
void RecordingEngine::writeFrame() {
    if (!active() || !renderFrame) return;
    const qint64 bytesPerFrame=qint64(options_.size.width())*
                               options_.size.height()*4;
    // Bound buffered data so FFmpeg can never consume unlimited RAM.
    // Encoder startup and first audio initialization can buffer several
    // frames; eight 1080p BGRA frames use at most about 64 MiB.
    if (ffmpeg_.bytesToWrite()>bytesPerFrame*8) {
        ++droppedFrames_;
        if (changed) changed();
        return;
    }
    QImage image=renderFrame(options_.size);
    if (image.size()!=options_.size || image.isNull()) {
        lastError_="The canvas renderer returned an invalid frame.";
        stoppedForRendererError_=true;
        appendWarning(lastError_);
        stop();
        return;
    }
    if (image.format()!=QImage::Format_ARGB32)
        image=image.convertToFormat(QImage::Format_ARGB32);
    if (image.bytesPerLine()!=options_.size.width()*4) {
        lastError_="Unexpected image stride from canvas renderer.";
        stoppedForRendererError_=true;
        appendWarning(lastError_);
        stop();
        return;
    }
    const qint64 result=ffmpeg_.write(
        reinterpret_cast<const char*>(image.constBits()),bytesPerFrame);
    if (result!=bytesPerFrame) {
        ++droppedFrames_;
        lastError_="FFmpeg video pipe could not accept a complete frame.";
        stoppedForRendererError_=true;
        appendWarning(lastError_);
        stop();
        return;
    }
    ++framesWritten_;
    if (changed && framesWritten_%options_.fps==0)
        changed();
}
void RecordingEngine::readDiagnostics() {
    bufferedLog_+=QString::fromUtf8(ffmpeg_.readAllStandardError());
    if (bufferedLog_.size()>128*1024)
        bufferedLog_=bufferedLog_.right(64*1024);
    const QRegularExpression db(
        R"(lavfi\.astats\.Overall\.RMS_level=([-+]?\d+(?:\.\d+)?|-inf))");
    int lines=0;
    while (true) {
        const int end=bufferedLog_.indexOf('\n');
        if (end<0 || ++lines>1000) break;
        const QString line=bufferedLog_.left(end);
        bufferedLog_.remove(0,end+1);
        const QString sanitized=sanitizeDiagnostic(line);
        if (!sanitized.trimmed().isEmpty()) {
            recentLog_.append(sanitized.left(400));
            if (recentLog_.size()>48) recentLog_.removeFirst();
        }
        const auto match=db.match(line);
        if (match.hasMatch()) {
            const double value=match.captured(1)=="-inf"
                ? -100 : match.captured(1).toDouble();
            setLevelDb(value);
        } else if (line.contains("Error",Qt::CaseInsensitive) ||
                   line.contains("No such device",Qt::CaseInsensitive) ||
                   line.contains("Invalid argument",Qt::CaseInsensitive)) {
            // Retain only recent error, not full FFmpeg command with device info.
            lastError_=sanitized.left(400);
        }
    }
}
void RecordingEngine::setLevelDb(double db) {
    const int next=std::clamp(int(std::round(std::pow(10.0,db/20.0)*100.0)),
                              0,100);
    if (next==levelPercent_) return;
    levelPercent_=next;
    maxLevelPercent_=std::max(maxLevelPercent_,next);
    if (changed) changed();
}
void RecordingEngine::stop() {
    if (!recording_ || stopping_) return;
    stopping_=true;
    timer_.stop();
    storageTimer_.stop();
    ffmpeg_.closeWriteChannel();
    if (changed) changed();
    // Only force termination if finalization becomes stuck. MKV remains less
    // fragile than MP4 on abrupt interruption.
    QTimer::singleShot(10000,this,[this] {
        if (stopping_ && ffmpeg_.state()!=QProcess::NotRunning) {
            lastError_="FFmpeg did not finish within 10 seconds.";
            ffmpeg_.kill();
        }
    });
}
void RecordingEngine::processFinished(int code,QProcess::ExitStatus status) {
    readDiagnostics();
    timer_.stop();
    storageTimer_.stop();
    unexpectedTermination_=!stopping_;
    const bool valid=code==0 && status==QProcess::NormalExit &&
                     !unexpectedTermination_ &&
                     !stoppedForRendererError_ &&
                     framesWritten_>0 && QFileInfo(output_).size()>1024;
    recording_=false;stopping_=false;levelPercent_=0;
    if (!valid && lastError_.isEmpty())
        lastError_=QString("FFmpeg ended with exit code %1.").arg(code);
    if (!valid) appendWarning(
        unexpectedTermination_?"FFmpeg terminated unexpectedly.":
        "FFmpeg could not finalize the recording.");
    writeReport(valid?(stoppedForLowDisk_?"mkv_early_disk":"mkv_ok"):
                "ffmpeg_failed",code,lastError_);
    if (changed) changed();
    if (valid && options_.convertToMp4 && !stoppedForLowDisk_) {
        finishMp4();
        return;
    }
    if (completed)
        completed(valid,
            !valid ? lastError_ :
            stoppedForLowDisk_
                ? QString("Recording stopped early due to low disk space; MKV preserved: %1").arg(output_)
                : QString("MKV saved: %1").arg(output_));
}
bool RecordingEngine::controlPulseSourceOutput(const QString& operation,
                                               int value,QString* error) {
    if (!recording_ || !options_.microphoneEnabled) {
        if (error) *error="Microphone is not recording.";
        return false;
    }
    const QString pactl=QStandardPaths::findExecutable("pactl");
    if (pactl.isEmpty()) {
        if (error) *error="pactl executable not found.";
        return false;
    }
    QProcess read;
    read.start(pactl,{"--format=json","list","source-outputs"});
    if (!read.waitForFinished(1700) || read.exitCode()!=0) {
        if (error) *error="Cannot read PulseAudio capture streams.";
        return false;
    }
    const QJsonDocument document=QJsonDocument::fromJson(read.readAllStandardOutput());
    if (!document.isArray()) return false;
    const QString pid=QString::number(ffmpeg_.processId());
    int index=-1;
    for (const auto& val:document.array()) {
        const auto obj=val.toObject();
        const auto props=obj.value("properties").toObject();
        const auto process=props.value("application.process.id").toVariant().toString();
        if (process==pid) {index=obj.value("index").toInt(-1);break;}
    }
    if (index<0) {
        if (error) *error="FFmpeg microphone source output not found yet.";
        return false;
    }
    const QStringList args={
       operation=="mute"?"set-source-output-mute":"set-source-output-volume",
       QString::number(index),
       operation=="mute"?(value?"1":"0"):QString("%1%").arg(value)
    };
    const int result=QProcess::execute(pactl,args);
    if (result!=0) {
        if (error) *error="Unable to control the application microphone stream.";
        return false;
    }
    return true;
}
bool RecordingEngine::setMuted(bool mute,QString* error) {
    if (!controlPulseSourceOutput("mute",mute?1:0,error)) return false;
    muted_=mute;
    if (changed) changed();
    return true;
}
bool RecordingEngine::setCaptureVolume(int percent,QString* error) {
    percent=std::clamp(percent,0,150);
    if (!controlPulseSourceOutput("volume",percent,error)) return false;
    volume_=percent;
    if (changed) changed();
    return true;
}
void RecordingEngine::finishMp4() {
    conversionPending_=true;
    if (changed) changed();
    const QString mp4=QFileInfo(output_).absolutePath()+"/"+
        QFileInfo(output_).completeBaseName()+".mp4";
    auto* remux=new QProcess(this);
    remux->setObjectName("remuxProcess");
    remux->setProcessChannelMode(QProcess::SeparateChannels);
    // Prevent duplicate completed() callbacks if a failed start causes
    // both errorOccurred and finished.
    auto notified=std::make_shared<bool>(false);
    auto finish=[this,remux,mp4,notified](bool success,const QString& detail) {
        if (*notified) return;
        *notified=true;
        conversionPending_=false;
        if (!success) appendWarning(
            "MP4 conversion failed; the primary MKV has been preserved.");
        writeReport(success?"mkv_mp4_ok":"mp4_failed",
                    success?0:1,detail);
        if (changed) changed();
        if (completed) completed(true,
            success ? QString("MKV and MP4 saved: %1").arg(mp4)
                    : QString("MKV saved, MP4 conversion failed: %1").arg(output_));
        remux->deleteLater();
    };
    connect(remux,&QProcess::errorOccurred,this,
            [finish,remux](QProcess::ProcessError code) {
        if (code==QProcess::FailedToStart)
            finish(false,remux->errorString());
    });
    connect(remux,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this,[finish,mp4](int code,QProcess::ExitStatus state) {
        const bool good=code==0 && state==QProcess::NormalExit &&
                        QFileInfo(mp4).size()>1024;
        finish(good,good?QString():QString("ffmpeg remux exit %1").arg(code));
    });
    remux->start(QStandardPaths::findExecutable("ffmpeg"),
        {"-nostdin","-hide_banner","-loglevel","error","-n",
         "-i",output_,"-map","0","-c","copy","-movflags","+faststart",mp4});
}

QString RecordingEngine::sanitizeDiagnostic(QString text) {
    // Never write auth fragments or raw RTSP URLs into the diagnostic report.
    text.replace(QRegularExpression(
        R"((?:rtsp|rtsps|https?)://\S+)",QRegularExpression::CaseInsensitiveOption),
        "[network-source]");
    text.replace(QRegularExpression(
        R"((?:password|token|apikey|api_key|secret)=\S+)",
        QRegularExpression::CaseInsensitiveOption),"[redacted]");
    return text.left(400);
}
int RecordingEngine::diskSpaceStatus(qint64 bytes) {
    if (bytes<0) return 1; // read failure: warn but don't pretend disk is full
    if (bytes<90LL*1024*1024) return 2;
    if (bytes<300LL*1024*1024) return 1;
    return 0;
}
void RecordingEngine::appendWarning(const QString& reason) {
    const QString line=sanitizeDiagnostic(reason);
    if (!warnings_.contains(line)) {
        warnings_.append(line);
        if (warnings_.size()>30) warnings_.removeFirst();
        if (warning) warning(line);
    }
}
void RecordingEngine::inspectStorage() {
    if (!active()) return;
    QStorageInfo disk(QFileInfo(output_).absolutePath());
    if (!disk.isValid()) {
        if (!lowDiskWarningIssued_) {
            appendWarning("Could not query output disk space.");
            lowDiskWarningIssued_=true;
        }
        return;
    }
    const int status=diskSpaceStatus(disk.bytesAvailable());
    if (status>=1 && !lowDiskWarningIssued_) {
        lowDiskWarningIssued_=true;
        appendWarning("Low available recording disk space.");
    }
    if (status==2) {
        stoppedForLowDisk_=true;
        lastError_="Recording stopped early to protect the output from a full disk.";
        appendWarning(lastError_);
        stop();
    }
}
void RecordingEngine::simulateEncoderCrashForTest() {
    if (active()) ffmpeg_.kill();
}
void RecordingEngine::writeReport(const QString& result,int exitCode,
                                  const QString& detail) const {
    if (output_.isEmpty()) return;
    QJsonObject report;
    report.insert("format","GXWB-Recording-Diagnostics");
    report.insert("schema",1);
    report.insert("status",result);
    report.insert("createdUtc",startedUtc_.toString(Qt::ISODateWithMs));
    report.insert("reportedUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    report.insert("output",QFileInfo(output_).fileName());
    report.insert("container","mkv");
    report.insert("encoder",encoderUsed_);
    report.insert("width",options_.size.width());
    report.insert("height",options_.size.height());
    report.insert("fps",options_.fps);
    report.insert("microphoneEnabled",options_.microphoneEnabled);
    report.insert("syntheticAudio",options_.syntheticAudio);
    report.insert("frameCount",framesWritten_);
    report.insert("droppedFrames",droppedFrames_);
    report.insert("durationMs",elapsed_.isValid()?elapsed_.elapsed():0);
    report.insert("outputBytes",QFileInfo(output_).size());
    report.insert("unexpectedTermination",unexpectedTermination_);
    report.insert("stoppedForLowDisk",stoppedForLowDisk_);
    report.insert("stoppedForRendererError",stoppedForRendererError_);
    report.insert("encoderExitCode",exitCode);
    report.insert("detail",sanitizeDiagnostic(detail));
    QJsonArray notices,logs;
    for (const auto& line:warnings_) notices.append(line);
    for (const auto& line:recentLog_) logs.append(line);
    report.insert("warnings",notices);
    report.insert("recentFFmpegLog",logs);
    QSaveFile file(diagnosticPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot save recording diagnostic report";
        return;
    }
    const QByteArray bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (file.write(bytes)!=bytes.size() || !file.commit())
        qWarning() << "Failed to commit recording diagnostic report";
}
