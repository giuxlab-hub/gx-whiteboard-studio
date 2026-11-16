// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#pragma once
#include "Localization.h"
#include "BoardView.h"
#include "CameraSource.h"
#include "NetworkSource.h"
#include "RecordingEngine.h"
#include "ProjectDocument.h"
#include <QHash>
#include <QMainWindow>
#include <QSettings>
#include <QTranslator>
#include <QPointer>
#include <QVector>
class QWidget;
class QLabel;
class QPushButton;
class QAction;
class QComboBox;
class QMenu;
class QToolBar;
class QCloseEvent;
class QListWidget;
class QSlider;
class QCheckBox;
class QActionGroup;
class QLineEdit;
class QToolButton;
class QProgressBar;
class QTimer;

class StudioWindow : public QMainWindow {
public:
    explicit StudioWindow(QString catalogRoot, QString initialLanguage = {});
    ~StudioWindow() override;
    void setLanguage(const QString& locale, bool savePreference = true);
    QString language() const { return loc_.language(); }
    QString sidebarHeading() const;
    QString gridOptionText() const;
    void setSidebarOpen(bool visible);
    void setDrawingToolsOpen(bool visible);
    bool drawingToolsOpen() const;
    bool sidebarOpen() const;
    void setPresentationMode(bool enabled);
    bool presentationMode() const { return isFullScreen(); }
    void setBoardMode(BoardView::Background mode);
    void openConfiguration();
    void applyStartupMode();
    bool allEditActionsDisabled() const;
    bool defaultFullscreen() const;
    BoardView* boardForTest() const { return board_; }
    void addImagePathToLibrary(const QString& path);
    bool startSelectedCamera(bool testPattern=false);
    void stopSelectedCamera();
    CameraSource* cameraForTest(const QString& device) const { return cameraSources_.value(device,nullptr); }
    bool addNetworkUrl(const QString& url,QString* error=nullptr);
    bool startSelectedNetwork();
    void openRecordingDialog();
    void stopRecording();
    bool recordingActive() const;
    int sceneCountForTest() const {return project_.count();}
    int activeSceneForTest() const {return project_.currentIndex();}
    bool selectSceneForTest(int index) {return activateScene(index);}
    bool addSceneForTest() {return addScene();}
    bool duplicateSceneForTest() {return duplicateScene();}
    bool saveProjectForTest(const QString& path);
    bool openProjectForTest(const QString& path);
    bool openInitialDocument(const QString& path,QString* error=nullptr);
    bool recoverBackupForTest(const QString& path);
    bool autosaveForTest();
    QString recoveryPathForTest() const {return project_.recoveryPath();}
    bool recoverForTest(const QString& path);
    RecordingEngine* recordingForTest() const { return recorder_; }
    void stopSelectedNetwork();
    NetworkSource* networkForTest(const QString& id) const { return networkSources_.value(id,nullptr); }
    QString selectedNetworkId() const;
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    void buildUI();
    void updateLanguage();
    void refreshSection();
    void applyStyle();
    void selectSection(int index);
    void restoreSettings();
    void saveSettings();
    void refreshEditActions();
    bool confirmDiscardChanges();
    void openDocument();
    bool saveDocument(bool forceChoosePath = false);
    void exportPng();
    void exportCurrentViewPng();
    void clearDocument();
    void requestText(const QPointF& at);
    void addImageFiles();
    void startImagePlacementFromList();
    void refreshImageControls();
    void refreshCameraDevices();
    void beginCameraPlacement();
    void refreshCameraStatus();
    void refreshNetworkStatus();
    void removeSelectedNetwork();
    void beginNetworkPlacement();
    void showLanScanDialog();
    void restoreNetworkSources();
    void persistNetworkSources();
    void updateRecordingUi();
    void showMediaContextMenu(const QPoint& globalPos);
    void refreshSceneUi();
    bool activateScene(int index);
    bool addScene();
    bool duplicateScene();
    void renameScene();
    void deleteScene();
    bool saveProject(bool forceChoosePath=false);
    void openProject();
    void createProject();
    void saveRecoveryCheckpoint();
    void offerRecoveryAtStartup();
    bool confirmDiscardProject();
    void applySceneVisuals();
    bool isRecordingBusy() const;
    Localization loc_;
    QSettings settings_;
    QTranslator qtTranslator_;
    QWidget* sidebar_ = nullptr;
    QWidget* imagePanel_ = nullptr;
    QWidget* imagePropertiesPanel_ = nullptr;
    QWidget* cameraPanel_ = nullptr;
    QComboBox* cameraDeviceCombo_ = nullptr;
    QComboBox* cameraResolutionCombo_ = nullptr;
    QComboBox* cameraFpsCombo_ = nullptr;
    QPushButton* cameraRefreshButton_ = nullptr;
    QPushButton* cameraStartButton_ = nullptr;
    QPushButton* cameraStopButton_ = nullptr;
    QPushButton* cameraInsertButton_ = nullptr;
    QLabel* cameraStatusLabel_ = nullptr;
    QLabel* cameraDeviceLabel_ = nullptr;
    QLabel* cameraResolutionLabel_ = nullptr;
    QLabel* cameraFpsLabel_ = nullptr;
    QLabel* cameraNoteLabel_ = nullptr;
    ProjectDocument project_;
    QWidget* scenesPanel_ = nullptr;
    QWidget* projectsPanel_ = nullptr;
    QListWidget* sceneList_ = nullptr;
    QPushButton* sceneAddButton_ = nullptr;
    QPushButton* sceneDuplicateButton_ = nullptr;
    QPushButton* sceneRenameButton_ = nullptr;
    QPushButton* sceneDeleteButton_ = nullptr;
    QPushButton* projectNewButton_ = nullptr;
    QPushButton* projectOpenButton_ = nullptr;
    QPushButton* projectSaveButton_ = nullptr;
    QLabel* projectSummary_ = nullptr;
    QLabel* recoveryStatus_ = nullptr;
    QAction* newProjectAction_ = nullptr;
    QAction* openProjectAction_ = nullptr;
    QAction* saveProjectAction_ = nullptr;
    QAction* saveProjectAsAction_ = nullptr;
    QTimer* recoveryTimer_ = nullptr;
    bool projectLoading_ = false;
    RecordingEngine* recorder_ = nullptr;
    QLabel* recordingIndicator_ = nullptr;
    QLabel* recordingDuration_ = nullptr;
    QProgressBar* recordingMeter_ = nullptr;
    QAction* recordAction_ = nullptr;
    QAction* stopRecordAction_ = nullptr;
    QAction* muteRecordAction_ = nullptr;
    QHash<QString,CameraSource*> cameraSources_;
    QHash<QString,NetworkSource*> networkSources_;
    QHash<QString,QString> networkUrls_;
    QToolButton* networkToggle_ = nullptr;
    QWidget* networkPanel_ = nullptr;
    QComboBox* networkCombo_ = nullptr;
    QLineEdit* networkUrlEdit_ = nullptr;
    QPushButton* networkAddButton_ = nullptr;
    QPushButton* networkRemoveButton_ = nullptr;
    QPushButton* networkStartButton_ = nullptr;
    QPushButton* networkStopButton_ = nullptr;
    QPushButton* networkInsertButton_ = nullptr;
    QPushButton* networkScanButton_ = nullptr;
    QLabel* networkStatusLabel_ = nullptr;
    QListWidget* imageList_ = nullptr;
    QPushButton* browseImageButton_ = nullptr;
    QPushButton* insertImageButton_ = nullptr;
    QPushButton* imageFrontButton_ = nullptr;
    QPushButton* imageBackButton_ = nullptr;
    QPushButton* imageRemoveButton_ = nullptr;
    QCheckBox* imageAspectBox_ = nullptr;
    QComboBox* imageFitBox_ = nullptr;
    QSlider* imageOpacitySlider_ = nullptr;
    QLabel* imageSelectedLabel_ = nullptr;
    QLabel* imageFitLabel_ = nullptr;
    QLabel* imageOpacityLabel_ = nullptr;
    BoardView* board_ = nullptr;
    QLabel* brand_ = nullptr;
    QLabel* sidebarLabel_ = nullptr;
    QLabel* sectionTitle_ = nullptr;
    QLabel* sectionText_ = nullptr;
    QLabel* sidebarFooter_ = nullptr;
    QLabel* backgroundLabel_ = nullptr;
    QLabel* statusMessage_ = nullptr;
    QLabel* statusProfile_ = nullptr;
    QPushButton* sections_[4] = {};
    QToolBar* toolbar_ = nullptr;
    QToolBar* drawingToolbar_ = nullptr;
    QActionGroup* drawingGroup_ = nullptr;
    QAction* drawActions_[9] = {};
    QComboBox* widthBox_ = nullptr;
    QComboBox* colorBox_ = nullptr;
    QLabel* widthLabel_ = nullptr;
    QLabel* colorLabel_ = nullptr;
    QComboBox* backgroundBox_ = nullptr;
    QMenu* fileMenu_ = nullptr;
    QMenu* editMenu_ = nullptr;
    QMenu* settingsMenu_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    QMenu* helpMenu_ = nullptr;
    QAction* quitAction_ = nullptr;
    QAction* openDocumentAction_ = nullptr;
    QAction* saveDocumentAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* exportPngAction_ = nullptr;
    QAction* exportCurrentViewPngAction_ = nullptr;
    QAction* clearDocumentAction_ = nullptr;
    QString currentDocument_;
    QAction* configurationAction_ = nullptr;
    QVector<QAction*> editActions_;
    QAction* toggleSidebarAction_ = nullptr;
    QAction* toggleDrawingToolsAction_ = nullptr;
    QAction* fullscreenAction_ = nullptr;
    QAction* fitAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
    QAction* manualAction_ = nullptr;
    int section_ = 0;
    bool savedSidebarBeforeFullscreen_ = true;
};
