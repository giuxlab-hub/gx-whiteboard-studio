#include "StudioWindow.h"
#include "CameraSource.h"
#include "NetworkSource.h"
#include "RecordingEngine.h"
#include <QCheckBox>
#include <QAction>
#include <QToolButton>
#include <QProgressBar>
#include "LanCameraScanner.h"
#include "LanScanDialog.h"
#include "Localization.h"
#include <memory>
#include <QPushButton>
#include <QApplication>
#include <QIcon>
#include <QAction>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QInputDialog>
#include <QMessageBox>
#include <QAbstractButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QDebug>
#include <QLocale>
#include <QScreen>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QSettings>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QLineEdit>
#include <QMenu>
#include <QLineF>
#include <QImage>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QScrollArea>
#include <QScrollBar>
#include <cmath>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <cstdlib>

static QString findCatalogs() {
    const QString exe = QCoreApplication::applicationDirPath();
    const QString paths[] = {
        exe + "/locale",
        exe + "/../share/locale",
        exe + "/../locale",
        "/usr/share/locale"
    };
    for (const auto& dir : paths) {
        if (QFileInfo::exists(dir + "/it/LC_MESSAGES/gx-whiteboard-studio.mo")) return dir;
    }
    return paths[0];
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("GiuxLab");
    app.setApplicationName("GXWhiteboardStudio");
    app.setApplicationVersion("0.1.0");
    const QString iconRoot=QCoreApplication::applicationDirPath();
    QIcon appIcon=QIcon::fromTheme("gx-whiteboard-studio");
    if (appIcon.isNull()) {
        const QStringList iconCandidates={
            iconRoot+"/../share/icons/hicolor/scalable/apps/gx-whiteboard-studio.svg",
            iconRoot+"/../packaging/gx-whiteboard-studio.svg"
        };
        for (const QString& candidate:iconCandidates) {
            if (QFileInfo::exists(candidate)) {
                appIcon=QIcon(candidate);
                break;
            }
        }
    }
    app.setWindowIcon(appIcon);
    QCommandLineParser parser;
    parser.setApplicationDescription("GX Whiteboard Studio — F08 Debian 13 packaging and QA");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("document", "Optional .gxwp project or .gxwb board to open.");
    parser.addOption(QCommandLineOption("lang", "Override language (it/en)", "code"));
    parser.addOption(QCommandLineOption("smoke-cli-open", "F08 startup document and local manual integration test"));
    parser.addOption(QCommandLineOption("smoke", "Run F02 regression checks"));
    parser.addOption(QCommandLineOption("smoke-images", "Run F03 image box smoke checks"));
    parser.addOption(QCommandLineOption("smoke-camera", "F04 simulated camera end-to-end test"));
    parser.addOption(QCommandLineOption("smoke-camera-real", "F04 local USB camera acquisition test (no images saved)"));
    parser.addOption(QCommandLineOption("smoke-zmenu", "F04.3 media Z-order and right-click regression"));
    parser.addOption(QCommandLineOption("smoke-network", "F05 loopback HTTP/MJPEG source test"));
    parser.addOption(QCommandLineOption("smoke-network-reconnect", "F05 HTTP/MJPEG drop/reconnect test"));
    parser.addOption(QCommandLineOption("smoke-network-restore", "F05 restore saved IP URLs without starting the camera"));
    parser.addOption(QCommandLineOption("smoke-scenes", "F07 scenes, project IO, backups and recovery test"));
    parser.addOption(QCommandLineOption("smoke-scene-undo", "F07.2 independent Undo/Redo histories for each scene"));
    parser.addOption(QCommandLineOption("smoke-resilience-av", "F07.3 unexpected FFmpeg failure and MP4 fallback test"));
    parser.addOption(QCommandLineOption("smoke-resilience-project", "F07.3 corrupt project backup restoration and heavy project test"));
    parser.addOption(QCommandLineOption("smoke-resilience-heavy", "F07.3 24-scene project with large embedded PNG assets"));
    parser.addOption(QCommandLineOption("smoke-autosave", "F07 automatic timer checkpoint test"));
    parser.addOption(QCommandLineOption("smoke-scene-record", "F07 scene switch during synthetic video recording test"));
    parser.addOption(QCommandLineOption("smoke-record", "F06 synthetic AV canvas recording"));
    parser.addOption(QCommandLineOption("smoke-record-nvenc", "F06 synthetic AV recording with NVENC"));
    parser.addOption(QCommandLineOption("smoke-record-hd", "F06 1920x1080 30fps NVENC synthetic AV test"));
    parser.addOption(QCommandLineOption("smoke-record-videoonly", "F06 synthetic video only"));
    parser.addOption(QCommandLineOption("smoke-record-ui", "F06 recording dialog and toolbar test, no microphone"));
    parser.addOption(QCommandLineOption("smoke-record-ui-en", "F06 English recording dialog test, no microphone"));
    parser.addOption(QCommandLineOption("smoke-thunar-record", "Test live recording with Thunar URI list and inline image drop"));
    parser.addOption(QCommandLineOption("smoke-lanscan", "F05 LAN scan test, loopback only"));
    parser.addOption(QCommandLineOption("scan-http-port", "Loopback HTTP test port", "port"));
    parser.addOption(QCommandLineOption("scan-rtsp-port", "Loopback RTSP test port", "port"));
    parser.addOption(QCommandLineOption("stream-url", "Loopback test stream address", "url"));
    parser.process(app);
    const QString chosen = parser.value("lang");
    StudioWindow w(findCatalogs(), chosen);
    w.show();
    const QStringList documents=parser.positionalArguments();
    if (documents.size()==1) {
        QString openError;
        const bool openSuccess=w.openInitialDocument(documents.at(0),&openError);
        if (!openSuccess) {
            qWarning().noquote() << "Desktop document open failed:" << openError;
            // A normal desktop double-click must report errors, while the
            // command-line regression remains noninteractive.
            if (!QCoreApplication::arguments().join(" ").contains("--smoke"))
                QTimer::singleShot(0,&w,[&w,openError] {
                    QMessageBox::warning(&w,"GX Whiteboard Studio",openError);
                });
        }
    }
    if (!parser.isSet("smoke")) {
        QTimer::singleShot(0, &w, [&w]() { w.applyStartupMode(); });
    }
    if (parser.isSet("smoke-cli-open")) {
        QTimer::singleShot(350,&app,[&app,&w,documents] {
            bool good=documents.size()==1 && w.sceneCountForTest()>=1;
            if (!documents.isEmpty()) {
                const QString ext=QFileInfo(documents.first()).suffix().toLower();
                if (ext=="gxwp") good &= w.sceneCountForTest()>=2;
                if (ext=="gxwb") good &= w.sceneCountForTest()==1;
            }
            auto* manual=w.findChild<QAction*>("manualAction");
            good &= manual!=nullptr;
            const QString bin=QCoreApplication::applicationDirPath();
            QStringList paths={
                bin+"/../share/doc/gx-whiteboard-studio/manual_it.html",
                bin+"/../share/doc/gx-whiteboard-studio/manual_en.html"
            };
            // The two manuals are installed together on Debian; source-build
            // smoke checks resolve them in the F08/docs directory instead.
            if (!QFileInfo::exists(paths.first())) {
                paths={bin+"/../docs/manual_it.html",bin+"/../docs/manual_en.html"};
            }
            for (const auto& path:paths) good &= QFileInfo(path).isFile();
            if (manual) {
                w.setLanguage("it_IT",false);
                good &= manual->text()=="Manuale utente";
                w.setLanguage("en_US",false);
                good &= manual->text()=="User manual";
            }
            qInfo().noquote() << "F08 DESKTOP FILE OPEN:"
                << (good?"PASS":"FAIL")
                << "file" << (documents.isEmpty()?QString():documents.first())
                << "scenes" << w.sceneCountForTest()
                << "manuals" << paths;
            app.exit(good?0:2);
        });
    }









    if (parser.isSet("smoke-autosave")) {
        QTimer::singleShot(300,&app,[&app,&w] {
            auto* timer=w.findChild<QTimer*>("recoveryTimer");
            auto* board=w.boardForTest();
            bool passed=timer && timer->isActive() &&
                        timer->interval()==60000;
            if (timer) timer->setInterval(180); // only in disposable test
            board->addSyntheticStroke(QPointF(170,200),QPointF(450,570),0.7);
            QTimer::singleShot(850,&app,[&app,&w,passed] {
                const QString recovery=w.recoveryPathForTest();
                QFile file(recovery);
                bool verified=passed && file.open(QIODevice::ReadOnly);
                if (verified) {
                    const auto root=QJsonDocument::fromJson(file.readAll()).object();
                    const auto scenes=root.value("scenes").toArray();
                    verified &= root.value("format")=="GXWB-Project" && scenes.size()==1;
                    if (!scenes.isEmpty())
                        verified &= scenes.first().toObject()
                            .value("canvas").toObject()
                            .value("strokes").toArray().size()==1;
                }
                qInfo().noquote() << "F07 AUTOSAVE TIMER:"
                    << (verified?"PASS":"FAIL")
                    << "| 60 sec default, forced 180 ms in test,"
                    << " atomic recovery checkpoint" << recovery;
                app.exit(verified?0:2);
            });
        });
    }
    if (parser.isSet("smoke-scene-record")) {
        QTimer::singleShot(300,&app,[&app,&w] {
            const QString output=QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath("../results");
            const QString file=output+"/f07_scenes_recorded.mkv";
            QFile::remove(file);
            auto* board=w.boardForTest();
            board->setStrokeColor(QColor("#ce2828"));
            board->setStrokeWidth(24);
            board->addSyntheticStroke(QPointF(200,180),QPointF(1730,900),0.9);
            bool passed=w.addSceneForTest();
            QString err;
            passed &= board->insertImageFromFile(
                output+"/../assets/f03_test_chart.png",
                QRectF(250,230,1250,650),&err);
            passed &= w.selectSceneForTest(0);
            auto* engine=w.recordingForTest();
            RecordingEngine::Options opts;
            opts.output=file;
            opts.size=QSize(640,360);
            opts.fps=15;
            opts.syntheticAudio=true; // never access microphone
            opts.encoder="libx264";
            const bool started=engine->start(opts,&err);
            passed &= started;
            const auto okay=std::make_shared<bool>(passed);
            QTimer::singleShot(1100,&app,[&w,okay] {
                *okay &= w.selectSceneForTest(1);
                *okay &= w.activeSceneForTest()==1 &&
                         w.boardForTest()->imageCount()==1;
            });
            QTimer::singleShot(2400,&app,[&w,okay] {
                *okay &= w.selectSceneForTest(0);
                *okay &= w.boardForTest()->strokeCount()==1;
            });
            engine->completed=[&app,engine,file,okay](bool success,const QString& msg) {
                const bool final=*okay && success &&
                    engine->frameCount()>40 &&
                    QFileInfo(file).size()>10000 &&
                    engine->maxLevelPercent()>0;
                qInfo().noquote()<<"F07 SCENES DURING REC:"
                    <<(final?"PASS":"FAIL")
                    <<"| transitions in current canvas, AAC synthetic,"
                    <<"frames" << engine->frameCount()
                    <<"dropped" << engine->droppedFrames()
                    <<msg;
                app.exit(final?0:2);
            };
            QTimer::singleShot(3700,&app,[engine]{engine->stop();});
            QTimer::singleShot(15000,&app,[&app,engine] {
                if (engine->active() || engine->finishing()) {
                    engine->stop();
                    app.exit(3);
                }
            });
        });
    }



    if (parser.isSet("smoke-resilience-av")) {
        QTimer::singleShot(300,&app,[&app,&w] {
            const QString output=QDir(QCoreApplication::applicationDirPath())
                     .absoluteFilePath("../results");
            bool ok=true;
            auto check=[&](bool test,const QString& label) {
                if (!test) qWarning().noquote() << "F07.3 AV FAIL:" << label;
                ok &= test;
            };
            check(RecordingEngine::diskSpaceStatus(2LL*1024*1024*1024)==0,
                  "two GB available is healthy");
            check(RecordingEngine::diskSpaceStatus(180LL*1024*1024)==1,
                  "low disk triggers warning");
            check(RecordingEngine::diskSpaceStatus(15LL*1024*1024)==2,
                  "critical disk triggers graceful stop");
            const QString failed=output+"/f07_3_encoder_crash.mkv";
            QFile::remove(failed);
            QFile::remove(failed+".recording-report.json");
            auto* engine=new RecordingEngine(&app);
            engine->renderFrame=w.recordingForTest()->renderFrame;
            RecordingEngine::Options opts;
            opts.output=failed;
            opts.size=QSize(640,360);
            opts.fps=15;
            opts.syntheticAudio=true;
            opts.encoder="libx264";
            auto checks=std::make_shared<bool>(ok);
            int* warnings=new int(0);
            engine->warning=[warnings](const QString& message) {
                ++(*warnings);
                qInfo().noquote() << "F07.3 RECORD WARNING:" << message;
            };
            engine->completed=[&app,engine,checks,warnings,failed,output]
                (bool success,const QString& message) {
                Q_UNUSED(message);
                *checks &= !success && engine->abnormalTermination();
                *checks &= *warnings>=1;
                QFile diagnostics(failed+".recording-report.json");
                *checks &= diagnostics.open(QIODevice::ReadOnly);
                if (diagnostics.isOpen()) {
                    const QJsonObject report=QJsonDocument::fromJson(
                        diagnostics.readAll()).object();
                    *checks &= report.value("format")=="GXWB-Recording-Diagnostics";
                    *checks &= report.value("status")=="ffmpeg_failed";
                    *checks &= report.value("unexpectedTermination").toBool();
                    *checks &= report.value("syntheticAudio").toBool();
                    *checks &= report.value("recentFFmpegLog").isArray();
                    *checks &= report.value("warnings").toArray().size()>0;
                    const QString asText=QString::fromUtf8(
                        QJsonDocument(report).toJson());
                    *checks &= !asText.contains("password=") &&
                               !asText.contains("token=");
                    qInfo() << "F07.3 FFmpeg failure report" << report.value("status")
                            << "frames" << report.value("frameCount")
                            << "log lines" << report.value("recentFFmpegLog").toArray().size();
                }
                delete warnings;
                engine->deleteLater();
                if (!*checks) {
                    qWarning() << "F07.3 AV test failed in unexpected encoder exit";
                    app.exit(2);return;
                }
                const QString successFile=output+"/f07_3_remux_failure.mkv";
                const QString blockedMp4=output+"/f07_3_remux_failure.mp4";
                QFile::remove(successFile);
                QFile::remove(successFile+".recording-report.json");
                QFile::remove(blockedMp4);
                // The destination MP4 already exists; FFmpeg -n must preserve
                // it and leave MKV usable while signaling partial success.
                QFile existing(blockedMp4);
                *checks &= existing.open(QIODevice::WriteOnly);
                if (existing.isOpen()) {
                    existing.write("existing user video - must not overwrite");
                    existing.close();
                }
                auto* second=new RecordingEngine(&app);
                second->renderFrame=engine->renderFrame;
                RecordingEngine::Options settings;
                settings.output=successFile;
                settings.size=QSize(640,360);
                settings.fps=15;
                settings.syntheticAudio=true;
                settings.convertToMp4=true;
                settings.encoder="libx264";
                second->completed=[&app,second,checks,successFile,blockedMp4]
                  (bool success,const QString& msg) {
                    Q_UNUSED(msg);
                    *checks &= success && !second->finishing();
                    *checks &= QFileInfo(successFile).size()>10000;
                    *checks &= QFileInfo(blockedMp4).size()==40;
                    QFile diag(successFile+".recording-report.json");
                    *checks &= diag.open(QIODevice::ReadOnly);
                    if (diag.isOpen()) {
                        const auto state=QJsonDocument::fromJson(
                              diag.readAll()).object();
                        *checks &= state.value("status")=="mp4_failed";
                        *checks &= state.value("warnings").toArray().size()>0;
                    }
                    qInfo().noquote() << "F07.3 RECORDER RESILIENCE:"
                        << (*checks?"PASS":"FAIL")
                        << "| unexpected encoder death, diagnostic report,"
                        << " protected old MP4, valid MKV, explicit partial success";
                    second->deleteLater();
                    app.exit(*checks?0:2);
                };
                QString error;
                if (!second->start(settings,&error)) {
                    qWarning().noquote() << "F07.3 remux setup failed" << error;
                    app.exit(2);
                    return;
                }
                QTimer::singleShot(1550,&app,[second] {second->stop();});
            };
            QString error;
            if (!engine->start(opts,&error)) {
                qWarning().noquote()<<"F07.3 encoder fail setup:"<<error;
                delete warnings;
                engine->deleteLater();
                app.exit(2);return;
            }
            QTimer::singleShot(1150,&app,[engine] {engine->simulateEncoderCrashForTest();});
            QTimer::singleShot(13000,&app,[&app] {app.exit(3);});
        });
    }
    if (parser.isSet("smoke-resilience-project") ||
        parser.isSet("smoke-resilience-heavy")) {
        const bool heavy=parser.isSet("smoke-resilience-heavy");
        QTimer::singleShot(300,&app,[&app,&w,heavy] {
            const QString root=QDir(QCoreApplication::applicationDirPath())
                                .absoluteFilePath("../results");
            bool ok=true;
            auto verify=[&](bool condition,const QString& label) {
                if (!condition) qWarning().noquote() << "F07.3 PROJECT FAIL:" << label;
                ok &= condition;
            };
            auto* board=w.boardForTest();
            const QString primary=root+"/f07_3_damaged_primary.gxwp";
            const QString recovered=root+"/f07_3_restored_from_backup.gxwp";
            QFile::remove(primary);QFile::remove(primary+".bak");
            QFile::remove(recovered);
            board->addSyntheticStroke(QPointF(200,100),QPointF(800,430),0.7);
            verify(w.addSceneForTest(),"add scene two");
            board->addSyntheticStroke(QPointF(500,640),QPointF(1000,950),0.6);
            verify(w.saveProjectForTest(primary),"first project save");
            board->addSyntheticStroke(QPointF(200,500),QPointF(1400,700),0.8);
            verify(w.saveProjectForTest(primary),"second project save creates backup");
            QFile backup(primary+".bak");
            verify(backup.open(QIODevice::ReadOnly),"read known-good backup");
            const QByteArray originalBackup=backup.readAll();
            backup.close();
            const QByteArray digest=QCryptographicHash::hash(originalBackup,
                QCryptographicHash::Sha256);
            QFile damage(primary);
            verify(damage.open(QIODevice::WriteOnly|QIODevice::Truncate),
                   "simulate damaged primary");
            damage.write("{INCOMPLETE PROJECT");
            damage.close();
            verify(!w.openProjectForTest(primary),
                   "invalid primary rejected without changing current scene");
            verify(!w.saveProjectForTest(primary),
                   "cannot overwrite damaged primary or its backup");
            QFile checkBackup(primary+".bak");
            verify(checkBackup.open(QIODevice::ReadOnly),"backup still exists");
            verify(QCryptographicHash::hash(checkBackup.readAll(),
                    QCryptographicHash::Sha256)==digest,
                   "failed save does not damage good backup");
            verify(w.recoverBackupForTest(primary),
                   "explicit safe backup recovery");
            verify(w.sceneCountForTest()==2 && w.activeSceneForTest()==1 &&
                   board->strokeCount()==1,
                   "recovered old version with both scenes");
            verify(w.saveProjectForTest(recovered),
                   "save recovered copy under a new filename");
            verify(QFileInfo(primary).size()<100 &&
                   QFileInfo(recovered).size()>500,
                   "damaged original retained and recovered project is separate");
            QFile backupAfter(primary+".bak");
            verify(backupAfter.open(QIODevice::ReadOnly),"backup still accessible");
            verify(QCryptographicHash::hash(backupAfter.readAll(),
                    QCryptographicHash::Sha256)==digest,
                   "original backup remains unchanged");
            // Stress the maximum 24 scenes with repeated embedded images.
            auto timer=QElapsedTimer();
            timer.start();
            verify(w.openProjectForTest(recovered),"open recovered baseline");
            while (w.sceneCountForTest()<24) {
                verify(w.addSceneForTest(),"add stress-test scene");
                board->addSyntheticStroke(QPointF(130,210),
                    QPointF(1600,770),0.8);
                const QString image=QDir(
                    QCoreApplication::applicationDirPath()).absoluteFilePath(
                        heavy?"../assets/f07_3_noise_stress.png":
                              "../assets/f03_test_chart.png");
                QString error;
                verify(board->insertImageFromFile(image,
                    QRectF(230,340,900,520),&error),
                    "embed image in stress-test scene");
                if (!ok) break;
            }
            verify(w.sceneCountForTest()==24,"maximum 24 scenes");
            const QString big=root+
                (heavy?"/f07_3_24scene_heavy.gxwp":
                       "/f07_3_24scene_stress.gxwp");
            QFile::remove(big);
            const qint64 prepMs=timer.elapsed();
            timer.restart();
            verify(w.saveProjectForTest(big),"save maximum-scene project");
            const qint64 saveMs=timer.elapsed();
            qint64 backupMs=0;
            if (heavy) {
                // Re-save the real 49 MB project. Before replacing its .bak,
                // F07.3 must validate all 24 embedded scenes and then copy
                // the old project atomically, with no loss of user data.
                board->addSyntheticStroke(QPointF(300,200),
                    QPointF(1250,740),0.6);
                timer.restart();
                verify(w.saveProjectForTest(big),
                       "heavy second save with strict old file validation");
                backupMs=timer.elapsed();
                verify(QFileInfo(big+".bak").size()>20LL*1024*1024,
                       "heavy previous version backed up intact");
            }
            timer.restart();
            verify(w.openProjectForTest(big),"load maximum-scene project");
            const qint64 loadMs=timer.elapsed();
            verify(w.sceneCountForTest()==24,
                   "24 scenes preserved after reload");
            if (heavy)
                verify(QFileInfo(big).size()>20LL*1024*1024,
                       "heavy project stores more than 20 MB of embedded assets");
            verify(w.selectSceneForTest(23) && board->imageCount()==1,
                   "last heavy scene image embedded");
            verify(w.selectSceneForTest(1) && board->strokeCount()==1,
                   "earlier scene content unaffected");
            qInfo().noquote()
                <<(heavy?"F07.3 HEAVY PROJECT:":"F07.3 PROJECT RESILIENCE:")
                <<(ok?"PASS":"FAIL")
                <<"| damaged main, protected .bak, explicit restore,"
                <<" 24 scenes, prepare/save/backup-save/load ms"
                <<prepMs<<saveMs<<backupMs<<loadMs
                <<"bytes"<<QFileInfo(big).size();
            app.exit(ok?0:2);
        });
    }

    if (parser.isSet("smoke-scene-undo")) {
        QTimer::singleShot(250,&app,[&app,&w] {
            bool good=true;
            auto verify=[&](bool result,const QString& label) {
                if (!result) qWarning().noquote() << "F07.2 UNDO FAIL:" << label;
                good &= result;
            };
            auto* board=w.boardForTest();
            auto* undoAction=w.findChild<QAction*>("editAction_0");
            auto* redoAction=w.findChild<QAction*>("editAction_1");
            verify(undoAction && redoAction,"dynamic Edit undo/redo actions present");
            verify(w.sceneCountForTest()==1 && board->strokeCount()==0,
                   "one empty scene at start");
            board->addSyntheticStroke(QPointF(120,140),QPointF(700,400),0.8);
            QUndoStack* stack1=board->undoStack();
            verify(stack1->canUndo() && board->strokeCount()==1,"first scene has undo history");
            verify(w.addSceneForTest(),"add second scene");
            verify(!board->undoStack()->canUndo(),"second new scene starts clean");
            verify(board->undoStack()!=stack1,"scene 2 owns its own QUndoStack");
            board->addSyntheticStroke(QPointF(800,400),QPointF(1200,650),0.7);
            QUndoStack* stack2=board->undoStack();
            board->insertTextAt(QPointF(920,650),"Scene two");
            verify(board->strokeCount()==2 && stack2->count()==2,
                   "scene two has two commands");
            verify(w.selectSceneForTest(0),"select first scene");
            verify(board->undoStack()==stack1 && board->strokeCount()==1 &&
                   board->undoStack()->canUndo(),
                   "first scene restores previous QUndoStack");
            if (undoAction) undoAction->trigger();
            verify(board->strokeCount()==0 &&
                   stack1->canRedo() && !stack1->canUndo(),
                   "Edit > Undo acts on first scene only");
            if (redoAction) redoAction->trigger();
            verify(board->strokeCount()==1 &&
                   stack1->canUndo(),"Edit > Redo restores first scene");
            verify(w.selectSceneForTest(1),"select second scene");
            verify(board->undoStack()==stack2 &&
                   board->strokeCount()==2 && stack2->canUndo(),
                   "second scene restored with full undo history");
            board->undoStack()->undo();
            verify(board->strokeCount()==1 &&
                   stack2->canRedo(),"second scene Undo removed its text");
            verify(w.selectSceneForTest(0),"return to first scene");
            verify(board->strokeCount()==1 &&
                   board->undoStack()==stack1 && stack1->canUndo(),
                   "scene one's history is unaffected by scene two Undo");
            verify(w.selectSceneForTest(1),"return to second scene");
            verify(board->strokeCount()==1 && stack2->canRedo(),
                   "redo branch retained across multiple scene switches");
            board->undoStack()->redo();
            verify(board->strokeCount()==2,"second-scene Redo succeeds");
            verify(w.duplicateSceneForTest(),"duplicate scene two");
            QUndoStack* copyStack=board->undoStack();
            verify(copyStack!=stack2 && copyStack->count()==0 &&
                   board->strokeCount()==2,
                   "duplicated scene has independent clean undo history");
            board->addSyntheticStroke(QPointF(100,500),QPointF(1600,920),0.65);
            verify(copyStack->canUndo() && board->strokeCount()==3,
                   "duplicated scene records its own changes");
            verify(w.selectSceneForTest(1),"return original second");
            verify(board->strokeCount()==2 && board->undoStack()==stack2 &&
                   stack2->count()==2,
                   "original scene history unaffected by duplicate");
            verify(w.selectSceneForTest(2),"return duplicate");
            verify(board->strokeCount()==3 && board->undoStack()==copyStack,
                   "duplicate's changes and history persist");

            // Project IO intentionally does NOT serialize QUndoCommand objects;
            // after reopening, scene content survives, history begins clean.
            const QString root=QDir(QCoreApplication::applicationDirPath())
                                .absoluteFilePath("../results");
            const QString filename=root+"/f07_2_undo_project.gxwp";
            QFile::remove(filename);
            verify(w.saveProjectForTest(filename),"save three-scene project");
            verify(w.openProjectForTest(filename),"reopen all scenes from project");
            verify(board->strokeCount()==3 && !board->undoStack()->canUndo(),
                   "reloaded scene contents intact; undo history intentionally resets");
            verify(w.selectSceneForTest(0) &&
                   board->strokeCount()==1 && !board->undoStack()->canUndo(),
                   "other saved scenes also reload without stale undo commands");
            qInfo().noquote() << "F07.2 PER-SCENE UNDO:"
                << (good?"PASS":"FAIL")
                << "| independent history, native Edit actions, undo/redo across"
                << " switches, duplicate isolation, file reopen resets commands";
            app.exit(good?0:2);
        });
    }

    if (parser.isSet("smoke-scenes")) {
        QTimer::singleShot(300,&app,[&app,&w] {
            const QString root=QDir(QCoreApplication::applicationDirPath())
                    .absoluteFilePath("..");
            const QString results=root+"/results";
            const QString projectPath=results+"/f07_test_project.gxwp";
            QFile::remove(projectPath);
            QFile::remove(projectPath+".bak");
            auto* board=w.boardForTest();
            w.setLanguage("it_IT",false);
            bool okay=true;
            auto verify=[&](bool result,const QString& step) {
                if (!result) qWarning().noquote()<<"F07 SCENES FAIL:"<<step;
                okay &= result;
            };
            verify(w.sceneCountForTest()==1 && w.activeSceneForTest()==0,
                   "initial project contains one scene");
            auto* list=w.findChild<QListWidget*>("sceneList");
            auto* scenes=w.findChild<QWidget*>("scenesPanel");
            auto* projects=w.findChild<QWidget*>("projectsPanel");
            verify(list && scenes && projects,"side panels exist");
            auto* saveAction=w.findChild<QAction*>("saveProjectAction");
            auto* openAction=w.findChild<QAction*>("openProjectAction");
            verify(saveAction && openAction &&
                   saveAction->shortcut()==QKeySequence("Ctrl+S") &&
                   openAction->shortcut()==QKeySequence("Ctrl+O"),
                   "project shortcuts are active");
            board->setBoardBackground(BoardView::Background::Grid);
            board->addSyntheticStroke(QPointF(100,150),QPointF(600,420),0.65);
            QString error;
            const QString temporaryImage=results+"/f07_source_to_remove.png";
            QFile::remove(temporaryImage);
            verify(QFile::copy(root+"/assets/f03_test_chart.png",temporaryImage),
                   "copy test image to removable source path");
            verify(board->insertImageFromFile(temporaryImage,
                   QRectF(800,210,500,320),&error),"first scene image: "+error);
            verify(board->strokeCount()==1 && board->imageCount()==1,
                   "first scene has ink and image");
            const auto firstSceneDocument=board->documentObject();
            verify(!firstSceneDocument.isEmpty(),"first scene snapshot");
            verify(w.addSceneForTest(),"add second blank scene");
            verify(w.sceneCountForTest()==2 && w.activeSceneForTest()==1,
                   "now on blank Scene 2");
            verify(board->strokeCount()==0 && board->imageCount()==0,
                   "new scene has no content");
            verify(board->boardBackground()==BoardView::Background::Grid,
                   "new blank scene inherits background");
            board->addSyntheticStroke(QPointF(1300,510),QPointF(1500,810),0.8);
            verify(board->insertCameraBox("test-pattern",QRectF(500,300,650,360)),
                   "second scene webcam snapshot");
            verify(board->strokeCount()==1 && board->cameraBoxCount()==1,
                   "second scene has distinct content");
            verify(w.duplicateSceneForTest(),"duplicate second scene");
            verify(w.sceneCountForTest()==3 && w.activeSceneForTest()==2,
                   "third scene is duplicated second");
            verify(board->strokeCount()==1 && board->cameraBoxCount()==1,
                   "duplicate preserves ink/camera snapshot");
            board->insertTextAt(QPointF(520,850),"Only duplicated scene");
            verify(board->strokeCount()==2,"duplicate uniquely editable");
            verify(w.selectSceneForTest(0),"switch to first scene");
            verify(board->strokeCount()==1 && board->imageCount()==1 &&
                   board->cameraBoxCount()==0,"first scene restored exactly");
            verify(board->documentObject().value("strokes").toArray().size()==1,
                   "original independent from duplicate");
            verify(w.selectSceneForTest(1),"switch to original second scene");
            verify(board->strokeCount()==1 && board->cameraBoxCount()==1,
                   "second remains independent");
            verify(w.selectSceneForTest(2),"switch to duplicated scene");
            verify(board->strokeCount()==2,"duplicated scene kept extra text");
            auto sectionButtons=w.findChildren<QPushButton*>("sideNav");
            verify(sectionButtons.size()==4,"four sidebar section navigation buttons");
            if (sectionButtons.size()==4 && list && scenes && projects) {
                sectionButtons[2]->click();
                QApplication::processEvents();
                verify(scenes->isVisible() && !projects->isVisible(),
                       "Scenes sidebar visible");
                verify(list->currentRow()==2 && list->count()==3,
                       "active scene selected in list");
                verify(w.grab().save(results+"/f07_scenes_italian.png","PNG"),
                       "screenshot of actual Scenes UI");
                list->setCurrentRow(0);
                QApplication::processEvents();
                verify(w.activeSceneForTest()==0 && board->imageCount()==1,
                       "click scene-list row switches to first scene");
                list->setCurrentRow(2);
                QApplication::processEvents();
                verify(w.activeSceneForTest()==2 && board->strokeCount()==2,
                       "click scene-list row restores duplicated scene");
                sectionButtons[3]->click();
                QApplication::processEvents();
                verify(projects->isVisible() && !scenes->isVisible(),
                       "Projects sidebar visible");
                verify(w.grab().save(results+"/f07_projects_italian.png","PNG"),
                       "screenshot of Projects UI");
            }
            verify(w.saveProjectForTest(projectPath),"save three-scene project");
            const auto saved=QFileInfo(projectPath);
            verify(saved.size()>5000,"project file contains embedded media");
            auto readProject=[&](const QString& name) -> QJsonObject {
                QFile file(name);
                if (!file.open(QIODevice::ReadOnly)) return {};
                return QJsonDocument::fromJson(file.readAll()).object();
            };
            const auto projectObj=readProject(projectPath);
            verify(projectObj.value("format")=="GXWB-Project" &&
                   projectObj.value("schema").toInt()==1 &&
                   projectObj.value("currentScene").toInt()==2,
                   "project format and current scene persisted");
            verify(projectObj.value("scenes").toArray().size()==3,
                   "project embeds all scenes");
            verify(!projectObj.value("scenes").toArray().at(0)
                       .toObject().value("canvas").toObject()
                       .value("images").toArray().isEmpty(),
                   "image embedded in project");
            verify(!projectObj.value("scenes").toArray().at(1)
                       .toObject().value("canvas").toObject()
                       .value("images").toArray().isEmpty(),
                   "webcam snapshot embedded in project");
            board->insertTextAt(QPointF(900,900),"After first save");
            verify(w.saveProjectForTest(projectPath),"second save creates backup");
            verify(QFileInfo(projectPath+".bak").size()>2000,
                   "atomic project backup available");
            verify(readProject(projectPath+".bak")
                   .value("scenes").toArray().at(2).toObject()
                   .value("canvas").toObject().value("strokes").toArray().size()==2,
                   "backup retains previous known-good state");
            verify(readProject(projectPath)
                   .value("scenes").toArray().at(2).toObject()
                   .value("canvas").toObject().value("strokes").toArray().size()==3,
                   "new saved version includes last stroke");
            verify(w.selectSceneForTest(0),"return first before open");
            verify(QFile::remove(temporaryImage),
                   "simulate missing source image after saving project");
            verify(w.openProjectForTest(projectPath),"reopen saved GXWP");
            verify(w.sceneCountForTest()==3 && w.activeSceneForTest()==2 &&
                   board->strokeCount()==3 && board->cameraBoxCount()==1,
                   "reload all scenes/current selection/camera");
            verify(w.selectSceneForTest(0),"scene 0 after reload");
            verify(board->imageCount()==1 && board->strokeCount()==1,
                   "embedded image is restored even when the source file is gone");
            verify(w.cameraForTest("test-pattern")==nullptr,
                   "loading a project never auto-starts a camera");
            verify(w.selectSceneForTest(2),"scene 2 again");
            auto* projectButton=w.findChild<QPushButton*>("projectSaveButton");
            verify(projectButton && projectButton->text()=="Salva progetto",
                   "Italian project button translated");
            // Autosave must not overwrite the user-selected project path.
            const auto committed=readProject(projectPath);
            board->insertTextAt(QPointF(200,960),"Unsaved recovery edit");
            verify(w.autosaveForTest(),"autosave modified project");
            const QString recovery=w.recoveryPathForTest();
            verify(QFileInfo(recovery).size()>3000,
                   "recovery checkpoint created under XDG data directory");
            verify(readProject(projectPath)==committed,
                   "autosave never overwrites main project");
            verify(readProject(recovery).value("scenes").toArray().size()==3,
                   "recovery includes all scenes");
            // Full recovery, including camera snapshot, uses the stored
            // project path and marks it dirty rather than silently saving it.
            verify(w.openProjectForTest(projectPath),"discard in-memory test edits");
            verify(w.recoverForTest(recovery),"recover autosave snapshot");
            verify(board->strokeCount()==4,"recovered latest ink stroke");
            verify(board->cameraBoxCount()==1,"recovered webcam snapshot");
            // Fail atomically on malformed or invalid inactive scenes.
            QJsonObject broken=committed;
            auto badScenes=broken.value("scenes").toArray();
            auto badSecond=badScenes.at(1).toObject();
            auto canvas=badSecond.value("canvas").toObject();
            canvas.insert("background",57);
            badSecond.insert("canvas",canvas);
            badScenes.replace(1,badSecond);
            broken.insert("scenes",badScenes);
            const QString corrupted=results+"/f07_corrupt_scene.gxwp";
            QFile malformed(corrupted);
            verify(malformed.open(QIODevice::WriteOnly|QIODevice::Truncate),
                   "open malformed project path");
            malformed.write(QJsonDocument(broken).toJson());
            malformed.close();
            const int inkBefore=board->strokeCount();
            verify(!w.openProjectForTest(corrupted),"corrupt inactive scene rejected");
            verify(board->strokeCount()==inkBefore &&
                   w.sceneCountForTest()==3,"failed open leaves current document intact");
            w.setLanguage("en_US",false);
            verify(projectButton && projectButton->text()=="Save project",
                   "English project control translated");
            if (sectionButtons.size()==4) {
                sectionButtons[2]->click();
                QApplication::processEvents();
                verify(w.grab().save(results+"/f07_scenes_english.png","PNG"),
                       "English Scenes UI screenshot");
            }
            w.setLanguage("it_IT",false);
            if (sectionButtons.size()==4 && list) {
                sectionButtons[2]->click();
                QApplication::processEvents();
                auto* renameButton=w.findChild<QPushButton*>("sceneRenameButton");
                auto* addButton=w.findChild<QPushButton*>("sceneAddButton");
                auto* deleteButton=w.findChild<QPushButton*>("sceneDeleteButton");
                verify(renameButton && addButton && deleteButton,
                       "scene action buttons exist");
                if (renameButton) {
                    QTimer::singleShot(0,&app,[&verify] {
                        auto* edit=qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                        verify(edit!=nullptr,"rename dialog opens");
                        if (edit) {edit->setTextValue("Risultati");edit->accept();}
                    });
                    renameButton->click();
                    verify(list->item(w.activeSceneForTest())->text()=="Risultati",
                           "renamed scene title shown immediately");
                }
                if (addButton) {
                    addButton->click();
                    verify(w.sceneCountForTest()==4 && board->strokeCount()==0,
                           "new-scene button creates empty scene");
                }
                // Remove the first scene while another scene is active.
                // This exercises the index-shift case for scene deletion.
                list->setCurrentRow(0);
                QApplication::processEvents();
                verify(w.activeSceneForTest()==0 && board->imageCount()==1,
                       "selected first image scene for deletion");
                if (deleteButton) {
                    QTimer::singleShot(0,&app,[&verify] {
                        auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                        verify(box!=nullptr,"delete confirmation dialog");
                        if (box && box->button(QMessageBox::Yes))
                            box->button(QMessageBox::Yes)->click();
                    });
                    deleteButton->click();
                    qInfo()<<"F07 delete first diagnostic"
                           <<"scenes" << w.sceneCountForTest()
                           <<"active" << w.activeSceneForTest()
                           <<"images" << board->imageCount()
                           <<"camera" << board->cameraBoxCount()
                           <<"ink" << board->strokeCount()
                           <<"list row" << list->currentRow();
                    verify(w.sceneCountForTest()==3 && w.activeSceneForTest()==0 &&
                           board->cameraBoxCount()==1,
                           "delete-first correctly loads former second scene");
                }
                // A new project always requests a decision about unsaved work.
                sectionButtons[3]->click();
                QApplication::processEvents();
                auto* newButton=w.findChild<QPushButton*>("projectNewButton");
                if (newButton) {
                    QTimer::singleShot(0,&app,[&verify] {
                        auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                        verify(box!=nullptr,"unsaved project confirmation");
                        if (box && box->button(QMessageBox::Discard))
                            box->button(QMessageBox::Discard)->click();
                    });
                    newButton->click();
                    verify(w.sceneCountForTest()==1 &&
                           board->strokeCount()==0 && board->imageCount()==0,
                           "new-project button resets to one blank scene");
                }
            }
            qInfo().noquote()<<"F07 SCENES PROJECTS:"
                <<(okay?"PASS":"FAIL")
                <<"| three scenes, duplicate, switch, images/camera,"
                <<" GXWP atomic save/backup/reload, autosave recovery,"
                <<" corrupted inactive scene rejection, IT/EN UI";
            app.exit(okay?0:2);
        });
    }

    if (parser.isSet("smoke-thunar-record")) {
        QTimer::singleShot(300,&app,[&app,&w] {
            const QString root=QDir(QCoreApplication::applicationDirPath())
                    .absoluteFilePath("..");
            const QString output=root+"/results";
            const QString file=output+"/f06_thunar_drop_recording.mkv";
            QFile::remove(file);
            auto ok=std::make_shared<bool>(true);
            auto check=[ok](bool value,const QString& label) {
                if (!value) qWarning().noquote() << "F06 THUNAR FAIL:" << label;
                *ok &= value;
            };
            auto* board=w.boardForTest();
            board->clearBoard();
            board->setBoardBackground(BoardView::Background::White);
            board->fitBoard();
            const QString source1=root+"/assets/f03_test_chart.png";
            const QString source2=root+"/assets/f03_test_microscope.png";
            QFile original(source1);
            check(original.open(QIODevice::ReadOnly),"can read source test image");
            const QByteArray beforeDigest=QCryptographicHash::hash(
                original.readAll(),QCryptographicHash::Sha256);
            original.close();
            const QImage before=board->renderViewImage(QSize(640,360));
            check(before.save(output+"/f06_thunar_before.png","PNG"),
                  "image before drag/drop");
            // Record using the SAME engine as the REC button, so the
            // user-visible REC overlay and stop logic are tested too.
            auto* engine=w.recordingForTest();
            check(engine!=nullptr,"window recording engine available");
            RecordingEngine::Options opts;
            opts.output=file;
            opts.size=QSize(640,360);
            opts.fps=15;
            opts.syntheticAudio=true;
            opts.encoder="libx264";
            QString error;
            check(engine->start(opts,&error),"start synthetic AV recording: "+error);
            check(w.recordingActive(),"main REC control is active");
            const auto drop=[board,check](QMimeData& mime,const QPointF& scene,bool accepted) {
                const QPoint local=board->mapFromScene(scene);
                QDragEnterEvent enter(local,Qt::CopyAction,&mime,
                                      Qt::LeftButton,Qt::NoModifier);
                QCoreApplication::sendEvent(board->viewport(),&enter);
                check(enter.isAccepted()==accepted,
                      "drag enter acceptance matches expectation");
                if (accepted) {
                    check(enter.dropAction()==Qt::CopyAction,
                          "drag enter explicitly copies source file");
                    QDragMoveEvent move(local,Qt::CopyAction,&mime,
                                        Qt::LeftButton,Qt::NoModifier);
                    QCoreApplication::sendEvent(board->viewport(),&move);
                    check(move.isAccepted(),"drag move accepted");
                }
                QDropEvent actual(QPointF(local),Qt::CopyAction,&mime,
                                  Qt::LeftButton,Qt::NoModifier);
                QCoreApplication::sendEvent(board->viewport(),&actual);
                check(actual.isAccepted()==accepted,
                      "drop event acceptance matches expectation");
                if (accepted)
                    check(actual.dropAction()==Qt::CopyAction,
                          "final source operation is CopyAction");
            };
            QTimer::singleShot(1000,&app,[=,&w] {
                QMimeData thunar;
                thunar.setUrls({QUrl::fromLocalFile(source1)});
                drop(thunar,QPointF(650,280),true);
                check(board->imageCount()==1,"single Thunar image inserted while recording");
                check(board->hasSelectedImage(),"dropped media is selected");
                check(board->selectedImage().rect.contains(QPointF(650,280)),
                      "image inserted where pointer released");
                check(board->selectedImage().sourcePath==QFileInfo(source1).absoluteFilePath(),
                      "preserved source path in model");
                check(board->renderViewImage(QSize(640,360))
                          .save(output+"/f06_thunar_after.png","PNG"),
                      "render first dropped image during REC");
                auto* record=w.findChild<QAction*>("recordAction");
                auto* indicator=w.findChild<QLabel*>("recordingIndicator");
                check(record && record->text()=="STOP REC",
                      "first-toolbar REC becomes STOP REC");
                check(indicator && indicator->isVisible() &&
                      indicator->text().contains("REC"),
                      "recording indicator visible during Thunar image drop");
                check(w.grab().save(output+"/f06_thunar_during_rec.png","PNG"),
                      "GUI image inserted while REC");
                QFile after(source1);
                check(after.open(QIODevice::ReadOnly),"source still present after drop");
                check(QCryptographicHash::hash(after.readAll(),QCryptographicHash::Sha256)
                      ==beforeDigest,"source file was not moved, modified or deleted");
            });
            QTimer::singleShot(1950,&app,[=] {
                // The file manager may send several URIs, including folders
                // or remote entries; import only supported local images.
                QMimeData multi;
                multi.setUrls({QUrl::fromLocalFile(source2),
                               QUrl::fromLocalFile(source1),
                               QUrl("https://invalid.example/missing.png")});
                drop(multi,QPointF(1180,540),true);
                check(board->imageCount()==3,
                      "two local images copied, remote URL ignored");
                QMimeData invalid;
                invalid.setUrls({QUrl("https://invalid.example/remote.jpg")});
                drop(invalid,QPointF(1420,690),false);
                check(board->imageCount()==3,"remote image URL not fetched");
            });
            QTimer::singleShot(2450,&app,[=] {
                QMimeData direct;
                QImage inlineImage(420,220,QImage::Format_ARGB32);
                inlineImage.fill(QColor("#ed5b20"));
                direct.setImageData(inlineImage);
                drop(direct,QPointF(970,745),true);
                check(board->imageCount()==4,"inline image payload inserted");
                board->undoStack()->undo();
                check(board->imageCount()==3,"Undo removes the inline image");
                board->undoStack()->redo();
                check(board->imageCount()==4,"Redo restores image");
                const QString doc=output+"/f06_thunar_canvas.gxwb";
                QString saveError;
                check(board->saveDocument(doc,&saveError),
                      "save project containing drops: "+saveError);
                check(board->renderViewImage(QSize(640,360))
                          .save(output+"/f06_thunar_final.png","PNG"),
                      "final canvas after file manager and inline drops");
            });
            engine->completed=[&app,engine,ok,file](bool success,const QString& msg) {
                const bool final=*ok && success &&
                    engine->frameCount()>32 && QFileInfo(file).size()>8000;
                qInfo().noquote() << "F06 THUNAR LIVE DROP:"
                    << (final?"PASS":"FAIL")
                    << "| images during REC, URI list, copy, inline image,"
                    << " undo/redo, source unchanged, MKV + AAC"
                    << "| frames" << engine->frameCount()
                    << "dropped" << engine->droppedFrames()
                    << "output" << file << msg;
                app.exit(final?0:2);
            };
            QTimer::singleShot(3700,&app,[engine] {engine->stop();});
            QTimer::singleShot(15000,&app,[&app,engine] {
                if (engine->active() || engine->finishing()) {
                    qWarning() << "F06 Thunar smoke timeout";
                    engine->stop();app.exit(3);
                }
            });
        });
    }

    if (parser.isSet("smoke-record-ui") || parser.isSet("smoke-record-ui-en")) {
        const bool english=parser.isSet("smoke-record-ui-en");
        QTimer::singleShot(350,&app,[&app,&w,english] {
            if (english) w.setLanguage("en_US",false);
            const QString output=QDir(QCoreApplication::applicationDirPath())
                         .absoluteFilePath("../results");
            const QString filename=output+
                         (english?"/f06_ui_videoonly_en.mkv":"/f06_ui_videoonly.mkv");
            QFile::remove(filename);
            auto checks=std::make_shared<bool>(true);
            auto* recordAction=w.findChild<QAction*>("recordAction");
            auto* stopAction=w.findChild<QAction*>("stopRecordAction");
            auto* muteAction=w.findChild<QAction*>("muteRecordAction");
            auto* overlay=w.findChild<QLabel*>("recordingIndicator");
            *checks &= recordAction && stopAction && muteAction && overlay;
            if (recordAction)
                *checks &= recordAction->isEnabled() && recordAction->text()=="REC";
            if (stopAction) *checks &= !stopAction->isEnabled();
            if (overlay) *checks &= !overlay->isVisible();
            qInfo() << "F06 UI initial"
                    << "record enabled" << (recordAction?recordAction->isEnabled():false)
                    << "record label" << (recordAction?recordAction->text():QString())
                    << "stop enabled" << (stopAction?stopAction->isEnabled():false)
                    << "overlay hidden" << (overlay?!overlay->isVisible():false)
                    << "checks" << *checks;
            QTimer::singleShot(70,&app,[&app,&w,output,filename,checks,english] {
                auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!modal || modal->objectName()!="recordingDialog") {
                    qWarning() << "F06 recording dialog not active";
                    *checks=false;
                    return;
                }
                auto* profile=modal->findChild<QComboBox*>("recordingProfile");
                auto* encoder=modal->findChild<QComboBox*>("recordingEncoder");
                auto* mic=modal->findChild<QCheckBox*>("recordingMicEnabled");
                auto* input=modal->findChild<QComboBox*>("recordingMicrophone");
                auto* path=modal->findChild<QLineEdit*>("recordingOutput");
                auto* mp4=modal->findChild<QCheckBox*>("recordingRemuxMp4");
                auto* volume=modal->findChild<QSlider*>("recordingMicVolume");
                auto* start=modal->findChild<QPushButton*>("recordingStart");
                *checks &= profile && encoder && mic && input && path && mp4 && volume && start;
                if (profile && encoder && mic && path && mp4 && start) {
                    *checks &= modal->windowTitle()==
                        (english?"Recording settings":"Impostazioni di registrazione");
                    *checks &= start->text()==
                        (english?"Start recording":"Avvia registrazione");
                    profile->setCurrentIndex(profile->findData("640x360@15"));
                    encoder->setCurrentIndex(encoder->findData("libx264"));
                    mic->setChecked(false);
                    path->setText(filename);
                    mp4->setChecked(false);
                    *checks &= modal->grab().save(output+
                          (english?"/f06_recording_dialog_en.png":
                                   "/f06_recording_dialog_it.png"),"PNG");
                    start->click();
                } else modal->reject();
            });
            w.openRecordingDialog();
            if (!w.recordingActive()) {
                qWarning() << "F06 UI did not start recording";
                app.exit(3);
                return;
            }
            QTimer::singleShot(550,&app,[&w,output,checks,english] {
                auto* overlay=w.findChild<QLabel*>("recordingIndicator");
                auto* stop=w.findChild<QAction*>("stopRecordAction");
                auto* record=w.findChild<QAction*>("recordAction");
                *checks &= overlay && overlay->isVisible() &&
                          overlay->text().contains("REC");
                // The single REC/STOP toggle is intentionally visible;
                // the separate legacy Stop action is hidden to save space.
                *checks &= stop && !stop->isVisible();
                *checks &= record && record->isEnabled() &&
                          record->text()=="STOP REC";
                qInfo() << "F06 UI active"
                        << "overlay visible" << (overlay?overlay->isVisible():false)
                        << "stop enabled" << (stop?stop->isEnabled():false)
                        << "stop visible" << (stop?stop->isVisible():false)
                        << "record label" << (record?record->text():QString())
                        << "record enabled" << (record?record->isEnabled():false)
                        << "record text comparison" << (record && record->text()=="STOP REC")
                        << "stop pointer" << (stop!=nullptr)
                        << "checks" << *checks;
                *checks &= w.grab().save(output+
                     (english?"/f06_recording_active_en.png":
                              "/f06_recording_active_it.png"),"PNG");
            });
            QTimer::singleShot(2200,&app,[&w] {w.stopRecording();});
            QTimer::singleShot(4700,&app,[&app,&w,output,filename,checks,english] {
                const auto* engine=w.recordingForTest();
                const auto* overlay=w.findChild<QLabel*>("recordingIndicator");
                *checks &= engine && !engine->active() && !engine->finishing();
                *checks &= overlay && !overlay->isVisible();
                *checks &= QFileInfo(filename).size()>1000;
                if (engine) *checks &= engine->frameCount()>15;
                qInfo() << "F06 UI final"
                        << "engine active" << (engine?engine->active():false)
                        << "finishing" << (engine?engine->finishing():false)
                        << "overlay hidden" << (overlay?!overlay->isVisible():false)
                        << "file size" << QFileInfo(filename).size()
                        << "checks" << *checks;
                qInfo() << (english?"F06 RECORDING UI EN:":"F06 RECORDING UI IT:")
                        << (*checks?"PASS":"FAIL")
                         << "file" << filename
                         << "frames" << (engine?engine->frameCount():-1);
                app.exit(*checks?0:2);
            });
        });
    }

    if (parser.isSet("smoke-record") || parser.isSet("smoke-record-nvenc") ||
        parser.isSet("smoke-record-videoonly") || parser.isSet("smoke-record-hd")) {
        const bool withAudio=!parser.isSet("smoke-record-videoonly");
        const bool hd=parser.isSet("smoke-record-hd");
        const bool useNvenc=parser.isSet("smoke-record-nvenc") || hd;
        QTimer::singleShot(300,&app,[&app,&w,withAudio,useNvenc,hd] {
            const QString output=QDir(QCoreApplication::applicationDirPath())
                 .absoluteFilePath("../results");
            const QString suffix=hd?"hd_nvenc":useNvenc?"nvenc":withAudio?"aac":"videoonly";
            const QString file=output+"/f06_synthetic_"+suffix+".mkv";
            QFile::remove(file);
            QFile::remove(output+"/f06_synthetic_"+suffix+".mp4");
            auto* engine=new RecordingEngine(&app);
            auto* board=w.boardForTest();
            board->setBoardBackground(BoardView::Background::Grid);
            board->setStrokeColor(QColor("#b72d44"));
            board->setStrokeWidth(16);
            board->addSyntheticStroke(QPointF(150,200),QPointF(880,450),0.8);
            const QString image=QDir(QCoreApplication::applicationDirPath())
                 .absoluteFilePath("../assets/f03_test_chart.png");
            QString importError;
            if (!board->insertImageFromFile(image,QRectF(900,420,630,380),&importError))
                qWarning().noquote() << "F06 image load:" << importError;
            // Use a local synthetic webcam to exercise live video compositing
            // during recording without accessing the user's real camera.
            if (withAudio) {
                board->insertCameraBox("test-pattern",QRectF(145,520,650,350));
                if (!w.startSelectedCamera(true))
                    qWarning() << "F06 synthetic camera was not started";
            }
            const QImage probe=board->renderViewImage(QSize(640,360));
            bool passed=!probe.isNull() && probe.size()==QSize(640,360);
            passed &= probe.save(output+"/f06_render_preview.png","PNG");
            RecordingEngine::Options opts;
            opts.output=file;
            opts.size=hd?QSize(1920,1080):QSize(640,360);
            opts.fps=hd?30:15;
            opts.syntheticAudio=withAudio;
            opts.microphoneEnabled=false;
            opts.encoder=useNvenc?"h264_nvenc":"libx264";
            opts.convertToMp4=withAudio;
            engine->renderFrame=[board](const QSize& size) {
                return board->renderViewImage(size);
            };
            engine->changed=[engine] {
                if (engine->frameCount() % 20 == 0 &&
                    engine->frameCount()>0) {
                    qInfo() << "F06 recorder frames" << engine->frameCount()
                            << "rms %" << engine->levelPercent()
                            << "dropped" << engine->droppedFrames();
                }
            };
            engine->completed=[&app,engine,passed,file](bool success,
                                                          const QString& msg) {
                const bool final=passed && success && engine->frameCount()>25 &&
                                 QFileInfo(file).size()>10000 &&
                                 (engine->maxLevelPercent()>0 ||
                                   file.contains("videoonly"));
                qInfo().noquote() << "F06 RECORD:"
                    << (final?"PASS":"FAIL")
                    << "frames" << engine->frameCount()
                    << "dropped" << engine->droppedFrames()
                    << "max microphone meter" << engine->maxLevelPercent()
                    << "encoder" << engine->encoderUsed()
                    << "output" << file
                    << "message" << msg;
                engine->deleteLater();
                app.exit(final?0:2);
            };
            QString error;
            if (!engine->start(opts,&error)) {
                qWarning().noquote() << "F06 recorder start failed:" << error;
                engine->deleteLater();
                app.exit(2);
                return;
            }
            QTimer::singleShot(1550,&app,[board] {
                board->setStrokeColor(QColor("#184f9b"));
                board->addSyntheticStroke(QPointF(220,650),QPointF(1600,780),0.55);
            });
            QTimer::singleShot(hd?6400:3600,&app,[engine] {
                engine->stop();
            });
            QTimer::singleShot(18000,&app,[&app,engine] {
                if (engine->active() || engine->finishing()) {
                    qWarning() << "F06 RECORD watchdog timed out";
                    engine->stop();
                    app.exit(3);
                }
            });
        });
    }

    if (parser.isSet("smoke-lanscan")) {
        const quint16 httpPort=quint16(parser.value("scan-http-port").toUShort());
        const quint16 rtspPort=quint16(parser.value("scan-rtsp-port").toUShort());
        QTimer::singleShot(250,&app,[&app,&w,httpPort,rtspPort] {
            auto passed=std::make_shared<bool>(true);
            auto check=[passed](bool result,const char* label) {
                if (!result) qWarning() << "F05 LAN SCAN FAIL:" << label;
                *passed &= result;
            };
            QVector<QString> hosts;
            QString error,normalized;
            check(LanCameraScanner::parseSubnet("192.168.1.0/24",&hosts,&error),
                  "private /24 accepted");
            check(hosts.size()==254 && hosts.first()=="192.168.1.1" &&
                  hosts.last()=="192.168.1.254","private /24 bounds");
            check(LanCameraScanner::parseSubnet("10.42.1.19/32",&hosts,&error) &&
                  hosts.size()==1 && hosts.first()=="10.42.1.19",
                  "private /32 accepted");
            check(LanCameraScanner::parseSubnet("172.19.16.0/29",&hosts,&error) &&
                  hosts.size()==6,"172.16/12 private mask");
            check(!LanCameraScanner::parseSubnet("192.168.0.0/16",&hosts,&error),
                  "reject huge scan range");
            check(!LanCameraScanner::parseSubnet("203.0.113.0/24",&hosts,&error),
                  "reject automatic public network scan");
            check(!LanCameraScanner::parseSubnet("8.8.8.8/32",&hosts,&error),
                  "reject automatic public host scan");
            check(!LanCameraScanner::parseSubnet("127.0.0.1/32",&hosts,&error),
                  "reject loopback in production");
            check(!LanCameraScanner::parseSubnet("192.168.1.999/24",&hosts,&error),
                  "reject invalid IPv4");
            check(!LanCameraScanner::parseSubnet("169.254.1.0/24",&hosts,&error),
                  "reject link-local subnet");
            check(!LanCameraScanner::parseSubnet("224.0.0.0/24",&hosts,&error),
                  "reject multicast scan");
            check(NetworkSource::validateUrl("rtsp://203.0.113.25:554/live",
                                             &normalized,&error) &&
                  normalized.startsWith("rtsp://203.0.113.25"),
                  "accept public IPv4 camera manual URL");
            check(NetworkSource::validateUrl("https://camera.example.org/mjpeg",
                                             &normalized,&error),
                  "accept public camera DNS name");
            check(NetworkSource::validateUrl("http://8.8.8.8:8080/stream",
                                             &normalized,&error),
                  "accept public IPv4 HTTP manual URL");
            check(!NetworkSource::validateUrl("ftp://8.8.8.8/media",
                                             &normalized,&error),
                  "reject unsupported manual protocol");
            check(httpPort!=0 && rtspPort!=0,"valid local fixture ports");
            auto* button=w.findChild<QPushButton*>("networkScanButton");
            check(button!=nullptr,"Scan LAN button added to camera panel");
            if (button) check(button->text()=="Scansiona LAN",
                               "Scan LAN button localized IT");
            const QString localeBase=QDir(QCoreApplication::applicationDirPath())
                    .absoluteFilePath("locale");
            auto translation=std::make_shared<Localization>(localeBase);
            translation->setLanguage("it_IT");
            auto* dialog=new LanScanDialog(&w,true,{httpPort,rtspPort},
                     [translation](const char* str){return translation->tr(str);});
            dialog->setSubnet("127.0.0.1/32");
            dialog->show();
            QApplication::processEvents();
            check(dialog->windowTitle()=="Ricerca telecamere LAN",
                  "Italian dialog translation from MO");
            check(dialog->findChild<QPushButton*>("lanScanStart")->text()=="Scansiona",
                  "Italian scan button translation");
            check(!dialog->scanRunning() && dialog->resultCount()==0,
                  "no automatic scan on opening dialog");
            check(dialog->beginScan(),"explicit loopback scan started");
            auto* poll=new QTimer(dialog);
            poll->setInterval(60);
            int* steps=new int(0); // released in completion lambda
            QObject::connect(poll,&QTimer::timeout,dialog,
                [&app,&w,poll,dialog,passed,steps,rtspPort,httpPort,check] {
                    ++(*steps);
                    if (dialog->scanRunning()) {
                        if (*steps>110) {
                            check(false,"scan watchdog exceeded 6.6 seconds");
                        } else return;
                    }
                    poll->stop();
                    delete steps;
                    check(dialog->resultCount()>=2,"RTSP and HTTP services discovered");
                    const QString output=QDir(QCoreApplication::applicationDirPath())
                              .absoluteFilePath("../results");
                    check(dialog->grab().save(output+"/f05_lan_discovery_it.png","PNG"),
                          "LAN discovery IT screenshot");
                    check(dialog->checked()==dialog->total(),
                          "scan completed every selected probe");
                    check(dialog->total()==2,"test only probes selected loopback ports");
                    bool foundHttp=false,foundRtsp=false;
                    auto* tree=dialog->findChild<QTreeWidget*>("lanScanResults");
                    check(tree!=nullptr,"results tree available");
                    if (tree) {
                        for (int i=0;i<tree->topLevelItemCount();++i) {
                            auto* item=tree->topLevelItem(i);
                            const QString candidate=item->data(0,Qt::UserRole).toString();
                            check(candidate.startsWith("http://127.0.0.1:") ||
                                  candidate.startsWith("rtsp://127.0.0.1:"),
                                  "candidate URL must remain local and editable");
                            check(item->text(3)==QString("Risposta protocollo"),
                                  "protocol signature verified for both loopback tests");
                        }
                    }
                    for (int i=0;i<dialog->resultCount();++i) {
                        check(dialog->resultHost(i)=="127.0.0.1",
                              "discovery kept loopback target only");
                        foundHttp |= dialog->resultProtocol(i)=="HTTP";
                        foundRtsp |= dialog->resultProtocol(i)=="RTSP";
                    }
                    check(foundHttp && foundRtsp,"protocol-aware fingerprinting");
                    dialog->selectResult(0);
                    dialog->chooseSelected();
                    check(dialog->result()==QDialog::Accepted,
                          "Use address accepts selected candidate");
                    check(dialog->selectedCandidateUrl().startsWith("rtsp://127.0.0.1:") ||
                          dialog->selectedCandidateUrl().startsWith("http://127.0.0.1:"),
                          "candidate selected URL with expected protocol");
                    check(w.networkForTest(w.selectedNetworkId())==nullptr,
                          "LAN scan never automatically connects a camera");
                    dialog->deleteLater();
                    // Test Cancel with 127.0.0.0/24 only. No public/private
                    // real device is contacted in this automated test.
                    LanScanDialog cancelDialog(&w,true,{quint16(65000)});
                    cancelDialog.setSubnet("127.0.0.0/24");
                    check(cancelDialog.beginScan(),"begin cancelable loopback-only scan");
                    cancelDialog.findChild<QPushButton*>("lanScanCancel")->click();
                    check(!cancelDialog.scanRunning(),"user Cancel stops scan");
                    // Test English translation with the same gettext library.
                    auto english=std::make_shared<Localization>(
                        QDir(QCoreApplication::applicationDirPath())
                          .absoluteFilePath("locale"));
                    english->setLanguage("en_US");
                    LanScanDialog englishDialog(&w,true,{httpPort},
                        [english](const char* str){return english->tr(str);});
                    check(englishDialog.windowTitle()=="LAN camera discovery",
                          "English dialog title");
                    check(englishDialog.findChild<QPushButton*>("lanScanStart")->text()=="Scan",
                          "English scan button");
                    // Exercise the real "Scan LAN" button wiring in StudioWindow,
                    // without initiating a scan from the user-facing modal.
                    auto* scanButton=w.findChild<QPushButton*>("networkScanButton");
                    bool actualModalOpened=false;
                    if (scanButton) {
                        QTimer::singleShot(70,&app,[&actualModalOpened] {
                            // Previously used findChild(): it could locate the
                            // old deferred-delete test dialog instead of this
                            // newly opened modal. Only the active modal is valid.
                            auto* modal=qobject_cast<QDialog*>(
                                QApplication::activeModalWidget());
                            if (modal && modal->objectName()=="lanScanDialog") {
                                actualModalOpened=true;
                                modal->reject();
                            }
                        });
                        QTimer::singleShot(700,&app,[] {
                            // Timeout guard against a modal left open due to a
                            // regression in its wiring; never hang the test.
                            for (QWidget* widget : QApplication::topLevelWidgets())
                                if (widget->objectName()=="lanScanDialog" &&
                                    widget->isVisible()) widget->close();
                        });
                        scanButton->click();
                    }
                    check(actualModalOpened,"main IP panel opens scanner dialog");
                    qInfo().noquote() << "F05 LAN DISCOVERY:"
                        << (*passed?"PASS":"FAIL")
                        << "| CIDR bounds, public stream URLs, RFC1918-only scan,"
                        << " loopback RTSP/HTTP fingerprints, cancel, IT/EN UI";
                    app.exit(*passed?0:2);
                });
            poll->start();
        });
    }

    if (parser.isSet("smoke-network-restore")) {
        QTimer::singleShot(250,&app,[&app,&w] {
            auto* combo=w.findChild<QComboBox*>("networkCombo");
            const bool ok=combo && combo->count()>=1 &&
                !w.selectedNetworkId().isEmpty() &&
                w.networkForTest(w.selectedNetworkId())==nullptr;
            qInfo().noquote() << "F05 RESTORE WITHOUT AUTOSTART:" << (ok?"PASS":"FAIL")
                << "| saved source count" << (combo?combo->count():0);
            app.exit(ok?0:2);
        });
    }
    if (parser.isSet("smoke-network") || parser.isSet("smoke-network-reconnect")) {
        const bool reconnectTest=parser.isSet("smoke-network-reconnect");
        const QString testUrl=parser.value("stream-url");
        QTimer::singleShot(250,&app,[&app,&w,testUrl,reconnectTest] {
            bool okay=true;
            auto verify=[&](bool result,const QString& step) {
                if (!result) qWarning().noquote() << "F05 TEST FAIL:" << step;
                okay &= result;
            };
            QString urlError,normalized;
            verify(!NetworkSource::validateUrl("file:///etc/passwd",&normalized,&urlError),
                   "forbid non-stream file URL");
            verify(!NetworkSource::validateUrl("http://user:pass@127.0.0.1/live",
                    &normalized,&urlError),"forbid basic-auth credentials");
            verify(!NetworkSource::validateUrl("rtsp://192.168.1.5/live?token=SECRET",
                    &normalized,&urlError),"forbid bearer token in query");
            verify(!NetworkSource::validateUrl("https://camera/live?api_key=SECRET",
                    &normalized,&urlError),"forbid API key in query");
            verify(!NetworkSource::validateUrl("ftp://127.0.0.1/foo",
                    &normalized,&urlError),"forbid unapproved protocol");
            verify(NetworkSource::validateUrl("rtsp://127.0.0.1:8554/stream",
                    &normalized,&urlError),"validate RTSP URL");
            verify(NetworkSource::validateUrl("http://127.0.0.1/mjpg?action=stream",
                    &normalized,&urlError),"accept MJPEG action query");
            verify(!testUrl.isEmpty() &&
                   NetworkSource::validateUrl(testUrl,&normalized,&urlError),
                   "validate loopback MJPEG test URL");
            auto nav=w.findChildren<QPushButton*>("sideNav");
            if (nav.size()>1) nav[1]->click();
            auto* toggle=w.findChild<QToolButton*>("networkToggle");
            auto* panel=w.findChild<QWidget*>("networkPanel");
            auto* combo=w.findChild<QComboBox*>("networkCombo");
            auto* urlInput=w.findChild<QLineEdit*>("networkUrlEdit");
            auto* insert=w.findChild<QPushButton*>("networkInsertButton");
            verify(toggle && panel && combo && urlInput && insert,"IP controls exist");
            if (toggle && panel) {
                verify(!panel->isVisible(),"network controls collapsed by default");
                toggle->click();
                QApplication::processEvents();
                verify(panel->isVisible(),"expanded network controls");
            }
            if (urlInput) urlInput->setText(testUrl);
            verify(w.addNetworkUrl(testUrl,&urlError),"register source "+urlError);
            verify(w.addNetworkUrl(testUrl,&urlError),"deduplicate existing source");
            if (combo) {
                verify(combo->count()==1 && combo->itemText(0)=="ip0",
                       "short IP source alias");
                verify(combo->itemData(0,Qt::ToolTipRole).toString()==normalized,
                       "full URL in source tooltip only");
            }
            auto* board=w.boardForTest();
            const QString id=w.selectedNetworkId();
            verify(id.startsWith("net:") && id.size()==24,"safe net media ID");
            verify(board->insertCameraBox(id,QRectF(100,210,700,445)),
                   "first IP box created");
            verify(board->insertCameraBox(id,QRectF(970,300,600,340)),
                   "second IP box shares source");
            board->setStrokeColor(QColor("#e51b70"));
            board->addSyntheticStroke(QPointF(200,430),QPointF(1250,520),0.8);
            verify(w.networkForTest(id)==nullptr,"adding URL never starts a connection");
            verify(w.startSelectedNetwork(),"start IP stream");
            // Place a third video box using the same pen/mouse gesture path
            // exposed by the Insert IP box control.
            if (insert) {
                insert->click();
                const auto mouse=[&](QEvent::Type type,const QPointF& scene,
                                     Qt::MouseButton button,Qt::MouseButtons buttons) {
                    const QPoint local=board->mapFromScene(scene);
                    QMouseEvent event(type,QPointF(local),
                        QPointF(board->viewport()->mapToGlobal(local)),
                        button,buttons,Qt::NoModifier);
                    QCoreApplication::sendEvent(board->viewport(),&event);
                };
                mouse(QEvent::MouseButtonPress,QPointF(920,680),
                      Qt::LeftButton,Qt::LeftButton);
                mouse(QEvent::MouseMove,QPointF(1420,1000),
                      Qt::NoButton,Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease,QPointF(1420,1000),
                      Qt::LeftButton,Qt::NoButton);
                verify(board->cameraBoxCount(id)==3,
                       "pen-defined third IP box shares network decoder");
            }
            if (toggle) toggle->setChecked(true);
            // Disconnected test server closes its socket after each 20 frames;
            // allow the source's bounded backoff to reconnect multiple times.
            const int duration=reconnectTest ? 8200 :
                (testUrl.startsWith("rtsp",Qt::CaseInsensitive) ? 6500 : 3200);
            QTimer::singleShot(duration,&app,[&app,&w,reconnectTest,testUrl,id,okay] () mutable {
                auto verify=[&](bool result,const QString& step) {
                    if (!result) qWarning().noquote() << "F05 TEST FAIL:" << step;
                    okay &= result;
                };
                auto* source=w.networkForTest(id);
                auto* board=w.boardForTest();
                verify(source!=nullptr,"network source exists");
                if (source) {
                    qInfo().noquote() << "F05 loopback frames" << source->framesSeen()
                        << "attempts" << source->connectionAttempts()
                        << "state" << source->status()
                        << "error" << source->lastError();
                    verify(source->framesSeen()>=3,"decoded multiple MJPEG frames");
                    verify(source->isActive(),"network stream active before stop");
                    if (reconnectTest)
                        verify(source->connectionAttempts()>=2,
                               "network source reconnect after disconnect");
                }
                verify(board->cameraBoxCount(id)==3,
                       "three IP boxes share one source");
                verify(board->strokeCount()==1,
                       "vector strokes remain above IP box media");
                const QString out=QDir(QCoreApplication::applicationDirPath())
                                      .absoluteFilePath("../results");
                if (!reconnectTest) {
                    verify(w.grab().save(out+"/f05_ip_stream.png","PNG"),
                           "IP screenshot");
                    verify(board->exportPng(out+"/f05_composite.png"),
                           "composite export");
                }
                w.stopSelectedNetwork();
                if (source) verify(!source->isActive(),
                                   "explicit stop cancels automatic retries");
                QString error;
                const QString project=out+"/f05_ip_project.gxwb";
                verify(board->saveDocument(project,&error),
                       "save IP snapshot project: "+error);
                QFile file(project);
                if (file.open(QIODevice::ReadOnly)) {
                    const QByteArray bytes=file.readAll();
                    verify(!bytes.contains(testUrl.toUtf8()),
                           "project excludes IP URL, embeds only safe media ID");
                    const auto doc=QJsonDocument::fromJson(bytes).object();
                    verify(doc.value("images").toArray().size()==3,
                           "saved three IP boxes");
                } else verify(false,"open project file");
                board->clearBoard();
                verify(board->loadDocument(project,&error),
                       "reload IP snapshot project");
                verify(board->cameraBoxCount(id)==3,
                       "restored IP box geometry and device IDs");
                verify(board->strokeCount()==1,
                       "restored overlay ink");
                verify(board->exportPng(out+"/f05_reloaded.png"),
                       "export reloaded IP snapshot");
                qInfo().noquote() << (reconnectTest?"F05 RECONNECT:":"F05 MJPEG:")
                    << (okay?"PASS":"FAIL");
                app.exit(okay?0:2);
            });
        });
    }

    if (parser.isSet("smoke-zmenu")) {
        QTimer::singleShot(300,&app,[&app,&w]() {
            const QString root=QDir(QCoreApplication::applicationDirPath())
                                   .absoluteFilePath("..");
            const QString output=root+"/results";
            QDir().mkpath(output);
            bool okay=true;
            auto verify=[&](bool check,const QString& step) {
                if (!check) qWarning().noquote() << "F04.3 Z FAIL:" << step;
                okay &= check;
            };
            auto* board=w.boardForTest();
            w.setLanguage("it_IT",false);
            board->fitBoard();
            const QString chart=root+"/assets/f03_test_chart.png";
            const QString microscope=root+"/assets/f03_test_microscope.png";
            QString error;
            verify(board->insertImageFromFile(chart,QRectF(200,200,720,460),&error),
                   "insert chart image");
            const qint64 chartId=board->selectedImage().id;
            verify(board->insertImageFromFile(microscope,QRectF(440,290,710,440),&error),
                   "insert microscope image");
            const qint64 microscopeId=board->selectedImage().id;
            verify(board->insertCameraBox("test-pattern",QRectF(610,350,710,440)),
                   "insert camera media box");
            const qint64 cameraId=board->selectedImage().id;
            verify(board->selectedImage().isCamera,"camera selection is video box");
            board->setStrokeColor(QColor("#ec1255"));
            board->setStrokeWidth(18);
            board->addSyntheticStroke(QPointF(660,470),QPointF(980,470),1.0);
            const int inkCount=board->strokeCount();
            verify(inkCount==1,"one overlay ink stroke");
            auto saveOrder=[&](const QString& name) {
                QVector<qint64> result;
                QString saveError;
                const QString filename=output+"/f04_3_"+name+".gxwb";
                if (!board->saveDocument(filename,&saveError)) {
                    verify(false,"save document "+name+": "+saveError);
                    return result;
                }
                QFile file(filename);
                if (!file.open(QIODevice::ReadOnly)) {
                    verify(false,"reopen document "+name);
                    return result;
                }
                const auto doc=QJsonDocument::fromJson(file.readAll()).object();
                for (const auto& media : doc.value("images").toArray())
                    result.push_back(media.toObject().value("id").toString().toLongLong());
                verify(doc.value("strokes").toArray().size()==inkCount,
                       "vector ink preserved "+name);
                return result;
            };
            auto checkOrder=[&](const QVector<qint64>& expected,const QString& label) {
                const auto actual=saveOrder(label);
                verify(actual==expected,QString("Z order %1: expected %2 items, got %3")
                       .arg(label).arg(expected.size()).arg(actual.size()));
            };
            const QVector<qint64> initial{chartId,microscopeId,cameraId};
            checkOrder(initial,"initial");
            verify(board->exportPng(output+"/f04_3_before.png"),"initial composition");
            const auto mouse=[&](QEvent::Type type, const QPointF& where,
                                 Qt::MouseButton button, Qt::MouseButtons buttons) {
                const QPoint local=board->mapFromScene(where);
                QMouseEvent event(type,QPointF(local),
                       QPointF(board->viewport()->mapToGlobal(local)),
                       button,buttons,Qt::NoModifier);
                QCoreApplication::sendEvent(board->viewport(),&event);
            };
            board->setTool(BoardView::Tool::Select);
            const QPointF chartSpot(280,240); // uncovered: bottom image only
            mouse(QEvent::MouseButtonPress,chartSpot,Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease,chartSpot,Qt::LeftButton,Qt::NoButton);
            verify(board->hasSelectedImage() && board->selectedImage().id==chartId,
                   "selected underlying still image");
            verify(board->canBringSelectedImageForward() &&
                   !board->canSendSelectedImageBackward(),
                   "bottom image movement constraints");

            auto findAction=[](QMenu* menu,const QString& objectName) -> QAction* {
                for (auto* action : menu->actions())
                    if (action->objectName()==objectName) return action;
                return nullptr;
            };
            const QStringList actionNames{
                "mediaZForward","mediaZBackward","mediaZFront","mediaZBack"};
            auto context=[&](const QString& activate,
                             const QVector<bool>& expectedStates,
                             const QString& capture=QString()) {
                bool shown=false;
                // Timer runs inside QMenu::exec's nested event loop. It also
                // tests the real right-press/right-release path on the viewport.
                QTimer::singleShot(55,&app,[&] {
                    auto* menu=w.findChild<QMenu*>("mediaZMenu");
                    if (!menu) {
                        verify(false,"menu did not open");
                        return;
                    }
                    shown=true;
                    verify(menu->actions().size()==5,"menu has four actions and divider");
                    for (int i=0;i<actionNames.size();++i) {
                        QAction* action=findAction(menu,actionNames.at(i));
                        verify(action!=nullptr,"menu action "+actionNames.at(i));
                        if (action) {
                            verify(action->isEnabled()==expectedStates.value(i),
                                   "menu action enable "+actionNames.at(i));
                        }
                    }
                    if (capture=="f04_3_menu_it.png") {
                        auto* first=findAction(menu,"mediaZForward");
                        verify(first && first->text()=="Porta avanti",
                               "Italian Z menu label");
                    } else if (capture=="f04_3_menu_en.png") {
                        auto* first=findAction(menu,"mediaZForward");
                        auto* last=findAction(menu,"mediaZBack");
                        verify(first && first->text()=="Bring forward" &&
                               last && last->text()=="Send to back",
                               "English Z menu labels");
                    }
                    if (!capture.isEmpty())
                        verify(menu->grab().save(output+"/"+capture,"PNG"),
                               "capture actual menu");
                    if (!activate.isEmpty()) {
                        if (auto* action=findAction(menu,activate)) {
                            verify(action->isEnabled(),"clicked action enabled");
                            if (action->isEnabled()) action->trigger();
                        }
                    }
                    menu->close();
                });
                mouse(QEvent::MouseButtonPress,chartSpot,Qt::RightButton,Qt::RightButton);
                mouse(QEvent::MouseButtonRelease,chartSpot,Qt::RightButton,Qt::NoButton);
                QApplication::processEvents();
                verify(shown,"right click triggered context menu");
            };

            context("mediaZForward",{true,false,true,false},"f04_3_menu_it.png");
            checkOrder({microscopeId,chartId,cameraId},"one_step_forward");
            board->undoStack()->undo();
            checkOrder(initial,"undo_forward");
            board->undoStack()->redo();
            checkOrder({microscopeId,chartId,cameraId},"redo_forward");

            context("mediaZFront",{true,true,true,true});
            checkOrder({microscopeId,cameraId,chartId},"to_front");
            context("",{false,true,false,true},"f04_3_top_menu_it.png");
            context("mediaZBackward",{false,true,false,true});
            checkOrder({microscopeId,chartId,cameraId},"one_step_backward");
            context("mediaZBack",{true,true,true,true});
            checkOrder(initial,"to_back");
            w.setLanguage("en_US",false);
            context("",{true,false,true,false},"f04_3_menu_en.png");
            w.setLanguage("it_IT",false);

            // Right-drag on the same selected object still pans the canvas.
            const auto panBefore=board->viewCenter();
            mouse(QEvent::MouseButtonPress,chartSpot,Qt::RightButton,Qt::RightButton);
            mouse(QEvent::MouseMove,chartSpot+QPointF(220,140),
                  Qt::NoButton,Qt::RightButton);
            mouse(QEvent::MouseButtonRelease,chartSpot+QPointF(220,140),
                  Qt::RightButton,Qt::NoButton);
            verify(QLineF(panBefore,board->viewCenter()).length()>40,
                   "right drag pans when starting over selected image");
            checkOrder(initial,"pan_keeps_order");
            board->fitBoard();

            // Select the camera by clicking a part not covered by either image.
            const QPointF cameraSpot(1220,690);
            mouse(QEvent::MouseButtonPress,cameraSpot,Qt::LeftButton,Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease,cameraSpot,Qt::LeftButton,Qt::NoButton);
            verify(board->hasSelectedImage() && board->selectedImage().id==cameraId
                   && board->selectedImage().isCamera,"camera box selected");
            // Context helper used chartSpot until now; camera context uses
            // the requested media position, and should behave identically.
            bool cameraMenuShown=false;
            QTimer::singleShot(55,&app,[&] {
                auto* menu=w.findChild<QMenu*>("mediaZMenu");
                cameraMenuShown=(menu!=nullptr);
                if (menu) {
                    auto* back=findAction(menu,"mediaZBack");
                    verify(back && back->isEnabled(),"camera Send to back available");
                    if (back) back->trigger();
                    menu->close();
                }
            });
            mouse(QEvent::MouseButtonPress,cameraSpot,Qt::RightButton,Qt::RightButton);
            mouse(QEvent::MouseButtonRelease,cameraSpot,Qt::RightButton,Qt::NoButton);
            QApplication::processEvents();
            verify(cameraMenuShown,"camera right-click menu displayed");
            checkOrder({cameraId,chartId,microscopeId},"camera_to_back");
            verify(board->exportPng(output+"/f04_3_camera_back.png"),
                   "render camera under images");
            const QImage initialComposite(output+"/f04_3_before.png");
            const QImage reorderedComposite(output+"/f04_3_camera_back.png");
            verify(!initialComposite.isNull() && !reorderedComposite.isNull(),
                   "media composition images decode");
            if (!initialComposite.isNull() && !reorderedComposite.isNull()) {
                verify(initialComposite.pixelColor(800,550)!=
                       reorderedComposite.pixelColor(800,550),
                       "Z move changes actual pixel where media overlap");
                verify(initialComposite.pixelColor(730,470)==
                       reorderedComposite.pixelColor(730,470) &&
                       reorderedComposite.pixelColor(730,470).red()>200,
                       "pink ink remains the topmost layer");
            }
            board->undoStack()->undo();
            checkOrder(initial,"undo_camera_to_back");
            board->undoStack()->redo();
            checkOrder({cameraId,chartId,microscopeId},"redo_camera_to_back");
            verify(board->strokeCount()==inkCount,"ink untouched after Z operations");

            const QString finalFile=output+"/f04_3_saved_media_layers.gxwb";
            verify(board->saveDocument(finalFile,&error),"save final Z project");
            board->clearBoard();
            verify(board->loadDocument(finalFile,&error),"reload Z project");
            verify(board->strokeCount()==inkCount,"ink persisted across reload");
            checkOrder({cameraId,chartId,microscopeId},"reloaded");
            verify(board->exportPng(output+"/f04_3_reloaded.png"),
                   "PNG after reload");

            // An unselected media box must not steal right-click from pan.
            verify(!board->hasSelectedImage(),
                   "loading a project clears media selection");
            mouse(QEvent::MouseButtonPress,chartSpot,Qt::RightButton,Qt::RightButton);
            mouse(QEvent::MouseButtonRelease,chartSpot,Qt::RightButton,Qt::NoButton);
            QApplication::processEvents();
            verify(w.findChild<QMenu*>("mediaZMenu")==nullptr,
                   "no context menu on unselected media");

            // Test a click in free canvas: it must not invoke the media menu.
            const QPointF freeSpot(1720,850);
            mouse(QEvent::MouseButtonPress,freeSpot,Qt::RightButton,Qt::RightButton);
            mouse(QEvent::MouseButtonRelease,freeSpot,Qt::RightButton,Qt::NoButton);
            QApplication::processEvents();
            verify(w.findChild<QMenu*>("mediaZMenu")==nullptr,
                   "no media menu in empty canvas");

            qInfo().noquote() << "F04.3 Z MENU:"
                << (okay?"PASS":"FAIL")
                << "| four actions, disabled bounds, images/camera, right-drag pan,"
                << " undo/redo, JSON reload, ink always retained";
            app.exit(okay?0:2);
        });
    }

    if (parser.isSet("smoke-camera") || parser.isSet("smoke-camera-real")) {
        const bool physical=parser.isSet("smoke-camera-real");
        QTimer::singleShot(300,&app,[&app,&w,physical] {
            const QString root=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("..");
            const QString output=root+"/results";
            const auto devices=CameraSource::listVideoDevices();
            qInfo().noquote() << "F04 devices:" << devices.join(", ");
            const QString sourceDevice=physical ? devices.value(0) : QString("test-pattern");
            if (sourceDevice.isEmpty()) {
                qWarning() << "F04: No physical USB camera found.";
                app.exit(3); return;
            }
            auto* board=w.boardForTest();
            w.setLanguage("it_IT",false);
            // F04.1 compact-panel regression: short aliases with a complete
            // device tooltip, non-scrolling controls and bilingual buttons.
            bool compactUiPass = true;
            auto* combo=w.findChild<QComboBox*>("cameraDeviceCombo");
            auto* panel=w.findChild<QScrollArea*>("cameraPanel");
            auto buttons=w.findChildren<QPushButton*>("cameraAction");
            compactUiPass &= combo && panel && buttons.size()==4;
            if (combo && combo->count() > 0) {
                compactUiPass &= combo->itemText(0)=="cam0";
                compactUiPass &= combo->itemData(0).toString()==devices.value(0);
                const QString detail=combo->itemData(0,Qt::ToolTipRole).toString();
                compactUiPass &= detail.contains(devices.value(0));
                compactUiPass &= combo->toolTip()==detail;
            } else compactUiPass = false;
            auto allButtonsMatch = [&buttons](const QStringList& needed) {
                QStringList actual;
                for (const auto* button : buttons) actual << button->text();
                for (const auto& label : needed) {
                    if (!actual.contains(label)) return false;
                }
                return true;
            };
            compactUiPass &= allButtonsMatch({"Aggiorna","Avvia","Ferma","Inserisci box"});
            w.setLanguage("en_US",false);
            compactUiPass &= allButtonsMatch({"Refresh","Start","Stop","Insert box"});
            w.setLanguage("it_IT",false);
            auto navButtons=w.findChildren<QPushButton*>("sideNav");
            if (navButtons.size() > 1) navButtons[1]->click();
            QApplication::processEvents();
            if (panel) {
                const bool scrollVisible=panel->verticalScrollBar()->isVisible();
                compactUiPass &= !scrollVisible;
                qInfo() << "F04.1 compact panel:" << "cam="
                        << (combo?combo->currentText():QString())
                        << "tooltip=" << (combo?combo->toolTip():QString())
                        << "vertical scrollbar shown=" << scrollVisible;
            }
            qInfo() << "F04.1 UI TEST:" << (compactUiPass?"PASS":"FAIL");
            if (physical) {
                // Test the camera without persisting any real frames to disk.
                if (!w.startSelectedCamera(false)) { app.exit(2); return; }
            } else {
                auto nav=w.findChildren<QPushButton*>("sideNav");
                if (nav.size()>1) nav[1]->click();
                board->insertCameraBox(sourceDevice,QRectF(180,200,680,470));
                board->insertCameraBox(sourceDevice,QRectF(1030,290,570,330));
                board->addSyntheticStroke(QPointF(130,140),QPointF(900,560),0.7);
                if (!w.startSelectedCamera(true)) { app.exit(2); return; }
            }
            QTimer::singleShot(3000,&app,[&app,&w,physical,sourceDevice,output,compactUiPass] {
                auto* source=w.cameraForTest(sourceDevice);
                auto* board=w.boardForTest();
                bool passed=compactUiPass && source && source->framesSeen()>=4 && source->isRunning();
                if (source)
                    qInfo().noquote() << "F04 captured frames:" << source->framesSeen()
                        << "status:" << source->status()
                        << "lastError:" << source->lastError();
                if (!physical) {
                    passed &= board->cameraBoxCount(sourceDevice)==2;
                    passed &= board->strokeCount()>0;
                    board->setTool(BoardView::Tool::Select);
                    const QPoint a=board->mapFromScene(QPointF(490,380));
                    const QPoint b=board->mapFromScene(QPointF(560,430));
                    QMouseEvent press(QEvent::MouseButtonPress,QPointF(a),
                        QPointF(board->viewport()->mapToGlobal(a)),
                        Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QCoreApplication::sendEvent(board->viewport(),&press);
                    QMouseEvent move(QEvent::MouseMove,QPointF(b),
                        QPointF(board->viewport()->mapToGlobal(b)),
                        Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
                    QCoreApplication::sendEvent(board->viewport(),&move);
                    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(b),
                        QPointF(board->viewport()->mapToGlobal(b)),
                        Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QCoreApplication::sendEvent(board->viewport(),&release);
                    passed &= board->hasSelectedImage();
                    passed &= board->cameraBoxCount(sourceDevice)==2;
                    passed &= board->strokeCount()>0;
                    passed &= w.grab().save(output+"/f04_test_stream.png","PNG");
                    QString error;
                    passed &= board->saveDocument(output+"/f04_sample.gxwb",&error);
                    passed &= board->exportPng(output+"/f04_composite.png");
                    qInfo() << "F04 doc" << error;
                    // Saving live boxes should yield frame snapshots, not URL dependencies.
                    passed &= board->loadDocument(output+"/f04_sample.gxwb",&error);
                    passed &= board->cameraBoxCount(sourceDevice)==2;
                }
                if (source) source->stop();
                qInfo().noquote() << (physical ? "F04 REAL CAMERA:" : "F04 CAMERA:")
                     << (passed ? "PASS" : "FAIL");
                app.exit(passed?0:2);
            });
        });
    }

    if (parser.isSet("smoke-images")) {
        QTimer::singleShot(300, &app, [&app, &w]() {
            const QString root=QCoreApplication::applicationDirPath()+"/..";
            const QString output=QDir(root).absoluteFilePath("results");
            const QString image1=QDir(root).absoluteFilePath("assets/f03_test_chart.png");
            const QString image2=QDir(root).absoluteFilePath("assets/f03_test_microscope.png");
            QDir().mkpath(output);
            auto* board=w.boardForTest();
            w.setLanguage("it_IT", false);
            w.setDrawingToolsOpen(true);
            w.setSidebarOpen(true);
            board->fitBoard();
            bool okay=true;
            auto verify=[&](bool condition, const QString& step) {
                if (!condition) qWarning().noquote() << "F03 FAIL:" << step;
                okay &= condition;
            };
            const auto mouse=[&](QEvent::Type type, const QPointF& pos,
                                 Qt::MouseButton button,Qt::MouseButtons buttons) {
                const QPoint local=board->mapFromScene(pos);
                QMouseEvent event(type,QPointF(local),
                    QPointF(board->viewport()->mapToGlobal(local)),button,buttons,Qt::NoModifier);
                QCoreApplication::sendEvent(board->viewport(),&event);
            };
            const auto drag=[&](const QPointF& a, const QPointF& b) {
                mouse(QEvent::MouseButtonPress,a,Qt::LeftButton,Qt::LeftButton);
                mouse(QEvent::MouseMove,b,Qt::NoButton,Qt::LeftButton);
                mouse(QEvent::MouseButtonRelease,b,Qt::LeftButton,Qt::NoButton);
            };
            QString error;
            verify(board->insertImageFromFile(image1,QRectF(150,180,600,400),&error),
                   "insert first image: "+error);
            verify(board->imageCount()==1 && board->hasSelectedImage(),
                   "first image selected");
            const auto first=board->selectedImage();
            verify(!first.image.isNull() && first.image.width()==840,
                   "first image decoded");
            verify(w.grab().save(output+"/f03_one_image.png","PNG"),
                   "save one image screenshot");
            board->setTool(BoardView::Tool::Select);
            const QRectF original=board->selectedImage().rect;
            drag(original.center(),original.center()+QPointF(120,80));
            verify(QLineF(board->selectedImage().rect.topLeft(),
                          original.topLeft()+QPointF(120,80)).length()<6,
                   "move selected image");
            board->undoStack()->undo();
            verify(board->selectedImage().rect==original,"undo image move");
            board->undoStack()->redo();
            verify(board->selectedImage().rect!=original,"redo image move");
            board->undoStack()->undo();

            const QVector<QPointF> offsets={
                QPointF(-42,-32),QPointF(0,-38),QPointF(42,-32),
                QPointF(40,0),QPointF(43,33),QPointF(0,40),
                QPointF(-42,33),QPointF(-40,0)
            };
            for (int i=0;i<8;i++) {
                const QRectF before=board->selectedImage().rect;
                const QPointF center=before.center();
                const QVector<QPointF> handles={
                    before.topLeft(),QPointF(center.x(),before.top()),before.topRight(),
                    QPointF(before.right(),center.y()),before.bottomRight(),
                    QPointF(center.x(),before.bottom()),before.bottomLeft(),
                    QPointF(before.left(),center.y())
                };
                drag(handles[i],handles[i]+offsets[i]);
                verify(board->selectedImage().rect != before,
                       QString("resize handle %1").arg(i+1));
                board->undoStack()->undo();
                verify(board->selectedImage().rect==before,
                       QString("resize undo %1").arg(i+1));
            }
            verify(board->insertImageFromFile(image2,QRectF(450,320,550,340),&error),
                   "insert second image");
            verify(board->imageCount()==2,"two images");
            const auto selectedBefore=board->selectedImage();
            board->setSelectedImageOpacity(0.5);
            board->setSelectedImageFillCrop(true);
            board->setSelectedImageAspectLocked(false);
            verify(std::abs(board->selectedImage().opacity-0.5)<0.001 &&
                   board->selectedImage().fillCrop &&
                   !board->selectedImage().lockAspect,"image properties");
            board->sendSelectedImageBackward();
            board->bringSelectedImageForward();

            verify(board->startImagePlacement(image1,&error),"prepare image pen placement");
            board->setTool(BoardView::Tool::Select);
            drag(QPointF(1000,210),QPointF(1560,600));
            verify(board->imageCount()==3,"pen-sized rectangle creates image box");
            verify(board->selectedImage().rect.width()>500,"pen-defined box width");
            verify(w.grab().save(output+"/f03_three_boxes.png","PNG"),"three boxes screenshot");
            const QString project=output+"/f03_sample.gxwb";
            verify(board->saveDocument(project,&error),"save self-contained GXWB");
            QFile projectFile(project);
            verify(projectFile.open(QIODevice::ReadOnly),"open saved project");
            const auto projectJson=QJsonDocument::fromJson(projectFile.readAll()).object();
            const auto images=projectJson.value("images").toArray();
            verify(images.size()==3 && images[0].toObject()
                .value("pngBase64").toString().size()>1000,"embedded images in document");
            projectFile.close();
            verify(board->exportPng(output+"/f03_export.png"),"export composed PNG");
            verify(board->exportViewPng(output+"/f03_current_view.png"),
                   "export viewport PNG");
            board->clearBoard();
            verify(board->imageCount()==0,"clear board removes images");
            board->undoStack()->undo();
            verify(board->imageCount()==3,"undo clear restores images");
            board->undoStack()->redo();
            verify(board->imageCount()==0,"redo clear removes images");
            verify(board->loadDocument(project,&error),"reload images "+error);
            verify(board->imageCount()==3,"embedded image reload");
            const int imageCountBefore=board->imageCount();
            board->removeSelectedImage();
            // Loaded documents intentionally have no selection.
            verify(board->imageCount()==imageCountBefore,"remove without selection no-op");

            // Exercise the left image library and actual insertion command.
            w.addImagePathToLibrary(image1);
            w.addImagePathToLibrary(image2);
            auto* library=w.findChild<QListWidget*>("imageLibrary");
            auto* insertButton=w.findChild<QPushButton*>("insertImageButton");
            verify(library && library->count()==2,"gallery has thumbnails");
            verify(insertButton!=nullptr,"insertion button exists");
            if (library && insertButton) {
                QApplication::processEvents();
                auto* imagePanel=w.findChild<QWidget*>("imagePanel");
                auto* imageProps=w.findChild<QWidget*>("imagePropertiesPanel");
                auto* title=w.findChild<QLabel*>("sectionTitle");
                auto* intro=w.findChild<QLabel*>("sectionText");
                verify(imagePanel && imagePanel->isVisible(), "image panel visible");
                verify(!w.findChild<QScrollArea*>("imagePanel"),
                       "no nested outer scroll area in Images");
                verify(title && !title->isVisible() && intro && !intro->isVisible(),
                       "redundant image heading and description hidden");
                verify(imageProps && !imageProps->isVisible(),
                       "image properties initially hidden");
                verify(library->viewMode()==QListView::IconMode &&
                       library->iconSize().width()>=200 &&
                       library->iconSize().height()>=110,"large preview icon mode");
                for (int i=0;i<library->count();++i) {
                    const auto* item=library->item(i);
                    verify(item->text().isEmpty(),"no inline image filename");
                    const QString path=item->data(Qt::UserRole).toString();
                    verify(item->toolTip().contains(QFileInfo(path).fileName()) &&
                           item->toolTip().contains(path),"tooltip contains filename and path");
                    verify(!item->icon().isNull(),"preview icon is present");
                }
                const bool imageListScroll=library->verticalScrollBar()->isVisible();
                qInfo() << "F04.2 image gallery:"
                        << "icons="<<library->iconSize()
                        << "viewport="<<library->viewport()->size()
                        << "scrollbar shown="<<imageListScroll;
                verify(!imageListScroll,"two example images fit without scrollbar");
                verify(w.grab().save(output+"/f04_2_gallery_previews.png","PNG"),
                       "preview-only image library screenshot");
                library->setCurrentRow(0);
                insertButton->click();
                drag(QPointF(170,770),QPointF(610,1010));
                verify(board->imageCount()==4,"gallery item placed by pointer drag");
                auto* aspect=w.findChild<QCheckBox*>("imageAspectLock");
                auto* fit=w.findChild<QComboBox*>("imageFitMode");
                auto* opacity=w.findChild<QSlider*>("imageOpacity");
                verify(aspect && fit && opacity,"image controls present");
                if (aspect && fit && opacity) {
                    aspect->setChecked(false);
                    fit->setCurrentIndex(1);
                    opacity->setValue(65);
                    verify(!board->selectedImage().lockAspect &&
                           board->selectedImage().fillCrop &&
                           std::abs(board->selectedImage().opacity-0.65)<0.01,
                           "sidebar image property controls");
                }
                if (imageProps) {
                    verify(imageProps->isVisible(),"image properties visible when selected");
                    const auto* propTitle=w.findChild<QLabel*>("imagePropertiesHeading");
                    verify(propTitle && propTitle->text()=="Proprietà immagine",
                           "short properties caption");
                    verify(propTitle && propTitle->toolTip().contains("f03_test_chart"),
                           "properties caption exposes name only in tooltip");
                    verify(!library->verticalScrollBar()->isVisible() ||
                           library->verticalScrollBar()->width()<=12,
                           "only a narrow gallery scrollbar if necessary");
                }
                w.setLanguage("en_US",false);
                verify(insertButton->text()=="Insert","gallery English label");
                w.setLanguage("it_IT",false);
                verify(insertButton->text()=="Inserisci","gallery Italian label");
                QApplication::processEvents();
                verify(w.grab().save(output+"/f03_gallery_controls.png","PNG"),
                       "gallery controls screenshot");
            }
            const QString older=QDir(root).absoluteFilePath("results/f02_sample.gxwb");
            if (QFileInfo::exists(older)) {
                verify(board->loadDocument(older,&error),"legacy F02 file loads");
                verify(board->imageCount()==0,"legacy file has no images");
            }
            // Drag-and-drop local image URL into the QGraphicsView viewport.
            QMimeData localImage;
            localImage.setUrls({QUrl::fromLocalFile(image2)});
            const QPoint dropPoint=board->mapFromScene(QPointF(1300,730));
            QDragEnterEvent dragEnter(dropPoint,Qt::CopyAction,&localImage,
                                      Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(board->viewport(),&dragEnter);
            const int beforeDrop=board->imageCount();
            QDropEvent drop(QPointF(dropPoint),Qt::CopyAction,&localImage,
                            Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(board->viewport(),&drop);
            verify(board->imageCount()==beforeDrop+1,
                   "drop image URL directly on canvas");
            verify(w.grab().save(output+"/f03_dragdrop.png","PNG"),
                   "drag-drop screenshot");
            qInfo().noquote() << "F03 IMAGE SMOKE:"
                << (okay ? "PASS" : "FAIL")
                << "| import,8 handles,moving,undo/redo,opacity,crop,aspect,"
                << " ordering,pen sizing,embed,export,clear and reload";
            app.exit(okay?0:2);
        });
    }
    if (parser.isSet("smoke")) {
        QTimer::singleShot(250, &app, [&app, &w]() {
            const QString output = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../results");
            QDir().mkpath(output);
            bool passed = true;
            w.setBoardMode(BoardView::Background::White);
            w.setLanguage("it_IT", false);
            QApplication::processEvents();
            passed &= w.sidebarHeading() == "LIBRERIA";
            passed &= w.gridOptionText() == "Griglia";
            passed &= w.grab().save(output + "/f02_it.png", "PNG");
            w.setLanguage("en_US", false);
            QApplication::processEvents();
            passed &= w.sidebarHeading() == "LIBRARY";
            passed &= w.gridOptionText() == "Grid";
            passed &= w.grab().save(output + "/f02_en.png", "PNG");
            // Exercise both first-row clickable actions, not the mouse wheel.
            auto* zoomPlus = w.findChild<QAction*>("zoomInAction");
            auto* zoomMinus = w.findChild<QAction*>("zoomOutAction");
            passed &= zoomPlus && zoomMinus;
            if (zoomPlus && zoomMinus) {
                const qreal before = w.boardForTest()->viewScale();
                w.boardForTest()->setWheelZoomEnabled(false);
                zoomPlus->trigger();
                const qreal enlarged = w.boardForTest()->viewScale();
                passed &= enlarged > before + 0.001;
                passed &= w.grab().save(output + "/f02_zoom_buttons.png", "PNG");
                zoomMinus->trigger();
                passed &= std::abs(w.boardForTest()->viewScale()-before) < 0.0002;
                w.boardForTest()->setWheelZoomEnabled(true);
                qInfo() << "First-row zoom buttons" << before << enlarged
                        << w.boardForTest()->viewScale();
            }
            const auto* drawingBar = w.findChild<QToolBar*>("drawingToolbar");
            passed &= drawingBar && w.drawingToolsOpen();
            w.setDrawingToolsOpen(false);
            QApplication::processEvents();
            passed &= !w.drawingToolsOpen();
            passed &= drawingBar && !drawingBar->isVisible();
            passed &= w.grab().save(output + "/f02_toolbar_hidden.png", "PNG");
            w.setPresentationMode(true);
            w.setPresentationMode(false);
            passed &= !w.drawingToolsOpen();
            passed &= drawingBar && !drawingBar->isVisible();
            w.setDrawingToolsOpen(true);
            QApplication::processEvents();
            passed &= w.drawingToolsOpen();
            passed &= drawingBar && drawingBar->isVisible();
            passed &= w.grab().save(output + "/f02_toolbar_shown.png", "PNG");
            w.setSidebarOpen(false);
            passed &= !w.sidebarOpen();
            w.setSidebarOpen(true);
            passed &= w.sidebarOpen();
            w.setBoardMode(BoardView::Background::Grid);
            w.setPresentationMode(true);
            QApplication::processEvents();
            passed &= w.presentationMode();
            passed &= w.grab().save(output + "/f02_fullscreen.png", "PNG");
            w.setPresentationMode(false);
            QApplication::processEvents();
            passed &= !w.presentationMode();
            passed &= w.grab().save(output + "/f02_grid.png", "PNG");
            // Before the first stroke edit commands have no applicable document content.
            passed &= w.allEditActionsDisabled();
            // Accept settings in Italian, capture modal and verify persistence.
            w.setLanguage("it_IT", false);
            QTimer::singleShot(0, &app, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "configurationDialog") {
                    passed = false;
                    if (dialog) dialog->reject();
                    return;
                }
                auto* language = dialog->findChild<QComboBox*>("settingsLanguage");
                auto* startup = dialog->findChild<QComboBox*>("settingsStartupView");
                auto* background = dialog->findChild<QComboBox*>("settingsBackground");
                auto* sidebar = dialog->findChild<QCheckBox*>("settingsSidebar");
                auto* mousePan = dialog->findChild<QComboBox*>("settingsMousePan");
                auto* penSide = dialog->findChild<QComboBox*>("settingsPenSide");
                auto* wheelZoom = dialog->findChild<QCheckBox*>("settingsWheelZoom");
                passed &= language && startup && background && sidebar
                          && mousePan && penSide && wheelZoom;
                passed &= dialog->grab().save(output + "/f02_configuration_it.png", "PNG");
                if (language && startup && background && sidebar) {
                    language->setCurrentIndex(0);
                    startup->setCurrentIndex(1);
                    background->setCurrentIndex(1);
                    sidebar->setChecked(false);
                    if (mousePan && penSide && wheelZoom) {
                        mousePan->setCurrentIndex(mousePan->findData(0));
                        penSide->setCurrentIndex(penSide->findData(1));
                        wheelZoom->setChecked(false);
                    }
                }
                dialog->accept();
            });
            w.openConfiguration();
            QSettings persisted("GiuxLab", "GXWhiteboardStudio");
            persisted.sync();
            passed &= persisted.value("language").toString() == "it_IT";
            passed &= persisted.value("startupFullscreen").toBool();
            passed &= persisted.value("mousePanMode").toInt() == 0;
            passed &= persisted.value("penSideMode").toInt() == 1;
            passed &= !persisted.value("wheelZoomEnabled").toBool();
            passed &= w.boardForTest()->mousePanMode() == BoardView::MousePanMode::Right;
            passed &= w.boardForTest()->penSideMode() == BoardView::PenSideMode::Eraser;
            passed &= !w.boardForTest()->wheelZoomEnabled();
            passed &= persisted.value("background").toInt() == 1;
            passed &= !persisted.value("sidebarVisible").toBool();
            passed &= !w.sidebarOpen();
            passed &= w.language() == "it_IT";
            passed &= w.defaultFullscreen();
            // Cancel settings: modifications must not affect saved configuration.
            w.setLanguage("en_US", false);
            QTimer::singleShot(0, &app, [&]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (!dialog || dialog->objectName() != "configurationDialog") {
                    passed = false;
                    if (dialog) dialog->reject();
                    return;
                }
                auto* language = dialog->findChild<QComboBox*>("settingsLanguage");
                auto* startup = dialog->findChild<QComboBox*>("settingsStartupView");
                auto* background = dialog->findChild<QComboBox*>("settingsBackground");
                auto* sidebar = dialog->findChild<QCheckBox*>("settingsSidebar");
                auto* mousePan = dialog->findChild<QComboBox*>("settingsMousePan");
                auto* penSide = dialog->findChild<QComboBox*>("settingsPenSide");
                auto* wheelZoom = dialog->findChild<QCheckBox*>("settingsWheelZoom");
                passed &= language && startup && background && sidebar
                          && mousePan && penSide && wheelZoom;
                passed &= dialog->grab().save(output + "/f02_configuration_en.png", "PNG");
                if (language && startup && background && sidebar) {
                    language->setCurrentIndex(1);
                    startup->setCurrentIndex(0);
                    background->setCurrentIndex(0);
                    sidebar->setChecked(true);
                    if (mousePan && penSide && wheelZoom) {
                        mousePan->setCurrentIndex(mousePan->findData(3));
                        penSide->setCurrentIndex(penSide->findData(0));
                        wheelZoom->setChecked(true);
                    }
                }
                dialog->reject();
            });
            w.openConfiguration();
            persisted.sync();
            passed &= persisted.value("language").toString() == "it_IT";
            passed &= persisted.value("startupFullscreen").toBool();
            passed &= persisted.value("background").toInt() == 1;
            passed &= !persisted.value("sidebarVisible").toBool();
            passed &= persisted.value("mousePanMode").toInt() == 0;
            passed &= persisted.value("penSideMode").toInt() == 1;
            passed &= !persisted.value("wheelZoomEnabled").toBool();
            // Restore defaults for navigation regression: user settings stay
            // unchanged in this disposable test-only configuration directory.
            w.boardForTest()->setMousePanMode(BoardView::MousePanMode::Both);
            w.boardForTest()->setPenSideMode(BoardView::PenSideMode::Pan);
            w.boardForTest()->setWheelZoomEnabled(true);
            // Start-up preference is applied explicitly after the window is shown.
            w.applyStartupMode();
            QApplication::processEvents();
            passed &= w.presentationMode();
            w.setPresentationMode(false);
            auto* board = w.boardForTest();
            w.setLanguage("it_IT", false);
            w.setBoardMode(BoardView::Background::White);
            w.setSidebarOpen(true);
            board->setStrokeColor(QColor("#c13737"));
            board->setStrokeWidth(12);
            passed &= !board->hasStrokes();
            passed &= board->addSyntheticStroke(QPointF(250,240), QPointF(620,510), 0.3);
            passed &= board->strokeCount() == 1;
            passed &= board->undoStack()->canUndo();
            board->undoStack()->undo();
            passed &= board->strokeCount() == 0;
            board->undoStack()->redo();
            passed &= board->strokeCount() == 1;
            board->selectAllStrokes();
            passed &= board->hasSelection();
            board->copySelection();
            board->pasteSelection();
            passed &= board->strokeCount() == 2;
            board->duplicateSelection();
            passed &= board->strokeCount() == 3;
            board->deleteSelection();
            passed &= board->strokeCount() == 2;
            board->undoStack()->undo();
            passed &= board->strokeCount() == 3;
            board->undoStack()->redo();
            passed &= board->strokeCount() == 2;
            board->insertTextAt(QPointF(300, 700), "GX WHITEBOARD - F02");
            passed &= board->strokeCount() == 3;
            passed &= board->exportPng(output + "/f02_export.png");
            passed &= board->saveDocument(output + "/f02_sample.gxwb");
            board->clearBoard();
            passed &= board->strokeCount() == 0;
            QString loadError;
            passed &= board->loadDocument(output + "/f02_sample.gxwb", &loadError);
            if (!loadError.isEmpty()) qWarning().noquote() << "F02 reload error:" << loadError;
            passed &= board->strokeCount() == 3;

            // Exercise the viewport event route using ordinary mouse events.
            // Physical QTabletEvent data needs a real device test (user gate);
            // constructing fake tablet devices destabilizes Qt's global registry.
            const QPointF mStart = board->mapFromScene(QPointF(820, 310));
            const QPointF mMiddle = board->mapFromScene(QPointF(920, 350));
            const QPointF mEnd = board->mapFromScene(QPointF(1000, 395));
            const auto globalPoint = [board](const QPointF& local) {
                return QPointF(board->viewport()->mapToGlobal(local.toPoint()));
            };
            auto deliverMouse = [board, &globalPoint](
                QEvent::Type eventType, const QPointF& local,
                Qt::MouseButton button, Qt::MouseButtons buttons) {
                QMouseEvent event(eventType, local, globalPoint(local),
                    button, buttons, Qt::NoModifier);
                return QCoreApplication::sendEvent(board->viewport(), &event);
            };
            board->setTool(BoardView::Tool::Pen);
            const int beforeMouse = board->strokeCount();
            deliverMouse(QEvent::MouseButtonPress, mStart, Qt::LeftButton, Qt::LeftButton);
            deliverMouse(QEvent::MouseMove, mMiddle, Qt::NoButton, Qt::LeftButton);
            deliverMouse(QEvent::MouseButtonRelease, mEnd, Qt::LeftButton, Qt::NoButton);
            const int afterMouse = board->strokeCount();
            passed &= afterMouse == beforeMouse + 1;
            qInfo() << "F02 mouse-driven pen: before" << beforeMouse
                    << "after" << afterMouse;
            board->addSyntheticStroke(QPointF(1000,650), QPointF(1300,650), 0.7);
            const int beforeErase = board->strokeCount();
            const QPointF eraseHere = board->mapFromScene(QPointF(1150,650));
            board->setTool(BoardView::Tool::Eraser);
            deliverMouse(QEvent::MouseButtonPress, eraseHere, Qt::LeftButton, Qt::LeftButton);
            deliverMouse(QEvent::MouseButtonRelease, eraseHere, Qt::LeftButton, Qt::NoButton);
            const int afterErase = board->strokeCount();
            passed &= afterErase == beforeErase - 1;
            board->undoStack()->undo();
            passed &= board->strokeCount() == beforeErase;
            board->undoStack()->redo();
            passed &= board->strokeCount() == afterErase;
            board->setTool(BoardView::Tool::Pen);
            qInfo() << "F02 mouse-driven eraser: before" << beforeErase
                    << "after" << afterErase;
            // Drag an already-selected stroke while holding the mouse button;
            // the same sequence is used by physical QTabletEvent input.
            board->setTool(BoardView::Tool::Pen);
            board->addSyntheticStroke(QPointF(1400, 450), QPointF(1600, 450), 0.7);
            board->deselect();
            const QString beforeDragFile = output + "/f02_drag_before.gxwb";
            const QString movedFile = output + "/f02_drag_moved.gxwb";
            const QString undoneFile = output + "/f02_drag_undone.gxwb";
            const QString redoneFile = output + "/f02_drag_redone.gxwb";
            passed &= board->saveDocument(beforeDragFile);
            auto readStarts = [](const QString& path) -> QVector<QPointF> {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) return {};
                const QJsonArray strokes = QJsonDocument::fromJson(file.readAll())
                    .object().value("strokes").toArray();
                QVector<QPointF> starts;
                for (const auto& item : strokes) {
                    const auto point = item.toObject().value("start").toArray();
                    if (point.size() != 2) return {};
                    starts.push_back(QPointF(point.at(0).toDouble(), point.at(1).toDouble()));
                }
                return starts;
            };
            const auto initial = readStarts(beforeDragFile);
            board->setTool(BoardView::Tool::Select);
            const QPointF selectAt = board->mapFromScene(QPointF(1500, 450));
            const QPointF dragTo = board->mapFromScene(QPointF(1550, 510));
            // First select by clicking once, then press again and drag.
            deliverMouse(QEvent::MouseButtonPress, selectAt, Qt::LeftButton, Qt::LeftButton);
            deliverMouse(QEvent::MouseButtonRelease, selectAt, Qt::LeftButton, Qt::NoButton);
            passed &= board->hasSelection();
            deliverMouse(QEvent::MouseButtonPress, selectAt, Qt::LeftButton, Qt::LeftButton);
            deliverMouse(QEvent::MouseMove, dragTo, Qt::NoButton, Qt::LeftButton);
            deliverMouse(QEvent::MouseButtonRelease, dragTo, Qt::LeftButton, Qt::NoButton);
            passed &= board->hasSelection();
            passed &= board->saveDocument(movedFile);
            const auto moved = readStarts(movedFile);
            passed &= !initial.isEmpty() && moved.size() == initial.size();
            if (moved.size() == initial.size() && !moved.isEmpty()) {
                const QPointF delta = moved.last() - initial.last();
                passed &= QLineF(delta, QPointF(0,0)).length() > 1.0;
                // Other strokes must not move when dragging a single selection.
                for (qsizetype i=0; i+1<initial.size(); ++i)
                    passed &= moved[i] == initial[i];
            }
            board->undoStack()->undo();
            passed &= board->saveDocument(undoneFile);
            passed &= readStarts(undoneFile) == initial;
            board->undoStack()->redo();
            passed &= board->saveDocument(redoneFile);
            passed &= readStarts(redoneFile) == moved;
            // Select all and drag the entire group as a *single* undo command.
            board->selectAllStrokes();
            const QPointF groupStart = board->mapFromScene(QPointF(1550, 510));
            const QPointF groupEnd = board->mapFromScene(QPointF(1575, 550));
            deliverMouse(QEvent::MouseButtonPress, groupStart, Qt::LeftButton, Qt::LeftButton);
            deliverMouse(QEvent::MouseMove, groupEnd, Qt::NoButton, Qt::LeftButton);
            deliverMouse(QEvent::MouseButtonRelease, groupEnd, Qt::LeftButton, Qt::NoButton);
            const QString groupMovedFile = output + "/f02_group_moved.gxwb";
            passed &= board->saveDocument(groupMovedFile);
            const auto groupMoved = readStarts(groupMovedFile);
            passed &= groupMoved.size() == moved.size();
            if (groupMoved.size() == moved.size() && !groupMoved.isEmpty()) {
                const QPointF delta = groupMoved.last() - moved.last();
                passed &= QLineF(delta, QPointF(0,0)).length() > 1.0;
                for (qsizetype i=0; i<moved.size(); ++i)
                    passed &= QLineF(groupMoved[i] - moved[i], delta).length() < 0.01;
            }
            board->undoStack()->undo();
            passed &= board->saveDocument(output + "/f02_group_undone.gxwb");
            passed &= readStarts(output + "/f02_group_undone.gxwb") == moved;
            board->undoStack()->redo();
            passed &= board->saveDocument(output + "/f02_group_redone.gxwb");
            passed &= readStarts(output + "/f02_group_redone.gxwb") == groupMoved;
            qInfo().noquote() << "F02 selection drag:"
                << (passed ? "PASS" : "FAIL")
                << "| single and group move, geometry persistence, undo/redo";
            QApplication::processEvents();
            // Navigation regression. The world exceeds 1920x1080, but Ctrl+0
            // must still fit the reference video/output frame.
            bool navPassed = true;
            auto navigationCheck = [&navPassed](bool condition, const char* label) {
                if (!condition) qWarning() << "F02 NAV FAIL" << label;
                navPassed &= condition;
            };
            board->fitBoard();
            const QPointF fitCenter = board->viewCenter();
            const qreal fitScale = board->viewScale();
            navigationCheck(QLineF(fitCenter, QPointF(960,540)).length() < 20,
                            "initial 1920x1080 fit");
            const QPoint anchor(board->viewport()->width()/3,
                                board->viewport()->height()/3);
            const QPointF anchorBefore = board->mapToScene(anchor);
            board->zoomAt(1.62, anchor);
            QApplication::processEvents();
            navigationCheck(board->viewScale() > fitScale * 1.5, "zoom scale");
            navigationCheck(QLineF(anchorBefore, board->mapToScene(anchor)).length() < 6,
                            "cursor-anchor zoom");
            const qreal scaled = board->viewScale();
            w.setDrawingToolsOpen(false);
            QApplication::processEvents();
            navigationCheck(!w.drawingToolsOpen(), "hide drawing toolbar");
            navigationCheck(std::abs(board->viewScale()-scaled) < 0.001,
                            "hide toolbar preserves zoom");
            w.setDrawingToolsOpen(true);
            QApplication::processEvents();
            navigationCheck(std::abs(board->viewScale()-scaled) < 0.001,
                            "show toolbar preserves zoom");
            w.setPresentationMode(true);
            QApplication::processEvents();
            w.setPresentationMode(false);
            QApplication::processEvents();
            navigationCheck(std::abs(board->viewScale()-scaled) < 0.001,
                            "fullscreen retains zoom");

            // Right/middle button pannings must never create marks.
            const QPointF panFrom(board->viewport()->width()/2,
                                  board->viewport()->height()/2);
            const QPointF panTo = panFrom + QPointF(135, 95);
            const QPointF rightBefore = board->viewCenter();
            const int strokesBeforePan = board->strokeCount();
            deliverMouse(QEvent::MouseButtonPress, panFrom, Qt::RightButton, Qt::RightButton);
            deliverMouse(QEvent::MouseMove, panTo, Qt::NoButton, Qt::RightButton);
            deliverMouse(QEvent::MouseButtonRelease, panTo, Qt::RightButton, Qt::NoButton);
            navigationCheck(QLineF(rightBefore, board->viewCenter()).length() > 50,
                            "right mouse pan");
            navigationCheck(board->strokeCount() == strokesBeforePan,
                            "right pan does not draw");
            const QPointF middleBefore = board->viewCenter();
            deliverMouse(QEvent::MouseButtonPress, panFrom, Qt::MiddleButton, Qt::MiddleButton);
            deliverMouse(QEvent::MouseMove, panFrom+QPointF(-70,-50), Qt::NoButton,
                         Qt::MiddleButton);
            deliverMouse(QEvent::MouseButtonRelease, panFrom+QPointF(-70,-50),
                         Qt::MiddleButton, Qt::NoButton);
            navigationCheck(QLineF(middleBefore, board->viewCenter()).length() > 30,
                            "middle mouse pan");
            const qreal beforeWheel = board->viewScale();
            QWheelEvent wheel(QPointF(anchor), QPointF(board->viewport()->mapToGlobal(anchor)),
                QPoint(), QPoint(0,120), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
            QCoreApplication::sendEvent(board->viewport(), &wheel);
            navigationCheck(board->viewScale() > beforeWheel, "wheel zoom");
            const qreal wheelAfter = board->viewScale();
            board->setWheelZoomEnabled(false);
            QWheelEvent blockedWheel(QPointF(anchor), QPointF(board->viewport()->mapToGlobal(anchor)),
                QPoint(), QPoint(0,120), Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
            QCoreApplication::sendEvent(board->viewport(), &blockedWheel);
            navigationCheck(std::abs(wheelAfter - board->viewScale()) < 0.000001,
                            "disabled wheel zoom");
            board->setWheelZoomEnabled(true);
            board->setMousePanMode(BoardView::MousePanMode::None);
            const QPointF disabledBefore = board->viewCenter();
            deliverMouse(QEvent::MouseButtonPress, panFrom, Qt::RightButton, Qt::RightButton);
            deliverMouse(QEvent::MouseMove, panTo, Qt::NoButton, Qt::RightButton);
            deliverMouse(QEvent::MouseButtonRelease, panTo, Qt::RightButton, Qt::NoButton);
            navigationCheck(QLineF(disabledBefore, board->viewCenter()).length() < 2,
                            "disabled mouse pan");
            board->setMousePanMode(BoardView::MousePanMode::Right);
            const QPointF rightOnlyBefore = board->viewCenter();
            deliverMouse(QEvent::MouseButtonPress, panFrom, Qt::MiddleButton, Qt::MiddleButton);
            deliverMouse(QEvent::MouseMove, panTo, Qt::NoButton, Qt::MiddleButton);
            deliverMouse(QEvent::MouseButtonRelease, panTo, Qt::MiddleButton, Qt::NoButton);
            navigationCheck(QLineF(rightOnlyBefore, board->viewCenter()).length() < 2,
                            "right-only ignores middle");
            board->setMousePanMode(BoardView::MousePanMode::Both);

            // Panning/zooming are view-only; creating a stroke beyond the reference
            // frame must be allowed and save/reload without clamping or loss.
            board->setTool(BoardView::Tool::Pen);
            const int countBeforeOut = board->strokeCount();
            navigationCheck(board->addSyntheticStroke(QPointF(2700,1720),
                    QPointF(3150,1920), 0.8), "stroke outside reference frame");
            navigationCheck(board->strokeCount() == countBeforeOut+1,
                            "new offscreen stroke exists");
            board->panByPixels(QPointF(-700, -420));
            const QPointF savedCenter = board->viewCenter();
            const qreal savedScale = board->viewScale();
            const QString extendedDoc = output + "/f02_extended_workspace.gxwb";
            navigationCheck(board->saveDocument(extendedDoc), "save extended project");
            navigationCheck(w.grab().save(output + "/f02_workspace_navigated.png", "PNG"),
                            "screenshot of off-frame view");
            navigationCheck(board->exportViewPng(output + "/f02_current_view.png"),
                            "export current view");
            board->fitBoard();
            navigationCheck(QLineF(savedCenter,board->viewCenter()).length() > 200,
                            "fit resets pan");
            QString extendedError;
            navigationCheck(board->loadDocument(extendedDoc, &extendedError),
                            "reload extended project");
            navigationCheck(board->strokeCount()==countBeforeOut+1,
                            "offscreen content roundtrip");
            navigationCheck(QLineF(savedCenter,board->viewCenter()).length() < 8,
                            "view pan restored");
            navigationCheck(std::abs(savedScale-board->viewScale()) < 0.000001,
                            "zoom restored");
            board->fitBoard();
            navigationCheck(QLineF(board->viewCenter(),QPointF(960,540)).length() < 20,
                            "Ctrl+0 fits reference frame after extended drawing");
            navigationCheck(std::abs(board->viewScale()-fitScale) < 0.1,
                            "reset zoom fits output");
            qInfo().noquote() << "F02 NAVIGATION:" << (navPassed ? "PASS" : "FAIL")
                << "| pan mouse right/middle, zoom/wheel, anchor,"
                << " viewport/toolbar/fullscreen, off-frame drawing and persistence";
            passed &= navPassed;
            QApplication::processEvents();
            passed &= w.grab().save(output + "/f02_drawings.png", "PNG");
            qInfo().noquote() << "F02 SMOKE:" << (passed ? "PASS" : "FAIL")
                << "| menu+settings, IT/EN, fullscreen, stroke add/undo/redo,"
                << " select/copy/paste/duplicate/delete, text, JSON save/load, PNG export,"
                << " viewport mouse input, toolbar visibility/persistence/fullscreen,"
                << " selection drag single/group undo-redo (physical tablet user test pending)";
            app.exit(passed ? 0 : 2);
            app.exit(passed ? 0 : 2);
        });
    }
    return app.exec();
}
