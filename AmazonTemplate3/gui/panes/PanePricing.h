#ifndef PANEPRICING_H
#define PANEPRICING_H
#include "pricing/AmazonPricingRepository.h"
#include <QCoro/QCoroTask>
#include <QDir>
#include <QJsonArray>
#include <QSettings>
#include <QWidget>
#include <memory>
QT_BEGIN_NAMESPACE
namespace Ui {
class PanePricing;
}
QT_END_NAMESPACE
class QStandardItemModel;
class QLabel;
class QButtonGroup;

class PanePricing : public QWidget {
    Q_OBJECT
  public:
    explicit PanePricing(QWidget *parent = nullptr);
    explicit PanePricing(const QDir &workingDir, QWidget *parent = nullptr);
    ~PanePricing();
    QList<AmazonPricingRepository::Update> pendingUpdates() const;

  signals:
    void previewChanged();

  private:
    void changed();
    void restoreSort();
    void saveFilters();
    void loadFilter(int index);
    void rebuildChoices();
    void viewAllPrices();
    bool validRules() const;
    QList<Pricing::Market> marketsForRegion(bool keepMetadata = false) const;
    PricingFilterProxy::Filter filter() const;
    AmazonPricingRepository::Credentials credentials(bool includeSecrets) const;
    QCoro::Task<void> retrieve();
    QCoro::Task<void> update();
    QCoro::Task<void> refreshRates();
    Ui::PanePricing *ui;
    QDir m_working;
    QSettings m_settings;
    TableAmazonPricing *m_model;
    PricingFilterProxy *m_proxy;
    QStandardItemModel *m_prices;
    QLabel *m_status;
    QButtonGroup *m_modes;
    QList<Pricing::Market> m_markets;
    QString m_region;
    QJsonArray m_filters;
    QHash<QString, double> m_overrides;
    double m_default = -1;
    Pricing::Direction m_direction = Pricing::Direction::Increase;
    bool m_loading = true, m_busy = false;
    std::unique_ptr<AmazonPricingRepository> m_repository;
    std::shared_ptr<bool> m_cancelled;
    QCoro::Task<void> m_task;
};
#endif
