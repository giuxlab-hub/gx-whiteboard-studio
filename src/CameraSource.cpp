// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#include "CameraSource.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QPointer>
#include <QDebug>
#include <QImageReader>

static constexpr int kMaxJpegBuffer = 6 * 1024 * 1024;
static constexpr int kMaxCameraPixels = 1920*1080;
CameraSource::CameraSource(QObject* parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { consume(); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (stopping_) return;
        lastError_ = QString("Camera process error %1: %2").arg(int(error)).arg(process_.errorString());
        setStatus("Error");
    });
    connect(&process_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (stopping_) {
            stopping_ = false;
            setStatus(lastError_.isEmpty() ? "Stopped" : "Disconnected");
            return;
        }
        if (exitCode || exitStatus != QProcess::NormalExit) {
            QString diagnostics=QString::fromLocal8Bit(process_.readAllStandardError());
            if (device_.startsWith("http",Qt::CaseInsensitive) ||
                device_.startsWith("rtsp",Qt::CaseInsensitive))
                diagnostics.replace(device_,"[network source]");
            lastError_ = QString("Camera pipeline stopped: code=%1, %2")
                .arg(exitCode).arg(diagnostics.left(300));
            setStatus("Disconnected");
        } else setStatus("Stopped");
    });
}
CameraSource::~CameraSource() {
    frameReady = {};
    statusChanged = {};
    stop();
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1200);
    }
}
QStringList CameraSource::listVideoDevices() {
    // V4L2 UVC cameras usually create two /dev/video* nodes:
    // index 0 is capture, index 1 is metadata. Do not duplicate the device.
    QStringList found;
    const QDir dir("/sys/class/video4linux");
    const auto entries=dir.entryList({"video*"},QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name);
    for (const auto& name : entries) {
        QFile index(dir.absoluteFilePath(name+"/index"));
        if (index.open(QIODevice::ReadOnly)) {
            if (index.readAll().trimmed() != "0") continue;
        }
        const QString device="/dev/"+name;
        if (QFileInfo::exists(device)) found << device;
    }
    return found;
}
void CameraSource::setStatus(QString value) {
    if (status_==value) return;
    status_=std::move(value);
    if (statusChanged) statusChanged(status_);
}
bool CameraSource::startV4L2(const QString& device, int width, int height, int fps, QString* error) {
    static const QRegularExpression safe("^/dev/video[0-9]+$");
    if (!safe.match(device).hasMatch() || !QFileInfo::exists(device)) {
        if (error) *error="Invalid or missing V4L2 camera.";
        return false;
    }
    return launch(device,{"-f","v4l2","-input_format","mjpeg",
        "-video_size",QString("%1x%2").arg(width).arg(height),
        "-framerate",QString::number(fps),"-i",device},width,height,fps,error);
}
bool CameraSource::startTestPattern(int width,int height,int fps,QString* error) {
    return launch("test-pattern",{"-re","-f","lavfi","-i",
        QString("testsrc2=size=%1x%2:rate=%3").arg(width).arg(height).arg(fps)},
        width,height,fps,error);
}
bool CameraSource::startNetwork(const QString& url, int width,int height,int fps,
                                QString* error) {
    // The NetworkSource layer validates scheme, hostname and credential policy.
    // This method only prepares transport arguments for FFmpeg.
    const bool rtsp=url.startsWith("rtsp://",Qt::CaseInsensitive) ||
                    url.startsWith("rtsps://",Qt::CaseInsensitive);
    QStringList input;
    if (rtsp) {
        input={"-rtsp_transport","tcp","-timeout","5000000",
               "-fflags","nobuffer","-flags","low_delay","-i",url};
    } else {
        input={"-rw_timeout","5000000","-fflags","nobuffer",
               "-flags","low_delay","-i",url};
    }
    return launch(url,input,width,height,fps,error);
}
bool CameraSource::launch(const QString& device,const QStringList& inputArgs,
                          int width,int height,int fps,QString* error) {
    if (width < 160 || height < 90 || qint64(width)*height>kMaxCameraPixels ||
        fps<1 || fps>60) {
        if (error) *error="Unsupported camera resolution or frame rate.";
        return false;
    }
    const QString ffmpeg=QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) {
        if (error) *error="FFmpeg is not installed.";
        return false;
    }
    // Do not emit an artificial "Stopped" transition before an initial
    // network connection: that would trigger a false reconnect cycle.
    if (process_.state() != QProcess::NotRunning) stop();
    bytes_.clear();
    frames_=0;
    lastError_.clear();
    device_=device;
    stopping_=false;
    QStringList args={"-hide_banner","-nostdin","-loglevel","warning",
        "-filter_threads","1"};
    args += inputArgs;
    args += {"-an","-vf",QString("fps=%1,scale=%2:%3").arg(fps).arg(width).arg(height),
             "-q:v","7","-f","image2pipe","-vcodec","mjpeg","pipe:1"};
    process_.setProgram(ffmpeg);
    process_.setArguments(args);
    setStatus("Starting");
    process_.start();
    if (!process_.waitForStarted(1500)) {
        lastError_=process_.errorString();
        if (error) *error=lastError_;
        setStatus("Error");
        return false;
    }
    // Never report a feed as live until actual decoded frames arrive.
    setStatus("Starting");
    QPointer<CameraSource> source(this);
    QTimer::singleShot(4500,this,[source] {
        if (!source || source->frames_>0 || source->stopping_ ||
            source->process_.state()==QProcess::NotRunning) return;
        source->lastError_="No decoded video frames after 4.5 seconds. Try 640x480 at 15 fps.";
        source->stop();
    });
    return true;
}
void CameraSource::stop() {
    if (process_.state()==QProcess::NotRunning) {
        setStatus("Stopped");
        return;
    }
    stopping_=true;
    setStatus("Stopping");
    process_.terminate();
    // The UI never waits for a slow webcam driver during normal stop.
    QPointer<CameraSource> ptr(this);
    QTimer::singleShot(600,this,[ptr] {
        if (ptr && ptr->process_.state()!=QProcess::NotRunning)
            ptr->process_.kill();
    });
}
void CameraSource::consume() {
    bytes_ += process_.readAllStandardOutput();
    const QByteArray soi=QByteArray::fromHex("ffd8");
    const QByteArray eoi=QByteArray::fromHex("ffd9");
    int found=0;
    while (true) {
        const qsizetype first=bytes_.indexOf(soi);
        if (first<0) {
            if (bytes_.size()>kMaxJpegBuffer) bytes_.clear();
            break;
        }
        if (first>0) bytes_.remove(0,first);
        const qsizetype last=bytes_.indexOf(eoi,2);
        if (last<0) break;
        QByteArray jpeg=bytes_.left(last+2);
        bytes_.remove(0,last+2);
        if (jpeg.size()>kMaxJpegBuffer) continue;
        const QImage frame=QImage::fromData(jpeg,"JPEG");
        if (frame.isNull() || qint64(frame.width())*frame.height()>kMaxCameraPixels)
            continue;
        ++frames_;
        if (frames_==1) setStatus("Streaming");
        if (frameReady) frameReady(frame);
        if (++found>=3) {
            // Drop stale frames if video decoding/painting cannot keep up.
            if (bytes_.size()>kMaxJpegBuffer) bytes_.clear();
            break;
        }
    }
    if (bytes_.size()>kMaxJpegBuffer) {
        bytes_.clear();
        lastError_="Oversized or malformed MJPEG frame discarded.";
        setStatus("Buffer reset");
    }
}
