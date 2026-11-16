// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#include "BoardView.h"
#include <QImageReader>
#include <QRegularExpression>
#include <QBuffer>
#include <QPainter>
#include <QFileInfo>
#include <QFile>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QUrl>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <cmath>
#include <algorithm>

static constexpr int kMaxImageCount = 80;
static constexpr qint64 kMaxSourceBytes = 24LL*1024*1024;
static constexpr qint64 kMaxPixels = 36LL*1000*1000;
static constexpr double kMinBoxSize = 32.0;

static QImage cameraPlaceholder(const QString& label) {
    QImage image(640,360,QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor("#172237"));
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#dbe8f8"));
    QFont font=p.font(); font.setPixelSize(27); font.setBold(true);
    p.setFont(font);
    p.drawText(image.rect().adjusted(28,28,-28,-28),Qt::AlignCenter|Qt::TextWordWrap,
               label+"\nCamera not streaming");
    return image;
}
int BoardView::cameraBoxCount(const QString& device) const {
    int count=0;
    for (const auto& image : images_)
        if (image.isCamera && (device.isEmpty() || image.cameraDevice==device)) ++count;
    return count;
}
bool BoardView::cameraFrameAvailable(const QString& device) const {
    for (const auto& image : images_)
        if (image.isCamera && image.cameraDevice==device && !image.image.isNull())
            return true;
    return false;
}
void BoardView::updateCameraFrame(const QString& device,const QImage& frame) {
    if (frame.isNull() || frame.width()<80 || frame.height()<60 ||
        qint64(frame.width())*frame.height()>1920*1080) return;
    for (auto& image : images_) {
        if (image.isCamera && image.cameraDevice==device) image.image=frame;
    }
    if (pendingImage_.isCamera && pendingImage_.cameraDevice==device)
        pendingImage_.image=frame;
    // Real-time frames must not create Undo actions or update the settings panel.
    viewport()->update();
}
bool BoardView::startCameraPlacement(const QString& device) {
    if (!device.startsWith("/dev/video") && device!="test-pattern" &&
        !QRegularExpression("^net:[a-f0-9]{20}$").match(device).hasMatch())
        return false;
    ImageBox camera;
    camera.name=device.startsWith("net:") ? QString("IP camera") : QString("Camera %1").arg(device);
    camera.sourcePath=device;
    camera.cameraDevice=device;
    camera.isCamera=true;
    camera.image=cameraPlaceholder(device);
    pendingImage_=std::move(camera);
    selectedImageId_=0;
    setTool(Tool::Select);
    viewport()->update();
    return true;
}
bool BoardView::insertCameraBox(const QString& device,const QRectF& rect) {
    ImageBox camera;
    camera.name=device.startsWith("net:") ? QString("IP camera") : QString("Camera %1").arg(device);
    camera.sourcePath=device;
    camera.cameraDevice=device;
    camera.isCamera=true;
    camera.image=cameraPlaceholder(device);
    return addImageBox(std::move(camera),rect);
}
bool BoardView::hasSelectedImage() const {
    return imageById(selectedImageId_) != nullptr;
}
BoardView::ImageBox BoardView::selectedImage() const {
    const auto* found = imageById(selectedImageId_);
    return found ? *found : ImageBox();
}
BoardView::ImageBox* BoardView::imageById(qint64 id) {
    for (auto& img : images_) if (img.id == id) return &img;
    return nullptr;
}
const BoardView::ImageBox* BoardView::imageById(qint64 id) const {
    for (const auto& img : images_) if (img.id == id) return &img;
    return nullptr;
}
bool BoardView::readImage(const QString& filename, ImageBox* output, QString* error) const {
    if (!output) return false;
    const QFileInfo info(filename);
    const QString ext = info.suffix().toLower();
    if (!info.exists() || !info.isFile() || info.size() > kMaxSourceBytes ||
        !(ext=="png" || ext=="jpg" || ext=="jpeg" || ext=="webp")) {
        if (error) *error = "Select a PNG, JPEG or WebP image smaller than 24 MB.";
        return false;
    }
    QImageReader reader(info.absoluteFilePath());
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid() && (qint64(size.width())*size.height() > kMaxPixels ||
        size.width() > 12000 || size.height() > 12000)) {
        if (error) *error = "The image exceeds the 36 megapixel limit.";
        return false;
    }
    QImage loaded = reader.read();
    if (loaded.isNull() || qint64(loaded.width())*loaded.height() > kMaxPixels) {
        if (error) *error = "Unable to decode image or dimensions too large.";
        return false;
    }
    ImageBox result;
    result.name = info.completeBaseName();
    result.sourcePath = info.absoluteFilePath();
    result.image = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    *output = std::move(result);
    return true;
}
bool BoardView::startImagePlacement(const QString& path, QString* error) {
    ImageBox image;
    if (!readImage(path, &image, error)) return false;
    pendingImage_ = std::move(image);
    selectedImageId_ = 0;
    setTool(Tool::Select);
    notifyState();
    return true;
}
void BoardView::cancelImagePlacement() {
    pendingImage_ = {};
    placingImage_ = false;
    imageGesture_ = false;
    notifyState();
}
bool BoardView::addImageBox(ImageBox image, const QRectF& sourceRect) {
    if (image.image.isNull() || images_.size() >= kMaxImageCount) return false;
    const QRectF rect = sourceRect.normalized();
    if (rect.width() < kMinBoxSize || rect.height() < kMinBoxSize) return false;
    const auto before = images_;
    image.id = nextImageId_++;
    image.rect = rect;
    image.opacity = std::clamp(image.opacity, 0.0, 1.0);
    selectedImageId_ = image.id;
    images_.push_back(std::move(image));
    commitImageChange(before, "Insert image");
    return true;
}
bool BoardView::insertImageFromFile(const QString& filename, const QRectF& rect, QString* error) {
    ImageBox img;
    if (!readImage(filename, &img, error)) return false;
    if (!addImageBox(std::move(img), rect)) {
        if (error) *error = "Invalid image rectangle or too many images.";
        return false;
    }
    return true;
}
QVector<QPointF> BoardView::imageHandles(const QRectF& r) const {
    const QPointF center = r.center();
    return {
        r.topLeft(), QPointF(center.x(),r.top()), r.topRight(),
        QPointF(r.right(),center.y()), r.bottomRight(),
        QPointF(center.x(),r.bottom()), r.bottomLeft(),
        QPointF(r.left(),center.y())
    };
}
int BoardView::hitImageHandle(const ImageBox& box, const QPointF& pos) const {
    const qreal tolerance = std::clamp(11.0/std::max(0.05, viewScale()), 5.0, 120.0);
    const auto handles = imageHandles(box.rect);
    for (int i=0; i<handles.size(); ++i)
        if (QLineF(handles[i],pos).length() <= tolerance) return i;
    return -1;
}
qint64 BoardView::imageAt(const QPointF& pos) const {
    for (qsizetype i=images_.size(); i>0; --i) {
        if (images_[i-1].rect.contains(pos)) return images_[i-1].id;
    }
    return 0;
}
void BoardView::moveImageGesture(const QPointF& pos) {
    if (!imageGesture_) return;
    const QPointF current = clampToBoard(pos);
    if (placingImage_) {
        imagePreviewRect_ = QRectF(imageGestureStart_, current).normalized();
        notifyState();
        return;
    }
    ImageBox* box = imageById(selectedImageId_);
    if (!box) return;
    if (imageResizeHandle_ < 0) {
        const QPointF delta = current - imageGestureLast_;
        box->rect.translate(delta);
        imageGestureLast_ = current;
        notifyState();
        return;
    }
    const QRectF original = imageGestureOriginalRect_;
    qreal left=original.left(), right=original.right(), top=original.top(), bottom=original.bottom();
    const int h=imageResizeHandle_;
    const bool leftEdge = h==0 || h==6 || h==7;
    const bool rightEdge = h==2 || h==3 || h==4;
    const bool topEdge = h==0 || h==1 || h==2;
    const bool bottomEdge = h==4 || h==5 || h==6;
    if (leftEdge) left = std::min(current.x(), right-kMinBoxSize);
    if (rightEdge) right = std::max(current.x(), left+kMinBoxSize);
    if (topEdge) top = std::min(current.y(), bottom-kMinBoxSize);
    if (bottomEdge) bottom = std::max(current.y(), top+kMinBoxSize);
    QRectF candidate(QPointF(left,top),QPointF(right,bottom));
    if (box->lockAspect) {
        const qreal aspect=std::max(0.001,original.width()/original.height());
        const bool horizontalOnly = h==3 || h==7;
        const bool verticalOnly = h==1 || h==5;
        if (horizontalOnly) {
            const qreal height=std::max(kMinBoxSize,candidate.width()/aspect);
            candidate.setTop(original.center().y()-height/2);
            candidate.setBottom(original.center().y()+height/2);
        } else if (verticalOnly) {
            const qreal width=std::max(kMinBoxSize,candidate.height()*aspect);
            candidate.setLeft(original.center().x()-width/2);
            candidate.setRight(original.center().x()+width/2);
        } else {
            const qreal widthRatio=candidate.width()/original.width();
            const qreal heightRatio=candidate.height()/original.height();
            // Choose the dimension with the larger relative change.
            const qreal factor=(std::abs(widthRatio-1) > std::abs(heightRatio-1))
                 ? widthRatio : heightRatio;
            const qreal newW=std::max(kMinBoxSize,original.width()*factor);
            const qreal newH=std::max(kMinBoxSize,original.height()*factor);
            const QPointF anchor = (h==0) ? original.bottomRight()
                : (h==2) ? original.bottomLeft()
                : (h==4) ? original.topLeft() : original.topRight();
            candidate=QRectF(anchor.x()-(leftEdge?newW:0),
                             anchor.y()-(topEdge?newH:0),newW,newH);
        }
    }
    box->rect=candidate.normalized();
    notifyState();
}
void BoardView::finishImageGesture(const QPointF& pos) {
    if (!imageGesture_) return;
    moveImageGesture(pos);
    if (placingImage_) {
        QRectF rect=imagePreviewRect_;
        if (rect.width()<kMinBoxSize || rect.height()<kMinBoxSize) {
            const double w=560.0;
            const double h=w*pendingImage_.image.height()/std::max(1,pendingImage_.image.width());
            rect=QRectF(imageGestureStart_, QSizeF(w,std::clamp(h,80.0,800.0)));
        }
        ImageBox image=pendingImage_;
        pendingImage_={};
        addImageBox(std::move(image), rect);
    } else if (hasImageChanged(imageGestureBefore_)) {
        commitImageChange(imageGestureBefore_,"Transform image");
    }
    imageGesture_=false;
    placingImage_=false;
    imageResizeHandle_=-1;
    inputActive_=false;
    notifyState();
}
bool BoardView::hasImageChanged(const QVector<ImageBox>& before) const {
    if (before.size()!=images_.size()) return true;
    for (qsizetype i=0;i<before.size();++i) {
        const auto& a=before[i];
        const auto& b=images_[i];
        if (a.id!=b.id || a.rect!=b.rect || a.opacity!=b.opacity
            || a.lockAspect!=b.lockAspect || a.fillCrop!=b.fillCrop) return true;
    }
    return false;
}
void BoardView::paintImage(QPainter* p,const ImageBox& box) const {
    if (box.image.isNull()) return;
    p->save();
    p->setOpacity(box.opacity);
    p->setClipRect(box.rect);
    const QSizeF size=box.image.size();
    QRectF src(0,0,size.width(),size.height());
    QRectF dst=box.rect;
    const double sourceAspect=size.width()/std::max(1.0,size.height());
    const double targetAspect=dst.width()/std::max(1.0,dst.height());
    const QPointF targetCenter=dst.center();
    if (box.fillCrop) {
        if (sourceAspect>targetAspect) {
            const double w=size.height()*targetAspect;
            src.setLeft((size.width()-w)/2);
            src.setWidth(w);
        } else {
            const double h=size.width()/targetAspect;
            src.setTop((size.height()-h)/2);
            src.setHeight(h);
        }
    } else {
        if (sourceAspect>targetAspect) {
            const double height=dst.width()/sourceAspect;
            dst.setTop(targetCenter.y()-height/2);
            dst.setHeight(height);
        } else {
            const double width=dst.height()*sourceAspect;
            dst.setLeft(targetCenter.x()-width/2);
            dst.setWidth(width);
        }
    }
    p->drawImage(dst,box.image,src);
    p->restore();
}
void BoardView::drawImageHandles(QPainter* p) const {
    const ImageBox* box=imageById(selectedImageId_);
    if (!box) return;
    p->save();
    const qreal scale=std::max(0.03,viewScale());
    const qreal handleSize=std::clamp(13.0/scale,5.0,120.0);
    p->setPen(QPen(QColor("#2484da"),2.2/scale,Qt::DashLine));
    p->setBrush(Qt::NoBrush);
    p->drawRect(box->rect);
    p->setPen(QPen(QColor("#10589f"),1.6/scale));
    p->setBrush(QColor("#f7fbff"));
    for (const auto& point : imageHandles(box->rect))
        p->drawRect(QRectF(point.x()-handleSize/2,point.y()-handleSize/2,
                           handleSize,handleSize));
    p->restore();
}
void BoardView::setSelectedImageAspectLocked(bool value) {
    ImageBox* img=imageById(selectedImageId_);
    if (!img || img->lockAspect==value) return;
    const auto before=images_;
    img->lockAspect=value;
    commitImageChange(before,"Lock image aspect");
}
void BoardView::setSelectedImageFillCrop(bool value) {
    ImageBox* img=imageById(selectedImageId_);
    if (!img || img->fillCrop==value) return;
    const auto before=images_;
    img->fillCrop=value;
    commitImageChange(before,"Change image fit");
}
void BoardView::setSelectedImageOpacity(qreal value) {
    ImageBox* img=imageById(selectedImageId_);
    if (!img) return;
    value=std::clamp(value,0.0,1.0);
    if (std::abs(img->opacity-value)<0.00001) return;
    const auto before=images_;
    img->opacity=value;
    commitImageChange(before,"Change image opacity");
}
void BoardView::bringSelectedImageForward() {
    for (qsizetype i=0;i+1<images_.size();++i) {
        if (images_[i].id!=selectedImageId_) continue;
        const auto before=images_;
        images_.swapItemsAt(i,i+1);
        commitImageChange(before,"Bring image forward");
        return;
    }
}
void BoardView::sendSelectedImageBackward() {
    for (qsizetype i=1;i<images_.size();++i) {
        if (images_[i].id!=selectedImageId_) continue;
        const auto before=images_;
        images_.swapItemsAt(i,i-1);
        commitImageChange(before,"Send image backward");
        return;
    }
}
bool BoardView::canBringSelectedImageForward() const {
    for (qsizetype i=0; i<images_.size(); ++i)
        if (images_[i].id == selectedImageId_) return i+1 < images_.size();
    return false;
}
bool BoardView::canSendSelectedImageBackward() const {
    for (qsizetype i=0; i<images_.size(); ++i)
        if (images_[i].id == selectedImageId_) return i > 0;
    return false;
}
void BoardView::bringSelectedImageToFront() {
    if (!canBringSelectedImageForward()) return;
    const auto before=images_;
    for (qsizetype i=0; i+1<images_.size(); ++i) {
        if (images_[i].id != selectedImageId_) continue;
        images_.append(images_.takeAt(i));
        commitImageChange(before,"Bring media to front");
        return;
    }
}
void BoardView::sendSelectedImageToBack() {
    if (!canSendSelectedImageBackward()) return;
    const auto before=images_;
    for (qsizetype i=1; i<images_.size(); ++i) {
        if (images_[i].id != selectedImageId_) continue;
        images_.prepend(images_.takeAt(i));
        commitImageChange(before,"Send media to back");
        return;
    }
}
void BoardView::removeSelectedImage() {
    if (!hasSelectedImage()) return;
    const auto before=images_;
    images_.erase(std::remove_if(images_.begin(),images_.end(),[this](const ImageBox& box) {
        return box.id==selectedImageId_;
    }),images_.end());
    selectedImageId_=0;
    commitImageChange(before,"Remove image");
}
QJsonObject BoardView::imageToJson(const ImageBox& box) const {
    QJsonObject json;
    json.insert("id",QString::number(box.id));
    json.insert("name",box.name);
    json.insert("x",box.rect.x());
    json.insert("y",box.rect.y());
    json.insert("width",box.rect.width());
    json.insert("height",box.rect.height());
    json.insert("opacity",box.opacity);
    json.insert("aspectLocked",box.lockAspect);
    json.insert("fillCrop",box.fillCrop);
    json.insert("isCamera",box.isCamera);
    if (box.isCamera) json.insert("cameraDevice",box.cameraDevice);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    if (box.image.save(&buffer,"PNG"))
        json.insert("pngBase64",QString::fromLatin1(buffer.data().toBase64()));
    return json;
}
bool BoardView::imageFromJson(const QJsonObject& json,ImageBox* output) const {
    if (!output) return false;
    ImageBox box;
    box.id=json.value("id").toString().toLongLong();
    if (box.id<=0) return false;
    box.name=json.value("name").toString().left(200);
    box.rect=QRectF(json.value("x").toDouble(),json.value("y").toDouble(),
                    json.value("width").toDouble(),json.value("height").toDouble());
    if (!std::isfinite(box.rect.x()) || !std::isfinite(box.rect.y()) ||
        !std::isfinite(box.rect.width()) || !std::isfinite(box.rect.height()) ||
        std::abs(box.rect.x())>65536 || std::abs(box.rect.y())>65536 ||
        box.rect.width()<kMinBoxSize || box.rect.height()<kMinBoxSize ||
        box.rect.width()>130000 || box.rect.height()>130000) return false;
    box.opacity=json.value("opacity").toDouble(1);
    if (!std::isfinite(box.opacity) || box.opacity<0 || box.opacity>1) return false;
    box.lockAspect=json.value("aspectLocked").toBool(true);
    box.fillCrop=json.value("fillCrop").toBool(false);
    box.isCamera=json.value("isCamera").toBool(false);
    if (box.isCamera) {
        box.cameraDevice=json.value("cameraDevice").toString();
        if (box.cameraDevice!="test-pattern" &&
            !QRegularExpression("^/dev/video[0-9]+$").match(box.cameraDevice).hasMatch() &&
            !QRegularExpression("^net:[a-f0-9]{20}$").match(box.cameraDevice).hasMatch())
            return false;
    }
    const QString encoded=json.value("pngBase64").toString();
    if (encoded.isEmpty() || encoded.size() > 60*1024*1024) return false;
    const QByteArray bytes=QByteArray::fromBase64(encoded.toLatin1());
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer,"PNG");
    const QSize imageSize=reader.size();
    if (!imageSize.isValid() || qint64(imageSize.width())*imageSize.height()>kMaxPixels)
        return false;
    box.image=reader.read();
    if (box.image.isNull()) return false;
    *output=std::move(box);
    return true;
}
namespace {
constexpr int kMaxDropImages = 8;

bool isSupportedLocalImage(const QUrl& url) {
    if (!url.isLocalFile()) return false; // never fetch from Internet
    const QFileInfo f(url.toLocalFile());
    const QString suffix=f.suffix().toLower();
    return f.isFile() && f.size()>0 && f.size()<=kMaxSourceBytes &&
        (suffix=="png" || suffix=="jpg" || suffix=="jpeg" || suffix=="webp");
}
bool acceptsExternalImages(const QMimeData* mime) {
    if (!mime) return false;
    if (mime->hasUrls()) {
        for (const QUrl& uri:mime->urls())
            if (isSupportedLocalImage(uri)) return true;
    }
    return mime->hasImage();
}
QRectF imageDropRect(const QRectF& view, const QPointF& anchor,
                     const QSize& imageSize, int order) {
    // Choose a useful visual size regardless of the current zoom, and keep
    // the image visible when it is released near the canvas edges.
    if (view.isEmpty() || !imageSize.isValid()) return {};
    double width=std::min(1300.0,view.width()*0.42);
    double height=width*double(imageSize.height())/double(imageSize.width());
    if (height>view.height()*0.70) {
        const double scale=(view.height()*0.70)/height;
        width*=scale; height*=scale;
    }
    width=std::max(40.0,width);
    height=std::max(40.0,height);
    const QPointF shift((order%3)*std::min(65.0,width*0.15),
                        (order/3)*std::min(65.0,height*0.15));
    QPointF start=anchor+shift;
    if (width<=view.width()) {
        start.setX(std::clamp(start.x(),view.left(),view.right()-width));
    }
    if (height<=view.height()) {
        start.setY(std::clamp(start.y(),view.top(),view.bottom()-height));
    }
    return QRectF(start,QSizeF(width,height));
}
}
void BoardView::dragEnterEvent(QDragEnterEvent* event) {
    if (acceptsExternalImages(event->mimeData()) &&
        event->possibleActions().testFlag(Qt::CopyAction)) {
        // Explicit copy: Thunar's source file must never be moved/deleted.
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}
void BoardView::dragMoveEvent(QDragMoveEvent* event) {
    if (acceptsExternalImages(event->mimeData()) &&
        event->possibleActions().testFlag(Qt::CopyAction)) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    } else {
        event->ignore();
    }
}
void BoardView::dropEvent(QDropEvent* event) {
    const QMimeData* mime=event->mimeData();
    if (!acceptsExternalImages(mime) ||
        !event->possibleActions().testFlag(Qt::CopyAction)) {
        event->ignore();
        return;
    }
    const QPointF anchor=mapToScene(event->position().toPoint());
    const QRectF view=visibleWorldRect();
    int added=0;
    QString error;
    // Native Thunar/XFCE and most file managers send text/uri-list.
    if (mime->hasUrls()) {
        for (const QUrl& url:mime->urls()) {
            if (added>=kMaxDropImages) break;
            if (!isSupportedLocalImage(url)) continue;
            ImageBox box;
            if (!readImage(url.toLocalFile(),&box,&error)) continue;
            const QRectF rect=imageDropRect(view,anchor,box.image.size(),added);
            if (addImageBox(std::move(box),rect)) ++added;
            else error="Image count limit reached.";
        }
    }
    // Other applications may provide decoded image data, rather than a
    // filesystem URI. Treat the payload as a one-time embedded image.
    if (added==0 && mime->hasImage()) {
        const QVariant imageData=mime->imageData();
        if (imageData.canConvert<QImage>()) {
            QImage image=imageData.value<QImage>();
            if (!image.isNull() && qint64(image.width())*image.height()<=kMaxPixels &&
                image.width()<=12000 && image.height()<=12000) {
                ImageBox box;
                box.name="Dropped image";
                box.image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                if (addImageBox(std::move(box),
                        imageDropRect(view,anchor,image.size(),0))) ++added;
                else error="Image count limit reached.";
            } else error="Image is too large or invalid.";
        } else error="Unsupported image payload.";
    }
    if (added==0) {
        event->ignore();
        if (externalImagesDropped)
            externalImagesDropped(0,error.isEmpty()
              ? QString("No supported local PNG/JPEG/WebP image.")
              : error);
        return;
    }
    event->setDropAction(Qt::CopyAction);
    event->accept();
    if (externalImagesDropped)
        externalImagesDropped(added,QString());
}
