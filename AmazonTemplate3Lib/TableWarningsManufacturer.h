#ifndef TABLEWARNINGSMANUFACTURER_H
#define TABLEWARNINGSMANUFACTURER_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>

// Session-only table of GPSR "manufacturer_contact" warnings.
// Rebuilt on each load from the violations returned by AmazonWarningsApi.
// The Manufacturer column is editable so the user can assign one of the
// known manufacturers (from TableGpsrManufacturers) to each warning row.
// No persistence — this is a transient working table.
struct ManufacturerWarningRow {
    QString sku;
    QString asin;
    QString title;
    QString countryCode;
    QString attributeId;
    QString issueMessage;
    QString manufacturerName;
};

class TableWarningsManufacturer : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColSku = 0, ColAsin, ColTitle, ColCountry, ColAttributeId, ColDescription, ColManufacturer, ColCount };

    explicit TableWarningsManufacturer(QObject *parent = nullptr);

    void addRow(const ManufacturerWarningRow &row);
    void clear();

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
    QList<ManufacturerWarningRow> m_rows;
};

#endif // TABLEWARNINGSMANUFACTURER_H
