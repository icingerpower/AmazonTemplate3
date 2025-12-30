#ifndef ATTRIBUTEPOSSIBLEMISSINGTABLE_H
#define ATTRIBUTEPOSSIBLEMISSINGTABLE_H

#include <QAbstractTableModel>
#include <QStringList>

class AttributePossibleMissingTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AttributePossibleMissingTable(
            const QString &workingDirectory, QObject *parent = nullptr);
    void remove(const QModelIndex &index);
    bool contains(const QString &marketplace,
                           const QString &countryCode,
                           const QString &langCode,
                           const QString &productType,
                           const QString &attrId) const;

    void recordAttribute(const QString &marketplace,
                         const QString &countryCode,
                         const QString &langCode,
                         const QString &productType,
                         const QString &attrId,
                         const QStringList &possibleValues);
    QSet<QString> possibleValues(
            const QString &marketplace,
            const QString &countryCode,
            const QString &langCode,
            const QString &productType,
            const QString &attrId) const;

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
    static const QStringList HEADERS;
    QString m_filePath;
    QList<QStringList> m_listOfStringList;
    void _loadFromFile();
    void _saveInFile();
};

#endif // ATTRIBUTEPOSSIBLEMISSINGTABLE_H
