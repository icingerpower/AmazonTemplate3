#ifndef AIFAILURETABLE_H
#define AIFAILURETABLE_H

#include <QAbstractItemModel>

class AiFailureTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit AiFailureTable(QObject *parent = nullptr);
    void recordError(const QString &marketplaceTo
                     , const QString &countryCodeTo
                     , const QString &countryCodeFrom
                     , const QString &fieldId
                     , const QString &error
                     );
    void clear();

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    QList<QStringList> m_listOfStringList;
    static const QStringList HEADER;
};

#endif // AIFAILURETABLE_H
