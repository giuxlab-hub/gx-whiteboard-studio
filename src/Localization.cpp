#include "Localization.h"
#include <QFile>
#include <QByteArray>
#include <QDebug>
#include <QtEndian>
#include <limits>

Localization::Localization(QString catalogRoot): catalogRoot_(std::move(catalogRoot)) {}
QString Localization::localePath() const { return catalogRoot_; }
bool Localization::setLanguage(const QString& locale) {
    QString normalized = locale;
    if (normalized.startsWith("it", Qt::CaseInsensitive)) normalized = "it_IT";
    else normalized = "en_US";
    language_ = normalized;
    strings_.clear();
    const QString code = language_.startsWith("it") ? "it" : "en";
    const QString file = catalogRoot_ + "/" + code + "/LC_MESSAGES/gx-whiteboard-studio.mo";
    return loadMo(file);
}
QString Localization::tr(const char* source) const {
    const QString original = QString::fromUtf8(source);
    return strings_.value(original, original);
}
bool Localization::loadMo(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "Could not open catalog:" << path;
        return false;
    }
    const QByteArray bytes = f.readAll();
    const uchar* raw = reinterpret_cast<const uchar*>(bytes.constData());
    const quint64 len = quint64(bytes.size());
    if (len < 28) return false;
    auto rd = [raw](quint64 o, bool swapped) -> quint32 {
        quint32 v = qFromLittleEndian<quint32>(raw + o);
        return swapped ? qbswap(v) : v;
    };
    const quint32 magic = qFromLittleEndian<quint32>(raw);
    if (magic != 0x950412deu && magic != 0xde120495u) return false;
    const bool swap = magic == 0xde120495u;
    const quint64 count = rd(8, swap);
    const quint64 sourceOffset = rd(12, swap);
    const quint64 translationOffset = rd(16, swap);
    if (count > 100000 || sourceOffset + count * 8 > len ||
        translationOffset + count * 8 > len) return false;
    for (quint64 i = 0; i < count; ++i) {
        const quint64 o = sourceOffset + i * 8, t = translationOffset + i * 8;
        const quint64 keyLength = rd(o, swap), keyOffset = rd(o + 4, swap);
        const quint64 valLength = rd(t, swap), valOffset = rd(t + 4, swap);
        if (keyOffset + keyLength > len || valOffset + valLength > len) return false;
        if (!keyLength || !valLength) continue;
        const QString key = QString::fromUtf8(bytes.constData() + keyOffset, qsizetype(keyLength));
        const QString value = QString::fromUtf8(bytes.constData() + valOffset, qsizetype(valLength));
        strings_.insert(key, value);
    }
    qInfo().noquote() << "Loaded" << strings_.size() << "translations for" << language_ << "from" << path;
    return true;
}
