#include <QDir>
#include <QFile>
#include <QTextStream>
#include <algorithm>

#include "AttributeEquivalentTable.h"

// Note: CsvReader is not used as per instruction "No more m_indFirstFlag that is useless here => adapt the code"
// and to avoid dependency if not needed, though the user said "Keep the same style of code of me"
// but explicitly removed CsvReader in previous snippet? actually checking previous snippet (Step 181/182)
// User removed CsvReader include in AttributePossibleMissingTable.cpp and AttributeValueReplacedTable.cpp
// So I will not include it here either.

// Also user used QObject::tr for headers.

const QStringList AttributeEquivalentTable::HEADERS{
    QObject::tr("Attribute")
    , QObject::tr("Equivalent values")
};

AttributeEquivalentTable::AttributeEquivalentTable(
        const QString &workingDirectory, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = QDir{workingDirectory}.absoluteFilePath("attributeEquivalent.csv");
    _loadFromFile();
}

int AttributeEquivalentTable::getPosAttr(const QString &attrId) const
{
    int pos = 0;
    for (const auto &stringList : m_listOfStringList)
    {
        const auto &attributes = stringList[0].split(";");
        for (const auto &attribute : attributes)
        {
            if (attrId == attribute)
            {
                return pos;
            }
        }
        ++pos;
    }
    return -1;
}

void AttributeEquivalentTable::recordAttribute(
        const QString &attrId,
        const QStringList &equivalentValues)
{
    int posAttr = getPosAttr(attrId);
    if (posAttr < 0)
    {
        QStringList newRow;
        newRow << attrId << equivalentValues.join(";");

        beginInsertRows(QModelIndex{}, 0, 0);
        m_listOfStringList.insert(0, newRow);
        _saveInFile();
        endInsertRows();
    }
    else
    {
        const auto &currentEquivalent = m_listOfStringList[posAttr].last().split(";");
        QSet<QString> equivalentValues{currentEquivalent.begin(), currentEquivalent.end()};
        for (const auto &newVal : equivalentValues)
        {
            equivalentValues.insert(newVal);
        }
        QStringList listEquivalentValues{equivalentValues.begin(), equivalentValues.end()};
        std::sort(listEquivalentValues.begin(), listEquivalentValues.end());
        int indColLast = m_listOfStringList[posAttr].size() - 1;
        m_listOfStringList[posAttr][indColLast] = listEquivalentValues.join(";");
        emit dataChanged(index(posAttr, indColLast)
                         , index(posAttr, indColLast));
    }
}

QVariant AttributeEquivalentTable::headerData(
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

int AttributeEquivalentTable::rowCount(const QModelIndex &) const
{
    return m_listOfStringList.size();
}

int AttributeEquivalentTable::columnCount(const QModelIndex &) const
{
    return HEADERS.size();
}

QVariant AttributeEquivalentTable::data(const QModelIndex &index, int role) const
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

bool AttributeEquivalentTable::setData(
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

Qt::ItemFlags AttributeEquivalentTable::flags(const QModelIndex &) const
{
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void AttributeEquivalentTable::_loadFromFile()
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
                
                // Assuming standard comma separation for the CSV columns themselves
                // We need a definition for COL_SEP or use "," directly. 
                // Previous files use COL_SEP which is defined in CMakeLists.txt via add_definitions(-DCOL_SEP=",")
                // So I can use COL_SEP here too.
                
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

void AttributeEquivalentTable::_saveInFile()
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
