#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>

#include "AttributePossibleMissingTable.h"

const QStringList AttributePossibleMissingTable::HEADERS{
    QObject::tr("marketplace")
    , QObject::tr("country")
    , QObject::tr("lang")
    , QObject::tr("attribute")
    , QObject::tr("possible values")
};

AttributePossibleMissingTable::AttributePossibleMissingTable(
        const QString &workingDirectory, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = QDir{workingDirectory}.absoluteFilePath("attributePossibleMissing.csv");
    _loadFromFile();
}

void AttributePossibleMissingTable::recordAttribute(
        const QString &marketplaceId,
        const QString &countryCode,
        const QString &langCode,
        const QString &attrId,
        const QStringList &possibleValues)
{
    QStringList newRow;
    newRow << marketplaceId << countryCode << langCode << attrId << possibleValues.join(";");

    beginInsertRows(QModelIndex{}, 0, 0);
    m_listOfStringList.insert(0, newRow);
    _saveInFile();
    endInsertRows();
}

QVariant AttributePossibleMissingTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        if (section >= 0 && section < HEADERS.size())
        {
            return HEADERS[section];
        }
    }
    return QVariant{};
}

int AttributePossibleMissingTable::rowCount(const QModelIndex &) const
{
    return m_listOfStringList.size();
}

int AttributePossibleMissingTable::columnCount(const QModelIndex &) const
{
    return HEADERS.size();
}

QVariant AttributePossibleMissingTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::EditRole || role == Qt::DisplayRole)
    {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < HEADERS.size())
        {
            return m_listOfStringList[index.row()][index.column()];
        }
    }
    return QVariant{};
}

bool AttributePossibleMissingTable::setData(
        const QModelIndex &index, const QVariant &value, int role)
{
    if (data(index, role) != value)
    {
        if (index.row() >= 0 && index.row() < m_listOfStringList.size() &&
            index.column() >= 0 && index.column() < HEADERS.size())
        {
            m_listOfStringList[index.row()][index.column()] = value.toString();
            _saveInFile();
            emit dataChanged(index, index, {role});
            return true;
        }
    }
    return false;
}

Qt::ItemFlags AttributePossibleMissingTable::flags(const QModelIndex &) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void AttributePossibleMissingTable::_loadFromFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::ReadOnly))
    {
        QTextStream stream{&file};
        const auto &lines = stream.readAll().split("\n");
        
        if (!lines.isEmpty())
        {
            // Skip first line (header)
            for (int i=1; i<lines.size(); ++i)
            {
                if (lines[i].trimmed().isEmpty()) continue;
                
                const auto &elements = lines[i].split(COL_SEP);
                if (elements.size() == HEADERS.size())
                {
                    m_listOfStringList << elements;
                }
            }
            std::sort(m_listOfStringList.begin(), m_listOfStringList.end());
        }
        file.close();
    }
}

void AttributePossibleMissingTable::_saveInFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::WriteOnly))
    {
        QTextStream stream{&file};
        stream << HEADERS.join(COL_SEP);
        for (const auto &row : m_listOfStringList)
        {
            stream << "\n" + row.join(COL_SEP);
        }
        file.close();
    }
}
