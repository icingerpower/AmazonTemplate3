#include "TemuStoreModel.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

static const QString ARRAY_KEY = QStringLiteral("TemuApi/stores");

TemuStoreModel::TemuStoreModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    _load();
}

void TemuStoreModel::addStore()
{
    const int row = m_stores.size();
    beginInsertRows({}, row, row);
    m_stores.append(TemuStore{});
    endInsertRows();
    _save();
}

void TemuStoreModel::removeStore(int row)
{
    if (row < 0 || row >= m_stores.size())
        return;
    beginRemoveRows({}, row, row);
    m_stores.removeAt(row);
    endRemoveRows();
    _save();
}

int TemuStoreModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_stores.size();
}

int TemuStoreModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 7;
}

QVariant TemuStoreModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_stores.size())
        return {};

    const TemuStore &s = m_stores[index.row()];

    const QString val = [&]() -> QString {
        switch (index.column()) {
        case COL_COUNTRY:    return s.country;
        case COL_LABEL:      return s.label;
        case COL_TOKEN:      return s.token;
        case COL_PROXY_HOST: return s.proxyHost;
        case COL_PROXY_PORT: return s.proxyPort > 0 ? QString::number(s.proxyPort) : QString();
        case COL_PROXY_USER: return s.proxyUser;
        case COL_PROXY_PASS: return s.proxyPassword;
        default:             return {};
        }
    }();

    if (role == Qt::EditRole || role == Qt::UserRole)
        return val;
    if (role == Qt::DisplayRole) {
        if ((index.column() == COL_TOKEN || index.column() == COL_PROXY_PASS) && !val.isEmpty()) {
            return QStringLiteral("••••••••");
        }
        return val;
    }
    return {};
}

bool TemuStoreModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.row() >= m_stores.size())
        return false;

    TemuStore &s = m_stores[index.row()];
    const QString v = value.toString();
    switch (index.column()) {
    case COL_COUNTRY:    s.country = v; break;
    case COL_LABEL:      s.label   = v; break;
    case COL_TOKEN:      s.token   = v; break;
    case COL_PROXY_HOST: s.proxyHost = v; break;
    case COL_PROXY_PORT: s.proxyPort = v.toInt(); break;
    case COL_PROXY_USER: s.proxyUser = v; break;
    case COL_PROXY_PASS: s.proxyPassword = v; break;
    default: return false;
    }

    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    _save();
    return true;
}

QVariant TemuStoreModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case COL_COUNTRY:    return QStringLiteral("Country");
    case COL_LABEL:      return QStringLiteral("Store name");
    case COL_TOKEN:      return QStringLiteral("Access token");
    case COL_PROXY_HOST: return QStringLiteral("Proxy Host");
    case COL_PROXY_PORT: return QStringLiteral("Proxy Port");
    case COL_PROXY_USER: return QStringLiteral("Proxy User");
    case COL_PROXY_PASS: return QStringLiteral("Proxy Pass");
    default:             return {};
    }
}

Qt::ItemFlags TemuStoreModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void TemuStoreModel::_load()
{
    auto s = WorkingDirectoryManager::instance()->settings();
    const int size = s->beginReadArray(ARRAY_KEY);
    m_stores.reserve(size);
    for (int i = 0; i < size; ++i) {
        s->setArrayIndex(i);
        TemuStore store;
        store.country       = s->value(QStringLiteral("country")).toString();
        store.label         = s->value(QStringLiteral("label")).toString();
        store.token         = s->value(QStringLiteral("token")).toString();
        store.proxyHost     = s->value(QStringLiteral("proxyHost")).toString();
        store.proxyPort     = s->value(QStringLiteral("proxyPort")).toInt();
        store.proxyUser     = s->value(QStringLiteral("proxyUser")).toString();
        store.proxyPassword = s->value(QStringLiteral("proxyPassword")).toString();
        m_stores.append(store);
    }
    s->endArray();
}

void TemuStoreModel::_save() const
{
    auto s = WorkingDirectoryManager::instance()->settings();
    s->beginWriteArray(ARRAY_KEY, m_stores.size());
    for (int i = 0; i < m_stores.size(); ++i) {
        s->setArrayIndex(i);
        s->setValue(QStringLiteral("country"),       m_stores[i].country);
        s->setValue(QStringLiteral("label"),         m_stores[i].label);
        s->setValue(QStringLiteral("token"),         m_stores[i].token);
        s->setValue(QStringLiteral("proxyHost"),     m_stores[i].proxyHost);
        s->setValue(QStringLiteral("proxyPort"),     m_stores[i].proxyPort);
        s->setValue(QStringLiteral("proxyUser"),     m_stores[i].proxyUser);
        s->setValue(QStringLiteral("proxyPassword"), m_stores[i].proxyPassword);
    }
    s->endArray();
}
