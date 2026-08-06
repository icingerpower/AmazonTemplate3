#ifndef GSPRSKIPPEDTABLE_H
#define GSPRSKIPPEDTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStringList>

// Products the user chose to skip (via the "Skip" button of the GSPR pause
// dialog). A skipped ASIN is skipped automatically on every later run, for
// every country. One row per ASIN; persisted to QSettings.
//
// The ASIN column is editable ON PURPOSE without a setData() implementation:
// double-clicking opens an editor whose text can be copied, and any change is
// discarded (same trick as the other GSPR tables).
class GsprSkippedTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit GsprSkippedTable(QObject *parent = nullptr);

    void recordSkip(const QString &asin, const QString &sku);
    void removeAt(int row); // "Remove" button — un-skip a product
    bool contains(const QString &asin) const;
    QStringList asins() const;

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    void _load();
    void _save() const;

    QList<QStringList> m_rows; // {asin, sku, dateSkipped (ISO)}
    static const QStringList HEADER;
};

#endif // GSPRSKIPPEDTABLE_H
