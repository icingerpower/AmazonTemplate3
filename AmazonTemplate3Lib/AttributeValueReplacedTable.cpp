#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>

#include "AttributeValueReplacedTable.h"

const QStringList AttributeValueReplacedTable::HEADERS{
    QObject::tr("marketplace")
    , QObject::tr("country")
    , QObject::tr("lang")
    , QObject::tr("attribute")
    , QObject::tr("from")
    , QObject::tr("to")
};

AttributeValueReplacedTable::AttributeValueReplacedTable(
        const QString &workingDirectory, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = QDir{workingDirectory}.absoluteFilePath("attributeReplacement.csv");
    _loadFromFile();
}

bool AttributeValueReplacedTable::contains(
        const QString &marketplaceId
        , const QString &countryCode
        , const QString &langCode
        , const QString &attrId
        , const QString &valueFrom) const
{
    for (const auto &row : std::as_const(m_listOfStringList))
    {
        if (row.size() >= 5 &&
            row[0] == marketplaceId &&
            row[1] == countryCode &&
            row[2] == langCode &&
            row[3] == attrId &&
            row[4] == valueFrom)
        {
            return true;
        }
    }
    return false;
}

void AttributeValueReplacedTable::recordAttribute(
        const QString &marketplaceId,
        const QString &countryCode,
        const QString &langCode,
        const QString &attrId,
        const QString &valueFrom,
        const QString &valueTo)
{
    if (!contains(marketplaceId, countryCode, langCode, attrId, valueFrom))
    {
        QStringList newRow;
        newRow << marketplaceId << countryCode << langCode << attrId << valueFrom << valueTo;

        beginInsertRows(QModelIndex{}, 0, 0);
        m_listOfStringList.insert(0, newRow);
        _saveInFile();
        endInsertRows();
    }
}

QVariant AttributeValueReplacedTable::headerData(
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

int AttributeValueReplacedTable::rowCount(const QModelIndex &) const
{
    return m_listOfStringList.size();
}

int AttributeValueReplacedTable::columnCount(const QModelIndex &) const
{
    return HEADERS.size();
}

QVariant AttributeValueReplacedTable::data(const QModelIndex &index, int role) const
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

bool AttributeValueReplacedTable::setData(
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

Qt::ItemFlags AttributeValueReplacedTable::flags(const QModelIndex &) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void AttributeValueReplacedTable::_loadFromFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::ReadOnly))
    {
        QTextStream stream{&file};
        const auto &lines = stream.readAll().split("\n");
        // Skip header if needed or verify it. Implementation typically just reads data.
        // Assuming file starts with headers, we skip line 0.
        // However, AttributeFlagsTable logic was complex with mapping. Here headers are static.
        // We will assume first line is header and skip it, then read rest.
        
        if (!lines.isEmpty())
        {
            // Verify headers/format or just skip first line if it matches HEADERS join
            // For simplicity and robustness like existing code often does, we iterate from 1.
            for (int i=1; i<lines.size(); ++i)
            {
                if (lines[i].trimmed().isEmpty()) continue;
                
                // Use CsvReader logic or manual split? 
                // AttributeFlagsTable used split(COL_SEP). COL_SEP is likely defined in CsvReader.h or utils.cmake defines it?
                // The user code showed `add_definitions(-DCOL_SEP=",")` in CMakeLists.txt.
                // So we can use COL_SEP.
                
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

void AttributeValueReplacedTable::_saveInFile()
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
