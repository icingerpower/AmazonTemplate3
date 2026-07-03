#include "TreeSizingAsins.h"

#include <QMap>
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

// Map well-known non-English single-word color names to their English canonical
// form so that "Schwarz" and "Black" (or "Noir" and "Black") dedup correctly
// when Amazon's UK normalisation step didn't cover every child ASIN.
// Only unambiguous, single-word, case-insensitive matches are handled; compound
// colours such as "Schwarz-Weiß" are left unchanged because the English compound
// may differ structurally.
static QString canonicalColorKey(const QString &colorLower)
{
    static const QHash<QString, QString> kMap = {
        // German
        {QStringLiteral("schwarz"),  QStringLiteral("black")},
        {QStringLiteral("weiß"),     QStringLiteral("white")},
        {QStringLiteral("weiss"),    QStringLiteral("white")},
        {QStringLiteral("grau"),     QStringLiteral("grey")},
        {QStringLiteral("silber"),   QStringLiteral("silver")},
        {QStringLiteral("gold"),     QStringLiteral("gold")},
        {QStringLiteral("blau"),     QStringLiteral("blue")},
        {QStringLiteral("rot"),      QStringLiteral("red")},
        {QStringLiteral("gelb"),     QStringLiteral("yellow")},
        {QStringLiteral("grün"),     QStringLiteral("green")},
        {QStringLiteral("gruen"),    QStringLiteral("green")},
        {QStringLiteral("braun"),    QStringLiteral("brown")},
        {QStringLiteral("marine"),   QStringLiteral("navy")},
        // French
        {QStringLiteral("noir"),     QStringLiteral("black")},
        {QStringLiteral("blanc"),    QStringLiteral("white")},
        {QStringLiteral("gris"),     QStringLiteral("grey")},
        {QStringLiteral("argent"),   QStringLiteral("silver")},
        {QStringLiteral("bleu"),     QStringLiteral("blue")},
        {QStringLiteral("rouge"),    QStringLiteral("red")},
        {QStringLiteral("jaune"),    QStringLiteral("yellow")},
        {QStringLiteral("vert"),     QStringLiteral("green")},
        {QStringLiteral("marron"),   QStringLiteral("brown")},
        {QStringLiteral("brun"),     QStringLiteral("brown")},
        // Italian
        {QStringLiteral("nero"),     QStringLiteral("black")},
        {QStringLiteral("bianco"),   QStringLiteral("white")},
        {QStringLiteral("grigio"),   QStringLiteral("grey")},
        {QStringLiteral("rosso"),    QStringLiteral("red")},
        {QStringLiteral("blu"),      QStringLiteral("blue")},
        {QStringLiteral("giallo"),   QStringLiteral("yellow")},
        {QStringLiteral("verde"),    QStringLiteral("green")},
        {QStringLiteral("marrone"),  QStringLiteral("brown")},
        // Spanish
        {QStringLiteral("negro"),    QStringLiteral("black")},
        {QStringLiteral("blanco"),   QStringLiteral("white")},
        {QStringLiteral("gris"),     QStringLiteral("grey")},
        {QStringLiteral("plata"),    QStringLiteral("silver")},
        {QStringLiteral("azul"),     QStringLiteral("blue")},
        {QStringLiteral("rojo"),     QStringLiteral("red")},
        {QStringLiteral("amarillo"), QStringLiteral("yellow")},
        {QStringLiteral("verde"),    QStringLiteral("green")},
        {QStringLiteral("marrón"),   QStringLiteral("brown")},
        {QStringLiteral("marron"),   QStringLiteral("brown")},
    };
    return kMap.value(colorLower, colorLower);
}

// Representative marketplace ID per geographic region, tried in order when
// auto-detecting which region a product belongs to.
// EU markets are tried first because most products managed here are EU-listed.
// This prevents US returning a partial catalog hit (parent ASIN known globally
// but not actually listed in NA) from being mistaken for a successful NA load,
// which would cause the EU existence check to run against UK and fail for
// products that are only on DE/FR/IT/ES.
const QStringList kRegionMarketplaces = QStringList()
    << QStringLiteral("A1PA6795UKMFR9")   // DE  (EU) — try first, largest EU market
    << QStringLiteral("A13V1IB3VIYZZH")   // FR  (EU)
    << QStringLiteral("A1F83G8C2ARO7P")   // UK  (EU)
    << QStringLiteral("APJ6JRA9NG5V4")    // IT  (EU)
    << QStringLiteral("A1RKKUPIHCS9HS")   // ES  (EU)
    << QStringLiteral("ATVPDKIKX0DER")    // US  (NA)
    << QStringLiteral("A2EUQ1WTGCTBG2")   // CA  (NA)
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

QMap<QString, QString> TreeSizingAsins::_readAsinsFromXlsx(const QString& xlsxPath) const
{
    QMap<QString, QString> result;
    QXlsx::Document doc(xlsxPath);
    if (!doc.load()) {
        qWarning() << "TreeSizingAsins: failed to load xlsx" << xlsxPath;
        return result;
    }

    const QRegularExpression asinRegex("^[A-Z0-9]{10}$");

    // Find ASIN and SKU columns: scan the first 10 rows for header cells.
    int asinCol = -1;
    int skuCol  = -1;
    for (int row = 1; row <= 10 && (asinCol == -1 || skuCol == -1); ++row) {
        for (int col = 1; col <= 500; ++col) {
            const QVariant v = doc.read(row, col);
            if (!v.isValid()) continue;
            const QString s = v.toString().trimmed();
            if (s.isEmpty()) continue;

            if (asinCol == -1 && (s.compare("asin", Qt::CaseInsensitive) == 0
                                  || s.compare("external_product_id", Qt::CaseInsensitive) == 0)) {
                asinCol = col;
            } else if (skuCol == -1 && (s.compare("seller_sku", Qt::CaseInsensitive) == 0
                                        || s.compare("item_sku", Qt::CaseInsensitive) == 0
                                        || s.compare("sku", Qt::CaseInsensitive) == 0)) {
                skuCol = col;
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
                    result.insert(s, QString{});
            }
        }
        return result;
    }

    // Read the columns, skipping header rows.
    for (int row = 2; row <= 5000; ++row) {
        const QVariant vAsin = doc.read(row, asinCol);
        if (!vAsin.isValid()) continue;
        const QString sAsin = vAsin.toString().trimmed();
        if (sAsin.isEmpty()) continue;

        if (asinRegex.match(sAsin).hasMatch() && !result.contains(sAsin)) {
            QString sSku;
            if (skuCol != -1) {
                const QVariant vSku = doc.read(row, skuCol);
                if (vSku.isValid()) sSku = vSku.toString().trimmed();
            }
            result.insert(sAsin, sSku);
        }
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

    QMap<QString, QString> asinsWithSkus;
    if (asinOrXlsxPath.endsWith(".xlsx", Qt::CaseInsensitive)) {
        asinsWithSkus = _readAsinsFromXlsx(asinOrXlsxPath);
    } else {
        asinsWithSkus.insert(asinOrXlsxPath, QString{});
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
    static const QString kEuRep = QStringLiteral("A1PA6795UKMFR9"); // DE
    static const QString kNaRep = QStringLiteral("ATVPDKIKX0DER");  // US
    static const QString kJpRep = QStringLiteral("A1VC38T7YXB528");  // JP

    QList<ParentItem> newFamilies;
    QSet<QString> seenParents;
    bool attributesEmitted = false;
    int  succeededRegionIdx = -1;
    QString probeChildAsin;
    for (auto it = asinsWithSkus.constBegin(); it != asinsWithSkus.constEnd(); ++it) {
        const QString asin = it.key();
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
        if (p.sku.isEmpty()) p.sku = asinsWithSkus.value(p.asin);

        // Regex shared across children: "XL=42", "M=10", etc.
        static const QRegularExpression kSizeRe(QStringLiteral(R"(([A-Z]{1,3})\s*=\s*\d+)"));

        for (const auto& c : family.children) {
            ChildItem ci;
            ci.asin         = c.asin;
            ci.sku          = c.sku;
            if (ci.sku.isEmpty()) ci.sku = asinsWithSkus.value(ci.asin);
            ci.size         = c.size;
            ci.color        = c.color;
            ci.title        = c.title;
            ci.hasSizeTable = c.hasSizeTable;

            // If the Amazon catalog API didn't return a size attribute, try to
            // extract it from the title (e.g. "Abaya… XL=42" → "XL").
            if (ci.size.isEmpty() && !ci.title.isEmpty()) {
                const auto m = kSizeRe.match(ci.title);
                if (m.hasMatch())
                    ci.size = m.captured(1);
            }

            p.children.append(ci);
        }
        _applyDatesToFamily(p);
        newFamilies.append(p);

        // Emit bullet points + material for the first child of the first family
        if (!attributesEmitted && !family.children.isEmpty()) {
            probeChildAsin = family.children.first().asin;

            // Collect unique shoe widths from ALL children (shoe_width varies per size variant).
            static const QString kWidthPrefix = QStringLiteral("Shoe width: ");
            QStringList shoeWidths;
            for (const auto& c : family.children) {
                for (const QString& ma : c.materialAttrs) {
                    if (!ma.startsWith(kWidthPrefix)) continue;
                    const QStringList vals = ma.mid(kWidthPrefix.length()).split(
                        QStringLiteral(", "), Qt::SkipEmptyParts);
                    for (const QString& v : vals) {
                        const QString t = v.trimmed();
                        if (!t.isEmpty() && !shoeWidths.contains(t, Qt::CaseInsensitive))
                            shoeWidths.append(t);
                    }
                }
            }

            // Remove shoe_width from the first child's material list to avoid duplication —
            // PaneSizing will add a deduplicated single line from shoeWidths instead.
            QStringList filteredMaterial;
            for (const QString& ma : family.children.first().materialAttrs)
                if (!ma.startsWith(kWidthPrefix))
                    filteredMaterial.append(ma);

            emit attributesFetched(family.children.first().bulletPoints,
                                   filteredMaterial,
                                   family.children.first().mainImageUrl,
                                   family.parentAsin,
                                   family.children.first().title,
                                   shoeWidths);

            // Emit images grouped by unique color. Dedup by canonical color key
            // (lowercased + cross-language translation) so that "Black" and "Schwarz"
            // are treated as the same color when Amazon's UK normalisation step didn't
            // cover every child ASIN. Image-URL-based dedup is unreliable: Amazon
            // frequently returns the parent product's images for all children regardless
            // of color, making distinct colors appear identical.
            // Two-pass approach: first build a canonical-key → children map so we can
            // prefer the English display name when multiple language variants exist.
            using AsinItem = AmazonCatalogApi::AsinItem;
            QMap<QString, QList<const AsinItem *>> canonicalGroups;
            QList<QString> canonicalOrder; // insertion-order for stable output
            for (const auto& c : family.children) {
                if (c.color.isEmpty()) continue;
                const QString canon = canonicalColorKey(c.color.toLower());
                if (!canonicalGroups.contains(canon))
                    canonicalOrder.append(canon);
                canonicalGroups[canon].append(&c);
            }

            QList<QPair<QString, QStringList>> colorImages;
            QSet<QString> seenColors;

            QStringList logLines;
            logLines << tr("Color detection for %1:").arg(family.parentAsin);

            for (const QString &canon : canonicalOrder) {
                const QList<const AsinItem *> &group = canonicalGroups[canon];
                // Prefer the English (un-translated) name; fall back to first seen.
                QString displayColor = group.first()->color;
                for (const AsinItem *child : group) {
                    if (canonicalColorKey(child->color.toLower()) == child->color.toLower()) {
                        displayColor = child->color; // exact canonical = English word
                        break;
                    }
                }
                if (seenColors.contains(canon)) continue;
                seenColors.insert(canon);
                // Use the first child's images (most complete).
                colorImages.append(qMakePair(displayColor, group.first()->allImageUrls));
                if (group.size() > 1) {
                    QStringList aliases;
                    for (const AsinItem *ch : group) aliases << ch->color;
                    logLines << tr("  ADD   \"%1\" (merged: %2) — images: %3")
                                .arg(displayColor)
                                .arg(aliases.join(QStringLiteral(", ")))
                                .arg(group.first()->allImageUrls.size());
                } else {
                    logLines << tr("  ADD   \"%1\" — images: %2")
                                .arg(displayColor)
                                .arg(group.first()->allImageUrls.size());
                }
            }

            logLines << tr("  → %1 distinct color(s): %2")
                        .arg(colorImages.size())
                        .arg([&]{
                            QStringList names;
                            for (const auto& p : colorImages) names << p.first;
                            return names.join(QStringLiteral(", "));
                        }());
            emit colorLog(logLines.join(QLatin1Char('\n')));

            if (!colorImages.isEmpty())
                emit variantImagesFetched(colorImages);

            // Emit color → child ASINs map (all sizes per color).
            QMap<QString, QStringList> colorToAsins;
            for (const auto& c : family.children) {
                if (!c.asin.isEmpty())
                    colorToAsins[c.color.toLower()].append(c.asin);
            }
            if (!colorToAsins.isEmpty())
                emit colorAsinsReady(colorToAsins);

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

void TreeSizingAsins::setSku(const QString& asin, const QString& sku)
{
    for (int fi = 0; fi < m_families.size(); ++fi) {
        ParentItem& p = m_families[fi];
        if (p.asin == asin) {
            if (p.sku == sku) return;
            p.sku = sku;
            const QModelIndex idx = _makeTopIndex(fi, SKU);
            emit dataChanged(idx, idx, {Qt::DisplayRole});
            return;
        }
        for (int ci = 0; ci < p.children.size(); ++ci) {
            ChildItem& c = p.children[ci];
            if (c.asin == asin) {
                if (c.sku == sku) return;
                c.sku = sku;
                const QModelIndex idx = _makeChildIndex(fi, ci, SKU);
                emit dataChanged(idx, idx, {Qt::DisplayRole});
                return;
            }
        }
    }
}

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

        if (role == Qt::DisplayRole || role == Qt::EditRole) {
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

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
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
    if (index.column() == SizeTable && index.internalId() != kTopLevelId) {
        f |= Qt::ItemIsUserCheckable;
    }
    // SKU and ASIN columns are editable so the user can double-click to select
    // and copy the value; setData() ignores these edits intentionally.
    if (index.column() == SKU || index.column() == ASIN) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}
