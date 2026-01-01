#ifndef ATTRIBUTEFLAGSTABLE_H
#define ATTRIBUTEFLAGSTABLE_H

#include <QAbstractTableModel>
#include <QSettings>
#include <QSharedPointer>

#include "../../common/utils/CsvReader.h"

#include "Attribute.h"

class AttributeFlagsTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AttributeFlagsTable(
            const QString &workingDirectory, QObject *parent = nullptr);
    QSet<QString> getUnrecordedFieldIds(
            const QString &marketplace, const QSet<QString> &fieldIds) const;
    QHash<QString, QString> get_marketplace_id(
            const QString &marketplace, const QString &fieldId) const;
    QString getFieldId(
            const QString &marketplaceFrom
            , const QString &fieldIdFrom
            , const QString &marketplaceTo) const;
    Attribute::Flag getFlags(const QString &marketplace, const QString &fieldId) const;
    bool hasFlag(const QString &marketplace, const QString &fieldId, Attribute::Flag flag) const;
    QStringList getSizeFieldIds() const;
    //Attribute::Flag getFlag(const QString &attrId, const QString &marketplace) const;

    void recordAttributeNotRecordedYet(
            const QString &marketplace, const QSet<QString> &fieldIds);
    void recordAttribute(const QHash<QString, QString> &marketplace_ids);
    void recordAttribute(const QHash<QString, QString> &marketplace_ids, Attribute::Flag flag);
    int getPosAttr(const QHash<QString, QString> &marketplace_ids) const;

    // Header:
    QVariant headerData(
            int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    void _sort();
    QString m_filePath;
    QStringList m_colNames;
    int m_indFirstFlag;
    QList<QVariantList> m_listOfVariantList;
    void _loadFromFile();
    void _saveInFile();
    void _updateMap();
    QHash<QString, QHash<QString, int>> m_marketplace_fieldId_indRow;
};

#endif // ATTRIBUTEFLAGSTABLE_H
