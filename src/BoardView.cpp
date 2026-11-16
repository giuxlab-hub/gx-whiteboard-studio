#include "BoardView.h"
#include <QTabletEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QUndoCommand>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QFontMetricsF>
#include <QResizeEvent>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QSaveFile>
#include <QScrollBar>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QTransform>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <utility>

static constexpr int kBoardWidth = 1920;
static constexpr int kBoardHeight = 1080;
// Wide finite coordinate system, independent from the 1920x1080 output frame.
static constexpr int kWorldLimit = 65536;
static constexpr qreal kMinViewScale = 0.035;
static constexpr qreal kMaxViewScale = 12.0;
static constexpr int kMaxStrokes = 100000;
static constexpr int kMaxPointsPerStroke = 200000;
static const char* kStrokeMime = "application/x-gx-whiteboard-strokes+json";

class HistoryCommand final : public QUndoCommand {
public:
    HistoryCommand(BoardView* board, QVector<BoardView::Stroke> before,
                   QVector<BoardView::Stroke> after,
                   QVector<BoardView::ImageBox> imagesBefore,
                   QVector<BoardView::ImageBox> imagesAfter,
                   const QString& text)
        : QUndoCommand(text), board_(board), before_(std::move(before)),
          after_(std::move(after)), imagesBefore_(std::move(imagesBefore)),
          imagesAfter_(std::move(imagesAfter)) {}
    void undo() override { board_->replaceCompleteHistoryState(before_, imagesBefore_); }
    void redo() override { board_->replaceCompleteHistoryState(after_, imagesAfter_); }
private:
    BoardView* board_;
    QVector<BoardView::Stroke> before_, after_;
    QVector<BoardView::ImageBox> imagesBefore_, imagesAfter_;
};

BoardView::BoardView(QWidget* parent) : QGraphicsView(parent) {
    scene_.setSceneRect(-kWorldLimit, -kWorldLimit, 2*kWorldLimit, 2*kWorldLimit);
    setScene(&scene_);
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setDragMode(QGraphicsView::NoDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents, false);
    setMinimumSize(320, 180);
    tabletClock_.start();
    activeUndo_->setUndoLimit(100); // F02 bounded history, to be optimized for larger documents
    setWatermark("WHITEBOARD STUDIO", "F02");
    sceneHistories_.insert(activeSceneHistory_, &undo_);
    connect(&undo_, &QUndoStack::canUndoChanged, this, [this](bool){ notifyState(); });
    connect(&undo_, &QUndoStack::canRedoChanged, this, [this](bool){ notifyState(); });
}
void BoardView::setHistoryScene(const QString& key) {
    if (key.isEmpty() || key==activeSceneHistory_) return;
    QUndoStack* stack=sceneHistories_.value(key,nullptr);
    if (!stack) {
        stack=new QUndoStack(this);
        stack->setUndoLimit(100);
        connect(stack,&QUndoStack::canUndoChanged,this,[this] {notifyState();});
        connect(stack,&QUndoStack::canRedoChanged,this,[this] {notifyState();});
        sceneHistories_.insert(key,stack);
    }
    activeUndo_=stack;
    activeSceneHistory_=key;
    notifyState();
}
void BoardView::resetSceneHistories(const QString& activeKey) {
    // QUndoStack::clear() emits signals, synchronously reaching StudioWindow.
    // Never let such callbacks use a deleted active scene stack.
    activeUndo_=&undo_;
    activeSceneHistory_=activeKey;
    for (auto* stack:sceneHistories_) {
        if (stack!=&undo_) delete stack;
    }
    sceneHistories_.clear();
    undo_.clear();
    sceneHistories_.insert(activeKey,&undo_);
    notifyState();
}
void BoardView::removeSceneHistory(const QString& key) {
    if (key.isEmpty() || key==activeSceneHistory_) return;
    QUndoStack* stack=sceneHistories_.take(key);
    if (stack && stack!=&undo_) delete stack;
}
void BoardView::setBoardBackground(Background b) {
    background_ = b;
    viewport()->update();
}
void BoardView::setWatermark(const QString& name, const QString& note) {
    name_ = name;
    note_ = note;
    viewport()->update();
}
QRectF BoardView::visibleWorldRect() const {
    return mapToScene(viewport()->rect()).boundingRect();
}
QPointF BoardView::viewCenter() const {
    return mapToScene(viewport()->rect().center());
}
void BoardView::fitBoard() {
    // Ctrl+0 returns to the reference recording frame, not to the full workspace.
    fitInView(QRectF(0, 0, kBoardWidth, kBoardHeight), Qt::KeepAspectRatio);
    viewCustomized_ = false;
    viewport()->update();
}
void BoardView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    if (!viewCustomized_) fitBoard();
}
void BoardView::zoomAt(qreal factor, const QPoint& viewportPosition) {
    if (!std::isfinite(factor) || factor <= 0) return;
    const qreal previousScale = viewScale();
    const qreal targetScale = std::clamp(previousScale * factor, kMinViewScale, kMaxViewScale);
    if (std::abs(targetScale - previousScale) < 0.0000001) return;
    const QPointF anchor = mapToScene(viewportPosition);
    const qreal applied = targetScale / previousScale;
    setTransformationAnchor(QGraphicsView::NoAnchor);
    scale(applied, applied);
    const QPointF anchorAfter = mapToScene(viewportPosition);
    const QPointF center = viewCenter();
    centerOn(center + anchor - anchorAfter);
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    viewCustomized_ = true;
    viewport()->update();
}
void BoardView::panByPixels(const QPointF& pixels) {
    if (pixels.isNull()) return;
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() - qRound(pixels.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() - qRound(pixels.y()));
    viewCustomized_ = true;
    viewport()->update();
}
bool BoardView::isMousePanButton(Qt::MouseButton button) const {
    return (mousePanMode_ == MousePanMode::Both && (button == Qt::RightButton || button == Qt::MiddleButton)) ||
           (mousePanMode_ == MousePanMode::Right && button == Qt::RightButton) ||
           (mousePanMode_ == MousePanMode::Middle && button == Qt::MiddleButton);
}
bool BoardView::hasPenSideButton(Qt::MouseButtons buttons) const {
    return bool(buttons & (Qt::RightButton | Qt::MiddleButton));
}
void BoardView::startPan(const QPointF& viewportPos, bool tablet) {
    if (inputActive_) {
        endInput(mapToScene(viewportPos.toPoint()), 0.5);
    }
    panning_ = true;
    penPanning_ = tablet;
    lastPanViewport_ = viewportPos;
    viewport()->setCursor(Qt::ClosedHandCursor);
}
void BoardView::movePan(const QPointF& viewportPos) {
    if (!panning_) return;
    panByPixels(viewportPos - lastPanViewport_);
    lastPanViewport_ = viewportPos;
}
void BoardView::stopPan() {
    panning_ = false;
    penPanning_ = false;
    panMouseButton_ = Qt::NoButton;
    setTool(tool_);
}
void BoardView::setTool(Tool t) {
    if (inputActive_) return;
    tool_ = t;
    if (t == Tool::Select) viewport()->setCursor(Qt::CrossCursor);
    else if (t == Tool::Text) viewport()->setCursor(Qt::IBeamCursor);
    else viewport()->setCursor(Qt::CrossCursor);
    notifyState();
}
void BoardView::setStrokeColor(const QColor &c) {
    if (c.isValid()) color_ = c;
}
void BoardView::setStrokeWidth(qreal w) { width_ = std::clamp(w, 1.0, 64.0); }
qint64 BoardView::nextStrokeId() { return nextId_++; }
QPointF BoardView::clampToBoard(const QPointF& p) const {
    return {std::clamp(p.x(), -double(kWorldLimit), double(kWorldLimit)),
            std::clamp(p.y(), -double(kWorldLimit), double(kWorldLimit))};
}
void BoardView::notifyState() {
    viewport()->update();
    if (stateChanged) stateChanged();
}
void BoardView::beginInput(const QPointF& pos, qreal pressure, Tool t) {
    if (inputActive_) return;
    const QPointF local = clampToBoard(pos);
    if (!pendingImage_.image.isNull() && t != Tool::Eraser) {
        inputActive_ = true;
        imageGesture_ = true;
        placingImage_ = true;
        imageGestureStart_ = local;
        imageGestureLast_ = local;
        imagePreviewRect_ = QRectF(local,local);
        selectedImageId_ = 0;
        notifyState();
        return;
    }
    if (t == Tool::Select) {
        qint64 target = 0;
        int handle = -1;
        if (const auto* box = imageById(selectedImageId_)) {
            handle = hitImageHandle(*box,local);
            if (handle >= 0 || box->rect.contains(local)) target = box->id;
        }
        if (!target) target = imageAt(local);
        if (target) {
            selectedImageId_ = target;
            selected_.clear();
            imageGestureBefore_ = images_;
            imageGesture_ = true;
            placingImage_ = false;
            imageResizeHandle_ = handle;
            imageGestureStart_ = local;
            imageGestureLast_ = local;
            imageGestureOriginalRect_ = imageById(target)->rect;
            inputActive_ = true;
            notifyState();
            return;
        }
    }
    selectedImageId_ = 0;
    inputActive_ = true;
    selecting_ = false;
    movingSelection_ = false;
    gestureTool_ = t;
    gestureBefore_ = strokes_;
    gestureChanged_ = false;
    selectionAnchor_ = clampToBoard(pos);
    lastMovePoint_ = selectionAnchor_;
    previousEraserPoint_ = selectionAnchor_;
    if (t == Tool::Select) {
        // If the press hits an already selected stroke, drag the whole selection.
        // Otherwise select the topmost stroke under the pen, ready to drag it.
        qint64 hitId = -1;
        for (qsizetype i = strokes_.size(); i > 0; --i) {
            if (hitsStroke(strokes_[i-1], selectionAnchor_, 12)) {
                hitId = strokes_[i-1].id;
                break;
            }
        }
        if (hitId >= 0) {
            if (!selected_.contains(hitId)) {
                selected_.clear();
                selected_.insert(hitId);
            }
            movingSelection_ = true;
        } else {
            selected_.clear();
            selecting_ = true; // empty space: rectangular/marquee selection
        }
    } else {
        selected_.clear();
    }
    selectionRectangle_ = QRectF(selectionAnchor_, selectionAnchor_);
    draft_ = {};
    draft_.id = nextStrokeId();
    draft_.tool = t;
    draft_.width = width_;
    draft_.color = color_;
    draft_.start = selectionAnchor_;
    draft_.end = selectionAnchor_;
    draft_.points.push_back({selectionAnchor_, std::clamp(pressure, 0.0, 1.0)});
    if (t == Tool::Eraser) eraseAt(selectionAnchor_);
    notifyState();
}
void BoardView::moveInput(const QPointF& pos, qreal pressure) {
    if (!inputActive_) return;
    if (imageGesture_) {
        moveImageGesture(pos);
        return;
    }
    const QPointF p = clampToBoard(pos);
    if (movingSelection_) {
        const QPointF delta = p - lastMovePoint_;
        if (!qFuzzyIsNull(delta.x()) || !qFuzzyIsNull(delta.y())) {
            moveSelectedStrokes(delta);
            lastMovePoint_ = p;
        }
    } else if (selecting_) {
        selectionRectangle_ = QRectF(selectionAnchor_, p).normalized();
    } else if (gestureTool_ == Tool::Eraser) {
        const double length = QLineF(previousEraserPoint_, p).length();
        const int samples = std::max(1, int(std::ceil(length / 10.0)));
        for (int i=1; i<=samples; ++i) {
            const qreal t = qreal(i) / samples;
            eraseAt(previousEraserPoint_*(1.0-t) + p*t);
        }
        previousEraserPoint_ = p;
    } else {
        draft_.end = p;
        if (gestureTool_ == Tool::Pen || gestureTool_ == Tool::Highlighter) {
            if (draft_.points.isEmpty() ||
                QLineF(draft_.points.last().pos, p).length() >= 0.25) {
                draft_.points.push_back({p, std::clamp(pressure, 0.0, 1.0)});
            }
        }
    }
    notifyState();
}
void BoardView::endInput(const QPointF& pos, qreal pressure) {
    if (!inputActive_) return;
    if (imageGesture_) {
        finishImageGesture(pos);
        return;
    }
    moveInput(pos, pressure);
    if (movingSelection_) {
        // A complete pen-down/move/pen-up sequence is one undoable action.
        if (hasChanged(gestureBefore_)) commit(gestureBefore_, "Move selection");
    } else if (selecting_) {
        if (selectionRectangle_.width() < 8 && selectionRectangle_.height() < 8) {
            for (qsizetype i = strokes_.size(); i > 0; --i) {
                if (hitsStroke(strokes_[i-1], clampToBoard(pos), 12)) {
                    selected_.insert(strokes_[i-1].id);
                    break;
                }
            }
        } else {
            for (const auto& stroke : strokes_) {
                if (strokeBounds(stroke).intersects(selectionRectangle_))
                    selected_.insert(stroke.id);
            }
        }
    } else if (gestureTool_ != Tool::Eraser) {
        strokes_.push_back(draft_);
        gestureChanged_ = true;
    }
    inputActive_ = false;
    selecting_ = false;
    const bool wasMovingSelection = movingSelection_;
    movingSelection_ = false;
    selectionRectangle_ = {};
    if (!wasMovingSelection && gestureTool_ != Tool::Select &&
        (gestureChanged_ || hasChanged(gestureBefore_))) {
        const QString label = gestureTool_ == Tool::Eraser ? "Erase strokes" : "Draw stroke";
        commit(gestureBefore_, label);
    }
    draft_ = {};
    notifyState();
}
bool BoardView::hasChanged(const QVector<Stroke>& before) const {
    if (before.size() != strokes_.size()) return true;
    for (qsizetype i = 0; i < before.size(); ++i) {
        const auto& a = before[i];
        const auto& b = strokes_[i];
        if (a.id != b.id || a.start != b.start || a.end != b.end ||
            a.tool != b.tool || a.color != b.color || a.width != b.width ||
            a.text != b.text || a.points.size() != b.points.size()) return true;
        for (qsizetype j = 0; j < a.points.size(); ++j) {
            if (a.points[j].pos != b.points[j].pos ||
                a.points[j].pressure != b.points[j].pressure) return true;
        }
    }
    return false;
}
void BoardView::commit(const QVector<Stroke>& before, const QString& action) {
    if (!hasChanged(before)) return;
    const auto selection = selected_;
    activeUndo_->push(new HistoryCommand(this, before, strokes_, images_, images_, action));
    selected_ = selection; // keep selection after the implicit first redo
    notifyState();
}
void BoardView::replaceHistoryState(const QVector<Stroke>& strokes) {
    strokes_ = strokes;
    selected_.clear();
    notifyState();
}
void BoardView::replaceCompleteHistoryState(
    const QVector<Stroke>& strokes, const QVector<ImageBox>& images) {
    strokes_ = strokes;
    images_ = images;
    selected_.clear();
    if (!imageById(selectedImageId_)) selectedImageId_ = 0;
    notifyState();
}
void BoardView::commitImageChange(const QVector<ImageBox>& before, const QString& action) {
    if (!hasImageChanged(before)) return;
    const qint64 selected = selectedImageId_;
    activeUndo_->push(new HistoryCommand(this, strokes_, strokes_, before, images_, action));
    selectedImageId_ = selected;
    notifyState();
}
QRectF BoardView::strokeBounds(const Stroke& s) const {
    QRectF bound;
    if (s.tool == Tool::Pen || s.tool == Tool::Highlighter) {
        if (s.points.isEmpty()) return {};
        qreal left = s.points.first().pos.x(), right = left;
        qreal top = s.points.first().pos.y(), bottom = top;
        for (const auto& pt : s.points) {
            left = std::min(left, pt.pos.x());
            right = std::max(right, pt.pos.x());
            top = std::min(top, pt.pos.y());
            bottom = std::max(bottom, pt.pos.y());
        }
        bound = QRectF(QPointF(left, top), QPointF(right, bottom));
    } else if (s.tool == Tool::Text) {
        QFont font;
        font.setPixelSize(50);
        const QFontMetricsF metrics(font);
        bound = QRectF(s.start.x(), s.start.y()-metrics.ascent(),
                       std::max(10.0, metrics.horizontalAdvance(s.text)),
                       metrics.height());
    } else {
        bound = QRectF(s.start, s.end).normalized();
    }
    return bound.adjusted(-s.width-7, -s.width-7, s.width+7, s.width+7);
}
static double distanceSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const double dx = b.x()-a.x(), dy = b.y()-a.y();
    const double denom = dx*dx + dy*dy;
    if (denom < 1e-8) return QLineF(p, a).length();
    const double t = std::clamp(((p.x()-a.x())*dx + (p.y()-a.y())*dy) / denom, 0.0, 1.0);
    return QLineF(p, QPointF(a.x()+t*dx, a.y()+t*dy)).length();
}
bool BoardView::hitsStroke(const Stroke& s, const QPointF& pos, qreal tolerance) const {
    if (!strokeBounds(s).adjusted(-tolerance,-tolerance,tolerance,tolerance).contains(pos)) return false;
    if (s.tool == Tool::Pen || s.tool == Tool::Highlighter) {
        if (s.points.size() == 1)
            return QLineF(s.points.first().pos, pos).length() <= tolerance + s.width;
        for (qsizetype i=1; i<s.points.size(); ++i) {
            if (distanceSegment(pos, s.points[i-1].pos, s.points[i].pos) <= tolerance + s.width)
                return true;
        }
        return false;
    }
    if (s.tool == Tool::Text || s.tool == Tool::Rectangle || s.tool == Tool::Ellipse) return true;
    if (distanceSegment(pos, s.start, s.end) <= tolerance + s.width) return true;
    return false;
}
void BoardView::moveSelectedStrokes(const QPointF& delta) {
    for (auto& stroke : strokes_) {
        if (!selected_.contains(stroke.id)) continue;
        stroke.start += delta;
        stroke.end += delta;
        for (auto& point : stroke.points) point.pos += delta;
    }
}
void BoardView::eraseAt(const QPointF& pos) {
    const auto before = strokes_.size();
    strokes_.erase(std::remove_if(strokes_.begin(), strokes_.end(),
        [this, &pos](const Stroke& stroke) { return hitsStroke(stroke, pos, std::max(12.0, width_*2)); }),
        strokes_.end());
    if (strokes_.size() != before) gestureChanged_ = true;
}
void BoardView::drawBackground(QPainter *p, const QRectF &rect) {
    p->save();
    p->fillRect(rect, QColor("#181e2c"));
    const QColor bg = background_ == Background::Dark ? QColor("#171c27") : QColor("#ffffff");
    const QRectF region = rect.intersected(scene_.sceneRect());
    p->fillRect(region, bg);
    if (background_ == Background::Grid && !region.isEmpty()) {
        p->setPen(QPen(QColor("#e3e8ef"), 1.25));
        const int left = int(std::floor(region.left()/40.0))*40;
        const int top = int(std::floor(region.top()/40.0))*40;
        for (int x=left; x<=region.right(); x+=40)
            p->drawLine(x, region.top(), x, region.bottom());
        for (int y=top; y<=region.bottom(); y+=40)
            p->drawLine(region.left(), y, region.right(), y);
    }
    p->restore();
}
void BoardView::paintStroke(QPainter* p, const Stroke& s) const {
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QColor color = s.color;
    if (s.tool == Tool::Highlighter) color.setAlpha(85);
    p->setPen(QPen(color, std::max(1.0, double(s.width)), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (s.tool == Tool::Pen || s.tool == Tool::Highlighter) {
        if (s.points.size() == 1) {
            qreal width = s.width * (0.3 + 1.55*s.points.first().pressure);
            p->setBrush(color);
            p->setPen(Qt::NoPen);
            p->drawEllipse(s.points.first().pos, width/2.0, width/2.0);
        } else if (s.tool == Tool::Highlighter) {
            QPainterPath path(s.points.first().pos);
            for (qsizetype i=1; i<s.points.size(); ++i) path.lineTo(s.points[i].pos);
            p->setPen(QPen(color, s.width*3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->drawPath(path);
        } else {
            for (qsizetype i=1; i<s.points.size(); ++i) {
                const qreal pressure = (s.points[i-1].pressure + s.points[i].pressure) / 2;
                p->setPen(QPen(color, std::max(1.0, s.width*(0.3+1.55*pressure)),
                           Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p->drawLine(s.points[i-1].pos, s.points[i].pos);
            }
        }
    } else if (s.tool == Tool::Line || s.tool == Tool::Arrow) {
        p->drawLine(s.start, s.end);
        if (s.tool == Tool::Arrow && QLineF(s.start, s.end).length() >= 5) {
            const qreal length = QLineF(s.start, s.end).length();
            const QPointF back = (s.start - s.end) / length;
            const QPointF normal(-back.y(), back.x());
            const qreal size = std::max(13.0, double(s.width)*3.0);
            p->drawLine(s.end, s.end + back*size + normal*size*0.55);
            p->drawLine(s.end, s.end + back*size - normal*size*0.55);
        }
    } else if (s.tool == Tool::Rectangle) {
        p->drawRect(QRectF(s.start, s.end).normalized());
    } else if (s.tool == Tool::Ellipse) {
        p->drawEllipse(QRectF(s.start, s.end).normalized());
    } else if (s.tool == Tool::Text) {
        QFont font = p->font();
        font.setPixelSize(50);
        p->setFont(font);
        p->drawText(s.start, s.text);
    }
    p->restore();
}
void BoardView::drawForeground(QPainter* p, const QRectF &rect) {
    Q_UNUSED(rect);
    p->save();
    p->setClipRect(scene_.sceneRect());
    if (strokes_.isEmpty() && images_.isEmpty() && !inputActive_) {
        const QColor primary = background_ == Background::Dark ? QColor("#c1ccd9") : QColor("#8d9ab0");
        p->setPen(primary);
        QFont font = p->font(); font.setPixelSize(38); font.setWeight(QFont::DemiBold);
        p->setFont(font);
        p->drawText(QRectF(100,460,1720,100), Qt::AlignCenter, name_);
        font.setPixelSize(23); font.setWeight(QFont::Normal); p->setFont(font);
        p->drawText(QRectF(110,555,1700,65), Qt::AlignCenter, note_);
    }
    for (const auto& image : images_) paintImage(p,image);
    if (placingImage_ && !pendingImage_.image.isNull()) {
        ImageBox preview = pendingImage_;
        preview.rect = imagePreviewRect_;
        preview.opacity = 0.65;
        if (preview.rect.width()>5 && preview.rect.height()>5) paintImage(p,preview);
        p->setPen(QPen(QColor("#237cce"),2.0/std::max(0.05,viewScale()),Qt::DashLine));
        p->setBrush(Qt::NoBrush);
        p->drawRect(imagePreviewRect_);
    }
    for (const auto& stroke : strokes_) {
        paintStroke(p, stroke);
        if (selected_.contains(stroke.id)) {
            p->setPen(QPen(QColor("#1d8cd7"), 1.75, Qt::DashLine));
            p->setBrush(Qt::NoBrush);
            p->drawRect(strokeBounds(stroke));
        }
    }
    if (inputActive_ && gestureTool_ != Tool::Eraser && !selecting_
        && !movingSelection_) paintStroke(p, draft_);
    if (selecting_) {
        p->setPen(QPen(QColor("#277ed1"), 2.0, Qt::DashLine));
        p->setBrush(QColor(44, 125, 210, 40));
        p->drawRect(selectionRectangle_);
    }
    // The dashed 16:9 frame indicates what legacy PNG export and future
    // fixed-reference recording include. Outside the frame is still drawable.
    drawImageHandles(p);
    p->setPen(QPen(QColor("#8897ad"), 2.5, Qt::DashLine));
    p->setBrush(Qt::NoBrush);
    p->drawRect(QRectF(0,0,kBoardWidth,kBoardHeight));
    p->restore();
}
bool BoardView::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::TabletPress ||
        event->type() == QEvent::TabletMove ||
        event->type() == QEvent::TabletRelease) {
        auto* tablet = static_cast<QTabletEvent*>(event);
        lastTabletMs_ = tabletClock_.elapsed();
        const QPointF local = tablet->position();
        const QPointF where = mapToScene(local.toPoint());
        const bool released = event->type() == QEvent::TabletRelease;
        const bool sideDown = hasPenSideButton(tablet->buttons());
        const bool inverted = tablet->pointerType() == QPointingDevice::PointerType::Eraser;

        if (penPanning_) {
            if (released) {
                stopPan();
                tabletGesture_ = false;
                suppressTabletUntilRelease_ = false;
            } else if (!sideDown) {
                stopPan();
                suppressTabletUntilRelease_ = bool(tablet->buttons() & Qt::LeftButton);
            } else movePan(local);
            event->accept();
            return true;
        }
        if (released && suppressTabletUntilRelease_) {
            suppressTabletUntilRelease_ = false;
            tabletGesture_ = false;
            event->accept();
            return true;
        }
        if (suppressTabletUntilRelease_) {
            event->accept();
            return true;
        }
        if (penSideMode_ == PenSideMode::Pan && sideDown) {
            if (inputActive_) endInput(where, tablet->pressure());
            // Any side button works with the Graphire3 X11 Right/Middle mappings.
            startPan(local, true);
            tabletGesture_ = true;
            if (released) {
                stopPan();
                tabletGesture_ = false;
            }
            event->accept();
            return true;
        }
        const Tool t = (inverted ||
            (penSideMode_ == PenSideMode::Eraser && sideDown)) ? Tool::Eraser : tool_;
        if (event->type() == QEvent::TabletPress) {
            if (inputActive_ && mouseGesture_) {
                // Prefer tablet input over synthetic mouse input to avoid duplicates.
                inputActive_ = false;
                selecting_ = false;
                movingSelection_ = false;
                draft_ = {};
                gestureBefore_.clear();
                mouseGesture_ = false;
            }
            tabletGesture_ = true;
            if (t == Tool::Text) {
                if (requestText) requestText(where);
            } else beginInput(where, tablet->pressure(), t);
        } else if (event->type() == QEvent::TabletMove) {
            moveInput(where, tablet->pressure());
        } else {
            endInput(where, tablet->pressure());
            tabletGesture_ = false;
        }
        event->accept();
        return true;
    }
    return QGraphicsView::viewportEvent(event);
}
void BoardView::mousePressEvent(QMouseEvent* e) {
    if (e->source() != Qt::MouseEventNotSynthesized ||
        tabletGesture_ || tabletClock_.elapsed() - lastTabletMs_ < 180) {
        e->accept();
        return;
    }
    if (e->button() == Qt::RightButton &&
        tool_ == Tool::Select && hasSelectedImage() &&
        selectedImage().rect.contains(mapToScene(e->position().toPoint()))) {
        // A right-click on a *selected* media box opens its Z-order menu.
        // Movement over 8 pixels instead becomes the usual right-button pan.
        pendingMediaRightClick_ = true;
        rightClickStart_ = e->position();
        e->accept();
        return;
    }
    if (isMousePanButton(e->button())) {
        panMouseButton_ = e->button();
        startPan(e->position(), false);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        const QPointF where = mapToScene(e->pos());
        mouseGesture_ = true;
        if (tool_ == Tool::Text) {
            if (requestText) requestText(where);
            mouseGesture_ = false;
        } else beginInput(where, 0.5, tool_);
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}
void BoardView::mouseMoveEvent(QMouseEvent* e) {
    if (tabletGesture_ || e->source() != Qt::MouseEventNotSynthesized) {
        e->accept();
        return;
    }
    if (pendingMediaRightClick_) {
        const double distance=QLineF(rightClickStart_,e->position()).length();
        if (!(e->buttons() & Qt::RightButton)) {
            pendingMediaRightClick_ = false;
        } else if (distance > 8.0) {
            pendingMediaRightClick_ = false;
            if (isMousePanButton(Qt::RightButton)) {
                panMouseButton_ = Qt::RightButton;
                startPan(rightClickStart_, false);
                movePan(e->position());
            }
        }
        e->accept();
        return;
    }
    if (panning_ && !penPanning_) {
        if (e->buttons() & panMouseButton_) movePan(e->position());
        else stopPan();
        e->accept();
        return;
    }
    if (inputActive_ && mouseGesture_ && (e->buttons() & Qt::LeftButton)) {
        moveInput(mapToScene(e->pos()), 0.5);
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}
void BoardView::mouseReleaseEvent(QMouseEvent* e) {
    if (tabletGesture_ || e->source() != Qt::MouseEventNotSynthesized) {
        e->accept();
        return;
    }
    if (pendingMediaRightClick_) {
        const bool isClick=e->button()==Qt::RightButton &&
            QLineF(rightClickStart_,e->position()).length()<=8.0;
        pendingMediaRightClick_ = false;
        e->accept();
        if (isClick && tool_ == Tool::Select && hasSelectedImage()
            && requestMediaContextMenu)
            requestMediaContextMenu(e->globalPosition().toPoint());
        return;
    }
    if (panning_ && !penPanning_) {
        if (e->button() == panMouseButton_) stopPan();
        e->accept();
        return;
    }
    if (inputActive_ && mouseGesture_ && e->button() == Qt::LeftButton) {
        endInput(mapToScene(e->pos()), 0.5);
        mouseGesture_ = false;
        e->accept();
        return;
    }
    mouseGesture_ = false;
    QGraphicsView::mouseReleaseEvent(e);
}
void BoardView::contextMenuEvent(QContextMenuEvent* e) {
    // The right-button release above owns the menu. Suppress the additional
    // platform context-menu event so one click never opens the menu twice.
    // The keyboard Menu key remains available for accessibility.
    if (e->reason() == QContextMenuEvent::Keyboard &&
        tool_ == Tool::Select && hasSelectedImage() &&
        requestMediaContextMenu) {
        requestMediaContextMenu(e->globalPos());
    }
    e->accept();
}
void BoardView::wheelEvent(QWheelEvent* e) {
    if (!wheelZoomEnabled_) {
        e->ignore();
        return;
    }
    const qreal steps = e->angleDelta().y() != 0
        ? qreal(e->angleDelta().y())/120.0
        : qreal(e->pixelDelta().y())/100.0;
    if (!qFuzzyIsNull(steps))
        zoomAt(std::pow(1.18, steps), e->position().toPoint());
    e->accept();
}
void BoardView::selectAllStrokes() {
    selected_.clear();
    for (const auto& stroke : strokes_) selected_.insert(stroke.id);
    notifyState();
}
void BoardView::deselect() {
    selected_.clear();
    selectedImageId_ = 0;
    notifyState();
}
void BoardView::deleteSelection() {
    if (selected_.isEmpty()) {
        if (hasSelectedImage()) removeSelectedImage();
        return;
    }
    const auto before = strokes_;
    strokes_.erase(std::remove_if(strokes_.begin(), strokes_.end(),
        [this](const Stroke& stroke) { return selected_.contains(stroke.id); }),
        strokes_.end());
    selected_.clear();
    commit(before, "Delete selection");
    notifyState();
}
QJsonObject BoardView::strokeToJson(const Stroke& s) const {
    QJsonObject obj;
    obj.insert("id", QString::number(s.id));
    obj.insert("tool", int(s.tool));
    obj.insert("color", s.color.name(QColor::HexArgb));
    obj.insert("width", s.width);
    obj.insert("start", QJsonArray{s.start.x(), s.start.y()});
    obj.insert("end", QJsonArray{s.end.x(), s.end.y()});
    obj.insert("text", s.text);
    QJsonArray points;
    for (const auto& pt : s.points)
        points.append(QJsonArray{pt.pos.x(), pt.pos.y(), pt.pressure});
    obj.insert("points", points);
    return obj;
}
static bool validCoord(qreal n) {
    return std::isfinite(n) && n >= -kWorldLimit && n <= kWorldLimit;
}
bool BoardView::strokeFromJson(const QJsonObject& obj, Stroke* output) const {
    if (!output) return false;
    Stroke s;
    const int tool = obj.value("tool").toInt(-1);
    if (tool < 0 || tool > int(Tool::Text) || tool == int(Tool::Eraser) ||
        tool == int(Tool::Select)) return false;
    s.tool = static_cast<Tool>(tool);
    s.id = obj.value("id").toString().toLongLong();
    s.color = QColor(obj.value("color").toString());
    s.width = obj.value("width").toDouble(0);
    if (!s.color.isValid() || s.width <= 0 || s.width > 64) return false;
    const auto start = obj.value("start").toArray();
    const auto end = obj.value("end").toArray();
    if (start.size() != 2 || end.size() != 2) return false;
    s.start = QPointF(start.at(0).toDouble(), start.at(1).toDouble());
    s.end = QPointF(end.at(0).toDouble(), end.at(1).toDouble());
    if (!validCoord(s.start.x()) || !validCoord(s.start.y()) ||
        !validCoord(s.end.x()) || !validCoord(s.end.y())) return false;
    s.text = obj.value("text").toString().left(5000);
    const auto points = obj.value("points").toArray();
    if (points.size() > kMaxPointsPerStroke) return false;
    for (const auto& v : points) {
        const auto p = v.toArray();
        if (p.size() != 3) return false;
        const double x=p.at(0).toDouble(), y=p.at(1).toDouble(), pressure=p.at(2).toDouble();
        if (!validCoord(x) || !validCoord(y) || !std::isfinite(pressure) ||
            pressure < 0 || pressure > 1) return false;
        s.points.push_back({QPointF(x,y), pressure});
    }
    if ((s.tool == Tool::Pen || s.tool == Tool::Highlighter) && s.points.isEmpty())
        return false;
    *output = std::move(s);
    return true;
}
void BoardView::copySelection() {
    if (selected_.isEmpty()) return;
    QJsonArray arr;
    for (const auto& stroke : strokes_) if (selected_.contains(stroke.id))
        arr.append(strokeToJson(stroke));
    if (arr.isEmpty()) return;
    auto* mime = new QMimeData;
    mime->setData(kStrokeMime, QJsonDocument(arr).toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(mime);
    pasteOffset_ = 0;
    notifyState();
}
void BoardView::cutSelection() {
    if (selected_.isEmpty()) return;
    copySelection();
    deleteSelection();
}
void BoardView::pasteSelection() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(kStrokeMime)) return;
    const auto doc = QJsonDocument::fromJson(mime->data(kStrokeMime));
    if (!doc.isArray() || doc.array().size() > kMaxStrokes) return;
    QVector<Stroke> decoded;
    decoded.reserve(doc.array().size());
    for (const auto& value : doc.array()) {
        Stroke stroke;
        if (!strokeFromJson(value.toObject(), &stroke)) return;
        decoded.push_back(std::move(stroke));
    }
    auto before = strokes_;
    selected_.clear();
    pasteOffset_ = (pasteOffset_ % 8) + 1;
    const QPointF shift(35*pasteOffset_, 35*pasteOffset_);
    for (auto& stroke : decoded) {
        stroke.id = nextStrokeId();
        stroke.start += shift; stroke.end += shift;
        for (auto& point : stroke.points) point.pos += shift;
        strokes_.push_back(stroke);
        selected_.insert(stroke.id);
    }
    if (strokes_.size() == before.size()) return;
    commit(before, "Paste strokes");
    notifyState();
}
void BoardView::duplicateSelection() {
    if (selected_.isEmpty()) return;
    const auto before = strokes_;
    const auto oldSelection = selected_;
    selected_.clear();
    for (const auto& stroke : before) {
        if (!oldSelection.contains(stroke.id)) continue;
        Stroke copy = stroke;
        copy.id = nextStrokeId();
        copy.start += QPointF(35,35); copy.end += QPointF(35,35);
        for (auto& point : copy.points) point.pos += QPointF(35,35);
        strokes_.push_back(copy);
        selected_.insert(copy.id);
    }
    commit(before, "Duplicate strokes");
    notifyState();
}
void BoardView::clearBoard() {
    if (strokes_.isEmpty() && images_.isEmpty()) return;
    const auto beforeStrokes=strokes_;
    const auto beforeImages=images_;
    strokes_.clear();
    images_.clear();
    selected_.clear();
    selectedImageId_=0;
    pendingImage_={};
    activeUndo_->push(new HistoryCommand(this,beforeStrokes,strokes_,
                beforeImages,images_,"Clear board"));
    notifyState();
}
void BoardView::insertTextAt(const QPointF& where, const QString& text) {
    if (text.trimmed().isEmpty()) return;
    const auto before = strokes_;
    Stroke stroke; stroke.id = nextStrokeId();
    stroke.tool = Tool::Text; stroke.color = color_; stroke.width = width_;
    stroke.start = clampToBoard(where); stroke.end = stroke.start; stroke.text = text.left(5000);
    strokes_.push_back(stroke);
    commit(before, "Insert text");
    notifyState();
}
QJsonObject BoardView::documentObject(QString* error) const {
    QJsonObject document;
    document.insert("format", "GXWB-F02");
    document.insert("schema", 1);
    document.insert("boardWidth", kBoardWidth);
    document.insert("boardHeight", kBoardHeight);
    document.insert("background", int(background_));
    document.insert("viewCenter", QJsonArray{viewCenter().x(), viewCenter().y()});
    document.insert("viewScale", viewScale());
    QJsonArray embeddedImages;
    for (const auto& image : images_) {
        auto item = imageToJson(image);
        if (item.value("pngBase64").toString().isEmpty()) {
            if (error) *error = "Image serialization failed.";
            return {};
        }
        embeddedImages.append(item);
    }
    document.insert("images", embeddedImages);
    QJsonArray strokes;
    for (const auto& stroke : strokes_) strokes.append(strokeToJson(stroke));
    document.insert("strokes", strokes);
    return document;
}
bool BoardView::saveDocument(const QString& fileName, QString* error) const {
    const QJsonObject document=documentObject(error);
    if (document.isEmpty()) return false;
    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray payload=QJsonDocument(document).toJson();
    if (payload.size() > 80*1024*1024) {
        if (error) *error = "Document exceeds 80 MB.";
        file.cancelWriting();
        return false;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
bool BoardView::loadDocument(const QString& fileName, QString* error) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.size() > 80*1024*1024) {
        if (error) *error = "Document exceeds F02 size limit";
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error) *error = parseError.errorString();
        return false;
    }
    return loadDocumentObject(doc.object(),error);
}
bool BoardView::loadDocumentObject(const QJsonObject& obj, QString* error,
                                   bool preserveHistory) {
    if (obj.value("format") != "GXWB-F02" || obj.value("schema").toInt() != 1) {
        if (error) *error = "Unsupported document format";
        return false;
    }
    const auto arr = obj.value("strokes").toArray();
    if (arr.size() > kMaxStrokes) {
        if (error) *error = "Too many strokes";
        return false;
    }
    QVector<Stroke> loaded;
    loaded.reserve(arr.size());
    qint64 maxId = 0;
    for (const auto& row : arr) {
        Stroke stroke;
        if (!strokeFromJson(row.toObject(), &stroke)) {
            if (error) *error = "Invalid stroke";
            return false;
        }
        maxId = std::max(maxId, stroke.id);
        loaded.push_back(std::move(stroke));
    }
    const QJsonArray assets = obj.value("images").toArray();
    if (assets.size() > 80) {
        if (error) *error = "Too many embedded images.";
        return false;
    }
    QVector<ImageBox> decodedImages;
    qint64 maxImageId = 0;
    for (const auto& val : assets) {
        ImageBox image;
        if (!imageFromJson(val.toObject(),&image)) {
            if (error) *error = "Invalid embedded image.";
            return false;
        }
        maxImageId = std::max(maxImageId,image.id);
        decodedImages.push_back(std::move(image));
    }
    const int bg = obj.value("background").toInt(0);
    if (bg < 0 || bg > 2) {
        if (error) *error = "Invalid background";
        return false;
    }
    if (!preserveHistory) activeUndo_->clear();
    strokes_ = std::move(loaded);
    images_ = std::move(decodedImages);
    selected_.clear();
    selectedImageId_ = 0;
    pendingImage_ = {};
    nextImageId_ = maxImageId+1;
    nextId_ = maxId+1;
    background_ = static_cast<Background>(bg);
    // Older F02 files have no saved view and continue to open normally.
    const auto savedCenter = obj.value("viewCenter").toArray();
    const qreal savedScale = obj.value("viewScale").toDouble(0);
    if (savedCenter.size() == 2 && std::isfinite(savedScale) &&
        savedScale >= kMinViewScale && savedScale <= kMaxViewScale) {
        const QPointF center(savedCenter.at(0).toDouble(), savedCenter.at(1).toDouble());
        if (std::isfinite(center.x()) && std::isfinite(center.y()) &&
            std::abs(center.x()) <= kWorldLimit &&
            std::abs(center.y()) <= kWorldLimit) {
            setTransform(QTransform::fromScale(savedScale, savedScale));
            centerOn(center);
            viewCustomized_ = true;
        } else fitBoard();
    } else fitBoard();
    notifyState();
    return true;
}
bool BoardView::exportPng(const QString& fileName) const {
    QImage image(kBoardWidth, kBoardHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(background_ == Background::Dark ? QColor("#171c27") : QColor("#ffffff"));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    if (background_ == Background::Grid) {
        p.setPen(QPen(QColor("#e3e8ef"), 1.25));
        for (int x=0;x<=kBoardWidth;x+=40) p.drawLine(x,0,x,kBoardHeight);
        for (int y=0;y<=kBoardHeight;y+=40) p.drawLine(0,y,kBoardWidth,y);
    }
    for (const auto& box : images_) paintImage(&p,box);
    for (const auto& stroke : strokes_) paintStroke(&p, stroke);
    p.end();
    return image.save(fileName, "PNG");
}
bool BoardView::addSyntheticStroke(const QPointF& a, const QPointF& b, qreal pressure) {
    if (inputActive_) return false;
    beginInput(a, pressure, Tool::Pen);
    moveInput(b, pressure);
    endInput(b, pressure);
    return !strokes_.isEmpty();
}

bool BoardView::exportViewPng(const QString& fileName) const {
    const QImage image=renderViewImage(QSize(kBoardWidth,kBoardHeight));
    return !image.isNull() && image.save(fileName,"PNG");
}
QImage BoardView::renderViewImage(const QSize& outputSize) const {
    if (!outputSize.isValid() || outputSize.width()>1920 ||
        outputSize.height()>1080 || outputSize.width()<160 ||
        outputSize.height()<90) return {};
    QRectF region = visibleWorldRect();
    if (region.width() <= 0 || region.height() <= 0) return {};
    const qreal aspect = qreal(outputSize.width()) / outputSize.height();
    const QPointF center = region.center();
    // Fit the current visible content into 16:9 (extend, never crop).
    if (region.width()/region.height() > aspect)
        region.setHeight(region.width()/aspect);
    else
        region.setWidth(region.height()*aspect);
    region.moveCenter(center);
    QImage image(outputSize.width(), outputSize.height(), QImage::Format_ARGB32);
    image.fill(background_ == Background::Dark ? QColor("#171c27") : QColor("#ffffff"));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(qreal(outputSize.width())/region.width(),
                  qreal(outputSize.height())/region.height());
    painter.translate(-region.left(), -region.top());
    if (background_ == Background::Grid) {
        painter.setPen(QPen(QColor("#e3e8ef"), 1.25));
        const int left = int(std::floor(region.left()/40))*40;
        const int top = int(std::floor(region.top()/40))*40;
        for (int x=left; x<=region.right(); x+=40)
            painter.drawLine(x, region.top(), x, region.bottom());
        for (int y=top; y<=region.bottom(); y+=40)
            painter.drawLine(region.left(), y, region.right(), y);
    }
    for (const auto& box : images_) {
        if (box.rect.intersects(region)) paintImage(&painter,box);
    }
    for (const auto& stroke : strokes_) {
        if (strokeBounds(stroke).intersects(region)) paintStroke(&painter, stroke);
    }
    painter.end();
    // Selection handles, page outlines, menus and cursor are deliberately
    // absent from the recording; ink is always rendered above media boxes.
    return image;
}
