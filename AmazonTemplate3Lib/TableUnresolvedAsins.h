#pragma once

#include <QAbstractTableModel>
#include <QDir>
#include <QHash>
#include <QList>
#include <QString>

// Persistent table of ASINs whose SKU could not be resolved automatically.
// The user fills in the SKU column manually; PaneWarnings consults this table
// as the last fallback before marking a row as unresolved.
//
// Persistence: {workingDir}/unresolved_asins.json
class TableUnresolvedAsins : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColAsin = 0, ColSku, ColTitle, ColCount };

    explicit TableUnresolvedAsins(QObject *parent = nullptr);

    void load(const QDir &workingDir);
    void save() const;

    // Add a new row for asin (with empty SKU) if it doesn't already exist.
    // If the row already exists and title is non-empty while the stored title
    // is empty, updates the title. Returns true when a new row was inserted.
    bool addOrUpdate(const QString &asin, const QString &title);

    // Returns the SKU stored for asin, or an empty string.
    QString skuForAsin(const QString &asin) const;

    // Returns a map of all entries whose SKU is non-empty.
    QHash<QString, QString> buildSkuMap() const;

    // QAbstractTableModel
    int           rowCount(const QModelIndex &parent = {}) const override;
    int           columnCount(const QModelIndex &parent = {}) const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool          setData(const QModelIndex &index, const QVariant &value,
                          int role = Qt::EditRole) override;

private:
    struct Row {
        QString asin;
        QString sku;
        QString title;
    };

    QDir       m_workingDir;
    QList<Row> m_rows;
    QHash<QString, int> m_asinIndex; // asin → index in m_rows
};
