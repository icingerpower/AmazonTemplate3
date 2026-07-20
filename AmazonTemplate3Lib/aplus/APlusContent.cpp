#include "APlusContent.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>

namespace {
constexpr const char *kIndexFileName = "index.json";

QString typeToString(APlusElementType t)
{
    switch (t) {
    case APlusElementType::SizeChart: return QStringLiteral("size_chart");
    case APlusElementType::Image:     return QStringLiteral("image");
    case APlusElementType::Faq:       return QStringLiteral("faq");
    }
    return QStringLiteral("image");
}

APlusElementType typeFromString(const QString &s)
{
    if (s == QLatin1String("size_chart")) return APlusElementType::SizeChart;
    if (s == QLatin1String("faq"))        return APlusElementType::Faq;
    return APlusElementType::Image;
}
} // namespace

const APlusVersion *APlusElement::current() const
{
    if (versions.isEmpty())
        return nullptr;
    return &versions.first();
}

APlusVersion *APlusElement::current()
{
    if (versions.isEmpty())
        return nullptr;
    return &versions.first();
}

APlusContent::APlusContent(QObject *parent)
    : QObject(parent)
{
}

void APlusContent::setDir(const QDir &aplusDir)
{
    m_dir = aplusDir;
}

QDir APlusContent::dir() const
{
    return m_dir;
}

int APlusContent::pruneOrphanFiles() const
{
    static const QStringList kImgGlobs{QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                                       QStringLiteral("*.jpeg")};
    int removed = 0;
    for (const APlusElement &el : m_elements) {
        if (el.id.isEmpty())
            continue;
        QDir elemDir(m_dir.filePath(el.id));
        if (!elemDir.exists())
            continue;
        // Files this element's version history still needs (by basename).
        QSet<QString> keep;
        for (const APlusVersion &v : el.versions) {
            if (!v.desktopFile.isEmpty()) keep.insert(QFileInfo(v.desktopFile).fileName());
            if (!v.mobileFile.isEmpty())  keep.insert(QFileInfo(v.mobileFile).fileName());
        }
        for (const QString &f : elemDir.entryList(kImgGlobs, QDir::Files)) {
            if (!keep.contains(f) && QFile::remove(elemDir.filePath(f)))
                ++removed;
        }
    }
    return removed;
}

const QList<APlusElement> &APlusContent::elements() const
{
    return m_elements;
}

QList<APlusElement> &APlusContent::elements()
{
    return m_elements;
}

APlusElement *APlusContent::findElement(const QString &id)
{
    for (auto &e : m_elements) {
        if (e.id == id)
            return &e;
    }
    return nullptr;
}

const APlusElement *APlusContent::findElement(const QString &id) const
{
    for (const auto &e : m_elements) {
        if (e.id == id)
            return &e;
    }
    return nullptr;
}

int APlusContent::indexOf(const QString &id) const
{
    for (int i = 0; i < m_elements.size(); ++i) {
        if (m_elements.at(i).id == id)
            return i;
    }
    return -1;
}

bool APlusContent::load()
{
    m_elements.clear();

    const QString path = m_dir.filePath(QString::fromLatin1(kIndexFileName));
    QFile f(path);
    if (!f.exists())
        return false;
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "APlusContent: failed to open" << path;
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    const QJsonArray elements = root.value(QStringLiteral("elements")).toArray();
    for (const QJsonValue &elemVal : elements) {
        if (!elemVal.isObject())
            continue;
        const QJsonObject elemObj = elemVal.toObject();

        APlusElement e;
        e.type        = typeFromString(elemObj.value(QStringLiteral("type")).toString());
        e.id          = elemObj.value(QStringLiteral("id")).toString();
        e.displayName = elemObj.value(QStringLiteral("name")).toString();

        const QJsonArray versions = elemObj.value(QStringLiteral("versions")).toArray();
        for (const QJsonValue &vVal : versions) {
            if (!vVal.isObject())
                continue;
            const QJsonObject vObj = vVal.toObject();
            APlusVersion v;
            v.generated   = QDateTime::fromString(
                vObj.value(QStringLiteral("generated")).toString(), Qt::ISODate);
            v.desktopFile = vObj.value(QStringLiteral("desktop")).toString();
            v.mobileFile  = vObj.value(QStringLiteral("mobile")).toString();
            e.versions.append(v);
        }
        m_elements.append(e);
    }
    return true;
}

void APlusContent::save() const
{
    if (!m_dir.exists())
        m_dir.mkpath(QStringLiteral("."));

    QJsonArray elements;
    for (const APlusElement &e : m_elements) {
        QJsonObject elemObj;
        elemObj.insert(QStringLiteral("type"), typeToString(e.type));
        elemObj.insert(QStringLiteral("id"),   e.id);
        elemObj.insert(QStringLiteral("name"), e.displayName);

        QJsonArray versions;
        for (const APlusVersion &v : e.versions) {
            QJsonObject vObj;
            vObj.insert(QStringLiteral("generated"),
                        v.generated.isValid()
                            ? v.generated.toString(Qt::ISODate) : QString());
            vObj.insert(QStringLiteral("desktop"), v.desktopFile);
            vObj.insert(QStringLiteral("mobile"),  v.mobileFile);
            versions.append(vObj);
        }
        elemObj.insert(QStringLiteral("versions"), versions);
        elements.append(elemObj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("elements"), elements);

    const QString path = m_dir.filePath(QString::fromLatin1(kIndexFileName));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "APlusContent: failed to write" << path;
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
}

void APlusContent::pushVersion(const QString &id, APlusElementType type,
                               const QString &displayName, const APlusVersion &ver)
{
    APlusElement *e = findElement(id);
    if (!e) {
        APlusElement newElem;
        newElem.type        = type;
        newElem.id          = id;
        newElem.displayName = displayName;
        m_elements.append(newElem);
        e = &m_elements.last();
        emit layoutChanged();
    }
    e->versions.prepend(ver);
    save();
    emit elementChanged(id);
}

void APlusContent::setSingleVersion(const QString &id, APlusElementType type,
                                    const QString &displayName, const APlusVersion &ver)
{
    APlusElement *e = findElement(id);
    if (!e) {
        APlusElement newElem;
        newElem.type        = type;
        newElem.id          = id;
        newElem.displayName = displayName;
        m_elements.append(newElem);
        e = &m_elements.last();
        emit layoutChanged();
    }
    e->versions = {ver};
    save();
    emit elementChanged(id);
}

void APlusContent::deleteVersion(const QString &id, int versionIndex)
{
    APlusElement *e = findElement(id);
    if (!e)
        return;
    if (versionIndex < 0 || versionIndex >= e->versions.size())
        return;
    e->versions.removeAt(versionIndex);
    save();
    emit elementChanged(id);
}

void APlusContent::clearVersionFile(const QString &id, int versionIndex, bool desktop)
{
    APlusElement *e = findElement(id);
    if (!e || versionIndex < 0 || versionIndex >= e->versions.size()) return;
    APlusVersion &ver = e->versions[versionIndex];
    const QString rel = desktop ? ver.desktopFile : ver.mobileFile;
    if (!rel.isEmpty())
        QFile::remove(m_dir.filePath(rel));
    if (desktop) ver.desktopFile.clear();
    else         ver.mobileFile.clear();
    if (ver.desktopFile.isEmpty() && ver.mobileFile.isEmpty())
        e->versions.removeAt(versionIndex);
    save();
    emit elementChanged(id);
}

void APlusContent::promoteVersion(const QString &id, int versionIndex)
{
    APlusElement *e = findElement(id);
    if (!e)
        return;
    if (versionIndex <= 0 || versionIndex >= e->versions.size())
        return;
    const APlusVersion v = e->versions.takeAt(versionIndex);
    e->versions.prepend(v);
    save();
    emit elementChanged(id);
}

void APlusContent::ensureImageElement(int index)
{
    const QString id = QStringLiteral("image_%1")
                           .arg(index + 1, 2, 10, QLatin1Char('0'));
    if (findElement(id))
        return;

    APlusElement e;
    e.type        = APlusElementType::Image;
    e.id          = id;
    e.displayName = tr("A+ Image %1").arg(index + 1);
    m_elements.append(e);
    save();
    emit layoutChanged();
}
