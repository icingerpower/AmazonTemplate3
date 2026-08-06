#ifndef GSPRFAILEDTABLE_H
#define GSPRFAILEDTABLE_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStringList>

// ASINs whose GSPR safety attestation could not be submitted (the "Safety
// attestation" option was unavailable, or another step failed). One row per
// (ASIN, country) couple; re-recording an existing couple only refreshes the
// date. Persisted to QSettings.
//
// The ASIN column is editable ON PURPOSE without a setData() implementation:
// double-clicking opens an editor whose text can be copied, and any change is
// discarded (same trick as AiFailureTable).
class GsprFailedTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit GsprFailedTable(QObject *parent = nullptr);

    void recordFailure(const QString &asin, const QString &countryCode,
                       const QString &reason);
    void removeAt(int row); // "Remove" button — drop a stale entry
    // Drop the entry when a later run succeeds for this couple.
    void removeFailure(const QString &asin, const QString &countryCode);
    void clear();

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    void _load();
    void _save() const;

    QList<QStringList> m_rows; // {asin, countryCode, dateTried (ISO), reason}
    static const QStringList HEADER;
};

#endif // GSPRFAILEDTABLE_H
