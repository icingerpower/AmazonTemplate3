#ifndef TABLEPRODUCTWARNINGS_H
#define TABLEPRODUCTWARNINGS_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>
#include <QStringList>

struct WarningRow {
    QString asin;
    QString sku;
    QString title;        // best available: en > fr > de > marketplace lang
    QString attributeId;
    QString issueMessage; // human-readable violation description from the issues[] array
    QString value;        // current attribute value (may be empty)
    QString aiValue;      // editable — filled by user or CLI later
    QString mainImageUrl; // not displayed, used for downloading the product image
    QStringList bulletPoints; // all current bullet values (for bullet_point violations only)
};

class TableProductWarnings : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        ColAsin = 0,
        ColSku,
        ColTitle,
        ColAttributeId,
        ColError,
        ColValue,
        ColAiValue,
        ColCount
    };

    explicit TableProductWarnings(QObject *parent = nullptr);

    void addRow(const WarningRow &row);
    void clear();
    const WarningRow &rowAt(int i) const { return m_rows.at(i); }

    int  rowCount   (const QModelIndex &parent = {}) const override;
    int  columnCount(const QModelIndex &parent = {}) const override;
    QVariant data       (const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData (int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool     setData    (const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags (const QModelIndex &index) const override;

private:
    QList<WarningRow>   m_rows;
    static const QStringList HEADER;
};

#endif // TABLEPRODUCTWARNINGS_H
