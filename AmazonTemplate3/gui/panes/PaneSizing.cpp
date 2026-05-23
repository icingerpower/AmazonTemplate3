// GCC 13 ICE workaround: same pragma as AmazonCatalogApi.cpp — needed because
// _uploadSizeChart is a coroutine with non-trivially-destructible frame locals.
#pragma GCC optimize("O1")
#include "PaneSizing.h"
#include "ui_PaneSizing.h"
#include "MiddleTruncateDelegate.h"
#include "SizeTableGenerator.h"
#include "SettingsTable.h"
#include "apis/AmazonCatalogApi.h"
#include "apis/TreeSizingAsins.h"
#include "sizecategories/AbstractSizeCategory.h"

#include <QInputDialog>
#include <QFileDialog>
#include <QStandardPaths>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSpacerItem>
#include <QPixmap>
#include <QRadioButton>
#include <QStandardItem>
#include <QListWidget>
#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QTimer>
#include <QTextEdit>
#include <QGuiApplication>
#include <QClipboard>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"

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
    for (const auto *cat : SizeTableGenerator::allCategories())
        ui->comboBoxSizeType->addItem(cat->displayName());

    connect(ui->comboBoxSizeType,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::onSizeTypeChanged);
    connect(ui->comboBoxSizeFrom,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::updateButtonStates);
    connect(ui->comboBoxSizeTo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::updateButtonStates);
    connect(ui->comboBoxLetterSizeFrom,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::updateButtonStates);
    connect(ui->comboBoxLetterSizeTo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::updateButtonStates);
    connect(ui->buttoGenSizeTables, &QPushButton::clicked,
            this, &PaneSizing::onGenSizeTablesClicked);
    connect(ui->buttonMakeEditable, &QPushButton::toggled,
            this, &PaneSizing::onMakeEditableToggled);
    connect(ui->buttonUploadSizeTable, &QPushButton::clicked,
            this, &PaneSizing::onUploadSizeTableClicked);

    connect(ui->radioButton,   &QRadioButton::toggled, this, &PaneSizing::onSizeModeChanged);
    connect(ui->radioButton_2, &QRadioButton::toggled, this, &PaneSizing::onSizeModeChanged);
    connect(ui->radioButton_3, &QRadioButton::toggled, this, &PaneSizing::onSizeModeChanged);

    connect(ui->listWidgetSizeGroups, &QListWidget::currentRowChanged,
            this, &PaneSizing::onGroupImageSelected);

    auto makePromptSaver = [this](QTextEdit* editor, const QString& key) {
        connect(editor, &QTextEdit::textChanged, this, [this, editor, key]() {
            QTimer::singleShot(2000, this, [this, editor, key]() {
                auto s = WorkingDirectoryManager::instance()->settings();
                const QString text = editor->toPlainText();
                if (text.isEmpty())
                    s->remove(key);
                else
                    s->setValue(key, text);
            });
        });
    };
    makePromptSaver(ui->textEditPrompt_01, QStringLiteral("aplusPromptOneColor"));
    makePromptSaver(ui->textEditPrompt_02, QStringLiteral("aplusPromptMultipleColors"));

    // Load saved prompts — working directory is already set by DialogOpenConfig before
    // MainWindow (and this widget) is constructed, so settings() is valid here.
    {
        auto s = WorkingDirectoryManager::instance()->settings();
        ui->textEditPrompt_01->blockSignals(true);
        ui->textEditPrompt_01->setPlainText(s->value(QStringLiteral("aplusPromptOneColor")).toString());
        ui->textEditPrompt_01->blockSignals(false);
        ui->textEditPrompt_02->blockSignals(true);
        ui->textEditPrompt_02->setPlainText(s->value(QStringLiteral("aplusPromptMultipleColors")).toString());
        ui->textEditPrompt_02->blockSignals(false);
    }

    connect(ui->buttonCopyPrompt, &QPushButton::clicked, this, [this]() {
        QTextEdit *editor = (ui->tabWidgetPrompt_01->currentIndex() == 0)
                          ? ui->textEditPrompt_01
                          : ui->textEditPrompt_02;
        QGuiApplication::clipboard()->setText(editor->toPlainText());
    });

    ui->treeViewAsins->setItemDelegateForColumn(
        TreeSizingAsins::Title, new MiddleTruncateDelegate(this));

    _rebuildMeasurementForm();
    onSizeModeChanged();
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
    const bool useNumbers = ui->radioButton->isChecked();
    const bool useLetters = ui->radioButton_2->isChecked();
    const bool fromOk = useNumbers ? ui->comboBoxSizeFrom->currentIndex() >= 0
                      : useLetters ? ui->comboBoxLetterSizeFrom->currentIndex() >= 0
                      :              ui->comboBoxHeightFrom->currentIndex() >= 0;
    const bool toOk   = useNumbers ? ui->comboBoxSizeTo->currentIndex() >= 0
                      : useLetters ? ui->comboBoxLetterSizeTo->currentIndex() >= 0
                      :              ui->comboBoxHeightTo->currentIndex() >= 0;

    ui->comboBoxSizeType->setEnabled(hasAsins);
    ui->comboBoxSizeFrom->setEnabled(hasAsins && typeOk && useNumbers);
    ui->comboBoxSizeTo->setEnabled(hasAsins && typeOk && useNumbers);
    ui->buttoGenSizeTables->setEnabled(hasAsins && typeOk && fromOk && toOk);

    ui->toolBoxSizing->setEnabled(m_generatedSuccessfully);
    ui->buttonMakeEditable->setEnabled(m_generatedSuccessfully);
    ui->buttonUploadSizeTable->setEnabled(m_generatedSuccessfully && hasAsins);
}

void PaneSizing::onSizeTypeChanged(int index)
{
    Q_UNUSED(index)
    _populateSizeRangeCombos();
    _rebuildMeasurementForm();
    updateButtonStates();
}

const AbstractSizeCategory* PaneSizing::_currentCategory() const
{
    const int idx = ui->comboBoxSizeType->currentIndex();
    const auto cats = SizeTableGenerator::allCategories();
    if (idx <= 0 || idx - 1 >= cats.size()) return nullptr;
    return cats[idx - 1];
}

void PaneSizing::_populateSizeRangeCombos()
{
    ui->comboBoxSizeFrom->clear();
    ui->comboBoxSizeTo->clear();

    const auto *cat = _currentCategory();
    if (!cat)
        return;

    const QStringList keys = cat->referenceKeys();
    ui->comboBoxSizeFrom->addItems(keys);
    ui->comboBoxSizeTo->addItems(keys);

    ui->comboBoxSizeFrom->setCurrentIndex(-1);
    ui->comboBoxSizeTo->setCurrentIndex(-1);

    _tryGuessSizeRange();

    const bool isHeightBased = cat && cat->referenceKey() == QStringLiteral("HEIGHT");
    const bool hasLetters    = cat && !cat->letterSizes().isEmpty();

    ui->radioButton->setEnabled(!isHeightBased);
    ui->radioButton_2->setEnabled(hasLetters);
    ui->radioButton_3->setEnabled(isHeightBased);

    ui->comboBoxLetterSizeFrom->clear();
    ui->comboBoxLetterSizeTo->clear();
    if (hasLetters) {
        ui->comboBoxLetterSizeFrom->addItems(cat->letterSizes());
        ui->comboBoxLetterSizeTo->addItems(cat->letterSizes());
        ui->comboBoxLetterSizeFrom->setCurrentIndex(-1);
        ui->comboBoxLetterSizeTo->setCurrentIndex(-1);
    }

    ui->comboBoxHeightFrom->clear();
    ui->comboBoxHeightTo->clear();
    if (isHeightBased) {
        ui->comboBoxHeightFrom->addItems(cat->referenceKeys());
        ui->comboBoxHeightTo->addItems(cat->referenceKeys());
        ui->comboBoxHeightFrom->setCurrentIndex(-1);
        ui->comboBoxHeightTo->setCurrentIndex(-1);
        ui->radioButton_3->setChecked(true);
    } else if (ui->radioButton_3->isChecked()) {
        ui->radioButton->setChecked(true);
    }
    if (!hasLetters && ui->radioButton_2->isChecked())
        ui->radioButton->setChecked(true);
    onSizeModeChanged();
}

void PaneSizing::_tryGuessSizeRange()
{
    if (!m_treeModel)
        return;
    const auto *cat = _currentCategory();
    if (!cat)
        return;

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

    const auto [minKey, maxKey] = cat->guessRange(rawSizes);
    if (!minKey.isEmpty()) {
        ui->comboBoxSizeFrom->setCurrentText(minKey);
        ui->comboBoxSizeTo->setCurrentText(maxKey.isEmpty() ? minKey : maxKey);
    }
}

void PaneSizing::_rebuildMeasurementForm()
{
    m_measurementWidgets.clear();

    QLayout *oldLayout = ui->widgetMeasurementForm->layout();
    if (oldLayout) {
        QLayoutItem *item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
    }

    const auto *cat = _currentCategory();
    QList<MeasurementField> inputFields;
    if (cat) {
        for (const auto &f : cat->measurementFields())
            if (f.derivedKey.isEmpty()) inputFields << f;
    }

    ui->widgetMeasurementForm->setVisible(!inputFields.isEmpty());
    if (inputFields.isEmpty()) return;

    QHBoxLayout *layout = qobject_cast<QHBoxLayout*>(ui->widgetMeasurementForm->layout());
    if (!layout) {
        layout = new QHBoxLayout(ui->widgetMeasurementForm);
        layout->setContentsMargins(0, 4, 0, 4);
    }

    for (const auto &field : inputFields) {
        layout->addWidget(new QLabel(field.label + ":"));

        auto *refSpin = new QDoubleSpinBox;
        refSpin->setRange(0, 500);
        refSpin->setDecimals(1);
        refSpin->setSingleStep(field.defaultStep);
        refSpin->setValue(field.defaultStep * 10);
        refSpin->setPrefix(tr("ref "));
        layout->addWidget(refSpin);

        layout->addWidget(new QLabel(tr("step")));

        auto *stepSpin = new QDoubleSpinBox;
        stepSpin->setRange(0, 50);
        stepSpin->setDecimals(1);
        stepSpin->setSingleStep(0.5);
        stepSpin->setValue(field.defaultStep);
        layout->addWidget(stepSpin);

        layout->addWidget(new QLabel(tr("range")));

        auto *rangeSpin = new QDoubleSpinBox;
        rangeSpin->setRange(0, 50);
        rangeSpin->setDecimals(1);
        rangeSpin->setSingleStep(0.5);
        rangeSpin->setValue(0.0);
        layout->addWidget(rangeSpin);

        layout->addSpacing(16);
        m_measurementWidgets.append({field.id, refSpin, stepSpin, rangeSpin});
    }
    layout->addStretch();
}

void PaneSizing::onSizeModeChanged()
{
    const bool useNumbers = ui->radioButton->isChecked();
    const bool useLetters = ui->radioButton_2->isChecked();
    const bool useHeight  = ui->radioButton_3->isChecked();
    ui->comboBoxSizeFrom->setEnabled(useNumbers);
    ui->comboBoxSizeTo->setEnabled(useNumbers);
    ui->comboBoxLetterSizeFrom->setEnabled(useLetters);
    ui->comboBoxLetterSizeTo->setEnabled(useLetters);
    ui->comboBoxHeightFrom->setEnabled(useHeight);
    ui->comboBoxHeightTo->setEnabled(useHeight);
    updateButtonStates();
}

void PaneSizing::onGenSizeTablesClicked()
{
    m_generatedSuccessfully = false;

    const auto *cat = _currentCategory();
    if (!cat) {
        updateButtonStates();
        return;
    }
    const bool useLetters = ui->radioButton_2->isChecked();
    QString keyFrom, keyTo;
    QStringList letterHeaders;

    const bool useHeight  = ui->radioButton_3->isChecked();
    if (useLetters) {
        const QString lFrom = ui->comboBoxLetterSizeFrom->currentText();
        const QString lTo   = ui->comboBoxLetterSizeTo->currentText();
        keyFrom = cat->letterToKey(lFrom);
        keyTo   = cat->letterToKey(lTo);
        const QStringList allLetters = cat->letterSizes();
        int fi = allLetters.indexOf(lFrom);
        int ti = allLetters.indexOf(lTo);
        if (fi > ti) std::swap(fi, ti);
        letterHeaders = allLetters.mid(fi, ti - fi + 1);
    } else if (useHeight) {
        keyFrom = ui->comboBoxHeightFrom->currentText();
        keyTo   = ui->comboBoxHeightTo->currentText();
    } else {
        keyFrom = ui->comboBoxSizeFrom->currentText();
        keyTo   = ui->comboBoxSizeTo->currentText();
    }

    QMap<QString, MeasurementInput> measurements;
    for (const auto &w : m_measurementWidgets)
        measurements[w.fieldId] = {w.refSpinBox->value(), w.stepSpinBox->value(), w.rangeSpinBox->value()};

    try {
        ui->tableViewSizing->setModel(nullptr);
        delete m_sizeTableModel;
        m_sizeTableModel = cat->buildTable(keyFrom, keyTo, measurements, this);
        if (useLetters && !letterHeaders.isEmpty()) {
            // Drop country-group rows — letter mode shows measurements only
            for (int i = 0; i < cat->countryGroups().size(); ++i)
                m_sizeTableModel->removeRow(0);

            // Prepend a "Size" header row with the letter labels
            m_sizeTableModel->insertRow(0);
            auto *labelItem = new QStandardItem(tr("Size"));
            labelItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_sizeTableModel->setItem(0, 0, labelItem);
            for (int i = 0; i < letterHeaders.size(); ++i) {
                auto *it = new QStandardItem(letterHeaders[i]);
                it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                m_sizeTableModel->setItem(0, i + 1, it);
                m_sizeTableModel->setHorizontalHeaderItem(i + 1, new QStandardItem(letterHeaders[i]));
            }
        }
        ui->tableViewSizing->setModel(m_sizeTableModel);
        ui->tableViewSizing->resizeColumnsToContents();
        ui->tableViewSizing->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->buttonMakeEditable->setChecked(false);

        const QImage img = cat->renderImage(m_sizeTableModel);
        ui->labelGeneratedImage->setPixmap(QPixmap::fromImage(img));
        ui->labelGeneratedImage->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        ui->listWidgetSizeGroups->clear();
        m_groupImages.clear();
        const auto groupImages = cat->renderGroupImages(keyFrom, keyTo, measurements, letterHeaders);
        for (const auto &[label, gimg] : groupImages) {
            ui->listWidgetSizeGroups->addItem(label);
            m_groupImages << gimg;
        }
        if (!m_groupImages.isEmpty())
            ui->listWidgetSizeGroups->setCurrentRow(0);

        m_generatedSuccessfully = true;
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Generation failed"), QString::fromUtf8(e.what()));
    }

    updateButtonStates();
}

void PaneSizing::onMakeEditableToggled(bool checked)
{
    ui->tableViewSizing->setEditTriggers(checked
        ? (QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed)
        : QAbstractItemView::NoEditTriggers);
}

void PaneSizing::onGroupImageSelected(int row)
{
    if (row < 0 || row >= m_groupImages.size()) {
        ui->labelSelectedImage->clear();
        return;
    }
    const QPixmap pm = QPixmap::fromImage(m_groupImages.at(row));
    const int maxW = ui->widgetGroupImages->width() - 4;
    ui->labelSelectedImage->setPixmap(
        (maxW > 0 && pm.width() > maxW)
            ? pm.scaledToWidth(maxW, Qt::SmoothTransformation)
            : pm);
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

void PaneSizing::onUploadSizeTableClicked()
{
    if (!m_generatedSuccessfully || !m_sizeTableModel)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Upload Size Chart"));
    auto *layout = new QVBoxLayout(&dlg);

    auto *mpLabel = new QLabel(tr("Marketplace:"), &dlg);
    auto *mpCombo = new QComboBox(&dlg);
    mpCombo->addItem(QStringLiteral("FR  (A13V1IB3VIYZZH)"), QStringLiteral("A13V1IB3VIYZZH"));
    mpCombo->addItem(QStringLiteral("DE  (A1PA6795UKMFR9)"), QStringLiteral("A1PA6795UKMFR9"));
    mpCombo->addItem(QStringLiteral("IT  (APJ6JRA9NG5V4)"),  QStringLiteral("APJ6JRA9NG5V4"));
    mpCombo->addItem(QStringLiteral("ES  (A1RKKUPIHCS9HS)"), QStringLiteral("A1RKKUPIHCS9HS"));
    mpCombo->addItem(QStringLiteral("UK  (A1F83G8C2ARO7P)"), QStringLiteral("A1F83G8C2ARO7P"));
    mpCombo->addItem(QStringLiteral("NL  (A1805IZSGTT6HS)"), QStringLiteral("A1805IZSGTT6HS"));
    mpCombo->addItem(QStringLiteral("SE  (A2NODRKZP88ZB9)"), QStringLiteral("A2NODRKZP88ZB9"));
    mpCombo->addItem(QStringLiteral("PL  (A1C3SOZRARQ6R3)"), QStringLiteral("A1C3SOZRARQ6R3"));
    mpCombo->addItem(QStringLiteral("BE  (AMEN7PMS3EDWL)"),  QStringLiteral("AMEN7PMS3EDWL"));
    mpCombo->addItem(QStringLiteral("US  (ATVPDKIKX0DER)"),  QStringLiteral("ATVPDKIKX0DER"));
    mpCombo->addItem(QStringLiteral("CA  (A2EUQ1WTGCTBG2)"), QStringLiteral("A2EUQ1WTGCTBG2"));
    mpCombo->addItem(QStringLiteral("JP  (A1VC38T7YXB528)"), QStringLiteral("A1VC38T7YXB528"));

    auto *ptLabel = new QLabel(tr("Product type (e.g. SHIRT, SHOES, PANTS):"), &dlg);
    auto *ptEdit  = new QLineEdit(&dlg);
    ptEdit->setPlaceholderText(QStringLiteral("SHIRT"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    layout->addWidget(mpLabel);
    layout->addWidget(mpCombo);
    layout->addWidget(ptLabel);
    layout->addWidget(ptEdit);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString productType = ptEdit->text().trimmed().toUpper();
    if (productType.isEmpty())
        return;

    _uploadSizeChart(mpCombo->currentData().toString(), productType);
}

QCoro::Task<void> PaneSizing::_uploadSizeChart(QString marketplaceId, QString productType)
{
    // Build header row: blank label cell + size column labels from horizontal header
    QStringList headerCells;
    headerCells << QString{};
    for (int c = 1; c < m_sizeTableModel->columnCount(); ++c) {
        auto *hItem = m_sizeTableModel->horizontalHeaderItem(c);
        headerCells << (hItem ? hItem->text() : QString::number(c));
    }

    // Build data rows from the model
    QList<QStringList> dataRows;
    for (int r = 0; r < m_sizeTableModel->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < m_sizeTableModel->columnCount(); ++c) {
            auto *it = m_sizeTableModel->item(r, c);
            row << (it ? it->text() : QString{});
        }
        dataRows << row;
    }

    // Collect all child SKUs from the tree model
    QStringList skus;
    if (m_treeModel) {
        for (int i = 0; i < m_treeModel->rowCount(); ++i) {
            const QModelIndex parentIdx = m_treeModel->index(i, 0);
            for (int j = 0; j < m_treeModel->rowCount(parentIdx); ++j) {
                const QModelIndex skuIdx =
                    m_treeModel->index(j, TreeSizingAsins::SKU, parentIdx);
                const QString sku =
                    m_treeModel->data(skuIdx, Qt::DisplayRole).toString().trimmed();
                if (!sku.isEmpty())
                    skus << sku;
            }
        }
    }

    if (skus.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"), tr("No SKUs found in the tree."));
        co_return;
    }

    int successCount = 0;
    QStringList errors;
    for (const QString& sku : skus) {
        bool ok = false;
        co_await m_api->patchListingSizeChart(
            marketplaceId, sku, productType, headerCells, dataRows, &ok);
        if (ok)
            ++successCount;
        else
            errors << QStringLiteral("%1: %2").arg(sku, m_api->lastError());
        m_api->clearLastError();
    }

    if (errors.isEmpty()) {
        QMessageBox::information(this, tr("Upload"),
            tr("Size chart uploaded to %1 listing(s).").arg(successCount));
    } else {
        QMessageBox::warning(this, tr("Upload"),
            tr("Uploaded to %1 of %2 listing(s).\n\nErrors:\n%3")
                .arg(successCount).arg(skus.size()).arg(errors.join('\n')));
    }
    co_return;
}
