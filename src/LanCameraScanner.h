#pragma once
#include <QObject>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>
#include <QVector>
#include <QString>
#include <functional>

// Explicit opt-in local subnet discovery. This class only probes selected
// TCP camera ports; it never attempts authentication or modifies devices.
class LanCameraScanner : public QObject {
public:
    struct Result {
        QString host;
        quint16 port = 0;
        QString protocol;   // "RTSP" or "HTTP"
        bool verified = false;  // true only when a protocol signature is received
        QString candidateUrl() const;
    };
    explicit LanCameraScanner(QObject* parent=nullptr);
    ~LanCameraScanner() override;
    static bool parseSubnet(const QString& cidr, QVector<QString>* addresses,
                            QString* error, bool allowLoopbackForTests=false);
    bool start(const QString& cidr, QString* error,
               bool allowLoopbackForTests=false,
               const QVector<quint16>& testPorts={});
    void cancel();
    bool active() const { return active_; }
    int checked() const { return checked_; }
    int total() const { return tasks_.size(); }
    const QVector<Result>& results() const { return results_; }
    std::function<void(int checked,int total)> progress;
    std::function<void(const Result&)> found;
    std::function<void(bool canceled)> finished;
private:
    struct Task { QString host; quint16 port; };
    void dispatch();
    void finalize(bool canceled);
    void onConnected(QTcpSocket* socket);
    void onRead(QTcpSocket* socket);
    void finishSocket(QTcpSocket* socket);
    QVector<Task> tasks_;
    QVector<Result> results_;
    QHash<QTcpSocket*,Task> sockets_;
    QHash<QTcpSocket*,QTimer*> timers_;
    int next_=0, checked_=0;
    bool active_=false;
};
