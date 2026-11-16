// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#pragma once
#include "RecordingEngine.h"
#include <QDialog>
#include <functional>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSlider;
class QLabel;

class RecordingDialog final : public QDialog {
public:
    using Translator=std::function<QString(const char*)>;
    explicit RecordingDialog(const QString& suggestedPath,
                             const QString& preferredMicrophone,
                             bool preferMicrophone,
                             int previousVolume,
                             Translator translator,
                             QWidget* parent=nullptr);
    RecordingEngine::Options options() const;
private:
    QString T(const char* source) const {
        return translator_?translator_(source):QString::fromUtf8(source);
    }
    Translator translator_;
    QComboBox* profile_ = nullptr;
    QComboBox* encoder_ = nullptr;
    QComboBox* microphone_ = nullptr;
    QCheckBox* micEnabled_ = nullptr;
    QCheckBox* mp4_ = nullptr;
    QLineEdit* output_ = nullptr;
    QSlider* volume_ = nullptr;
    QLabel* volumeText_ = nullptr;
};
