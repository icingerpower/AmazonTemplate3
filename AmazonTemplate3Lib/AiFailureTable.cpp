#include "AiFailureTable.h"

const QStringList AiFailureTable::HEADER{
    QObject::tr("Marketplace")
    , QObject::tr("Country")
    , QObject::tr("Lang")
    , QObject::tr("Field id")
    , QObject::tr("Error")
};

AiFailureTable::AiFailureTable(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void AiFailureTable::recordError(
        const QString &marketplaceTo
        , const QString &countryCodeTo
        , const QString &countryCodeFrom
        , const QString &fieldId
        , const QString &error)
{
    beginInsertRows(QModelIndex{}, m_listOfStringList.size(), m_listOfStringList.size());
    m_listOfStringList << QStringList{marketplaceTo, countryCodeTo, countryCodeFrom, fieldId, error};
    endInsertRows();
}

void AiFailureTable::clear()
{
    if (m_listOfStringList.size() > 0)
    {
        beginRemoveRows(QModelIndex{}, 0, m_listOfStringList.size()-1);
        m_listOfStringList.clear();
        endRemoveRows();
    }
}

QVariant AiFailureTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        return HEADER[section];
    }
    else if (orientation == Qt::Vertical && role == Qt::DisplayRole)
    {
        return QString::number(section + 1);
    }
    return QVariant{};
}

int AiFailureTable::rowCount(const QModelIndex &parent) const
{
    return m_listOfStringList.size();
}

int AiFailureTable::columnCount(const QModelIndex &parent) const
{
    return HEADER.size();
}

QVariant AiFailureTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        return m_listOfStringList[index.row()][index.column()];
    }
    return QVariant();
}

Qt::ItemFlags AiFailureTable::flags(const QModelIndex &index) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}
