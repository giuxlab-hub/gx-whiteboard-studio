// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 GiuxLab

#pragma once
#include <QHash>
#include <QString>

class Localization {
public:
    explicit Localization(QString catalogRoot);
    bool setLanguage(const QString& locale);
    QString language() const { return language_; }
    QString tr(const char* source) const;
    QString localePath() const;
private:
    bool loadMo(const QString& path);
    QString catalogRoot_;
    QString language_ = "en_US";
    QHash<QString, QString> strings_;
};
