#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QDir>
#include <QList>
#include <QMap>
#include <QString>
#include <QVector>

#include <QCoro/QCoroTask>

class AmazonCatalogApi;

class BrokenChildTable : public QAbstractTableModel
{
    Q_OBJECT
public:
    struct MarketplaceSpec {
        QString id;              // e.g. "A13V1IB3VIYZZH"
        QString code;            // e.g. "FR"
        bool    active = true;   // false when the seller has no listing there ("missing")
    };

    // Mark a marketplace as active or inactive by its ID.
    // Inactive marketplaces are excluded from getFixTargets().
    void setMarketplaceActive(const QString &marketplaceId, bool active);

    struct MarketplaceHealth {
        bool    loaded     = false;
        bool    exists     = false; // false if ASIN not found in this marketplace
        bool    hasParent  = false;
        int     imageCount = 0;
    };

    struct ChildEntry {
        QString asin;
        QString sku;
        QString parentAsin;
        QString parentSku;
        QString color;
        QString size;
        QString sizeSource; // "FR", "US", "DE" — marketplace where size was read
        QVector<MarketplaceHealth> health; // one slot per MarketplaceSpec
    };

    explicit BrokenChildTable(QObject *parent = nullptr);

    // Must be called before populate(). Defines dynamic columns (one per marketplace).
    void setMarketplaces(const QList<MarketplaceSpec> &specs);
    void clear();

    // Fires API calls for each child × each marketplace, updating the model progressively.
    // Cancels any previously running populate() via a generation counter.
    // GCC 13 ICE workaround: initialChildren passed by value.
    QCoro::Task<void> populate(AmazonCatalogApi *api, QList<ChildEntry> initialChildren);

    // Persist current data to {dir}/broken_child_health.json (atomic write).
    void saveToDir(const QDir &dir) const;
    // Load data from {dir}/broken_child_health.json.
    // Returns false if the file is absent, unreadable, or the stored marketplace
    // list doesn't match m_specs (stale data after a marketplace config change).
    bool loadFromDir(const QDir &dir);

    // Description of one cell that needs fixing.
    struct FixTarget {
        int  rowIdx;
        int  mktIdx;
        bool needsParent;
        bool needsImages;
    };

    // Returns the list of (row, marketplace) cells that need to be repaired.
    // Skips cells where the ASIN is MISSING (exists == false).
    //   forParents=true  → include rows where !hasParent
    //   forImages=true   → include rows where imageCount < max image count for that color
    // A target may have both flags set simultaneously.
    QList<FixTarget> getFixTargets(bool forParents, bool forImages) const;

    // Returns the ASIN with the highest imageCount among rows whose lowercase
    // color equals colorKey in marketplace mktIdx. Used to pick a "good" sibling
    // ASIN to copy image URLs from. Returns an empty string when nothing useful
    // exists (no row for that color, all MISSING, or only the row itself has imgs).
    QString bestImageSourceAsin(const QString &colorKey, int mktIdx) const;

    // Read-only access to all rows (used by PaneSizing's fix workflow).
    const QList<ChildEntry> &rows() const { return m_rows; }

    // For each entry, fill in sku / parentSku from asinToSku if currently empty.
    // asinToSku maps any ASIN (child or parent) to its seller SKU.
    // Emits dataChanged for updated cells.
    void updateSkus(const QHash<QString, QString> &asinToSku);

    // Number of configured marketplace columns.
    int marketplaceCount() const { return m_specs.size(); }
    const MarketplaceSpec &marketplaceAt(int i) const { return m_specs[i]; }

    int           rowCount(const QModelIndex &parent = {}) const override;
    int           columnCount(const QModelIndex &parent = {}) const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool          setData(const QModelIndex &index, const QVariant &value,
                          int role = Qt::EditRole) override;

private:
    static constexpr int kFixedCols = 6;
    enum FixedCol { ColAsin = 0, ColSku, ColParentAsin, ColParentSku, ColColor, ColSize };

    // Returns dark-red if the cell is broken, invalid color otherwise.
    QColor _cellColor(int row, int mktIdx) const;
    // Recomputes m_maxImages from current m_rows data.
    void   _recomputeMaxImages();

    QList<MarketplaceSpec> m_specs;
    QList<ChildEntry>      m_rows;
    int                    m_generation = 0;

    // m_maxImages[colorKey][mktIdx] = max imageCount among all rows with that color
    QMap<QString, QVector<int>> m_maxImages;
};
