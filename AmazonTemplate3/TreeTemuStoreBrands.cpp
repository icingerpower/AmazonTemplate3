#include "TreeTemuStoreBrands.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

#include <algorithm>

static const QString GROUP_KEY  = QStringLiteral("TemuStoreBrands");
static const QString BRANDS_KEY = QStringLiteral("TemuStoreBrands/knownBrands");

// Store rows carry this internal id; brand rows carry their store's row.
static const quintptr STORE_NODE_ID = static_cast<quintptr>(-1);

TreeTemuStoreBrands::TreeTemuStoreBrands(const QList<TemuStore> &stores, QObject *parent)
    : QAbstractItemModel(parent)
{
    m_stores.reserve(stores.size());
    for (const auto &s : stores) {
        StoreNode node;
        node.country = s.country;
        node.label   = s.label;
        m_stores.append(node);
    }
    _load();
}

void TreeTemuStoreBrands::cacheBrand(const QString &brand)
{
    const QString trimmed = brand.trimmed();
    if (trimmed.isEmpty())
        return;
    auto s = WorkingDirectoryManager::instance()->settings();
    QStringList brands = s->value(BRANDS_KEY).toStringList();
    if (brands.contains(trimmed, Qt::CaseInsensitive))
        return;
    brands << trimmed;
    std::sort(brands.begin(), brands.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    s->setValue(BRANDS_KEY, brands);
}

QStringList TreeTemuStoreBrands::knownBrands()
{
    return WorkingDirectoryManager::instance()->settings()
        ->value(BRANDS_KEY).toStringList();
}

QStringList TreeTemuStoreBrands::availableBrandsForStore(int storeRow) const
{
    if (storeRow < 0 || storeRow >= m_stores.size())
        return {};
    const QString country = m_stores[storeRow].country;
    QStringList result;
    for (const QString &brand : knownBrands())
        if (!_brandExistsInCountry(brand, country, -1, -1))
            result << brand;
    return result;
}

QString TreeTemuStoreBrands::_storeKey(const StoreNode &store)
{
    // '/' would nest QSettings groups; keep the key flat.
    QString key = store.country + QLatin1Char('|') + store.label;
    key.replace(QLatin1Char('/'), QLatin1Char('_'));
    return key;
}

void TreeTemuStoreBrands::setEntityChoices(int storeRow,
                                           const QList<TemuInventoryApi::RepEntity> &manufacturers,
                                           const QList<TemuInventoryApi::RepEntity> &gsprReps)
{
    if (storeRow < 0 || storeRow >= m_stores.size())
        return;
    StoreNode &store = m_stores[storeRow];
    store.manufacturers = manufacturers;
    store.gsprReps      = gsprReps;
    if (!store.brands.isEmpty()) {
        const QModelIndex storeIdx = index(storeRow, 0);
        emit dataChanged(index(0, COL_MANUFACTURER, storeIdx),
                         index(store.brands.size() - 1, COL_GSPR, storeIdx));
    }
}

QModelIndex TreeTemuStoreBrands::addBrand(int storeRow, const QString &brand)
{
    if (storeRow < 0 || storeRow >= m_stores.size())
        return {};
    if (!brand.isEmpty()
        && _brandExistsInCountry(brand, m_stores[storeRow].country, -1, -1)) {
        emit errorOccurred(tr("Brand \"%1\" is already assigned to a store in %2. "
                              "A brand can only be in one store per country.")
                               .arg(brand, m_stores[storeRow].country));
        return {};
    }
    const QModelIndex storeIdx = index(storeRow, 0);
    const int row = m_stores[storeRow].brands.size();
    beginInsertRows(storeIdx, row, row);
    Brand b;
    b.brand = brand;
    m_stores[storeRow].brands.append(b);
    endInsertRows();
    _save();
    return index(row, COL_BRAND, storeIdx);
}

void TreeTemuStoreBrands::removeBrand(const QModelIndex &brandIndex)
{
    const int storeRow = storeRowOf(brandIndex);
    if (storeRow < 0 || isStoreIndex(brandIndex))
        return;
    beginRemoveRows(index(storeRow, 0), brandIndex.row(), brandIndex.row());
    m_stores[storeRow].brands.removeAt(brandIndex.row());
    endRemoveRows();
    _save();
}

bool TreeTemuStoreBrands::isStoreIndex(const QModelIndex &index) const
{
    return index.isValid() && index.internalId() == STORE_NODE_ID;
}

int TreeTemuStoreBrands::storeRowOf(const QModelIndex &index) const
{
    if (!index.isValid())
        return -1;
    if (index.internalId() == STORE_NODE_ID)
        return index.row();
    return static_cast<int>(index.internalId());
}

QModelIndex TreeTemuStoreBrands::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};
    if (!parent.isValid())
        return createIndex(row, column, STORE_NODE_ID);
    if (parent.internalId() == STORE_NODE_ID)
        return createIndex(row, column, static_cast<quintptr>(parent.row()));
    return {};
}

QModelIndex TreeTemuStoreBrands::parent(const QModelIndex &index) const
{
    if (!index.isValid() || index.internalId() == STORE_NODE_ID)
        return {};
    return createIndex(static_cast<int>(index.internalId()), 0, STORE_NODE_ID);
}

int TreeTemuStoreBrands::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_stores.size();
    if (parent.internalId() == STORE_NODE_ID && parent.column() == 0)
        return m_stores[parent.row()].brands.size();
    return 0;
}

int TreeTemuStoreBrands::columnCount(const QModelIndex &) const
{
    return 3;
}

QVariant TreeTemuStoreBrands::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    if (index.internalId() == STORE_NODE_ID) {
        const StoreNode &store = m_stores[index.row()];
        if (role == Qt::DisplayRole && index.column() == COL_BRAND)
            return QStringLiteral("%1 – %2").arg(store.country, store.label);
        return {};
    }

    const int storeRow = static_cast<int>(index.internalId());
    const StoreNode &store = m_stores[storeRow];
    if (index.row() >= store.brands.size())
        return {};
    const Brand &b = store.brands[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case COL_BRAND:        return b.brand;
        case COL_MANUFACTURER: return b.manufacturerName;
        case COL_GSPR:         return b.gsprRepName;
        }
    } else if (role == RoleChoices) {
        if (index.column() == COL_BRAND) {
            // Brands not used yet in this store's country, plus the current
            // value so the combo can show it selected.
            QStringList choices = availableBrandsForStore(storeRow);
            if (!b.brand.isEmpty() && !choices.contains(b.brand))
                choices.prepend(b.brand);
            return choices;
        }
        const auto &entities = (index.column() == COL_MANUFACTURER) ? store.manufacturers
                                                                    : store.gsprReps;
        QStringList names;
        for (const auto &e : entities)
            names << e.name;
        return names;
    } else if (role == Qt::ToolTipRole) {
        const auto &entities = (index.column() == COL_MANUFACTURER) ? store.manufacturers
                             : (index.column() == COL_GSPR)         ? store.gsprReps
                             : QList<TemuInventoryApi::RepEntity>{};
        const QString name = (index.column() == COL_MANUFACTURER) ? b.manufacturerName : b.gsprRepName;
        for (const auto &e : entities)
            if (e.name == name && !e.address.isEmpty())
                return QStringLiteral("%1\n%2").arg(e.name, e.address);
    }
    return {};
}

bool TreeTemuStoreBrands::_brandExistsInCountry(const QString &brand, const QString &country,
                                                int exceptStore, int exceptBrand) const
{
    for (int s = 0; s < m_stores.size(); ++s) {
        if (m_stores[s].country.compare(country, Qt::CaseInsensitive) != 0)
            continue;
        for (int b = 0; b < m_stores[s].brands.size(); ++b) {
            if (s == exceptStore && b == exceptBrand)
                continue;
            if (m_stores[s].brands[b].brand.compare(brand, Qt::CaseInsensitive) == 0)
                return true;
        }
    }
    return false;
}

bool TreeTemuStoreBrands::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.internalId() == STORE_NODE_ID)
        return false;

    const int storeRow = static_cast<int>(index.internalId());
    StoreNode &store = m_stores[storeRow];
    if (index.row() >= store.brands.size())
        return false;
    Brand &b = store.brands[index.row()];
    const QString v = value.toString().trimmed();

    switch (index.column()) {
    case COL_BRAND: {
        if (!v.isEmpty() && !knownBrands().contains(v, Qt::CaseInsensitive)) {
            emit errorOccurred(tr("\"%1\" is not a known brand — load an ASIN of that "
                                  "brand first so it gets cached.").arg(v));
            return false;
        }
        if (!v.isEmpty() && _brandExistsInCountry(v, store.country, storeRow, index.row())) {
            emit errorOccurred(tr("Brand \"%1\" is already assigned to a store in %2. "
                                  "A brand can only be in one store per country.")
                                   .arg(v, store.country));
            return false;
        }
        b.brand = v;
        break;
    }
    case COL_MANUFACTURER:
    case COL_GSPR: {
        const auto &entities = (index.column() == COL_MANUFACTURER) ? store.manufacturers
                                                                    : store.gsprReps;
        qint64 id = 0;
        for (const auto &e : entities) {
            if (e.name == v) {
                id = e.repId;
                break;
            }
        }
        if (!v.isEmpty() && id == 0) {
            emit errorOccurred(tr("\"%1\" is not among the entities registered in this "
                                  "Temu store account.").arg(v));
            return false;
        }
        if (index.column() == COL_MANUFACTURER) {
            b.manufacturerId   = id;
            b.manufacturerName = v;
        } else {
            b.gsprRepId   = id;
            b.gsprRepName = v;
        }
        break;
    }
    default:
        return false;
    }

    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    _save();
    return true;
}

QVariant TreeTemuStoreBrands::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case COL_BRAND:        return tr("Store / Brand");
    case COL_MANUFACTURER: return tr("Manufacturer");
    case COL_GSPR:         return tr("GSPR representative");
    default:               return {};
    }
}

Qt::ItemFlags TreeTemuStoreBrands::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    if (index.internalId() == STORE_NODE_ID)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void TreeTemuStoreBrands::_load()
{
    auto s = WorkingDirectoryManager::instance()->settings();
    for (auto &store : m_stores) {
        const int size = s->beginReadArray(GROUP_KEY + QLatin1Char('/') + _storeKey(store));
        store.brands.reserve(size);
        for (int i = 0; i < size; ++i) {
            s->setArrayIndex(i);
            Brand b;
            b.brand            = s->value(QStringLiteral("brand")).toString();
            b.manufacturerId   = s->value(QStringLiteral("manufacturerId")).toLongLong();
            b.manufacturerName = s->value(QStringLiteral("manufacturerName")).toString();
            b.gsprRepId        = s->value(QStringLiteral("gsprRepId")).toLongLong();
            b.gsprRepName      = s->value(QStringLiteral("gsprRepName")).toString();
            store.brands.append(b);
        }
        s->endArray();
    }
}

void TreeTemuStoreBrands::_save() const
{
    // Only the arrays of the stores currently configured are rewritten;
    // entries of stores removed from PaneSettings stay untouched so their
    // brand data survives a temporary removal / re-add / reorder.
    auto s = WorkingDirectoryManager::instance()->settings();
    for (const auto &store : m_stores) {
        const QString arrayKey = GROUP_KEY + QLatin1Char('/') + _storeKey(store);
        s->remove(arrayKey); // shrink cleanly when brands were removed
        s->beginWriteArray(arrayKey, store.brands.size());
        for (int i = 0; i < store.brands.size(); ++i) {
            s->setArrayIndex(i);
            const Brand &b = store.brands[i];
            s->setValue(QStringLiteral("brand"),            b.brand);
            s->setValue(QStringLiteral("manufacturerId"),   b.manufacturerId);
            s->setValue(QStringLiteral("manufacturerName"), b.manufacturerName);
            s->setValue(QStringLiteral("gsprRepId"),        b.gsprRepId);
            s->setValue(QStringLiteral("gsprRepName"),      b.gsprRepName);
        }
        s->endArray();
    }
}
