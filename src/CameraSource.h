// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#pragma once
#include <QObject>
#include <QImage>
#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

class CameraSource final : public QObject {
public:
    explicit CameraSource(QObject* parent=nullptr);
    ~CameraSource() override;
    bool startV4L2(const QString& device, int width, int height, int fps, QString* error=nullptr);
    bool startTestPattern(int width=640, int height=480, int fps=15, QString* error=nullptr);
    bool startNetwork(const QString& url, int width, int height, int fps,
                      QString* error=nullptr);
    void stop();
    bool isRunning() const { return process_.state() != QProcess::NotRunning && !stopping_; }
    int framesSeen() const { return frames_; }
    QString device() const { return device_; }
    QString status() const { return status_; }
    QString lastError() const { return lastError_; }
    std::function<void(const QImage&)> frameReady;
    std::function<void(const QString&)> statusChanged;
    static QStringList listVideoDevices();
private:
    bool launch(const QString& device, const QStringList& inputArgs,
                int width, int height, int fps, QString* error);
    void consume();
    void setStatus(QString status);
    QProcess process_;
    QByteArray bytes_;
    QString device_;
    QString status_ = "Idle";
    QString lastError_;
    int frames_ = 0;
    bool stopping_ = false;
};
