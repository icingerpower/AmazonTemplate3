#pragma GCC optimize("O1")
#include "PaneSales.h"

#include "marketplaces/AbstractTargetMarketplaceFactory.h"
#include "workingdirectory/WorkingDirectoryManager.h"
#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStringListModel>
#include <QTableView>
#include <QTimeZone>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <exception>

// QWidget painting avoids introducing a Qt Charts deployment dependency.
class SalesChart : public QWidget
{
public:
    explicit SalesChart(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("salesChart");
        setMinimumSize(480, 220);
        setAccessibleName(tr("Monthly order count chart"));
    }
    QString title;
    QList<QDate> months;
    QList<int> counts;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        p.setPen(palette().text().color());
        if (title.isEmpty()) {
            p.drawText(rect(), Qt::AlignCenter, tr("Select a channel row to see monthly orders"));
            return;
        }
        p.drawText(QRect(0, 8, width(), 24), Qt::AlignCenter, title + tr(" — Orders"));
        const QRectF plot(65, 48, width() - 95, height() - 110);
        const int maximum = std::max(1, counts.isEmpty() ? 1 : *std::max_element(counts.begin(), counts.end()));
        const double step = std::max(1.0, std::ceil(double(maximum) / 4));
        const double scale = step * 4;
        for (int tick = 0; tick <= 4; ++tick) {
            const double value = step * tick;
            const double y = plot.bottom() - plot.height() * value / scale;
            p.setPen(palette().mid().color());
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(palette().text().color());
            p.drawText(QRectF(0, y - 10, 55, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(value, 'f', 0));
        }
        QPainterPath line;
        bool connected = false;
        for (int i = 0; i < months.size(); ++i) {
            const double x = plot.left() + plot.width() * i / std::max(1, int(months.size()) - 1);
            p.setPen(palette().text().color());
            p.drawText(QRectF(x - 25, plot.bottom() + 8, 50, 36), Qt::AlignCenter,
                       QLocale().toString(months[i], "MMM") + '\n' + months[i].toString("yyyy"));
            if (i >= counts.size() || counts[i] < 0) {
                connected = false;
                continue;
            }
            const QPointF point(x, plot.bottom() - plot.height() * counts[i] / scale);
            if (connected) line.lineTo(point); else line.moveTo(point);
            connected = true;
            p.setPen(QPen(palette().highlight().color(), 2));
            p.setBrush(palette().highlight());
            p.drawEllipse(point, 3, 3);
            p.setPen(palette().text().color());
            p.drawText(QRectF(x - 25, point.y() - 23, 50, 18), Qt::AlignCenter, QString::number(counts[i]));
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(palette().highlight().color(), 2));
        p.drawPath(line);
    }
};

struct PaneSales::CountJob
{
    QList<AbstractSalesChannel *> channels;
    QList<QDate> months;
    QDateTime until;
    std::shared_ptr<bool> cancelled = std::make_shared<bool>(false);
    bool retryFailed = false;
    ~CountJob() { qDeleteAll(channels); }
};

QList<QDate> PaneSales::monthsEndingAt(QDate date)
{
    QList<QDate> result;
    const QDate first(date.year(), date.month(), 1);
    for (int i = -11; i <= 0; ++i)
        result.append(first.addMonths(i));
    return result;
}

PaneSales::PaneSales(QWidget *parent)
    : PaneSales(WorkingDirectoryManager::instance()->settings(),
                AbstractTargetMarketplaceFactory::ALL_MARKETPLACE_FACTORIES(), parent) {}

PaneSales::PaneSales(QSharedPointer<QSettings> settings,
                     QList<AbstractTargetMarketplaceFactory *> factories, QWidget *parent)
    : QWidget(parent), m_settings(std::move(settings)), m_factories(std::move(factories))
{
    auto *layout = new QHBoxLayout(this);
    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Marketplace"), this));
    m_platforms = new QListView(this);
    m_platforms->setObjectName("listViewMarketplaces");
    m_platforms->setMaximumWidth(200);
    m_platforms->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QStringList names;
    for (const auto *factory : m_factories)
        names.append(factory->platformDisplayName());
    m_platforms->setModel(new QStringListModel(names, m_platforms));
    left->addWidget(m_platforms);
    m_count = new QPushButton(tr("Count order"), this);
    m_count->setObjectName("buttonCountOrder");
    left->addWidget(m_count);
    m_retry = new QPushButton(tr("Retry failed"), this);
    m_retry->setObjectName("buttonRetrySales");
    left->addWidget(m_retry);
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_cancel->setObjectName("buttonCancelCount");
    left->addWidget(m_cancel);
    layout->addLayout(left);

    auto *right = new QVBoxLayout;
    auto *description = new QLabel(tr("Monthly order counts · UTC · Current month is partial"), this);
    description->setToolTip(tr("Counts reported by each marketplace, not item quantities. "
        "Temu includes parent orders in all statuses, including canceled orders. "
        "Unavailable months remain blank in the chart."));
    right->addWidget(description);
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setObjectName("splitterSales");
    m_table = new QTableView(splitter);
    m_table->setObjectName("tableViewSales");
    m_counts = new QStandardItemModel(this);
    m_table->setModel(m_counts);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_chart = new SalesChart(splitter);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    right->addWidget(splitter, 1);
    m_status = new QLabel(this);
    m_status->setObjectName("labelSalesStatus");
    m_status->setWordWrap(true);
    right->addWidget(m_status);
    layout->addLayout(right, 1);
    connect(m_platforms->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
        resetTable();
        setBusy(false);
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { updateChart(); });
    connect(m_counts, &QStandardItemModel::dataChanged, this, [this] { updateChart(); });
    connect(m_count, &QPushButton::clicked, this, [this] { startCounting(); });
    connect(m_retry, &QPushButton::clicked, this, [this] { startCounting(true); });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_job) *m_job->cancelled = true;
        m_cancel->setEnabled(false);
        m_status->setText(tr("Canceling…"));
    });
    resetTable();
    if (!names.isEmpty())
        m_platforms->setCurrentIndex(m_platforms->model()->index(0, 0));
    setBusy(false);
}

PaneSales::~PaneSales()
{
    if (m_job) *m_job->cancelled = true;
}

void PaneSales::resetTable()
{
    m_months = monthsEndingAt(QDateTime::currentDateTimeUtc().date());
    m_counts->clear();
    QStringList headers;
    for (const auto &month : m_months)
        headers.append(QLocale().toString(month, "MMM yyyy"));
    m_counts->setHorizontalHeaderLabels(headers);
    m_status->setText(tr("Click Count order to load the last 12 months."));
    updateChart();
}

void PaneSales::setBusy(bool busy)
{
    m_platforms->setEnabled(!busy);
    m_count->setEnabled(!busy && m_platforms->currentIndex().isValid());
    bool missing = false;
    for (int row = 0; row < m_counts->rowCount(); ++row)
        for (int month = 0; month < m_counts->columnCount(); ++month)
            missing |= !m_counts->index(row, month).data(Qt::UserRole).isValid();
    m_retry->setEnabled(!busy && missing);
    m_cancel->setVisible(busy);
    m_cancel->setEnabled(busy);
}

void PaneSales::startCounting(bool retryFailed)
{
    if (m_job || !m_platforms->currentIndex().isValid()) return;
    if (!retryFailed) {
        resetTable();
        m_until = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch(), QTimeZone::UTC);
    }
    auto job = std::make_shared<CountJob>();
    job->months = m_months;
    job->until = m_until;
    job->retryFailed = retryFailed;
    job->channels = m_factories.at(m_platforms->currentIndex().row())->createSalesChannels(m_settings.data());
    if (job->channels.isEmpty()) {
        m_status->setText(tr("No configured sales channels. Configure this marketplace in Settings first."));
        setBusy(false);
        return;
    }
    if (retryFailed) {
        // Settings may have changed since the first count. Never apply results
        // to a different store merely because it now occupies the same row.
        bool same = job->channels.size() == m_counts->rowCount();
        for (int row = 0; same && row < job->channels.size(); ++row)
            same = job->channels[row]->displayName() == m_counts->verticalHeaderItem(row)->text();
        if (!same) {
            m_status->setText(tr("Configured channels changed. Click Count order to reload them."));
            return;
        }
    } else {
        for (auto *channel : job->channels) {
            QList<QStandardItem *> cells;
            for (int month = 0; month < 12; ++month) {
                auto *cell = new QStandardItem(QStringLiteral("—"));
                cell->setTextAlignment(Qt::AlignCenter);
                cell->setToolTip(tr("Not counted"));
                cells.append(cell);
            }
            m_counts->appendRow(cells);
            m_counts->setVerticalHeaderItem(m_counts->rowCount() - 1, new QStandardItem(channel->displayName()));
        }
        m_table->selectRow(0);
    }
    m_job = job;
    setBusy(true);
    m_task = countOrders(this, job);
}

QCoro::Task<void> PaneSales::countOrders(QPointer<PaneSales> pane, std::shared_ptr<CountJob> job)
{
    QString firstError;
    for (int row = 0; row < job->channels.size() && !*job->cancelled; ++row) {
        if (!pane) co_return;
        auto *channel = job->channels[row];
        QList<QDate> requested;
        QList<int> columns;
        for (int month = 0; month < job->months.size(); ++month) {
            if (job->retryFailed && pane->m_counts->index(row, month).data(Qt::UserRole).isValid()) continue;
            requested.append(job->months[month]);
            columns.append(month);
        }
        if (requested.isEmpty()) continue;
        const QString progress = tr("Counting %1 (%2/%3)")
            .arg(channel->displayName()).arg(row + 1).arg(job->channels.size());
        pane->m_status->setText(progress + QStringLiteral("…"));
        channel->setReadCancellation(job->cancelled);
        channel->setReadProgress([pane, progress, cancelled = job->cancelled](const QString &message) {
            if (pane && !*cancelled) pane->m_status->setText(progress + QStringLiteral(" · ") + message);
        });
        QList<SalesMonthCount> results;
        QString error;
        try {
            co_await channel->fetchMonthlyOrderCounts(requested, job->until, &results);
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        } catch (...) {
            error = tr("Unexpected error while counting orders");
        }
        if (!pane) co_return;
        for (int i = 0; i < requested.size(); ++i) {
            const auto result = i < results.size() ? results[i] : SalesMonthCount{-1, error};
            auto *item = pane->m_counts->item(row, columns[i]);
            if (result.count >= 0) {
                item->setData(result.count, Qt::DisplayRole);
                item->setData(result.count, Qt::UserRole);
                const auto from = requested[i].startOfDay(QTimeZone::UTC);
                const auto to = qMin(requested[i].addMonths(1).startOfDay(QTimeZone::UTC), job->until);
                item->setToolTip(tr("%1 orders · %2 to %3 (exclusive), UTC")
                    .arg(result.count).arg(from.toString(Qt::ISODate), to.toString(Qt::ISODate)));
            } else if (!*job->cancelled) {
                item->setText(tr("N/A"));
                const QString detail = result.error.isEmpty() ? tr("Order count unavailable") : result.error;
                item->setToolTip(detail);
                if (firstError.isEmpty()) firstError = channel->displayName() + ": " + detail;
            }
        }
    }
    if (!pane) co_return;
    int failures = 0;
    for (int row = 0; row < pane->m_counts->rowCount(); ++row)
        for (int month = 0; month < pane->m_counts->columnCount(); ++month)
            failures += !pane->m_counts->index(row, month).data(Qt::UserRole).isValid();
    pane->m_status->setText(*job->cancelled ? tr("Canceled. Counts already retrieved are shown. Use Retry failed to resume.")
        : failures ? tr("%1 unavailable months. Use Retry failed. %2").arg(failures).arg(firstError)
                   : tr("Order counts updated. Select a row to view its chart."));
    // Completed Tasks can retain their frames; release clients and credentials.
    qDeleteAll(job->channels);
    job->channels.clear();
    pane->m_job.reset();
    pane->setBusy(false);
}

void PaneSales::updateChart()
{
    const int row = m_table->currentIndex().row();
    m_chart->title.clear();
    m_chart->counts.clear();
    m_chart->months = m_months;
    if (row >= 0 && row < m_counts->rowCount()) {
        m_chart->title = m_counts->verticalHeaderItem(row)->text();
        for (int month = 0; month < m_counts->columnCount(); ++month) {
            const auto value = m_counts->index(row, month).data(Qt::UserRole);
            m_chart->counts.append(value.isValid() ? value.toInt() : -1);
        }
    }
    m_chart->setAccessibleDescription(m_chart->title);
    m_chart->update();
}
