#pragma once
#include "PricingData.h"
#include <QAbstractTableModel>
#include <QSortFilterProxyModel>

class TableAmazonPricing : public QAbstractTableModel {
    Q_OBJECT
  public:
    enum Mode { OnePrice, Continent, AllCountry };
    enum { Image, Sku, Title, MainPrice, AllPrice, Changes, Sales90, Size, Days, Color, FixedCount };
    enum { SortRole = Qt::UserRole + 1, RowKeyRole, RichTextRole };
    struct Group {
        QString id, label;
        QList<int> markets;
    };
    explicit TableAmazonPricing(QObject *parent = nullptr);
    void setMarkets(const QList<Pricing::Market> &markets);
    const QList<Pricing::Market> &markets() const {
        return m_markets;
    }
    void setRows(QList<Pricing::Row> rows, bool preserveEdits = true);
    const QList<Pricing::Row> &rows() const {
        return m_rows;
    }
    void setMode(Mode mode);
    Mode mode() const {
        return m_mode;
    }
    void setRules(double defaultEur, QHash<QString, double> overrides, Pricing::Direction direction);
    double proposed(const Pricing::Row &row, int market) const;
    void resetRows(const QSet<QString> &skus);
    void resetMarket(const QString &sku, const QString &mp);
    void updateRow(const Pricing::Row &row);
    QString columnKey(int column) const;
    int columnForKey(const QString &key) const;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

  private:
    struct ChangeGroup {
        double eur;
        bool increase;
        QStringList countries;
    };
    QList<ChangeGroup> changeGroups(const Pricing::Row &row) const;
    void rebuildGroups();
    // One price exposes the current aggregate as AllPrice, leaving only its new-price column here.
    int priceOffset(int column) const {
        return column - FixedCount + (m_mode == OnePrice ? 1 : 0);
    }
    Group m_allPrices;

    QList<Pricing::Market> m_markets;
    QList<Pricing::Row> m_rows;
    QList<Group> m_groups;
    Mode m_mode = OnePrice;
    double m_defaultEur = -1;
    QHash<QString, double> m_overrides;
    Pricing::Direction m_direction = Pricing::Direction::Increase;
};

class PricingFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
  public:
    struct Filter {
        QString sku, title, brand, sizeFrom, sizeTo, region;
        QStringList productTypes;
        double minPrice = 0, maxPrice = 0; // EUR; zero means unrestricted
        int minDays = 0;
    };
    explicit PricingFilterProxy(QObject *parent = nullptr);
    void setFilter(Filter filter);
    bool accepts(const Pricing::Row &row, const QList<Pricing::Market> &markets,
                 bool metadataOnly = false) const;
    bool mayMatchMetadata(const Pricing::Row &row) const;

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

  private:
    Filter m_filter;
};
