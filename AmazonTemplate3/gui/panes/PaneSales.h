#pragma once

#include <QDate>
#include <QPointer>
#include <QSharedPointer>
#include <QWidget>
#include <QCoro/QCoroTask>
#include <memory>

class AbstractTargetMarketplaceFactory;
class QLabel;
class QListView;
class QPushButton;
class QSettings;
class QStandardItemModel;
class QTableView;
class SalesChart;

class PaneSales : public QWidget
{
    Q_OBJECT
public:
    explicit PaneSales(QWidget *parent = nullptr);
    // Injectable registry/settings for offline widget tests.
    PaneSales(QSharedPointer<QSettings> settings,
              QList<AbstractTargetMarketplaceFactory *> factories, QWidget *parent = nullptr);
    ~PaneSales() override;
    static QList<QDate> monthsEndingAt(QDate date);

private:
    struct CountJob;
    QSharedPointer<QSettings> m_settings;
    QList<AbstractTargetMarketplaceFactory *> m_factories;
    QList<QDate> m_months;
    QListView *m_platforms;
    QTableView *m_table;
    QStandardItemModel *m_counts;
    QPushButton *m_count;
    QPushButton *m_cancel;
    QLabel *m_status;
    SalesChart *m_chart;
    std::shared_ptr<CountJob> m_job;
    QCoro::Task<void> m_task;

    void resetTable();
    void startCounting();
    void updateChart();
    void setBusy(bool busy);
    static QCoro::Task<void> countOrders(QPointer<PaneSales> pane, std::shared_ptr<CountJob> job);
};
