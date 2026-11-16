// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QString>
#include <QColor>
#include <QVector>
#include <QPointF>
#include <QSet>
#include <QUndoStack>
#include <QHash>
#include <QElapsedTimer>
#include <QWheelEvent>
#include <QJsonObject>
#include <QImage>
#include <QMimeData>
#include <functional>

class HistoryCommand;
class QPainter;
class QMouseEvent;
class QTabletEvent;
class QContextMenuEvent;

class BoardView : public QGraphicsView {
public:
    enum class Background { White, Dark, Grid };
    enum class Tool { Pen, Highlighter, Eraser, Select, Line, Arrow, Rectangle, Ellipse, Text };
    enum class MousePanMode { Right=0, Middle=1, Both=2, None=3 };
    enum class PenSideMode { Pan=0, Eraser=1, None=2 };
    struct Point { QPointF pos; qreal pressure = 0.5; };
    struct Stroke {
        qint64 id = 0;
        Tool tool = Tool::Pen;
        QColor color = QColor("#253b5a");
        qreal width = 8;
        QVector<Point> points;
        QPointF start;
        QPointF end;
        QString text;
    };
    struct ImageBox {
        qint64 id = 0;
        QString name;
        QString sourcePath;
        QRectF rect;
        QImage image;
        qreal opacity = 1.0;
        bool lockAspect = true;
        bool fillCrop = false;
        bool isCamera = false;
        QString cameraDevice;
    };
    explicit BoardView(QWidget* parent = nullptr);

    void setBoardBackground(Background background);
    Background boardBackground() const { return background_; }
    void setWatermark(const QString& name, const QString& note);
    void fitBoard();
    QSize boardSize() const { return {1920, 1080}; }
    QRectF visibleWorldRect() const;
    QPointF viewCenter() const;
    qreal viewScale() const { return transform().m11(); }
    void zoomAt(qreal zoomFactor, const QPoint& viewportPosition);
    void panByPixels(const QPointF& pixelDelta);
    void setMousePanMode(MousePanMode value) { mousePanMode_ = value; }
    MousePanMode mousePanMode() const { return mousePanMode_; }
    void setPenSideMode(PenSideMode value) { penSideMode_ = value; }
    PenSideMode penSideMode() const { return penSideMode_; }
    void setWheelZoomEnabled(bool value) { wheelZoomEnabled_ = value; }
    bool wheelZoomEnabled() const { return wheelZoomEnabled_; }

    void setTool(Tool t);
    Tool tool() const { return tool_; }
    void setStrokeColor(const QColor &color);
    QColor strokeColor() const { return color_; }
    void setStrokeWidth(qreal width);
    qreal strokeWidth() const { return width_; }
    QUndoStack* undoStack() { return activeUndo_; }
    void setHistoryScene(const QString& key);
    void resetSceneHistories(const QString& activeKey);
    void removeSceneHistory(const QString& key);
    bool hasSelection() const { return !selected_.isEmpty(); }
    bool hasStrokes() const { return !strokes_.isEmpty(); }
    bool inputGestureActive() const {return inputActive_ || imageGesture_;}
    int strokeCount() const { return int(strokes_.size()); }
    int imageCount() const { return int(images_.size()); }
    int cameraBoxCount(const QString& device={}) const;
    bool startCameraPlacement(const QString& device);
    bool insertCameraBox(const QString& device, const QRectF& rectangle);
    void updateCameraFrame(const QString& device, const QImage& frame);
    bool cameraFrameAvailable(const QString& device) const;
    bool hasSelectedImage() const;
    ImageBox selectedImage() const;
    bool startImagePlacement(const QString& fileName, QString* error = nullptr);
    bool insertImageFromFile(const QString& fileName, const QRectF& rectangle,
                             QString* error = nullptr);
    void setSelectedImageAspectLocked(bool enabled);
    void setSelectedImageFillCrop(bool enabled);
    void setSelectedImageOpacity(qreal opacity);
    void bringSelectedImageForward();
    void sendSelectedImageBackward();
    void bringSelectedImageToFront();
    void sendSelectedImageToBack();
    bool canBringSelectedImageForward() const;
    bool canSendSelectedImageBackward() const;
    void removeSelectedImage();
    void cancelImagePlacement();
    void selectAllStrokes();
    void deselect();
    void deleteSelection();
    void copySelection();
    void cutSelection();
    void pasteSelection();
    void duplicateSelection();
    void clearBoard();
    QJsonObject documentObject(QString* error=nullptr) const;
    bool loadDocumentObject(const QJsonObject& document, QString* error=nullptr,
                            bool preserveHistory=false);
    bool saveDocument(const QString &fileName, QString *error = nullptr) const;
    bool loadDocument(const QString &fileName, QString *error = nullptr);
    bool exportPng(const QString &fileName) const;
    bool exportViewPng(const QString &fileName) const;
    QImage renderViewImage(const QSize& outputSize) const;
    bool addSyntheticStroke(const QPointF& a, const QPointF& b, qreal pressure = 0.5);
    void insertTextAt(const QPointF& where, const QString& text);

    std::function<void()> stateChanged;
    std::function<void(const QPointF&)> requestText;
    std::function<void(const QPoint&)> requestMediaContextMenu;
    // A file-manager drop is a direct canvas edit, including during REC.
    std::function<void(int imported,const QString& details)> externalImagesDropped;
protected:
    bool viewportEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    friend class HistoryCommand;
    void beginInput(const QPointF& pos, qreal pressure, Tool tool);
    void moveInput(const QPointF& pos, qreal pressure);
    void endInput(const QPointF& pos, qreal pressure);
    void eraseAt(const QPointF& pos);
    void moveSelectedStrokes(const QPointF& delta);
    bool readImage(const QString& fileName, ImageBox* image, QString* error) const;
    bool addImageBox(ImageBox image, const QRectF& rect);
    void paintImage(QPainter* painter, const ImageBox& image) const;
    void drawImageHandles(QPainter* painter) const;
    QVector<QPointF> imageHandles(const QRectF& rect) const;
    int hitImageHandle(const ImageBox& image, const QPointF& pos) const;
    qint64 imageAt(const QPointF& pos) const;
    ImageBox* imageById(qint64 id);
    const ImageBox* imageById(qint64 id) const;
    void moveImageGesture(const QPointF& pos);
    void finishImageGesture(const QPointF& pos);
    void commitImageChange(const QVector<ImageBox>& before, const QString& action);
    bool hasImageChanged(const QVector<ImageBox>& before) const;
    void replaceCompleteHistoryState(const QVector<Stroke>& strokes,
                                      const QVector<ImageBox>& images);
    bool isMousePanButton(Qt::MouseButton button) const;
    bool hasPenSideButton(Qt::MouseButtons buttons) const;
    void startPan(const QPointF& viewportPos, bool tablet);
    void movePan(const QPointF& viewportPos);
    void stopPan();
    bool pendingMediaRightClick_ = false;
    QPointF rightClickStart_;
    void replaceHistoryState(const QVector<Stroke>& strokes);
    void commit(const QVector<Stroke>& before, const QString& action);
    QRectF strokeBounds(const Stroke& stroke) const;
    bool hitsStroke(const Stroke& stroke, const QPointF& pos, qreal tolerance) const;
    bool hasChanged(const QVector<Stroke>& before) const;
    void paintStroke(QPainter* painter, const Stroke& stroke) const;
    void notifyState();
    QJsonObject strokeToJson(const Stroke& stroke) const;
    bool strokeFromJson(const QJsonObject& object, Stroke* output) const;
    QJsonObject imageToJson(const ImageBox& image) const;
    bool imageFromJson(const QJsonObject& object, ImageBox* output) const;
    qint64 nextStrokeId();
    QPointF clampToBoard(const QPointF& point) const;
    QGraphicsScene scene_;
    MousePanMode mousePanMode_ = MousePanMode::Both;
    PenSideMode penSideMode_ = PenSideMode::Pan;
    Qt::MouseButton panMouseButton_ = Qt::NoButton;
    bool wheelZoomEnabled_ = true;
    bool panning_ = false;
    bool penPanning_ = false;
    bool suppressTabletUntilRelease_ = false;
    bool viewCustomized_ = false;
    QPointF lastPanViewport_;
    Background background_ = Background::White;
    QString name_, note_;
    Tool tool_ = Tool::Pen;
    QColor color_ = QColor("#253b5a");
    qreal width_ = 8;
    QVector<Stroke> strokes_;
    QVector<Stroke> gestureBefore_;
    QVector<ImageBox> images_;
    QVector<ImageBox> imageGestureBefore_;
    ImageBox pendingImage_;
    qint64 selectedImageId_ = 0;
    qint64 nextImageId_ = 1;
    bool placingImage_ = false;
    bool imageGesture_ = false;
    int imageResizeHandle_ = -1;
    QPointF imageGestureStart_;
    QPointF imageGestureLast_;
    QRectF imageGestureOriginalRect_;
    QRectF imagePreviewRect_;
    QSet<qint64> selected_;
    Stroke draft_;
    QRectF selectionRectangle_;
    bool inputActive_ = false;
    bool selecting_ = false;
    bool movingSelection_ = false;
    QPointF lastMovePoint_;
    bool gestureChanged_ = false;
    QPointF selectionAnchor_;
    QPointF previousEraserPoint_;
    QElapsedTimer tabletClock_;
    qint64 lastTabletMs_ = -100000;
    bool tabletGesture_ = false;
    bool mouseGesture_ = false;
    Tool gestureTool_ = Tool::Pen;
    qint64 nextId_ = 1;
    int pasteOffset_ = 0;
    QUndoStack undo_;
    QUndoStack* activeUndo_ = &undo_;
    QHash<QString, QUndoStack*> sceneHistories_;
    QString activeSceneHistory_ = "_initial";
};
