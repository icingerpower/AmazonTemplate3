#include "GsprManufacturerStore.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <xlsxdocument.h>

namespace {

const auto CACHE_FILE = QStringLiteral("gspr-manufacturer-cache-v3.json");

// Segment labels (validated against all real supplier files — 634/636 rows).
const QRegularExpression RE_PHONE(
    QStringLiteral("^(mobile\\s*phone|t[ée]l[ée]phone|phone|tel|cell\\s*phone|mobile|whatsapp)\\s*:?\\s*(.+)$"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression RE_ADDR(
    QStringLiteral("^(address|adresse|add)\\s*:?\\s*(.+)$"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression RE_EMAIL(
    QStringLiteral("^e-?mail\\s*:?\\s*(.+)$"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression RE_NAME_PREFIX(
    QStringLiteral("^(company\\s*name|name)\\s*:\\s*"),
    QRegularExpression::CaseInsensitiveOption);
const QRegularExpression RE_BARE_PHONE(
    QStringLiteral("^\\+?[\\d\\s\\-()\\.]{7,}$"));
// Force a segment break before an inline "Label:" glued after a single space.
const QRegularExpression RE_INLINE_LABEL(
    QStringLiteral("\\s+((?:mobile\\s*phone|t[ée]l[ée]phone|phone|tel|cell\\s*phone|mobile|whatsapp|"
                   "address|adresse|add|e-?mail|fax)\\s*:)"),
    QRegularExpression::CaseInsensitiveOption);
// Separators: " - ", " – " (en-dash) or runs of 2+ spaces.
const QRegularExpression RE_SEG_SEP(QStringLiteral("\\s+[-–]\\s+|\\s{2,}"));
const QRegularExpression RE_NEWLINES(QStringLiteral("\\s*\\n\\s*"));
const QRegularExpression RE_SKU_TOKEN(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9\\-_. ]*$"));
const QRegularExpression RE_SKU_SPLIT(QStringLiteral("[\\n/,;]+"));

QString joinAddress(const QString &addr, const QString &part)
{
    return addr.isEmpty() ? part : addr + QStringLiteral(", ") + part;
}

// Cell text with formula cells resolved: some sheets contain "=B6"-style
// drag-copies — Document::read() returns the formula string, while
// Cell::value() holds the cached computed value.
QString cellText(QXlsx::Document &doc, int row, int col)
{
    if (auto cell = doc.cellAt(row, col))
        return cell->value().toString().trimmed();
    return doc.read(row, col).toString().trimmed();
}

} // namespace

bool GsprManufacturerStore::parseManufacturer(const QString &raw, GsprManufacturerEntry *out)
{
    QString m = raw.trimmed();
    m.replace(RE_NEWLINES, QStringLiteral("  "));
    m.replace(RE_INLINE_LABEL, QStringLiteral("  \\1"));

    QString name, phone, addr, email, url;
    bool first = true;
    for (QString seg : m.split(RE_SEG_SEP, Qt::SkipEmptyParts)) {
        seg = seg.trimmed();
        if (seg.isEmpty())
            continue;
        if (first) {
            name = seg;
            name.remove(RE_NAME_PREFIX);
            first = false;
            continue;
        }
        const auto am = RE_ADDR.match(seg);
        const auto pm = RE_PHONE.match(seg);
        const auto em = RE_EMAIL.match(seg);
        if (am.hasMatch()) {
            QString v = am.captured(2);
            const auto inner = RE_ADDR.match(v); // "Address: address:X" → X
            if (inner.hasMatch())
                v = inner.captured(2);
            addr = joinAddress(addr, v);
        } else if (pm.hasMatch() && !RE_BARE_PHONE.match(seg).hasMatch()) {
            phone = pm.captured(2);
        } else if (em.hasMatch()) {
            email = em.captured(1);
        } else if (seg.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) {
            if (url.isEmpty())
                url = seg; // accepted by Amazon as "primary email address or URL"
        } else if (seg.startsWith(QLatin1String("fax:"), Qt::CaseInsensitive)
                   || seg.startsWith(QLatin1String("fax "), Qt::CaseInsensitive)) {
            // fax numbers are useless for the form
        } else if (RE_BARE_PHONE.match(seg).hasMatch()) {
            phone = seg;
        } else {
            addr = joinAddress(addr, seg); // unlabeled → address continuation
        }
    }

    if (out)
        *out = GsprManufacturerEntry{QString{}, name, phone.trimmed(),
                                     addr.trimmed(), email.trimmed(), url.trimmed()};
    return !name.isEmpty() && (!addr.isEmpty() || !phone.isEmpty());
}

QStringList GsprManufacturerStore::skuPrefixes(const QString &cell, QStringList *badTokens)
{
    QStringList out;
    for (QString t : cell.split(RE_SKU_SPLIT, Qt::SkipEmptyParts)) {
        t = t.trimmed();
        while (t.endsWith(QLatin1Char('*')) || t.endsWith(QLatin1Char('-')))
            t.chop(1);
        t = t.trimmed();
        if (t.isEmpty())
            continue;
        if (RE_SKU_TOKEN.match(t).hasMatch())
            out << t;
        else if (badTokens)
            *badTokens << t;
    }
    return out;
}

bool GsprManufacturerStore::reload(QString *error)
{
    m_entries.clear();
    m_unparseable.clear();

    if (!m_folder.exists()) {
        if (error) *error = QStringLiteral("Manufacturer folder does not exist: %1")
                                .arg(m_folder.absolutePath());
        return false;
    }

    // Load the cache (per-file mtime + parsed entries).
    QJsonObject cacheFiles;
    {
        QFile f(m_folder.filePath(CACHE_FILE));
        if (f.open(QIODevice::ReadOnly))
            cacheFiles = QJsonDocument::fromJson(f.readAll()).object()
                             .value(QStringLiteral("files")).toObject();
    }

    QJsonObject newCacheFiles;
    const QStringList xlsxFiles =
        m_folder.entryList({QStringLiteral("*.xlsx")}, QDir::Files, QDir::Name);
    for (const QString &fileName : xlsxFiles) {
        if (fileName.startsWith(QLatin1Char('.')) || fileName.startsWith(QLatin1Char('~')))
            continue; // LibreOffice lock files etc.

        const QString path = m_folder.filePath(fileName);
        const QString mtime = QFileInfo(path).lastModified()
                                  .toString(Qt::ISODateWithMs);

        // Unchanged since last parse → reuse the cached result, file not opened.
        const QJsonObject cached = cacheFiles.value(fileName).toObject();
        if (cached.value(QStringLiteral("mtime")).toString() == mtime) {
            for (const QJsonValue &v : cached.value(QStringLiteral("entries")).toArray()) {
                const QJsonObject o = v.toObject();
                m_entries.append(GsprManufacturerEntry{
                    o.value(QStringLiteral("prefix")).toString(),
                    o.value(QStringLiteral("name")).toString(),
                    o.value(QStringLiteral("phone")).toString(),
                    o.value(QStringLiteral("address")).toString(),
                    o.value(QStringLiteral("email")).toString(),
                    o.value(QStringLiteral("url")).toString()});
            }
            for (const QJsonValue &v : cached.value(QStringLiteral("unparseable")).toArray())
                m_unparseable << v.toString();
            newCacheFiles.insert(fileName, cached);
            continue;
        }

        // Parse the file.
        QXlsx::Document doc(path);
        QJsonArray entriesJson, unparseableJson;
        const int lastCol = qMin(doc.dimension().lastColumn(), 32);
        int colMan = -1, colSku = -1, colLink = -1;
        for (int c = 1; c <= lastCol; ++c) {
            const QString h = cellText(doc, 1, c).toLower();
            if (h == QLatin1String("manufacturer"))       colMan = c;
            else if (h == QLatin1String("sku"))           colSku = c;
            else if (h == QLatin1String("purchase link")) colLink = c;
        }
        if (colMan < 0 || colSku < 0) {
            const QString msg = QStringLiteral("%1: no Manufacturer/sku header row")
                                    .arg(fileName);
            m_unparseable << msg;
            unparseableJson.append(msg);
        } else {
            // Some sheets claim bogus huge dimensions (whole-column formatting)
            // — stop after a run of consecutive empty rows instead.
            int emptyStreak = 0;
            for (int r = 2; r <= doc.dimension().lastRow() && emptyStreak < 50; ++r) {
                const QString man = cellText(doc, r, colMan);
                const QString sku = cellText(doc, r, colSku);
                if (man.isEmpty() && sku.isEmpty()) { ++emptyStreak; continue; }
                emptyStreak = 0;
                QStringList bad;
                GsprManufacturerEntry e;
                const bool manOk = !man.isEmpty() && parseManufacturer(man, &e);
                // Amazon's form requires an email or URL — when the
                // manufacturer line has neither, the Purchase link column is
                // a valid URL to use.
                if (e.url.isEmpty() && e.email.isEmpty() && colLink > 0) {
                    const QString link = cellText(doc, r, colLink);
                    if (link.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
                        e.url = link;
                }
                const QStringList prefixes = skuPrefixes(sku, &bad);
                if (!manOk || prefixes.isEmpty() || !bad.isEmpty()) {
                    const QString msg = QStringLiteral("%1 row %2: %3")
                        .arg(fileName).arg(r)
                        .arg(!manOk ? QStringLiteral("manufacturer unparseable")
                                    : QStringLiteral("bad sku cell \"%1\"").arg(sku));
                    m_unparseable << msg;
                    unparseableJson.append(msg);
                    continue;
                }
                for (const QString &prefix : prefixes) {
                    e.prefix = prefix;
                    m_entries.append(e);
                    entriesJson.append(QJsonObject{
                        {QStringLiteral("prefix"),  e.prefix},
                        {QStringLiteral("name"),    e.name},
                        {QStringLiteral("phone"),   e.phone},
                        {QStringLiteral("address"), e.address},
                        {QStringLiteral("email"),   e.email},
                        {QStringLiteral("url"),     e.url}});
                }
            }
        }
        newCacheFiles.insert(fileName, QJsonObject{
            {QStringLiteral("mtime"), mtime},
            {QStringLiteral("entries"), entriesJson},
            {QStringLiteral("unparseable"), unparseableJson}});
    }

    // Persist the refreshed cache.
    QFile f(m_folder.filePath(CACHE_FILE));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(QJsonObject{{QStringLiteral("files"), newCacheFiles}})
                    .toJson(QJsonDocument::Indented));

    if (m_entries.isEmpty()) {
        if (error) *error = QStringLiteral("No manufacturer entries found in %1")
                                .arg(m_folder.absolutePath());
        return false;
    }
    return true;
}

QJsonArray GsprManufacturerStore::entriesJson() const
{
    QJsonArray arr;
    for (const GsprManufacturerEntry &e : m_entries)
        arr.append(QJsonObject{{QStringLiteral("prefix"),  e.prefix},
                               {QStringLiteral("name"),    e.name},
                               {QStringLiteral("phone"),   e.phone},
                               {QStringLiteral("address"), e.address},
                               {QStringLiteral("email"),   e.email},
                               {QStringLiteral("url"),     e.url}});
    return arr;
}
