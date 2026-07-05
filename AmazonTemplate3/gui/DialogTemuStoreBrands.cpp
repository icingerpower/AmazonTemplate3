#include "DialogTemuStoreBrands.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include "../TemuStoreModel.h"
#include "../TreeTemuStoreBrands.h"
#include "../SettingsTable.h"
#include "../../common/workingdirectory/WorkingDirectoryManager.h"

namespace {
// Combo editor for the Manufacturer / GSPR columns, fed by the entity names
// the model holds for the row's store (TreeTemuStoreBrands::RoleChoices).
class EntityComboDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        const QStringList choices = index.data(TreeTemuStoreBrands::RoleChoices).toStringList();
        auto *combo = new QComboBox(parent);
        combo->addItem(QString{}); // allow clearing the assignment
        combo->addItems(choices);
        return combo;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        combo->setCurrentText(index.data(Qt::EditRole).toString());
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *combo = static_cast<QComboBox *>(editor);
        model->setData(index, combo->currentText());
    }
};
} // namespace

DialogTemuStoreBrands::DialogTemuStoreBrands(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Temu stores – brands"));
    resize(820, 520);

    auto *storeModel = new TemuStoreModel(this);
    m_model = new TreeTemuStoreBrands(storeModel->stores(), this);
    connect(m_model, &TreeTemuStoreBrands::errorOccurred,
            this, [this](const QString &message) {
                QMessageBox::warning(this, tr("Invalid value"), message);
            });

    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->expandAll();
    m_tree->setUniformRowHeights(true);
    // Single click on an editable cell opens its combo directly.
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked
                            | QAbstractItemView::SelectedClicked
                            | QAbstractItemView::EditKeyPressed);
    m_tree->header()->setSectionResizeMode(TreeTemuStoreBrands::COL_BRAND, QHeaderView::Stretch);
    m_tree->header()->resizeSection(TreeTemuStoreBrands::COL_MANUFACTURER, 240);
    m_tree->header()->resizeSection(TreeTemuStoreBrands::COL_GSPR, 240);
    // All three columns are combo-driven: brands come from the ASIN cache,
    // manufacturer / GSPR entities from the store's Temu account.
    auto *entityDelegate = new EntityComboDelegate(m_tree);
    m_tree->setItemDelegateForColumn(TreeTemuStoreBrands::COL_BRAND, entityDelegate);
    m_tree->setItemDelegateForColumn(TreeTemuStoreBrands::COL_MANUFACTURER, entityDelegate);
    m_tree->setItemDelegateForColumn(TreeTemuStoreBrands::COL_GSPR, entityDelegate);

    auto *buttonAdd    = new QPushButton(tr("Add brand"), this);
    auto *buttonRemove = new QPushButton(tr("Remove brand"), this);
    connect(buttonAdd,    &QPushButton::clicked, this, &DialogTemuStoreBrands::_addBrand);
    connect(buttonRemove, &QPushButton::clicked, this, &DialogTemuStoreBrands::_removeBrand);

    m_status = new QLabel(tr("Loading manufacturers / GSPR representatives from Temu…"), this);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *topLayout = new QHBoxLayout;
    topLayout->addWidget(buttonAdd);
    topLayout->addWidget(buttonRemove);
    topLayout->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topLayout);
    layout->addWidget(m_tree);
    layout->addWidget(m_status);
    layout->addWidget(buttonBox);

    _fetchEntityChoices();
}

QCoro::Task<void> DialogTemuStoreBrands::_fetchEntityChoices()
{
    QPointer<DialogTemuStoreBrands> self(this);

    auto settings = WorkingDirectoryManager::instance()->settings();
    const QString appKey    = settings->value(SettingsTable::KEY_TEMU_APP_KEY).toString();
    const QString appSecret = settings->value(SettingsTable::KEY_TEMU_APP_SECRET).toString();

    TemuStoreModel storeModel;
    const QList<TemuStore> stores = storeModel.stores();

    if (appKey.isEmpty() || appSecret.isEmpty()) {
        m_status->setText(tr("Temu app key/secret not configured in Settings — "
                             "manufacturer / GSPR choices unavailable."));
        co_return;
    }

    QStringList failures;
    for (int row = 0; row < stores.size(); ++row) {
        const TemuStore &store = stores[row];
        if (store.token.isEmpty())
            continue;

        auto *api = new TemuInventoryApi(appKey, appSecret, store.token,
                                         store.proxyHost, store.proxyPort,
                                         store.proxyUser, store.proxyPassword);
        QList<TemuInventoryApi::RepEntity> manufacturers, gsprReps;
        co_await api->fetchComplianceEntities(TemuInventoryApi::COMPLIANCE_TYPE_MANUFACTURER,
                                              &manufacturers);
        const QString manufacturerError = api->lastError();
        co_await api->fetchComplianceEntities(TemuInventoryApi::COMPLIANCE_TYPE_GSPR_REP,
                                              &gsprReps);
        const QString gsprError = api->lastError();
        api->deleteLater();

        if (!self)
            co_return;
        if (!manufacturerError.isEmpty() || !gsprError.isEmpty()) {
            failures << QStringLiteral("%1 – %2: %3").arg(
                store.country, store.label,
                manufacturerError.isEmpty() ? gsprError : manufacturerError);
            continue;
        }
        m_model->setEntityChoices(row, manufacturers, gsprReps);
    }

    if (failures.isEmpty())
        m_status->setText(tr("Manufacturers / GSPR representatives loaded from Temu."));
    else
        m_status->setText(tr("Some stores failed to load: %1").arg(failures.join(QStringLiteral(" | "))));
}

void DialogTemuStoreBrands::_addBrand()
{
    const int storeRow = m_model->storeRowOf(m_tree->currentIndex());
    if (storeRow < 0) {
        QMessageBox::information(this, tr("Add brand"),
                                 tr("Select a store (or one of its brands) first."));
        return;
    }

    // Only brands cached from previously loaded ASINs can be assigned.
    const QStringList choices = m_model->availableBrandsForStore(storeRow);
    if (choices.isEmpty()) {
        QMessageBox::information(this, tr("Add brand"),
            TreeTemuStoreBrands::knownBrands().isEmpty()
                ? tr("No brand cached yet — load an ASIN in the Sizing tab first; "
                     "its brand is cached automatically.")
                : tr("All known brands are already assigned to a store of this country."));
        return;
    }

    bool ok = false;
    const QString brand = QInputDialog::getItem(
        this, tr("Add brand"), tr("Brand (cached from loaded ASINs):"),
        choices, 0, /*editable=*/false, &ok);
    if (!ok || brand.isEmpty())
        return;

    const QModelIndex idx = m_model->addBrand(storeRow, brand);
    if (idx.isValid())
        m_tree->setCurrentIndex(idx);
}

void DialogTemuStoreBrands::_removeBrand()
{
    const QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid() || m_model->isStoreIndex(idx)) {
        QMessageBox::information(this, tr("Remove brand"), tr("Select a brand line first."));
        return;
    }
    m_model->removeBrand(idx);
}
