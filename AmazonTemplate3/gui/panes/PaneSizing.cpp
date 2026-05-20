#include "PaneSizing.h"
#include "ui_PaneSizing.h"
#include "MiddleTruncateDelegate.h"
#include "SizeTableGenerator.h"
#include "SettingsTable.h"
#include "apis/AmazonCatalogApi.h"
#include "apis/TreeSizingAsins.h"

#include <QInputDialog>
#include <QFileDialog>
#include <QStandardPaths>
#include <QStandardItemModel>
#include <QMessageBox>

PaneSizing::PaneSizing(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSizing)
{
    ui->setupUi(this);

    auto *s = SettingsTable::instance();
    m_api = std::make_unique<AmazonCatalogApi>(
        s->value(SettingsTable::KEY_LWA_CLIENT_ID),
        s->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        s->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_EU_SELLER_ID),
        s->value(SettingsTable::KEY_NA_SELLER_ID),
        s->value(SettingsTable::KEY_JP_SELLER_ID));

    connect(ui->buttonAddFromASIN,     &QPushButton::clicked,
            this, &PaneSizing::onAddFromAsinClicked);
    connect(ui->buttonAddFromTemplate, &QPushButton::clicked,
            this, &PaneSizing::onAddFromTemplateClicked);

    ui->comboBoxSizeType->addItem(tr("Select type..."), -1);
    for (auto cat : SizeTableGenerator::allCategories())
        ui->comboBoxSizeType->addItem(
            SizeTableGenerator::displayName(cat), static_cast<int>(cat));

    connect(ui->comboBoxSizeType,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::onSizeTypeChanged);
    connect(ui->comboBoxSizeFrom,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::updateButtonStates);
    connect(ui->comboBoxSizeTo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::updateButtonStates);
    connect(ui->buttoGenSizeTables, &QPushButton::clicked,
            this, &PaneSizing::onGenSizeTablesClicked);

    ui->treeViewAsins->setItemDelegateForColumn(
        TreeSizingAsins::Title, new MiddleTruncateDelegate(this));

    updateButtonStates();
}

PaneSizing::~PaneSizing()
{
    delete ui;
}

void PaneSizing::setWorkingDir(const QDir &workingDir)
{
    _ensureModel(workingDir);
}

void PaneSizing::_refreshApi()
{
    auto *s = SettingsTable::instance();
    m_api = std::make_unique<AmazonCatalogApi>(
        s->value(SettingsTable::KEY_LWA_CLIENT_ID),
        s->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        s->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_EU_SELLER_ID),
        s->value(SettingsTable::KEY_NA_SELLER_ID),
        s->value(SettingsTable::KEY_JP_SELLER_ID));
    if (m_treeModel)
        m_treeModel->setApiClient(m_api.get());
}

void PaneSizing::_ensureModel(const QDir &dir)
{
    if (m_treeModel)
        return;
    m_treeModel = std::make_unique<TreeSizingAsins>(dir);
    m_treeModel->setApiClient(m_api.get());
    ui->treeViewAsins->setModel(m_treeModel.get());
    ui->treeViewAsins->expandAll();

    connect(m_treeModel.get(), &QAbstractItemModel::modelReset,
            this, [this]() {
                ui->treeViewAsins->expandAll();
                updateButtonStates();
                _tryGuessSizeRange();
            });

    connect(m_treeModel.get(), &TreeSizingAsins::loadError,
            this, [this](const QString& message) {
                QMessageBox::warning(this, tr("Amazon API error"), message);
            });
}

void PaneSizing::updateButtonStates()
{
    const bool hasAsins   = m_treeModel && m_treeModel->rowCount() > 0;
    const bool typeOk     = ui->comboBoxSizeType->currentIndex() > 0;
    const bool fromOk     = ui->comboBoxSizeFrom->currentIndex() >= 0;
    const bool toOk       = ui->comboBoxSizeTo->currentIndex() >= 0;

    ui->comboBoxSizeType->setEnabled(hasAsins);
    ui->comboBoxSizeFrom->setEnabled(hasAsins && typeOk);
    ui->comboBoxSizeTo->setEnabled(hasAsins && typeOk);
    ui->buttoGenSizeTables->setEnabled(hasAsins && typeOk && fromOk && toOk);

    ui->toolBoxSizing->setEnabled(m_generatedSuccessfully);
}

void PaneSizing::onSizeTypeChanged(int index)
{
    _populateSizeRangeCombos();
    updateButtonStates();
}

void PaneSizing::_populateSizeRangeCombos()
{
    ui->comboBoxSizeFrom->clear();
    ui->comboBoxSizeTo->clear();

    const int index = ui->comboBoxSizeType->currentIndex();
    if (index <= 0)
        return;

    const auto cat = static_cast<SizeTableGenerator::Category>(
        ui->comboBoxSizeType->currentData().toInt());
    const QStringList keys = SizeTableGenerator::referenceKeys(cat);
    ui->comboBoxSizeFrom->addItems(keys);
    ui->comboBoxSizeTo->addItems(keys);

    ui->comboBoxSizeFrom->setCurrentIndex(-1);
    ui->comboBoxSizeTo->setCurrentIndex(-1);

    _tryGuessSizeRange();
}

void PaneSizing::_tryGuessSizeRange()
{
    if (!m_treeModel || ui->comboBoxSizeType->currentIndex() <= 0)
        return;

    const auto cat = static_cast<SizeTableGenerator::Category>(
        ui->comboBoxSizeType->currentData().toInt());

    QStringList rawSizes;
    for (int i = 0; i < m_treeModel->rowCount(); ++i) {
        const QModelIndex parentIdx = m_treeModel->index(i, 0);
        for (int j = 0; j < m_treeModel->rowCount(parentIdx); ++j) {
            const QModelIndex childIdx =
                m_treeModel->index(j, TreeSizingAsins::Size, parentIdx);
            const QString s =
                m_treeModel->data(childIdx, Qt::DisplayRole).toString().trimmed();
            if (!s.isEmpty())
                rawSizes << s;
        }
    }

    const auto [minKey, maxKey] = SizeTableGenerator::guessRange(cat, rawSizes);
    if (!minKey.isEmpty()) {
        ui->comboBoxSizeFrom->setCurrentText(minKey);
        ui->comboBoxSizeTo->setCurrentText(maxKey.isEmpty() ? minKey : maxKey);
    }
}

void PaneSizing::onGenSizeTablesClicked()
{
    m_generatedSuccessfully = false;

    const auto cat = static_cast<SizeTableGenerator::Category>(
        ui->comboBoxSizeType->currentData().toInt());
    const QString keyFrom = ui->comboBoxSizeFrom->currentText();
    const QString keyTo   = ui->comboBoxSizeTo->currentText();

    try {
        ui->tableViewSizing->setModel(nullptr);
        delete m_sizeTableModel;
        m_sizeTableModel = SizeTableGenerator::build(cat, keyFrom, keyTo, this);
        ui->tableViewSizing->setModel(m_sizeTableModel);
        ui->tableViewSizing->resizeColumnsToContents();
        m_generatedSuccessfully = true;
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Generation failed"), QString::fromUtf8(e.what()));
    }

    updateButtonStates();
}

void PaneSizing::onAddFromAsinClicked()
{
    const QDir defaultDir(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    _ensureModel(defaultDir);

    bool ok = false;
    const QString asin = QInputDialog::getText(
        this, tr("Add from ASIN"), tr("ASIN:"), QLineEdit::Normal, {}, &ok);
    if (!ok || asin.trimmed().isEmpty())
        return;

    _refreshApi();
    m_treeModel->load(asin.trimmed());
}

void PaneSizing::onAddFromTemplateClicked()
{
    const QDir defaultDir(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    _ensureModel(defaultDir);

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Amazon template"), {}, tr("Excel (*.xlsx)"));
    if (path.isEmpty())
        return;

    _refreshApi();
    m_treeModel->load(path);
}
