#include <QDir>
#include <QColor>
#include <QBrush>

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

QSet<QString> AttributeFlagsTable::getUnrecordedFieldIds(
        const QString &marketplace, const QSet<QString> &fieldIds) const
{
    int colIdx = m_colNames.indexOf(marketplace);
    if (colIdx == -1)
    {
        return fieldIds;
    }

    QSet<QString> unrecordedIds = fieldIds;
    for (const auto &variantList : std::as_const(m_listOfVariantList))
    {
        const QString &val = variantList[colIdx].toString();
        if (!val.isEmpty())
        {
            unrecordedIds.remove(val);
            if (unrecordedIds.isEmpty())
            {
                break;
            }
        }
    }
    return unrecordedIds;
}

void AttributeFlagsTable::recordAttributeNotRecordedYet(
        const QString &marketplaceId, const QSet<QString> &fieldIds)
{
    for (const auto &fieldId : fieldIds)
    {
        QHash<QString, QString> marketplace_ids{{marketplaceId, fieldId}};
        QVariantList variantList;
        for (int i = 0; i < m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
            {
                variantList << marketplace_ids[colName];
            }
            else
            {
                variantList << QString{};
            }
        }
        // Append false for each flag column.
        for (int i = m_indFirstFlag; i < m_colNames.size(); ++i)
        {
            variantList << false;
        }
        beginInsertRows(QModelIndex{}, 0, 0);
        m_listOfVariantList.insert(0, variantList);
        _saveInFile();
        endInsertRows();
    }
}

void AttributeFlagsTable::recordAttribute(const QHash<QString, QString> &marketplace_ids)
{
    // Record attribute values and set all flag columns to false (no flags provided).
    int pos = getPosAttr(marketplace_ids);
    if (pos >= 0)
    {
        // Update existing row: fill attribute values if empty.
        for (int i = 0; i < m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
            {
                QModelIndex idx = index(pos, i);
                if (data(idx).toString().isEmpty())
                {
                    setData(idx, marketplace_ids[colName]);
                }
            }
        }
    }
    else
    {
        // Create a new row with attribute values and all flags false.
        QVariantList variantList;
        for (int i = 0; i < m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
                variantList << marketplace_ids[colName];
            else
                variantList << QString{};
        }
        // Append false for each flag column.
        for (int i = m_indFirstFlag; i < m_colNames.size(); ++i)
        {
            variantList << false;
        }
        beginInsertRows(QModelIndex{}, 0, 0);
        m_listOfVariantList.insert(0, variantList);
        _saveInFile();
        endInsertRows();
    }
}

int AttributeFlagsTable::getPosAttr(const QHash<QString, QString> &marketplace_ids) const
{
    int rowIdx = 0;
    for (const auto &variantList : std::as_const(m_listOfVariantList))
    {
        bool compatible = true;
        bool hasAtLeastOneMatch = false;

        for (int i=0; i<m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            const QString &val = variantList[i].toString();

            if (marketplace_ids.contains(colName))
            {
                if (!val.isEmpty())
                {
                    if (val != marketplace_ids[colName])
                    {
                        compatible = false;
                        break;
                    }
                    else
                    {
                        hasAtLeastOneMatch = true;
                    }
                }
            }
        }

        if (compatible && hasAtLeastOneMatch)
        {
            return rowIdx;
        }
        rowIdx++;
    }
    return -1;
}

void AttributeFlagsTable::recordAttribute(
        const QHash<QString, QString> &marketplace_ids, Attribute::Flag flag)
{
    int pos = getPosAttr(marketplace_ids);
    if (pos >= 0)
    {
        for (int i=0; i<m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
            {
                QModelIndex idx = index(pos, i);
                if (data(idx).toString().isEmpty())
                {
                    setData(idx, marketplace_ids[colName]);
                }
            }
        }

        /* // We don't update flags for now
        for (int i=m_indFirstFlag; i<m_colNames.size(); ++i)
        {
             QString flagName = m_colNames[i];
             Attribute::Flag curFlag = Attribute::STRING_FLAG.value(flagName);
             if ((flag & curFlag) == curFlag)
             {
                 QModelIndex idx = index(pos, i);
                 if (!data(idx).toBool())
                 {
                    setData(idx, true);
                 }
             }
        }
        //*/
    }
    else
    {
        QVariantList variantList;
        for (int i=0; i<m_indFirstFlag; ++i)
        {
            const auto &colName = m_colNames[i];
            if (marketplace_ids.contains(colName))
            {
                variantList << marketplace_ids[colName];
            }
            else
            {
                variantList << QString{};
            }
        }
        for (int i=m_indFirstFlag; i<m_colNames.size(); ++i)
        {
            QString flagName = m_colNames[i];
            Attribute::Flag curFlag = Attribute::STRING_FLAG.value(flagName);
            bool contains = (flag & curFlag) == curFlag;
            variantList << contains;
        }
        beginInsertRows(QModelIndex{}, 0, 0);
        m_listOfVariantList.insert(0, variantList);
        _saveInFile();
        endInsertRows();
    }
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
    if (role == Qt::BackgroundRole)
    {
        // Light pink background when all flag columns are false
        bool allFalse = true;
        for (int col = m_indFirstFlag; col < m_colNames.size(); ++col)
        {
            if (m_listOfVariantList[index.row()][col].toBool())
            {
                allFalse = false;
                break;
            }
        }
        if (allFalse)
        {
            return QBrush(QColor(255, 182, 193));
        }
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



