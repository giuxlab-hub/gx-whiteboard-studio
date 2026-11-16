#include "LanScanDialog.h"
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QTimer>

LanScanDialog::LanScanDialog(QWidget* parent,bool testLoopback,
                             const QVector<quint16>& testPorts,
                             Translator translator)
    : QDialog(parent),translator_(std::move(translator)),
      testLoopback_(testLoopback),testPorts_(testPorts),scanner_(this) {
    setObjectName("lanScanDialog");
    setWindowTitle(T("LAN camera discovery"));
    setMinimumSize(600,450);
    setAttribute(Qt::WA_DeleteOnClose,false);
    setStyleSheet(R"(
      QDialog {background:#202637;color:#edf3fb;}
      QLabel {color:#d7e4f3;font-size:12px;}
      QLineEdit,QTreeWidget {background:#172638;color:#f4f8ff;
         alternate-background-color:#20334e; selection-background-color:#42658b;
         selection-color:#ffffff; border:1px solid #526783;
         border-radius:5px;padding:6px;}
      QPushButton {color:#eff5fe;background:#304663;
         border:1px solid #55749a;border-radius:5px;padding:6px 12px;}
      QPushButton:disabled {color:#8494a7;background:#263347;}
      QTreeWidget::item:selected {background:#42658b;}
      QProgressBar {background:#253347;color:white;border:1px solid #526783;
         border-radius:4px;text-align:center;min-height:15px;}
      QProgressBar::chunk {background:#6599cf;}
    )");
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(16,16,16,14);
    layout->setSpacing(9);
    auto* label=new QLabel(T("IPv4 subnet (CIDR):"),this);
    layout->addWidget(label);
    auto* top=new QHBoxLayout();
    subnetEdit_=new QLineEdit(this);
    subnetEdit_->setObjectName("lanSubnetEdit");
    subnetEdit_->setPlaceholderText("192.168.1.0/24");
    top->addWidget(subnetEdit_,1);
    scanButton_=new QPushButton(T("Scan"),this);
    scanButton_->setObjectName("lanScanStart");
    top->addWidget(scanButton_);
    cancelScanButton_=new QPushButton(T("Cancel scan"),this);
    cancelScanButton_->setObjectName("lanScanCancel");
    top->addWidget(cancelScanButton_);
    layout->addLayout(top);
    progress_=new QProgressBar(this);
    progress_->setObjectName("lanScanProgress");
    progress_->setRange(0,100);
    progress_->setValue(0);
    layout->addWidget(progress_);
    status_=new QLabel(T("Ready. Scanning starts only when you press Scan."),this);
    status_->setObjectName("lanScanStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    results_=new QTreeWidget(this);
    results_->setObjectName("lanScanResults");
    results_->setColumnCount(4);
    results_->setHeaderLabels({T("Host"),T("Port"),T("Service"),T("Identification")});
    results_->setRootIsDecorated(false);
    results_->setAlternatingRowColors(true);
    results_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_->header()->setStretchLastSection(false);
    results_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    results_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    results_->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    results_->header()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    layout->addWidget(results_,1);
    note_=new QLabel(T("Searches for potential RTSP/HTTP services, not confirmed cameras. "
       "The suggested /stream or / path may need editing. Public networks are not "
       "scanned, but public IP cameras can be connected manually."),this);
    note_->setObjectName("lanScanNote");
    note_->setWordWrap(true);
    layout->addWidget(note_);
    auto* bottom=new QHBoxLayout();
    bottom->addStretch(1);
    useButton_=new QPushButton(T("Use address"),this);
    useButton_->setObjectName("lanScanUse");
    bottom->addWidget(useButton_);
    auto* close=new QPushButton(T("Close"),this);
    close->setObjectName("lanScanClose");
    bottom->addWidget(close);
    layout->addLayout(bottom);
    connect(scanButton_,&QPushButton::clicked,this,[this] {beginScan();});
    connect(cancelScanButton_,&QPushButton::clicked,this,[this] {scanner_.cancel();});
    connect(close,&QPushButton::clicked,this,&QDialog::reject);
    connect(useButton_,&QPushButton::clicked,this,&LanScanDialog::chooseSelected);
    connect(results_,&QTreeWidget::itemSelectionChanged,this,
            &LanScanDialog::updateButtons);
    connect(results_,&QTreeWidget::itemDoubleClicked,this,
            [this](QTreeWidgetItem*,int) { chooseSelected(); });
    scanner_.progress=[this](int done,int total) {updateProgress(done,total);};
    scanner_.found=[this](const LanCameraScanner::Result& result) {onResult(result);};
    scanner_.finished=[this](bool canceled) {
        status_->setText(canceled
            ? T("Scan canceled.")
            : T("Scan complete. Candidate services: %1.").arg(resultCount()));
        updateButtons();
    };
    updateButtons();
}
void LanScanDialog::setSubnet(const QString& cidr) {subnetEdit_->setText(cidr);}
bool LanScanDialog::beginScan() {
    if (scanner_.active()) return false;
    QString error;
    QVector<QString> addresses;
    if (!LanCameraScanner::parseSubnet(subnetEdit_->text(),&addresses,&error,
                                      testLoopback_)) {
        status_->setText(T(error.toUtf8().constData()));
        return false;
    }
    // UI confirmation is required before initiating any LAN scan,
    // except in the explicit localhost-only regression mode.
    if (!testLoopback_) {
        const auto answer=QMessageBox::question(this,
            T("Confirm LAN scan"),
            T("Probe %1 hosts in subnet %2?\n"
                    "Only common RTSP/HTTP TCP ports. No authentication.")
                .arg(addresses.size()).arg(subnetEdit_->text().trimmed()),
            QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
        if (answer!=QMessageBox::Yes) return false;
    }
    results_->clear();
    progress_->setValue(0);
    if (!scanner_.start(subnetEdit_->text(),&error,testLoopback_,testPorts_)) {
        status_->setText(T(error.toUtf8().constData()));
        return false;
    }
    status_->setText(T("Scanning..."));
    updateButtons();
    return true;
}
void LanScanDialog::onResult(const LanCameraScanner::Result& item) {
    auto* row=new QTreeWidgetItem(results_);
    row->setText(0,item.host);
    row->setText(1,QString::number(item.port));
    row->setText(2,item.protocol);
    row->setText(3,item.verified?T("Protocol response"):T("TCP port only"));
    row->setData(0,Qt::UserRole,item.candidateUrl());
    row->setToolTip(0,item.verified
        ? T("Service responded as RTSP/HTTP; this does not confirm a camera.")
        : T("Open TCP port; camera protocol not confirmed."));
    if (results_->topLevelItemCount()==1)
        results_->setCurrentItem(row);
    updateButtons();
}
void LanScanDialog::updateProgress(int done,int total) {
    progress_->setMaximum(qMax(1,total));
    progress_->setValue(done);
    if (scanner_.active())
        status_->setText(T("Checks %1 / %2 — results: %3")
                         .arg(done).arg(total).arg(resultCount()));
}
void LanScanDialog::updateButtons() {
    const bool scanning=scanner_.active();
    scanButton_->setEnabled(!scanning);
    cancelScanButton_->setEnabled(scanning);
    useButton_->setEnabled(!scanning && results_->currentItem()!=nullptr);
}
int LanScanDialog::resultCount() const {return results_->topLevelItemCount();}
QString LanScanDialog::resultHost(int index) const {
    const auto* row=results_->topLevelItem(index);
    return row?row->text(0):QString();
}
QString LanScanDialog::resultProtocol(int index) const {
    const auto* row=results_->topLevelItem(index);
    return row?row->text(2):QString();
}
void LanScanDialog::selectResult(int row) {
    if (auto* item=results_->topLevelItem(row))
        results_->setCurrentItem(item);
}
void LanScanDialog::chooseSelected() {
    if (scanner_.active()) return;
    auto* row=results_->currentItem();
    if (!row) return;
    selectedCandidateUrl_=row->data(0,Qt::UserRole).toString();
    accept();
}
