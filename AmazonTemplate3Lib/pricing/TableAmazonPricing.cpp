#include "TableAmazonPricing.h"
#include <QBrush>
#include <QFileInfo>
#include <QPixmap>
#include <QPixmapCache>
#include <limits>

TableAmazonPricing::TableAmazonPricing(QObject *parent) : QAbstractTableModel(parent) {}
void TableAmazonPricing::setMarkets(const QList<Pricing::Market> &markets) {
    beginResetModel();
    m_markets = markets;
    rebuildGroups();
    endResetModel();
}
void TableAmazonPricing::setMode(Mode mode) {
    if (mode == m_mode)
        return;
    beginResetModel();
    m_mode = mode;
    rebuildGroups();
    endResetModel();
}
void TableAmazonPricing::rebuildGroups() {
    m_groups.clear();
    m_allPrices = {"all", tr("All (EUR)"), {}};
    for (int i = 0; i < m_markets.size(); ++i) {
        const auto &m = m_markets[i];
        if (!m.enabled)
            continue;
        m_allPrices.markets << i;
        const QString key = m_mode == OnePrice ? "all" : m_mode == Continent ? m.continent : m.id;
        int group = -1;
        for (int j = 0; j < m_groups.size(); ++j)
            if (m_groups[j].id == key)
                group = j;
        if (group < 0) {
            group = m_groups.size();
            const QString label = m_mode == OnePrice    ? tr("All (EUR)")
                                  : m_mode == Continent ? m.continent + " (EUR)"
                                                        : m.country + " (" + m.currency + ')';
            m_groups << Group{key, label, {}};
        }
        m_groups[group].markets << i;
    }
}
void TableAmazonPricing::setRows(QList<Pricing::Row> rows, bool preserveEdits) {
    QHash<QString, int> previous;
    for (int i = 0; i < m_rows.size(); ++i)
        previous[m_rows[i].sku] = i;
    for (auto &row : rows)
        if (preserveEdits && previous.contains(row.sku)) {
            const auto &old = m_rows[previous.value(row.sku)];
            row.manualPrices = old.manualPrices;
            row.resetMarkets = old.resetMarkets;
        }
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}
void TableAmazonPricing::updateRow(const Pricing::Row &row) {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].sku == row.sku) {
            auto replacement = row;
            replacement.manualPrices = m_rows[i].manualPrices;
            replacement.resetMarkets = m_rows[i].resetMarkets;
            m_rows[i] = replacement;
            emit dataChanged(index(i, 0), index(i, columnCount() - 1));
            return;
        }
    beginInsertRows({}, m_rows.size(), m_rows.size());
    m_rows << row;
    endInsertRows();
}
void TableAmazonPricing::setRules(double defaultEur, QHash<QString, double> overrides,
                                  Pricing::Direction direction) {
    m_defaultEur = defaultEur;
    m_overrides = std::move(overrides);
    m_direction = direction;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, columnCount() - 1));
}
double TableAmazonPricing::proposed(const Pricing::Row &row, int market) const {
    if (market < 0 || market >= m_markets.size() || !Pricing::validPrice(m_defaultEur))
        return -1;
    const auto &m = m_markets[market];
    const auto l = row.listings.value(m.id);
    if (!m.enabled || !l.exists || row.resetMarkets.contains(m.id))
        return -1;
    double value =
        row.manualPrices.value(m.id, m_overrides.value(m.id, m.rate > 0 ? m_defaultEur * m.rate : -1));
    if (!Pricing::allowed(l.price, value, m_direction))
        return -1;
    return Pricing::rounded(value);
}
void TableAmazonPricing::resetRows(const QSet<QString> &skus) {
    for (auto &row : m_rows)
        if (skus.contains(row.sku))
            for (const auto &m : m_markets)
                if (m.enabled) {
                    row.manualPrices.remove(m.id);
                    row.resetMarkets.insert(m.id);
                }
    if (!m_rows.isEmpty())
        emit dataChanged(index(0, 0), index(m_rows.size() - 1, columnCount() - 1));
}
void TableAmazonPricing::resetMarket(const QString &sku, const QString &mp) {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].sku == sku) {
            m_rows[i].manualPrices.remove(mp);
            m_rows[i].resetMarkets.insert(mp);
            emit dataChanged(index(i, 0), index(i, columnCount() - 1));
            break;
        }
}
int TableAmazonPricing::rowCount(const QModelIndex &p) const {
    return p.isValid() ? 0 : m_rows.size();
}
int TableAmazonPricing::columnCount(const QModelIndex &p) const {
    return p.isValid() ? 0 : FixedCount + (m_mode == OnePrice ? 1 : 2) * m_groups.size() + 2;
}
QString TableAmazonPricing::columnKey(int c) const {
    const QStringList fixed = {"image",   "sku",     "title", "mainPrice", "all:price",
                               "changes", "sales90", "size",  "days",      "color"};
    if (c < 0 || c >= columnCount())
        return {};
    if (c < FixedCount)
        return fixed[c];
    if (c == columnCount() - 2)
        return "created";
    if (c == columnCount() - 1)
        return "asin";
    return m_groups[priceOffset(c) / 2].id + (priceOffset(c) % 2 ? ":new" : ":price");
}
int TableAmazonPricing::columnForKey(const QString &key) const {
    for (int i = 0; i < columnCount(); ++i)
        if (columnKey(i) == key)
            return i;
    return Sku;
}
QVariant TableAmazonPricing::headerData(int c, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || c < 0 || c >= columnCount())
        return {};
    const QStringList fixed = {tr("Image"),
                               tr("SKU"),
                               tr("Title"),
                               tr("Main price"),
                               tr("All (EUR) Price"),
                               tr("Changes to apply"),
                               tr("Last 90 days sales"),
                               tr("Size (FR)"),
                               tr("Est. days inv."),
                               tr("Color")};
    if (c < FixedCount)
        return fixed[c];
    if (c == columnCount() - 2)
        return tr("Creation date");
    if (c == columnCount() - 1)
        return tr("ASIN");
    return m_groups[priceOffset(c) / 2].label + (priceOffset(c) % 2 ? tr(" New Price") : tr(" Price"));
}
QList<TableAmazonPricing::ChangeGroup> TableAmazonPricing::changeGroups(const Pricing::Row &row) const {
    QList<ChangeGroup> result;
    for (int i = 0; i < m_markets.size(); ++i) {
        const auto &m = m_markets[i];
        const double value = proposed(row, i);
        if (value <= 0)
            continue;
        const double eur = m.rate > 0 ? Pricing::rounded(value / m.rate) : -1;
        const bool increase = value > row.listings.value(m.id).price;
        auto match = std::find_if(result.begin(), result.end(),
                                  [&](const auto &g) { return g.eur == eur && g.increase == increase; });
        if (match == result.end())
            result << ChangeGroup{eur, increase, {m.country}};
        else
            match->countries << m.country;
    }
    for (auto &g : result)
        g.countries.sort();
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.increase != b.increase)
            return a.increase;
        return a.eur < b.eur;
    });
    return result;
}
QVariant TableAmazonPricing::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_rows.size() || idx.column() >= columnCount())
        return {};
    const auto &row = m_rows[idx.row()];
    const int c = idx.column();
    if (role == RowKeyRole)
        return row.sku;
    if (role == Qt::SizeHintRole)
        return QSize(c == Image     ? 58
                     : c == Changes ? 380
                                    : 100,
                     c == Changes ? qMax(58, int(changeGroups(row).size()) * 26 + 8) : 58);
    if (c == Image && role == Qt::DecorationRole && !row.imagePath.isEmpty()) {
        QPixmap px;
        if (!QPixmapCache::find(row.imagePath, &px)) {
            px.load(row.imagePath);
            px = px.scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPixmapCache::insert(row.imagePath, px);
        }
        return px;
    }
    if (c == MainPrice) {
        for (const QString &country :
             {QString("FR"), QString("DE"), QString("US"), QString("CA"), QString("JP")}) {
            for (const auto &m : m_markets) {
                const auto listing = row.listings.value(m.id);
                if (m.country != country || !listing.exists || !Pricing::validPrice(listing.price))
                    continue;
                const double rate = m.currency == "EUR" ? 1 : m.rate;
                if (role == SortRole)
                    return rate > 0 ? QVariant(listing.price / rate) : QVariant{};
                if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
                    QString price = QString::number(listing.price, 'f', 2) + " €";
                    if (m.currency != "EUR") {
                        const QString converted =
                            rate > 0 ? QString::number(listing.price / rate, 'f', 2) : QString("—");
                        price = QString("%1 %2 (conv %3 EUR)")
                                    .arg(listing.price, 0, 'f', 2)
                                    .arg(m.currency, converted);
                        if (role == Qt::ToolTipRole && rate <= 0)
                            price += tr("\nClick Refresh exchange rates to calculate the EUR conversion.");
                    }
                    return country + " · " + price;
                }
                return {};
            }
        }
        return {};
    }
    if (c == Changes) {
        if (role == Qt::ToolTipRole) {
            QStringList lines;
            for (int i = 0; i < m_markets.size(); ++i) {
                const double value = proposed(row, i);
                if (value <= 0)
                    continue;
                const auto &m = m_markets[i];
                const double old = row.listings.value(m.id).price;
                lines << QString("%1 %2: %3 → %4 %5%6")
                             .arg(value > old ? "↑" : "↓", m.country)
                             .arg(old, 0, 'f', 2)
                             .arg(value, 0, 'f', 2)
                             .arg(m.currency, row.manualPrices.contains(m.id) ? tr(" (manual)") : QString());
            }
            return lines.isEmpty() ? tr("No eligible changes. Check the default, current prices, direction "
                                        "rule and resets.")
                                   : lines.join('\n');
        }
        const auto groups = changeGroups(row);
        if (role == SortRole) {
            int count = 0;
            for (const auto &g : groups)
                count += g.countries.size();
            return count;
        }
        if (role != Qt::DisplayRole && role != RichTextRole)
            return {};
        QStringList lines;
        for (const auto &g : groups) {
            const QString line =
                QString("%1 %2 (%3)")
                    .arg(g.increase ? "↑" : "↓",
                         g.eur > 0 ? QString::number(g.eur, 'f', 2) + " €" : tr("EUR unavailable"),
                         g.countries.join(" / "));
            lines << (role == RichTextRole
                          ? QString(R"(<div style="background-color:%1;color:white;">%2</div>)")
                                .arg(g.increase ? "#14532d" : "#991b1b", line.toHtmlEscaped())
                          : line);
        }
        return lines.isEmpty() ? tr("No changes") : lines.join(role == RichTextRole ? "" : "\n");
    }
    if (c == Size && role == Qt::ToolTipRole) {
        QStringList sizes;
        for (const auto &m : m_markets)
            if (m.enabled) {
                const auto converted =
                    Pricing::convertSize(row.sizeFr, "FR", m.country, row.gender, row.age, row.productType);
                sizes << m.country + ": " + (converted.isEmpty() ? tr("Unknown") : converted);
            }
        return sizes.join('\n');
    }
    if ((c < FixedCount && c != AllPrice) || c >= columnCount() - 2) {
        if (role != Qt::DisplayRole && role != SortRole && role != Qt::ToolTipRole)
            return {};
        switch (c) {
        case Image:
            return role == SortRole ? QVariant(row.sku) : QVariant{};
        case Sku:
            return row.sku;
        case Title:
            return row.title();
        case Sales90:
            return row.sales90 < 0 ? QVariant{} : QVariant(row.sales90);
        case Size: {
            if (role != SortRole)
                return row.sizeFr;
            bool letter = false;
            double n = Pricing::sizeOrder(row.sizeFr.section('/', 0, 0), &letter);
            return std::isfinite(n) ? QVariant(n + (letter ? 10000 : 0)) : QVariant{};
        }
        case Days:
            return row.inventoryDays() < 0 ? QVariant{} : QVariant(row.inventoryDays());
        case Color:
            return row.color;
        }
        if (c == columnCount() - 1)
            return row.asin;
        if (!row.created.isValid())
            return {};
        return role == SortRole ? QVariant(row.created) : QVariant(row.created.toString(Qt::ISODate));
    }
    const auto &group = c == AllPrice ? m_allPrices : m_groups[priceOffset(c) / 2];
    const bool isNew = c != AllPrice && priceOffset(c) % 2;
    const bool localCurrency = c != AllPrice && m_mode == AllCountry;
    QList<double> values;
    QStringList details;
    bool manual = false, up = false, down = false;
    for (int i : group.markets) {
        const auto &m = m_markets[i];
        const auto l = row.listings.value(m.id);
        const double applied = isNew ? proposed(row, i) : l.price;
        // Keep a manual edit visible even if the direction rule blocks sending it.
        // proposed() remains the single gate used when preparing API updates.
        const bool visibleManual =
            isNew && Pricing::validPrice(m_defaultEur) && l.exists && Pricing::validPrice(l.price) &&
            Pricing::validPrice(row.manualPrices.value(m.id, -1)) && !row.resetMarkets.contains(m.id);
        const double value = visibleManual ? row.manualPrices.value(m.id) : applied;
        if (value > 0 && (localCurrency || m.rate > 0)) {
            values << (localCurrency ? value : value / m.rate);
            details << QString("%1: %2 %3").arg(m.country).arg(value, 0, 'f', 2).arg(m.currency);
            if (visibleManual && applied < 0)
                details.last() += tr(" (not applied: direction rule or unchanged price)");
            if (isNew) {
                manual |= row.manualPrices.contains(m.id);
                up |= value > l.price;
                down |= value < l.price;
            }
        } else
            details << m.country + ": " + (isNew ? tr("No change") : tr("Unknown price"));
    }
    if (role == Qt::ToolTipRole)
        return details.join('\n');
    if (values.isEmpty())
        return {};
    const double low = *std::min_element(values.begin(), values.end()),
                 high = *std::max_element(values.begin(), values.end());
    if (role == SortRole || role == Qt::EditRole)
        return low;
    if (role == Qt::BackgroundRole && isNew) {
        if (manual)
            return QBrush(QColor("#173f73"));
        // Mixed grouped changes are neutral; each country retains its own color.
        if (up && !down)
            return QBrush(QColor("#14532d"));
        if (down && !up)
            return QBrush(QColor("#9a450b"));
    }
    if (role == Qt::ForegroundRole && isNew && (manual || up != down))
        return QBrush(Qt::white);
    if (role == Qt::DisplayRole)
        return std::abs(low - high) < 0.005 ? QString::number(low, 'f', 2)
                                            : QString("%1 – %2").arg(low, 0, 'f', 2).arg(high, 0, 'f', 2);
    return {};
}
Qt::ItemFlags TableAmazonPricing::flags(const QModelIndex &i) const {
    if (!i.isValid())
        return Qt::NoItemFlags;
    auto f = QAbstractTableModel::flags(i);
    if (i.column() >= FixedCount && i.column() < columnCount() - 2 && priceOffset(i.column()) % 2 &&
        Pricing::validPrice(m_defaultEur))
        f |= Qt::ItemIsEditable;
    return f;
}
bool TableAmazonPricing::setData(const QModelIndex &idx, const QVariant &v, int role) {
    if (role != Qt::EditRole || !(flags(idx) & Qt::ItemIsEditable))
        return false;
    bool ok = false;
    const double value = v.toDouble(&ok);
    const bool clear = v.toString().trimmed().isEmpty();
    if (!clear && (!ok || !Pricing::validPrice(value)))
        return false;
    auto &row = m_rows[idx.row()];
    const auto &group = m_groups[priceOffset(idx.column()) / 2];
    for (int i : group.markets) {
        const auto &m = m_markets[i];
        if (clear) {
            row.manualPrices.remove(m.id);
            row.resetMarkets.remove(m.id);
            continue;
        }
        if (m_mode != AllCountry && m.rate <= 0)
            continue;
        const double local = Pricing::rounded(value * (m_mode == AllCountry ? 1 : m.rate));
        // Manual edits obey the same direction restrictions as automatic proposals.
        row.manualPrices[m.id] = local;
        row.resetMarkets.remove(m.id);
    }
    emit dataChanged(index(idx.row(), 0), index(idx.row(), columnCount() - 1));
    return true;
}
PricingFilterProxy::PricingFilterProxy(QObject *p) : QSortFilterProxyModel(p) {
    setSortRole(TableAmazonPricing::SortRole);
    setDynamicSortFilter(true);
}
void PricingFilterProxy::setFilter(Filter f) {
    m_filter = std::move(f);
    invalidateFilter();
}
bool PricingFilterProxy::accepts(const Pricing::Row &row, const QList<Pricing::Market> &markets,
                                 bool metadataOnly) const {
    if (!row.sku.contains(m_filter.sku, Qt::CaseInsensitive) ||
        !row.title().contains(m_filter.title, Qt::CaseInsensitive))
        return false;
    if (!m_filter.brand.isEmpty() && row.brand != m_filter.brand)
        return false;
    if (!m_filter.productTypes.isEmpty() && !m_filter.productTypes.contains(row.productType))
        return false;
    if (!Pricing::sizeInRange(row.sizeFr, m_filter.sizeFrom, m_filter.sizeTo))
        return false;
    if (metadataOnly)
        return true;
    if (!m_filter.region.isEmpty()) {
        bool present = false;
        for (const auto &m : markets)
            if (m.continent == m_filter.region && row.listings.value(m.id).exists)
                present = true;
        if (!present)
            return false;
    }
    if (m_filter.minDays > 0 && row.inventoryDays() < m_filter.minDays)
        return false;
    if (m_filter.minPrice <= 0 && m_filter.maxPrice <= 0)
        return true;
    for (const auto &m : markets)
        if (m.enabled && m.rate > 0) {
            const double price = Pricing::rounded(row.listings.value(m.id).price / m.rate);
            if (price > 0 && (m_filter.minPrice <= 0 || price >= m_filter.minPrice - 0.000001) &&
                (m_filter.maxPrice <= 0 || price <= m_filter.maxPrice + 0.000001))
                return true;
        }
    return false;
}
bool PricingFilterProxy::mayMatchMetadata(const Pricing::Row &row) const {
    // Existing metadata can narrow a large imported catalog before per-country
    // calls; absent metadata must still be retrieved to decide membership.
    return row.sku.contains(m_filter.sku, Qt::CaseInsensitive) &&
           (row.title().isEmpty() || row.title().contains(m_filter.title, Qt::CaseInsensitive)) &&
           (row.brand.isEmpty() || m_filter.brand.isEmpty() || row.brand == m_filter.brand) &&
           (row.productType.isEmpty() || m_filter.productTypes.isEmpty() ||
            m_filter.productTypes.contains(row.productType)) &&
           (row.sizeFr.isEmpty() || Pricing::sizeInRange(row.sizeFr, m_filter.sizeFrom, m_filter.sizeTo));
}
bool PricingFilterProxy::filterAcceptsRow(int r, const QModelIndex &) const {
    const auto *model = qobject_cast<const TableAmazonPricing *>(sourceModel());
    return model && accepts(model->rows().at(r), model->markets());
}
bool PricingFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const {
    const auto a = left.data(TableAmazonPricing::SortRole), b = right.data(TableAmazonPricing::SortRole);
    const bool missingA = !a.isValid() || (a.metaType().id() == QMetaType::QString && a.toString().isEmpty());
    const bool missingB = !b.isValid() || (b.metaType().id() == QMetaType::QString && b.toString().isEmpty());
    if (missingA != missingB)
        return sortOrder() == Qt::AscendingOrder ? !missingA : missingA;
    if (a == b || (missingA && missingB))
        return left.data(TableAmazonPricing::RowKeyRole).toString() <
               right.data(TableAmazonPricing::RowKeyRole).toString();
    return QSortFilterProxyModel::lessThan(left, right);
}
