// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#include "StudioWindow.h"
#include "LanScanDialog.h"
#include "RecordingDialog.h"
#include <QProgressBar>
#include <QDateTime>
#include <QStorageInfo>
#include <QApplication>
#include <QAction>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QWidget>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QShortcut>
#include <QCloseEvent>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QFont>
#include <QTimer>
#include <QLibraryInfo>
#include <QLocale>
#include <QDir>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QToolButton>
#include <QCryptographicHash>
#include <QActionGroup>
#include <QFileDialog>
#include <QInputDialog>
#include <QStandardPaths>
#include <QClipboard>
#include <QMimeData>
#include <QUndoStack>
#include <QFileInfo>
#include <QListWidget>
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QDesktopServices>
#include <QImageReader>
#include <QSlider>
#include <QScrollArea>
#include <QIcon>
#include <QSizePolicy>
#include <QDir>

class AssetLibraryList final : public QListWidget {
public:
    explicit AssetLibraryList(QWidget* parent=nullptr): QListWidget(parent) {
        setDragEnabled(true);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setViewMode(QListView::IconMode);
        setFlow(QListView::TopToBottom);
        setWrapping(false);
        setMovement(QListView::Static);
        setResizeMode(QListView::Adjust);
        setUniformItemSizes(true);
        setIconSize(QSize(212,114));
        setGridSize(QSize(229,125));
        setSpacing(3);
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
protected:
    void startDrag(Qt::DropActions) override {
        auto* item=currentItem();
        if (!item) return;
        const QString path=item->data(Qt::UserRole).toString();
        if (!QFileInfo::exists(path)) return;
        auto* mime=new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(path)});
        auto* drag=new QDrag(this);
        drag->setMimeData(mime);
        drag->exec(Qt::CopyAction);
    }
};

StudioWindow::StudioWindow(QString catalogRoot, QString initialLanguage)
    : loc_(std::move(catalogRoot)), settings_("GiuxLab", "GXWhiteboardStudio") {
    buildUI();
    project_.initialize(board_);
    refreshSceneUi();
    applyStyle();
    // Restoring startup preferences must not create unsaved document edits:
    // otherwise existing crash-recovery checkpoints would be suppressed.
    projectLoading_=true;
    restoreSettings();
    projectLoading_=false;
    const QString preferred = !initialLanguage.isEmpty() ? initialLanguage :
        settings_.value("language", QLocale::system().name()).toString();
    setLanguage(preferred, false);
    setMinimumSize(850, 540);
    resize(1320, 820);
    if (settings_.contains("geometry")) restoreGeometry(settings_.value("geometry").toByteArray());
    statusBar()->showMessage(loc_.tr("Ready."), 3500);
    recorder_->renderFrame=[this](const QSize& size) {
        return board_->renderViewImage(size);
    };
    recorder_->changed=[this] {updateRecordingUi();};
    recorder_->warning=[this](const QString& diagnostic) {
        const QString translated=loc_.tr(diagnostic.toUtf8().constData());
        statusBar()->showMessage(loc_.tr("Recording warning: ")+translated,12000);
        qWarning().noquote() << "Recording health:" << diagnostic;
    };
    recorder_->completed=[this](bool success,const QString& message) {
        updateRecordingUi();
        if (success) {
            if (message.contains("MP4 conversion failed") ||
                message.contains("stopped early")) {
                QMessageBox::warning(this,
                    loc_.tr("Recording completed with warnings"),
                    message+"\n"+loc_.tr("Diagnostics: ")+
                    recorder_->diagnosticPath());
            } else
                statusBar()->showMessage(loc_.tr("Recording saved: ")+message,12000);
        } else {
            QMessageBox::warning(this,loc_.tr("Recording failed"),
                message+"\n"+loc_.tr("Diagnostics: ")+
                recorder_->diagnosticPath());
        }
    };
    board_->requestText = [this](const QPointF& at) { requestText(at); };
    board_->requestMediaContextMenu = [this](const QPoint& globalPos) {
        showMediaContextMenu(globalPos);
    };
    board_->externalImagesDropped=[this](int count,const QString& reason) {
        if (count>0) {
            statusBar()->showMessage(loc_.tr("Images inserted from file manager: ")+
                                     QString::number(count),5500);
        } else {
            statusBar()->showMessage(loc_.tr("Cannot insert dragged image: ")+reason,7500);
        }
    };
    board_->stateChanged = [this]() {
        refreshEditActions();
        refreshImageControls();
        if (!projectLoading_ && project_.count()>0) {
            // Tablet motion and selection fire this callback many times.
            // Only committed undoable changes need to dirty the project;
            // scene-list updates are cheap unless titles actually changed.
            if (!board_->undoStack()->isClean()) project_.setDirty();
            refreshSceneUi();
        }
    };
    connect(QApplication::clipboard(), &QClipboard::dataChanged,
            this, [this]() { refreshEditActions(); });
    refreshEditActions();
    QTimer::singleShot(0, board_, [this]() { board_->fitBoard(); });
    recoveryTimer_=new QTimer(this);
    recoveryTimer_->setObjectName("recoveryTimer");
    recoveryTimer_->setInterval(60000);
    connect(recoveryTimer_,&QTimer::timeout,this,&StudioWindow::saveRecoveryCheckpoint);
    recoveryTimer_->start();
    const bool automated=QCoreApplication::arguments().join(" ").contains("--smoke");
    if (!automated)
        QTimer::singleShot(1100,this,&StudioWindow::offerRecoveryAtStartup);
}
StudioWindow::~StudioWindow() {
    // Child widgets outlive this class's own members during QObject destruction.
    // Never let QUndoStack destruction call back into a torn-down StudioWindow.
    if (board_) {
        board_->stateChanged = {};
        board_->requestText = {};
        board_->requestMediaContextMenu = {};
        board_->externalImagesDropped = {};
    }
    if (recorder_) {
        recorder_->renderFrame={};
        recorder_->changed={};
        recorder_->completed={};
        recorder_->warning={};
    }
    // Explicitly stop camera processes before Qt destroys child widgets.
    for (auto* camera : cameraSources_) {
        camera->frameReady = {};
        camera->statusChanged = {};
        delete camera;
    }
    cameraSources_.clear();
    for (auto* network : networkSources_) {
        network->frameReady={};
        network->statusChanged={};
        delete network;
    }
    networkSources_.clear();
}
void StudioWindow::buildUI() {
    auto *root = new QWidget(this);
    auto *content = new QHBoxLayout(root);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(0);
    sidebar_ = new QFrame(root);
    sidebar_->setObjectName("sidebar");
    sidebar_->setFixedWidth(290);
    auto *sideLayout = new QVBoxLayout(sidebar_);
    sideLayout->setContentsMargins(18, 24, 18, 18);
    sideLayout->setSpacing(8);
    brand_ = new QLabel(sidebar_);
    brand_->setObjectName("brand");
    sideLayout->addWidget(brand_);
    sidebarLabel_ = new QLabel(sidebar_);
    sidebarLabel_->setObjectName("smallHeading");
    sideLayout->addSpacing(12);
    sideLayout->addWidget(sidebarLabel_);
    for (int i = 0; i < 4; ++i) {
        sections_[i] = new QPushButton(sidebar_);
        sections_[i]->setObjectName("sideNav");
        sections_[i]->setCheckable(true);
        sections_[i]->setCursor(Qt::PointingHandCursor);
        sections_[i]->setMinimumHeight(36);
        connect(sections_[i], &QPushButton::clicked, this, [this, i]() {
            selectSection(i);
        });
        sideLayout->addWidget(sections_[i]);
    }
    sideLayout->addSpacing(8);
    sectionTitle_ = new QLabel(sidebar_);
    sectionTitle_->setObjectName("sectionTitle");
    sectionTitle_->setWordWrap(true);
    sideLayout->addWidget(sectionTitle_);
    sectionText_ = new QLabel(sidebar_);
    sectionText_->setObjectName("sectionText");
    sectionText_->setWordWrap(true);
    sectionText_->setAlignment(Qt::AlignTop);
    sideLayout->addWidget(sectionText_);

    // A single icon-only gallery owns any required scrolling; there is no
    // second nested scroll area, so the canvas and sidebar widths stay fixed.
    imagePanel_ = new QWidget(sidebar_);
    imagePanel_->setObjectName("imagePanel");
    auto* imageLayout = new QVBoxLayout(imagePanel_);
    imageLayout->setContentsMargins(0,2,0,0);
    imageLayout->setSpacing(5);

    browseImageButton_ = new QPushButton(imagePanel_);
    browseImageButton_->setObjectName("browseImagesButton");
    insertImageButton_ = new QPushButton(imagePanel_);
    insertImageButton_->setObjectName("insertImageButton");
    auto* imageActionRow = new QHBoxLayout();
    imageActionRow->setContentsMargins(0,0,0,0);
    imageActionRow->setSpacing(5);
    imageActionRow->addWidget(browseImageButton_);
    imageActionRow->addWidget(insertImageButton_);
    imageLayout->addLayout(imageActionRow);

    imageList_ = new AssetLibraryList(imagePanel_);
    imageList_->setObjectName("imageLibrary");
    imageList_->setMinimumHeight(130);
    imageList_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    imageLayout->addWidget(imageList_,1);

    // Properties only occupy vertical space while a canvas image is selected.
    imagePropertiesPanel_ = new QWidget(imagePanel_);
    imagePropertiesPanel_->setObjectName("imagePropertiesPanel");
    auto* propertiesLayout = new QVBoxLayout(imagePropertiesPanel_);
    propertiesLayout->setContentsMargins(0,0,0,0);
    propertiesLayout->setSpacing(4);
    imageSelectedLabel_ = new QLabel(imagePropertiesPanel_);
    imageSelectedLabel_->setObjectName("imagePropertiesHeading");
    imageSelectedLabel_->setWordWrap(false);
    imageSelectedLabel_->setMinimumHeight(16);
    propertiesLayout->addWidget(imageSelectedLabel_);
    imageAspectBox_ = new QCheckBox(imagePropertiesPanel_);
    imageAspectBox_->setObjectName("imageAspectLock");
    imageAspectBox_->setChecked(true);
    imageFitLabel_ = new QLabel(imagePropertiesPanel_);
    imageFitLabel_->setObjectName("smallHeading");
    // The full fitting description is the combobox tooltip. A spare label
    // without a layout must remain hidden to avoid painting over the heading.
    imageFitLabel_->hide();
    imageFitBox_ = new QComboBox(imagePropertiesPanel_);
    imageFitBox_->addItem(QString(),false);
    imageFitBox_->addItem(QString(),true);
    imageFitBox_->setObjectName("imageFitMode");
    imageFitBox_->setMinimumWidth(103);
    auto* fitRow = new QHBoxLayout();
    fitRow->setContentsMargins(0,0,0,0);
    fitRow->setSpacing(5);
    fitRow->addWidget(imageAspectBox_);
    fitRow->addWidget(imageFitBox_,1);
    propertiesLayout->addLayout(fitRow);
    imageOpacityLabel_ = new QLabel(imagePropertiesPanel_);
    imageOpacityLabel_->setObjectName("smallHeading");
    imageOpacitySlider_ = new QSlider(Qt::Horizontal,imagePropertiesPanel_);
    imageOpacitySlider_->setObjectName("imageOpacity");
    imageOpacitySlider_->setRange(10,100);
    imageOpacitySlider_->setValue(100);
    imageOpacitySlider_->setTracking(false);
    auto* opacityRow = new QHBoxLayout();
    opacityRow->setContentsMargins(0,0,0,0);
    opacityRow->setSpacing(5);
    opacityRow->addWidget(imageOpacityLabel_);
    opacityRow->addWidget(imageOpacitySlider_,1);
    propertiesLayout->addLayout(opacityRow);
    imageFrontButton_ = new QPushButton(imagePropertiesPanel_);
    imageFrontButton_->setObjectName("imageFrontButton");
    imageBackButton_ = new QPushButton(imagePropertiesPanel_);
    imageBackButton_->setObjectName("imageBackButton");
    imageRemoveButton_ = new QPushButton(imagePropertiesPanel_);
    imageRemoveButton_->setObjectName("imageRemoveButton");
    auto* layerRow = new QHBoxLayout();
    layerRow->setContentsMargins(0,0,0,0);
    layerRow->setSpacing(5);
    layerRow->addWidget(imageFrontButton_);
    layerRow->addWidget(imageBackButton_);
    layerRow->addWidget(imageRemoveButton_);
    propertiesLayout->addLayout(layerRow);
    imageLayout->addWidget(imagePropertiesPanel_);
    imagePropertiesPanel_->hide();
    sideLayout->addWidget(imagePanel_,3);

    // F04 USB camera panel, intentionally separate from the static-image gallery.
    auto* cameraScroll = new QScrollArea(sidebar_);
    cameraPanel_ = cameraScroll;
    cameraPanel_->setObjectName("cameraPanel");
    cameraScroll->setWidgetResizable(true);
    cameraScroll->setFrameShape(QFrame::NoFrame);
    cameraScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cameraScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    cameraScroll->setMinimumHeight(180);
    auto* cameraContent = new QWidget(cameraScroll);
    cameraContent->setObjectName("cameraContent");
    auto* cameraLayout = new QVBoxLayout(cameraContent);
    cameraLayout->setContentsMargins(0,2,3,2);
    cameraLayout->setSpacing(3);
    cameraDeviceLabel_=new QLabel(cameraPanel_);
    cameraDeviceLabel_->setObjectName("smallHeading");
    cameraLayout->addWidget(cameraDeviceLabel_);
    cameraDeviceCombo_=new QComboBox(cameraPanel_);
    cameraDeviceCombo_->setObjectName("cameraDeviceCombo");
    cameraDeviceCombo_->setMinimumHeight(27);
    cameraRefreshButton_=new QPushButton(cameraPanel_);
    cameraRefreshButton_->setObjectName("cameraAction");
    cameraRefreshButton_->setToolTip(QString());
    auto* deviceRow=new QHBoxLayout();
    deviceRow->setSpacing(5);
    deviceRow->setContentsMargins(0,0,0,0);
    deviceRow->addWidget(cameraDeviceCombo_,3);
    deviceRow->addWidget(cameraRefreshButton_,1);
    cameraLayout->addLayout(deviceRow);
    cameraResolutionLabel_=new QLabel(cameraPanel_);
    cameraResolutionLabel_->setObjectName("smallHeading");
    cameraLayout->addWidget(cameraResolutionLabel_);
    cameraResolutionCombo_=new QComboBox(cameraPanel_);
    cameraResolutionCombo_->setObjectName("cameraResolutionCombo");
    cameraResolutionCombo_->setMinimumHeight(27);
    cameraResolutionCombo_->addItem("640 × 480",QSize(640,480));
    cameraResolutionCombo_->addItem("1280 × 720",QSize(1280,720));
    cameraResolutionCombo_->addItem("1920 × 1080",QSize(1920,1080));
    cameraLayout->addWidget(cameraResolutionCombo_);
    cameraFpsLabel_=new QLabel(cameraPanel_);
    cameraFpsLabel_->setObjectName("smallHeading");
    cameraLayout->addWidget(cameraFpsLabel_);
    cameraFpsCombo_=new QComboBox(cameraPanel_);
    cameraFpsCombo_->setObjectName("cameraFpsCombo");
    cameraFpsCombo_->setMinimumHeight(27);
    for (int fps : {10,15,30})
        cameraFpsCombo_->addItem(QString::number(fps),fps);
    cameraFpsCombo_->setCurrentIndex(1);
    cameraLayout->addWidget(cameraFpsCombo_);
    cameraStartButton_=new QPushButton(cameraPanel_);
    cameraStartButton_->setObjectName("cameraAction");
    cameraStopButton_=new QPushButton(cameraPanel_);
    cameraStopButton_->setObjectName("cameraAction");
    auto* transportRow=new QHBoxLayout();
    transportRow->setContentsMargins(0,0,0,0);
    transportRow->setSpacing(5);
    transportRow->addWidget(cameraStartButton_);
    transportRow->addWidget(cameraStopButton_);
    cameraLayout->addLayout(transportRow);
    cameraInsertButton_=new QPushButton(cameraPanel_);
    cameraInsertButton_->setObjectName("cameraAction");
    cameraLayout->addWidget(cameraInsertButton_);
    cameraStatusLabel_=new QLabel(cameraPanel_);
    cameraStatusLabel_->setObjectName("cameraStatus");
    cameraStatusLabel_->setWordWrap(true);
    cameraStatusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cameraLayout->addWidget(cameraStatusLabel_);
    cameraNoteLabel_=new QLabel(cameraPanel_);
    cameraNoteLabel_->setObjectName("cameraNote");
    cameraNoteLabel_->setWordWrap(true);
    cameraLayout->addWidget(cameraNoteLabel_);

    // F05: IP cameras share the left Cameras panel but their controls remain
    // collapsed until the user explicitly opens the IP section.
    networkToggle_=new QToolButton(cameraPanel_);
    networkToggle_->setObjectName("networkToggle");
    networkToggle_->setCheckable(true);
    networkToggle_->setChecked(false);
    networkToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    networkToggle_->setArrowType(Qt::RightArrow);
    // Put the IP/USB switch above both device panels, so a user never has
    // to scroll to discover network cameras.
    cameraLayout->removeWidget(cameraDeviceLabel_);
    auto* cameraModeRow = new QHBoxLayout();
    cameraModeRow->setContentsMargins(0,0,0,0);
    cameraModeRow->setSpacing(5);
    cameraModeRow->addWidget(cameraDeviceLabel_,1);
    cameraModeRow->addWidget(networkToggle_,0);
    cameraLayout->insertLayout(0,cameraModeRow);
    networkPanel_=new QWidget(cameraPanel_);
    networkPanel_->setObjectName("networkPanel");
    auto* ipLayout=new QVBoxLayout(networkPanel_);
    ipLayout->setContentsMargins(0,1,0,1);
    ipLayout->setSpacing(3);
    networkUrlEdit_=new QLineEdit(networkPanel_);
    networkUrlEdit_->setObjectName("networkUrlEdit");
    networkUrlEdit_->setClearButtonEnabled(true);
    networkUrlEdit_->setMaxLength(1024);
    auto* urlRow=new QHBoxLayout();
    urlRow->setSpacing(4);
    urlRow->setContentsMargins(0,0,0,0);
    urlRow->addWidget(networkUrlEdit_,1);
    networkScanButton_=new QPushButton(networkPanel_);
    networkScanButton_->setObjectName("networkScanButton");
    networkScanButton_->setProperty("networkControl",true);
    urlRow->addWidget(networkScanButton_);
    ipLayout->addLayout(urlRow);
    connect(networkScanButton_,&QPushButton::clicked,this,
            &StudioWindow::showLanScanDialog);
    auto* chooser=new QHBoxLayout();
    chooser->setContentsMargins(0,0,0,0);
    chooser->setSpacing(5);
    networkCombo_=new QComboBox(networkPanel_);
    networkCombo_->setObjectName("networkCombo");
    networkAddButton_=new QPushButton(networkPanel_);
    networkAddButton_->setObjectName("networkAddButton");
    networkAddButton_->setProperty("networkControl",true);
    networkRemoveButton_=new QPushButton(networkPanel_);
    networkRemoveButton_->setObjectName("networkRemoveButton");
    networkRemoveButton_->setProperty("networkControl",true);
    chooser->addWidget(networkCombo_,4);
    chooser->addWidget(networkAddButton_,1);
    chooser->addWidget(networkRemoveButton_,1);
    ipLayout->addLayout(chooser);
    auto* actions=new QHBoxLayout();
    actions->setContentsMargins(0,0,0,0);
    actions->setSpacing(5);
    networkStartButton_=new QPushButton(networkPanel_);
    networkStartButton_->setObjectName("networkStartButton");
    networkStartButton_->setProperty("networkControl",true);
    networkStopButton_=new QPushButton(networkPanel_);
    networkStopButton_->setObjectName("networkStopButton");
    networkStopButton_->setProperty("networkControl",true);
    actions->addWidget(networkStartButton_);
    actions->addWidget(networkStopButton_);
    ipLayout->addLayout(actions);
    networkInsertButton_=new QPushButton(networkPanel_);
    networkInsertButton_->setObjectName("networkInsertButton");
    networkInsertButton_->setProperty("networkControl",true);
    ipLayout->addWidget(networkInsertButton_);
    networkStatusLabel_=new QLabel(networkPanel_);
    networkStatusLabel_->setObjectName("networkStatus");
    networkStatusLabel_->setWordWrap(true);
    networkStatusLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ipLayout->addWidget(networkStatusLabel_);
    cameraLayout->addWidget(networkPanel_);
    networkPanel_->hide();
    connect(networkToggle_,&QToolButton::toggled,this,[this,cameraScroll](bool visible) {
        // Camera sources share only resolution/FPS. When IP is selected, hide
        // USB-specific controls instead of stacking two tall forms.
        cameraDeviceLabel_->setVisible(!visible);
        cameraDeviceCombo_->setVisible(!visible);
        cameraRefreshButton_->setVisible(!visible);
        cameraResolutionLabel_->setVisible(!visible);
        cameraFpsLabel_->setVisible(!visible);
        cameraStartButton_->setVisible(!visible);
        cameraStopButton_->setVisible(!visible);
        cameraInsertButton_->setVisible(!visible);
        cameraStatusLabel_->setVisible(!visible);
        networkPanel_->setVisible(visible);
        networkToggle_->setArrowType(visible?Qt::DownArrow:Qt::RightArrow);
        refreshNetworkStatus();
        if (visible) {
            QTimer::singleShot(0,cameraScroll,[this,cameraScroll]() {
                cameraScroll->ensureWidgetVisible(networkPanel_,0,12);
            });
        }
    });
    connect(networkAddButton_,&QPushButton::clicked,this,[this] {
        QString error;
        if (!addNetworkUrl(networkUrlEdit_->text(),&error)) {
            QMessageBox::warning(this,loc_.tr("Invalid network stream"),error);
            return;
        }
        networkUrlEdit_->clear();
    });
    connect(networkRemoveButton_,&QPushButton::clicked,this,&StudioWindow::removeSelectedNetwork);
    connect(networkStartButton_,&QPushButton::clicked,this,[this] { startSelectedNetwork(); });
    connect(networkStopButton_,&QPushButton::clicked,this,&StudioWindow::stopSelectedNetwork);
    connect(networkInsertButton_,&QPushButton::clicked,this,&StudioWindow::beginNetworkPlacement);
    connect(networkCombo_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int) { refreshNetworkStatus(); });
    cameraLayout->addStretch(1);
    cameraScroll->setWidget(cameraContent);
    sideLayout->addWidget(cameraPanel_,3);
    sideLayout->addStretch(0);
    connect(cameraRefreshButton_,&QPushButton::clicked,this,
            &StudioWindow::refreshCameraDevices);
    connect(cameraStartButton_,&QPushButton::clicked,this,
            [this]() { startSelectedCamera(); });
    connect(cameraStopButton_,&QPushButton::clicked,this,
            &StudioWindow::stopSelectedCamera);
    connect(cameraInsertButton_,&QPushButton::clicked,this,
            &StudioWindow::beginCameraPlacement);
    connect(cameraDeviceCombo_,QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,[this](int) { refreshCameraStatus(); });
    refreshCameraDevices();
    auto* cameraTimer=new QTimer(this);
    cameraTimer->setInterval(650);
    connect(cameraTimer,&QTimer::timeout,this,&StudioWindow::refreshCameraStatus);
    cameraTimer->start();
    connect(cameraTimer,&QTimer::timeout,this,&StudioWindow::refreshNetworkStatus);

    connect(browseImageButton_, &QPushButton::clicked, this, &StudioWindow::addImageFiles);
    connect(insertImageButton_, &QPushButton::clicked, this,
            &StudioWindow::startImagePlacementFromList);
    connect(imageList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { startImagePlacementFromList(); });
    connect(imageAspectBox_, &QCheckBox::toggled, this,
            [this](bool checked) { board_->setSelectedImageAspectLocked(checked); });
    connect(imageFitBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index >= 0) board_->setSelectedImageFillCrop(index==1);
    });
    connect(imageOpacitySlider_, &QSlider::valueChanged, this, [this](int opacity) {
        board_->setSelectedImageOpacity(opacity/100.0);
    });
    connect(imageFrontButton_, &QPushButton::clicked, this,
            [this]() { board_->bringSelectedImageForward(); });
    connect(imageBackButton_, &QPushButton::clicked, this,
            [this]() { board_->sendSelectedImageBackward(); });
    connect(imageRemoveButton_, &QPushButton::clicked, this,
            [this]() { board_->removeSelectedImage(); });

    scenesPanel_=new QWidget(sidebar_);
    scenesPanel_->setObjectName("scenesPanel");
    auto* sceneLayout=new QVBoxLayout(scenesPanel_);
    sceneLayout->setContentsMargins(0,0,0,0);
    sceneLayout->setSpacing(7);
    sceneList_=new QListWidget(scenesPanel_);
    sceneList_->setObjectName("sceneList");
    sceneList_->setMinimumHeight(140);
    sceneList_->setSelectionMode(QAbstractItemView::SingleSelection);
    sceneLayout->addWidget(sceneList_,1);
    auto* sceneRow1=new QHBoxLayout();
    sceneAddButton_=new QPushButton(scenesPanel_);
    sceneAddButton_->setObjectName("sceneAddButton");
    sceneDuplicateButton_=new QPushButton(scenesPanel_);
    sceneDuplicateButton_->setObjectName("sceneDuplicateButton");
    sceneRow1->addWidget(sceneAddButton_);
    sceneRow1->addWidget(sceneDuplicateButton_);
    sceneLayout->addLayout(sceneRow1);
    auto* sceneRow2=new QHBoxLayout();
    sceneRenameButton_=new QPushButton(scenesPanel_);
    sceneRenameButton_->setObjectName("sceneRenameButton");
    sceneDeleteButton_=new QPushButton(scenesPanel_);
    sceneDeleteButton_->setObjectName("sceneDeleteButton");
    sceneRow2->addWidget(sceneRenameButton_);
    sceneRow2->addWidget(sceneDeleteButton_);
    sceneLayout->addLayout(sceneRow2);
    sideLayout->addWidget(scenesPanel_,3);
    connect(sceneList_,&QListWidget::currentRowChanged,this,
            [this](int index) {
        if (index>=0 && index!=project_.currentIndex())
            activateScene(index);
    });
    connect(sceneAddButton_,&QPushButton::clicked,this,[this]{addScene();});
    connect(sceneDuplicateButton_,&QPushButton::clicked,this,
            [this]{duplicateScene();});
    connect(sceneRenameButton_,&QPushButton::clicked,this,&StudioWindow::renameScene);
    connect(sceneDeleteButton_,&QPushButton::clicked,this,&StudioWindow::deleteScene);

    projectsPanel_=new QWidget(sidebar_);
    projectsPanel_->setObjectName("projectsPanel");
    auto* projectLayout=new QVBoxLayout(projectsPanel_);
    projectLayout->setContentsMargins(0,0,0,0);
    projectLayout->setSpacing(9);
    projectSummary_=new QLabel(projectsPanel_);
    projectSummary_->setObjectName("projectSummary");
    projectSummary_->setWordWrap(true);
    projectSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    projectLayout->addWidget(projectSummary_);
    projectNewButton_=new QPushButton(projectsPanel_);
    projectNewButton_->setObjectName("projectNewButton");
    projectOpenButton_=new QPushButton(projectsPanel_);
    projectOpenButton_->setObjectName("projectOpenButton");
    projectSaveButton_=new QPushButton(projectsPanel_);
    projectSaveButton_->setObjectName("projectSaveButton");
    projectLayout->addWidget(projectNewButton_);
    projectLayout->addWidget(projectOpenButton_);
    projectLayout->addWidget(projectSaveButton_);
    recoveryStatus_=new QLabel(projectsPanel_);
    recoveryStatus_->setObjectName("recoveryStatus");
    recoveryStatus_->setWordWrap(true);
    projectLayout->addWidget(recoveryStatus_);
    projectLayout->addStretch(1);
    sideLayout->addWidget(projectsPanel_,3);
    connect(projectNewButton_,&QPushButton::clicked,this,&StudioWindow::createProject);
    connect(projectOpenButton_,&QPushButton::clicked,this,&StudioWindow::openProject);
    connect(projectSaveButton_,&QPushButton::clicked,this,
            [this]{saveProject(false);});
    scenesPanel_->hide();
    projectsPanel_->hide();

    sidebarFooter_ = new QLabel(sidebar_);
    sidebarFooter_->setObjectName("sidebarFooter");
    sidebarFooter_->setWordWrap(true);
    sideLayout->addWidget(sidebarFooter_);
    content->addWidget(sidebar_);

    auto *stage = new QFrame(root);
    stage->setObjectName("stage");
    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(25, 25, 25, 25);
    stageLayout->setSpacing(0);
    board_ = new BoardView(stage);
    board_->setObjectName("board");
    // Status overlay lives on the viewport, not the captured scene, and does
    // not intercept pen/tablet events even in full-screen presentation.
    recordingIndicator_=new QLabel(board_->viewport());
    recordingIndicator_->setObjectName("recordingIndicator");
    recordingIndicator_->setAttribute(Qt::WA_TransparentForMouseEvents,true);
    recordingIndicator_->setStyleSheet(
        "QLabel { background: rgba(25,24,30,205); color: #ff626b;"
        " font-weight: 700; padding: 7px 12px;"
        " border: 1px solid #cc3344; border-radius: 7px; }");
    recordingIndicator_->move(12,12);
    recordingIndicator_->hide();
    stageLayout->addWidget(board_, 1);
    content->addWidget(stage, 1);
    setCentralWidget(root);

    fileMenu_ = menuBar()->addMenu(QString());
    editMenu_ = menuBar()->addMenu(QString());
    editMenu_->setObjectName("editMenu");
    viewMenu_ = menuBar()->addMenu(QString());
    settingsMenu_ = menuBar()->addMenu(QString());
    settingsMenu_->setObjectName("settingsMenu");
    helpMenu_ = menuBar()->addMenu(QString());

    // The menu is ready for F02/F03. Commands remain disabled until there are
    // selectable canvas objects and a real undo/clipboard implementation.
    const char* editNames[] = {
        "Undo", "Redo", "Select", "Select all", "Deselect",
        "Cut", "Copy", "Paste", "Duplicate", "Delete"
    };
    const QKeySequence shortcuts[] = {
        QKeySequence::Undo, QKeySequence::Redo, QKeySequence(),
        QKeySequence::SelectAll, QKeySequence("Ctrl+D"), QKeySequence::Cut,
        QKeySequence::Copy, QKeySequence::Paste, QKeySequence("Ctrl+J"),
        QKeySequence::Delete
    };
    for (int i = 0; i < 10; ++i) {
        if (i == 2 || i == 5) editMenu_->addSeparator();
        auto* action = editMenu_->addAction(QString::fromUtf8(editNames[i]));
        action->setObjectName(QString("editAction_%1").arg(i));
        if (!shortcuts[i].isEmpty()) action->setShortcut(shortcuts[i]);
        action->setEnabled(false);
        editActions_.push_back(action);
    }
    connect(editActions_[0],&QAction::triggered,this,
            [this] {board_->undoStack()->undo();});
    connect(editActions_[1],&QAction::triggered,this,
            [this] {board_->undoStack()->redo();});
    connect(editActions_[2], &QAction::triggered, this, [this]() {
        board_->setTool(BoardView::Tool::Select);
        drawActions_[3]->setChecked(true);
    });
    connect(editActions_[3], &QAction::triggered, board_, &BoardView::selectAllStrokes);
    connect(editActions_[4], &QAction::triggered, board_, &BoardView::deselect);
    connect(editActions_[5], &QAction::triggered, board_, &BoardView::cutSelection);
    connect(editActions_[6], &QAction::triggered, board_, &BoardView::copySelection);
    connect(editActions_[7], &QAction::triggered, board_, &BoardView::pasteSelection);
    connect(editActions_[8], &QAction::triggered, board_, &BoardView::duplicateSelection);
    connect(editActions_[9], &QAction::triggered, board_, &BoardView::deleteSelection);
    configurationAction_ = settingsMenu_->addAction(QString());
    configurationAction_->setObjectName("configurationAction");
    connect(configurationAction_, &QAction::triggered,
            this, &StudioWindow::openConfiguration);

    newProjectAction_=fileMenu_->addAction(QString());
    newProjectAction_->setObjectName("newProjectAction");
    newProjectAction_->setShortcut(QKeySequence("Ctrl+N"));
    connect(newProjectAction_,&QAction::triggered,this,&StudioWindow::createProject);
    openProjectAction_=fileMenu_->addAction(QString());
    openProjectAction_->setObjectName("openProjectAction");
    openProjectAction_->setShortcut(QKeySequence("Ctrl+O"));
    connect(openProjectAction_,&QAction::triggered,this,&StudioWindow::openProject);
    saveProjectAction_=fileMenu_->addAction(QString());
    saveProjectAction_->setObjectName("saveProjectAction");
    saveProjectAction_->setShortcut(QKeySequence("Ctrl+S"));
    connect(saveProjectAction_,&QAction::triggered,this,[this]{saveProject(false);});
    saveProjectAsAction_=fileMenu_->addAction(QString());
    saveProjectAsAction_->setObjectName("saveProjectAsAction");
    saveProjectAsAction_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveProjectAsAction_,&QAction::triggered,this,[this]{saveProject(true);});
    fileMenu_->addSeparator();
    openDocumentAction_ = fileMenu_->addAction(QString());
    openDocumentAction_->setShortcut(QKeySequence("Ctrl+Alt+O"));
    connect(openDocumentAction_, &QAction::triggered, this, &StudioWindow::openDocument);
    saveDocumentAction_ = fileMenu_->addAction(QString());
    saveDocumentAction_->setShortcut(QKeySequence("Ctrl+Alt+S"));
    connect(saveDocumentAction_, &QAction::triggered, this, [this]() { saveDocument(false); });
    saveAsAction_ = fileMenu_->addAction(QString());
    saveAsAction_->setShortcut(QKeySequence("Ctrl+Alt+Shift+S"));
    connect(saveAsAction_, &QAction::triggered, this, [this]() { saveDocument(true); });
    exportPngAction_ = fileMenu_->addAction(QString());
    connect(exportPngAction_, &QAction::triggered, this, &StudioWindow::exportPng);
    exportCurrentViewPngAction_ = fileMenu_->addAction(QString());
    connect(exportCurrentViewPngAction_, &QAction::triggered,
            this, &StudioWindow::exportCurrentViewPng);
    fileMenu_->addSeparator();
    clearDocumentAction_ = fileMenu_->addAction(QString());
    connect(clearDocumentAction_, &QAction::triggered, this, &StudioWindow::clearDocument);
    fileMenu_->addSeparator();
    quitAction_ = fileMenu_->addAction(QString());
    quitAction_->setShortcut(QKeySequence::Quit);
    connect(quitAction_, &QAction::triggered, this, &QWidget::close);
    toggleSidebarAction_ = viewMenu_->addAction(QString());
    toggleSidebarAction_->setCheckable(true);
    toggleSidebarAction_->setChecked(true);
    toggleSidebarAction_->setShortcut(QKeySequence("Ctrl+B"));
    connect(toggleSidebarAction_, &QAction::toggled, this, [this](bool visible) {
        setSidebarOpen(visible);
    });
    toggleDrawingToolsAction_ = viewMenu_->addAction(QString());
    toggleDrawingToolsAction_->setObjectName("toggleDrawingToolsAction");
    toggleDrawingToolsAction_->setCheckable(true);
    toggleDrawingToolsAction_->setChecked(true);
    toggleDrawingToolsAction_->setShortcut(QKeySequence("Ctrl+Shift+T"));
    connect(toggleDrawingToolsAction_, &QAction::toggled, this, [this](bool visible) {
        setDrawingToolsOpen(visible);
    });
    fullscreenAction_ = viewMenu_->addAction(QString());
    fullscreenAction_->setShortcut(QKeySequence(Qt::Key_F11));
    fullscreenAction_->setShortcutContext(Qt::ApplicationShortcut);
    addAction(fullscreenAction_); // works even when toolbar/menu are hidden
    auto* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::ApplicationShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() {
        if (isFullScreen()) setPresentationMode(false);
    });
    connect(fullscreenAction_, &QAction::triggered, this, [this]() {
        setPresentationMode(!isFullScreen());
    });
    fitAction_ = viewMenu_->addAction(QString());
    fitAction_->setShortcut(QKeySequence("Ctrl+0"));
    connect(fitAction_, &QAction::triggered, board_, &BoardView::fitBoard);
    manualAction_ = helpMenu_->addAction(QString());
    manualAction_->setObjectName("manualAction");
    connect(manualAction_,&QAction::triggered,this,[this] {
        const QString lang=loc_.language().startsWith("it",Qt::CaseInsensitive)
            ? "it":"en";
        const QString fileName="manual_"+lang+".html";
        const QString bin=QCoreApplication::applicationDirPath();
        const QStringList candidates={
            bin+"/../share/doc/gx-whiteboard-studio/"+fileName,
            bin+"/../docs/"+fileName
        };
        for (const QString& path:candidates) {
            if (QFileInfo(path).isFile()) {
                if (!QDesktopServices::openUrl(
                        QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()))) {
                    QMessageBox::warning(this,loc_.tr("Manual unavailable"),
                        loc_.tr("Could not open the local manual."));
                }
                return;
            }
        }
        QMessageBox::warning(this,loc_.tr("Manual unavailable"),
            loc_.tr("The local manual could not be found."));
    });
    aboutAction_ = helpMenu_->addAction(QString());
    connect(aboutAction_, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "GX Whiteboard Studio",
            loc_.tr("Version 0.1.0 — Drawing, media, scenes and FFmpeg recording.")+
            QString("\nCopyright (C) 2026 GiuxLab\nGPL-3.0-or-later"));
    });

    toolbar_ = new QToolBar(this);
    toolbar_->setObjectName("topToolbar");
    toolbar_->setMovable(false);
    toolbar_->setFloatable(false);
    toolbar_->setIconSize(QSize(18, 18));
    addToolBar(Qt::TopToolBarArea, toolbar_);
    toolbar_->addAction(toggleSidebarAction_);
    toolbar_->addAction(toggleDrawingToolsAction_);
    toolbar_->addSeparator();
    backgroundLabel_ = new QLabel(toolbar_);
    toolbar_->addWidget(backgroundLabel_);
    backgroundBox_ = new QComboBox(toolbar_);
    backgroundBox_->setObjectName("backgroundBox");
    for (int i = 0; i < 3; ++i) backgroundBox_->addItem(QString(), i);
    backgroundBox_->setMinimumWidth(116);
    toolbar_->addWidget(backgroundBox_);
    connect(backgroundBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) {
        if (i >= 0 && i <= 2) setBoardMode(static_cast<BoardView::Background>(i));
    });
    toolbar_->addSeparator();
    toolbar_->addAction(fitAction_);
    zoomOutAction_ = toolbar_->addAction(QString());
    zoomOutAction_->setObjectName("zoomOutAction");
    zoomOutAction_->setShortcut(QKeySequence("Ctrl+-"));
    zoomOutAction_->setShortcutContext(Qt::WindowShortcut);
    connect(zoomOutAction_, &QAction::triggered, this, [this]() {
        board_->zoomAt(1.0 / 1.18, board_->viewport()->rect().center());
    });
    zoomInAction_ = toolbar_->addAction(QString());
    zoomInAction_->setObjectName("zoomInAction");
    zoomInAction_->setShortcut(QKeySequence("Ctrl++"));
    zoomInAction_->setShortcutContext(Qt::WindowShortcut);
    connect(zoomInAction_, &QAction::triggered, this, [this]() {
        board_->zoomAt(1.18, board_->viewport()->rect().center());
    });
    toolbar_->addAction(fullscreenAction_);
    auto *spacer = new QWidget(toolbar_);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar_->addWidget(spacer);
    toolbar_->addSeparator();
    recordAction_=toolbar_->addAction(QString());
    recordAction_->setObjectName("recordAction");
    recordAction_->setShortcut(QKeySequence("Ctrl+Shift+R"));
    recordAction_->setShortcutContext(Qt::ApplicationShortcut);
    addAction(recordAction_);
    connect(recordAction_,&QAction::triggered,this,[this] {
        if (recorder_->active()) stopRecording();
        else openRecordingDialog();
    });
    stopRecordAction_=toolbar_->addAction(QString());
    stopRecordAction_->setObjectName("stopRecordAction");
    connect(stopRecordAction_,&QAction::triggered,this,
            &StudioWindow::stopRecording);
    muteRecordAction_=toolbar_->addAction(QString());
    muteRecordAction_->setObjectName("muteRecordAction");
    muteRecordAction_->setCheckable(true);
    muteRecordAction_->setShortcut(QKeySequence("Ctrl+Shift+M"));
    muteRecordAction_->setShortcutContext(Qt::ApplicationShortcut);
    addAction(muteRecordAction_);
    connect(muteRecordAction_,&QAction::toggled,this,[this](bool mute) {
        if (!recorder_->active()) return;
        QString error;
        if (!recorder_->setMuted(mute,&error)) {
            const QSignalBlocker block(muteRecordAction_);
            muteRecordAction_->setChecked(recorder_->muted());
            statusBar()->showMessage(error,6500);
        }
        updateRecordingUi();
    });
    recordingDuration_=new QLabel(statusBar());
    recordingDuration_->setObjectName("recordingDuration");
    statusBar()->addPermanentWidget(recordingDuration_);
    recordingMeter_=new QProgressBar(statusBar());
    recordingMeter_->setObjectName("recordingMeter");
    recordingMeter_->setRange(0,100);
    recordingMeter_->setValue(0);
    recordingMeter_->setFixedWidth(66);
    recordingMeter_->setMaximumHeight(11);
    recordingMeter_->setTextVisible(false);
    statusBar()->addPermanentWidget(recordingMeter_);
    if (auto* button=toolbar_->widgetForAction(recordAction_))
        button->setObjectName("recordButton");
    if (auto* button=toolbar_->widgetForAction(stopRecordAction_))
        button->setObjectName("stopRecordButton");
    recorder_=new RecordingEngine(this);
    auto* recordTimer=new QTimer(this);
    recordTimer->setInterval(500);
    connect(recordTimer,&QTimer::timeout,this,&StudioWindow::updateRecordingUi);
    recordTimer->start();

    addToolBarBreak(Qt::TopToolBarArea);
    drawingToolbar_ = new QToolBar(this);
    drawingToolbar_->setObjectName("drawingToolbar");
    drawingToolbar_->setMovable(false);
    drawingToolbar_->setFloatable(false);
    addToolBar(Qt::TopToolBarArea, drawingToolbar_);
    drawingGroup_ = new QActionGroup(this);
    drawingGroup_->setExclusive(true);
    const BoardView::Tool kinds[] = {
        BoardView::Tool::Pen, BoardView::Tool::Highlighter,
        BoardView::Tool::Eraser, BoardView::Tool::Select,
        BoardView::Tool::Line, BoardView::Tool::Arrow,
        BoardView::Tool::Rectangle, BoardView::Tool::Ellipse,
        BoardView::Tool::Text
    };
    for (int i=0; i<9; ++i) {
        auto* action = drawingToolbar_->addAction(QString("Tool %1").arg(i));
        action->setObjectName(QString("drawingTool_%1").arg(i));
        action->setCheckable(true);
        drawingGroup_->addAction(action);
        drawActions_[i] = action;
        const BoardView::Tool kind = kinds[i];
        connect(action, &QAction::triggered, this, [this, kind]() {
            board_->setTool(kind);
            refreshEditActions();
        });
    }
    drawActions_[0]->setChecked(true);
    drawingToolbar_->addSeparator();
    colorLabel_ = new QLabel(drawingToolbar_);
    drawingToolbar_->addWidget(colorLabel_);
    colorBox_ = new QComboBox(drawingToolbar_);
    colorBox_->setObjectName("colorBox");
    const QColor colors[] = {
        QColor("#253b5a"), QColor("#1b7acb"), QColor("#c13737"),
        QColor("#22854d"), QColor("#e6a800"), QColor("#fafafa")
    };
    for (const auto& color : colors) colorBox_->addItem(QString(), color);
    colorBox_->setCurrentIndex(0);
    colorBox_->setMinimumWidth(103);
    drawingToolbar_->addWidget(colorBox_);
    connect(colorBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) {
        if (i>=0) board_->setStrokeColor(colorBox_->itemData(i).value<QColor>());
    });
    drawingToolbar_->addSeparator();
    widthLabel_ = new QLabel(drawingToolbar_);
    drawingToolbar_->addWidget(widthLabel_);
    widthBox_ = new QComboBox(drawingToolbar_);
    widthBox_->setObjectName("widthBox");
    for (int value : {3, 5, 8, 12, 18, 28})
        widthBox_->addItem(QString::number(value), value);
    widthBox_->setCurrentIndex(2);
    widthBox_->setMinimumWidth(65);
    drawingToolbar_->addWidget(widthBox_);
    connect(widthBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int i) {
        if (i >= 0) board_->setStrokeWidth(widthBox_->itemData(i).toDouble());
    });
    statusMessage_ = new QLabel(this);
    statusMessage_->setObjectName("statusMessage");
    statusProfile_ = new QLabel(this);
    statusProfile_->setObjectName("statusProfile");
    statusBar()->addPermanentWidget(statusMessage_, 1);
    statusBar()->addPermanentWidget(statusProfile_);
    selectSection(0);
    refreshImageControls();
}
void StudioWindow::applyStyle() {
    setStyleSheet(R"(
        QMainWindow { background: #171b27; color: #e9edf6; }
        QMenuBar, QMenu, QToolBar, QStatusBar { background: #202637; color: #f1f5fb; }
        QMenuBar::item:selected, QMenu::item:selected { background: #34455b; }
        QMenu { border: 1px solid #465268; padding: 4px; }
        QToolBar { border: 0; border-bottom: 1px solid #394357; padding: 6px 8px; spacing: 7px; }
        QToolBar QLabel { color: #b9c7d9; font-size: 12px; margin-right: 4px; }
        QToolBar QToolButton { color: #eaf0f8; background: #2a3448; border: 1px solid #43516b; border-radius: 6px; padding: 6px 9px; }
        QToolBar QToolButton:hover { background: #3b4c68; }
        QToolButton#recordButton { border-color:#b84e58; color:#ffb2b9; }
        QToolButton#stopRecordButton { border-color:#69646e; }
        QLabel#recordingDuration { color:#f9c7c7; min-width:38px; }
        QProgressBar#recordingMeter { background:#283348; border-radius:3px;
            border:1px solid #4c5a71; }
        QProgressBar#recordingMeter::chunk {background:#6cb987;}
        QComboBox { background: #29364b; border: 1px solid #52637e; border-radius: 5px; padding: 6px 8px; color: #fafcff; }
        QComboBox QAbstractItemView { color: #f9fbff; background: #29364b; selection-background-color: #445f88; }
        QFrame#sidebar { background: #202637; border-right: 1px solid #3a4658; }
        QLabel#brand { color: #f7faff; font-size: 20px; font-weight: 700; letter-spacing: 1px; }
        QLabel#smallHeading { color: #9daec3; font-size: 11px; font-weight: 700; }
        QPushButton#sideNav { background: transparent; border: 1px solid transparent; border-radius: 7px; color: #d6e2f2; font-size: 14px; padding: 8px 12px; text-align: left; }
        QPushButton#sideNav:hover { background: #2c3c55; }
        QPushButton#sideNav:checked { background: #304c70; color: #ffffff; border: 1px solid #5784bb; }
        QLabel#sectionTitle { color: #f0f5ff; font-size: 15px; font-weight: 600; }
        QLabel#sectionText { color: #a9bbd0; font-size: 13px; }
        QLabel#sidebarFooter { color: #8ea2bb; font-size: 11px; border-top: 1px solid #3a4658; padding-top: 12px; }
        QFrame#stage { background: #181e2c; }
        QStatusBar { border-top: 1px solid #394357; }
        QStatusBar QLabel { color: #b3c4d9; padding: 0 10px; }
        QWidget#imagePanel { background: #202637; }
        QWidget#imagePropertiesPanel { background: #202637; }
        QLabel#imagePropertiesHeading {
            color: #f0f5ff; font-size: 12px; font-weight: 600;
        }
        QListWidget#imageLibrary {
            background: #1a2638; color: #f1f5fb;
            border: 1px solid #43546b; border-radius: 6px;
        }
        QListWidget#imageLibrary::item {
            border: 1px solid transparent; border-radius: 5px;
        }
        QListWidget#imageLibrary::item:selected {
            background: #304c70; border: 2px solid #73a5e5;
        }
        QListWidget#sceneList {
            background:#1b273a; color:#e5edf9;
            border:1px solid #4b5d75; border-radius:5px;
        }
        QListWidget#sceneList::item { padding:8px 6px; }
        QListWidget#sceneList::item:selected {
            background:#41648d; color:#ffffff;
        }
        QWidget#scenesPanel QPushButton, QWidget#projectsPanel QPushButton {
            background:#304663; color:#eef4fc; border:1px solid #49658b;
            border-radius:5px; min-height:26px; padding:5px 7px;
        }
        QWidget#scenesPanel QPushButton:disabled {
            background:#252f40;color:#8395ab;
        }
        QLabel#projectSummary {color:#e7eef8;font-size:12px;}
        QLabel#recoveryStatus {color:#a4bed7;font-size:11px;}
        QListWidget#imageLibrary QScrollBar:vertical {
            background: #202637; width: 6px; margin: 0; border: none;
        }
        QListWidget#imageLibrary QScrollBar::handle:vertical {
            background: #617089; min-height: 24px; border-radius: 3px;
        }
        QListWidget#imageLibrary QScrollBar::handle:vertical:hover {
            background: #8aa3c2;
        }
        QListWidget#imageLibrary QScrollBar::add-line:vertical,
        QListWidget#imageLibrary QScrollBar::sub-line:vertical {
            height: 0px; background: none;
        }
        QListWidget#imageLibrary QScrollBar::add-page:vertical,
        QListWidget#imageLibrary QScrollBar::sub-page:vertical {
            background: none;
        }
        QPushButton#browseImagesButton, QPushButton#insertImageButton,
        QPushButton#imageFrontButton, QPushButton#imageBackButton,
        QPushButton#imageRemoveButton { color: #eef4ff; background: #304663;
            border: 1px solid #49658b; border-radius: 5px; padding: 6px 8px; }
        QPushButton#browseImagesButton:hover, QPushButton#insertImageButton:hover,
        QPushButton#imageFrontButton:hover, QPushButton#imageBackButton:hover,
        QPushButton#imageRemoveButton:hover { background: #416087; }
        QPushButton#browseImagesButton:disabled, QPushButton#insertImageButton:disabled,
        QPushButton#imageFrontButton:disabled, QPushButton#imageBackButton:disabled,
        QPushButton#imageRemoveButton:disabled, QPushButton#cameraAction:disabled {
            color: #91a0b1; background: #252d3b; }
        QPushButton#cameraAction { color: #eef4ff; background: #304663;
            border: 1px solid #49658b; border-radius: 4px;
            padding: 3px 5px; min-height: 22px; font-size: 12px; }
        QPushButton#cameraAction:hover { background: #416087; }
        QPushButton[networkControl="true"] { color: #eef4ff; background: #304663;
            border: 1px solid #49658b; border-radius: 4px;
            padding: 3px 4px; min-height: 22px; font-size: 12px; }
        QPushButton[networkControl="true"]:hover { background: #416087; }
        QPushButton[networkControl="true"]:disabled { color: #91a0b1; background: #252d3b; }
        QToolButton#networkToggle {
            color: #e5ecfa; text-align: left; background: #29374b;
            border: 1px solid #475b74; border-radius: 5px;
            padding: 5px 7px; font-weight: 600;
        }
        QWidget#networkPanel { background: #202637; }
        QLabel#networkStatus { color: #bacbdf; font-size: 11px; }
        QLineEdit#networkUrlEdit { background: #29364b; color: #f6f8fa;
            border: 1px solid #52637e; border-radius: 4px; padding: 5px; }
        QScrollArea#cameraPanel { border: none; background: #202637; }
        QWidget#cameraContent { background: #202637; }
        QLabel#cameraStatus { color: #f0f5ff; font-weight: 600; font-size: 12px; }
        QLabel#cameraNote { color: #9aaec7; font-size: 11px; }
        QScrollArea#cameraPanel QScrollBar:vertical {
            background: #202637; width: 6px; margin: 0; border: none;
        }
        QScrollArea#cameraPanel QScrollBar::handle:vertical {
            background: #617089; min-height: 24px; border-radius: 3px;
        }
        QScrollArea#cameraPanel QScrollBar::handle:vertical:hover {
            background: #8aa3c2;
        }
        QScrollArea#cameraPanel QScrollBar::add-line:vertical,
        QScrollArea#cameraPanel QScrollBar::sub-line:vertical {
            height: 0px; background: none;
        }
        QScrollArea#cameraPanel QScrollBar::add-page:vertical,
        QScrollArea#cameraPanel QScrollBar::sub-page:vertical {
            background: none;
        }
        QSlider#imageOpacity::groove:horizontal { height: 6px; background: #52617a; }
        QSlider#imageOpacity::handle:horizontal { width: 15px; background: #94bde9;
            margin: -5px 0; border-radius: 7px; }
        QCheckBox { color: #d7e2f1; }
    )");
}
void StudioWindow::selectSection(int index) {
    section_ = qBound(0, index, 3);
    for (int i = 0; i < 4; ++i) sections_[i]->setChecked(section_ == i);
    refreshSection();
    imagePanel_->setVisible(section_ == 0);
    cameraPanel_->setVisible(section_ == 1);
    scenesPanel_->setVisible(section_ == 2);
    projectsPanel_->setVisible(section_ == 3);
    refreshImageControls();
    // The selected Cameras tab already identifies this section; repeating a
    // heading and explanatory paragraph wastes space needed by live controls.
    sectionTitle_->setVisible(false);
    sectionText_->setVisible(false);
}
void StudioWindow::refreshSection() {
    const char *titles[] = {"Images", "Cameras", "Scenes", "Projects"};
    const char *descriptions[] = {
        "Import images, draw their placement rectangle, and resize the box with eight handles.",
        "Start a USB camera and insert its live feed in a resizable box.",
        "Manage, duplicate and rename independent whiteboard scenes.",
        "Save, open and recover full multi-scene projects."
    };
    sectionTitle_->setText(loc_.tr(titles[section_]));
    sectionText_->setText(loc_.tr(descriptions[section_]));
}
void StudioWindow::updateLanguage() {
    const auto t = [this](const char* text) { return loc_.tr(text); };
    setWindowTitle("GX Whiteboard Studio — " + t("Whiteboard"));
    brand_->setText("GX  /  WHITEBOARD");
    sidebarLabel_->setText(t("LIBRARY"));
    const char *sections[] = {"Images", "Cameras", "Scenes", "Projects"};
    for (int i = 0; i < 4; ++i) sections_[i]->setText(t(sections[i]));
    refreshSection();
    sidebarFooter_->setText(t("GX Whiteboard Studio 0.1.0 — project and recording workspace."));
    fileMenu_->setTitle(t("File"));
    sceneAddButton_->setText(t("New scene"));
    sceneDuplicateButton_->setText(t("Duplicate scene"));
    sceneRenameButton_->setText(t("Rename"));
    sceneDeleteButton_->setText(t("Delete scene"));
    projectNewButton_->setText(t("New project"));
    projectOpenButton_->setText(t("Open project..."));
    projectSaveButton_->setText(t("Save project"));
    newProjectAction_->setText(t("New project"));
    openProjectAction_->setText(t("Open project..."));
    saveProjectAction_->setText(t("Save project"));
    saveProjectAsAction_->setText(t("Save project as..."));
    refreshSceneUi();
    editMenu_->setTitle(t("Edit"));
    viewMenu_->setTitle(t("View"));
    settingsMenu_->setTitle(t("Settings"));
    helpMenu_->setTitle(t("Help"));
    configurationAction_->setText(t("Configuration..."));
    const char* editNames[] = {
        "Undo", "Redo", "Select", "Select all", "Deselect",
        "Cut", "Copy", "Paste", "Duplicate", "Delete"
    };
    for (int i = 0; i < editActions_.size(); ++i) {
        editActions_[i]->setText(t(editNames[i]));
        editActions_[i]->setToolTip(t(editNames[i]));
        editActions_[i]->setStatusTip(t(editNames[i]));
    }
    networkToggle_->setText(t("IP"));
    networkToggle_->setToolTip(t("IP streams (RTSP / MJPEG)"));
    networkUrlEdit_->setPlaceholderText("rtsp://192.168.1.10/stream");
    networkUrlEdit_->setToolTip(t("Private or public IP addresses and DNS names are accepted. No credentials in F05."));
    networkScanButton_->setText(t("Scan LAN"));
    networkScanButton_->setToolTip(t("Scan a private LAN subnet for possible camera services"));
    networkAddButton_->setText(t("Add"));
    networkRemoveButton_->setText(t("Remove"));
    networkStartButton_->setText(t("Start"));
    networkStopButton_->setText(t("Stop"));
    networkInsertButton_->setText(t("Insert IP box"));
    networkInsertButton_->setToolTip(t("Draw the IP video rectangle with the pen"));
    refreshNetworkStatus();
    cameraDeviceLabel_->setText(t("Camera"));
    cameraRefreshButton_->setText(t("Refresh"));
    cameraRefreshButton_->setToolTip(t("Refresh cameras"));
    cameraResolutionLabel_->setText(t("Resolution"));
    cameraResolutionCombo_->setToolTip(t("Resolution"));
    cameraFpsLabel_->setText(t("FPS"));
    cameraFpsCombo_->setToolTip(t("FPS"));
    cameraStartButton_->setText(t("Start"));
    cameraStartButton_->setToolTip(t("Start live feed") + QString(" — ") +
                                   t("Live feed starts only on request."));
    cameraStopButton_->setText(t("Stop"));
    cameraStopButton_->setToolTip(t("Stop live feed"));
    cameraInsertButton_->setText(t("Insert box"));
    cameraInsertButton_->setToolTip(t("Insert camera box with pen"));
    // Privacy information remains available as a tooltip, not a persistent
    // multi-line paragraph occupying the camera controls' working area.
    cameraNoteLabel_->setToolTip(t("Live feed starts only on request."));
    cameraNoteLabel_->hide();
    refreshCameraStatus();
    browseImageButton_->setText(t("Add"));
    browseImageButton_->setToolTip(t("Add images..."));
    insertImageButton_->setText(t("Insert"));
    insertImageButton_->setToolTip(t("Insert with pen"));
    imageAspectBox_->setText(t("Keep aspect ratio"));
    imageFitLabel_->setText(t("Image fitting"));
    imageFitBox_->setToolTip(t("Image fitting"));
    imageFitBox_->setItemText(0,t("Contain"));
    imageFitBox_->setItemText(1,t("Crop"));
    imageFitBox_->setItemData(0,t("Fit / contain"),Qt::ToolTipRole);
    imageFitBox_->setItemData(1,t("Fill / crop"),Qt::ToolTipRole);
    imageOpacityLabel_->setText(t("Opacity"));
    imageFrontButton_->setText(t("Forward"));
    imageBackButton_->setText(t("Backward"));
    imageRemoveButton_->setText(t("Remove"));
    imageRemoveButton_->setToolTip(t("Remove image"));
    refreshImageControls();
    const char* toolNames[] = {
        "Pen", "Highlighter", "Eraser", "Select", "Line",
        "Arrow", "Rectangle", "Ellipse", "Text"
    };
    for (int i=0; i<9; ++i) {
        drawActions_[i]->setText(t(toolNames[i]));
        drawActions_[i]->setToolTip(t(toolNames[i]));
    }
    const char* colorNames[] = {"Dark blue", "Blue", "Red", "Green", "Yellow", "White"};
    for (int i=0; i<6; ++i) colorBox_->setItemText(i, t(colorNames[i]));
    colorLabel_->setText(t("Color:"));
    widthLabel_->setText(t("Size:"));
    openDocumentAction_->setText(t("Open board..."));
    saveDocumentAction_->setText(t("Save board"));
    saveAsAction_->setText(t("Save board as..."));
    exportPngAction_->setText(t("Export PNG..."));
    exportCurrentViewPngAction_->setText(t("Export current view PNG..."));
    clearDocumentAction_->setText(t("Clear board"));
    quitAction_->setText(t("Quit"));
    toggleSidebarAction_->setText(t("Sidebar"));
    toggleDrawingToolsAction_->setText(t("Drawing tools"));
    toggleDrawingToolsAction_->setToolTip(t("Show or hide the drawing tools toolbar"));
    fullscreenAction_->setText(t("Fullscreen"));
    fitAction_->setText(t("Fit canvas"));
    zoomOutAction_->setText(t("Zoom −"));
    zoomOutAction_->setToolTip(t("Zoom out"));
    zoomOutAction_->setStatusTip(t("Zoom out"));
    zoomInAction_->setText(t("Zoom +"));
    zoomInAction_->setToolTip(t("Zoom in"));
    zoomInAction_->setStatusTip(t("Zoom in"));
    manualAction_->setText(t("User manual"));
    aboutAction_->setText(t("About"));
    recordAction_->setText(t("REC"));
    recordAction_->setToolTip(t("Start or stop video recording (Ctrl+Shift+R)"));
    stopRecordAction_->setText(t("Stop"));
    stopRecordAction_->setToolTip(t("Stop recording and finalize MKV"));
    muteRecordAction_->setText(t("Mute"));
    muteRecordAction_->setToolTip(t("Mute only the recording application microphone"));
    updateRecordingUi();
    backgroundLabel_->setText(t("Background:"));
    const QSignalBlocker blocker(backgroundBox_);
    backgroundBox_->setItemText(0, t("White"));
    backgroundBox_->setItemText(1, t("Dark"));
    backgroundBox_->setItemText(2, t("Grid"));
    statusMessage_->setText(t("Draw on the canvas"));
    statusProfile_->setText("1920 × 1080  |  16:9");
    refreshEditActions();
    board_->setWatermark(t("YOUR WHITEBOARD"),
            t("Draw here with your Wacom pen or mouse."));
}
void StudioWindow::setLanguage(const QString& locale, bool savePreference) {
    loc_.setLanguage(locale);
    QLocale::setDefault(QLocale(loc_.language()));
    qApp->removeTranslator(&qtTranslator_);
    if (qtTranslator_.load("qtbase_" + loc_.language().left(2),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        qApp->installTranslator(&qtTranslator_);
    }
    updateLanguage();
    if (savePreference) settings_.setValue("language", loc_.language());
}
bool StudioWindow::allEditActionsDisabled() const {
    if (editActions_.size() != 10) return false;
    for (const auto *action : editActions_) if (action->isEnabled()) return false;
    return true;
}
bool StudioWindow::defaultFullscreen() const {
    return settings_.value("startupFullscreen", false).toBool();
}
void StudioWindow::applyStartupMode() {
    if (defaultFullscreen()) setPresentationMode(true);
}
void StudioWindow::openConfiguration() {
    QDialog dialog(this);
    dialog.setObjectName("configurationDialog");
    dialog.setWindowTitle(loc_.tr("Configuration"));
    dialog.setMinimumWidth(490);
    dialog.setModal(true);
    dialog.setStyleSheet(R"(
        QDialog { background: #202637; color: #f2f6fb; }
        QLabel, QCheckBox { color: #d6e2f0; font-size: 13px; }
        QLabel#configHeading { color: #ffffff; font-size: 18px; font-weight: 700; }
        QLabel#configNote { color: #9eb1c9; font-size: 12px; }
        QGroupBox { color: #e8f2ff; border: 1px solid #4b5972;
                    border-radius: 7px; margin-top: 12px; padding: 10px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; }
        QComboBox { color: #ffffff; background: #29364b; border: 1px solid #647995;
                    padding: 6px 10px; border-radius: 5px; min-height: 20px; }
        QComboBox QAbstractItemView { color: white; background: #29364b;
                     selection-background-color: #46648b; }
        QPushButton { color: #ffffff; background: #304663; border: 1px solid #56749b;
                      border-radius: 5px; padding: 7px 18px; min-width: 75px; }
        QPushButton:hover { background: #43648e; }
        QCheckBox::indicator { width: 17px; height: 17px; }
    )");
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(22, 19, 22, 20);
    layout->setSpacing(16);
    auto* heading = new QLabel(loc_.tr("Application configuration"), &dialog);
    heading->setObjectName("configHeading");
    layout->addWidget(heading);
    auto* group = new QGroupBox(loc_.tr("Startup and display"), &dialog);
    auto* form = new QFormLayout(group);
    form->setSpacing(14);
    form->setLabelAlignment(Qt::AlignRight);

    auto* language = new QComboBox(group);
    language->setObjectName("settingsLanguage");
    language->addItem("Italiano", "it_IT");
    language->addItem("English", "en_US");
    language->setCurrentIndex(loc_.language() == "it_IT" ? 0 : 1);
    form->addRow(loc_.tr("Language:"), language);

    auto* startup = new QComboBox(group);
    startup->setObjectName("settingsStartupView");
    startup->addItem(loc_.tr("Windowed"), false);
    startup->addItem(loc_.tr("Fullscreen"), true);
    startup->setCurrentIndex(defaultFullscreen() ? 1 : 0);
    form->addRow(loc_.tr("Startup view:"), startup);

    auto* background = new QComboBox(group);
    background->setObjectName("settingsBackground");
    background->addItem(loc_.tr("White"), 0);
    background->addItem(loc_.tr("Dark"), 1);
    background->addItem(loc_.tr("Grid"), 2);
    background->setCurrentIndex(qBound(0, settings_.value("background", 0).toInt(), 2));
    form->addRow(loc_.tr("Default background:"), background);

    auto* sidebar = new QCheckBox(loc_.tr("Show sidebar at startup"), group);
    sidebar->setObjectName("settingsSidebar");
    sidebar->setChecked(settings_.value("sidebarVisible", true).toBool());
    form->addRow(QString(), sidebar);

    layout->addWidget(group);

    auto* inputGroup = new QGroupBox(loc_.tr("Input devices"), &dialog);
    auto* inputForm = new QFormLayout(inputGroup);
    inputForm->setSpacing(14);
    inputForm->setLabelAlignment(Qt::AlignRight);
    auto* mousePan = new QComboBox(inputGroup);
    mousePan->setObjectName("settingsMousePan");
    mousePan->addItem(loc_.tr("Right or middle button"), 2);
    mousePan->addItem(loc_.tr("Right button"), 0);
    mousePan->addItem(loc_.tr("Middle button"), 1);
    mousePan->addItem(loc_.tr("Disabled"), 3);
    const int currentMousePan = qBound(0, settings_.value("mousePanMode", 2).toInt(), 3);
    mousePan->setCurrentIndex(mousePan->findData(currentMousePan));
    inputForm->addRow(loc_.tr("Pan canvas with mouse:"), mousePan);

    auto* penSide = new QComboBox(inputGroup);
    penSide->setObjectName("settingsPenSide");
    penSide->addItem(loc_.tr("Pan canvas"), 0);
    penSide->addItem(loc_.tr("Temporary eraser"), 1);
    penSide->addItem(loc_.tr("No special action"), 2);
    penSide->setCurrentIndex(qBound(0, settings_.value("penSideMode", 0).toInt(), 2));
    inputForm->addRow(loc_.tr("Wacom side button:"), penSide);

    auto* wheelZoom = new QCheckBox(loc_.tr("Zoom with mouse wheel"), inputGroup);
    wheelZoom->setObjectName("settingsWheelZoom");
    wheelZoom->setChecked(settings_.value("wheelZoomEnabled", true).toBool());
    inputForm->addRow(QString(), wheelZoom);
    layout->addWidget(inputGroup);

    auto* note = new QLabel(loc_.tr(
        "Language and background update when saved. Startup view takes effect next time the application opens."),
        &dialog);
    note->setObjectName("configNote");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(loc_.tr("Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(loc_.tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    settings_.setValue("startupFullscreen", startup->currentData().toBool());
    setBoardMode(static_cast<BoardView::Background>(background->currentData().toInt()));
    setSidebarOpen(sidebar->isChecked());
    settings_.setValue("mousePanMode", mousePan->currentData().toInt());
    settings_.setValue("penSideMode", penSide->currentData().toInt());
    settings_.setValue("wheelZoomEnabled", wheelZoom->isChecked());
    board_->setMousePanMode(static_cast<BoardView::MousePanMode>(mousePan->currentData().toInt()));
    board_->setPenSideMode(static_cast<BoardView::PenSideMode>(penSide->currentData().toInt()));
    board_->setWheelZoomEnabled(wheelZoom->isChecked());
    setLanguage(language->currentData().toString(), true);
    settings_.sync();
    statusBar()->showMessage(loc_.tr("Configuration saved."), 3500);
}

QString StudioWindow::sidebarHeading() const { return sidebarLabel_->text(); }
QString StudioWindow::gridOptionText() const { return backgroundBox_->itemText(2); }
void StudioWindow::setSidebarOpen(bool visible) {
    if (isFullScreen()) {
        savedSidebarBeforeFullscreen_ = visible;
        {
            const QSignalBlocker block(toggleSidebarAction_);
            toggleSidebarAction_->setChecked(visible);
        }
        settings_.setValue("sidebarVisible", visible);
        return;
    }
    sidebar_->setVisible(visible);
    if (toggleSidebarAction_->isChecked() != visible) {
        const QSignalBlocker block(toggleSidebarAction_);
        toggleSidebarAction_->setChecked(visible);
    }
    settings_.setValue("sidebarVisible", visible);
    // BoardView::resizeEvent fits only the default view; custom pan/zoom is preserved.
}
bool StudioWindow::sidebarOpen() const { return sidebar_->isVisible(); }
void StudioWindow::setDrawingToolsOpen(bool visible) {
    if (toggleDrawingToolsAction_->isChecked() != visible) {
        const QSignalBlocker blocker(toggleDrawingToolsAction_);
        toggleDrawingToolsAction_->setChecked(visible);
    }
    settings_.setValue("drawingToolsVisible", visible);
    if (!isFullScreen()) drawingToolbar_->setVisible(visible);
    // BoardView::resizeEvent fits only the default view; custom pan/zoom is preserved.
}
bool StudioWindow::drawingToolsOpen() const {
    // Return the preference, even while presentation mode hides all chrome.
    return toggleDrawingToolsAction_->isChecked();
}
void StudioWindow::setPresentationMode(bool enabled) {
    if (enabled == isFullScreen()) return;
    if (enabled) {
        savedSidebarBeforeFullscreen_ = toggleSidebarAction_->isChecked();
        sidebar_->hide();
        toolbar_->hide();
        drawingToolbar_->hide();
        menuBar()->hide();
        statusBar()->hide();
        showFullScreen();
    } else {
        showNormal();
        menuBar()->show();
        toolbar_->show();
        drawingToolbar_->setVisible(toggleDrawingToolsAction_->isChecked());
        statusBar()->show();
        sidebar_->setVisible(savedSidebarBeforeFullscreen_);
        {
            const QSignalBlocker block(toggleSidebarAction_);
            toggleSidebarAction_->setChecked(savedSidebarBeforeFullscreen_);
        }
    }
    // BoardView::resizeEvent fits only the default view; custom pan/zoom is preserved.
}
void StudioWindow::setBoardMode(BoardView::Background mode) {
    if (board_->boardBackground()!=mode && !projectLoading_ &&
        project_.count()>0)
        project_.setDirty();
    board_->setBoardBackground(mode);
    if (backgroundBox_->currentIndex() != int(mode)) {
        const QSignalBlocker block(backgroundBox_);
        backgroundBox_->setCurrentIndex(int(mode));
    }
    settings_.setValue("background", int(mode));
}

void StudioWindow::refreshSceneUi() {
    if (!sceneList_ || !projectSummary_ || project_.count()==0) return;
    const auto displaySceneTitle=[this](const QString& name) {
        const QRegularExpression automaticallyNamed(R"(^Scene (\d+)( copy)?$)");
        const auto match=automaticallyNamed.match(name);
        if (!match.hasMatch()) return name;
        return loc_.tr("Scene")+" "+match.captured(1)+
            (match.captured(2).isEmpty()?QString():" "+loc_.tr("copy"));
    };
    bool listChanged=sceneList_->count()!=project_.count();
    if (!listChanged) {
        for (int i=0;i<project_.count();++i) {
            if (sceneList_->item(i)->text()!=
                displaySceneTitle(project_.sceneTitle(i))) {
                listChanged=true;
                break;
            }
        }
    }
    if (listChanged || sceneList_->currentRow()!=project_.currentIndex()) {
        const QSignalBlocker ignore(sceneList_);
        if (listChanged) {
            sceneList_->clear();
            for (int i=0;i<project_.count();++i) {
                auto* item=new QListWidgetItem(
                    displaySceneTitle(project_.sceneTitle(i)),sceneList_);
                item->setData(Qt::UserRole,i);
                item->setToolTip(project_.sceneTitle(i));
            }
        }
        sceneList_->setCurrentRow(project_.currentIndex());
    }
    sceneDuplicateButton_->setEnabled(project_.count()<24);
    sceneAddButton_->setEnabled(project_.count()<24);
    sceneDeleteButton_->setEnabled(project_.count()>1);
    projectSummary_->setText(
        loc_.tr("Project: ")+
            (project_.title()=="Untitled project"
               ? loc_.tr("Untitled project"):project_.title())+"\n"+
        loc_.tr("Scenes: ")+QString::number(project_.count())+"\n"+
        loc_.tr("File: ")+(project_.filename().isEmpty()
            ? loc_.tr("Not saved"):QFileInfo(project_.filename()).fileName())+"\n"+
        loc_.tr("Changes: ")+(project_.dirty()
            ? loc_.tr("Not saved"):loc_.tr("Saved")));
    const QString recovery=project_.recoveryPath();
    recoveryStatus_->setText(loc_.tr("Recovery checkpoint: ")+
        (QFileInfo::exists(recovery)
            ? loc_.tr("Available"):loc_.tr("None yet")));
}
bool StudioWindow::activateScene(int index) {
    if (index<0 || index>=project_.count()) return false;
    if (index==project_.currentIndex()) return true;
    const bool previous=projectLoading_;
    projectLoading_=true;
    QString error;
    const bool result=project_.select(index,board_,&error);
    projectLoading_=previous;
    if (!result) {
        statusBar()->showMessage(loc_.tr("Cannot switch scene: ")+error,8000);
        refreshSceneUi();
        return false;
    }
    applySceneVisuals();
    refreshSceneUi();
    statusBar()->showMessage(loc_.tr("Scene: ")+project_.sceneTitle(index),2500);
    return true;
}
bool StudioWindow::addScene() {
    const bool previous=projectLoading_;
    projectLoading_=true;
    QString error;
    const bool ok=project_.addBlank(board_,&error);
    projectLoading_=previous;
    if (!ok) {
        statusBar()->showMessage(loc_.tr("Cannot add scene: ")+error,7000);
        return false;
    }
    applySceneVisuals();
    refreshSceneUi();
    return true;
}
bool StudioWindow::duplicateScene() {
    const bool previous=projectLoading_;
    projectLoading_=true;
    QString error;
    const bool ok=project_.duplicate(board_,&error);
    projectLoading_=previous;
    if (!ok) {
        statusBar()->showMessage(loc_.tr("Cannot duplicate scene: ")+error,7000);
        return false;
    }
    applySceneVisuals();
    refreshSceneUi();
    return true;
}
void StudioWindow::renameScene() {
    if (project_.count()==0) return;
    bool accepted=false;
    const QString chosen=QInputDialog::getText(this,loc_.tr("Rename scene"),
        loc_.tr("Scene title:"),QLineEdit::Normal,
        project_.sceneTitle(project_.currentIndex()),&accepted);
    if (accepted) {
        if (!project_.rename(project_.currentIndex(),chosen))
            statusBar()->showMessage(loc_.tr("Invalid scene title."),4000);
        refreshSceneUi();
    }
}
void StudioWindow::deleteScene() {
    if (project_.count()<=1) return;
    if (QMessageBox::question(this,loc_.tr("Delete scene"),
        loc_.tr("Delete the selected scene? Other scenes will be kept."),
        QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)
        return;
    const bool previous=projectLoading_;
    projectLoading_=true;
    QString error;
    const bool ok=project_.remove(project_.currentIndex(),board_,&error);
    projectLoading_=previous;
    if (!ok) {
        statusBar()->showMessage(loc_.tr("Cannot remove scene: ")+error,6000);
    } else applySceneVisuals();
    refreshSceneUi();
}
void StudioWindow::applySceneVisuals() {
    // Undo history is intentionally scoped to a scene until the full
    // per-scene command history is implemented. Its canvas remains lossless.
    const QSignalBlocker block(backgroundBox_);
    backgroundBox_->setCurrentIndex(int(board_->boardBackground()));
    refreshEditActions();
    refreshImageControls();
}
bool StudioWindow::saveProjectForTest(const QString& path) {
    QString error;
    if (!project_.save(path,board_,&error)) {
        qWarning().noquote() << "Project save failed:" << error;
        return false;
    }
    board_->undoStack()->setClean();
    refreshSceneUi();
    return true;
}
bool StudioWindow::openInitialDocument(const QString& path,QString* error) {
    // Called only on initial startup by the desktop file / file manager.
    // Never connect webcams, activate microphones or start recording here.
    const QFileInfo file(path);
    if (!file.isFile()) {
        if (error) *error=loc_.tr("Document does not exist.");
        return false;
    }
    const QString extension=file.suffix().toLower();
    if (extension=="gxwp") {
        if (!openProjectForTest(file.absoluteFilePath())) {
            if (error) *error=loc_.tr("Cannot open the project.");
            return false;
        }
        return true;
    }
    if (extension=="gxwb") {
        const bool prior=projectLoading_;
        projectLoading_=true;
        QString problem;
        const bool loaded=board_->loadDocument(file.absoluteFilePath(),&problem);
        if (loaded) {
            project_.initialize(board_);
            project_.setDirty();
            currentDocument_=file.absoluteFilePath();
        }
        projectLoading_=prior;
        if (!loaded) {
            if (error) *error=problem;
            return false;
        }
        applySceneVisuals();
        refreshSceneUi();
        return true;
    }
    if (error) *error=loc_.tr("Unsupported document extension.");
    return false;
}
bool StudioWindow::openProjectForTest(const QString& path) {
    const bool previous=projectLoading_;
    projectLoading_=true;
    QString error;
    const bool ok=project_.open(path,board_,&error);
    projectLoading_=previous;
    if (!ok) {
        qWarning().noquote() << "Project open failed:" << error;
        return false;
    }
    currentDocument_.clear();
    applySceneVisuals();
    refreshSceneUi();
    return true;
}
bool StudioWindow::recoverBackupForTest(const QString& path) {
    const bool old=projectLoading_;
    projectLoading_=true;
    QString error;
    const bool ok=project_.recoverBackup(path,board_,&error);
    projectLoading_=old;
    if (!ok) {
        qWarning().noquote() << "Project backup recovery failed:" << error;
        return false;
    }
    currentDocument_.clear();
    applySceneVisuals();
    refreshSceneUi();
    return true;
}
bool StudioWindow::saveProject(bool forceChoosePath) {
    QString file=project_.filename();
    if (forceChoosePath || file.isEmpty()) {
        QString dir=QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
        if (dir.isEmpty()) dir=QDir::homePath();
        const QString suggested=file.isEmpty()
            ? dir+"/Whiteboard-project.gxwp":file;
        file=QFileDialog::getSaveFileName(this,loc_.tr("Save project"),
            suggested,loc_.tr("GX Whiteboard projects (*.gxwp)"));
        if (file.isEmpty()) return false;
        if (!file.endsWith(".gxwp",Qt::CaseInsensitive)) file+=".gxwp";
    }
    QString error;
    if (!project_.save(file,board_,&error)) {
        QMessageBox::warning(this,loc_.tr("Project save failed"),error);
        return false;
    }
    board_->undoStack()->setClean();
    refreshSceneUi();
    statusBar()->showMessage(loc_.tr("Project saved."),4500);
    return true;
}
bool StudioWindow::confirmDiscardProject() {
    if (!project_.dirty() && board_->undoStack()->isClean()) return true;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(loc_.tr("Unsaved project"));
    box.setText(loc_.tr("The project contains unsaved changes."));
    box.setInformativeText(loc_.tr("Save the project before continuing?"));
    box.setStandardButtons(QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    const auto answer=box.exec();
    if (answer==QMessageBox::Save) return saveProject(false);
    if (answer==QMessageBox::Discard) return true;
    return false;
}
void StudioWindow::openProject() {
    if (!confirmDiscardProject()) return;
    const QString file=QFileDialog::getOpenFileName(this,loc_.tr("Open project"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        loc_.tr("GX Whiteboard projects (*.gxwp)"));
    if (file.isEmpty()) return;
    QString error;
    const bool previous=projectLoading_;
    projectLoading_=true;
    const bool success=project_.open(file,board_,&error);
    projectLoading_=previous;
    if (!success) {
        // A backup can restore a damaged project, but must never silently
        // replace the user's primary file or its .bak checkpoint.
        const QString backup=file+".bak";
        if (QFileInfo::exists(backup) &&
            QMessageBox::question(this,loc_.tr("Project open failed"),
                error+"\n"+
                loc_.tr("A backup exists. Recover it as a new unsaved project?"),
                QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)==QMessageBox::Yes) {
            QString backupError;
            const bool old=projectLoading_;
            projectLoading_=true;
            const bool recovered=project_.recoverBackup(file,board_,&backupError);
            projectLoading_=old;
            if (recovered) {
                currentDocument_.clear();
                applySceneVisuals();
                refreshSceneUi();
                statusBar()->showMessage(loc_.tr(
                    "Backup recovered. Save the project under a NEW filename."),12000);
                return;
            }
            QMessageBox::warning(this,loc_.tr("Backup recovery failed"),backupError);
            return;
        }
        QMessageBox::warning(this,loc_.tr("Project open failed"),error);
        return;
    }
    currentDocument_.clear();
    applySceneVisuals();
    refreshSceneUi();
    statusBar()->showMessage(loc_.tr("Project opened."),4500);
}
void StudioWindow::createProject() {
    if (!confirmDiscardProject()) return;
    const bool previous=projectLoading_;
    projectLoading_=true;
    const bool success=project_.newProject(board_);
    projectLoading_=previous;
    if (!success) return;
    currentDocument_.clear();
    applySceneVisuals();
    refreshSceneUi();
    statusBar()->showMessage(loc_.tr("New project created."),4000);
}
bool StudioWindow::autosaveForTest() {
    QString error;
    if (!project_.capture(board_,&error)) return false;
    return project_.writeRecovery(board_,&error);
}
void StudioWindow::saveRecoveryCheckpoint() {
    if (!project_.count() || board_->inputGestureActive()) return;
    QString error;
    if (!project_.capture(board_,&error)) {
        statusBar()->showMessage(loc_.tr("Recovery error: ")+error,6500);
        return;
    }
    if (project_.dirty() && !project_.writeRecovery(board_,&error)) {
        statusBar()->showMessage(loc_.tr("Recovery error: ")+error,6500);
    }
    refreshSceneUi();
}
bool StudioWindow::recoverForTest(const QString& file) {
    const bool previous=projectLoading_;
    projectLoading_=true;
    QString error;
    const bool okay=project_.restoreRecovery(file,board_,&error);
    projectLoading_=previous;
    if (!okay) {
        qWarning().noquote() << "Recovery failed:" << error;
        return false;
    }
    applySceneVisuals();
    refreshSceneUi();
    return true;
}
void StudioWindow::offerRecoveryAtStartup() {
    const QStringList candidates=ProjectDocument::recoveryFiles();
    if (candidates.isEmpty() || project_.dirty()) return;
    const QString latest=candidates.first();
    const auto answer=QMessageBox::question(this,
        loc_.tr("Recovery available"),
        loc_.tr("An automatic project checkpoint exists. Restore it now?")+
        "\n"+QFileInfo(latest).fileName(),
        QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes);
    if (answer==QMessageBox::Yes) {
        if (recoverForTest(latest)) {
            statusBar()->showMessage(loc_.tr("Project recovered. Save it to a .gxwp file."),9000);
        } else QMessageBox::warning(this,loc_.tr("Recovery failed"),
                loc_.tr("The recovery file could not be loaded."));
    }
}
bool StudioWindow::isRecordingBusy() const {
    return recorder_ && (recorder_->active() || recorder_->finishing());
}
void StudioWindow::refreshEditActions() {
    if (!board_ || editActions_.size() != 10) return;
    const bool hasStrokes = board_->hasStrokes() || board_->imageCount() > 0;
    const bool hasSelection = board_->hasSelection();
    const auto* mime = QApplication::clipboard()->mimeData();
    const bool hasPaste = mime && mime->hasFormat("application/x-gx-whiteboard-strokes+json");
    editActions_[0]->setEnabled(board_->undoStack()->canUndo());
    editActions_[1]->setEnabled(board_->undoStack()->canRedo());
    editActions_[2]->setEnabled(hasStrokes);
    editActions_[3]->setEnabled(hasStrokes);
    editActions_[4]->setEnabled(hasSelection);
    editActions_[5]->setEnabled(hasSelection);
    editActions_[6]->setEnabled(hasSelection);
    editActions_[7]->setEnabled(hasPaste);
    editActions_[8]->setEnabled(hasSelection);
    editActions_[9]->setEnabled(hasSelection || board_->hasSelectedImage());
}
void StudioWindow::requestText(const QPointF& at) {
    bool accepted = false;
    const QString text = QInputDialog::getText(this, loc_.tr("Insert text"),
        loc_.tr("Text:"), QLineEdit::Normal, QString(), &accepted);
    if (accepted && !text.trimmed().isEmpty())
        board_->insertTextAt(at, text);
}
bool StudioWindow::saveDocument(bool forceChoosePath) {
    QString fileName = currentDocument_;
    if (forceChoosePath || fileName.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString suggested = fileName.isEmpty() ? dir + "/Whiteboard.gxwb" : fileName;
        fileName = QFileDialog::getSaveFileName(this, loc_.tr("Save board"),
            suggested, loc_.tr("GX Whiteboard documents (*.gxwb)"));
        if (fileName.isEmpty()) return false;
        if (!fileName.endsWith(".gxwb", Qt::CaseInsensitive)) fileName += ".gxwb";
    }
    QString error;
    if (!board_->saveDocument(fileName, &error)) {
        QMessageBox::warning(this, loc_.tr("Save failed"), error);
        return false;
    }
    currentDocument_ = fileName;
    board_->undoStack()->setClean();
    statusBar()->showMessage(loc_.tr("Board saved."), 3500);
    return true;
}
bool StudioWindow::confirmDiscardChanges() {
    return confirmDiscardProject();
}
void StudioWindow::openDocument() {
    if (!confirmDiscardChanges()) return;
    const QString fileName = QFileDialog::getOpenFileName(
        this, loc_.tr("Open board"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        loc_.tr("GX Whiteboard documents (*.gxwb)"));
    if (fileName.isEmpty()) return;
    QString error;
    if (!board_->loadDocument(fileName, &error)) {
        QMessageBox::warning(this, loc_.tr("Open failed"), error);
        return;
    }
    currentDocument_ = fileName;
    // Legacy .gxwb remains a single-scene document; the new project
    // format can later save it as a multi-scene .gxwp.
    projectLoading_=true;
    project_.initialize(board_);
    project_.setDirty();
    projectLoading_=false;
    refreshSceneUi();
    {
        const QSignalBlocker block(backgroundBox_);
        backgroundBox_->setCurrentIndex(int(board_->boardBackground()));
    }
    statusBar()->showMessage(loc_.tr("Board loaded."), 3500);
    refreshEditActions();
}
void StudioWindow::exportPng() {
    QString path = QFileDialog::getSaveFileName(
        this, loc_.tr("Export PNG"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/Whiteboard.png",
        loc_.tr("PNG images (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (!board_->exportPng(path)) {
        QMessageBox::warning(this, loc_.tr("Export failed"), loc_.tr("Could not write the PNG file."));
        return;
    }
    statusBar()->showMessage(loc_.tr("PNG exported."), 3500);
}
void StudioWindow::exportCurrentViewPng() {
    QString path = QFileDialog::getSaveFileName(
        this, loc_.tr("Export current view PNG"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
            + "/Whiteboard-view.png",
        loc_.tr("PNG images (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (!board_->exportViewPng(path)) {
        QMessageBox::warning(this, loc_.tr("Export failed"), loc_.tr("Could not write the PNG file."));
        return;
    }
    statusBar()->showMessage(loc_.tr("PNG exported."), 3500);
}
void StudioWindow::clearDocument() {
    if (!board_->hasStrokes() && board_->imageCount() == 0) return;
    if (QMessageBox::question(this, loc_.tr("Clear board"),
          loc_.tr("Remove all strokes? You can undo this action."),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        board_->clearBoard();
    }
}
void StudioWindow::addImagePathToLibrary(const QString& path) {
    const QFileInfo file(path);
    if (!file.isFile()) return;
    const QString suffix=file.suffix().toLower();
    if (!(suffix=="png" || suffix=="jpg" || suffix=="jpeg" || suffix=="webp")) return;
    for (int i=0; i<imageList_->count(); ++i) {
        if (imageList_->item(i)->data(Qt::UserRole).toString()==file.absoluteFilePath())
            return;
    }
    QImageReader reader(file.absoluteFilePath());
    reader.setAutoTransform(true);
    if (file.size() > 24LL*1024*1024) return;
    const QSize sourceSize=reader.size();
    if (sourceSize.isValid()) {
        if (qint64(sourceSize.width())*sourceSize.height() > 36000000)
            return;
        reader.setScaledSize(sourceSize.scaled(QSize(212,114),Qt::KeepAspectRatio));
    }
    const QImage thumb=reader.read();
    if (thumb.isNull()) return;
    const QPixmap preview=QPixmap::fromImage(
        thumb.scaled(QSize(212,114),Qt::KeepAspectRatio,Qt::SmoothTransformation));
    // No title beside or underneath the image: filename and path are tooltip-
    // only, and accessible text is exposed to screen readers.
    auto* item=new QListWidgetItem(QIcon(preview),QString(),imageList_);
    item->setData(Qt::UserRole,file.absoluteFilePath());
    item->setData(Qt::AccessibleTextRole,file.fileName());
    item->setToolTip(file.fileName()+"\n"+file.absoluteFilePath());
    if (imageList_->count()==1) imageList_->setCurrentItem(item);
}
void StudioWindow::addImageFiles() {
    const auto files=QFileDialog::getOpenFileNames(this,loc_.tr("Add images"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        loc_.tr("Images (*.png *.jpg *.jpeg *.webp)"));
    for (const QString& path : files) addImagePathToLibrary(path);
    QStringList library;
    for (int i=0; i<imageList_->count(); ++i)
        library << imageList_->item(i)->data(Qt::UserRole).toString();
    settings_.setValue("imageLibrary",library);
}
void StudioWindow::startImagePlacementFromList() {
    auto* item=imageList_->currentItem();
    if (!item) return;
    QString error;
    if (!board_->startImagePlacement(item->data(Qt::UserRole).toString(),&error)) {
        QMessageBox::warning(this,loc_.tr("Image import failed"),
                             loc_.tr(error.toUtf8().constData()));
        return;
    }
    drawActions_[3]->setChecked(true);
    setDrawingToolsOpen(true);
    statusBar()->showMessage(loc_.tr(
        "Draw a rectangle on the canvas with the pen to insert the image."),6000);
}
void StudioWindow::showMediaContextMenu(const QPoint& globalPos) {
    if (!board_ || !board_->hasSelectedImage()) return;
    QMenu menu(this);
    menu.setObjectName("mediaZMenu");
    menu.setTitle(loc_.tr("Media layers"));
    // Both images and USB webcam boxes share this exact media ordering.
    // Ink, text and geometry are rendered later and always stay above media.
    QAction* forward=menu.addAction(loc_.tr("Bring forward"));
    forward->setObjectName("mediaZForward");
    forward->setEnabled(board_->canBringSelectedImageForward());
    QAction* backward=menu.addAction(loc_.tr("Send backward"));
    backward->setObjectName("mediaZBackward");
    backward->setEnabled(board_->canSendSelectedImageBackward());
    menu.addSeparator();
    QAction* front=menu.addAction(loc_.tr("Bring to front"));
    front->setObjectName("mediaZFront");
    front->setEnabled(board_->canBringSelectedImageForward());
    QAction* back=menu.addAction(loc_.tr("Send to back"));
    back->setObjectName("mediaZBack");
    back->setEnabled(board_->canSendSelectedImageBackward());
    connect(forward,&QAction::triggered,board_,&BoardView::bringSelectedImageForward);
    connect(backward,&QAction::triggered,board_,&BoardView::sendSelectedImageBackward);
    connect(front,&QAction::triggered,board_,&BoardView::bringSelectedImageToFront);
    connect(back,&QAction::triggered,board_,&BoardView::sendSelectedImageToBack);
    menu.exec(globalPos);
}
void StudioWindow::refreshImageControls() {
    if (!imagePanel_ || !board_) return;
    const bool selected=board_->hasSelectedImage();
    const auto image=board_->selectedImage();
    imagePropertiesPanel_->setVisible(selected && section_ == 0);
    imageSelectedLabel_->setText(loc_.tr("Image properties"));
    imageSelectedLabel_->setToolTip(selected
        ? image.name+"\n"+image.sourcePath : QString());
    for (QWidget* control : {static_cast<QWidget*>(imageAspectBox_),
          static_cast<QWidget*>(imageFitBox_), static_cast<QWidget*>(imageOpacitySlider_),
          static_cast<QWidget*>(imageFrontButton_),static_cast<QWidget*>(imageBackButton_),
          static_cast<QWidget*>(imageRemoveButton_)}) {
        control->setEnabled(selected);
    }
    if (selected) {
        const QSignalBlocker blockAspect(imageAspectBox_);
        const QSignalBlocker blockFit(imageFitBox_);
        const QSignalBlocker blockOpacity(imageOpacitySlider_);
        imageAspectBox_->setChecked(image.lockAspect);
        imageFitBox_->setCurrentIndex(image.fillCrop ? 1 : 0);
        imageOpacitySlider_->setValue(qRound(image.opacity*100));
    }
}

void StudioWindow::showLanScanDialog() {
    LanScanDialog dialog(this,false,{},[this](const char* message) {
        return loc_.tr(message);
    });
    // Suggest a local /24 to edit; scanning remains user initiated and confirmed.
    dialog.setSubnet("192.168.1.0/24");
    if (dialog.exec()!=QDialog::Accepted) return;
    const QString candidate=dialog.selectedCandidateUrl();
    if (candidate.isEmpty()) return;
    networkUrlEdit_->setText(candidate);
    networkUrlEdit_->setFocus();
    networkUrlEdit_->selectAll();
    statusBar()->showMessage(loc_.tr(
        "Check the suggested camera path and press Add; no connection has started."),9000);
}
QString StudioWindow::selectedNetworkId() const {
    return networkCombo_ ? networkCombo_->currentData().toString() : QString();
}
bool StudioWindow::addNetworkUrl(const QString& text,QString* error) {
    if (!networkCombo_) {
        if (error) *error="Network UI is unavailable.";
        return false;
    }
    QString normalized;
    if (!NetworkSource::validateUrl(text,&normalized,error)) return false;
    // Alias is deterministic across restarts and remains safe in .gxwb:
    // never embed the network URL or credentials inside the canvas object.
    const QString id="net:"+QString::fromLatin1(
        QCryptographicHash::hash(normalized.toUtf8(),
            QCryptographicHash::Sha256).toHex().left(20));
    if (networkUrls_.contains(id)) {
        const int existing=networkCombo_->findData(id);
        if (existing>=0) networkCombo_->setCurrentIndex(existing);
        return true;
    }
    if (networkUrls_.size()>=20) {
        if (error) *error="Maximum 20 IP sources in this version.";
        return false;
    }
    networkUrls_.insert(id,normalized);
    const QString alias=QString("ip%1").arg(networkCombo_->count());
    networkCombo_->addItem(alias,id);
    networkCombo_->setItemData(networkCombo_->count()-1,normalized,Qt::ToolTipRole);
    networkCombo_->setCurrentIndex(networkCombo_->count()-1);
    persistNetworkSources();
    refreshNetworkStatus();
    return true;
}
void StudioWindow::restoreNetworkSources() {
    if (!networkCombo_) return;
    const auto saved=settings_.value("ipSourceUrls").toStringList();
    for (const auto& address : saved) {
        QString error;
        // Reopen a saved address without starting any network connection.
        if (!addNetworkUrl(address,&error))
            qWarning() << "Skipped invalid saved IP source configuration";
    }
    refreshNetworkStatus();
}
void StudioWindow::persistNetworkSources() {
    if (!networkCombo_) return;
    QStringList addresses;
    for (int i=0;i<networkCombo_->count();++i) {
        const QString id=networkCombo_->itemData(i).toString();
        if (networkUrls_.contains(id)) addresses << networkUrls_.value(id);
    }
    settings_.setValue("ipSourceUrls",addresses);
}
bool StudioWindow::startSelectedNetwork() {
    const QString id=selectedNetworkId();
    if (id.isEmpty() || !networkUrls_.contains(id)) {
        statusBar()->showMessage(loc_.tr("Select an IP stream first."),4000);
        return false;
    }
    if (auto* existing=networkSources_.value(id,nullptr)) {
        if (existing->isActive()) return true;
        existing->frameReady={};
        existing->statusChanged={};
        delete existing;
        networkSources_.remove(id);
    }
    auto* source=new NetworkSource(this);
    source->frameReady=[this,id](const QImage& frame) {
        if (board_) board_->updateCameraFrame(id,frame);
    };
    source->statusChanged=[this](const QString&) { refreshNetworkStatus(); };
    QString error;
    const QSize size=cameraResolutionCombo_->currentData().toSize();
    const int fps=cameraFpsCombo_->currentData().toInt();
    if (!source->start(networkUrls_.value(id),size.width(),size.height(),fps,&error)) {
        source->frameReady={};
        source->statusChanged={};
        delete source;
        statusBar()->showMessage(loc_.tr("Cannot start IP stream: ")+error,8000);
        refreshNetworkStatus();
        return false;
    }
    networkSources_.insert(id,source);
    refreshNetworkStatus();
    return true;
}
void StudioWindow::stopSelectedNetwork() {
    if (auto* source=networkSources_.value(selectedNetworkId(),nullptr)) source->stop();
    refreshNetworkStatus();
}
void StudioWindow::removeSelectedNetwork() {
    const int index=networkCombo_->currentIndex();
    if (index<0) return;
    const QString id=selectedNetworkId();
    if (auto* source=networkSources_.take(id)) {
        source->frameReady={};
        source->statusChanged={};
        delete source;
    }
    networkUrls_.remove(id);
    networkCombo_->removeItem(index);
    persistNetworkSources();
    refreshNetworkStatus();
}
void StudioWindow::beginNetworkPlacement() {
    const QString id=selectedNetworkId();
    if (id.isEmpty()) {
        statusBar()->showMessage(loc_.tr("Select an IP stream first."),4000);
        return;
    }
    if (!startSelectedNetwork()) return;
    if (board_->startCameraPlacement(id)) {
        drawActions_[3]->setChecked(true);
        statusBar()->showMessage(loc_.tr("Draw the IP video rectangle with the pen."),6000);
    }
}
bool StudioWindow::recordingActive() const {
    return recorder_ && recorder_->active();
}
void StudioWindow::openRecordingDialog() {
    if (!recorder_ || recorder_->active() || recorder_->finishing()) return;
    QString dir=QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (dir.isEmpty() || !QDir(dir).exists()) dir=QDir::homePath();
    const QString suggested=dir+"/GXWB_"+
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+".mkv";
    RecordingDialog dialog(suggested,
        settings_.value("recordingMicrophoneName").toString(),
        settings_.value("recordingMicrophoneEnabled",false).toBool(),
        settings_.value("recordingMicrophoneVolume",100).toInt(),
        [this](const char* source){return loc_.tr(source);},this);
    if (dialog.exec()!=QDialog::Accepted) return;
    const auto options=dialog.options();
    // Persist only preferences, never raw microphone data.
    settings_.setValue("recordingMicrophoneEnabled",options.microphoneEnabled);
    settings_.setValue("recordingMicrophoneName",options.microphoneSource);
    settings_.setValue("recordingMicrophoneVolume",options.volumePercent);
    QString error;
    if (!recorder_->start(options,&error)) {
        QMessageBox::warning(this,loc_.tr("Recording failed"),error);
        return;
    }
    updateRecordingUi();
    statusBar()->showMessage(loc_.tr("Recording started."),3500);
}
void StudioWindow::stopRecording() {
    if (!recorder_ || !recorder_->active()) return;
    recorder_->stop();
    updateRecordingUi();
    statusBar()->showMessage(loc_.tr("Finalizing recording..."),5000);
}
void StudioWindow::updateRecordingUi() {
    if (!recorder_ || !recordAction_ || !stopRecordAction_ ||
        !muteRecordAction_ || !recordingDuration_) return;
    const bool active=recorder_->active();
    const bool stopping=recorder_->finishing();
    const bool inProgress=active || stopping;
    const bool mic=active && recorder_->usingMicrophone();
    const QString hours=QString::number(recorder_->elapsedMs()/3600000).rightJustified(2,'0');
    const QString minutes=QString::number((recorder_->elapsedMs()/60000)%60).rightJustified(2,'0');
    const QString seconds=QString::number((recorder_->elapsedMs()/1000)%60).rightJustified(2,'0');
    const QString time=hours+":"+minutes+":"+seconds;
    recordAction_->setText(active ? loc_.tr("STOP REC") : loc_.tr("REC"));
    recordAction_->setEnabled(!stopping);
    stopRecordAction_->setEnabled(active);
    stopRecordAction_->setVisible(false); // one REC/STOP toggle saves first-row space
    muteRecordAction_->setEnabled(mic);
    muteRecordAction_->setVisible(mic);
    if (!mic && muteRecordAction_->isChecked()) {
        const QSignalBlocker guard(muteRecordAction_);
        muteRecordAction_->setChecked(false);
    } else if (mic && muteRecordAction_->isChecked()!=recorder_->muted()) {
        const QSignalBlocker guard(muteRecordAction_);
        muteRecordAction_->setChecked(recorder_->muted());
    }
    recordingDuration_->setVisible(inProgress);
    recordingDuration_->setText(inProgress?time:QString());
    recordingDuration_->setToolTip(inProgress ?
        loc_.tr("Frames queued: ")+QString::number(recorder_->frameCount())+
        " / "+loc_.tr("Dropped: ")+QString::number(recorder_->droppedFrames())+
        (recorder_->recentWarnings().isEmpty()?QString():
            "\n"+recorder_->recentWarnings().last()) :
        QString());
    recordingMeter_->setVisible(mic);
    recordingMeter_->setValue(mic?recorder_->levelPercent():0);
    recordingMeter_->setToolTip(loc_.tr("Microphone input level"));
    if (recordingIndicator_) {
        recordingIndicator_->setVisible(inProgress);
        if (inProgress) {
            recordingIndicator_->setText(
                stopping ? loc_.tr("Finalizing...") :
                    QString("● REC  ")+time+
                    (recorder_->muted()?QString("  ")+loc_.tr("MIC MUTED"):QString()));
            recordingIndicator_->adjustSize();
            recordingIndicator_->raise();
        }
    }
}
void StudioWindow::refreshNetworkStatus() {
    if (!networkStatusLabel_ || !networkCombo_) return;
    const QString id=selectedNetworkId();
    const bool hasSource=!id.isEmpty() && networkUrls_.contains(id);
    networkStartButton_->setEnabled(hasSource);
    networkInsertButton_->setEnabled(hasSource);
    networkRemoveButton_->setEnabled(hasSource);
    networkStopButton_->setEnabled(hasSource &&
        networkSources_.contains(id) && networkSources_.value(id)->isActive());
    if (!hasSource) {
        networkStatusLabel_->setText(loc_.tr("No IP source selected."));
        networkCombo_->setToolTip({});
        return;
    }
    const QString url=networkUrls_.value(id);
    networkCombo_->setToolTip(url);
    const NetworkSource* stream=networkSources_.value(id,nullptr);
    if (!stream || stream->status()=="Stopped" || stream->status()=="Idle") {
        networkStatusLabel_->setText(loc_.tr("IP stream stopped."));
        return;
    }
    if (stream->status()=="Streaming") {
        networkStatusLabel_->setText(loc_.tr("IP frames: ") +
            QString::number(stream->framesSeen()));
        return;
    }
    if (stream->status()=="Reconnecting") {
        networkStatusLabel_->setText(loc_.tr("Reconnecting (attempt ") +
            QString::number(stream->retries()) + ")...");
        networkStatusLabel_->setToolTip(stream->lastError());
        return;
    }
    networkStatusLabel_->setText(loc_.tr("IP status: ")+stream->status());
}
void StudioWindow::refreshCameraDevices() {
    if (!cameraDeviceCombo_) return;
    const QString selected=cameraDeviceCombo_->currentData().toString();
    const QSignalBlocker block(cameraDeviceCombo_);
    cameraDeviceCombo_->clear();
    const auto available=CameraSource::listVideoDevices();
    for (const auto& path : available) {
        QFile name("/sys/class/video4linux/"+QFileInfo(path).fileName()+"/name");
        QString label=QFileInfo(path).fileName();
        if (name.open(QIODevice::ReadOnly))
            label += " — " + QString::fromUtf8(name.readAll()).trimmed();
        // Show a compact local alias. Keep the real /dev/video* path as the
        // item data so the capture pipeline is completely unchanged.
        const QString alias=QString("cam%1").arg(cameraDeviceCombo_->count());
        cameraDeviceCombo_->addItem(alias,path);
        cameraDeviceCombo_->setItemData(cameraDeviceCombo_->count()-1,
                                       label+" ("+path+")",Qt::ToolTipRole);
    }
    int index=cameraDeviceCombo_->findData(selected);
    if (index<0) index=0;
    cameraDeviceCombo_->setCurrentIndex(cameraDeviceCombo_->count()?index:-1);
    refreshCameraStatus();
}
bool StudioWindow::startSelectedCamera(bool testPattern) {
    const QString device=testPattern ? QString("test-pattern")
                                    : cameraDeviceCombo_->currentData().toString();
    if (device.isEmpty()) {
        statusBar()->showMessage(loc_.tr("No USB camera selected."),4000);
        return false;
    }
    if (auto* old=cameraSources_.value(device,nullptr)) {
        if (old->isRunning()) {
            refreshCameraStatus();
            return true;
        }
        if (old->status()=="Starting" || old->status()=="Stopping") return false;
        delete old;
        cameraSources_.remove(device);
    }
    auto* source=new CameraSource(this);
    source->frameReady=[this,device](const QImage& frame) {
        if (board_) board_->updateCameraFrame(device,frame);
    };
    source->statusChanged=[this](const QString&) { refreshCameraStatus(); };
    const QSize size=cameraResolutionCombo_->currentData().toSize();
    const int fps=cameraFpsCombo_->currentData().toInt();
    QString error;
    const bool started=testPattern ? source->startTestPattern(size.width(),size.height(),fps,&error)
              : source->startV4L2(device,size.width(),size.height(),fps,&error);
    if (!started) {
        statusBar()->showMessage(loc_.tr("Cannot start camera: ")+error,8000);
        delete source;
        refreshCameraStatus();
        return false;
    }
    cameraSources_.insert(device,source);
    refreshCameraStatus();
    return true;
}
void StudioWindow::stopSelectedCamera() {
    const QString device=cameraDeviceCombo_->currentData().toString();
    if (auto* camera=cameraSources_.value(device,nullptr)) camera->stop();
    refreshCameraStatus();
}
void StudioWindow::beginCameraPlacement() {
    const QString device=cameraDeviceCombo_->currentData().toString();
    if (device.isEmpty()) {
        statusBar()->showMessage(loc_.tr("No USB camera selected."),4000);
        return;
    }
    if (!startSelectedCamera()) return;
    if (board_->startCameraPlacement(device)) {
        drawActions_[3]->setChecked(true);
        statusBar()->showMessage(loc_.tr("Draw a camera rectangle on the canvas with the pen."),6000);
    }
}
void StudioWindow::refreshCameraStatus() {
    if (!cameraStatusLabel_ || !cameraDeviceCombo_) return;
    const QString device=cameraDeviceCombo_->currentData().toString();
    // The closed combo also exposes the full device identity on hover.
    const int current=cameraDeviceCombo_->currentIndex();
    const QString full=current>=0
        ? cameraDeviceCombo_->itemData(current,Qt::ToolTipRole).toString() : QString();
    cameraDeviceCombo_->setToolTip(full);
    const bool devicePresent=!device.isEmpty();
    cameraStartButton_->setEnabled(devicePresent);
    cameraInsertButton_->setEnabled(devicePresent);
    cameraStopButton_->setEnabled(devicePresent && cameraSources_.contains(device));
    if (!devicePresent) {
        cameraStatusLabel_->setText(loc_.tr("No USB capture devices found."));
        return;
    }
    const auto* source=cameraSources_.value(device,nullptr);
    if (!source) {
        cameraStatusLabel_->setText(loc_.tr("Camera stopped."));
        return;
    }
    const QString state=source->status();
    if (state=="Streaming")
        cameraStatusLabel_->setText(loc_.tr("Live frames: ")+QString::number(source->framesSeen()));
    else if (state=="Disconnected" || state=="Error") {
        cameraStatusLabel_->setText(loc_.tr("Camera disconnected: ")+source->lastError().left(180));
    } else cameraStatusLabel_->setText(loc_.tr("Camera status: ")+state);
}
void StudioWindow::restoreSettings() {
    setBoardMode(static_cast<BoardView::Background>(
        qBound(0, settings_.value("background", 0).toInt(), 2)));
    setSidebarOpen(settings_.value("sidebarVisible", true).toBool());
    setDrawingToolsOpen(settings_.value("drawingToolsVisible", true).toBool());
    for (const QString& path : settings_.value("imageLibrary").toStringList())
        addImagePathToLibrary(path);
    restoreNetworkSources();
    board_->setMousePanMode(static_cast<BoardView::MousePanMode>(
        qBound(0, settings_.value("mousePanMode", 2).toInt(), 3)));
    board_->setPenSideMode(static_cast<BoardView::PenSideMode>(
        qBound(0, settings_.value("penSideMode", 0).toInt(), 2)));
    board_->setWheelZoomEnabled(settings_.value("wheelZoomEnabled", true).toBool());
}
void StudioWindow::saveSettings() {
    if (!isFullScreen()) settings_.setValue("geometry", saveGeometry());
    settings_.sync();
}
void StudioWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        setPresentationMode(false);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
void StudioWindow::closeEvent(QCloseEvent* event) {
    if (recorder_ && (recorder_->active() || recorder_->finishing())) {
        QMessageBox::information(this,loc_.tr("Recording in progress"),
            loc_.tr("Stop recording and wait for finalization before closing."));
        event->ignore();
        return;
    }
    if (!confirmDiscardProject()) {
        event->ignore();
        return;
    }
    saveSettings();
    QMainWindow::closeEvent(event);
}
