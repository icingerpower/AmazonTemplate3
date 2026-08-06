#ifndef GSPRDONETABLE_H
#define GSPRDONETABLE_H

#include <QAbstractTableModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// GSPR warnings that were submitted (by the app, or observed already pending
// on Seller Central — i.e. done by a human). One row per (ASIN, country,
// warning type); the date is the FIRST time it was done and never overwritten.
// Rows still listed on Amazon more than a month after being done get flagged
// "long to process" so a human can investigate. Persisted to QSettings.
//
// The ASIN column is editable ON PURPOSE without a setData() implementation:
// double-clicking opens an editor whose text can be copied, and any change is
// discarded (same trick as AiFailureTable / GsprFailedTable).
class GsprDoneTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit GsprDoneTable(QObject *parent = nullptr);

    // Records now as done date; keeps the existing date if already recorded.
    void recordDone(const QString &asin, const QString &countryCode,
                    const QString &warningType);
    bool isDone(const QString &asin, const QString &countryCode,
                const QString &warningType) const;
    QDateTime dateDone(const QString &asin, const QString &countryCode,
                       const QString &warningType) const;
    void markLongToProcess(const QString &asin, const QString &countryCode,
                           const QString &warningType);
    void removeAt(int row); // "Remove" button — drop a mis-recorded entry
    // Un-record an entry (e.g. Amazon lost the submission after confirming).
    void removeDone(const QString &asin, const QString &countryCode,
                    const QString &warningType);
    // ASINs already done for this country + warning type (worker skip list).
    QStringList asinsDone(const QString &countryCode,
                          const QString &warningType) const;
    void clear();

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    int  _find(const QString &asin, const QString &countryCode,
               const QString &warningType) const;
    void _load();
    void _save() const;

    // {asin, countryCode, warningType, dateDone (ISO), longToProcess ("1"/"")}
    QList<QStringList> m_rows;
    static const QStringList HEADER;
};

#endif // GSPRDONETABLE_H
