#include "SettingsTable.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"   // (found via include-path, same trick as the rest of the project)

SettingsTable *SettingsTable::s_instance = nullptr;

// Keys — must be defined before ENTRIES
const QString SettingsTable::KEY_OPENAI_API_KEY       = "OpenAI/apiKey";
const QString SettingsTable::KEY_LWA_CLIENT_ID        = "AmazonApi/lwaClientId";
const QString SettingsTable::KEY_LWA_CLIENT_SECRET    = "AmazonApi/lwaClientSecret";
const QString SettingsTable::KEY_EU_LWA_REFRESH_TOKEN = "AmazonApi/eu/lwaRefreshToken";
const QString SettingsTable::KEY_NA_LWA_REFRESH_TOKEN = "AmazonApi/na/lwaRefreshToken";
const QString SettingsTable::KEY_JP_LWA_REFRESH_TOKEN = "AmazonApi/jp/lwaRefreshToken";
const QString SettingsTable::KEY_EU_SELLER_ID         = "AmazonApi/eu/sellerId";
const QString SettingsTable::KEY_NA_SELLER_ID         = "AmazonApi/na/sellerId";
const QString SettingsTable::KEY_JP_SELLER_ID         = "AmazonApi/jp/sellerId";

const QList<SettingsTable::Entry> SettingsTable::ENTRIES = {
    {"OpenAI API Key",        KEY_OPENAI_API_KEY,        true},
    {"LWA Client ID",         KEY_LWA_CLIENT_ID,         true},
    {"LWA Client Secret",        KEY_LWA_CLIENT_SECRET,        true},
    {"EU LWA Refresh Token",     KEY_EU_LWA_REFRESH_TOKEN,     true},
    {"NA LWA Refresh Token",     KEY_NA_LWA_REFRESH_TOKEN,     true},
    {"JP LWA Refresh Token",     KEY_JP_LWA_REFRESH_TOKEN,     true},
    {"Europe – Seller ID",    KEY_EU_SELLER_ID,          false},
    {"N. America – Seller ID",KEY_NA_SELLER_ID,          false},
    {"Japan – Seller ID",     KEY_JP_SELLER_ID,          false},
};

SettingsTable::SettingsTable(QObject *parent)
    : QAbstractTableModel(parent)
{}

SettingsTable *SettingsTable::instance()
{
    if (!s_instance)
        s_instance = new SettingsTable();
    return s_instance;
}

QString SettingsTable::value(const QString &key, const QString &defaultValue) const
{
    return WorkingDirectoryManager::instance()->settings()->value(key, defaultValue).toString();
}

void SettingsTable::setValue(const QString &key, const QString &value)
{
    auto s = WorkingDirectoryManager::instance()->settings();
    if (value.isEmpty())
        s->remove(key);
    else
        s->setValue(key, value);

    for (int row = 0; row < ENTRIES.size(); ++row) {
        if (ENTRIES[row].key == key) {
            const QModelIndex idx = index(row, 1);
            emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole, Qt::UserRole});
            break;
        }
    }
}

int SettingsTable::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ENTRIES.size();
}

int SettingsTable::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 2;
}

QVariant SettingsTable::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= ENTRIES.size())
        return {};

    const Entry &entry = ENTRIES[index.row()];

    if (index.column() == 0) {
        if (role == Qt::DisplayRole)
            return entry.label;
        return {};
    }

    // column 1 — value
    const QString val = value(entry.key);
    if (role == Qt::UserRole || role == Qt::EditRole)
        return val;
    if (role == Qt::DisplayRole)
        return (entry.sensitive && !val.isEmpty()) ? QString("••••••••") : val;
    return {};
}

bool SettingsTable::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.column() != 1 || role != Qt::EditRole)
        return false;
    if (index.row() >= ENTRIES.size())
        return false;

    setValue(ENTRIES[index.row()].key, value.toString());
    return true;
}

QVariant SettingsTable::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return section == 0 ? QStringLiteral("Setting") : QStringLiteral("Value");
}

Qt::ItemFlags SettingsTable::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 1)
        f |= Qt::ItemIsEditable;
    return f;
}
