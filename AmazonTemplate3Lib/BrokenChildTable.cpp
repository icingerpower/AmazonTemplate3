#include "BrokenChildTable.h"
#include "apis/AmazonCatalogApi.h"

#include <QBrush>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace {
constexpr auto kHealthFile = "broken_child_health.json";
} // namespace

BrokenChildTable::BrokenChildTable(QObject *parent)
    : QAbstractTableModel(parent)
{}

void BrokenChildTable::setMarketplaces(const QList<MarketplaceSpec> &specs)
{
    beginResetModel();
    m_specs = specs;
    endResetModel();
}

void BrokenChildTable::clear()
{
    ++m_generation;
    beginResetModel();
    m_rows.clear();
    m_maxImages.clear();
    endResetModel();
}

QCoro::Task<void> BrokenChildTable::populate(AmazonCatalogApi *api,
                                              QList<ChildEntry> initialChildren)
{
    const int myGen = ++m_generation;

    beginResetModel();
    m_rows.clear();
    m_maxImages.clear();
    for (ChildEntry &e : initialChildren) {
        e.health.fill({}, m_specs.size());
        m_rows.append(e);
    }
    endResetModel();

    for (int ri = 0; ri < m_rows.size(); ++ri) {
        for (int mi = 0; mi < m_specs.size(); ++mi) {
            AmazonCatalogApi::ChildHealthInfo info;
            co_await api->fetchChildHealth(m_rows[ri].asin, m_specs[mi].id, &info);
            if (m_generation != myGen)
                co_return;

            m_rows[ri].health[mi].loaded     = true;
            m_rows[ri].health[mi].exists     = info.exists;
            m_rows[ri].health[mi].hasParent  = !info.parentAsin.isEmpty();
            m_rows[ri].health[mi].imageCount = info.imageCount;

            if (m_rows[ri].parentAsin.isEmpty() && !info.parentAsin.isEmpty())
                m_rows[ri].parentAsin = info.parentAsin;

            // Size priority: FR > US > DE > anything else
            if (!info.size.isEmpty()) {
                const QString &code = m_specs[mi].code;
                const QString &cur  = m_rows[ri].sizeSource;
                const bool take =
                    cur.isEmpty()
                    || (code == QLatin1String("FR"))
                    || (code == QLatin1String("US") && cur != QLatin1String("FR"))
                    || (code == QLatin1String("DE") && cur != QLatin1String("FR")
                                                    && cur != QLatin1String("US"));
                if (take) {
                    m_rows[ri].size       = info.size;
                    m_rows[ri].sizeSource = code;
                }
            }

            _recomputeMaxImages();
            emit dataChanged(index(ri, 0), index(ri, columnCount() - 1));
        }
    }
    co_return;
}

void BrokenChildTable::_recomputeMaxImages()
{
    m_maxImages.clear();
    for (const ChildEntry &row : m_rows) {
        const QString ck = row.color.toLower();
        if (!m_maxImages.contains(ck))
            m_maxImages[ck].fill(0, m_specs.size());
        QVector<int> &v = m_maxImages[ck];
        for (int mi = 0; mi < m_specs.size() && mi < row.health.size(); ++mi) {
            // Only count cells where the ASIN actually exists in the marketplace —
            // MISSING cells should not influence the per-color "max image count".
            if (row.health[mi].loaded && row.health[mi].exists)
                v[mi] = qMax(v[mi], row.health[mi].imageCount);
        }
    }
}

QList<BrokenChildTable::FixTarget>
BrokenChildTable::getFixTargets(bool forParents, bool forImages) const
{
    QList<FixTarget> targets;
    for (int ri = 0; ri < m_rows.size(); ++ri) {
        const ChildEntry &row = m_rows[ri];
        const QString ck = row.color.toLower();
        for (int mi = 0; mi < m_specs.size() && mi < row.health.size(); ++mi) {
            if (!m_specs[mi].active) continue; // seller has no listing here — skip fix
            const MarketplaceHealth &h = row.health[mi];
            if (!h.loaded || !h.exists) continue;

            const int maxImgs = m_maxImages.value(ck).value(mi, 0);

            const bool needsParent = forParents && !h.hasParent;
            // Image count is "wrong" only when the listing HAS some images but fewer
            // than the family max (0 < count < max). count == 0 usually means the
            // page was never created — nothing to copy images onto — so it is not an
            // image-fix target (it's flagged dark-orange in the table instead).
            const bool needsImages = forImages && maxImgs > 0
                                     && h.imageCount > 0 && h.imageCount < maxImgs;
            if (!needsParent && !needsImages) continue;

            targets.append({ri, mi, needsParent, needsImages});
        }
    }
    return targets;
}

QString BrokenChildTable::bestImageSourceAsin(const QString &colorKey, int mktIdx) const
{
    if (mktIdx < 0 || mktIdx >= m_specs.size())
        return {};

    QString bestAsin;
    int bestCount = 0;
    for (const ChildEntry &row : m_rows) {
        if (row.color.toLower() != colorKey) continue;
        if (mktIdx >= row.health.size()) continue;
        const MarketplaceHealth &h = row.health[mktIdx];
        if (!h.loaded || !h.exists) continue;
        if (h.imageCount > bestCount) {
            bestCount = h.imageCount;
            bestAsin  = row.asin;
        }
    }
    return bestAsin;
}

QColor BrokenChildTable::_cellColor(int row, int mi) const
{
    if (row < 0 || row >= m_rows.size() || mi < 0 || mi >= m_specs.size())
        return {};
    const MarketplaceHealth &h = m_rows[row].health[mi];
    if (!h.loaded)
        return {};
    // MISSING cells (ASIN not in this marketplace) are not "broken" — leave them
    // un-highlighted; the displayed "MISSING" label conveys the state.
    if (!h.exists)
        return {};
    const QString ck = m_rows[row].color.toLower();
    const int maxImgs = m_maxImages.value(ck).value(mi, 0);
    // No images at all usually means the page was never created on this
    // marketplace — flag dark orange (distinct from a genuine broken cell).
    if (h.imageCount == 0)
        return QColor(160, 90, 0);
    // Broken parent link, or a partial image set (some images but fewer than the
    // family max) — a genuine "needs fixing" cell → red.
    if (!h.hasParent || (maxImgs > 0 && h.imageCount < maxImgs))
        return QColor(160, 20, 20);
    return {};
}

void BrokenChildTable::saveToDir(const QDir &dir) const
{
    if (!dir.exists()) return;

    QJsonArray specsArr;
    for (const MarketplaceSpec &sp : m_specs)
        specsArr.append(sp.id);

    QJsonArray rowsArr;
    for (const ChildEntry &row : m_rows) {
        QJsonObject rObj;
        rObj[QStringLiteral("asin")]       = row.asin;
        rObj[QStringLiteral("sku")]        = row.sku;
        rObj[QStringLiteral("parentAsin")] = row.parentAsin;
        rObj[QStringLiteral("parentSku")]  = row.parentSku;
        rObj[QStringLiteral("color")]      = row.color;
        rObj[QStringLiteral("size")]       = row.size;
        rObj[QStringLiteral("sizeSource")] = row.sizeSource;
        QJsonArray hArr;
        for (const MarketplaceHealth &h : row.health) {
            QJsonObject hObj;
            hObj[QStringLiteral("loaded")]     = h.loaded;
            hObj[QStringLiteral("exists")]     = h.exists;
            hObj[QStringLiteral("hasParent")]  = h.hasParent;
            hObj[QStringLiteral("imageCount")] = h.imageCount;
            hArr.append(hObj);
        }
        rObj[QStringLiteral("health")] = hArr;
        rowsArr.append(rObj);
    }

    QJsonObject root;
    root[QStringLiteral("timestamp")]    = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    root[QStringLiteral("marketplaces")] = specsArr;
    root[QStringLiteral("rows")]         = rowsArr;

    QSaveFile f(dir.filePath(QString::fromLatin1(kHealthFile)));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
}

bool BrokenChildTable::loadFromDir(const QDir &dir)
{
    if (!dir.exists()) return false;
    QFile f(dir.filePath(QString::fromLatin1(kHealthFile)));
    if (!f.open(QIODevice::ReadOnly)) return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const QJsonObject root = doc.object();

    // Reject stale data if the marketplace list no longer matches.
    const QJsonArray savedSpecs = root.value(QStringLiteral("marketplaces")).toArray();
    if (savedSpecs.size() != m_specs.size()) return false;
    for (int i = 0; i < m_specs.size(); ++i) {
        if (savedSpecs[i].toString() != m_specs[i].id) return false;
    }

    QList<ChildEntry> loaded;
    for (const QJsonValue &rv : root.value(QStringLiteral("rows")).toArray()) {
        const QJsonObject rObj = rv.toObject();
        ChildEntry e;
        e.asin       = rObj.value(QStringLiteral("asin")).toString();
        e.sku        = rObj.value(QStringLiteral("sku")).toString();
        e.parentAsin = rObj.value(QStringLiteral("parentAsin")).toString();
        e.parentSku  = rObj.value(QStringLiteral("parentSku")).toString();
        e.color      = rObj.value(QStringLiteral("color")).toString();
        e.size       = rObj.value(QStringLiteral("size")).toString();
        e.sizeSource = rObj.value(QStringLiteral("sizeSource")).toString();
        const QJsonArray hArr = rObj.value(QStringLiteral("health")).toArray();
        e.health.resize(m_specs.size());
        for (int mi = 0; mi < hArr.size() && mi < m_specs.size(); ++mi) {
            const QJsonObject hObj = hArr[mi].toObject();
            e.health[mi].loaded     = hObj.value(QStringLiteral("loaded")).toBool();
            // Default missing "exists" key to true to remain compatible with files
            // written before the field existed (those entries were assumed present).
            e.health[mi].exists     = hObj.value(QStringLiteral("exists")).toBool(true);
            e.health[mi].hasParent  = hObj.value(QStringLiteral("hasParent")).toBool();
            e.health[mi].imageCount = hObj.value(QStringLiteral("imageCount")).toInt();
        }
        loaded.append(e);
    }

    beginResetModel();
    m_rows = std::move(loaded);
    _recomputeMaxImages();
    endResetModel();
    return true;
}

void BrokenChildTable::setMarketplaceActive(const QString &marketplaceId, bool active)
{
    for (auto &spec : m_specs) {
        if (spec.id == marketplaceId) {
            spec.active = active;
            return;
        }
    }
}

void BrokenChildTable::updateSkus(const QHash<QString, QString> &asinToSku)
{
    for (int ri = 0; ri < m_rows.size(); ++ri) {
        ChildEntry &row = m_rows[ri];
        bool changed = false;

        if (row.sku.isEmpty()) {
            const QString sku = asinToSku.value(row.asin);
            if (!sku.isEmpty()) { row.sku = sku; changed = true; }
        }
        if (row.parentSku.isEmpty() && !row.parentAsin.isEmpty()) {
            const QString pSku = asinToSku.value(row.parentAsin);
            if (!pSku.isEmpty()) { row.parentSku = pSku; changed = true; }
        }

        if (changed)
            emit dataChanged(index(ri, ColSku), index(ri, ColParentSku));
    }
}

int BrokenChildTable::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int BrokenChildTable::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : kFixedCols + m_specs.size();
}

QVariant BrokenChildTable::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_rows.size())
        return {};
    const ChildEntry &row = m_rows[idx.row()];
    const int col = idx.column();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (col < kFixedCols) {
            switch (col) {
            case ColAsin:       return row.asin;
            case ColSku:        return row.sku;
            case ColParentAsin: return row.parentAsin;
            case ColParentSku:  return row.parentSku;
            case ColColor:      return row.color;
            case ColSize:
                if (row.size.isEmpty()) return {};
                return row.sizeSource.isEmpty()
                    ? row.size
                    : QStringLiteral("%1 (%2)").arg(row.size, row.sizeSource);
            default: return {};
            }
        }
        const int mi = col - kFixedCols;
        if (mi >= row.health.size()) return {};
        const MarketplaceHealth &h = row.health[mi];
        if (!h.loaded) return QStringLiteral("…"); // …
        if (!h.exists) return tr("MISSING");
        return QStringLiteral("%1 %2")
            .arg(h.hasParent ? QStringLiteral("✓") : QStringLiteral("✗")) // ✓ / ✗
            .arg(h.imageCount);
    }

    if (role == Qt::BackgroundRole && col >= kFixedCols) {
        const QColor c = _cellColor(idx.row(), col - kFixedCols);
        if (c.isValid()) return QBrush(c);
    }

    if (role == Qt::ForegroundRole && col >= kFixedCols) {
        const QColor c = _cellColor(idx.row(), col - kFixedCols);
        if (c.isValid()) return QBrush(Qt::white);
    }

    return {};
}

QVariant BrokenChildTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    if (section < kFixedCols) {
        switch (section) {
        case ColAsin:       return tr("ASIN");
        case ColSku:        return tr("SKU");
        case ColParentAsin: return tr("Parent ASIN");
        case ColParentSku:  return tr("Parent SKU");
        case ColColor:      return tr("Color");
        case ColSize:       return tr("Size");
        default:            return {};
        }
    }
    const int mi = section - kFixedCols;
    return mi < m_specs.size() ? QVariant(m_specs[mi].code) : QVariant{};
}

Qt::ItemFlags BrokenChildTable::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    // Columns 0-3 (ASIN, SKU, Parent ASIN, Parent SKU) open an inline editor on
    // double-click so the user can select and copy the text. Changes are not saved.
    if (index.column() < 4)
        f |= Qt::ItemIsEditable;
    return f;
}

bool BrokenChildTable::setData(const QModelIndex &, const QVariant &, int)
{
    // Editing is for copy-paste only — no changes are persisted.
    return false;
}
