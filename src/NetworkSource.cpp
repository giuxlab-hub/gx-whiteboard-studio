// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#include "NetworkSource.h"
#include <QUrlQuery>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QDebug>
#include <algorithm>

NetworkSource::NetworkSource(QObject* parent): QObject(parent), camera_(this) {
    retryTimer_.setSingleShot(true);
    connect(&retryTimer_, &QTimer::timeout, this, [this]() { attempt(); });
    watchdog_.setInterval(1000);
    connect(&watchdog_, &QTimer::timeout, this, [this]() {
        if (!desiredActive_) return;
        const int current=camera_.framesSeen();
        if (current != lastFrameCount_) {
            lastFrameCount_=current;
            idleSeconds_=0;
            // An active feed resets exponential backoff after a healthy interval.
            if (current >= 30) retryCount_=0;
        } else if (camera_.status()=="Streaming") {
            ++idleSeconds_;
            if (idleSeconds_>=8) {
                lastError_="No new network video frames for 8 seconds.";
                camera_.stop();
                statusUpdate("Reconnecting");
            }
        }
    });
    camera_.frameReady=[this](const QImage& frame) {
        if (frameReady) frameReady(frame);
    };
    camera_.statusChanged=[this](const QString& value) { onStatus(value); };
}
NetworkSource::~NetworkSource() {
    frameReady={}; statusChanged={};
    stop();
}
bool NetworkSource::validateUrl(const QString& input, QString* normalized, QString* error) {
    const QString candidate=input.trimmed();
    if (candidate.isEmpty() || candidate.size()>1024 ||
        candidate.contains('\n') || candidate.contains('\r') ||
        candidate.contains('\\') || candidate.contains(' ')) {
        if (error) *error="Invalid stream address.";
        return false;
    }
    QUrl url(candidate,QUrl::StrictMode);
    const QString scheme=url.scheme().toLower();
    if (!url.isValid() || !(scheme=="rtsp" || scheme=="rtsps" ||
                            scheme=="http" || scheme=="https") ||
        url.host().isEmpty() || url.port(0)<0 || url.port(0)>65535 ||
        url.hasFragment() || !url.userInfo().isEmpty()) {
        if (error) *error="Use an RTSP or HTTP(S) URL without embedded credentials.";
        return false;
    }
    // The prototype may persist non-secret URLs. Prevent accidental storage
    // of typical access tokens or passwords in arguments, logs and settings.
    const QUrlQuery query(url);
    const QRegularExpression sensitive(
        "(password|passwd|pass|token|auth|api.?key|secret|signature|credential|access.?key)",
        QRegularExpression::CaseInsensitiveOption);
    for (const auto& item : query.queryItems()) {
        if (sensitive.match(item.first).hasMatch()) {
            if (error) *error="Credentials or access tokens are not supported in F05 URLs.";
            return false;
        }
    }
    // Do not permit password-style URL fragments or basic auth.
    if (candidate.contains('@')) {
        if (error) *error="Credentials are not supported in F05.";
        return false;
    }
    // Public IP and DNS targets are deliberately allowed for manual stream
    // URLs; only LanCameraScanner restricts *automatic discovery* to RFC1918.
    if (normalized) *normalized=url.toString(QUrl::FullyEncoded);
    return true;
}
bool NetworkSource::start(const QString& sourceUrl,int width,int height,int fps,QString* error) {
    QString normalized;
    if (!validateUrl(sourceUrl,&normalized,error)) return false;
    if (width<160 || height<90 || width*height>1920*1080 ||
        fps<1 || fps>30) {
        if (error) *error="Unsupported network preview format.";
        return false;
    }
    stop();
    url_=normalized; width_=width; height_=height; fps_=fps;
    desiredActive_=true;
    retryCount_=0; connectionAttempts_=0;
    idleSeconds_=0; lastFrameCount_=0; lastError_.clear();
    watchdog_.start();
    attempt();
    return true; // asynchronous acquisition; status and frameReady report results
}
void NetworkSource::statusUpdate(const QString& value) {
    if (status_==value) return;
    status_=value;
    if (statusChanged) statusChanged(value);
}
void NetworkSource::attempt() {
    if (!desiredActive_) return;
    if (camera_.isRunning() || camera_.status()=="Stopping") {
        retryTimer_.start(300);
        return;
    }
    QString error;
    ++connectionAttempts_;
    const bool ok=camera_.startNetwork(url_,width_,height_,fps_,&error);
    if (!ok) {
        lastError_=error;
        retryCount_=std::min(retryCount_+1,5);
        const int delay=std::min(1000*(1 << std::min(retryCount_-1,3)),8000);
        statusUpdate("Reconnecting");
        retryTimer_.start(delay);
        return;
    }
    statusUpdate(retryCount_ ? "Reconnecting" : "Starting");
}
void NetworkSource::onStatus(const QString& value) {
    if (!desiredActive_) {
        if (value!="Stopping") statusUpdate("Stopped");
        return;
    }
    if (value=="Streaming") {
        idleSeconds_=0;
        lastFrameCount_=camera_.framesSeen();
        statusUpdate("Streaming");
    } else if (value=="Error" || value=="Disconnected" || value=="Stopped") {
        lastError_=camera_.lastError();
        // Sanitized error includes FFmpeg status but no URL or credentials.
        retryCount_=std::min(retryCount_+1,5);
        const int delay=std::min(1000*(1<<std::min(retryCount_-1,3)),8000);
        statusUpdate("Reconnecting");
        retryTimer_.start(delay);
    } else if (value=="Starting") {
        statusUpdate(retryCount_ ? "Reconnecting" : "Starting");
    }
}
void NetworkSource::stop() {
    desiredActive_=false;
    retryTimer_.stop();
    watchdog_.stop();
    camera_.stop();
    statusUpdate("Stopped");
}
