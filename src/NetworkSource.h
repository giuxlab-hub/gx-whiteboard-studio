// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#pragma once
#include "CameraSource.h"
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>

// A deliberate-user-action network feed. Each source owns only one decoder;
// multiple canvas boxes subscribe to its same sequence of QImages.
class NetworkSource final : public QObject {
public:
    explicit NetworkSource(QObject* parent=nullptr);
    ~NetworkSource() override;
    static bool validateUrl(const QString& text, QString* normalized, QString* error);
    bool start(const QString& url, int width, int height, int fps, QString* error=nullptr);
    void stop();
    QString url() const { return url_; }
    QString status() const { return status_; }
    QString lastError() const { return lastError_; }
    int framesSeen() const { return camera_.framesSeen(); }
    bool isActive() const { return desiredActive_; }
    int retries() const { return retryCount_; }
    int connectionAttempts() const { return connectionAttempts_; }
    std::function<void(const QImage&)> frameReady;
    std::function<void(const QString&)> statusChanged;
private:
    void attempt();
    void onStatus(const QString& status);
    void statusUpdate(const QString& status);
    CameraSource camera_;
    QTimer retryTimer_;
    QTimer watchdog_;
    QString url_, status_="Idle", lastError_;
    int width_=640, height_=480, fps_=15, retryCount_=0, connectionAttempts_=0;
    int lastFrameCount_=0, idleSeconds_=0;
    bool desiredActive_=false;
};
