#pragma once

#include <QAbstractTableModel>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>

// Persistent table of GPSR manufacturer contact details per ASIN.
// The user fills in the company name / country / email / phone columns; the
// data is uploaded to Amazon as the manufacturer_contact structured attribute.
//
// Persistence: {workingDir}/gpsr_manufacturers.json
class TableGpsrManufacturers : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColAsin = 0, ColCompanyName, ColCountryCode, ColEmail, ColPhone, ColTitle, ColCount };

    explicit TableGpsrManufacturers(QObject *parent = nullptr);

    void load(const QDir &workingDir);
    void save() const;

    // Add/update row. Returns true if new. Does NOT save() automatically.
    bool addOrUpdate(const QString &asin, const QString &title);

    // Build a QJsonArray with one entry for the given ASIN and marketplaceId,
    // or empty if the ASIN has no company name.
    QJsonArray buildAttributeJson(const QString &asin, const QString &marketplaceId) const;

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
        QString asin, companyName, countryCode, email, phone, title;
    };

    QDir       m_workingDir;
    QList<Row> m_rows;
    QHash<QString, int> m_asinIndex; // asin → index in m_rows
};
