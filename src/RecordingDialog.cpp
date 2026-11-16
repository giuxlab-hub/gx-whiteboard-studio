#include "RecordingDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QRegularExpression>

RecordingDialog::RecordingDialog(const QString& suggestedPath,
                                 const QString& preferredMic,
                                 bool preferMicrophone,
                                 int previousVolume,
                                 Translator translator,
                                 QWidget* parent)
    : QDialog(parent),translator_(std::move(translator)) {
    setObjectName("recordingDialog");
    setWindowTitle(T("Recording settings"));
    setMinimumWidth(525);
    setModal(true);
    setStyleSheet(R"(
       QDialog {background:#202637;color:#edf4fe;}
       QLabel,QCheckBox {color:#e5edf8;font-size:13px;}
       QLabel#recordingPrivacy {color:#a9bbd0;font-size:11px;}
       QLineEdit,QComboBox {background:#29364b;color:#ffffff;
         border:1px solid #52637e;border-radius:5px;padding:6px;}
       QComboBox QAbstractItemView {background:#29364b;color:#ffffff;}
       QPushButton {color:#f6faff;background:#304663;border:1px solid #56749b;
         border-radius:5px;padding:7px 13px;}
       QPushButton:hover {background:#42658b;}
       QPushButton:disabled {background:#2a3345;color:#8298b4;}
       QSlider::groove:horizontal {height:6px;background:#52637e;}
       QSlider::handle:horizontal {background:#9ac4ee;width:16px;
         margin:-5px 0;border-radius:7px;}
    )");
    auto* layout=new QVBoxLayout(this);
    layout->setContentsMargins(23,20,23,20);
    layout->setSpacing(12);
    auto* form=new QFormLayout();
    form->setSpacing(11);
    form->setLabelAlignment(Qt::AlignRight);

    profile_=new QComboBox(this);
    profile_->setObjectName("recordingProfile");
    profile_->addItem("1920 × 1080 — 30 fps","1920x1080@30");
    profile_->addItem("1280 × 720 — 30 fps","1280x720@30");
    profile_->addItem("1280 × 720 — 15 fps","1280x720@15");
    profile_->addItem("640 × 360 — 15 fps","640x360@15");
    form->addRow(T("Video profile:"),profile_);

    encoder_=new QComboBox(this);
    encoder_->setObjectName("recordingEncoder");
    encoder_->addItem(T("CPU — H.264 libx264"),"libx264");
    if (RecordingEngine::nvencAvailable())
        encoder_->addItem(T("NVIDIA — H.264 NVENC"),"h264_nvenc");
    if (encoder_->count()>1) encoder_->setCurrentIndex(1);
    form->addRow(T("Video encoder:"),encoder_);

    auto* pathRow=new QWidget(this);
    auto* pathLayout=new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0,0,0,0);
    pathLayout->setSpacing(5);
    output_=new QLineEdit(suggestedPath,this);
    output_->setObjectName("recordingOutput");
    pathLayout->addWidget(output_,1);
    auto* browse=new QPushButton(T("Browse..."),this);
    browse->setObjectName("recordingBrowse");
    pathLayout->addWidget(browse);
    connect(browse,&QPushButton::clicked,this,[this] {
        const QString file=QFileDialog::getSaveFileName(
            this,T("Choose recording file"),output_->text(),
            T("Matroska video (*.mkv)"));
        if (!file.isEmpty())
            output_->setText(file.endsWith(".mkv",Qt::CaseInsensitive)
                             ? file:file+".mkv");
    });
    form->addRow(T("Output file:"),pathRow);

    mp4_=new QCheckBox(T("Also create MP4 without re-encoding"),this);
    mp4_->setObjectName("recordingRemuxMp4");
    mp4_->setChecked(true);
    form->addRow(QString(),mp4_);

    micEnabled_=new QCheckBox(T("Record microphone audio"),this);
    micEnabled_->setObjectName("recordingMicEnabled");
    micEnabled_->setChecked(preferMicrophone);
    form->addRow(QString(),micEnabled_);
    microphone_=new QComboBox(this);
    microphone_->setObjectName("recordingMicrophone");
    for (const auto& source: RecordingEngine::listMicrophones())
        microphone_->addItem(source.description,source.name);
    int match=microphone_->findData(preferredMic);
    if (match>=0) microphone_->setCurrentIndex(match);
    if (microphone_->count()==0) {
        microphone_->addItem(T("No microphone input found"),QString());
        micEnabled_->setChecked(false);
        micEnabled_->setEnabled(false);
    }
    microphone_->setEnabled(micEnabled_->isChecked());
    form->addRow(T("Microphone:"),microphone_);
    connect(micEnabled_,&QCheckBox::toggled,microphone_,
            &QWidget::setEnabled);

    auto* volContainer=new QWidget(this);
    auto* volRow=new QHBoxLayout(volContainer);
    volRow->setContentsMargins(0,0,0,0);
    volume_=new QSlider(Qt::Horizontal,volContainer);
    volume_->setObjectName("recordingMicVolume");
    volume_->setRange(0,150);
    volume_->setValue(qBound(0,previousVolume,150));
    volRow->addWidget(volume_);
    volumeText_=new QLabel(QString::number(volume_->value())+"%",volContainer);
    volumeText_->setFixedWidth(40);
    volRow->addWidget(volumeText_);
    connect(volume_,&QSlider::valueChanged,this,[this](int v) {
        volumeText_->setText(QString::number(v)+"%");
    });
    form->addRow(T("Input volume:"),volContainer);
    layout->addLayout(form);

    auto* note=new QLabel(T("The microphone is accessed only after you press Start recording. "
            "No audio is saved in the editable whiteboard project."),this);
    note->setObjectName("recordingPrivacy");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons=new QDialogButtonBox(this);
    auto* start=buttons->addButton(T("Start recording"),QDialogButtonBox::AcceptRole);
    start->setObjectName("recordingStart");
    auto* cancel=buttons->addButton(T("Cancel"),QDialogButtonBox::RejectRole);
    cancel->setObjectName("recordingCancel");
    connect(cancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(start,&QPushButton::clicked,this,[this] {
        const auto opts=options();
        if (opts.microphoneEnabled && opts.microphoneSource.isEmpty()) {
            QMessageBox::warning(this,T("Microphone error"),
                T("Select a microphone or disable audio recording."));
            return;
        }
        const QFileInfo file(opts.output);
        if (file.absolutePath().isEmpty() || !QDir(file.absolutePath()).exists() ||
            file.suffix().toLower()!="mkv") {
            QMessageBox::warning(this,T("Recording path error"),
                T("Choose an existing directory and a .mkv filename."));
            return;
        }
        if (file.exists()) {
            QMessageBox::warning(this,T("Recording path error"),
                T("The output file already exists. Choose another filename."));
            return;
        }
        accept();
    });
    layout->addWidget(buttons);
}
RecordingEngine::Options RecordingDialog::options() const {
    RecordingEngine::Options opts;
    const QString spec=profile_->currentData().toString();
    const QRegularExpression pattern(R"((\d+)x(\d+)@(\d+))");
    const auto parsed=pattern.match(spec);
    if (parsed.hasMatch()) {
        opts.size=QSize(parsed.captured(1).toInt(),parsed.captured(2).toInt());
        opts.fps=parsed.captured(3).toInt();
    }
    opts.output=output_->text().trimmed();
    opts.encoder=encoder_->currentData().toString();
    opts.microphoneEnabled=micEnabled_->isChecked();
    opts.microphoneSource=microphone_->currentData().toString();
    opts.convertToMp4=mp4_->isChecked();
    opts.volumePercent=volume_->value();
    return opts;
}
