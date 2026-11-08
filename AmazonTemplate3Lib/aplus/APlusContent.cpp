#include "APlusContent.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
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

    QDir faqDir(m_dir.filePath(QStringLiteral("faq")));
    if (faqDir.exists()) {
        QSet<QString> keepFaq;
        for (const APlusElement &el : m_elements) {
            if (el.type != APlusElementType::Faq)
                continue;
            for (const APlusVersion &v : el.versions) {
                if (!v.desktopFile.isEmpty()) keepFaq.insert(QFileInfo(v.desktopFile).fileName());
                if (!v.mobileFile.isEmpty())  keepFaq.insert(QFileInfo(v.mobileFile).fileName());
            }
        }
        for (const QString &f : faqDir.entryList({QStringLiteral("*.txt")}, QDir::Files)) {
            if (!keepFaq.contains(f) && QFile::remove(faqDir.filePath(f)))
                ++removed;
        }
    }
    return removed;
}

QStringList APlusContent::activeFaqFilePaths() const
{
    QStringList paths;
    QSet<QString> seen;
    for (const APlusElement &el : m_elements) {
        if (el.type != APlusElementType::Faq)
            continue;
        const APlusVersion *ver = el.current();
        if (!ver || ver->desktopFile.isEmpty())
            continue;
        const QString abs = m_dir.absoluteFilePath(ver->desktopFile);
        if (!seen.contains(abs)) {
            seen.insert(abs);
            paths.append(abs);
        }
    }
    return paths;
}

APlusAssessmentParseResult APlusContent::parseAssessmentOutput(
    const QString &rawOutput,
    const QStringList &expectedImageElementIds) const
{
    APlusAssessmentParseResult res;
    const QString trimmed = rawOutput.trimmed();
    if (trimmed.isEmpty())
        return res;

    const QStringList lines = trimmed.split(QLatin1Char('\n'));
    for (int i = lines.size() - 1; i >= 0; --i) {
        QString t = lines.at(i).trimmed();
        t.remove(QLatin1Char('*')).remove(QLatin1Char('`')).remove(QLatin1Char('#'));
        t = t.trimmed();
        if (!t.isEmpty()) {
            if (t.compare(QLatin1String("PASS"), Qt::CaseInsensitive) == 0)
                res.verdict = QStringLiteral("PASS");
            else if (t.compare(QLatin1String("PASS_WITH_WARNINGS"), Qt::CaseInsensitive) == 0)
                res.verdict = QStringLiteral("PASS_WITH_WARNINGS");
            else if (t.compare(QLatin1String("FAIL"), Qt::CaseInsensitive) == 0
                     || t.startsWith(QLatin1String("FAIL"), Qt::CaseInsensitive))
                res.verdict = QStringLiteral("FAIL");
            else
                res.verdict = t;
            break;
        }
    }

    auto addOrMergeFailure = [&res](const QString &elemId, const QString &sideIn, const QString &reasonIn) {
        const QString id = elemId.trimmed();
        if (id.isEmpty())
            return;
        QString side = sideIn.trimmed().toLower();
        if (side != QLatin1String("desktop")
                && side != QLatin1String("mobile")
                && side != QLatin1String("both")
                && side != QLatin1String("faq")) {
            side = id.startsWith(QLatin1String("faq"))
                 ? QStringLiteral("faq") : QStringLiteral("both");
        }
        const QString reason = reasonIn.trimmed();
        for (APlusAssessmentFailure &existing : res.failures) {
            if (existing.elementId == id) {
                if (existing.side != side && existing.side != QLatin1String("faq"))
                    existing.side = QStringLiteral("both");
                if (!reason.isEmpty() && !existing.reason.contains(reason)) {
                    if (!existing.reason.isEmpty())
                        existing.reason += QStringLiteral("; ");
                    existing.reason += reason;
                }
                return;
            }
        }
        res.failures.append({id, side, reason});
    };

    // 1. Parse explicit FAILED_ITEM: <elementId> | <side> | <reason> lines
    static const QRegularExpression kFailedItemRe(
        QStringLiteral("(?i)^[-*\\s]*FAILED_ITEM\\s*:\\s*([^|]+)\\|\\s*([^|]+)\\|\\s*(.+)$"));
    for (const QString &line : lines) {
        const QRegularExpressionMatch m = kFailedItemRe.match(line.trimmed());
        if (m.hasMatch()) {
            addOrMergeFailure(m.captured(1), m.captured(2), m.captured(3));
        }
    }

    if (res.verdict != QLatin1String("FAIL"))
        return res;

    // 2. Also include any expected image element whose active desktop or mobile file is missing
    for (const QString &expId : expectedImageElementIds) {
        const APlusElement *el = findElement(expId);
        const APlusVersion *ver = el ? el->current() : nullptr;
        const bool hasD = ver && !ver->desktopFile.isEmpty()
                       && QFileInfo::exists(m_dir.filePath(ver->desktopFile));
        const bool hasM = ver && !ver->mobileFile.isEmpty()
                       && QFileInfo::exists(m_dir.filePath(ver->mobileFile));
        if (!hasD && !hasM)
            addOrMergeFailure(expId, QStringLiteral("both"), QStringLiteral("Missing output image file."));
        else if (!hasD)
            addOrMergeFailure(expId, QStringLiteral("desktop"), QStringLiteral("Missing desktop image file."));
        else if (!hasM)
            addOrMergeFailure(expId, QStringLiteral("mobile"), QStringLiteral("Missing mobile image file."));
    }

    // 3. Fallback if the CLI emitted FAIL without structured FAILED_ITEM lines:
    //    match any element ID or active filename mentioned in the assessment text.
    if (res.failures.isEmpty()) {
        for (const APlusElement &el : m_elements) {
            if (el.type == APlusElementType::SizeChart)
                continue;
            const APlusVersion *ver = el.current();
            const QString dName = (ver && !ver->desktopFile.isEmpty())
                                ? QFileInfo(ver->desktopFile).fileName() : QString{};
            const QString mName = (ver && !ver->mobileFile.isEmpty())
                                ? QFileInfo(ver->mobileFile).fileName() : QString{};
            const bool mentionsId = !el.id.isEmpty() && trimmed.contains(el.id, Qt::CaseInsensitive);
            const bool mentionsD  = !dName.isEmpty() && trimmed.contains(dName, Qt::CaseInsensitive);
            const bool mentionsM  = !mName.isEmpty() && trimmed.contains(mName, Qt::CaseInsensitive);
            if (!mentionsId && !mentionsD && !mentionsM)
                continue;

            if (el.type == APlusElementType::Faq) {
                addOrMergeFailure(el.id, QStringLiteral("faq"), trimmed);
            } else {
                QString side = QStringLiteral("both");
                if (mentionsD && !mentionsM)
                    side = QStringLiteral("desktop");
                else if (mentionsM && !mentionsD)
                    side = QStringLiteral("mobile");
                addOrMergeFailure(el.id, side, trimmed);
            }
        }
    }

    // 4. Last-resort fallback: if verdict is FAIL and still nothing matched,
    //    mark all expected image elements (or all image elements) as failed.
    if (res.failures.isEmpty()) {
        if (!expectedImageElementIds.isEmpty()) {
            for (const QString &expId : expectedImageElementIds)
                addOrMergeFailure(expId, QStringLiteral("both"), trimmed);
        } else {
            for (const APlusElement &el : m_elements) {
                if (el.type == APlusElementType::Image)
                    addOrMergeFailure(el.id, QStringLiteral("both"), trimmed);
            }
        }
    }

    return res;
}

void APlusContent::deleteFailedAssessmentItems(const QList<APlusAssessmentFailure> &failures)
{
    for (const APlusAssessmentFailure &f : failures) {
        APlusElement *el = findElement(f.elementId);
        if (!el)
            continue;
        QDir elemDir(m_dir.filePath(el->id));

        if (el->type == APlusElementType::Image && f.side == QLatin1String("desktop")) {
            while (el->versions.size() > 1)
                deleteVersion(el->id, 1);
            if (!el->versions.isEmpty())
                clearVersionFile(el->id, 0, true);
            QFile::remove(elemDir.filePath(QStringLiteral("desktop.png")));
        } else if (el->type == APlusElementType::Image && f.side == QLatin1String("mobile")) {
            while (el->versions.size() > 1)
                deleteVersion(el->id, 1);
            if (!el->versions.isEmpty())
                clearVersionFile(el->id, 0, false);
            QFile::remove(elemDir.filePath(QStringLiteral("mobile.png")));
        } else {
            while (!el->versions.isEmpty())
                deleteVersion(el->id, 0);
            QFile::remove(elemDir.filePath(QStringLiteral("desktop.png")));
            QFile::remove(elemDir.filePath(QStringLiteral("mobile.png")));
        }
    }
    pruneOrphanFiles();
    save();
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
    const APlusVersion removed = e->versions.takeAt(versionIndex);
    auto stillReferenced = [&](const QString &rel) {
        if (rel.isEmpty()) return true;
        for (const APlusVersion &v : std::as_const(e->versions)) {
            if (v.desktopFile == rel || v.mobileFile == rel)
                return true;
        }
        return false;
    };
    if (!stillReferenced(removed.desktopFile))
        QFile::remove(m_dir.filePath(removed.desktopFile));
    if (removed.mobileFile != removed.desktopFile && !stillReferenced(removed.mobileFile))
        QFile::remove(m_dir.filePath(removed.mobileFile));
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
