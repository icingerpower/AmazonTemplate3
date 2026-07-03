#include "APlusUploadDialog.h"

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

// Same logic as APlusWorkflow::colorSafeId — removes non-alphanumeric chars, lowercases
QString colorSafeId(const QString &color)
{
    QString r;
    for (const QChar &c : color.toLower())
        if (c.isLetterOrNumber())
            r += c;
    return r;
}

QString capitalizeFirst(const QString &s)
{
    if (s.isEmpty()) return s;
    QString r = s;
    r[0] = r.at(0).toUpper();
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// Marketplace -> suffix-key mappings
// ---------------------------------------------------------------------------

QString APlusUploadDialog::sizeChartKeyForMarketplace(const QString &marketplaceId)
{
    static const QHash<QString, QString> kMap = {
        {QStringLiteral("A1F83G8C2ARO7P"), QStringLiteral("uk")},   // UK
        {QStringLiteral("A28R8C7NBKEWEA"), QStringLiteral("uk")},   // IE (UK group)
        {QStringLiteral("A13V1IB3VIYZZH"), QStringLiteral("fr")},   // FR
        {QStringLiteral("A1PA6795UKMFR9"), QStringLiteral("de")},   // DE
        {QStringLiteral("APJ6JRA9NG5V4"),  QStringLiteral("it")},   // IT
        {QStringLiteral("A1RKKUPIHCS9HS"), QStringLiteral("es")},   // ES
        {QStringLiteral("A1805IZSGTT6HS"), QStringLiteral("nl")},   // NL
        {QStringLiteral("A2NODRKZP88ZB9"), QStringLiteral("se")},   // SE
        {QStringLiteral("A1C3SOZRARQ6R3"), QStringLiteral("pl")},   // PL
        {QStringLiteral("AMEN7PMS3EDWL"),  QStringLiteral("fr")},   // BE (French)
        {QStringLiteral("A33AVAJ2PDY3EV"), QStringLiteral("tr")},   // TR
        {QStringLiteral("ATVPDKIKX0DER"),  QStringLiteral("com")},  // US
        {QStringLiteral("A2EUQ1WTGCTBG2"), QStringLiteral("com")},  // CA
        {QStringLiteral("A1AM78C64UM0Y8"), QStringLiteral("com")},  // MX
        {QStringLiteral("A1VC38T7YXB528"), QStringLiteral("jp")},   // JP
    };
    return kMap.value(marketplaceId, QStringLiteral("uk"));
}

QString APlusUploadDialog::faqLangKeyForMarketplace(const QString &marketplaceId)
{
    static const QHash<QString, QString> kMap = {
        {QStringLiteral("A1F83G8C2ARO7P"), QStringLiteral("en")},   // UK
        {QStringLiteral("A28R8C7NBKEWEA"), QStringLiteral("en")},   // IE
        {QStringLiteral("A13V1IB3VIYZZH"), QStringLiteral("fr")},   // FR
        {QStringLiteral("A1PA6795UKMFR9"), QStringLiteral("de")},   // DE
        {QStringLiteral("APJ6JRA9NG5V4"),  QStringLiteral("it")},   // IT
        {QStringLiteral("A1RKKUPIHCS9HS"), QStringLiteral("es")},   // ES
        {QStringLiteral("A1805IZSGTT6HS"), QStringLiteral("nl")},   // NL
        {QStringLiteral("A2NODRKZP88ZB9"), QStringLiteral("se")},   // SE
        {QStringLiteral("A1C3SOZRARQ6R3"), QStringLiteral("pl")},   // PL
        {QStringLiteral("AMEN7PMS3EDWL"),  QStringLiteral("fr")},   // BE
        {QStringLiteral("A33AVAJ2PDY3EV"), QStringLiteral("tr")},   // TR
        {QStringLiteral("ATVPDKIKX0DER"),  QStringLiteral("en")},   // US
        {QStringLiteral("A2EUQ1WTGCTBG2"), QStringLiteral("en")},   // CA
        {QStringLiteral("A1AM78C64UM0Y8"), QStringLiteral("es")},   // MX
        {QStringLiteral("A1VC38T7YXB528"), QStringLiteral("jp")},   // JP
    };
    return kMap.value(marketplaceId, QStringLiteral("en"));
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

APlusUploadDialog::APlusUploadDialog(const QList<ElementInfo> &elements,
                                     const QList<QPair<QString, QString>> &marketplaces,
                                     const QStringList &colorNames,
                                     bool submitForApprovalDefault,
                                     QWidget *parent)
    : QDialog(parent),
      m_marketplaces(marketplaces)
{
    setMinimumSize(760, 480);
    setWindowTitle(tr("Upload A+ Content"));

    // 1. Partition incoming elements by type.
    for (const ElementInfo &info : elements) {
        switch (info.type) {
        case APlusElementType::SizeChart: m_sizeCharts.append(info); break;
        case APlusElementType::Faq:       m_faqs.append(info);       break;
        case APlusElementType::Image:     m_images.append(info);     break;
        }
    }

    // 2. Build m_colorNames / m_colorKeys
    if (!colorNames.isEmpty()) {
        m_colorNames = colorNames;
        for (const QString &n : colorNames)
            m_colorKeys.append(colorSafeId(n));
    } else {
        // Derive from image elements with id starting "image_color_"
        const QString prefix = QStringLiteral("image_color_");
        for (const ElementInfo &info : m_images) {
            if (!info.id.startsWith(prefix)) continue;
            const QString suffix = info.id.mid(prefix.size());
            const QString key = colorSafeId(suffix);
            if (key.isEmpty()) continue;
            if (m_colorKeys.contains(key)) continue;
            m_colorKeys.append(key);
            m_colorNames.append(capitalizeFirst(suffix));
        }
    }
    if (m_colorNames.isEmpty()) {
        m_colorNames = QStringList{ tr("Images") };
        m_colorKeys  = QStringList{ QString() };
    }

    // 3. Layout
    auto *root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(8, 8, 8, 8);

    // --- Countries area ---
    auto *countriesBox = new QGroupBox(tr("Upload to:"), this);
    auto *countriesLayout = new QHBoxLayout(countriesBox);
    for (const auto &mp : m_marketplaces) {
        auto *cb = new QCheckBox(mp.first, countriesBox);
        cb->setChecked(true);
        countriesLayout->addWidget(cb);
        m_countryChecks.append(cb);
        connect(cb, &QCheckBox::toggled, this, &APlusUploadDialog::onOptionChanged);
    }
    countriesLayout->addStretch();
    root->addWidget(countriesBox);

    // --- Module order strip ---
    // Shows the A+ module sequence: ① Size Chart → ② Images → ③ FAQ
    // Checkboxes for Size Chart and FAQ sit directly on their position labels.
    auto *orderRow = new QHBoxLayout;
    orderRow->setSpacing(6);
    orderRow->addWidget(new QLabel(tr("Module order:"), this));

    // Position ①: Size Chart
    m_inclSizeChart = new QCheckBox(tr("① Size Chart"), this);
    m_inclSizeChart->setChecked(true);
    if (m_sizeCharts.isEmpty()) {
        m_inclSizeChart->setEnabled(false);
        m_inclSizeChart->setChecked(false);
        m_inclSizeChart->setVisible(false);
    }
    orderRow->addWidget(m_inclSizeChart);

    orderRow->addWidget(new QLabel(QStringLiteral("→"), this));

    // Position ②: Images (always present, no toggle)
    orderRow->addWidget(new QLabel(tr("② Images (table below)"), this));

    orderRow->addWidget(new QLabel(QStringLiteral("→"), this));

    // Position ③: FAQ
    m_inclFaq = new QCheckBox(tr("③ FAQ"), this);
    m_inclFaq->setChecked(true);
    if (m_faqs.isEmpty()) {
        m_inclFaq->setEnabled(false);
        m_inclFaq->setChecked(false);
        m_inclFaq->setVisible(false);
    }
    orderRow->addWidget(m_inclFaq);

    orderRow->addStretch();
    root->addLayout(orderRow);

    // --- Budget label ---
    m_budgetLabel = new QLabel(this);
    root->addWidget(m_budgetLabel);

    // --- Table ---
    m_table = new QTableWidget(m_images.size(), m_colorKeys.size(), this);
    _buildTable();
    root->addWidget(m_table, /*stretch=*/1);

    // --- Bottom row ---
    auto *bottomRow = new QHBoxLayout;
    m_submitCheck = new QCheckBox(tr("Submit for approval after creation"), this);
    m_submitCheck->setChecked(submitForApprovalDefault);
    bottomRow->addWidget(m_submitCheck);
    bottomRow->addStretch();

    auto *btnBox = new QDialogButtonBox(this);
    m_uploadButton = btnBox->addButton(tr("Upload"), QDialogButtonBox::AcceptRole);
    btnBox->addButton(QDialogButtonBox::Cancel);
    m_uploadButton->setEnabled(false);
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bottomRow->addWidget(btnBox);

    root->addLayout(bottomRow);

    // 4. Apply default checks BEFORE connecting itemChanged to avoid spurious updates.
    _applyDefaultChecks();
    _updateBudget();

    // 5. Connect option signals + table itemChanged (after init).
    if (m_inclSizeChart)
        connect(m_inclSizeChart, &QCheckBox::toggled,
                this, &APlusUploadDialog::onOptionChanged);
    if (m_inclFaq)
        connect(m_inclFaq, &QCheckBox::toggled,
                this, &APlusUploadDialog::onOptionChanged);
    connect(m_table, &QTableWidget::itemChanged,
            this, &APlusUploadDialog::onTableItemChanged);
}

// ---------------------------------------------------------------------------
// _buildTable
// ---------------------------------------------------------------------------

void APlusUploadDialog::_buildTable()
{
    QSignalBlocker b(m_table);

    m_table->setHorizontalHeaderLabels(m_colorNames);

    QStringList vHeaders;
    vHeaders.reserve(m_images.size());
    for (const ElementInfo &info : m_images)
        vHeaders << info.displayName;
    m_table->setVerticalHeaderLabels(vHeaders);

    const QBrush disabledBg(QColor(220, 220, 220));
    const QBrush disabledFg(QColor(140, 140, 140));

    for (int row = 0; row < m_images.size(); ++row) {
        for (int col = 0; col < m_colorKeys.size(); ++col) {
            const bool enabled = _isCellEnabled(row, col);
            auto *item = new QTableWidgetItem(QString());
            if (enabled) {
                item->setFlags(Qt::ItemIsUserCheckable
                               | Qt::ItemIsEnabled
                               | Qt::ItemIsSelectable);
                item->setCheckState(Qt::Unchecked);
            } else {
                item->setFlags(Qt::NoItemFlags);
                item->setBackground(disabledBg);
                item->setForeground(disabledFg);
            }
            m_table->setItem(row, col, item);
        }
    }

    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
}

// ---------------------------------------------------------------------------
// _isCellEnabled
// ---------------------------------------------------------------------------

bool APlusUploadDialog::_isCellEnabled(int row, int col) const
{
    if (row < 0 || row >= m_images.size()) return false;
    if (col < 0 || col >= m_colorKeys.size()) return false;
    const QString &id = m_images.at(row).id;
    static const QString kColorPrefix  = QStringLiteral("image_color_");
    static const QString kDetailPrefix = QStringLiteral("image_detail_");
    const bool isColorSlot  = id.startsWith(kColorPrefix);
    const bool isDetailSlot = id.startsWith(kDetailPrefix);
    if (!isColorSlot && !isDetailSlot) return true;  // non-color-specific: enabled everywhere
    const QString imageKey = isColorSlot
        ? colorSafeId(id.mid(kColorPrefix.size()))
        : colorSafeId(id.mid(kDetailPrefix.size()));
    const QString &colKey = m_colorKeys.at(col);
    if (colKey.isEmpty() || imageKey == colKey) return true;  // exact match
    // If imageKey matches another column, this element belongs exclusively there.
    for (const QString &k : std::as_const(m_colorKeys))
        if (!k.isEmpty() && imageKey == k) return false;
    // imageKey matches no column at all — the element was generated with a colour name
    // in a different language than the current session (e.g. "Blanc et or jaune" stored
    // but "White and Yellow Gold" is the current name). Enable it only for columns that
    // have no native element of the same type so it can fill the gap rather than being invisible.
    const QString &sameTypePrefix = isColorSlot ? kColorPrefix : kDetailPrefix;
    for (int r = 0; r < m_images.size(); ++r) {
        if (r == row) continue;
        const QString &rid = m_images.at(r).id;
        if (!rid.startsWith(sameTypePrefix)) continue;
        if (colorSafeId(rid.mid(sameTypePrefix.size())) == colKey) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// _budget
// ---------------------------------------------------------------------------

int APlusUploadDialog::_budget() const
{
    int b = 5;
    if (m_inclSizeChart && m_inclSizeChart->isChecked() && !m_sizeCharts.isEmpty()) --b;
    if (m_inclFaq && m_inclFaq->isChecked() && !m_faqs.isEmpty()) --b;
    return qMax(0, b);
}

// ---------------------------------------------------------------------------
// _applyDefaultChecks
// ---------------------------------------------------------------------------

void APlusUploadDialog::_applyDefaultChecks()
{
    QSignalBlocker b(m_table);
    const int budget = _budget();
    const int rowCount = m_table->rowCount();
    const int colCount = m_table->columnCount();

    for (int col = 0; col < colCount; ++col) {
        int count = 0;
        for (int row = 0; row < rowCount; ++row) {
            QTableWidgetItem *it = m_table->item(row, col);
            if (!it) continue;
            if (!_isCellEnabled(row, col)) continue;
            if (count < budget) {
                it->setCheckState(Qt::Checked);
                ++count;
            } else {
                it->setCheckState(Qt::Unchecked);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// _updateBudget
// ---------------------------------------------------------------------------

void APlusUploadDialog::_updateBudget()
{
    const int budget = _budget();
    const bool hasSC  = m_inclSizeChart && m_inclSizeChart->isChecked() && !m_sizeCharts.isEmpty();
    const bool hasFaq = m_inclFaq && m_inclFaq->isChecked() && !m_faqs.isEmpty();

    QString detail;
    if (hasSC && hasFaq)
        detail = tr("(5 - 1 size chart - 1 FAQ)");
    else if (hasSC)
        detail = tr("(5 - 1 size chart)");
    else if (hasFaq)
        detail = tr("(5 - 1 FAQ)");
    else
        detail = tr("(5 image slots)");

    if (m_budgetLabel) {
        m_budgetLabel->setText(
            tr("Up to %1 image slots per column %2").arg(budget).arg(detail));
    }

    // Enforce: for each column, recount checked items. If over budget, auto-uncheck
    // from bottom up. Then update enabled/disabled flags for unchecked items.
    {
        QSignalBlocker b(m_table);
        const int rowCount = m_table->rowCount();
        const int colCount = m_table->columnCount();

        for (int col = 0; col < colCount; ++col) {
            // Pass 1: count checked, uncheck excess from bottom up.
            QList<int> checkedRows;
            for (int row = 0; row < rowCount; ++row) {
                QTableWidgetItem *it = m_table->item(row, col);
                if (!it) continue;
                if (!_isCellEnabled(row, col)) continue;
                if (it->checkState() == Qt::Checked)
                    checkedRows.append(row);
            }
            while (checkedRows.size() > budget) {
                const int rmRow = checkedRows.takeLast();
                QTableWidgetItem *it = m_table->item(rmRow, col);
                if (it) it->setCheckState(Qt::Unchecked);
            }
            const int count = checkedRows.size();

            // Pass 2: update flags for enabled-by-color cells.
            for (int row = 0; row < rowCount; ++row) {
                QTableWidgetItem *it = m_table->item(row, col);
                if (!it) continue;
                if (!_isCellEnabled(row, col)) continue;
                const bool checked = (it->checkState() == Qt::Checked);
                Qt::ItemFlags f = Qt::ItemIsUserCheckable | Qt::ItemIsSelectable;
                if (checked || count < budget)
                    f |= Qt::ItemIsEnabled;
                it->setFlags(f);
            }
        }
    }

    _updateUploadButton();
}

// ---------------------------------------------------------------------------
// _updateUploadButton
// ---------------------------------------------------------------------------

void APlusUploadDialog::_updateUploadButton()
{
    if (!m_uploadButton) return;

    bool anyCountry = false;
    for (QCheckBox *cb : m_countryChecks) {
        if (cb && cb->isChecked()) { anyCountry = true; break; }
    }

    bool anyCellChecked = false;
    if (m_table) {
        const int rowCount = m_table->rowCount();
        const int colCount = m_table->columnCount();
        for (int row = 0; row < rowCount && !anyCellChecked; ++row) {
            for (int col = 0; col < colCount; ++col) {
                QTableWidgetItem *it = m_table->item(row, col);
                if (it && it->checkState() == Qt::Checked) {
                    anyCellChecked = true;
                    break;
                }
            }
        }
    }

    const bool hasSC  = includeSizeChart();
    const bool hasFaq = includeFaq();

    m_uploadButton->setEnabled(anyCountry && (anyCellChecked || hasSC || hasFaq));
}

// ---------------------------------------------------------------------------
// Slot handlers
// ---------------------------------------------------------------------------

void APlusUploadDialog::onOptionChanged()
{
    _updateBudget();
    _updateUploadButton();
}

void APlusUploadDialog::onTableItemChanged(QTableWidgetItem *item)
{
    if (!item) return;
    const int col = item->column();
    const int budget = _budget();

    // Recount checked items in this column (with signal blocked).
    QSignalBlocker b(m_table);
    int count = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *it = m_table->item(row, col);
        if (!it) continue;
        if (!(it->flags() & Qt::ItemIsEnabled) && it->checkState() != Qt::Checked) continue;
        if (it->checkState() == Qt::Checked) {
            if (count >= budget) {
                it->setCheckState(Qt::Unchecked);
            } else {
                ++count;
            }
        }
    }
    // Disable unchecked items beyond budget.
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *it = m_table->item(row, col);
        if (!it) continue;
        if (!_isCellEnabled(row, col)) continue;
        const bool checked = (it->checkState() == Qt::Checked);
        Qt::ItemFlags f = Qt::ItemIsUserCheckable | Qt::ItemIsSelectable;
        if (checked || count < budget)
            f |= Qt::ItemIsEnabled;
        it->setFlags(f);
    }

    _updateUploadButton();
}

// ---------------------------------------------------------------------------
// Public accessors
// ---------------------------------------------------------------------------

QStringList APlusUploadDialog::selectedMarketplaceIds() const
{
    QStringList out;
    for (int i = 0; i < m_countryChecks.size() && i < m_marketplaces.size(); ++i) {
        if (m_countryChecks.at(i) && m_countryChecks.at(i)->isChecked())
            out.append(m_marketplaces.at(i).second);
    }
    return out;
}

bool APlusUploadDialog::includeSizeChart() const
{
    return m_inclSizeChart && m_inclSizeChart->isChecked() && !m_sizeCharts.isEmpty();
}

bool APlusUploadDialog::includeFaq() const
{
    return m_inclFaq && m_inclFaq->isChecked() && !m_faqs.isEmpty();
}

QList<QList<APlusUploadDialog::ElementInfo>> APlusUploadDialog::selectedImagesByColor() const
{
    QList<QList<ElementInfo>> result;
    if (!m_table) return result;
    for (int col = 0; col < m_table->columnCount(); ++col) {
        QList<ElementInfo> colImages;
        for (int row = 0; row < m_table->rowCount(); ++row) {
            QTableWidgetItem *it = m_table->item(row, col);
            if (it && it->checkState() == Qt::Checked
                && row >= 0 && row < m_images.size())
                colImages.append(m_images.at(row));
        }
        if (!colImages.isEmpty())
            result.append(colImages);
    }
    return result;
}

bool APlusUploadDialog::shouldSubmitForApproval() const
{
    return m_submitCheck && m_submitCheck->isChecked();
}
