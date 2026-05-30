#ifndef TREESIZINGASINS_H
#define TREESIZINGASINS_H

#include <QAbstractItemModel>
#include <QDir>
#include <QList>
#include <QMap>
#include <QDate>
#include <QPair>
#include <QString>

#include <QCoro/QCoroTask>

#include "AmazonCatalogApi.h"

class TreeSizingAsins : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        SKU = 0,
        ASIN,
        Size,
        Color,
        Title,
        SizeImage,
        APlusContent,
        SizeTable,
        COLUMN_COUNT
    };

    explicit TreeSizingAsins(const QDir& workingDir, QObject* parent = nullptr);
    ~TreeSizingAsins() override;

    void setApiClient(AmazonCatalogApi* api);

    // asinOrXlsxPath: if ends with ".xlsx", reads ASIN values from the
    // corresponding column of the file; otherwise treats it as a raw ASIN.
    QCoro::Task<void> load(const QString& asinOrXlsxPath,
                           const QString& marketplaceId = QStringLiteral("A13V1IB3VIYZZH"));

    QCoro::Task<void> recordSizeImageUploaded(const QString& asin, const QDate& date,
                                              const QString& marketplaceId);
    QCoro::Task<void> recordAPlusUploaded(const QString& asin, const QDate& date,
                                          const QString& marketplaceId);

    // QAbstractItemModel interface
    QModelIndex index(int row, int col, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

signals:
    void loadError(const QString& message);
    // Emitted after the first variation family loads; carries the first child's
    // bullet points and material/fabric attributes for use in content generation.
    void attributesFetched(QStringList bulletPoints, QStringList materialAttrs,
                           QString mainImageUrl, QString parentAsin, QString firstChildTitle);
    // Images grouped by color variant: each pair is (colorName, imageUrls).
    // For size-only products, deduplication by color yields a single entry.
    // For color variants, one entry per unique color is emitted so each
    // color's photos can be downloaded and shown separately.
    void variantImagesFetched(QList<QPair<QString, QStringList>> colorImages);
    // Emitted alongside variantImagesFetched: maps color.toLower() → all child ASINs
    // with that color (all sizes). Empty-string key covers size-only products.
    void colorAsinsReady(QMap<QString, QStringList> colorToAsins);
    // Emitted after the first family load: country codes where the product
    // exists (no suffix) followed by missing ones suffixed with " (missing)".
    // Regions checked: EU representative (FR), NA representative (US), JP.
    void marketplacesChecked(QStringList countryCodes);

private:
    struct ChildItem {
        QString sku;
        QString asin;
        QString size;
        QString color;
        QString title;
        bool    hasSizeTable = false;
        QDate   sizeImageDate;
        QDate   aPlusDate;
    };
    struct ParentItem {
        QString sku;
        QString asin;
        QList<ChildItem> children;
    };

    // Internal id encoding for tree indexes:
    //   internalId() == 0          -> index is a top-level family row
    //                                 (family index == row())
    //   internalId() == parentRow+1 -> index is a child row of
    //                                  m_families[parentRow]
    static constexpr quintptr kTopLevelId = 0;

    // Index helpers
    QModelIndex _makeTopIndex(int familyRow, int col) const;
    QModelIndex _makeChildIndex(int familyRow, int childRow, int col) const;

    QStringList _readAsinsFromXlsx(const QString& xlsxPath) const;

    void _loadJson();
    void _saveJson();
    void _applyDatesToFamily(ParentItem& family);

    // GCC 13 ICE workaround: pass QString parameters by value.
    QCoro::Task<void> _findOrLoadFamily(QString asin, QString marketplaceId, int* outFamilyIndex);

    QDir                m_workingDir;
    AmazonCatalogApi*   m_api = nullptr;
    QList<ParentItem>   m_families;
    QMap<QString, QDate> m_sizeImageDates;
    QMap<QString, QDate> m_aPlusDates;
};

#endif // TREESIZINGASINS_H
