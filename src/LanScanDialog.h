// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#pragma once
#include "LanCameraScanner.h"
#include <QDialog>
#include <QVector>
#include <functional>
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QProgressBar;
class QLabel;

class LanScanDialog final : public QDialog {
public:
    using Translator=std::function<QString(const char*)>;
    explicit LanScanDialog(QWidget* parent=nullptr, bool testLoopback=false,
                           const QVector<quint16>& testPorts={},
                           Translator translator={});
    void setSubnet(const QString& cidr);
    bool beginScan();
    int resultCount() const;
    QString selectedCandidateUrl() const {return selectedCandidateUrl_;}
    QString resultHost(int index) const;
    QString resultProtocol(int index) const;
    bool scanRunning() const {return scanner_.active();}
    int checked() const {return scanner_.checked();}
    int total() const {return scanner_.total();}
    void selectResult(int row);
    void chooseSelected();
private:
    void onResult(const LanCameraScanner::Result& item);
    void updateProgress(int done,int total);
    void updateButtons();
    QString T(const char* text) const {
        return translator_ ? translator_(text) : QString::fromUtf8(text);
    }
    Translator translator_;
    bool testLoopback_=false;
    QVector<quint16> testPorts_;
    LanCameraScanner scanner_;
    QLineEdit* subnetEdit_;
    QPushButton* scanButton_;
    QPushButton* cancelScanButton_;
    QPushButton* useButton_;
    QTreeWidget* results_;
    QProgressBar* progress_;
    QLabel* status_;
    QLabel* note_;
    QString selectedCandidateUrl_;
};
