#ifndef TABLESTOREASINS_H
#define TABLESTOREASINS_H

#include <QAbstractTableModel>
#include <QDate>
#include <QList>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QStringList>

class TableStoreAsin : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum FixedColumn {
        ColImage = 0,
        ColAsin,
        ColTitle,
        ColCreatedDate,
        ColExistsStart  // per-marketplace existence columns begin here (one per configured mp)
    };

    struct Row {
        QString asin;
        QString title;
        QPixmap image;
        QDate   createdDate;
        QSet<QString> existsInMarketplaces; // marketplaceIds where this color-group rep exists
    };

    explicit TableStoreAsin(QObject *parent = nullptr);

    void setMarketplaces(const QStringList &mpIds, const QStringList &mpLabels);
    void setRows(const QList<Row> &rows);
    void clear();

    void updateImage(const QString &asin, const QPixmap &px);

    // Move a row up or down in the model; returns true if the move was applied.
    bool moveRowUp(int row);
    bool moveRowDown(int row);
    bool moveRowToTop(int row);
    bool moveRowToBottom(int row);

    // QAbstractTableModel
    int           rowCount(const QModelIndex &parent = {}) const override;
    int           columnCount(const QModelIndex &parent = {}) const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    QStringList m_mpIds;    // ordered marketplace IDs for existence columns
    QStringList m_mpLabels; // short labels (e.g. "DE", "FR")
    QList<Row>  m_rows;
};

#endif // TABLESTOREASINS_H
