#include <QDir>

#include "AttributeFlagsTable.h"

AttributeFlagsTable::AttributeFlagsTable(
        const QString &workingDirectory, QObject *parent)
    : QAbstractTableModel(parent)
{
    m_filePath = QDir{workingDirectory}.absoluteFilePath("attributeFlags.csv");
    for (const auto &markeplace : Attribute::MARKETPLACES)
    {
        m_colNames << markeplace;
    }
    m_indFirstFlag = m_colNames.size();
    const auto &colFlags = Attribute::STRING_FLAG.keys();
    for (const auto &flagName : colFlags)
    {
        m_colNames << flagName;
    }
}

void AttributeFlagsTable::recordAttribute(
        const QHash<QString, QString> ids, Attribute::Flag flag)
{
    QVariantList variantList;
    for (int i=0; i<m_indFirstFlag; ++i)
    {
        const auto &colName = m_colNames[i];
        if (ids.contains(colName))
        {
            variantList << ids[colName];
        }
        else
        {
            variantList << QString{};
        }
    }
    for (int i=m_indFirstFlag; i<m_colNames.size(); ++i)
    {
        for (auto it = Attribute::STRING_FLAG.begin();
             it != Attribute::STRING_FLAG.end(); ++it)
        {
            Attribute::Flag curFlag = it.value();
            bool contains = (flag & curFlag) == curFlag;
            variantList << contains;
        }
    }
    beginInsertRows(QModelIndex{}, 0, 0);
    m_listOfVariantList.insert(0, variantList);
    _saveInFile();
    endInsertRows();
}

QVariant AttributeFlagsTable::headerData(
        int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        return m_colNames[section];
    }
    return QVariant{};
}

int AttributeFlagsTable::rowCount(const QModelIndex &) const
{
    return m_listOfVariantList.size();
}

int AttributeFlagsTable::columnCount(const QModelIndex &) const
{
    return m_colNames.size();
}

QVariant AttributeFlagsTable::data(const QModelIndex &index, int role) const
{
    if (role == Qt::EditRole || role == Qt::DisplayRole)
    {
        return m_listOfVariantList[index.row()][index.column()];
    }
    return QVariant{};
}

bool AttributeFlagsTable::setData(
        const QModelIndex &index, const QVariant &value, int role)
{
    if (data(index, role) != value)
    {
        m_listOfVariantList[index.row()][index.column()] = value;
        _saveInFile();
        emit dataChanged(index, index, {role});
        return true;
    }
    return false;
}

Qt::ItemFlags AttributeFlagsTable::flags(const QModelIndex &index) const
{
    if (index.column() < m_indFirstFlag)
    {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

void AttributeFlagsTable::_loadFromFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::ReadOnly))
    {
        QTextStream stream{&file};
        const auto &lines = stream.readAll().split("\n");
        const auto &oldColNames = lines[0].split(COL_SEP);
        QHash<QString, int> oldColName_index;
        int i = 0;
        for (const auto &colName : oldColNames)
        {
            oldColName_index[colName] = i;
            ++i;
        }
        for (int i=1; i<lines.size(); ++i)
        {
            QVariantList newElements;
            const auto &oldElements = lines[i].split(COL_SEP);
            int j=0;
            for (const auto &newColName : m_colNames)
            {
                if (oldColName_index.contains(newColName))
                {
                    int oldColIndex = oldColName_index[newColName];
                    if (j < m_indFirstFlag)
                    {
                        newElements <<  oldElements[oldColIndex];
                    }
                    else
                    {
                        newElements << (oldElements[oldColIndex] == STR_TRUE);
                    }
                }
                else
                {
                    if (j < m_indFirstFlag)
                    {
                        newElements << QString{};
                    }
                    else
                    {
                        newElements << false;
                    }
                }
                ++j;
            }
            m_listOfVariantList << newElements;
        }
        file.close();
    }
}

void AttributeFlagsTable::_saveInFile()
{
    QFile file{m_filePath};
    if (file.open(QFile::WriteOnly))
    {
        QTextStream stream{&file};
        stream << m_colNames.join(COL_SEP);
        for (const auto &variantList : m_listOfVariantList)
        {
            QStringList elements;
            for (int i=0; i<m_indFirstFlag; ++i)
            {
                elements << variantList[i].toString();
            }
            for (int i=m_indFirstFlag; i<m_colNames.size(); ++i)
            {
                if (variantList[i].toBool())
                {
                    elements << STR_TRUE;
                }
                else
                {
                    elements << STR_FALSE;
                }
            }
            stream << "\n" + elements.join(COL_SEP);
        }
        file.close();
    }
}



