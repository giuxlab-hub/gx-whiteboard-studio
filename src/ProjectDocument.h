#pragma once
#include "BoardView.h"
#include <QJsonObject>
#include <QVector>
#include <QString>
#include <QStringList>

// The project owns all scene snapshots. A live BoardView represents only
// the currently selected scene; media decoders are not stored in the project.
class ProjectDocument {
public:
    struct Scene {
        QString id;
        QString title;
        QJsonObject document;
    };
    ProjectDocument()=default;
    bool initialize(BoardView* board);
    int count() const {return scenes_.size();}
    int currentIndex() const {return currentIndex_;}
    bool dirty() const {return dirty_;}
    void setDirty(bool value=true) { dirty_=value; }
    QString title() const {return title_;}
    QString filename() const {return filename_;}
    QString id() const {return projectId_;}
    QString sceneTitle(int i) const;
    QStringList names() const;
    bool capture(BoardView* board,QString* error=nullptr);
    bool select(int index,BoardView* board,QString* error=nullptr);
    bool addBlank(BoardView* board,QString* error=nullptr);
    bool duplicate(BoardView* board,QString* error=nullptr);
    bool rename(int index,const QString& name);
    bool remove(int index,BoardView* board,QString* error=nullptr);
    bool newProject(BoardView* board);
    bool save(const QString& filename,BoardView* board,QString* error=nullptr);
    bool open(const QString& filename,BoardView* board,QString* error=nullptr);
    bool recoverBackup(const QString& primaryFilename,BoardView* board,
                       QString* error=nullptr);
    bool writeRecovery(BoardView* board,QString* error=nullptr);
    bool restoreRecovery(const QString& filename,BoardView* board,QString* error=nullptr);
    bool clearRecovery();
    QString recoveryPath() const;
    static QStringList recoveryFiles();
private:
    static constexpr int MaxScenes=24;
    static constexpr qint64 MaxProjectBytes=192LL*1024*1024;
    static bool writeAtomic(const QString& path,const QByteArray& bytes,QString* error);
    static bool readProject(const QString& path,QVector<Scene>* scenes,
                            int* currentIndex,QString* title,QString* uuid,
                            QString* originalPath,QString* error);
    QByteArray serialize() const;
    bool writeBackup(const QString& path,QString* error);
    QString projectId_;
    QString title_="Untitled project";
    QString filename_;
    QVector<Scene> scenes_;
    int currentIndex_=0;
    bool dirty_=false;
};
