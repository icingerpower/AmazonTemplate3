#include "TreeSizingAsins.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QDebug>

#include <xlsxdocument.h>

namespace {
constexpr const char* kJsonFileName = "sizing_upload.json";

// Representative marketplace ID per geographic region, tried in order when
// auto-detecting which region a product belongs to.
// Marketplaces tried in order when auto-detecting which region a product is in.
// All EU entries share the EU LWA token, so trying multiple costs no extra auth
// round-trips — Amazon EU products may be listed in one country but not another.
const QStringList kRegionMarketplaces = QStringList()
    << QStringLiteral("ATVPDKIKX0DER")    // US  (NA)
    << QStringLiteral("A1F83G8C2ARO7P")   // UK  (EU)
    << QStringLiteral("A2EUQ1WTGCTBG2")   // CA  (NA)
    << QStringLiteral("A13V1IB3VIYZZH")   // FR  (EU)
    << QStringLiteral("APJ6JRA9NG5V4")    // IT  (EU)
    << QStringLiteral("A1RKKUPIHCS9HS")   // ES  (EU)
    << QStringLiteral("A1PA6795UKMFR9")   // DE  (EU)
    << QStringLiteral("A1VC38T7YXB528");  // JP  (FE)
} // namespace

TreeSizingAsins::TreeSizingAsins(const QDir& workingDir, QObject* parent)
    : QAbstractItemModel(parent)
    , m_workingDir(workingDir)
{
    _loadJson();
}

TreeSizingAsins::~TreeSizingAsins() = default;

void TreeSizingAsins::setApiClient(AmazonCatalogApi* api)
{
    m_api = api;
}

// ---------------------------------------------------------------------------
// Tree-index encoding helpers
// ---------------------------------------------------------------------------

QModelIndex TreeSizingAsins::_makeTopIndex(int familyRow, int col) const
{
    return createIndex(familyRow, col, kTopLevelId);
}

QModelIndex TreeSizingAsins::_makeChildIndex(int familyRow, int childRow, int col) const
{
    return createIndex(childRow, col, static_cast<quintptr>(familyRow + 1));
}

// ---------------------------------------------------------------------------
// JSON persistence
// ---------------------------------------------------------------------------

void TreeSizingAsins::_loadJson()
{
    const QString path = m_workingDir.filePath(QString::fromLatin1(kJsonFileName));
    QFile f(path);
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray data = f.readAll();
    f.close();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();

    const QJsonObject sizeImages = root.value("sizeImages").toObject();
    m_sizeImageDates.clear();
    for (auto it = sizeImages.constBegin(); it != sizeImages.constEnd(); ++it) {
        const QDate d = QDate::fromString(it.value().toString(), Qt::ISODate);
        if (d.isValid())
            m_sizeImageDates.insert(it.key(), d);
    }

    const QJsonObject aPlus = root.value("aPlus").toObject();
    m_aPlusDates.clear();
    for (auto it = aPlus.constBegin(); it != aPlus.constEnd(); ++it) {
        const QDate d = QDate::fromString(it.value().toString(), Qt::ISODate);
        if (d.isValid())
            m_aPlusDates.insert(it.key(), d);
    }
}

void TreeSizingAsins::_saveJson()
{
    QJsonObject root;
    QJsonObject sizeImages;
    for (auto it = m_sizeImageDates.constBegin(); it != m_sizeImageDates.constEnd(); ++it)
        sizeImages.insert(it.key(), it.value().toString(Qt::ISODate));
    root.insert("sizeImages", sizeImages);

    QJsonObject aPlus;
    for (auto it = m_aPlusDates.constBegin(); it != m_aPlusDates.constEnd(); ++it)
        aPlus.insert(it.key(), it.value().toString(Qt::ISODate));
    root.insert("aPlus", aPlus);

    if (!m_workingDir.exists())
        m_workingDir.mkpath(".");
    const QString path = m_workingDir.filePath(QString::fromLatin1(kJsonFileName));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "TreeSizingAsins: failed to write" << path;
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
}

void TreeSizingAsins::_applyDatesToFamily(ParentItem& family)
{
    for (ChildItem& c : family.children) {
        if (m_sizeImageDates.contains(c.asin))
            c.sizeImageDate = m_sizeImageDates.value(c.asin);
        if (m_aPlusDates.contains(c.asin))
            c.aPlusDate = m_aPlusDates.value(c.asin);
    }
}

// ---------------------------------------------------------------------------
// xlsx helper
// ---------------------------------------------------------------------------

QStringList TreeSizingAsins::_readAsinsFromXlsx(const QString& xlsxPath) const
{
    QStringList result;
    QXlsx::Document doc(xlsxPath);
    if (!doc.load()) {
        qWarning() << "TreeSizingAsins: failed to load xlsx" << xlsxPath;
        return result;
    }

    const QRegularExpression asinRegex("^[A-Z0-9]{10}$");
    const QString dim = doc.dimension().toString(); // e.g. "A1:AZ200"
    Q_UNUSED(dim);

    // Find ASIN column: scan the first 5 rows for a header cell that
    // contains "asin" (case-insensitive) or is named "external_product_id".
    int asinCol = -1;
    for (int row = 1; row <= 5 && asinCol == -1; ++row) {
        for (int col = 1; col <= 200; ++col) {
            const QVariant v = doc.read(row, col);
            if (!v.isValid()) continue;
            const QString s = v.toString().trimmed();
            if (s.isEmpty()) continue;
            if (s.compare("asin", Qt::CaseInsensitive) == 0
                || s.compare("external_product_id", Qt::CaseInsensitive) == 0) {
                asinCol = col;
                break;
            }
        }
    }

    if (asinCol == -1) {
        // Fallback: scan all cells looking for an ASIN-shaped value.
        for (int row = 1; row <= 1000; ++row) {
            for (int col = 1; col <= 200; ++col) {
                const QVariant v = doc.read(row, col);
                if (!v.isValid()) continue;
                const QString s = v.toString().trimmed();
                if (asinRegex.match(s).hasMatch() && !result.contains(s))
                    result.append(s);
            }
        }
        return result;
    }

    // Read the ASIN column, skipping header rows.
    for (int row = 2; row <= 5000; ++row) {
        const QVariant v = doc.read(row, asinCol);
        if (!v.isValid()) continue;
        const QString s = v.toString().trimmed();
        if (s.isEmpty()) continue;
        if (asinRegex.match(s).hasMatch() && !result.contains(s))
            result.append(s);
    }

    return result;
}

// ---------------------------------------------------------------------------
// load()
// ---------------------------------------------------------------------------

QCoro::Task<void> TreeSizingAsins::load(const QString& asinOrXlsxPath,
                                        const QString& marketplaceId)
{
    if (!m_api) {
        qWarning() << "TreeSizingAsins::load called without an API client";
        co_return;
    }

    QStringList asins;
    if (asinOrXlsxPath.endsWith(".xlsx", Qt::CaseInsensitive)) {
        asins = _readAsinsFromXlsx(asinOrXlsxPath);
    } else {
        asins << asinOrXlsxPath;
    }

    // Human-readable country code for each marketplace ID (for error messages).
    static const QMap<QString,QString> kMktName = {
        {"A13V1IB3VIYZZH","FR"}, {"A1PA6795UKMFR9","DE"}, {"A1F83G8C2ARO7P","UK"},
        {"APJ6JRA9NG5V4","IT"},  {"A1RKKUPIHCS9HS","ES"}, {"ATVPDKIKX0DER","US"},
        {"A2EUQ1WTGCTBG2","CA"}, {"A1VC38T7YXB528","JP"}
    };

    // Region classification — independent of the probe order in kRegionMarketplaces.
    enum Region { EU, NA, JP };
    static const QHash<QString,Region> kMktRegion = {
        {"A13V1IB3VIYZZH", EU}, {"A1PA6795UKMFR9", EU}, {"A1F83G8C2ARO7P", EU},
        {"APJ6JRA9NG5V4",  EU}, {"A1RKKUPIHCS9HS", EU},
        {"ATVPDKIKX0DER",  NA}, {"A2EUQ1WTGCTBG2", NA},
        {"A1VC38T7YXB528", JP}
    };
    // Fixed representatives for the per-region existence check (order-independent).
    static const QString kEuRep = QStringLiteral("A1F83G8C2ARO7P"); // UK
    static const QString kNaRep = QStringLiteral("ATVPDKIKX0DER");  // US
    static const QString kJpRep = QStringLiteral("A1VC38T7YXB528");  // JP

    QList<ParentItem> newFamilies;
    QSet<QString> seenParents;
    bool attributesEmitted = false;
    int  succeededRegionIdx = -1;
    QString probeChildAsin;
    for (const QString& asin : asins) {
        if (asin.isEmpty()) continue;
        AmazonCatalogApi::VariationFamily family;
        QStringList attemptLog;

        // Try EU (FR/DE/UK/IT/ES), NA, then JP.
        // Clear the error before each attempt so we can tell "no token" (error stays empty)
        // from "got a real API error" (error is set by fetchVariationFamily).
        for (int ri = 0; ri < kRegionMarketplaces.size(); ++ri) {
            m_api->clearLastError();
            co_await m_api->fetchVariationFamily(asin, kRegionMarketplaces[ri], &family);
            if (!family.parentAsin.isEmpty() || !family.children.isEmpty()) {
                if (!attributesEmitted) succeededRegionIdx = ri;
                break;
            }
            const QString mkt = kRegionMarketplaces[ri];
            const QString err = m_api->lastError();
            attemptLog << QStringLiteral("%1(%2): %3")
                              .arg(kMktName.value(mkt, mkt), mkt,
                                   err.isEmpty() ? tr("no token configured") : err);
        }

        if (family.parentAsin.isEmpty() && family.children.isEmpty()) {
            emit loadError(
                tr("Could not load ASIN %1.\n\n%2").arg(asin, attemptLog.join('\n')));
            continue;
        }
        if (seenParents.contains(family.parentAsin))
            continue;
        seenParents.insert(family.parentAsin);

        ParentItem p;
        p.asin = family.parentAsin;
        p.sku  = family.parentSku;
        for (const auto& c : family.children) {
            ChildItem ci;
            ci.asin         = c.asin;
            ci.sku          = c.sku;
            ci.size         = c.size;
            ci.color        = c.color;
            ci.title        = c.title;
            ci.hasSizeTable = c.hasSizeTable;
            p.children.append(ci);
        }
        _applyDatesToFamily(p);
        newFamilies.append(p);

        // Emit bullet points + material for the first child of the first family
        if (!attributesEmitted && !family.children.isEmpty()) {
            probeChildAsin = family.children.first().asin;

            emit attributesFetched(family.children.first().bulletPoints,
                                   family.children.first().materialAttrs,
                                   family.children.first().mainImageUrl,
                                   family.parentAsin,
                                   family.children.first().title);

            // Emit images grouped by unique color. Color variants each have
            // their own photos; size-only variants share photos, so dedup
            // by color naturally collapses them to one entry.
            QList<QPair<QString, QStringList>> colorImages;
            QSet<QString> seenColors;
            for (const auto& c : family.children) {
                if (c.allImageUrls.isEmpty()) continue;
                const QString key = c.color.toLower();
                if (seenColors.contains(key)) continue;
                seenColors.insert(key);
                colorImages.append({c.color, c.allImageUrls});
            }
            if (!colorImages.isEmpty())
                emit variantImagesFetched(colorImages);

            attributesEmitted = true;
        }
    }

    beginResetModel();
    m_families = std::move(newFamilies);
    endResetModel();

    // Check which geographic regions carry this product and emit the result.
    // We use one lightweight existence check per unchecked region so we don't
    // repeat the region we already confirmed during the family load above.
    if (!probeChildAsin.isEmpty() && succeededRegionIdx >= 0) {
        const Region succeededRegion =
            kMktRegion.value(kRegionMarketplaces[succeededRegionIdx], EU);
        const bool euConfirmed = (succeededRegion == EU);
        const bool naConfirmed = (succeededRegion == NA);
        const bool jpConfirmed = (succeededRegion == JP);

        bool euExists = euConfirmed;
        bool naExists = naConfirmed;
        bool jpExists = jpConfirmed;

        if (!euConfirmed)
            co_await m_api->checkAsinExists(probeChildAsin, kEuRep, &euExists);
        if (!naConfirmed)
            co_await m_api->checkAsinExists(probeChildAsin, kNaRep, &naExists);
        if (!jpConfirmed)
            co_await m_api->checkAsinExists(probeChildAsin, kJpRep, &jpExists);


        // EU country codes (same LWA region, so if one works they all do)
        static const QStringList kEuCodes = {
            QStringLiteral("fr"), QStringLiteral("de"), QStringLiteral("it"),
            QStringLiteral("es"), QStringLiteral("uk"), QStringLiteral("nl"),
            QStringLiteral("se"), QStringLiteral("pl"), QStringLiteral("be"),
            QStringLiteral("ie"), QStringLiteral("tr")
        };
        static const QStringList kNaCodes = {
            QStringLiteral("us"), QStringLiteral("ca"), QStringLiteral("mx")
        };

        QStringList available, missing;
        for (const QString &c : kEuCodes) {
            if (euExists) available.append(c);
            else          missing.append(c + QStringLiteral(" (missing)"));
        }
        for (const QString &c : kNaCodes) {
            if (naExists) available.append(c);
            else          missing.append(c + QStringLiteral(" (missing)"));
        }
        if (jpExists) available.append(QStringLiteral("jp"));
        else          missing.append(QStringLiteral("jp (missing)"));

        emit marketplacesChecked(available + missing);
    }

    co_return;
}

// ---------------------------------------------------------------------------
// Recording uploads
// ---------------------------------------------------------------------------

QCoro::Task<void> TreeSizingAsins::_findOrLoadFamily(QString asin, QString marketplaceId,
                                                    int* outFamilyIndex)
{
    *outFamilyIndex = -1;

    // 1. Scan already-loaded families for a match.
    for (int fi = 0; fi < m_families.size(); ++fi) {
        const ParentItem& p = m_families.at(fi);
        if (p.asin == asin) {
            *outFamilyIndex = fi;
            co_return;
        }
        for (const ChildItem& c : p.children) {
            if (c.asin == asin) {
                *outFamilyIndex = fi;
                co_return;
            }
        }
    }

    // 2. Fallback to API — try EU → NA → JP.
    if (!m_api)
        co_return;

    AmazonCatalogApi::VariationFamily family;
    for (int ri = 0; ri < kRegionMarketplaces.size(); ++ri) {
        co_await m_api->fetchVariationFamily(asin, kRegionMarketplaces[ri], &family);
        if (!family.parentAsin.isEmpty() || !family.children.isEmpty())
            break;
    }

    if (family.parentAsin.isEmpty() && family.children.isEmpty())
        co_return;

    // 3. Maybe the family (by parent asin) was already loaded under a different
    //    child search. Re-scan now that we have the parent asin resolved.
    for (int fi = 0; fi < m_families.size(); ++fi) {
        if (m_families.at(fi).asin == family.parentAsin) {
            *outFamilyIndex = fi;
            co_return;
        }
    }

    // 4. Build a new ParentItem from the fetched family (same loop as load()).
    ParentItem p;
    p.asin = family.parentAsin;
    p.sku  = family.parentSku;
    for (const auto& c : family.children) {
        ChildItem ci;
        ci.asin         = c.asin;
        ci.sku          = c.sku;
        ci.size         = c.size;
        ci.color        = c.color;
        ci.title        = c.title;
        ci.hasSizeTable = c.hasSizeTable;
        p.children.append(ci);
    }
    _applyDatesToFamily(p);

    const int newRow = m_families.size();
    beginInsertRows(QModelIndex{}, newRow, newRow);
    m_families.append(std::move(p));
    endInsertRows();
    *outFamilyIndex = newRow;
    co_return;
}

QCoro::Task<void> TreeSizingAsins::recordSizeImageUploaded(
    const QString& asin, const QDate& date, const QString& marketplaceId)
{
    int familyIdx = -1;
    co_await _findOrLoadFamily(asin, marketplaceId, &familyIdx);

    m_sizeImageDates.insert(asin, date);

    if (familyIdx >= 0) {
        ParentItem& p = m_families[familyIdx];
        for (int ci = 0; ci < p.children.size(); ++ci) {
            if (p.children[ci].asin == asin) {
                p.children[ci].sizeImageDate = date;
                const QModelIndex idx = _makeChildIndex(familyIdx, ci, SizeImage);
                emit dataChanged(idx, idx, {Qt::DisplayRole});
            }
        }
    }
    _saveJson();
    co_return;
}

QCoro::Task<void> TreeSizingAsins::recordAPlusUploaded(
    const QString& asin, const QDate& date, const QString& marketplaceId)
{
    int familyIdx = -1;
    co_await _findOrLoadFamily(asin, marketplaceId, &familyIdx);

    m_aPlusDates.insert(asin, date);

    if (familyIdx >= 0) {
        ParentItem& p = m_families[familyIdx];
        for (int ci = 0; ci < p.children.size(); ++ci) {
            if (p.children[ci].asin == asin) {
                p.children[ci].aPlusDate = date;
                const QModelIndex idx = _makeChildIndex(familyIdx, ci, APlusContent);
                emit dataChanged(idx, idx, {Qt::DisplayRole});
            }
        }
    }
    _saveJson();
    co_return;
}

// ---------------------------------------------------------------------------
// QAbstractItemModel implementation
// ---------------------------------------------------------------------------

QModelIndex TreeSizingAsins::index(int row, int col, const QModelIndex& parent) const
{
    if (row < 0 || col < 0 || col >= COLUMN_COUNT)
        return {};

    if (!parent.isValid()) {
        if (row >= m_families.size())
            return {};
        return _makeTopIndex(row, col);
    }

    // parent is a top-level family row; row is a child index within it.
    if (parent.internalId() != kTopLevelId)
        return {}; // children have no children

    const int familyRow = parent.row();
    if (familyRow < 0 || familyRow >= m_families.size())
        return {};
    if (row >= m_families[familyRow].children.size())
        return {};
    return _makeChildIndex(familyRow, row, col);
}

QModelIndex TreeSizingAsins::parent(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};
    if (index.internalId() == kTopLevelId)
        return {}; // top-level has no parent
    const quintptr id = index.internalId();
    const int parentFamilyRow = static_cast<int>(id) - 1;
    if (parentFamilyRow < 0 || parentFamilyRow >= m_families.size())
        return {};
    return _makeTopIndex(parentFamilyRow, 0);
}

int TreeSizingAsins::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return m_families.size();
    if (parent.internalId() == kTopLevelId) {
        const int fi = parent.row();
        if (fi < 0 || fi >= m_families.size())
            return 0;
        return m_families[fi].children.size();
    }
    // Child rows have no children
    return 0;
}

int TreeSizingAsins::columnCount(const QModelIndex& /*parent*/) const
{
    return COLUMN_COUNT;
}

QVariant TreeSizingAsins::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) return {};
    const int col = index.column();

    // Top-level (family/parent) row
    if (index.internalId() == kTopLevelId) {
        const int fi = index.row();
        if (fi < 0 || fi >= m_families.size()) return {};
        const ParentItem& p = m_families.at(fi);

        if (role == Qt::DisplayRole) {
            switch (col) {
            case SKU:  return p.sku;
            case ASIN: return p.asin;
            default:   return {};
            }
        }
        return {};
    }

    // Child row
    const int familyRow = static_cast<int>(index.internalId()) - 1;
    if (familyRow < 0 || familyRow >= m_families.size()) return {};
    const ParentItem& p = m_families.at(familyRow);
    const int ci = index.row();
    if (ci < 0 || ci >= p.children.size()) return {};
    const ChildItem& c = p.children.at(ci);

    if (role == Qt::DisplayRole) {
        switch (col) {
        case SKU:   return c.sku;
        case ASIN:  return c.asin;
        case Size:  return c.size;
        case Color: return c.color;
        case Title: return c.title;
        case SizeImage:
            return c.sizeImageDate.isValid()
                   ? c.sizeImageDate.toString("yyyy-MM-dd") : QString();
        case APlusContent:
            return c.aPlusDate.isValid()
                   ? c.aPlusDate.toString("yyyy-MM-dd") : QString();
        case SizeTable:
            return {}; // SizeTable uses CheckStateRole, not DisplayRole
        default:
            return {};
        }
    }

    if (role == Qt::CheckStateRole && col == SizeTable) {
        return c.hasSizeTable ? Qt::Checked : Qt::Unchecked;
    }

    if (role == Qt::ToolTipRole && col == Title) {
        return c.title;
    }

    return {};
}

QVariant TreeSizingAsins::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section) {
    case SKU:          return tr("SKU");
    case ASIN:         return tr("ASIN");
    case Size:         return tr("Size");
    case Color:        return tr("Color");
    case Title:        return tr("Title");
    case SizeImage:    return tr("Size image");
    case APlusContent: return tr("A+ content");
    case SizeTable:    return tr("Size table");
    default:           return {};
    }
}

Qt::ItemFlags TreeSizingAsins::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    // SizeTable column is displayed as a checkbox only on child rows
    // (parent rows don't expose the checkbox).
    if (index.column() == SizeTable && index.internalId() != kTopLevelId) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}
