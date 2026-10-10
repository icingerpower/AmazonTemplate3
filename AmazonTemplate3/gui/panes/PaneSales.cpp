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
    bool cancelled = false;
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
    connect(m_count, &QPushButton::clicked, this, &PaneSales::startCounting);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_job) m_job->cancelled = true;
        m_cancel->setEnabled(false);
        m_status->setText(tr("Canceling after the current request…"));
    });
    resetTable();
    if (!names.isEmpty())
        m_platforms->setCurrentIndex(m_platforms->model()->index(0, 0));
    setBusy(false);
}

PaneSales::~PaneSales()
{
    if (m_job) m_job->cancelled = true;
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
    m_cancel->setVisible(busy);
    m_cancel->setEnabled(busy);
}

void PaneSales::startCounting()
{
    if (m_job || !m_platforms->currentIndex().isValid()) return;
    resetTable();
    auto job = std::make_shared<CountJob>();
    job->months = m_months;
    job->until = QDateTime::currentDateTimeUtc();
    job->channels = m_factories.at(m_platforms->currentIndex().row())->createSalesChannels(m_settings.data());
    if (job->channels.isEmpty()) {
        m_status->setText(tr("No configured sales channels. Configure this marketplace in Settings first."));
        return;
    }
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
    m_job = job;
    setBusy(true);
    m_task = countOrders(this, job);
}

QCoro::Task<void> PaneSales::countOrders(QPointer<PaneSales> pane, std::shared_ptr<CountJob> job)
{
    int failures = 0;
    for (int row = 0; row < job->channels.size() && !job->cancelled; ++row) {
        auto *channel = job->channels[row];
        for (int month = 0; month < job->months.size() && !job->cancelled; ++month) {
            if (!pane) co_return;
            pane->m_status->setText(tr("Counting %1 · %2 (%3/%4)…")
                .arg(channel->displayName(), job->months[month].toString("MMM yyyy"))
                .arg(row * 12 + month + 1).arg(job->channels.size() * 12));
            const auto from = job->months[month].startOfDay(QTimeZone::UTC);
            const auto to = std::min(job->months[month].addMonths(1).startOfDay(QTimeZone::UTC), job->until);
            int count = -1;
            QString error;
            try {
                co_await channel->fetchOrderCount(from, to, &count);
                error = channel->lastError();
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            } catch (...) {
                error = tr("Unexpected error while counting orders");
            }
            if (!pane) co_return;
            if (job->cancelled) break;
            auto *item = pane->m_counts->item(row, month);
            if (count >= 0 && error.isEmpty()) {
                item->setData(count, Qt::DisplayRole);
                item->setData(count, Qt::UserRole);
                item->setToolTip(tr("%1 orders · %2 to %3 (exclusive), UTC")
                    .arg(count).arg(from.toString(Qt::ISODate), to.toString(Qt::ISODate)));
            } else {
                ++failures;
                item->setText(tr("N/A"));
                item->setToolTip(error.isEmpty() ? tr("Order count unavailable") : error);
            }
        }
    }
    if (!pane) co_return;
    pane->m_status->setText(job->cancelled ? tr("Canceled. Counts already retrieved are shown.")
        : failures ? tr("Finished with %1 unavailable months. Hover over N/A for details.").arg(failures)
                   : tr("Order counts updated. Select a row to view its chart."));
    // A completed Task can retain its frame until the next refresh. Release API
    // clients (and their credentials) now, rather than keeping them in the pane.
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
