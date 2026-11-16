// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#include "LanCameraScanner.h"
#include <QAbstractSocket>
#include <QRegularExpression>
#include <algorithm>

namespace {
constexpr int MaxHosts = 256;
constexpr int MaxInFlight = 24;
constexpr int ConnectTimeoutMs = 350;
constexpr int SignatureTimeoutMs = 260;
const QVector<quint16> DefaultPorts{
    554,8554,8555,10554,80,8080,8081,8000,8899,8090
};

bool isPrivateIpv4(quint32 value) {
    const quint8 a=quint8(value>>24);
    const quint8 b=quint8((value>>16)&255);
    return a==10 || (a==172 && b>=16 && b<=31) || (a==192 && b==168);
}
bool isLoopbackIpv4(quint32 value) { return (value>>24)==127; }
bool isRtspPort(quint16 port) {
    return port==554 || port==8554 || port==8555 || port==10554;
}
}

QString LanCameraScanner::Result::candidateUrl() const {
    const bool rtsp=(protocol=="RTSP");
    // Camera-specific RTSP paths are unknown; /stream is a *suggestion* only.
    return QString("%1://%2:%3%4")
        .arg(rtsp?"rtsp":"http",host,QString::number(port),
             rtsp?"/stream":"/");
}
LanCameraScanner::LanCameraScanner(QObject* parent):QObject(parent) {}
LanCameraScanner::~LanCameraScanner() {
    progress={};found={};finished={};
    cancel();
}
bool LanCameraScanner::parseSubnet(const QString& input,QVector<QString>* addresses,
                                   QString* error,bool allowLoopbackForTests) {
    if (addresses) addresses->clear();
    const QString text=input.trimmed();
    const QRegularExpression cidr(
        R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})/(\d{1,2})$)");
    const auto match=cidr.match(text);
    if (!match.hasMatch()) {
        if (error) *error="Use an IPv4 subnet in CIDR form (e.g. 192.168.1.0/24).";
        return false;
    }
    quint32 value=0;
    for (int i=1;i<=4;++i) {
        bool ok=false;
        const uint octet=match.captured(i).toUInt(&ok);
        if (!ok || octet>255) {
            if (error) *error="Invalid IPv4 address.";
            return false;
        }
        value=(value<<8)|octet;
    }
    bool ok=false;
    const uint prefix=match.captured(5).toUInt(&ok);
    if (!ok || prefix<24 || prefix>32) {
        if (error) *error="Scan range must be between /24 and /32 (maximum 256 hosts).";
        return false;
    }
    const quint32 mask=prefix==0?0u:(0xffffffffu<<(32-prefix));
    const quint32 network=value&mask;
    const quint32 count=1u<<(32-prefix);
    if (count>MaxHosts || (!isPrivateIpv4(network) &&
        !(allowLoopbackForTests && isLoopbackIpv4(network)))) {
        if (error) *error="Only private LAN subnets are accepted; maximum 256 hosts.";
        return false;
    }
    QVector<QString> result;
    result.reserve(int(count));
    for (quint32 i=0;i<count;++i) {
        const quint32 host=network+i;
        if (!isPrivateIpv4(host) &&
            !(allowLoopbackForTests && isLoopbackIpv4(host))) {
            if (error) *error="Subnet crosses a private network boundary.";
            return false;
        }
        // Skip network/broadcast on ordinary networks, not /31-/32.
        if (prefix<=30 && (i==0 || i==count-1)) continue;
        result.push_back(QHostAddress(host).toString());
    }
    if (result.isEmpty()) {
        if (error) *error="The subnet contains no addresses to scan.";
        return false;
    }
    if (addresses) *addresses=std::move(result);
    return true;
}
bool LanCameraScanner::start(const QString& subnet,QString* error,
                             bool allowLoopbackForTests,
                             const QVector<quint16>& testPorts) {
    if (active_) {
        if (error) *error="A scan is already running.";
        return false;
    }
    QVector<QString> addresses;
    if (!parseSubnet(subnet,&addresses,error,allowLoopbackForTests)) return false;
    const QVector<quint16> ports=testPorts.isEmpty()?DefaultPorts:testPorts;
    if (ports.size()>12 || ports.isEmpty() ||
        std::any_of(ports.begin(),ports.end(),[](quint16 port){return port==0;})) {
        if (error) *error="Invalid TCP port selection.";
        return false;
    }
    tasks_.clear();
    results_.clear();
    next_=0;checked_=0;
    tasks_.reserve(addresses.size()*ports.size());
    for (const auto& host : addresses)
        for (auto port : ports) tasks_.append({host,port});
    active_=true;
    if (progress) progress(0,tasks_.size());
    QTimer::singleShot(0,this,[this] {dispatch();});
    return true;
}
void LanCameraScanner::dispatch() {
    if (!active_) return;
    while (sockets_.size()<MaxInFlight && next_<tasks_.size()) {
        const Task task=tasks_.at(next_++);
        auto* socket=new QTcpSocket(this);
        auto* timer=new QTimer(socket);
        timer->setSingleShot(true);
        sockets_.insert(socket,task);
        timers_.insert(socket,timer);
        connect(socket,&QTcpSocket::connected,this,[this,socket] {
            onConnected(socket);
        });
        connect(socket,&QTcpSocket::readyRead,this,[this,socket] {
            onRead(socket);
        });
        connect(socket,&QTcpSocket::errorOccurred,this,[this,socket](
                QAbstractSocket::SocketError) {finishSocket(socket);});
        connect(timer,&QTimer::timeout,this,[this,socket] {
            if (!sockets_.contains(socket)) return;
            // A confirmed TCP connection with no response is still a
            // candidate, not a verified camera.
            if (socket->state()==QAbstractSocket::ConnectedState) {
                const Task task=sockets_.value(socket);
                Result item{task.host,task.port,
                            isRtspPort(task.port)?"RTSP":"HTTP",false};
                results_.append(item);
                if (found) found(item);
            }
            finishSocket(socket);
        });
        timer->start(ConnectTimeoutMs);
        socket->connectToHost(task.host,task.port);
    }
    if (sockets_.isEmpty() && next_>=tasks_.size())
        finalize(false);
}
void LanCameraScanner::onConnected(QTcpSocket* socket) {
    if (!sockets_.contains(socket)) return;
    const Task task=sockets_.value(socket);
    if (auto* timer=timers_.value(socket,nullptr))
        timer->start(SignatureTimeoutMs);
    // Lightweight, unauthenticated protocol fingerprints only; no enumeration
    // of paths, passwords, login attempts or RTSP DESCRIBE.
    if (isRtspPort(task.port)) {
        const QByteArray command=
          "OPTIONS rtsp://"+task.host.toLatin1()+":"+
          QByteArray::number(task.port)+
          "/ RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: GXWhiteboardStudio/0.5\r\n\r\n";
        socket->write(command);
    } else {
        socket->write(
          "HEAD / HTTP/1.1\r\nHost: "+task.host.toLatin1()+
          "\r\nConnection: close\r\nUser-Agent: GXWhiteboardStudio/0.5\r\n\r\n");
    }
}
void LanCameraScanner::onRead(QTcpSocket* socket) {
    if (!sockets_.contains(socket)) return;
    QByteArray response=socket->read(1024);
    const Task task=sockets_.value(socket);
    const bool isRtsp=response.startsWith("RTSP/1.");
    const bool isHttp=response.startsWith("HTTP/1.");
    if (!isRtsp && !isHttp) return;
    Result item{task.host,task.port,isRtsp?"RTSP":"HTTP",true};
    results_.append(item);
    if (found) found(item);
    finishSocket(socket);
}
void LanCameraScanner::finishSocket(QTcpSocket* socket) {
    if (!sockets_.contains(socket)) return;
    sockets_.remove(socket);
    timers_.remove(socket);
    socket->abort();
    socket->deleteLater();
    ++checked_;
    if (progress) progress(checked_,tasks_.size());
    if (active_) QTimer::singleShot(0,this,[this] {dispatch();});
}
void LanCameraScanner::finalize(bool canceled) {
    if (!active_) return;
    active_=false;
    if (finished) finished(canceled);
}
void LanCameraScanner::cancel() {
    if (!active_) return;
    active_=false;
    for (QTcpSocket* socket : sockets_.keys()) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    sockets_.clear();
    timers_.clear();
    if (finished) finished(true);
}
