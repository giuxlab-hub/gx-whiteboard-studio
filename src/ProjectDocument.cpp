// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#include "ProjectDocument.h"
#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QSet>
#include <algorithm>

namespace {
QString freshId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QJsonObject emptyScene(const QJsonObject& from) {
    QJsonObject object=from;
    object.insert("images",QJsonArray());
    object.insert("strokes",QJsonArray());
    object.remove("viewCenter");
    object.remove("viewScale");
    return object;
}
}
bool ProjectDocument::initialize(BoardView* board) {
    if (!board) return false;
    QString error;
    const QJsonObject initial=board->documentObject(&error);
    if (initial.isEmpty()) return false;
    scenes_.clear();
    scenes_.append({freshId(),"Scene 1",initial});
    board->resetSceneHistories(scenes_[0].id);
    projectId_=freshId();
    title_="Untitled project";
    filename_.clear();
    currentIndex_=0;
    dirty_=false;
    return true;
}
QString ProjectDocument::sceneTitle(int index) const {
    if (index<0 || index>=scenes_.size()) return {};
    return scenes_.at(index).title;
}
QStringList ProjectDocument::names() const {
    QStringList titles;
    for (const auto& scene:scenes_) titles << scene.title;
    return titles;
}
bool ProjectDocument::capture(BoardView* board,QString* error) {
    if (!board || scenes_.isEmpty() || currentIndex_>=scenes_.size()) {
        if (error) *error="The current project has no active scene.";
        return false;
    }
    const QJsonObject current=board->documentObject(error);
    if (current.isEmpty()) return false;
    if (scenes_[currentIndex_].document!=current) {
        scenes_[currentIndex_].document=current;
        dirty_=true;
    }
    return true;
}
bool ProjectDocument::select(int index,BoardView* board,QString* error) {
    if (!board || index<0 || index>=scenes_.size()) return false;
    if (index==currentIndex_) return true;
    if (!capture(board,error)) return false;
    const QString priorId=scenes_[currentIndex_].id;
    board->setHistoryScene(scenes_[index].id);
    if (!board->loadDocumentObject(scenes_[index].document,error,true)) {
        board->setHistoryScene(priorId);
        return false;
    }
    currentIndex_=index;
    return true;
}
bool ProjectDocument::addBlank(BoardView* board,QString* error) {
    if (scenes_.size()>=MaxScenes) {
        if (error) *error="Maximum 24 scenes per project.";
        return false;
    }
    if (!capture(board,error)) return false;
    const QJsonObject blank=emptyScene(scenes_[currentIndex_].document);
    const QString id=freshId();
    const QString priorId=scenes_[currentIndex_].id;
    board->setHistoryScene(id);
    if (!board->loadDocumentObject(blank,error,true)) {
        board->setHistoryScene(priorId);
        board->removeSceneHistory(id);
        return false;
    }
    const int target=currentIndex_+1;
    scenes_.insert(target,{id,QString("Scene %1").arg(scenes_.size()+1),blank});
    currentIndex_=target;
    dirty_=true;
    return true;
}
bool ProjectDocument::duplicate(BoardView* board,QString* error) {
    if (scenes_.size()>=MaxScenes) {
        if (error) *error="Maximum 24 scenes per project.";
        return false;
    }
    if (!capture(board,error)) return false;
    const Scene copy={freshId(),scenes_[currentIndex_].title+" copy",
                      scenes_[currentIndex_].document};
    const int target=currentIndex_+1;
    const QString priorId=scenes_[currentIndex_].id;
    board->setHistoryScene(copy.id);
    if (!board->loadDocumentObject(copy.document,error,true)) {
        board->setHistoryScene(priorId);
        board->removeSceneHistory(copy.id);
        return false;
    }
    scenes_.insert(target,copy);
    currentIndex_=target;
    dirty_=true;
    return true;
}
bool ProjectDocument::rename(int index,const QString& name) {
    const QString title=name.trimmed();
    if (index<0 || index>=scenes_.size() || title.isEmpty() ||
        title.size()>100) return false;
    if (title==scenes_[index].title) return true;
    scenes_[index].title=title;
    dirty_=true;
    return true;
}
bool ProjectDocument::remove(int index,BoardView* board,QString* error) {
    if (!board || index<0 || index>=scenes_.size() ||
        scenes_.size()==1) return false;
    if (!capture(board,error)) return false;
    // If deleting the first scene, select the next scene *before* removing
    // index 0; otherwise the deleted scene would be reloaded by mistake.
    const int loadIndex=index==currentIndex_
        ? (index>0?index-1:index+1) : currentIndex_;
    const QString deletedId=scenes_[index].id;
    if (index==currentIndex_) {
        board->setHistoryScene(scenes_[loadIndex].id);
        if (!board->loadDocumentObject(
                scenes_[loadIndex].document,error,true)) {
            board->setHistoryScene(deletedId);
            return false;
        }
    }
    scenes_.removeAt(index);
    currentIndex_=loadIndex-(index<loadIndex ? 1:0);
    board->removeSceneHistory(deletedId);
    dirty_=true;
    return true;
}
bool ProjectDocument::newProject(BoardView* board) {
    if (!board) return false;
    QString error;
    const QJsonObject current=board->documentObject(&error);
    if (current.isEmpty()) return false;
    const QJsonObject blank=emptyScene(current);
    if (!board->loadDocumentObject(blank,&error)) return false;
    scenes_={{freshId(),"Scene 1",blank}};
    board->resetSceneHistories(scenes_[0].id);
    projectId_=freshId();
    title_="Untitled project";
    filename_.clear();
    currentIndex_=0;
    dirty_=false;
    return true;
}
QByteArray ProjectDocument::serialize() const {
    QJsonObject obj;
    obj.insert("format","GXWB-Project");
    obj.insert("schema",1);
    obj.insert("id",projectId_);
    obj.insert("title",title_);
    obj.insert("filePath",filename_);
    obj.insert("updatedUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    obj.insert("currentScene",currentIndex_);
    QJsonArray list;
    for (const auto& scene:scenes_) {
        QJsonObject row;
        row.insert("id",scene.id);
        row.insert("title",scene.title);
        row.insert("canvas",scene.document);
        list.append(row);
    }
    obj.insert("scenes",list);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}
bool ProjectDocument::writeAtomic(const QString& path,
                                  const QByteArray& bytes,QString* error) {
    if (bytes.isEmpty() || bytes.size()>MaxProjectBytes) {
        if (error) *error="Project exceeds the 192 MB limit.";
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error=file.errorString();
        return false;
    }
    if (file.write(bytes)!=bytes.size() || !file.commit()) {
        if (error) *error=file.errorString();
        return false;
    }
    return true;
}
bool ProjectDocument::writeBackup(const QString& path,QString* error) {
    const QFileInfo old(path);
    if (!old.exists()) return true;
    if (!old.isFile() || old.size()<=0 || old.size()>MaxProjectBytes) {
        if (error) *error="Cannot create a safe backup of the existing project.";
        return false;
    }
    // A damaged project must never replace the last known-good .bak.
    // Validate the container before creating a new backup. A recovery must
    // be saved under a NEW name, not written over a corrupt primary.
    QFile probe(path);
    if (!probe.open(QIODevice::ReadOnly)) {
        if (error) *error=probe.errorString();
        return false;
    }
    probe.close();
    // Strictly validate every scene and its embedded media before replacing
    // a known-good backup. This happens only on manual project Save.
    QVector<Scene> validated;
    int selected=-1;
    QString validationError;
    if (!readProject(path,&validated,&selected,nullptr,nullptr,nullptr,&validationError)) {
        if (error) *error="Existing project is invalid; choose a NEW filename to preserve its backup: "+validationError;
        return false;
    }
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error=in.errorString();
        return false;
    }
    return writeAtomic(path+".bak",in.readAll(),error);
}
bool ProjectDocument::save(const QString& path,BoardView* board,QString* error) {
    const QString absolute=QFileInfo(path).absoluteFilePath();
    if (!absolute.endsWith(".gxwp",Qt::CaseInsensitive)) {
        if (error) *error="Project filename must end with .gxwp.";
        return false;
    }
    if (!capture(board,error)) return false;
    const QString priorFile=filename_;
    filename_=absolute;
    if (title_=="Untitled project")
        title_=QFileInfo(absolute).completeBaseName();
    const QByteArray payload=serialize();
    if (!writeBackup(absolute,error) || !writeAtomic(absolute,payload,error)) {
        filename_=priorFile;
        return false;
    }
    // Recovery files are not the main document and are removed only after
    // successful atomic save. This does not touch the .bak checkpoint.
    if (!priorFile.isEmpty() && priorFile!=absolute) {
        const QString nextFile=filename_;
        filename_=priorFile;
        clearRecovery();
        filename_=nextFile;
    }
    clearRecovery();
    dirty_=false;
    return true;
}
bool ProjectDocument::readProject(const QString& path,QVector<Scene>* out,
                                  int* selected,QString* title,QString* uuid,
                                  QString* originalPath,QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error=file.errorString();
        return false;
    }
    if (file.size()<=0 || file.size()>MaxProjectBytes) {
        if (error) *error="Invalid or oversized project file.";
        return false;
    }
    QJsonParseError parseError;
    const auto document=QJsonDocument::fromJson(file.readAll(),&parseError);
    if (!document.isObject()) {
        if (error) *error=parseError.errorString();
        return false;
    }
    const auto root=document.object();
    if (root.value("format")!="GXWB-Project" || root.value("schema").toInt()!=1) {
        if (error) *error="Unsupported GX Whiteboard project format.";
        return false;
    }
    const QJsonArray items=root.value("scenes").toArray();
    const int current=root.value("currentScene").toInt(-1);
    if (items.isEmpty() || items.size()>MaxScenes ||
        current<0 || current>=items.size()) {
        if (error) *error="Invalid project scene list.";
        return false;
    }
    QVector<Scene> decoded;
    QSet<QString> identifiers;
    for (const auto& row:items) {
        const auto obj=row.toObject();
        const QString id=obj.value("id").toString();
        const QString name=obj.value("title").toString().trimmed();
        const auto canvas=obj.value("canvas").toObject();
        if (id.isEmpty() || identifiers.contains(id) || name.isEmpty() ||
            name.size()>100 || canvas.value("format")!="GXWB-F02" ||
            canvas.value("schema").toInt()!=1 ||
            !canvas.value("strokes").isArray() ||
            !canvas.value("images").isArray()) {
            if (error) *error="Invalid scene metadata or canvas content.";
            return false;
        }
        identifiers.insert(id);
        decoded.append({id,name,canvas});
    }
    // Validate *every* embedded scene, including inactive scenes, before
    // replacing the user's current project. No partial project loads.
    BoardView validator;
    for (const auto& scene:decoded) {
        QString validationError;
        if (!validator.loadDocumentObject(scene.document,&validationError)) {
            if (error) *error="Invalid scene '"+scene.title+"': "+validationError;
            return false;
        }
    }
    if (out) *out=std::move(decoded);
    if (selected) *selected=current;
    if (title) *title=root.value("title").toString("Untitled project").left(100);
    if (uuid) *uuid=root.value("id").toString();
    if (originalPath) *originalPath=root.value("filePath").toString();
    return true;
}
bool ProjectDocument::open(const QString& path,BoardView* board,QString* error) {
    if (!board) return false;
    QVector<Scene> decoded;
    int current=-1;
    QString title,uuid,original;
    if (!readProject(path,&decoded,&current,&title,&uuid,&original,error))
        return false;
    if (!board->loadDocumentObject(decoded[current].document,error)) return false;
    scenes_=std::move(decoded);
    currentIndex_=current;
    board->resetSceneHistories(scenes_[currentIndex_].id);
    title_=title.isEmpty() ? QFileInfo(path).completeBaseName():title;
    projectId_=uuid.isEmpty()?freshId():uuid;
    filename_=QFileInfo(path).absoluteFilePath();
    dirty_=false;
    return true;
}
bool ProjectDocument::recoverBackup(const QString& primaryFilename,
                                    BoardView* board,QString* error) {
    if (!board) return false;
    const QString backup=QFileInfo(primaryFilename).absoluteFilePath()+".bak";
    QVector<Scene> recovered;
    int index=-1;
    QString title,identifier,oldPath;
    if (!readProject(backup,&recovered,&index,&title,&identifier,&oldPath,error))
        return false;
    if (!board->loadDocumentObject(recovered[index].document,error))
        return false;
    scenes_=std::move(recovered);
    currentIndex_=index;
    board->resetSceneHistories(scenes_[index].id);
    title_=title.isEmpty()?"Recovered backup":title+" (Recovered backup)";
    projectId_=identifier.isEmpty()?freshId():identifier;
    filename_.clear();  // Requires Save As. Never overwrite damaged source.
    dirty_=true;
    return true;
}
QString ProjectDocument::recoveryPath() const {
    const QString root=QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)+"/recoveries";
    const QString key=filename_.isEmpty() ?
        "unsaved-"+projectId_ :
        QString::fromLatin1(QCryptographicHash::hash(
            filename_.toUtf8(),QCryptographicHash::Sha256).toHex().left(24));
    return root+"/"+key+".gxwp";
}
bool ProjectDocument::writeRecovery(BoardView* board,QString* error) {
    if (!dirty_) return true;
    if (!capture(board,error)) return false;
    const QString path=recoveryPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error="Cannot create autosave directory.";
        return false;
    }
    return writeAtomic(path,serialize(),error);
}
bool ProjectDocument::restoreRecovery(const QString& fileName,BoardView* board,
                                      QString* error) {
    QVector<Scene> decoded;
    int current=-1;
    QString title,uuid,originalPath;
    if (!readProject(fileName,&decoded,&current,&title,&uuid,&originalPath,error))
        return false;
    if (!board || !board->loadDocumentObject(decoded[current].document,error))
        return false;
    scenes_=std::move(decoded);
    currentIndex_=current;
    board->resetSceneHistories(scenes_[currentIndex_].id);
    title_=title.isEmpty()?"Recovered project":title;
    projectId_=uuid.isEmpty()?freshId():uuid;
    filename_=originalPath;
    dirty_=true;
    return true;
}
bool ProjectDocument::clearRecovery() {
    const QString path=recoveryPath();
    return !QFileInfo::exists(path) || QFile::remove(path);
}
QStringList ProjectDocument::recoveryFiles() {
    QDir dir(QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation)+"/recoveries");
    QStringList result;
    for (const auto& name:dir.entryList({"*.gxwp"},QDir::Files,QDir::Time))
        result.append(dir.absoluteFilePath(name));
    return result;
}
