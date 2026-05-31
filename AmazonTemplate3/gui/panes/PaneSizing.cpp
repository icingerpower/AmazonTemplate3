// GCC 13 ICE workaround: same pragma as AmazonCatalogApi.cpp — needed because
// some coroutines here have non-trivially-destructible frame locals.
#pragma GCC optimize("O1")
#include "PaneSizing.h"
#include "ui_PaneSizing.h"
#include "MiddleTruncateDelegate.h"
#include "SizeRangeWidget.h"
#include "SizeTableGenerator.h"
#include "SettingsTable.h"
#include "apis/AmazonCatalogApi.h"
#include "apis/AmazonAplusApi.h"
#include "apis/TreeSizingAsins.h"
#include "aplus/APlusUploadDialog.h"
#include "sizecategories/AbstractSizeCategory.h"
#include "sizecategories/SizingTableTemplateModel.h"
#include <QTableView>
#include <QHeaderView>
#include <QUuid>

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
#include <QPainter>
#include <QPixmap>
#include <QRadioButton>
#include <QStandardItem>
#include <QListWidget>
#include <QListView>
#include <QStringListModel>
#include <QTableWidget>
#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QTimer>
#include <QTextEdit>
#include <QProgressBar>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QClipboard>

#include "../../common/workingdirectory/WorkingDirectoryManager.h"
#include "AbstractCli.h"

#include <QDesktopServices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QButtonGroup>
#include <QTreeView>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QDateTime>
#include <QDir>
#include <QBuffer>
#include <QSet>
#include <xlsxdocument.h>

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
        s->value(SettingsTable::KEY_JP_SELLER_ID),
        s->value(SettingsTable::KEY_IMGBB_API_KEY));
    m_aplusApi = std::make_unique<AmazonAplusApi>(
        s->value(SettingsTable::KEY_LWA_CLIENT_ID),
        s->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        s->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN));

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
    connect(ui->sizeRangeMain,  &SizeRangeWidget::changed, this, &PaneSizing::updateButtonStates);
    connect(ui->sizeRangeBrand, &SizeRangeWidget::changed, this, &PaneSizing::updateButtonStates);
    connect(ui->buttoGenSizeTables, &QPushButton::clicked,
            this, &PaneSizing::onGenSizeTablesClicked);
    connect(ui->buttonMakeEditable, &QPushButton::toggled,
            this, &PaneSizing::onMakeEditableToggled);
    connect(ui->buttonUploadImageSize, &QPushButton::clicked,
            this, &PaneSizing::onUploadSizeImageClicked);
    connect(ui->radioButtonReplaceImageAtIndex, &QRadioButton::toggled,
            ui->spinBoxImagePos, &QSpinBox::setEnabled);

    connect(ui->listWidgetSizeGroups, &QListWidget::currentRowChanged,
            this, &PaneSizing::onGroupImageSelected);

    connect(ui->listWidgetImages, &QListWidget::currentRowChanged,
            this, &PaneSizing::onVariantImageSelected);

    auto makePromptSaver = [this](QTextEdit* editor, int step) {
        connect(editor, &QTextEdit::textChanged, this, [this, editor, step]() {
            QTimer::singleShot(2000, this, [this, editor, step]() {
                APlusWorkflow *wf = _currentWorkflow();
                if (!wf) return;
                const QString key = QStringLiteral("aplus/") + wf->id()
                                  + QStringLiteral("/step") + QString::number(step);
                auto s = WorkingDirectoryManager::instance()->settings();
                const QString text = editor->toPlainText();
                if (text.isEmpty()) s->remove(key);
                else                s->setValue(key, text);
            });
        });
    };
    makePromptSaver(ui->textEditPrompt_01, 0);
    makePromptSaver(ui->textEditPrompt_02, 1);
    makePromptSaver(ui->textEditPrompt_03, 2);

    connect(ui->textEditFaqPrompt, &QTextEdit::textChanged, this, [this]() {
        QTimer::singleShot(2000, this, [this]() {
            auto s = WorkingDirectoryManager::instance()->settings();
            const QString text = ui->textEditFaqPrompt->toPlainText();
            if (text.isEmpty()) s->remove(QStringLiteral("aplusPromptFaq"));
            else                s->setValue(QStringLiteral("aplusPromptFaq"), text);
        });
    });

    connect(ui->buttonCopyPrompt, &QPushButton::clicked, this, [this]() {
        const int idx = ui->tabWidgetPrompt_01->currentIndex();
        QTextEdit *editor = (idx == 0) ? ui->textEditPrompt_01
                          : (idx == 1) ? ui->textEditPrompt_02
                          : (idx == 2) ? ui->textEditPrompt_03
                          : ui->textEditFaqPrompt;
        QGuiApplication::clipboard()->setText(editor->toPlainText());
    });

    m_imageNam = new QNetworkAccessManager(this);
    _initWorkflowCombo();

    // --- A+ content wiring ---
    connect(ui->buttonAplusAddImageSlot, &QPushButton::clicked,
            this, &PaneSizing::onAplusAddImageSlot);
    connect(ui->buttonAplusDeleteVersion, &QPushButton::clicked,
            this, &PaneSizing::onAplusDeleteVersion);
    connect(ui->buttonAplusUpload, &QPushButton::clicked,
            this, &PaneSizing::onAplusUploadClicked);

    // Desktop/Mobile toggle — mutually exclusive
    auto *viewGroup = new QButtonGroup(this);
    viewGroup->addButton(ui->buttonAplusDesktop);
    viewGroup->addButton(ui->buttonAplusMobile);
    viewGroup->setExclusive(true);
    ui->buttonAplusDesktop->setChecked(true);
    connect(ui->buttonAplusDesktop, &QToolButton::clicked,
            this, [this]() {
                m_aplusDesktop = true;
                _refreshAplusPreview(ui->aplusTreeView->currentIndex());
            });
    connect(ui->buttonAplusMobile, &QToolButton::clicked,
            this, [this]() {
                m_aplusDesktop = false;
                _refreshAplusPreview(ui->aplusTreeView->currentIndex());
            });

    // Ignored horizontal: label's width hint contributes 0 to widgetGroupImages's preferred
    // width, so calling setPixmap() never shifts splitter_2.
    ui->labelSizeChartDisplay->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

    ui->comboBoxAplusLanguage->setVisible(false);
    connect(ui->comboBoxAplusLanguage,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { _refreshAplusPreview(ui->aplusTreeView->currentIndex()); });

    // Tree clicks — column decides desktop vs mobile
    connect(ui->aplusTreeView, &QTreeView::clicked,
            this, &PaneSizing::onAplusTreeClicked);

    // Show the generate menu when the button is clicked (wired once here, not in _initAplusContent).
    connect(ui->buttonAplusGenerate, &QPushButton::clicked,
            this, [this]() {
                if (m_aplusMenu)
                    m_aplusMenu->exec(ui->buttonAplusGenerate->mapToGlobal(
                        QPoint(0, ui->buttonAplusGenerate->height())));
            });

    ui->buttonAplusDeleteVersion->setEnabled(false);
    ui->buttonAplusGenerate->setEnabled(false);

    connect(ui->buttonOpenSubWorkingDir, &QPushButton::clicked, this, [this]() {
        const QDir &dir = m_productWorkingDir.exists() ? m_productWorkingDir : m_workingDir;
        if (dir.exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
    });

    connect(ui->buttonOpenSizeTableFolder, &QPushButton::clicked,
            this, &PaneSizing::onOpenSizeTableFolderClicked);
    connect(ui->buttonAddSkusFromTemplate, &QPushButton::clicked,
            this, &PaneSizing::onAddSkusFromTemplateClicked);

    ui->treeViewAsins->setItemDelegateForColumn(
        TreeSizingAsins::Title, new MiddleTruncateDelegate(this));

    m_templateModel = new SizingTableTemplateModel(this);
    connect(ui->buttonSavedSizeAdd,  &QPushButton::clicked, this, &PaneSizing::onSavedSizeAddClicked);
    connect(ui->buttonSavedSizeSave, &QPushButton::clicked, this, &PaneSizing::onSavedSizeSaveClicked);
    connect(ui->buttonSavedSizeLoad, &QPushButton::clicked, this, &PaneSizing::onSavedSizeLoadClicked);
    connect(ui->buttonSavedSizeEdit, &QPushButton::clicked, this, &PaneSizing::onSavedSizeEditClicked);

    _rebuildMeasurementForm();
    updateButtonStates();
}

PaneSizing::~PaneSizing()
{
    delete ui;
}

void PaneSizing::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    ui->comboBoxCli->blockSignals(true);
    ui->comboBoxCli->clear();
    for (AbstractCli *cli : clis)
        ui->comboBoxCli->addItem(cli->getName(), QVariant::fromValue(cli));

    // Default: first CLI with canGenImages(); fall back to first available.
    int defaultIndex = 0;
    for (int i = 0; i < clis.size(); ++i) {
        if (clis[i]->canGenImages()) { defaultIndex = i; break; }
    }

    // Restore last user selection.
    const QString saved = QSettings().value(QStringLiteral("sizing/selectedCli")).toString();
    int restoredIndex = -1;
    for (int i = 0; i < clis.size(); ++i) {
        if (clis[i]->getName() == saved) { restoredIndex = i; break; }
    }
    ui->comboBoxCli->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : defaultIndex);
    ui->comboBoxCli->blockSignals(false);

    connect(ui->comboBoxCli, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index < 0 || index >= m_availableClis.size()) return;
        QSettings().setValue(QStringLiteral("sizing/selectedCli"),
                             m_availableClis[index]->getName());
    });
}

void PaneSizing::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
    _ensureModel(workingDir);
    if (m_templateModel) {
        m_templateModel->setWorkingDir(m_workingDir);
        _refreshTemplateCombo();
    }
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
        s->value(SettingsTable::KEY_JP_SELLER_ID),
        s->value(SettingsTable::KEY_IMGBB_API_KEY));
    m_aplusApi = std::make_unique<AmazonAplusApi>(
        s->value(SettingsTable::KEY_LWA_CLIENT_ID),
        s->value(SettingsTable::KEY_LWA_CLIENT_SECRET),
        s->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN),
        s->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN));
    if (m_treeModel)
        m_treeModel->setApiClient(m_api.get());
}

static QString countryCodeToLanguage(const QString &code);
static QList<PaneSizing::SizeChartTarget> buildSizeChartTargets(
    const AbstractSizeCategory *cat, QListWidget *countriesList);
static QString simplifyForDirName(const QString &s)
{
    QString result;
    for (const QChar &c : s.toLower()) {
        if (c.isLetterOrNumber())
            result += c;
        else if (!result.isEmpty() && result.back() != QLatin1Char('-'))
            result += QLatin1Char('-');
    }
    while (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result.left(60);
}

// Read unique SKU values from a listing template xlsx. Looks in the first
// 10 rows / 500 cols for a header matching seller_sku / item_sku / sku
// (case-insensitive). Data starts headerRow + 2 to skip the
// required/optional markers row. Stops at the first empty cell.
static QStringList readSkusFromXlsx(const QString &path)
{
    QStringList result;
    QXlsx::Document doc(path);
    if (!doc.load())
        return result;

    const QStringList skuHeaders = {
        QStringLiteral("seller_sku"),
        QStringLiteral("item_sku"),
        QStringLiteral("sku"),
    };

    int skuCol = -1;
    int headerRow = -1;
    for (int row = 1; row <= 10 && skuCol < 0; ++row) {
        for (int col = 1; col <= 500; ++col) {
            const QVariant v = doc.read(row, col);
            if (!v.isValid()) continue;
            const QString s = v.toString().trimmed();
            for (const QString &h : skuHeaders) {
                if (s.compare(h, Qt::CaseInsensitive) == 0) {
                    skuCol = col;
                    headerRow = row;
                    break;
                }
            }
            if (skuCol > 0) break;
        }
    }

    if (skuCol < 0)
        return result;

    const int firstDataRow = headerRow + 2; // skip required/optional markers row
    for (int row = firstDataRow; row <= 10000; ++row) {
        const QVariant v = doc.read(row, skuCol);
        if (!v.isValid()) break;
        const QString s = v.toString().trimmed();
        if (s.isEmpty()) break;
        if (!result.contains(s))
            result << s;
    }
    return result;
}

// Resolve the first marketplace ID matching a country code from the
// listWidgetCountries widget. Falls back to UK (A1F83G8C2ARO7P) if no entry
// matches a known country code.
static QString firstMarketplaceIdFromCountryList(QListWidget *listWidget)
{
    static const QHash<QString, QString> kCodeToMp = {
        {QStringLiteral("fr"), QStringLiteral("A13V1IB3VIYZZH")},
        {QStringLiteral("de"), QStringLiteral("A1PA6795UKMFR9")},
        {QStringLiteral("it"), QStringLiteral("APJ6JRA9NG5V4")},
        {QStringLiteral("es"), QStringLiteral("A1RKKUPIHCS9HS")},
        {QStringLiteral("uk"), QStringLiteral("A1F83G8C2ARO7P")},
        {QStringLiteral("nl"), QStringLiteral("A1805IZSGTT6HS")},
        {QStringLiteral("se"), QStringLiteral("A2NODRKZP88ZB9")},
        {QStringLiteral("pl"), QStringLiteral("A1C3SOZRARQ6R3")},
        {QStringLiteral("be"), QStringLiteral("AMEN7PMS3EDWL")},
        {QStringLiteral("ie"), QStringLiteral("A28R8C7NBKEWEA")},
        {QStringLiteral("tr"), QStringLiteral("A33AVAJ2PDY3EV")},
        {QStringLiteral("us"), QStringLiteral("ATVPDKIKX0DER")},
        {QStringLiteral("ca"), QStringLiteral("A2EUQ1WTGCTBG2")},
        {QStringLiteral("mx"), QStringLiteral("A1AM78C64UM0Y8")},
        {QStringLiteral("jp"), QStringLiteral("A1VC38T7YXB528")},
    };
    for (int i = 0; i < listWidget->count(); ++i) {
        const QString code = listWidget->item(i)->text()
                                 .trimmed().toLower().split(QLatin1Char(' ')).first();
        const QString mp = kCodeToMp.value(code);
        if (!mp.isEmpty()) return mp;
    }
    return QStringLiteral("A1F83G8C2ARO7P"); // fallback: UK
}

QDir PaneSizing::_resolveProductDir(const QString &asin, const QString &title)
{
    if (!m_workingDir.exists())
        return m_workingDir;

    const QDir sizingRoot(m_workingDir.filePath(QStringLiteral("sizing")));

    const QString prefix = asin + QLatin1Char('-');
    for (const QString &entry : sizingRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry == asin || entry.startsWith(prefix))
            return QDir(sizingRoot.filePath(entry));
    }

    const QString simplified = simplifyForDirName(title);
    const QString dirName = simplified.isEmpty() ? asin : asin + QLatin1Char('-') + simplified;
    m_workingDir.mkpath(QStringLiteral("sizing/") + dirName);
    return QDir(sizingRoot.filePath(dirName));
}

void PaneSizing::_saveProductSettings()
{
    if (!m_productWorkingDir.exists())
        return;

    QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                 QSettings::IniFormat);

    const auto *cat = _currentCategory();
    s.setValue(QStringLiteral("sizing/type"),
               cat ? cat->displayName() : ui->comboBoxSizeType->currentText());

    s.setValue(QStringLiteral("sizing/mode"), ui->sizeRangeMain->mode());
    s.setValue(QStringLiteral("sizing/from"), ui->sizeRangeMain->from());
    s.setValue(QStringLiteral("sizing/to"),   ui->sizeRangeMain->to());
    s.setValue(QStringLiteral("sizing/brandMode"), ui->sizeRangeBrand->mode());
    s.setValue(QStringLiteral("sizing/brandFrom"), ui->sizeRangeBrand->from());
    s.setValue(QStringLiteral("sizing/brandTo"),   ui->sizeRangeBrand->to());

    const QString mPrefix = QStringLiteral("sizing/measurements/");
    for (const auto &w : m_measurementWidgets) {
        const QString base = mPrefix + w.fieldId;
        s.setValue(base + QStringLiteral("/ref"),   w.refSpinBox->value());
        s.setValue(base + QStringLiteral("/step"),  w.stepSpinBox->value());
        s.setValue(base + QStringLiteral("/range"), w.rangeSpinBox->value());
    }
}

void PaneSizing::_loadProductSettings()
{
    if (!m_productWorkingDir.exists())
        return;

    QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                 QSettings::IniFormat);

    if (!s.contains(QStringLiteral("sizing/type")))
        return;

    // Set size type — triggers onSizeTypeChanged → _populateSizeRangeCombos +
    // _rebuildMeasurementForm, which also restores generic measurement defaults.
    const QString savedType = s.value(QStringLiteral("sizing/type")).toString();
    const int typeIdx = ui->comboBoxSizeType->findText(savedType);
    if (typeIdx >= 0)
        ui->comboBoxSizeType->setCurrentIndex(typeIdx);

    const QString mode = s.value(QStringLiteral("sizing/mode")).toString();
    const QString from = s.value(QStringLiteral("sizing/from")).toString();
    const QString to   = s.value(QStringLiteral("sizing/to")).toString();
    ui->sizeRangeMain->setMode(mode);
    if (!from.isEmpty()) ui->sizeRangeMain->setFrom(from);
    if (!to.isEmpty())   ui->sizeRangeMain->setTo(to);

    const QString brandMode = s.value(QStringLiteral("sizing/brandMode"), QStringLiteral("letters")).toString();
    const QString brandFrom = s.value(QStringLiteral("sizing/brandFrom")).toString();
    const QString brandTo   = s.value(QStringLiteral("sizing/brandTo")).toString();
    ui->sizeRangeBrand->setMode(brandMode);
    if (!brandFrom.isEmpty()) ui->sizeRangeBrand->setFrom(brandFrom);
    if (!brandTo.isEmpty())   ui->sizeRangeBrand->setTo(brandTo);

    // Override measurement spinbox values with the product-specific ones.
    // These take priority over the generic category defaults restored by
    // _rebuildMeasurementForm above.
    const QString mPrefix = QStringLiteral("sizing/measurements/");
    for (const auto &w : m_measurementWidgets) {
        const QString base = mPrefix + w.fieldId;
        if (s.contains(base + QStringLiteral("/ref"))) {
            w.refSpinBox->setValue( s.value(base + QStringLiteral("/ref")).toDouble());
            w.stepSpinBox->setValue(s.value(base + QStringLiteral("/step")).toDouble());
            w.rangeSpinBox->setValue(s.value(base + QStringLiteral("/range")).toDouble());
        }
    }

    _initAplusContent();
}

void PaneSizing::_ensureModel(const QDir &dir)
{
    if (m_treeModel)
        return;
    m_treeModel = std::make_unique<TreeSizingAsins>(dir);
    m_treeModel->setApiClient(m_api.get());
    ui->treeViewAsins->setModel(m_treeModel.get());
    ui->treeViewAsins->expandAll();

    connect(m_treeModel.get(), &TreeSizingAsins::marketplacesChecked,
            this, [this](const QStringList &codes) {
                ui->listWidgetCountries->clear();
                for (const QString &c : codes)
                    ui->listWidgetCountries->addItem(c);
                _refreshSizeGroupList();
            });

    connect(m_treeModel.get(), &QAbstractItemModel::modelReset,
            this, [this]() {
                m_productType.clear();
                // m_productTitle is intentionally NOT cleared here: attributesFetched
                // fires before modelReset (before endResetModel), so m_productTitle
                // already holds the current product's title when this lambda runs.
                // We need it for _tryGuessBrandRangeFromTitle() below, which must
                // run after the model is populated (i.e. after endResetModel).
                ui->listWidgetCountries->clear();
                ui->treeViewAsins->expandAll();
                updateButtonStates();
                _tryGuessSizeRange();

                // If a product subdir already exists for this ASIN, show it
                // immediately. Creation (with full ASIN-title name) is deferred
                // to attributesFetched once the title is available.
                if (m_treeModel->rowCount() > 0) {
                    const QString asin = m_treeModel->data(
                        m_treeModel->index(0, TreeSizingAsins::ASIN),
                        Qt::DisplayRole).toString();
                    if (!asin.isEmpty() && m_workingDir.exists()) {
                        const QDir sizingRoot(m_workingDir.filePath(QStringLiteral("sizing")));
                        const QString prefix = asin + QLatin1Char('-');
                        for (const QString &entry : sizingRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                            if (entry == asin || entry.startsWith(prefix)) {
                                m_productWorkingDir = QDir(sizingRoot.filePath(entry));
                                ui->lineEditSubWorkingDir->setText(m_productWorkingDir.absolutePath());
                                _loadProductSettings();
                                break;
                            }
                        }
                    }
                }

                // Re-check button states now that m_productWorkingDir may have been
                // set above. The earlier updateButtonStates() at line 519 runs before
                // the folder lookup, so the A+ buttons were evaluated with stale state.
                updateButtonStates();

                // Brand range guess runs here (not in attributesFetched) so that
                // the tree model is fully populated and all child titles are scannable.
                if (!ui->sizeRangeBrand->isRangeSelected())
                    _tryGuessBrandRangeFromTitle();
            });

    connect(m_treeModel.get(), &TreeSizingAsins::variantImagesFetched,
            this, &PaneSizing::_downloadVariantImages);
    connect(m_treeModel.get(), &TreeSizingAsins::colorAsinsReady,
            this, [this](const QMap<QString, QStringList> &map) { m_colorAsins = map; });


    connect(m_treeModel.get(), &TreeSizingAsins::loadError,
            this, [this](const QString& message) {
                QMessageBox::warning(this, tr("Amazon API error"), message);
            });

    connect(m_treeModel.get(), &TreeSizingAsins::attributesFetched,
            this, [this](const QStringList& bullets, const QStringList& materials,
                         const QString& mainImageUrl, const QString& asin,
                         const QString& title) {
                m_productTitle = title;
                m_currentAsin = asin;
                QString text;
                if (!bullets.isEmpty()) {
                    text += tr("Bullet points:\n");
                    for (const QString& b : bullets)
                        text += QStringLiteral("• ") + b + QLatin1Char('\n');
                }
                if (!materials.isEmpty()) {
                    if (!text.isEmpty()) text += QLatin1Char('\n');
                    text += tr("Material / fabric:\n");
                    for (const QString& m : materials)
                        text += QStringLiteral("• ") + m + QLatin1Char('\n');
                }
                ui->textEditAttributes->setPlainText(text.trimmed());

                if (!asin.isEmpty()) {
                    m_productWorkingDir = _resolveProductDir(asin, title);
                    ui->lineEditSubWorkingDir->setText(m_productWorkingDir.absolutePath());
                    _loadProductSettings();
                    // updateButtonStates() is called here unconditionally because
                    // _loadProductSettings() may return early (no saved type yet),
                    // and the A+ buttons depend on m_productWorkingDir being set.
                    updateButtonStates();
                    // Brand range guess is deferred to the modelReset handler, which
                    // fires after endResetModel() when all child rows are available.
                }

                if (!mainImageUrl.isEmpty() && !asin.isEmpty())
                    _downloadMainImage(mainImageUrl, asin);
            });
}

void PaneSizing::updateButtonStates()
{
    const bool hasAsins = m_treeModel && m_treeModel->rowCount() > 0;
    const bool typeOk   = ui->comboBoxSizeType->currentIndex() > 0;
    const bool rangeOk  = ui->sizeRangeMain->isRangeSelected();

    ui->comboBoxSizeType->setEnabled(hasAsins);
    ui->buttoGenSizeTables->setEnabled(hasAsins && typeOk && rangeOk);

    ui->toolBoxSizing->setEnabled(m_generatedSuccessfully);
    ui->buttonMakeEditable->setEnabled(m_generatedSuccessfully);

    // A+ generate: enabled when a product dir is loaded
    const bool hasProduct = m_productWorkingDir.exists();
    ui->buttonAplusGenerate->setEnabled(hasProduct);
    ui->buttonAplusUpload->setEnabled(hasProduct && m_aplusContent != nullptr);
    ui->buttonOpenSizeTableFolder->setEnabled(hasProduct);
    ui->buttonAddSkusFromTemplate->setEnabled(hasProduct);
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
    const auto *cat = _currentCategory();
    if (!cat)
        return;

    ui->sizeRangeMain->setCategory(cat);
    ui->sizeRangeBrand->setCategory(cat);
    _tryGuessSizeRange();
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

    ui->sizeRangeMain->guessRange(rawSizes);
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

    // Restore previously saved values for this category (if any)
    auto s = WorkingDirectoryManager::instance()->settings();
    const QString prefix = QStringLiteral("sizeCat/") + cat->displayName() + QLatin1Char('/');
    for (const auto &w : m_measurementWidgets) {
        const QString base = prefix + w.fieldId;
        if (s->contains(base + QStringLiteral("/ref"))) {
            w.refSpinBox->setValue( s->value(base + QStringLiteral("/ref")).toDouble());
            w.stepSpinBox->setValue(s->value(base + QStringLiteral("/step")).toDouble());
            w.rangeSpinBox->setValue(s->value(base + QStringLiteral("/range")).toDouble());
        }
    }
}

void PaneSizing::onGenSizeTablesClicked()
{
    m_generatedSuccessfully = false;

    const auto *cat = _currentCategory();
    if (!cat) {
        updateButtonStates();
        return;
    }
    const bool useLetters = ui->sizeRangeMain->mode() == QLatin1String("letters");
    const bool useHeight  = ui->sizeRangeMain->mode() == QLatin1String("height");
    QString keyFrom, keyTo;
    QStringList letterHeaders;

    if (useLetters) {
        const QString lFrom = ui->sizeRangeMain->from();
        const QString lTo   = ui->sizeRangeMain->to();
        keyFrom = cat->letterToKey(lFrom);
        keyTo   = cat->letterToKey(lTo);
        const QStringList allLetters = cat->letterSizes();
        int fi = allLetters.indexOf(lFrom);
        int ti = allLetters.indexOf(lTo);
        if (fi > ti) std::swap(fi, ti);
        letterHeaders = allLetters.mid(fi, ti - fi + 1);
    } else if (useHeight) {
        keyFrom = ui->sizeRangeMain->from();
        keyTo   = ui->sizeRangeMain->to();
    } else {
        keyFrom = ui->sizeRangeMain->from();
        keyTo   = ui->sizeRangeMain->to();
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

        m_generatedSuccessfully = true;

        _saveToSizeTableFolder();

        _saveProductSettings();

        // Persist spinbox values for this category (generic fallback)
        auto s = WorkingDirectoryManager::instance()->settings();
        const QString prefix = QStringLiteral("sizeCat/") + cat->displayName() + QLatin1Char('/');
        for (const auto &w : m_measurementWidgets) {
            const QString base = prefix + w.fieldId;
            s->setValue(base + QStringLiteral("/ref"),   w.refSpinBox->value());
            s->setValue(base + QStringLiteral("/step"),  w.stepSpinBox->value());
            s->setValue(base + QStringLiteral("/range"), w.rangeSpinBox->value());
        }

        if (m_aplusContent)
            _aplusPushSizeChart();
        _refreshSizeGroupList();
        _rebuildAplusMenu();

        // Render English charts synchronously, then translate non-English row labels
        // via the AI CLI inside a modal progress dialog so the user cannot trigger
        // dependent actions (e.g. A+ upload) before translations complete.
        if (m_sizeTableModel && m_aplusContent) {
            const QList<SizeChartTarget> targets = buildSizeChartTargets(cat, ui->listWidgetCountries);

            // English groups: render synchronously — no AI needed, keep inches
            for (const SizeChartTarget &t : targets) {
                if (!t.isEnglish) continue;
                _renderAndSaveChart(cat, t.groupRow,
                                    QStringLiteral("size_chart_") + t.groupKey,
                                    t.language, {}, true);
            }

            AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
            if (cli) {
                QList<SizeChartTarget> nonEnglish;
                for (const SizeChartTarget &t : targets)
                    if (!t.isEnglish) nonEnglish.append(t);

                if (!nonEnglish.isEmpty()) {
                    QStringList origRowLabels;
                    for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
                        auto *it = m_sizeTableModel->item(row, 0);
                        origRowLabels << (it ? it->text() : QString{});
                    }

                    QList<CliTask> tasks =
                        _buildSizeChartTranslationTasks(nonEnglish, origRowLabels);

                    if (!tasks.isEmpty()) {
                        const int total = tasks.size();

                        auto *progressDlg = new QDialog(this);
                        progressDlg->setAttribute(Qt::WA_DeleteOnClose);
                        progressDlg->setWindowModality(Qt::ApplicationModal);
                        progressDlg->setWindowTitle(
                            tr("Generating size charts — %1").arg(cli->getName()));
                        progressDlg->resize(560, 380);
                        auto *pLayout = new QVBoxLayout(progressDlg);

                        auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
                        QFont boldFont = statusLabel->font();
                        boldFont.setBold(true);
                        statusLabel->setFont(boldFont);
                        pLayout->addWidget(statusLabel);

                        auto *progressBar = new QProgressBar(progressDlg);
                        progressBar->setRange(0, total);
                        progressBar->setValue(0);
                        pLayout->addWidget(progressBar);

                        auto *logEdit = new QTextEdit(progressDlg);
                        logEdit->setReadOnly(true);
                        logEdit->setFont(
                            QFontDatabase::systemFont(QFontDatabase::FixedFont));
                        pLayout->addWidget(logEdit);

                        auto *btnLayout = new QHBoxLayout();
                        auto *copyBtn = new QPushButton(tr("Copy log"), progressDlg);
                        btnLayout->addWidget(copyBtn);
                        btnLayout->addStretch();
                        auto *closeBtns =
                            new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
                        QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
                        if (closeBtn) closeBtn->setEnabled(false);
                        btnLayout->addWidget(closeBtns);
                        pLayout->addLayout(btnLayout);

                        QPointer<QLabel>          statusLabelPtr(statusLabel);
                        QPointer<QProgressBar>    progressBarPtr(progressBar);
                        QPointer<QTextEdit>       logEditPtr(logEdit);
                        QPointer<QPushButton>     closeBtnPtr(closeBtn);

                        connect(copyBtn, &QPushButton::clicked, progressDlg,
                            [logEditPtr]() {
                                if (logEditPtr)
                                    QGuiApplication::clipboard()->setText(
                                        logEditPtr->toPlainText());
                            });
                        connect(closeBtns, &QDialogButtonBox::rejected,
                                progressDlg, &QDialog::close);

                        progressDlg->show();

                        auto appendLog = [logEditPtr](const QString &line) {
                            if (!logEditPtr) return;
                            const QString ts = QDateTime::currentDateTime()
                                .toString(QStringLiteral("HH:mm:ss"));
                            logEditPtr->append(
                                QStringLiteral("[%1] %2").arg(ts, line));
                        };

                        auto onTaskStart = [statusLabelPtr, progressBarPtr, appendLog]
                            (int step, int total, const QString &label) {
                                if (statusLabelPtr) statusLabelPtr->setText(
                                    QStringLiteral("(%1/%2) %3")
                                        .arg(step).arg(total).arg(label));
                                if (progressBarPtr) progressBarPtr->setValue(step - 1);
                                appendLog(QStringLiteral("▶ %1").arg(label));
                            };

                        auto onTaskDone = [statusLabelPtr, progressBarPtr,
                                           closeBtnPtr, appendLog]
                            (int step, int total, const QString &label,
                             CliRunResult r) {
                                if (step == total + 1) {
                                    if (statusLabelPtr)
                                        statusLabelPtr->setText(QObject::tr("Done."));
                                    if (progressBarPtr)
                                        progressBarPtr->setValue(progressBarPtr->maximum());
                                    if (closeBtnPtr) closeBtnPtr->setEnabled(true);
                                    return;
                                }
                                const QString outcome = r.output.trimmed().isEmpty()
                                    ? QObject::tr("no output")
                                    : QObject::tr("ok");
                                appendLog(QStringLiteral("[%1/%2] %3 — %4 (%5ms)")
                                    .arg(step).arg(total).arg(label)
                                    .arg(outcome).arg(r.durationMs));
                            };

                        _runSequentially(std::move(tasks),
                                         std::move(onTaskStart),
                                         std::move(onTaskDone));
                    }
                }
            }
        }

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
    if (row < 0 || row >= m_groupImages.size())
        return;
    const QPixmap pm = QPixmap::fromImage(m_groupImages.at(row));
    const int w = ui->labelSizeChartDisplay->width();
    ui->labelSizeChartDisplay->setPixmap(
        (w > 0 && pm.width() > w) ? pm.scaledToWidth(w, Qt::SmoothTransformation) : pm);
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

void PaneSizing::onOpenSizeTableFolderClicked()
{
    const QString sizeTablePath = m_productWorkingDir.filePath(QStringLiteral("size-table"));
    if (QFileInfo::exists(sizeTablePath))
        QDesktopServices::openUrl(QUrl::fromLocalFile(sizeTablePath));
    else if (m_productWorkingDir.exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_productWorkingDir.absolutePath()));
    else
        QMessageBox::information(this, tr("Size-table folder"),
                                 tr("Load a product first."));
}

void PaneSizing::onAddSkusFromTemplateClicked()
{
    _addSkusFromTemplate();
}

void PaneSizing::onUploadSizeImageClicked()
{
    if (m_groupImages.isEmpty())
        return;

    int imageIndex;
    if (ui->radioButtonAppendImage->isChecked())
        imageIndex = -1;
    else if (ui->radioButtonReplaceLastImage->isChecked())
        imageIndex = -2;
    else
        imageIndex = ui->spinBoxImagePos->value();

    _uploadSizeImage(imageIndex);
}

static QString colorToFileSegment(const QString &color)
{
    QString result;
    for (const QChar &c : color.toLower()) {
        if (c.isLetterOrNumber())
            result += c;
        else if (!result.isEmpty() && result.back() != QLatin1Char('-'))
            result += QLatin1Char('-');
    }
    while (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result.isEmpty() ? QStringLiteral("unknown") : result;
}

void PaneSizing::_downloadVariantImages(const QList<QPair<QString, QStringList>> &colorImages)
{
    if (!m_productWorkingDir.exists() || colorImages.isEmpty())
        return;

    m_colorVariants = colorImages;

    ui->listWidgetImages->clear();
    m_variantImagePaths.clear();

    const bool multiColor = colorImages.size() > 1;
    const QString dir = m_productWorkingDir.absolutePath();

    for (const auto &[color, urls] : colorImages) {
        const QString prefix = multiColor
            ? colorToFileSegment(color) + QLatin1Char('-')
            : QString{};
        int index = 1;
        for (const QString &url : urls) {
            const QString filename = QStringLiteral("%1image-%2.jpg")
                .arg(prefix).arg(index, 2, 10, QLatin1Char('0'));
            const QString localPath = dir + QLatin1Char('/') + filename;
            m_variantImagePaths.append(localPath);
            ui->listWidgetImages->addItem(filename);

            if (!QFileInfo::exists(localPath)) {
                QNetworkRequest req{QUrl(url)};
                QNetworkReply *reply = m_imageNam->get(req);
                const QString savedPath = localPath;
                connect(reply, &QNetworkReply::finished, this, [this, reply, savedPath]() {
                    reply->deleteLater();
                    if (reply->error() != QNetworkReply::NoError)
                        return;
                    QFile f(savedPath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(reply->readAll());
                        f.close();
                    }
                    const int row = m_variantImagePaths.indexOf(savedPath);
                    if (row >= 0 && ui->listWidgetImages->currentRow() == row)
                        onVariantImageSelected(row);
                });
            }
            ++index;
        }
    }

    if (ui->listWidgetImages->count() > 0) {
        ui->listWidgetImages->setCurrentRow(0);
        onVariantImageSelected(0);
    }
}

void PaneSizing::onVariantImageSelected(int row)
{
    if (row < 0 || row >= m_variantImagePaths.size()) {
        ui->labelVariantImage->clear();
        return;
    }
    const QPixmap pm(m_variantImagePaths.at(row));
    if (pm.isNull()) {
        ui->labelVariantImage->setText(tr("(image not yet downloaded)"));
        return;
    }
    const QSize vp = ui->scrollAreaImage->viewport()->size();
    const int maxW = vp.width()  - 4;
    const int maxH = vp.height() - 4;
    ui->labelVariantImage->setPixmap(
        pm.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PaneSizing::_downloadMainImage(const QString &url, const QString &asin)
{
    const QDir &targetDir = m_productWorkingDir.exists() ? m_productWorkingDir : m_workingDir;
    const QString dir = targetDir.isAbsolute()
        ? targetDir.path()
        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString filename = dir + QLatin1Char('/') + (asin.isEmpty() ? QStringLiteral("main") : asin) + QStringLiteral("_main.jpg");

    if (QFileInfo::exists(filename)) {
        m_mainImageLocalPath = filename;
        return;
    }

    QNetworkRequest req{QUrl(url)};
    QNetworkReply *reply = m_imageNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, filename]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        QFile f(filename);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(reply->readAll());
            f.close();
            m_mainImageLocalPath = filename;
        }
    });
}

// --- A+ content implementation -----------------------------------------------

void PaneSizing::_initAplusContent()
{
    if (!m_productWorkingDir.exists())
        return;

    // Skip re-init if we already loaded content for this same directory.
    const QDir newAplusDir(m_productWorkingDir.filePath(QStringLiteral("aplus")));
    if (m_aplusContent && m_aplusContent->dir().absolutePath() == newAplusDir.absolutePath())
        return;

    m_aplusContent = std::make_unique<APlusContent>(this);
    m_aplusContent->setDir(newAplusDir);
    m_aplusContent->load();

    if (m_aplusModel) {
        ui->aplusTreeView->setModel(nullptr);
        delete m_aplusModel;
        m_aplusModel = nullptr;
    }
    m_aplusModel = new APlusTreeModel(m_aplusContent.get(), this);
    ui->aplusTreeView->setModel(m_aplusModel);
    ui->aplusTreeView->setColumnWidth(APlusTreeModel::Name,    220);
    ui->aplusTreeView->setColumnWidth(APlusTreeModel::Desktop,  70);
    ui->aplusTreeView->setColumnWidth(APlusTreeModel::Mobile,   70);

    connect(m_aplusContent.get(), &APlusContent::elementChanged,
            this, [this](const QString &) { _rebuildAplusModel(); });
    connect(m_aplusContent.get(), &APlusContent::layoutChanged,
            this, [this]() { _rebuildAplusModel(); });

    if (auto *sel = ui->aplusTreeView->selectionModel()) {
        connect(sel, &QItemSelectionModel::currentChanged,
                this, &PaneSizing::onAplusSelectionChanged);
    }

    _rebuildAplusMenu();
    ui->buttonAplusDeleteVersion->setEnabled(false);

    ui->comboBoxAplusLanguage->blockSignals(true);
    ui->comboBoxAplusLanguage->clear();
    ui->comboBoxAplusLanguage->setProperty("aplusFamily", QString{});
    ui->comboBoxAplusLanguage->blockSignals(false);
    ui->comboBoxAplusLanguage->setVisible(false);

    _refreshSizeGroupList();
    _refreshAplusPreview();
}

// ---------------------------------------------------------------------------
// A+ upload helpers (file-local) — kept outside the coroutine frame to avoid
// GCC 13 ICE in build_special_member_call when too many non-trivially-
// destructible locals straddle a suspension point.
// ---------------------------------------------------------------------------

namespace {

QList<APlusUploadDialog::ElementInfo>
buildAplusElementInfos(const APlusContent &content)
{
    QList<APlusUploadDialog::ElementInfo> infos;
    const QDir aplusDir = content.dir();
    for (const APlusElement &elem : content.elements()) {
        const APlusVersion *ver = elem.current();
        if (!ver) continue;

        APlusUploadDialog::ElementInfo info;
        info.id          = elem.id;
        info.displayName = elem.displayName;
        info.type        = elem.type;

        if (elem.type == APlusElementType::Faq) {
            const QString txtPath = aplusDir.filePath(ver->desktopFile);
            QFile f(txtPath);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                info.textContent = QString::fromUtf8(f.readAll()).trimmed();
        } else {
            info.imagePath = aplusDir.filePath(ver->desktopFile);
            const QImage full(info.imagePath);
            if (!full.isNull())
                info.thumbnail = full.scaled(120, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        infos.append(info);
    }
    return infos;
}

QList<QPair<QString, QString>>
buildAplusMarketplaceList(QListWidget *countriesList)
{
    static const QHash<QString, QString> kCodeToMp = {
        {QStringLiteral("fr"), QStringLiteral("A13V1IB3VIYZZH")},
        {QStringLiteral("de"), QStringLiteral("A1PA6795UKMFR9")},
        {QStringLiteral("it"), QStringLiteral("APJ6JRA9NG5V4")},
        {QStringLiteral("es"), QStringLiteral("A1RKKUPIHCS9HS")},
        {QStringLiteral("uk"), QStringLiteral("A1F83G8C2ARO7P")},
        {QStringLiteral("nl"), QStringLiteral("A1805IZSGTT6HS")},
        {QStringLiteral("se"), QStringLiteral("A2NODRKZP88ZB9")},
        {QStringLiteral("pl"), QStringLiteral("A1C3SOZRARQ6R3")},
        {QStringLiteral("be"), QStringLiteral("AMEN7PMS3EDWL")},
        {QStringLiteral("ie"), QStringLiteral("A28R8C7NBKEWEA")},
        {QStringLiteral("tr"), QStringLiteral("A33AVAJ2PDY3EV")},
        {QStringLiteral("us"), QStringLiteral("ATVPDKIKX0DER")},
        {QStringLiteral("ca"), QStringLiteral("A2EUQ1WTGCTBG2")},
        {QStringLiteral("mx"), QStringLiteral("A1AM78C64UM0Y8")},
        {QStringLiteral("jp"), QStringLiteral("A1VC38T7YXB528")},
    };
    QList<QPair<QString, QString>> marketplaces;
    QSet<QString> seenMps;
    for (int i = 0; i < countriesList->count(); ++i) {
        const QString code = countriesList->item(i)->text().trimmed().toLower();
        if (code.contains(QLatin1String("(missing)"))) continue;
        const QString mpId = kCodeToMp.value(code);
        if (mpId.isEmpty() || seenMps.contains(mpId)) continue;
        seenMps.insert(mpId);
        marketplaces.append({code.toUpper(), mpId});
    }
    if (marketplaces.isEmpty())
        marketplaces.append({QStringLiteral("UK"), QStringLiteral("A1F83G8C2ARO7P")});
    return marketplaces;
}

static QString sizeChartTitle(const QString &locale)
{
    static const QHash<QString, QString> t{
        {QStringLiteral("fr-FR"), QStringLiteral("Tableau des tailles")},
        {QStringLiteral("fr-BE"), QStringLiteral("Tableau des tailles")},
        {QStringLiteral("de-DE"), QStringLiteral("Größentabelle")},
        {QStringLiteral("it-IT"), QStringLiteral("Tabella delle taglie")},
        {QStringLiteral("es-ES"), QStringLiteral("Tabla de tallas")},
        {QStringLiteral("nl-NL"), QStringLiteral("Maattabel")},
        {QStringLiteral("en-GB"), QStringLiteral("Size guide")},
        {QStringLiteral("en-US"), QStringLiteral("Size chart")},
    };
    return t.value(locale, QStringLiteral("Size chart")).toUpper();
}

static QString apparelSlogan(const QString &locale)
{
    static const QHash<QString, QString> t{
        {QStringLiteral("fr-FR"), QStringLiteral("Réveillez votre aura")},
        {QStringLiteral("fr-BE"), QStringLiteral("Réveillez votre aura")},
        {QStringLiteral("de-DE"), QStringLiteral("Entfalte deine Ausstrahlung")},
        {QStringLiteral("it-IT"), QStringLiteral("Esalta la tua aura")},
        {QStringLiteral("es-ES"), QStringLiteral("Eleva tu aura")},
        {QStringLiteral("nl-NL"), QStringLiteral("Straal je aura uit")},
        {QStringLiteral("en-GB"), QStringLiteral("Raise your aura")},
        {QStringLiteral("en-US"), QStringLiteral("Raise your aura")},
    };
    return t.value(locale, QStringLiteral("Raise your aura")).toUpper();
}

static QJsonObject buildHeadlineModule(const QString &title)
{
    return QJsonObject{
        {QStringLiteral("contentModuleType"), QStringLiteral("STANDARD_TEXT")},
        {QStringLiteral("standardText"), QJsonObject{
            {QStringLiteral("headline"), QJsonObject{
                {QStringLiteral("value"), title},
                {QStringLiteral("decoratorSet"), QJsonArray{}}
            }},
            {QStringLiteral("body"), QJsonObject{
                {QStringLiteral("textList"), QJsonArray{}}
            }}
        }}
    };
}

static QString stripQaPrefix(const QString &line)
{
    // Remove leading "Q: ", "A: ", "Q : ", "A : " (case-insensitive, any whitespace after colon)
    static const QRegularExpression re(QStringLiteral("^[QqAa]\\s*:\\s*"));
    return QString(line).remove(re).trimmed();
}

QJsonObject buildFaqModule(const QString &text)
{
    QJsonArray textList;

    // One text item per Q&A pair: bold question + STYLE_LINEBREAK + answer.
    // Paragraph spacing appears only between pairs, not between Q and A.
    const QStringList pairs = text.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        const QStringList lines = pair.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString question = lines.isEmpty() ? QString{} : stripQaPrefix(lines[0]);
        const QString answer   = lines.size() > 1 ? stripQaPrefix(lines[1]) : QString{};
        if (question.isEmpty()) continue;

        // Q and A go in the same text item (no \n — \n creates a full paragraph
        // break which gives the same spacing as separate items). One space
        // between them keeps them in the same paragraph so Q and A stay tight,
        // while full paragraph spacing appears only between Q&A pairs.
        const QString sep      = answer.isEmpty() ? QString{} : QStringLiteral(" ");
        const QString combined = (question + sep + answer).left(2000);
        const int     qLen     = qMin(question.length(), combined.length());
        textList.append(QJsonObject{
            {QStringLiteral("value"), combined},
            {QStringLiteral("decoratorSet"), QJsonArray{
                QJsonObject{{QStringLiteral("type"),   QStringLiteral("STYLE_BOLD")},
                            {QStringLiteral("offset"), 0},
                            {QStringLiteral("length"), qLen}}
            }}
        });
    }
    if (textList.isEmpty())
        textList.append(QJsonObject{
            {QStringLiteral("value"), text.left(2000)},
            {QStringLiteral("decoratorSet"), QJsonArray{}}
        });
    return QJsonObject{
        {QStringLiteral("contentModuleType"), QStringLiteral("STANDARD_TEXT")},
        {QStringLiteral("standardText"), QJsonObject{
            {QStringLiteral("headline"), QJsonObject{
                {QStringLiteral("value"), QStringLiteral("FAQ")},
                {QStringLiteral("decoratorSet"), QJsonArray{}}
            }},
            {QStringLiteral("body"), QJsonObject{
                {QStringLiteral("textList"), textList}
            }}
        }}
    };
}

// Returns the ASIN with the smallest size from asinList by querying the tree model.
// Numeric sizes are compared as numbers; letter sizes follow XS<S<M<L<XL<XXL order.
// Falls back to the first entry if the tree model is null or has no matching rows.
static QString smallestSizeAsin(TreeSizingAsins *model, const QStringList &asinList)
{
    if (asinList.isEmpty()) return {};
    if (!model || asinList.size() == 1) return asinList.first();

    static const QStringList kLetterOrder = {
        QStringLiteral("XXXS"), QStringLiteral("XXS"), QStringLiteral("XS"),
        QStringLiteral("S"),    QStringLiteral("M"),   QStringLiteral("L"),
        QStringLiteral("XL"),   QStringLiteral("XXL"), QStringLiteral("XXXL"),
        QStringLiteral("3XL"),  QStringLiteral("4XL"), QStringLiteral("5XL")
    };

    QList<QPair<QString, QString>> sizeToAsin;
    const int famCount = model->rowCount();
    for (int f = 0; f < famCount; ++f) {
        const QModelIndex famIdx = model->index(f, 0);
        const int childCount = model->rowCount(famIdx);
        for (int c = 0; c < childCount; ++c) {
            const QString asin = model->data(
                model->index(c, TreeSizingAsins::ASIN, famIdx)).toString().trimmed();
            if (!asinList.contains(asin)) continue;
            const QString size = model->data(
                model->index(c, TreeSizingAsins::Size, famIdx)).toString().trimmed();
            sizeToAsin.append({size, asin});
        }
    }
    if (sizeToAsin.isEmpty()) return asinList.first();

    std::sort(sizeToAsin.begin(), sizeToAsin.end(),
        [](const QPair<QString,QString> &a, const QPair<QString,QString> &b) {
            bool okA, okB;
            const double dA = a.first.toDouble(&okA);
            const double dB = b.first.toDouble(&okB);
            if (okA && okB) return dA < dB;
            const int iA = kLetterOrder.indexOf(a.first.toUpper());
            const int iB = kLetterOrder.indexOf(b.first.toUpper());
            if (iA >= 0 && iB >= 0) return iA < iB;
            return a.first < b.first;
        });

    return sizeToAsin.first().second;
}

// headline is optional — pass empty string to omit it.
QJsonObject buildImageModule(const QString &uploadId,
                              const QString &caption,
                              int imgWidth, int imgHeight,
                              const QString &headline = {})
{
    // Amazon rejects em/en dashes as guideline violations — replace with hyphen.
    const QString safeCaption = QString(caption).replace(QChar(0x2014), QLatin1Char('-'))
                                                .replace(QChar(0x2013), QLatin1Char('-'));
    QJsonObject inner;
    if (!headline.isEmpty())
        inner[QStringLiteral("headline")] = QJsonObject{
            {QStringLiteral("value"), headline},
            {QStringLiteral("decoratorSet"), QJsonArray{}}
        };
    inner[QStringLiteral("block")] = QJsonObject{
        {QStringLiteral("image"), QJsonObject{
            {QStringLiteral("uploadDestinationId"), uploadId},
            {QStringLiteral("altText"), safeCaption},
            {QStringLiteral("imageCropSpecification"), QJsonObject{
                {QStringLiteral("size"), QJsonObject{
                    {QStringLiteral("width"),  QJsonObject{{QStringLiteral("value"), imgWidth},  {QStringLiteral("units"), QStringLiteral("pixels")}}},
                    {QStringLiteral("height"), QJsonObject{{QStringLiteral("value"), imgHeight}, {QStringLiteral("units"), QStringLiteral("pixels")}}}
                }},
                {QStringLiteral("offset"), QJsonObject{
                    {QStringLiteral("x"), QJsonObject{{QStringLiteral("value"), 0}, {QStringLiteral("units"), QStringLiteral("pixels")}}},
                    {QStringLiteral("y"), QJsonObject{{QStringLiteral("value"), 0}, {QStringLiteral("units"), QStringLiteral("pixels")}}}
                }}
            }}
        }},
        {QStringLiteral("body"), QJsonObject{
            {QStringLiteral("textList"), QJsonArray{}}
        }}
    };
    return QJsonObject{
        {QStringLiteral("contentModuleType"), QStringLiteral("STANDARD_HEADER_IMAGE_TEXT")},
        {QStringLiteral("standardHeaderImageText"), inner}
    };
}

QByteArray imageToPngBytes(const QImage &img)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

struct AplusProgressUi {
    QPointer<QLabel>       statusPtr;
    QPointer<QProgressBar> barPtr;
    QPointer<QTextEdit>    logPtr;
};

AplusProgressUi createAplusProgressDialog(QWidget *parent, int totalSteps)
{
    auto *progressDlg = new QDialog(parent);
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowTitle(QObject::tr("Uploading A+ Content"));
    progressDlg->resize(500, 320);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(QObject::tr("Starting…"), progressDlg);
    QFont boldF = statusLabel->font(); boldF.setBold(true);
    statusLabel->setFont(boldF);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, totalSteps);
    progressBar->setValue(0);
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QObject::connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);
    pLayout->addWidget(closeBtns);

    progressDlg->show();

    return AplusProgressUi{
        QPointer<QLabel>(statusLabel),
        QPointer<QProgressBar>(progressBar),
        QPointer<QTextEdit>(logEdit)
    };
}

void appendAplusLog(const QPointer<QTextEdit> &logPtr, const QString &msg)
{
    if (logPtr)
        logPtr->append(QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), msg));
}

void setAplusStatus(const AplusProgressUi &ui, const QString &msg, int step)
{
    if (ui.statusPtr) ui.statusPtr->setText(msg);
    if (ui.barPtr)    ui.barPtr->setValue(step);
}

} // namespace

void PaneSizing::onAplusUploadClicked()
{
    _uploadAplusContent();
}

// Helper: find element from flat list by type + exact id, with fallback to base id.
static const APlusUploadDialog::ElementInfo *
findAplusElement(const QList<APlusUploadDialog::ElementInfo> &infos,
                 APlusElementType type,
                 const QString &exactId,
                 const QString &fallbackId = QString{})
{
    for (const auto &info : infos)
        if (info.type == type && info.id == exactId) return &info;
    if (!fallbackId.isEmpty())
        for (const auto &info : infos)
            if (info.type == type && info.id == fallbackId) return &info;
    return nullptr;
}

QCoro::Task<void> PaneSizing::_uploadAplusContent()
{
    if (!m_aplusContent || !m_aplusApi) co_return;

    // --- Sync prep ---
    QList<APlusUploadDialog::ElementInfo> infos = buildAplusElementInfos(*m_aplusContent);
    if (infos.isEmpty()) {
        QMessageBox::information(this, tr("Upload A+ Content"),
            tr("No A+ content elements found. Generate content first."));
        co_return;
    }

    QList<QPair<QString, QString>> marketplaces = buildAplusMarketplaceList(ui->listWidgetCountries);

    QStringList colorNames;
    for (const auto &[color, urls] : std::as_const(m_colorVariants))
        if (!color.isEmpty()) colorNames << color;

    // --- Dialog (all locals scoped before any co_await) ---
    QStringList mpIds;
    bool addSizeChart  = false;
    bool addFaq        = false;
    bool submitApproval = false;
    QList<QList<APlusUploadDialog::ElementInfo>> imageSets;
    {
        auto s = WorkingDirectoryManager::instance()->settings();
        const bool submitDefault = s->value(
            QStringLiteral("aplus/submitForApproval"), true).toBool();

        APlusUploadDialog dlg(infos, marketplaces, colorNames, submitDefault, this);
        if (dlg.exec() != QDialog::Accepted) co_return;
        mpIds          = dlg.selectedMarketplaceIds();
        addSizeChart   = dlg.includeSizeChart();
        addFaq         = dlg.includeFaq();
        imageSets      = dlg.selectedImagesByColor();
        submitApproval = dlg.shouldSubmitForApproval();

        s->setValue(QStringLiteral("aplus/submitForApproval"), submitApproval);
    }
    if (mpIds.isEmpty()) co_return;
    if (imageSets.isEmpty()) imageSets.append(QList<APlusUploadDialog::ElementInfo>{}); // size chart + FAQ only

    // --- Probe A+ Content API access before attempting uploads ---
    {
        int probeStatus = 0;
        co_await m_aplusApi->probeContentDocumentAccess(mpIds.first(), &probeStatus);
        qDebug() << "PaneSizing: A+ Content API probe for" << mpIds.first()
                 << "→ HTTP" << probeStatus;
    }

    // --- Estimate total steps ---
    const int fixedPerUpload = 3 + (submitApproval ? 1 : 0);
    int totalSteps = 0;
    for (const auto &imgSet : std::as_const(imageSets))
        totalSteps += mpIds.size() * (imgSet.size() + (addSizeChart ? 1 : 0) + fixedPerUpload);

    AplusProgressUi progressUi = createAplusProgressDialog(this, totalSteps);
    int step = 0;

    // --- Main loop: marketplace × color set ---
    for (const QString &mpId : std::as_const(mpIds)) {
        const QString locale = AmazonAplusApi::localeForMarketplace(mpId);

        for (int colorIdx = 0; colorIdx < imageSets.size(); ++colorIdx) {
            const QList<APlusUploadDialog::ElementInfo> &imgSet = imageSets.at(colorIdx);

            appendAplusLog(progressUi.logPtr,
                imageSets.size() > 1
                ? tr("─── %1  |  color set %2/%3 ───").arg(mpId).arg(colorIdx + 1).arg(imageSets.size())
                : tr("─── %1 ───").arg(mpId));

            QJsonArray moduleList;

            // --- Size chart ---
            if (addSizeChart) {
                ++step;
                const QString scKey = APlusUploadDialog::sizeChartKeyForMarketplace(mpId);
                setAplusStatus(progressUi,
                    tr("Uploading size chart (%1)…").arg(scKey), step - 1);
                const auto *sc = findAplusElement(infos, APlusElementType::SizeChart,
                    QStringLiteral("size_chart_") + scKey, QStringLiteral("size_chart"));
                if (!sc) {
                    appendAplusLog(progressUi.logPtr,
                        tr("  ⚠ Size chart '%1' not found — skipped").arg(scKey));
                } else {
                    appendAplusLog(progressUi.logPtr,
                        tr("▶ Uploading size chart: %1").arg(sc->displayName));
                    QImage img(sc->imagePath);
                    if (!img.isNull()) {
                        img = [](QImage src) {
                            if (src.width() > 970) src = src.scaledToWidth(970, Qt::SmoothTransformation);
                            // No extra height padding here: _renderAndSaveChart already
                            // saves with 400 px minimum (table centered, ~50 px white each
                            // side).  Adding another 600 px floor creates ~250 px of dead
                            // white below the table in the A+ module.
                            return src;
                        }(img);
                        const int imgW = img.width(), imgH = img.height();
                        QByteArray bytes = imageToPngBytes(img);
                        QString uploadId;
                        co_await m_aplusApi->uploadImage(mpId, bytes,
                            QStringLiteral("image/png"), &uploadId);
                        if (uploadId.isEmpty()) {
                            appendAplusLog(progressUi.logPtr,
                                tr("  ✗ Upload failed: %1").arg(m_aplusApi->lastError()));
                            setAplusStatus(progressUi, tr("Upload failed."), step);
                            co_return;
                        }
                        appendAplusLog(progressUi.logPtr,
                            tr("  ✓ Uploaded — ID: %1").arg(uploadId.left(40)));
                        // Module headline used (same Amazon font/style as FAQ headline).
                        moduleList.append(buildImageModule(uploadId, sc->displayName, imgW, imgH,
                                                           sizeChartTitle(locale)));
                    } else {
                        appendAplusLog(progressUi.logPtr,
                            tr("  ⚠ Cannot load size chart image — skipped"));
                    }
                }
            }

            // --- Color-set images ---
            const bool useSlogan = !imgSet.isEmpty() && [this]() {
                const auto *cat = _currentCategory(); return cat && cat->isApparel();
            }();
            for (int ii = 0; ii < imgSet.size(); ++ii) {
                const APlusUploadDialog::ElementInfo &info = imgSet.at(ii);
                ++step;
                setAplusStatus(progressUi, tr("Uploading %1…").arg(info.displayName), step - 1);
                appendAplusLog(progressUi.logPtr, tr("▶ Uploading %1").arg(info.displayName));
                if (info.imagePath.isEmpty()) {
                    appendAplusLog(progressUi.logPtr, tr("  ⚠ No image path — skipped"));
                    continue;
                }
                QImage img(info.imagePath);
                if (img.isNull()) {
                    appendAplusLog(progressUi.logPtr, tr("  ⚠ Cannot load image — skipped"));
                    continue;
                }
                img = [](QImage src) {
                    if (src.width() > 970) src = src.scaledToWidth(970, Qt::SmoothTransformation);
                    if (src.height() < 600) {
                        QImage p(src.width(), 600, QImage::Format_ARGB32);
                        p.fill(Qt::white);
                        QPainter(&p).drawImage(0, 0, src);
                        src = p;
                    }
                    return src;
                }(img);
                const int imgW = img.width(), imgH = img.height();
                QByteArray bytes = imageToPngBytes(img);
                QString uploadId;
                co_await m_aplusApi->uploadImage(mpId, bytes,
                    QStringLiteral("image/png"), &uploadId);
                if (uploadId.isEmpty()) {
                    appendAplusLog(progressUi.logPtr,
                        tr("  ✗ Upload failed: %1").arg(m_aplusApi->lastError()));
                    setAplusStatus(progressUi, tr("Upload failed."), step);
                    co_return;
                }
                appendAplusLog(progressUi.logPtr,
                    tr("  ✓ Uploaded — ID: %1").arg(uploadId.left(40)));
                // Module headline — same Amazon font/style as the size chart and FAQ.
                const QString imgHeadline = (ii == 0 && useSlogan) ? apparelSlogan(locale) : QString{};
                moduleList.append(buildImageModule(uploadId, info.displayName, imgW, imgH, imgHeadline));
            }

            // --- FAQ ---
            if (addFaq) {
                const QString faqKey = APlusUploadDialog::faqLangKeyForMarketplace(mpId);
                const auto *faq = findAplusElement(infos, APlusElementType::Faq,
                    QStringLiteral("faq_") + faqKey, QStringLiteral("faq"));
                if (!faq) {
                    appendAplusLog(progressUi.logPtr,
                        tr("  ⚠ FAQ '%1' not found — skipped").arg(faqKey));
                } else {
                    const QString text = faq->textContent.isEmpty()
                        ? QStringLiteral("(no FAQ content)") : faq->textContent;
                    moduleList.append(buildFaqModule(text));
                    appendAplusLog(progressUi.logPtr,
                        tr("  ✓ FAQ '%1' included (%2 chars)").arg(faq->displayName).arg(text.size()));
                }
            }

            if (moduleList.isEmpty()) {
                appendAplusLog(progressUi.logPtr, tr("  ⚠ No modules — skipping this upload."));
                step += fixedPerUpload;
                continue;
            }

            // --- Create content document ---
            ++step;
            setAplusStatus(progressUi, tr("Creating A+ content document…"), step - 1);
            appendAplusLog(progressUi.logPtr,
                tr("▶ Creating document (locale: %1)").arg(locale));
            // --- Resolve child ASINs for this color set (needed for name + association) ---
            QStringList asinList;
            {
                const QString colorKey = (colorIdx < colorNames.size())
                    ? colorNames.at(colorIdx).toLower() : QString{};
                if (!colorKey.isEmpty() && m_colorAsins.contains(colorKey)) {
                    asinList = m_colorAsins.value(colorKey);
                } else {
                    for (const auto &asins : std::as_const(m_colorAsins))
                        for (const QString &a : asins)
                            if (!asinList.contains(a)) asinList << a;
                }
            }

            // Build a per-color document name prefixed with the smallest-size child ASIN
            // so the document is findable by ASIN in the Seller Central search bar.
            QString docName = m_productTitle.isEmpty() ? QStringLiteral("A+ Content") : m_productTitle;
            if (colorIdx < colorNames.size() && !colorNames.at(colorIdx).isEmpty()) {
                const int paren = docName.indexOf(QLatin1Char('('));
                const QString base = (paren > 0 ? docName.left(paren) : docName).trimmed();
                docName = base + QStringLiteral(" (") + colorNames.at(colorIdx) + QLatin1Char(')');
            }
            {
                const QString smallAsin = smallestSizeAsin(m_treeModel.get(), asinList);
                if (!smallAsin.isEmpty())
                    docName = smallAsin + QStringLiteral(" - ") + docName;
            }
            QJsonObject contentDoc{
                {QStringLiteral("name"),              docName.left(100)},
                {QStringLiteral("contentType"),       QStringLiteral("EBC")},
                {QStringLiteral("locale"),            locale},
                {QStringLiteral("contentModuleList"), moduleList}
            };
            QString contentReferenceKey;
            co_await m_aplusApi->createContentDocument(mpId, contentDoc, &contentReferenceKey);
            if (contentReferenceKey.isEmpty()) {
                appendAplusLog(progressUi.logPtr,
                    tr("✗ createContentDocument failed: %1").arg(m_aplusApi->lastError()));
                setAplusStatus(progressUi, tr("Create document failed."), step);
                co_return;
            }
            appendAplusLog(progressUi.logPtr,
                tr("  ✓ Document created — key: %1").arg(contentReferenceKey.left(40)));

            // --- Associate with ASINs ---
            ++step;
            setAplusStatus(progressUi, tr("Associating with ASINs…"), step - 1);
            if (asinList.isEmpty()) {
                appendAplusLog(progressUi.logPtr,
                    tr("  ⚠ No child ASINs available — skipping association."));
            } else {
                appendAplusLog(progressUi.logPtr,
                    tr("▶ Associating with %1 ASIN(s): %2")
                        .arg(asinList.size())
                        .arg(asinList.join(QStringLiteral(", "))));
                bool asinOk = false;
                co_await m_aplusApi->postAsinRelations(contentReferenceKey, mpId,
                                                       asinList, &asinOk);
                appendAplusLog(progressUi.logPtr, asinOk
                    ? tr("  ✓ ASINs associated.")
                    : tr("  ⚠ ASIN association failed: %1 (continuing)")
                          .arg(m_aplusApi->lastError()));
            }

            // --- Validate ---
            ++step;
            setAplusStatus(progressUi, tr("Validating content…"), step - 1);
            appendAplusLog(progressUi.logPtr, tr("▶ Validating content document…"));
            {
                QStringList valErrors, valWarnings;
                co_await m_aplusApi->validateContentDocumentAsinRelations(
                    contentReferenceKey, mpId, contentDoc,
                    asinList, &valErrors, &valWarnings);
                for (const QString &w : std::as_const(valWarnings))
                    appendAplusLog(progressUi.logPtr, tr("  ⚠ Warning: %1").arg(w));
                for (const QString &e : std::as_const(valErrors))
                    appendAplusLog(progressUi.logPtr, tr("  ✗ Error: %1").arg(e));
                if (!valErrors.isEmpty()) {
                    setAplusStatus(progressUi, tr("Validation failed."), step);
                    appendAplusLog(progressUi.logPtr,
                        tr("✗ Created (key: %1) but not submitted.").arg(contentReferenceKey));
                    co_return;
                }
                appendAplusLog(progressUi.logPtr, valWarnings.isEmpty()
                    ? tr("  ✓ Validation passed.")
                    : tr("  ✓ Validation passed (with warnings)."));
            }

            // --- Submit for approval ---
            if (submitApproval) {
                ++step;
                setAplusStatus(progressUi, tr("Submitting for approval…"), step - 1);
                appendAplusLog(progressUi.logPtr, tr("▶ Submitting for approval…"));
                bool approvalOk = false;
                co_await m_aplusApi->submitForApproval(contentReferenceKey, mpId, &approvalOk);
                appendAplusLog(progressUi.logPtr, approvalOk
                    ? tr("  ✓ Submitted. Amazon reviews within 24–48 hours.")
                    : tr("  ⚠ Approval submission failed: %1").arg(m_aplusApi->lastError()));
            }

            appendAplusLog(progressUi.logPtr,
                tr("  ✓ Upload complete — key: %1").arg(contentReferenceKey));
        }
    }

    setAplusStatus(progressUi, tr("Done!"), totalSteps);
    appendAplusLog(progressUi.logPtr, tr("✓ All uploads complete."));
}

void PaneSizing::_rebuildAplusMenu()
{
    if (!m_aplusMenu) {
        m_aplusMenu = new QMenu(this);
        ui->buttonAplusGenerate->setMenu(m_aplusMenu);
    }
    m_aplusMenu->clear();

    QAction *genAllAct = m_aplusMenu->addAction(tr("Generate All (images + FAQ)"));
    connect(genAllAct, &QAction::triggered, this, &PaneSizing::onAplusGenerateAll);

    m_aplusMenu->addSeparator();

    QAction *sizeChartAct = m_aplusMenu->addAction(
        tr("Size Chart (from generated table)"));
    sizeChartAct->setEnabled(m_generatedSuccessfully);
    connect(sizeChartAct, &QAction::triggered,
            this, &PaneSizing::onAplusGenerateSizeChart);

    if (m_aplusContent) {
        for (const APlusElement &e : m_aplusContent->elements()) {
            if (e.type != APlusElementType::Image)
                continue;
            const QString id = e.id;
            QAction *imgAct = m_aplusMenu->addAction(e.displayName);
            connect(imgAct, &QAction::triggered, this, [this, id]() {
                onAplusGenerateImage(id);
            });
        }
    }

    m_aplusMenu->addSeparator();
    QAction *faqAct = m_aplusMenu->addAction(tr("FAQ"));
    connect(faqAct, &QAction::triggered, this, &PaneSizing::onAplusGenerateFaq);
}

QString PaneSizing::_aplusTimestamp() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

void PaneSizing::_aplusPushImage(const QImage &img, const QString &elementId,
                                  const QString &displayName, APlusElementType type)
{
    if (!m_aplusContent || img.isNull())
        return;

    QDir aplusDir = m_aplusContent->dir();
    aplusDir.mkpath(elementId);

    const QString ts = _aplusTimestamp();
    const QString relDesktop = elementId + QStringLiteral("/v_") + ts + QStringLiteral("_desktop.png");
    const QString relMobile  = elementId + QStringLiteral("/v_") + ts + QStringLiteral("_mobile.png");

    const QImage desktopImg = (img.width() > 970)
        ? img.scaledToWidth(970, Qt::SmoothTransformation)
        : img;
    const QImage mobileImg  = img.scaledToWidth(600, Qt::SmoothTransformation);

    desktopImg.save(aplusDir.filePath(relDesktop), "PNG");
    mobileImg .save(aplusDir.filePath(relMobile),  "PNG");

    APlusVersion ver;
    ver.generated   = QDateTime::currentDateTime();
    ver.desktopFile = relDesktop;
    ver.mobileFile  = relMobile;

    m_aplusContent->pushVersion(elementId, type, displayName, ver);
    _rebuildAplusModel();

    // Expand and select the new version row (latest version is index 0 under family).
    const int famIdx = m_aplusModel->familyIndexForElement(elementId);
    if (famIdx >= 0) {
        const QModelIndex familyIndex = m_aplusModel->index(famIdx, 0, {});
        ui->aplusTreeView->expand(familyIndex);
        const QModelIndex versionIndex = m_aplusModel->index(0, 0, familyIndex);
        if (versionIndex.isValid()) {
            const QModelIndex langIndex = m_aplusModel->index(0, 0, versionIndex);
            if (langIndex.isValid()) {
                ui->aplusTreeView->expand(versionIndex);
                ui->aplusTreeView->setCurrentIndex(langIndex);
                _refreshAplusPreview(langIndex);
            } else {
                ui->aplusTreeView->setCurrentIndex(versionIndex);
                _refreshAplusPreview(versionIndex);
            }
        }
    }
}

void PaneSizing::_aplusPushSizeChart()
{
    if (!m_aplusContent || !m_sizeTableModel)
        return;

    const auto *cat = _currentCategory();
    if (!cat)
        return;

    // Size chart is deterministic — save directly with setSingleVersion so it
    // never accumulates version history the way AI-generated content does.
    const QImage img = cat->renderImage(m_sizeTableModel);
    if (img.isNull())
        return;

    const QDir aplusDir = m_aplusContent->dir();
    aplusDir.mkpath(QStringLiteral("size_chart"));
    const QString relPath = QStringLiteral("size_chart/size_chart.png");
    const QString absPath = aplusDir.filePath(relPath);

    // Always scale to target width (upscale if needed — renderImage produces screen-res output).
    QImage desktop = img.scaledToWidth(970, Qt::SmoothTransformation);
    QImage mobile  = img.scaledToWidth(600, Qt::SmoothTransformation);

    // Pad to minimum height so the image meets Amazon A+ content requirements.
    // A size chart rendered from a table is often 90–150 px tall at screen DPI.
    auto padToMinHeight = [](const QImage &src, int minH) -> QImage {
        if (src.height() >= minH) return src;
        QImage padded(src.width(), minH, QImage::Format_ARGB32);
        padded.fill(Qt::white);
        QPainter p(&padded);
        p.drawImage(0, (minH - src.height()) / 2, src);
        p.end();
        return padded;
    };
    desktop = padToMinHeight(desktop, 400);
    mobile  = padToMinHeight(mobile,  400);

    const QString relMobile = QStringLiteral("size_chart/size_chart_mobile.png");
    desktop.save(absPath);
    mobile.save(aplusDir.filePath(relMobile));

    APlusVersion ver;
    ver.generated   = QDateTime::currentDateTime();
    ver.desktopFile = relPath;
    ver.mobileFile  = relMobile;
    m_aplusContent->setSingleVersion(QStringLiteral("size_chart"),
                                     APlusElementType::SizeChart,
                                     tr("Size Chart"), ver);
    if (m_aplusModel)
        _rebuildAplusModel();
    _refreshSizeGroupList();
}

// --- A+ workflow helpers -----------------------------------------------------

void PaneSizing::_rebuildAplusModel()
{
    if (!m_aplusModel) return;

    // Save which families (by stable familyId) and versions are expanded.
    QSet<QString>            expandedFamilies;
    QSet<QPair<QString,int>> expandedVersions;

    const int famCount = m_aplusModel->rowCount();
    for (int f = 0; f < famCount; ++f) {
        const QModelIndex famIdx = m_aplusModel->index(f, 0);
        if (!ui->aplusTreeView->isExpanded(famIdx)) continue;
        const QString fid = m_aplusModel->familyIdAt(f);
        if (fid.isEmpty()) continue;
        expandedFamilies.insert(fid);
        for (int v = 0; v < m_aplusModel->rowCount(famIdx); ++v) {
            const QModelIndex verIdx = m_aplusModel->index(v, 0, famIdx);
            if (ui->aplusTreeView->isExpanded(verIdx))
                expandedVersions.insert({fid, v});
        }
    }

    m_aplusModel->rebuild();

    // Restore expansion by matching on stable familyId.
    for (int f = 0; f < m_aplusModel->rowCount(); ++f) {
        const QString fid = m_aplusModel->familyIdAt(f);
        if (!expandedFamilies.contains(fid)) continue;
        const QModelIndex famIdx = m_aplusModel->index(f, 0);
        ui->aplusTreeView->expand(famIdx);
        for (int v = 0; v < m_aplusModel->rowCount(famIdx); ++v) {
            if (expandedVersions.contains({fid, v}))
                ui->aplusTreeView->expand(m_aplusModel->index(v, 0, famIdx));
        }
    }
}

void PaneSizing::_initWorkflowCombo()
{
    for (APlusWorkflow *wf : APlusWorkflow::all())
        ui->comboBoxWorkflow->addItem(wf->name(), wf->id());

    const QString savedId = WorkingDirectoryManager::instance()->settings()
        ->value(QStringLiteral("aplusWorkflow")).toString();
    const int idx = ui->comboBoxWorkflow->findData(savedId);
    ui->comboBoxWorkflow->setCurrentIndex(idx >= 0 ? idx : 0);

    _rebuildPromptTabs();
    _loadWorkflowPrompts();

    connect(ui->comboBoxWorkflow,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                APlusWorkflow *wf = _currentWorkflow();
                if (wf)
                    WorkingDirectoryManager::instance()->settings()
                        ->setValue(QStringLiteral("aplusWorkflow"), wf->id());
                _rebuildPromptTabs();
                _loadWorkflowPrompts();
            });
}

void PaneSizing::_rebuildPromptTabs()
{
    APlusWorkflow *wf = _currentWorkflow();
    if (!wf) return;
    ui->tabWidgetPrompt_01->setTabText(0, wf->stepName(0));
    ui->tabWidgetPrompt_01->setTabText(1, wf->stepName(1));
    const bool hasStep2 = wf->stepCount() >= 3;
    ui->tabWidgetPrompt_01->setTabVisible(2, hasStep2);
    if (hasStep2)
        ui->tabWidgetPrompt_01->setTabText(2, wf->stepName(2));
    // FAQ is always the last tab; its title is set in the .ui file.
}

void PaneSizing::_loadWorkflowPrompts()
{
    APlusWorkflow *wf = _currentWorkflow();
    if (!wf) return;
    auto s = WorkingDirectoryManager::instance()->settings();
    const QString prefix = QStringLiteral("aplus/") + wf->id() + QStringLiteral("/");

    auto load = [&](QTextEdit *ed, int step, const QString &legacyKey = {}) {
        ed->blockSignals(true);
        QString val = s->value(prefix + QStringLiteral("step") + QString::number(step)).toString();
        if (val.isEmpty() && !legacyKey.isEmpty())
            val = s->value(legacyKey).toString();
        ed->setPlainText(val);
        ed->blockSignals(false);
    };

    if (wf->id() == QStringLiteral("generic")) {
        load(ui->textEditPrompt_01, 0, QStringLiteral("aplusPromptDesktop"));
        load(ui->textEditPrompt_02, 1, QStringLiteral("aplusPromptMobile"));
    } else {
        load(ui->textEditPrompt_01, 0);
        load(ui->textEditPrompt_02, 1);
        load(ui->textEditPrompt_03, 2);
    }

    {
        ui->textEditFaqPrompt->blockSignals(true);
        ui->textEditFaqPrompt->setPlainText(
            s->value(QStringLiteral("aplusPromptFaq")).toString());
        ui->textEditFaqPrompt->blockSignals(false);
    }
}

APlusWorkflow *PaneSizing::_currentWorkflow() const
{
    return APlusWorkflow::findById(
        ui->comboBoxWorkflow->currentData().toString());
}

QStringList PaneSizing::_stepInstructions() const
{
    return {
        ui->textEditPrompt_01->toPlainText().trimmed(),
        ui->textEditPrompt_02->toPlainText().trimmed(),
        ui->textEditPrompt_03->toPlainText().trimmed(),
    };
}

using TaskStartFn = std::function<void(int, int, const QString &)>;
using TaskDoneFn  = std::function<void(int, int, const QString &, CliRunResult)>;

static void doRunSequentially(AbstractCli *cli,
                               QPointer<PaneSizing> self,
                               QList<PaneSizing::CliTask> tasks,
                               int step, int total,
                               TaskStartFn onTaskStart,
                               TaskDoneFn  onTaskDone)
{
    if (!self || tasks.isEmpty()) {
        // Sentinel: notify caller that all tasks have completed.
        if (onTaskDone) onTaskDone(total + 1, total, {}, {});
        return;
    }
    PaneSizing::CliTask task = tasks.takeFirst();
    if (onTaskStart) onTaskStart(step, total, task.label);
    if (task.onBefore) task.onBefore();
    const QString taskPrompt = task.promptFn ? task.promptFn() : task.prompt;
    cli->runPromptAsync(taskPrompt, task.workDir, self,
        [self, cli, tasks, task, step, total, onTaskStart, onTaskDone](CliRunResult result) mutable {
            if (!self) return;
            if (task.onDone) task.onDone(result);
            if (onTaskDone) onTaskDone(step, total, task.label, result);
            doRunSequentially(cli, self, std::move(tasks), step + 1, total, onTaskStart, onTaskDone);
        });
}

void PaneSizing::_runSequentially(QList<CliTask> tasks,
                                   TaskStartFn onTaskStart,
                                   TaskDoneFn  onTaskDone)
{
    if (tasks.isEmpty()) return;
    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) return;
    const int total = tasks.size();
    doRunSequentially(cli, this, std::move(tasks), 1, total,
                      std::move(onTaskStart), std::move(onTaskDone));
}

static QString countryCodeToLanguage(const QString &code)
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("fr"), QStringLiteral("French")},
        {QStringLiteral("de"), QStringLiteral("German")},
        {QStringLiteral("it"), QStringLiteral("Italian")},
        {QStringLiteral("es"), QStringLiteral("Spanish")},
        {QStringLiteral("nl"), QStringLiteral("Dutch")},
        {QStringLiteral("se"), QStringLiteral("Swedish")},
        {QStringLiteral("pl"), QStringLiteral("Polish")},
        {QStringLiteral("be"), QStringLiteral("French")},
        {QStringLiteral("mx"), QStringLiteral("Spanish")},
        {QStringLiteral("jp"), QStringLiteral("Japanese")},
        {QStringLiteral("tr"), QStringLiteral("Turkish")},
        {QStringLiteral("uk"), QStringLiteral("English")},
        {QStringLiteral("ie"), QStringLiteral("English")},
        {QStringLiteral("au"), QStringLiteral("English")},
        {QStringLiteral("us"), QStringLiteral("English")},
        {QStringLiteral("ca"), QStringLiteral("English")},
    };
    return map.value(code.toLower().trimmed());
}

// Builds one SizeChartTarget per unique (country-group-row, language) pair
// found in the country list widget. English groups use the group key ("uk"/"com")
// as groupKey; others use the first matching country code.
static QList<PaneSizing::SizeChartTarget> buildSizeChartTargets(
    const AbstractSizeCategory *cat, QListWidget *countriesList)
{
    QList<PaneSizing::SizeChartTarget> result;
    if (!cat) return result;
    const QList<CountryGroup> groups = cat->countryGroups();
    using Key = QPair<int, QString>; // (groupRow, language)
    QSet<Key> seen;

    for (int i = 0; i < countriesList->count(); ++i) {
        const QString code = countriesList->item(i)->text().trimmed().toLower();
        if (code.contains(QLatin1String("(missing)"))) continue;

        int groupRow = -1;
        for (int g = 0; g < groups.size() && groupRow < 0; ++g) {
            const QStringList parts = groups[g].label.split(QLatin1Char('/'));
            for (const QString &part : parts)
                if (part.compare(code, Qt::CaseInsensitive) == 0)
                    { groupRow = g; break; }
        }
        if (groupRow < 0) continue;

        const bool isEnglish = groups[groupRow].isEnglish;
        const QString lang = isEnglish ? QStringLiteral("English")
                                       : countryCodeToLanguage(code);
        if (lang.isEmpty()) continue;

        const Key key{groupRow, lang};
        if (seen.contains(key)) continue;
        seen.insert(key);

        PaneSizing::SizeChartTarget t;
        t.groupKey   = isEnglish ? groups[groupRow].key.toLower() : code;
        t.groupLabel = isEnglish ? groups[groupRow].label         : lang;
        t.groupRow   = groupRow;
        t.language   = lang;
        t.isEnglish  = isEnglish;
        result.append(t);
    }
    return result;
}

// Strips any leading CLI commentary from a FAQ output and returns only the Q&A block.
// The AI sometimes prefixes its answer with progress reports or file-link summaries.
// We detect the first line that starts with "Q" followed by optional whitespace and ":",
// which reliably marks the beginning of the FAQ content regardless of language.
static QString extractFaqContent(const QString &raw)
{
    const QStringList lines = raw.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.length() >= 3 && trimmed[0] == QLatin1Char('Q')
                && (trimmed[1] == QLatin1Char(':') || trimmed[1] == QLatin1Char(' '))) {
            return lines.mid(i).join(QLatin1Char('\n')).trimmed();
        }
    }
    return raw; // no Q: pattern found — return as-is
}

static QString makeFaqFormatPrompt(const QString &text)
{
    return QStringLiteral(
        "Reformat the following Amazon A+ Content FAQ using EXACTLY this structure:\n"
        "Q: [question]\n"
        "A: [answer]\n"
        "\n"
        "Rules:\n"
        "- Every question line starts with 'Q: ' (no other prefix)\n"
        "- Every answer line starts with 'A: ' (no other prefix)\n"
        "- Exactly one blank line between Q/A pairs, none at the start or end\n"
        "- No markdown (no *, **, #, -, numbered lists)\n"
        "- Keep every original question and answer — only reformat\n"
        "Return ONLY the reformatted FAQ. No extra text.\n\n")
        + text;
}

static QString makeFaqValidatePrompt(const QString &text)
{
    return QStringLiteral(
        "Validate the format of this Amazon A+ Content FAQ:\n\n")
        + text
        + QStringLiteral(
        "\n\nChecks:\n"
        "1. Every Q line starts exactly with 'Q: '\n"
        "2. Every A line starts exactly with 'A: '\n"
        "3. Q and A lines alternate correctly (Q then A, Q then A, …)\n"
        "4. Exactly one blank line between each Q/A pair\n"
        "5. No markdown symbols (*, **, #, -, numbered lists)\n"
        "6. Answers are complete sentences (not cut off)\n\n"
        "If ALL checks pass → reply with exactly the word: PASS\n"
        "If ANY check fails → reply with FAIL on the first line, "
        "then the fully corrected FAQ on the following lines.");
}

void PaneSizing::_appendFaqFormatValidateTasks(
    QList<CliTask> &tasks,
    QSharedPointer<QString> textHolder,
    const QString &workDir,
    std::function<void(const QString &)> onFinalText)
{
    auto formatted = QSharedPointer<QString>::create();

    // Format task
    CliTask fmt;
    fmt.label   = tr("FAQ — formatting");
    fmt.workDir = workDir;
    fmt.promptFn = [textHolder]() -> QString {
        if (textHolder->isEmpty()) return QStringLiteral("(nothing to format)");
        return makeFaqFormatPrompt(*textHolder);
    };
    fmt.onDone = [formatted](CliRunResult r) {
        *formatted = extractFaqContent(r.output.trimmed());
    };
    tasks.append(fmt);

    // Validate task — also writes the final text back into *textHolder
    CliTask val;
    val.label   = tr("FAQ — validation");
    val.workDir = workDir;
    val.promptFn = [formatted]() -> QString {
        if (formatted->isEmpty()) return QStringLiteral("(nothing to validate)");
        return makeFaqValidatePrompt(*formatted);
    };
    val.onDone = [textHolder, formatted, onFinalText](CliRunResult r) {
        const QString out = r.output.trimmed();
        QString finalText;
        if (!formatted->isEmpty()) {
            if (out.isEmpty() || out.startsWith(QStringLiteral("PASS"), Qt::CaseInsensitive)) {
                finalText = *formatted;
            } else {
                // FAIL — try to extract corrected FAQ from the response
                const int nl = out.indexOf(QLatin1Char('\n'));
                if (nl >= 0)
                    finalText = extractFaqContent(out.mid(nl + 1).trimmed());
                // No correction provided — fall back to the formatted version
                if (finalText.isEmpty())
                    finalText = *formatted;
            }
        } else if (!textHolder->isEmpty()) {
            // Format step produced nothing — fall back to raw extracted text
            finalText = *textHolder;
        }
        *textHolder = finalText;
        if (onFinalText)
            onFinalText(finalText);
    };
    tasks.append(val);
}

void PaneSizing::onAplusGenerateAll()
{
    if (!m_aplusContent) {
        QMessageBox::information(this, tr("Generate All"),
            tr("Load a product first."));
        return;
    }
    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) {
        QMessageBox::warning(this, tr("Generate All"),
            tr("No CLI tool selected."));
        return;
    }
    const QString description = ui->textEditAttributes->toPlainText().trimmed();

    APlusWorkflow *workflow = _currentWorkflow();
    if (!workflow) {
        QMessageBox::warning(this, tr("Generate All"), tr("No workflow selected."));
        return;
    }

    const QString workDir = m_productWorkingDir.exists()
                          ? m_productWorkingDir.absolutePath() : QString{};
    const QString faqInstructions = ui->textEditFaqPrompt->toPlainText().trimmed();

    // Collect colors from m_colorVariants (focus = first entry)
    QStringList colors;
    for (const auto &[color, urls] : std::as_const(m_colorVariants))
        if (!color.isEmpty())
            colors << color;
    const QString focusColor = colors.isEmpty() ? QString{} : colors.first();

    // Build main image hint (used by the workflow's preamble + by FAQ prompt below)
    const QString mainImageHint = m_mainImageLocalPath.isEmpty() ? QString{}
        : tr("A product photo is available in the working directory as \"%1\". "
             "You may use it as reference.")
          .arg(QFileInfo(m_mainImageLocalPath).fileName());

    const QStringList stepInstrs = _stepInstructions();

    // Build all image slot specs from the workflow.
    const QList<ImageSlotSpec> slotSpecs = workflow->buildSlots(
        m_aplusContent.get(), colors, focusColor,
        description, mainImageHint, stepInstrs);

    // Ensure all destination element directories exist for the planned slots.
    for (const ImageSlotSpec &spec : slotSpecs)
        m_aplusContent->dir().mkpath(spec.elementId);
    if (m_aplusModel) { _rebuildAplusModel(); _rebuildAplusMenu(); }

    // Build FAQ prompt (independent of the workflow image specs)
    const QString imgHintWithGap = mainImageHint.isEmpty()
        ? QString{}
        : mainImageHint + QStringLiteral("\n\n");
    QString faqPrompt = tr("Product:\n") + description + QStringLiteral("\n\n") + imgHintWithGap;
    if (!faqInstructions.isEmpty())
        faqPrompt += tr("Instructions:\n") + faqInstructions + QStringLiteral("\n\n");
    faqPrompt += tr("Generate a concise, engaging Amazon A+ Content FAQ section for "
                    "this product in English. Output as a list of question/answer pairs in plain text.");

    // --- Prompt review dialog ---
    // For clothing: show one representative prompt per workflow step (desktop only).
    // For generic: show desktop + mobile of the first slot, same as before.
    QString groupShotPreview, perColorPreview, detailPreview;
    for (const ImageSlotSpec &spec : slotSpecs) {
        if (spec.elementId == QStringLiteral("image_group") && groupShotPreview.isEmpty())
            groupShotPreview = spec.desktopPrompt;
        else if (spec.elementId.startsWith(QStringLiteral("image_color_")) && perColorPreview.isEmpty())
            perColorPreview = spec.desktopPrompt;
        else if (spec.elementId == QStringLiteral("image_detail") && detailPreview.isEmpty())
            detailPreview = spec.desktopPrompt;
    }
    const int imageSlotCount = slotSpecs.size();

    QDialog reviewDlg(this);
    reviewDlg.setWindowTitle(tr("Review prompts — %1").arg(cli->getName()));
    reviewDlg.resize(750, 520);
    auto *dlgLayout = new QVBoxLayout(&reviewDlg);

    const QString summary = tr("Will generate %1 image slot(s) × 2 (desktop + mobile) = %2 images, plus FAQ.")
        .arg(imageSlotCount).arg(imageSlotCount * 2);
    dlgLayout->addWidget(new QLabel(summary, &reviewDlg));

    auto *tabs = new QTabWidget(&reviewDlg);
    if (workflow->id() == QStringLiteral("clothing")) {
        if (!groupShotPreview.isEmpty()) {
            auto *ed = new QTextEdit(); ed->setPlainText(groupShotPreview);
            tabs->addTab(ed, tr("Group Shot (example)"));
        }
        if (!perColorPreview.isEmpty()) {
            auto *ed = new QTextEdit(); ed->setPlainText(perColorPreview);
            tabs->addTab(ed, tr("Per-Color (example)"));
        }
        if (!detailPreview.isEmpty()) {
            auto *ed = new QTextEdit(); ed->setPlainText(detailPreview);
            tabs->addTab(ed, tr("Detail / Fabric"));
        }
    } else {
        if (!slotSpecs.isEmpty()) {
            auto *desktopEdit = new QTextEdit(); desktopEdit->setPlainText(slotSpecs.first().desktopPrompt);
            auto *mobileEdit  = new QTextEdit(); mobileEdit->setPlainText(slotSpecs.first().mobilePrompt);
            tabs->addTab(desktopEdit, tr("Desktop image"));
            tabs->addTab(mobileEdit,  tr("Mobile image"));
        }
    }
    auto *faqEdit = new QTextEdit(); faqEdit->setPlainText(faqPrompt);
    tabs->addTab(faqEdit, tr("FAQ"));
    dlgLayout->addWidget(tabs);

    auto *btns = new QDialogButtonBox(&reviewDlg);
    btns->addButton(tr("Generate All"), QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &reviewDlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &reviewDlg, &QDialog::reject);
    dlgLayout->addWidget(btns);

    if (reviewDlg.exec() != QDialog::Accepted)
        return;

    // FAQ prompt may have been edited by the user in the dialog.
    const QString finalFaq = faqEdit->toPlainText();

    // Collect unique non-English target languages from available countries
    QList<QPair<QString,QString>> targetLangs; // (countryCode, "French" / "German" / ...)
    {
        QSet<QString> seen;
        for (int i = 0; i < ui->listWidgetCountries->count(); ++i) {
            const QString code = ui->listWidgetCountries->item(i)->text().trimmed();
            if (code.contains(QStringLiteral("(missing)"))) continue;
            const QString lang = countryCodeToLanguage(code);
            if (lang.isEmpty() || lang == QLatin1String("English") || seen.contains(lang)) continue;
            seen.insert(lang);
            targetLangs.append({code, lang});
        }
    }

    // --- Build sequential task list ---
    QList<CliTask> tasks;

    // Accumulates absolute paths of every image file produced, for the assessment step.
    auto generatedImages = QSharedPointer<QStringList>::create();

    // One desktop + mobile task pair per workflow slot
    for (const ImageSlotSpec &spec : slotSpecs) {
        const QString elemId = spec.elementId;
        const QString displayName = spec.displayName;
        const QDir elemDir(m_aplusContent->dir().filePath(elemId));
        elemDir.mkpath(QStringLiteral("."));
        const QString elemWorkDir = elemDir.absolutePath();

        auto filePair  = QSharedPointer<QPair<QString,QString>>::create();
        auto beforeSnap = QSharedPointer<QStringList>::create();

        CliTask desktopTask;
        desktopTask.label   = tr("Desktop image — %1").arg(displayName);
        desktopTask.prompt  = spec.desktopPrompt;
        desktopTask.workDir = elemWorkDir;
        desktopTask.onBefore = [beforeSnap, elemDir]() {
            *beforeSnap = elemDir.entryList({QStringLiteral("*.png"),
                QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
        };
        desktopTask.onDone = [this, elemDir, beforeSnap, filePair, elemId,
                               generatedImages](CliRunResult r) {
            const QString preferred = elemDir.filePath(QStringLiteral("desktop.png"));
            if (QFileInfo::exists(preferred)) {
                filePair->first = preferred;
            } else {
                for (const QString &f : elemDir.entryList(
                         {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")},
                         QDir::Files)) {
                    if (!beforeSnap->contains(f)) { filePair->first = elemDir.filePath(f); break; }
                }
            }
            if (filePair->first.isEmpty() && !r.output.trimmed().isEmpty()) {
                const QString p = elemDir.filePath(QStringLiteral("v_") + _aplusTimestamp()
                                                   + QStringLiteral("_desktop.txt"));
                QFile f(p); if (f.open(QIODevice::WriteOnly)) f.write(r.output.toUtf8());
                filePair->first = p;
            }
            if (!filePair->first.isEmpty())
                generatedImages->append(filePair->first);
        };
        tasks.append(desktopTask);

        CliTask mobileTask;
        mobileTask.label   = tr("Mobile image — %1").arg(displayName);
        mobileTask.prompt  = spec.mobilePrompt;
        mobileTask.workDir = elemWorkDir;
        mobileTask.onBefore = [beforeSnap, elemDir]() {
            *beforeSnap = elemDir.entryList({QStringLiteral("*.png"),
                QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
        };
        mobileTask.onDone = [this, elemDir, beforeSnap, filePair, elemId, displayName,
                              generatedImages](CliRunResult r) {
            const QString preferred = elemDir.filePath(QStringLiteral("mobile.png"));
            if (QFileInfo::exists(preferred)) {
                filePair->second = preferred;
            } else {
                for (const QString &f : elemDir.entryList(
                         {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")},
                         QDir::Files)) {
                    if (!beforeSnap->contains(f)) { filePair->second = elemDir.filePath(f); break; }
                }
            }
            if (filePair->second.isEmpty() && !r.output.trimmed().isEmpty()) {
                const QString p = elemDir.filePath(QStringLiteral("v_") + _aplusTimestamp()
                                                   + QStringLiteral("_mobile.txt"));
                QFile f(p); if (f.open(QIODevice::WriteOnly)) f.write(r.output.toUtf8());
                filePair->second = p;
            }
            if (!filePair->second.isEmpty())
                generatedImages->append(filePair->second);

            if (!m_aplusContent) return;
            const QDir aplusDir = m_aplusContent->dir();
            APlusVersion ver;
            ver.generated   = QDateTime::currentDateTime();
            ver.desktopFile = aplusDir.relativeFilePath(filePair->first);
            ver.mobileFile  = aplusDir.relativeFilePath(filePair->second);
            m_aplusContent->pushVersion(elemId, APlusElementType::Image, displayName, ver);
            if (m_aplusModel) _rebuildAplusModel();
        };
        tasks.append(mobileTask);
    }

    // FAQ task — generates English FAQ and stores the result for translation tasks.
    // Codex exec writes output to a file in workDir rather than stdout, so we snapshot
    // the directory before the task and pick up any new .txt file as fallback.
    auto englishFaqText = QSharedPointer<QString>::create();
    auto faqDirSnap     = QSharedPointer<QSet<QString>>::create();
    CliTask faqTask;
    faqTask.label   = tr("FAQ (English)");
    faqTask.prompt  = finalFaq;
    faqTask.workDir = workDir;
    faqTask.onBefore = [faqDirSnap, workDir]() {
        if (workDir.isEmpty()) return;
        for (const QString &f : QDir(workDir).entryList(
                 {QStringLiteral("*.txt"), QStringLiteral("*.md")}, QDir::Files))
            faqDirSnap->insert(f);
    };
    faqTask.onDone  = [englishFaqText, faqDirSnap, workDir](CliRunResult r) {
        QString text = r.output.trimmed();
        if (text.isEmpty() && !workDir.isEmpty()) {
            for (const QString &f : QDir(workDir).entryList(
                     {QStringLiteral("*.txt"), QStringLiteral("*.md")}, QDir::Files)) {
                if (!faqDirSnap->contains(f)) {
                    QFile fFile(QDir(workDir).filePath(f));
                    if (fFile.open(QIODevice::ReadOnly | QIODevice::Text))
                        text = QString::fromUtf8(fFile.readAll()).trimmed();
                    break;
                }
            }
        }
        *englishFaqText = extractFaqContent(text);
    };
    tasks.append(faqTask);
    _appendFaqFormatValidateTasks(tasks, englishFaqText, workDir,
        [this](const QString &finalText) {
            if (finalText.isEmpty() || !m_aplusContent) return;
            QDir aplusDir = m_aplusContent->dir();
            aplusDir.mkpath(QStringLiteral("faq"));
            const QString relPath = QStringLiteral("faq/v_") + _aplusTimestamp()
                                  + QStringLiteral(".txt");
            QFile f(aplusDir.filePath(relPath));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                f.write(finalText.toUtf8());
            APlusVersion ver;
            ver.generated   = QDateTime::currentDateTime();
            ver.desktopFile = ver.mobileFile = relPath;
            m_aplusContent->pushVersion(QStringLiteral("faq_en"), APlusElementType::Faq,
                                        tr("FAQ (English)"), ver);
            if (m_aplusModel) _rebuildAplusModel();
        });

    // FAQ translation tasks — one translate + format + validate per target language
    for (const auto &[langCode, langName] : std::as_const(targetLangs)) {
        auto rawTransHolder = QSharedPointer<QString>::create();
        CliTask transTask;
        transTask.label  = tr("FAQ — %1").arg(langName);
        transTask.workDir = workDir;
        const QString capturedLangCode = langCode;
        const QString capturedLangName = langName;
        transTask.promptFn = [englishFaqText, capturedLangName]() -> QString {
            const QString base = *englishFaqText;
            if (base.isEmpty())
                return QStringLiteral("(No English FAQ available to translate.)");
            return QStringLiteral("Translate the following Amazon A+ Content FAQ to ")
                   + capturedLangName
                   + QStringLiteral(". Keep the question/answer format. "
                                    "Return only the translated text, no extra commentary.\n\n")
                   + base;
        };
        transTask.onDone = [rawTransHolder](CliRunResult r) {
            *rawTransHolder = extractFaqContent(r.output.trimmed());
        };
        tasks.append(transTask);
        _appendFaqFormatValidateTasks(tasks, rawTransHolder, workDir,
            [this, capturedLangCode, capturedLangName](const QString &finalText) {
                if (finalText.isEmpty() || !m_aplusContent) return;
                QDir aplusDir = m_aplusContent->dir();
                aplusDir.mkpath(QStringLiteral("faq"));
                const QString relPath = QStringLiteral("faq/v_") + _aplusTimestamp()
                                      + QStringLiteral("_") + capturedLangCode
                                      + QStringLiteral(".txt");
                QFile f(aplusDir.filePath(relPath));
                if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                    f.write(finalText.toUtf8());
                APlusVersion ver;
                ver.generated   = QDateTime::currentDateTime();
                ver.desktopFile = ver.mobileFile = relPath;
                const QString elemId = QStringLiteral("faq_") + capturedLangCode;
                m_aplusContent->pushVersion(elemId, APlusElementType::Faq,
                                            tr("FAQ (%1)").arg(capturedLangName), ver);
                if (m_aplusModel) _rebuildAplusModel();
            });
    }

    // Size chart tasks — one per country group
    if (m_sizeTableModel) {
        const auto *cat = _currentCategory();
        const QList<SizeChartTarget> chartTargets = buildSizeChartTargets(cat, ui->listWidgetCountries);

        // English groups: render synchronously now (no AI)
        for (const SizeChartTarget &t : chartTargets) {
            if (!t.isEnglish) continue;
            _renderAndSaveChart(cat, t.groupRow,
                                QStringLiteral("size_chart_") + t.groupKey,
                                t.language, {}, true);
        }

        // Non-English: add CLI translation tasks to the queue
        QList<SizeChartTarget> nonEnglishCharts;
        for (const SizeChartTarget &t : chartTargets)
            if (!t.isEnglish) nonEnglishCharts.append(t);
        if (!nonEnglishCharts.isEmpty()) {
            QStringList origRowLabels;
            for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
                auto *it = m_sizeTableModel->item(row, 0);
                origRowLabels << (it ? it->text() : QString{});
            }
            tasks.append(_buildSizeChartTranslationTasks(nonEnglishCharts, origRowLabels));
        }
    }

    // Assessment runs via the onTaskDone sentinel (step==total+1) after all content tasks.

    // --- Progress dialog ---
    auto *progressDlg = new QDialog(this);
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowTitle(tr("Generating A+ content — %1").arg(cli->getName()));
    progressDlg->resize(560, 420);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, tasks.size() + 1); // +1 for the assessment step
    progressBar->setValue(0);
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btnLayout = new QHBoxLayout();
    auto *copyBtn = new QPushButton(tr("Copy log"), progressDlg);
    btnLayout->addWidget(copyBtn);
    btnLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    btnLayout->addWidget(closeBtns);
    pLayout->addLayout(btnLayout);

    auto *startOverBtn = new QPushButton(tr("Start Over"), progressDlg);
    startOverBtn->setEnabled(false);
    btnLayout->insertWidget(0, startOverBtn);
    QPointer<QPushButton> startOverPtr(startOverBtn);

    QPointer<PaneSizing> restartGuard(this);
    connect(startOverBtn, &QPushButton::clicked, progressDlg,
        [progressDlg, restartGuard]() {
            progressDlg->close();
            if (restartGuard)
                QTimer::singleShot(0, restartGuard,
                    [restartGuard]() { if (restartGuard) restartGuard->onAplusGenerateAll(); });
        });

    connect(copyBtn, &QPushButton::clicked, progressDlg, [logEdit]() {
        QGuiApplication::clipboard()->setText(logEdit->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);
    progressDlg->show();

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };

    auto onStart = [statusLabelPtr, progressBarPtr, appendLog]
                   (int step, int total, const QString &label) {
        if (statusLabelPtr) statusLabelPtr->setText(
            QObject::tr("Step %1 of %2: %3").arg(step).arg(total).arg(label));
        if (progressBarPtr) progressBarPtr->setValue(step - 1);
        appendLog(QObject::tr("▶ %1").arg(label));
    };

    QPointer<PaneSizing>   selfPtr(this);
    auto onDone = [selfPtr, statusLabelPtr, progressBarPtr, logEditPtr, appendLog,
                   generatedImages, workDir, startOverPtr]
                  (int step, int total, const QString &label, CliRunResult result) mutable {
        if (step == total + 1) {
            // All content tasks done — run assessment.
            if (statusLabelPtr) statusLabelPtr->setText(
                QObject::tr("Step %1 of %2: %3").arg(total + 1).arg(total + 1)
                            .arg(QObject::tr("Assessing images…")));
            appendLog(QObject::tr("▶ Assessing generated images…"));

            if (!selfPtr) return;
            AbstractCli *assessCli =
                selfPtr->ui->comboBoxCli->currentData().value<AbstractCli *>();
            if (!assessCli) {
                appendLog(QObject::tr("⚠ No CLI available for assessment."));
                if (statusLabelPtr) statusLabelPtr->setText(QObject::tr("Done."));
                if (progressBarPtr) progressBarPtr->setValue(progressBarPtr->maximum());
                if (startOverPtr) startOverPtr->setEnabled(true);
                return;
            }

            // Build the assessment prompt now (generatedImages is fully populated)
            QString p = QStringLiteral(
                "You just generated Amazon A+ content images. "
                "Please verify the following output files:\n\n");
            if (generatedImages->isEmpty()) {
                p += QStringLiteral("(no image files were recorded — generation may have failed)\n");
            } else {
                for (const QString &path : std::as_const(*generatedImages)) {
                    const bool exists = QFileInfo::exists(path);
                    p += (exists ? QStringLiteral("  [EXISTS]  ")
                                 : QStringLiteral("  [MISSING] "))
                         + path + QLatin1Char('\n');
                }
            }
            p += QStringLiteral(
                "\nFor each [EXISTS] image:\n"
                "1. Confirm it is a valid, non-empty image file.\n"
                "2. Briefly describe its content and whether it looks like a proper "
                   "Amazon A+ marketing image.\n"
                "3. Flag any file that looks wrong or is unexpectedly small.\n"
                "\nFor each [MISSING] file, explain what likely went wrong.\n");

            // List any FAQ files for assessment
            p += QStringLiteral("\nAlso check the FAQ files:\n");
            if (selfPtr && selfPtr->m_aplusContent) {
                const QDir aplusDir = selfPtr->m_aplusContent->dir();
                const QDir faqDir(aplusDir.filePath(QStringLiteral("faq")));
                if (faqDir.exists()) {
                    const QStringList faqFiles = faqDir.entryList(
                        {QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
                    for (const QString &f : faqFiles)
                        p += QStringLiteral("  ") + faqDir.absoluteFilePath(f)
                             + QLatin1Char('\n');
                }
            }
            p += QStringLiteral(
                "For each FAQ: confirm it reads naturally in the correct language "
                "and is relevant to the product.\n");

            p += QStringLiteral(
                "\nFinish with a one-line summary: PASS (all content OK) or FAIL (issues found).");

            assessCli->runPromptAsync(p, workDir, selfPtr,
                [statusLabelPtr, progressBarPtr, logEditPtr, appendLog, startOverPtr]
                (CliRunResult assessResult) {
                    const QString out = assessResult.output.trimmed();
                    const QString display = out.isEmpty()
                                         ? assessResult.errorOutput.trimmed() : out;
                    if (!display.isEmpty())
                        appendLog(QStringLiteral("Assessment:\n") + display);
                    else
                        appendLog(QObject::tr("(assessment produced no output)"));

                    if (statusLabelPtr) statusLabelPtr->setText(QObject::tr("All done!"));
                    if (progressBarPtr) progressBarPtr->setValue(progressBarPtr->maximum());
                    if (startOverPtr) startOverPtr->setEnabled(true);
                });
            return;
        }

        // Regular task completed
        if (!result.processStarted) {
            appendLog(QObject::tr("✗ Failed to start CLI for: %1").arg(label));
        } else {
            const qint64 secs = result.durationMs / 1000;
            appendLog(QObject::tr("✓ Done (%1s): %2").arg(secs).arg(label));
            if (!result.errorOutput.isEmpty())
                appendLog(QObject::tr("  stderr: %1")
                          .arg(result.errorOutput.left(200).trimmed()));
        }
        if (progressBarPtr) progressBarPtr->setValue(step);
    };

    _runSequentially(std::move(tasks), std::move(onStart), std::move(onDone));
}

void PaneSizing::_refreshSizeGroupList()
{
    // Preserve the current selection by label text so we can restore it after rebuilding.
    const QString prevLabel = ui->listWidgetSizeGroups->currentItem()
                            ? ui->listWidgetSizeGroups->currentItem()->text()
                            : QString{};

    // clear() emits currentRowChanged(-1); onGroupImageSelected guards row < 0 → no-op.
    ui->listWidgetSizeGroups->clear();
    m_groupImages.clear();

    if (!m_aplusContent)
        return;

    QHash<QString, QImage> generatedImages;
    for (const APlusElement &e : m_aplusContent->elements()) {
        if (e.id != QLatin1String("size_chart") && !e.id.startsWith(QLatin1String("size_chart_")))
            continue;
        const APlusVersion *ver = e.current();
        if (!ver) continue;
        const QImage img(m_aplusContent->dir().filePath(ver->desktopFile));
        if (!img.isNull())
            generatedImages.insert(e.id, img);
    }

    const QImage defaultImg = generatedImages.value(QStringLiteral("size_chart"));
    if (defaultImg.isNull())
        return;

    // addItem() does NOT auto-select, so no currentRowChanged fires here.
    auto addEntry = [&](const QString &label, const QImage &img) {
        m_groupImages << img;
        ui->listWidgetSizeGroups->addItem(label);
    };

    addEntry(tr("Size Chart"), defaultImg);

    const auto *cat = _currentCategory();
    const QList<SizeChartTarget> targets = buildSizeChartTargets(cat, ui->listWidgetCountries);
    for (const SizeChartTarget &t : targets) {
        const QString elemId = QStringLiteral("size_chart_") + t.groupKey;
        const QString label = t.isEnglish
            ? tr("Size Chart (%1)").arg(t.groupLabel)
            : tr("Size Chart (%1)").arg(t.language);
        addEntry(label, generatedImages.contains(elemId) ? generatedImages[elemId] : defaultImg);
    }

    int restoreRow = 0;
    if (!prevLabel.isEmpty()) {
        for (int i = 0; i < ui->listWidgetSizeGroups->count(); ++i) {
            if (ui->listWidgetSizeGroups->item(i)->text() == prevLabel) {
                restoreRow = i;
                break;
            }
        }
    }
    // setCurrentRow fires currentRowChanged(restoreRow) → onGroupImageSelected sets pixmap.
    ui->listWidgetSizeGroups->setCurrentRow(restoreRow);
}

static QImage padImageToMinHeight(const QImage &src, int minH,
                                   const QColor &fill = Qt::white)
{
    if (src.height() >= minH) return src;
    QImage padded(src.width(), minH, QImage::Format_ARGB32);
    padded.fill(fill);
    QPainter p(&padded);
    p.drawImage(0, (minH - src.height()) / 2, src);  // centered
    p.end();
    return padded;
}

void PaneSizing::_renderAndSaveChart(const AbstractSizeCategory *cat,
                                      int groupRow,
                                      const QString &elemId,
                                      const QString &displayLang,
                                      const QStringList &translatedLabels,
                                      bool keepInches)
{
    if (!m_aplusContent || !m_sizeTableModel || !cat) return;

    const QList<CountryGroup> groups = cat->countryGroups();
    const int groupCount = groups.size();

    // Optionally swap row labels (column 0) for translated charts
    QStringList savedLabels;
    if (!translatedLabels.isEmpty()) {
        for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
            auto *it = m_sizeTableModel->item(row, 0);
            savedLabels << (it ? it->text() : QString{});
        }
        for (int row = 0; row < m_sizeTableModel->rowCount() && row < translatedLabels.size(); ++row) {
            if (auto *it = m_sizeTableModel->item(row, 0))
                it->setText(translatedLabels[row].trimmed());
        }
    }

    // Strip " cm / xx in" from data cells for non-English charts
    static const QString kCmSep = QStringLiteral(" cm / ");
    QList<QPair<int,int>> inchCells;
    QStringList savedCellTexts;
    if (!keepInches) {
        for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
            for (int col = 1; col < m_sizeTableModel->columnCount(); ++col) {
                auto *it = m_sizeTableModel->item(row, col);
                if (!it) continue;
                const int sep = it->text().indexOf(kCmSep);
                if (sep >= 0) {
                    inchCells.append({row, col});
                    savedCellTexts << it->text();
                    it->setText(it->text().left(sep) + QStringLiteral(" cm"));
                }
            }
        }
    }

    // Temporarily remove non-target group rows (high→low to keep indices stable)
    using RowData = QList<QStandardItem *>;
    QList<QPair<int, RowData>> removedGroupRows;
    if (groupRow >= 0 && groupRow < groupCount) {
        for (int i = groupCount - 1; i >= 0; --i) {
            if (i == groupRow) continue;
            RowData rowItems;
            for (int c = 0; c < m_sizeTableModel->columnCount(); ++c) {
                auto *it = m_sizeTableModel->item(i, c);
                rowItems << (it ? it->clone() : new QStandardItem());
            }
            removedGroupRows.prepend({i, rowItems});
            m_sizeTableModel->removeRow(i);
        }
    }

    const QImage img = cat->renderImage(m_sizeTableModel);

    // Restore removed group rows (low→high to preserve original positions)
    for (auto &[origIdx, rowItems] : removedGroupRows) {
        m_sizeTableModel->insertRow(origIdx);
        for (int c = 0; c < rowItems.size(); ++c)
            m_sizeTableModel->setItem(origIdx, c, rowItems[c]);
    }
    // Restore row labels
    for (int row = 0; row < savedLabels.size() && row < m_sizeTableModel->rowCount(); ++row) {
        if (auto *it = m_sizeTableModel->item(row, 0))
            it->setText(savedLabels[row]);
    }
    // Restore data cells
    for (int i = 0; i < inchCells.size(); ++i) {
        if (auto *it = m_sizeTableModel->item(inchCells[i].first, inchCells[i].second))
            it->setText(savedCellTexts[i]);
    }

    if (img.isNull()) return;

    const QDir aplusDir = m_aplusContent->dir();
    aplusDir.mkpath(elemId);
    // Fill padding with the table's lighter alternating-row teal so top/bottom
    // breathing room blends with the table instead of showing as white gaps.
    const QColor tableBg(QStringLiteral("#e8f6f3"));
    QImage desktop = padImageToMinHeight(img.scaledToWidth(970, Qt::SmoothTransformation), 400, tableBg);
    QImage mobile  = padImageToMinHeight(img.scaledToWidth(600, Qt::SmoothTransformation), 400, tableBg);
    const QString relD = elemId + QStringLiteral("/size_chart.png");
    const QString relM = elemId + QStringLiteral("/size_chart_mobile.png");
    desktop.save(aplusDir.filePath(relD));
    mobile.save(aplusDir.filePath(relM));

    APlusVersion ver;
    ver.generated   = QDateTime::currentDateTime();
    ver.desktopFile = relD;
    ver.mobileFile  = relM;
    m_aplusContent->setSingleVersion(elemId, APlusElementType::SizeChart,
                                     tr("Size Chart (%1)").arg(displayLang), ver);
    if (m_aplusModel) _rebuildAplusModel();
    _refreshSizeGroupList();
}

QList<PaneSizing::CliTask> PaneSizing::_buildSizeChartTranslationTasks(
    const QList<SizeChartTarget> &targets,
    const QStringList &origRowLabels)
{
    auto origLabelsPtr = QSharedPointer<QStringList>::create(origRowLabels);
    const QString workDir = m_productWorkingDir.exists()
                          ? m_productWorkingDir.absolutePath() : QString{};

    QList<CliTask> tasks;
    for (const SizeChartTarget &t : targets) {
        if (t.isEnglish) continue; // English rendered synchronously without AI

        CliTask chartTask;
        chartTask.label   = tr("Size chart — %1").arg(t.language);
        chartTask.workDir = workDir;
        const QString capturedGroupKey  = t.groupKey;
        const QString capturedLang      = t.language;
        const int     capturedGroupRow  = t.groupRow;

        chartTask.promptFn = [origLabelsPtr, capturedLang]() -> QString {
            QString p = QStringLiteral("Translate the following Amazon size chart row labels to ")
                      + capturedLang
                      + QStringLiteral(".\nReturn ONLY the translated labels, one per line, "
                                       "in the same order. No extra text.\n\n");
            for (const QString &h : std::as_const(*origLabelsPtr))
                p += h + QLatin1Char('\n');
            return p;
        };
        chartTask.onDone = [this, capturedGroupKey, capturedLang, capturedGroupRow](CliRunResult r) {
            if (!m_sizeTableModel) return;
            const auto *cat = _currentCategory();
            if (!cat) return;
            const QStringList lines = r.output.trimmed().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (lines.isEmpty()) return;
            const QString elemId = QStringLiteral("size_chart_") + capturedGroupKey;
            _renderAndSaveChart(cat, capturedGroupRow, elemId, capturedLang, lines, false);
        };
        tasks.append(chartTask);
    }
    return tasks;
}

void PaneSizing::onAplusGenerateSizeChart()
{
    if (!m_generatedSuccessfully) {
        QMessageBox::information(this, tr("Generate Size Chart"),
            tr("Generate a size table first using the Sizing tab."));
        return;
    }

    // Always produce the default (English) size chart.
    _aplusPushSizeChart();

    if (!m_sizeTableModel || !m_aplusContent) return;

    const auto *cat = _currentCategory();
    const QList<SizeChartTarget> targets = buildSizeChartTargets(cat, ui->listWidgetCountries);

    // English groups: render synchronously
    for (const SizeChartTarget &t : targets) {
        if (!t.isEnglish) continue;
        _renderAndSaveChart(cat, t.groupRow,
                            QStringLiteral("size_chart_") + t.groupKey,
                            t.language, {}, true);
    }

    // Non-English: translate via AI CLI
    QList<SizeChartTarget> nonEnglish;
    for (const SizeChartTarget &t : targets)
        if (!t.isEnglish) nonEnglish.append(t);

    if (nonEnglish.isEmpty()) return;

    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) return;

    QStringList origRowLabels;
    for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
        auto *it = m_sizeTableModel->item(row, 0);
        origRowLabels << (it ? it->text() : QString{});
    }

    QList<CliTask> tasks = _buildSizeChartTranslationTasks(nonEnglish, origRowLabels);
    if (tasks.isEmpty()) return;

    const int total = tasks.size();

    auto *progressDlg = new QDialog(this);
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowTitle(tr("Translating size charts — %1").arg(cli->getName()));
    progressDlg->resize(480, 300);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font(); boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, total);
    progressBar->setValue(0);
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    connect(btns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);
    pLayout->addWidget(btns);

    QPointer<QLabel>       labelPtr(statusLabel);
    QPointer<QProgressBar> barPtr(progressBar);
    QPointer<QTextEdit>    logPtr(logEdit);

    progressDlg->show();

    _runSequentially(
        std::move(tasks),
        [labelPtr, barPtr](int step, int total, const QString &label) {
            if (labelPtr) labelPtr->setText(
                QStringLiteral("(%1/%2) %3").arg(step).arg(total).arg(label));
            if (barPtr) barPtr->setValue(step - 1);
        },
        [labelPtr, barPtr, logPtr](int step, int total, const QString &label, CliRunResult r) {
            if (step == total + 1) {
                if (labelPtr) labelPtr->setText(QObject::tr("Done."));
                if (barPtr) barPtr->setValue(total);
                return;
            }
            if (logPtr) {
                const QString ms = QString::number(r.durationMs) + QStringLiteral("ms");
                const QString outcome = r.output.trimmed().isEmpty()
                    ? QStringLiteral("(no output)") : QStringLiteral("ok");
                logPtr->append(
                    QStringLiteral("[%1/%2] %3 — %4 (%5)")
                    .arg(step).arg(total).arg(label).arg(outcome).arg(ms));
            }
        });
}

void PaneSizing::onAplusGenerateFaq()
{
    const QString description = ui->textEditAttributes->toPlainText().trimmed();
    if (description.isEmpty()) {
        QMessageBox::information(this, tr("Generate FAQ"),
            tr("No product description available. Load an ASIN first."));
        return;
    }

    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) {
        QMessageBox::warning(this, tr("Generate FAQ"),
            tr("No AI CLI tool is available. Install Claude or another CLI tool and check Settings."));
        return;
    }

    const QTextEdit *promptEditor = (ui->tabWidgetPrompt_01->currentIndex() == 0)
                                  ? ui->textEditPrompt_01
                                  : ui->textEditPrompt_02;
    const QString userPrompt = promptEditor->toPlainText().trimmed();

    const QDir &effectiveDir = m_productWorkingDir.exists() ? m_productWorkingDir : m_workingDir;
    const QString workDir = effectiveDir.isAbsolute() ? effectiveDir.path() : QString{};

    QString prompt;
    prompt += QStringLiteral("Product description:\n") + description + QStringLiteral("\n\n");
    if (!m_mainImageLocalPath.isEmpty()) {
        const QString imgName = QFileInfo(m_mainImageLocalPath).fileName();
        prompt += QStringLiteral("A product photo is available in the working directory as \"")
                + imgName
                + QStringLiteral("\". You may read it if it helps.\n\n");
    }
    if (!userPrompt.isEmpty())
        prompt += QStringLiteral("Instructions:\n") + userPrompt + QStringLiteral("\n\n");
    prompt += QStringLiteral("Generate a concise, engaging Amazon A+ Content FAQ section for this product in English. "
                             "Output as a list of question/answer pairs in plain text.");

    // --- Prompt review dialog ---
    QDialog reviewDlg(this);
    reviewDlg.setWindowTitle(tr("Review prompt — %1").arg(cli->getName()));
    reviewDlg.resize(700, 450);
    auto *reviewLayout = new QVBoxLayout(&reviewDlg);
    auto *promptEdit = new QTextEdit(&reviewDlg);
    promptEdit->setPlainText(prompt);
    reviewLayout->addWidget(promptEdit);
    auto *reviewBtns = new QDialogButtonBox(&reviewDlg);
    auto *generateBtn = reviewBtns->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    Q_UNUSED(generateBtn)
    reviewBtns->addButton(QDialogButtonBox::Cancel);
    connect(reviewBtns, &QDialogButtonBox::accepted, &reviewDlg, &QDialog::accept);
    connect(reviewBtns, &QDialogButtonBox::rejected, &reviewDlg, &QDialog::reject);
    reviewLayout->addWidget(reviewBtns);

    if (reviewDlg.exec() != QDialog::Accepted)
        return;

    const QString finalPrompt = promptEdit->toPlainText();

    // --- Result dialog ---
    auto *resultDlg = new QDialog(this);
    resultDlg->setAttribute(Qt::WA_DeleteOnClose);
    resultDlg->setWindowTitle(tr("FAQ — %1").arg(cli->getName()));
    resultDlg->resize(700, 500);
    auto *resultLayout = new QVBoxLayout(resultDlg);
    auto *output = new QTextEdit(resultDlg);
    output->setReadOnly(true);
    output->setPlainText(tr("Generating FAQ with %1…").arg(cli->getName()));
    resultLayout->addWidget(output);
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, resultDlg);
    connect(closeBtns, &QDialogButtonBox::rejected, resultDlg, &QDialog::reject);
    resultLayout->addWidget(closeBtns);
    resultDlg->show();

    // Save text result to APlusContent when it arrives.
    QPointer<PaneSizing> guard = this;
    auto saveFaqToAplus = [guard](const QString &text) {
        if (!guard || text.isEmpty()) return;
        if (!guard->m_aplusContent) return;
        QDir aplusDir = guard->m_aplusContent->dir();
        aplusDir.mkpath(QStringLiteral("faq"));
        const QString ts = guard->_aplusTimestamp();
        const QString relPath = QStringLiteral("faq/v_") + ts + QStringLiteral(".txt");
        QFile f(aplusDir.filePath(relPath));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            return;
        f.write(text.toUtf8());
        f.close();

        APlusVersion ver;
        ver.generated   = QDateTime::currentDateTime();
        ver.desktopFile = relPath;
        ver.mobileFile  = relPath;
        guard->m_aplusContent->pushVersion(QStringLiteral("faq"),
                                           APlusElementType::Faq,
                                           guard->tr("FAQ"), ver);
        guard->_rebuildAplusModel();

        const int famIdx =
            guard->m_aplusModel->familyIndexForElement(QStringLiteral("faq"));
        if (famIdx >= 0) {
            const QModelIndex familyIndex =
                guard->m_aplusModel->index(famIdx, 0, {});
            guard->ui->aplusTreeView->expand(familyIndex);
            const QModelIndex versionIndex =
                guard->m_aplusModel->index(0, 0, familyIndex);
            if (versionIndex.isValid()) {
                const QModelIndex langIndex =
                    guard->m_aplusModel->index(0, 0, versionIndex);
                if (langIndex.isValid()) {
                    guard->ui->aplusTreeView->expand(versionIndex);
                    guard->ui->aplusTreeView->setCurrentIndex(langIndex);
                    guard->_refreshAplusPreview(langIndex);
                } else {
                    guard->ui->aplusTreeView->setCurrentIndex(versionIndex);
                    guard->_refreshAplusPreview(versionIndex);
                }
            }
        }
    };

    // Snapshot directory before the CLI runs so we can detect files it creates.
    QSet<QString> faqDirSnap;
    if (!workDir.isEmpty()) {
        for (const QString &f : QDir(workDir).entryList(
                 {QStringLiteral("*.txt"), QStringLiteral("*.md")}, QDir::Files))
            faqDirSnap.insert(f);
    }

    cli->runPromptAsync(finalPrompt, workDir, resultDlg,
                        [output, saveFaqToAplus, workDir, faqDirSnap, guard](CliRunResult result) {
        if (!result.processStarted) {
            output->setPlainText(QObject::tr("Failed to start CLI process."));
            return;
        }
        QString text = result.output.trimmed();
        if (text.isEmpty() && !workDir.isEmpty()) {
            for (const QString &f : QDir(workDir).entryList(
                     {QStringLiteral("*.txt"), QStringLiteral("*.md")}, QDir::Files)) {
                if (!faqDirSnap.contains(f)) {
                    QFile fFile(QDir(workDir).filePath(f));
                    if (fFile.open(QIODevice::ReadOnly | QIODevice::Text))
                        text = QString::fromUtf8(fFile.readAll()).trimmed();
                    break;
                }
            }
        }
        const QString display = text.isEmpty() ? result.errorOutput.trimmed() : text;
        output->setPlainText(display);
        text = extractFaqContent(text);

        if (text.isEmpty() || !guard) return;

        auto textHolder = QSharedPointer<QString>::create(text);

        QList<PaneSizing::CliTask> fvTasks;
        guard->_appendFaqFormatValidateTasks(fvTasks, textHolder, workDir,
            [saveFaqToAplus, guard, workDir, textHolder](const QString &finalText) {
                saveFaqToAplus(finalText);
                if (finalText.isEmpty() || !guard) return;

                // Collect unique non-English target languages
                QList<QPair<QString,QString>> targetLangs;
                {
                    QSet<QString> seen;
                    for (int i = 0; i < guard->ui->listWidgetCountries->count(); ++i) {
                        const QString code =
                            guard->ui->listWidgetCountries->item(i)->text().trimmed();
                        if (code.contains(QStringLiteral("(missing)"))) continue;
                        const QString lang = countryCodeToLanguage(code);
                        if (lang.isEmpty() || seen.contains(lang)) continue;
                        seen.insert(lang);
                        targetLangs.append({code, lang});
                    }
                }
                if (targetLangs.isEmpty()) return;

                QList<PaneSizing::CliTask> transTasks;
                for (const auto &[langCode, langName] : std::as_const(targetLangs)) {
                    auto rawHolder = QSharedPointer<QString>::create();
                    PaneSizing::CliTask transTask;
                    transTask.label   = QObject::tr("FAQ — %1").arg(langName);
                    transTask.workDir = workDir;
                    const QString cLC = langCode;
                    const QString cLN = langName;
                    transTask.prompt  =
                        QStringLiteral("Translate the following Amazon A+ Content FAQ to ")
                        + cLN
                        + QStringLiteral(". Keep the question/answer format. "
                                         "Return only the translated text, no extra commentary.\n\n")
                        + finalText;
                    transTask.onDone = [rawHolder](CliRunResult r) {
                        *rawHolder = extractFaqContent(r.output.trimmed());
                    };
                    transTasks.append(transTask);
                    guard->_appendFaqFormatValidateTasks(transTasks, rawHolder, workDir,
                        [guard, cLC, cLN](const QString &ft) {
                            if (ft.isEmpty() || !guard || !guard->m_aplusContent) return;
                            QDir aplusDir = guard->m_aplusContent->dir();
                            aplusDir.mkpath(QStringLiteral("faq"));
                            const QString relPath = QStringLiteral("faq/v_")
                                                  + guard->_aplusTimestamp()
                                                  + QStringLiteral("_") + cLC
                                                  + QStringLiteral(".txt");
                            QFile f(aplusDir.filePath(relPath));
                            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                                f.write(ft.toUtf8());
                            APlusVersion ver;
                            ver.generated   = QDateTime::currentDateTime();
                            ver.desktopFile = ver.mobileFile = relPath;
                            const QString elemId = QStringLiteral("faq_") + cLC;
                            guard->m_aplusContent->pushVersion(
                                elemId, APlusElementType::Faq,
                                guard->tr("FAQ (%1)").arg(cLN), ver);
                            if (guard->m_aplusModel) guard->_rebuildAplusModel();
                        });
                }
                guard->_runSequentially(std::move(transTasks));
            });
        guard->_runSequentially(std::move(fvTasks));
    });
}

void PaneSizing::onAplusGenerateImage(const QString &elementId)
{
    if (!m_aplusContent) return;

    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli || !cli->canGenImages()) {
        QMessageBox::warning(this, tr("Generate Image"),
            tr("Selected CLI cannot generate images. Pick a CLI with image generation support."));
        return;
    }

    const QString description = ui->textEditAttributes->toPlainText().trimmed();
    const QTextEdit *promptEditor = (ui->tabWidgetPrompt_01->currentIndex() == 0)
                                  ? ui->textEditPrompt_01
                                  : ui->textEditPrompt_02;
    const QString userPrompt = promptEditor->toPlainText().trimmed();

    QString prompt;
    if (!description.isEmpty())
        prompt += QStringLiteral("Product description:\n") + description + QStringLiteral("\n\n");
    if (!userPrompt.isEmpty())
        prompt += QStringLiteral("Instructions:\n") + userPrompt + QStringLiteral("\n\n");
    prompt += QStringLiteral(
        "Generate a professional Amazon A+ content marketing image for this product. "
        "Output as desktop.png (970x600 landscape, white background) and "
        "mobile.png (600x600 square) in the working directory.");

    // Snapshot existing image files so we can detect new ones after CLI runs.
    QDir aplusDir = m_aplusContent->dir();
    aplusDir.mkpath(elementId);
    const QDir elementDir(aplusDir.filePath(elementId));
    const QStringList nameFilters = {
        QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")
    };
    QSet<QString> existingBefore;
    for (const QString &f : elementDir.entryList(nameFilters, QDir::Files))
        existingBefore.insert(f);

    // --- Prompt review dialog ---
    QDialog reviewDlg(this);
    reviewDlg.setWindowTitle(tr("Review prompt — %1").arg(cli->getName()));
    reviewDlg.resize(700, 450);
    auto *reviewLayout = new QVBoxLayout(&reviewDlg);
    auto *promptEdit = new QTextEdit(&reviewDlg);
    promptEdit->setPlainText(prompt);
    reviewLayout->addWidget(promptEdit);
    auto *reviewBtns = new QDialogButtonBox(&reviewDlg);
    auto *generateBtn = reviewBtns->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    Q_UNUSED(generateBtn)
    reviewBtns->addButton(QDialogButtonBox::Cancel);
    connect(reviewBtns, &QDialogButtonBox::accepted, &reviewDlg, &QDialog::accept);
    connect(reviewBtns, &QDialogButtonBox::rejected, &reviewDlg, &QDialog::reject);
    reviewLayout->addWidget(reviewBtns);

    if (reviewDlg.exec() != QDialog::Accepted)
        return;

    const QString finalPrompt = promptEdit->toPlainText();
    const QString workDir = elementDir.absolutePath();

    // --- Result dialog ---
    auto *resultDlg = new QDialog(this);
    resultDlg->setAttribute(Qt::WA_DeleteOnClose);
    resultDlg->setWindowTitle(tr("Image — %1").arg(cli->getName()));
    resultDlg->resize(700, 500);
    auto *resultLayout = new QVBoxLayout(resultDlg);
    auto *output = new QTextEdit(resultDlg);
    output->setReadOnly(true);
    output->setPlainText(tr("Generating image with %1…").arg(cli->getName()));
    resultLayout->addWidget(output);
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, resultDlg);
    connect(closeBtns, &QDialogButtonBox::rejected, resultDlg, &QDialog::reject);
    resultLayout->addWidget(closeBtns);
    resultDlg->show();

    QPointer<PaneSizing> guard = this;
    const QString capturedId = elementId;
    const QString capturedDisplayName =
        m_aplusContent->findElement(elementId)
            ? m_aplusContent->findElement(elementId)->displayName
            : elementId;

    cli->runPromptAsync(finalPrompt, workDir, resultDlg,
                        [guard, output, capturedId, capturedDisplayName, elementDir, existingBefore]
                        (CliRunResult result) {
        if (!guard) return;

        if (!result.processStarted) {
            output->setPlainText(QObject::tr("Failed to start CLI process."));
            return;
        }
        const QString text = result.output.trimmed();
        output->setPlainText(text.isEmpty() ? result.errorOutput.trimmed() : text);

        if (!guard->m_aplusContent) return;

        // Look for new image files created by the CLI.
        const QStringList nameFilters = {
            QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")
        };
        QStringList newFiles;
        for (const QString &f : elementDir.entryList(nameFilters, QDir::Files)) {
            if (!existingBefore.contains(f))
                newFiles << f;
        }

        if (!newFiles.isEmpty()) {
            const QImage img(elementDir.absoluteFilePath(newFiles.first()));
            if (!img.isNull()) {
                guard->_aplusPushImage(img, capturedId, capturedDisplayName,
                                       APlusElementType::Image);
                return;
            }
        }

        // No new image — save text output as a fallback version.
        if (!text.isEmpty()) {
            QDir aplusDir = guard->m_aplusContent->dir();
            aplusDir.mkpath(capturedId);
            const QString ts = guard->_aplusTimestamp();
            const QString relPath = capturedId + QStringLiteral("/v_") + ts + QStringLiteral(".txt");
            QFile f(aplusDir.filePath(relPath));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                f.write(text.toUtf8());
                f.close();
                APlusVersion ver;
                ver.generated   = QDateTime::currentDateTime();
                ver.desktopFile = relPath;
                ver.mobileFile  = relPath;
                guard->m_aplusContent->pushVersion(capturedId,
                                                   APlusElementType::Image,
                                                   capturedDisplayName, ver);
                guard->_rebuildAplusModel();
            }
        }
    });
}

void PaneSizing::onAplusDeleteVersion()
{
    if (!m_aplusContent || !m_aplusModel) return;
    const QModelIndex idx = ui->aplusTreeView->currentIndex();
    const APlusTreeModel::Location loc = m_aplusModel->locate(idx);
    if (!loc.isVersion() && !loc.isLanguage()) return;

    const int elemIdx = m_aplusModel->elementIndexForLocation(loc);
    const QList<APlusElement> &els = m_aplusContent->elements();
    if (elemIdx < 0 || elemIdx >= els.size()) return;
    const QString id = els.at(elemIdx).id;

    m_aplusContent->deleteVersion(id, loc.version);
    _rebuildAplusModel();
    _refreshAplusPreview(ui->aplusTreeView->currentIndex());
}

void PaneSizing::onAplusAddImageSlot()
{
    if (!m_aplusContent) return;
    int count = 0;
    for (const APlusElement &e : m_aplusContent->elements())
        if (e.type == APlusElementType::Image)
            ++count;
    m_aplusContent->ensureImageElement(count);
    _rebuildAplusModel();
    _rebuildAplusMenu();
}

void PaneSizing::onAplusTreeClicked(const QModelIndex &idx)
{
    if (!idx.isValid()) return;
    if (idx.column() == APlusTreeModel::Desktop) {
        m_aplusDesktop = true;
        ui->buttonAplusDesktop->setChecked(true);
        ui->buttonAplusMobile->setChecked(false);
    } else if (idx.column() == APlusTreeModel::Mobile) {
        m_aplusDesktop = false;
        ui->buttonAplusMobile->setChecked(true);
        ui->buttonAplusDesktop->setChecked(false);
    }
    _refreshAplusPreview(idx);
}

void PaneSizing::onAplusSelectionChanged(const QModelIndex &current,
                                         const QModelIndex &previous)
{
    Q_UNUSED(previous)
    bool deletable = false;
    if (m_aplusModel && current.isValid()) {
        const auto loc = m_aplusModel->locate(current);
        deletable = loc.isVersion() || loc.isLanguage();
    }
    ui->buttonAplusDeleteVersion->setEnabled(deletable);
    _refreshAplusPreview(current);
}

void PaneSizing::_updateLangCombo(const QString &, const QString &)
{
    ui->comboBoxAplusLanguage->setVisible(false);
}

void PaneSizing::_refreshAplusPreview(const QModelIndex &idx)
{
    if (!m_aplusModel || !idx.isValid()) {
        _showAplusFile({});
        return;
    }
    const APlusTreeModel::Location loc = m_aplusModel->locate(idx);
    if (!loc.isValid()) {
        _showAplusFile({});
        return;
    }
    const QString absPath = m_aplusModel->absoluteFilePath(loc, m_aplusDesktop);
    _showAplusFile(absPath);
}

void PaneSizing::_showAplusFile(const QString &absPath)
{
    if (absPath.isEmpty()) {
        ui->labelAplusPreview->clear();
        ui->aplusPreviewStack->setCurrentIndex(0);
        return;
    }

    if (absPath.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive)
            || absPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        QFile f(absPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(f.readAll());
            ui->textEditAplusPreview->setPlainText(content);
            f.close();
        } else {
            ui->textEditAplusPreview->setPlainText(
                tr("(file not found: %1)").arg(absPath));
        }
        ui->aplusPreviewStack->setCurrentIndex(1);
        return;
    }

    QPixmap pm(absPath);
    if (pm.isNull()) {
        ui->labelAplusPreview->setText(tr("(image not available: %1)").arg(absPath));
        ui->aplusPreviewStack->setCurrentIndex(0);
        return;
    }
    const QSize vp = ui->scrollAreaAplusPreview->viewport()->size();
    const int maxW = vp.width()  - 4;
    const int maxH = vp.height() - 4;
    if (maxW > 0 && maxH > 0) {
        ui->labelAplusPreview->setPixmap(
            pm.scaled(maxW, maxH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        ui->labelAplusPreview->setPixmap(pm);
    }
    ui->aplusPreviewStack->setCurrentIndex(0);
}

void PaneSizing::_runCliPrompt(const QString &executable, const QStringList &args,
                                const QByteArray &stdinData, const QString &workDir,
                                QObject *guard, std::function<void(QString)> callback)
{
    auto *process = new QProcess(this);
    process->setProgram(executable);
    process->setArguments(args);
    if (!workDir.isEmpty())
        process->setWorkingDirectory(workDir);

    connect(process, &QProcess::finished, this,
            [process, guard, cb = std::move(callback)](int, QProcess::ExitStatus) {
        process->deleteLater();
        if (!guard)
            return;
        const QString out = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        const QString err = QString::fromUtf8(process->readAllStandardError()).trimmed();
        cb(out.isEmpty() ? err : out);
    });

    process->start();
    if (process->waitForStarted(3000)) {
        process->write(stdinData);
        process->closeWriteChannel();
    } else {
        process->deleteLater();
        if (guard)
            callback(tr("Failed to start CLI process."));
    }
}

void PaneSizing::_tryGuessBrandRangeFromTitle()
{
    const auto *cat = _currentCategory();
    if (!cat || cat->letterSizes().isEmpty())
        return;

    // Scan all available title strings for "LETTER=NUMBER" patterns (e.g. "S=8", "M=10").
    // Check parent title first, then every child variant title in the tree.
    static const QRegularExpression re(QStringLiteral(R"(([A-Z]{1,3})\s*=\s*(\d+))"));

    QStringList titlesToScan;
    if (!m_productTitle.isEmpty())
        titlesToScan << m_productTitle;
    if (m_treeModel) {
        for (int i = 0; i < m_treeModel->rowCount(); ++i) {
            const QModelIndex parent = m_treeModel->index(i, 0);
            for (int j = 0; j < m_treeModel->rowCount(parent); ++j) {
                const QString t = m_treeModel->data(
                    m_treeModel->index(j, TreeSizingAsins::Title, parent),
                    Qt::DisplayRole).toString().trimmed();
                if (!t.isEmpty())
                    titlesToScan << t;
            }
        }
    }

    // Build letter→order map from the category so we can sort letters correctly
    const QStringList catLetters = cat->letterSizes();
    auto letterRank = [&](const QString &l) { return catLetters.indexOf(l); };

    // Collect unique letters found across all titles; only keep those known to the category
    QSet<QString> foundLetters;
    for (const QString &title : std::as_const(titlesToScan)) {
        auto it = re.globalMatch(title);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString letter = m.captured(1);
            if (letterRank(letter) >= 0)
                foundLetters.insert(letter);
        }
    }

    if (!foundLetters.isEmpty()) {
        // Sort by category order and take the first/last
        QStringList sorted(foundLetters.begin(), foundLetters.end());
        std::sort(sorted.begin(), sorted.end(),
                  [&](const QString &a, const QString &b) {
                      return letterRank(a) < letterRank(b);
                  });
        ui->sizeRangeBrand->setMode(QStringLiteral("letters"));
        ui->sizeRangeBrand->setFrom(sorted.first());
        ui->sizeRangeBrand->setTo(sorted.last());
        return;
    }

    // Fallback: no X=N patterns found — infer the letter range from the number of
    // size columns in the current main range (lower letter = lower size).
    if (!ui->sizeRangeMain->isRangeSelected())
        return;
    const QStringList mainKeys = cat->referenceKeys();
    const int fi = mainKeys.indexOf(ui->sizeRangeMain->from());
    const int ti = mainKeys.indexOf(ui->sizeRangeMain->to());
    if (fi < 0 || ti < 0)
        return;
    const int nCols = qAbs(ti - fi) + 1;
    if (nCols > catLetters.size())
        return;
    ui->sizeRangeBrand->setMode(QStringLiteral("letters"));
    ui->sizeRangeBrand->setFrom(catLetters.first());
    ui->sizeRangeBrand->setTo(catLetters.value(nCols - 1));
}

QCoro::Task<void> PaneSizing::_saveToSizeTableFolder()
{
    if (!m_productWorkingDir.exists() || !m_sizeTableModel)
        co_return;

    // Collect child ASINs from tree model
    QStringList childAsins;
    if (m_treeModel) {
        for (int i = 0; i < m_treeModel->rowCount(); ++i) {
            const QModelIndex parentIdx = m_treeModel->index(i, 0);
            for (int j = 0; j < m_treeModel->rowCount(parentIdx); ++j) {
                const QString asin = m_treeModel->data(
                    m_treeModel->index(j, TreeSizingAsins::ASIN, parentIdx),
                    Qt::DisplayRole).toString().trimmed();
                if (!asin.isEmpty())
                    childAsins << asin;
            }
        }
    }

    // --- Determine product type ---
    // First check settings.ini (cached from previous run)
    if (m_productType.isEmpty() && m_productWorkingDir.exists()) {
        QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
        m_productType = ps.value(QStringLiteral("sizing/productType")).toString();
    }

    // Try API if still empty and we can find a SKU
    if (m_productType.isEmpty() && !childAsins.isEmpty() && m_productWorkingDir.exists()) {
        // Look for a cached SKU in settings.ini
        QString sku;
        QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
        for (const QString &asin : childAsins) {
            sku = ps.value(QStringLiteral("sizing/skus/") + asin).toString();
            if (!sku.isEmpty()) break;
        }

        if (!sku.isEmpty()) {
            const QString mpId = firstMarketplaceIdFromCountryList(ui->listWidgetCountries);

            co_await m_api->fetchListingProductType(mpId, sku, &m_productType);

            if (!m_productType.isEmpty()) {
                ps.setValue(QStringLiteral("sizing/productType"), m_productType);
            }
        }
    }

    // If still empty, ask the user
    if (m_productType.isEmpty()) {
        bool ok = false;
        const QString entered = QInputDialog::getText(
            this, tr("Product Type"),
            tr("Enter the Amazon product type (e.g. DRESS, SHIRT, SHOES):"),
            QLineEdit::Normal, {}, &ok);
        if (!ok || entered.trimmed().isEmpty())
            co_return;
        m_productType = entered.trimmed().toUpper();
        if (m_productWorkingDir.exists()) {
            QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                         QSettings::IniFormat);
            ps.setValue(QStringLiteral("sizing/productType"), m_productType);
        }
    }

    // --- Get/ask for the template for this product type ---
    auto wdSettings = WorkingDirectoryManager::instance()->settings();
    const QString templateKey = QStringLiteral("productTypeTemplates/") + m_productType;
    QString templatePath = wdSettings->value(templateKey).toString();

    if (templatePath.isEmpty() || !QFileInfo::exists(templatePath)) {
        templatePath = QFileDialog::getOpenFileName(
            this,
            tr("Select template for product type \"%1\"").arg(m_productType),
            {}, tr("Excel (*.xlsx)"));
        if (templatePath.isEmpty())
            co_return;
        wdSettings->setValue(templateKey, templatePath);
    }

    // --- Create size-table/ folder ---
    m_productWorkingDir.mkpath(QStringLiteral("size-table"));
    const QString sizeTablePath = m_productWorkingDir.filePath(QStringLiteral("size-table"));

    // --- Write asin.txt ---
    if (!childAsins.isEmpty()) {
        QFile f(sizeTablePath + QStringLiteral("/asin.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(childAsins.join(QLatin1Char('\n')).toUtf8());
    }

    // --- Fill template and save ---
    QXlsx::Document doc(templatePath);
    if (!doc.load()) {
        QMessageBox::warning(this, tr("Template error"),
                             tr("Could not open template file:\n%1").arg(templatePath));
        co_return;
    }

    // Detect template type and fill:
    //
    // Type A – Amazon listing template: has "external_product_id" / "asin" column header.
    //          Write child ASINs into that column.
    // Type B – Amazon size chart template: has "ROW" markers in column A, data starts at
    //          column C (BrandSize). Transpose the size table model: each size column (one
    //          size) becomes one ROW, country-group values mapped to the correct data column
    //          via the template's COLUMN_HEADERS_STANDARD_DIMENSION_NAME row.

    // Scan first 10 rows for an ASIN column header (Type A detection)
    int asinCol    = -1;
    int asinHdrRow = -1;
    for (int row = 1; row <= 10 && asinCol < 0; ++row) {
        for (int col = 1; col <= 500; ++col) {
            const QVariant v = doc.read(row, col);
            if (!v.isValid()) continue;
            const QString s = v.toString().trimmed();
            if (s.compare(QStringLiteral("external_product_id"), Qt::CaseInsensitive) == 0 ||
                s.compare(QStringLiteral("asin"), Qt::CaseInsensitive) == 0) {
                asinCol    = col;
                asinHdrRow = row;
                break;
            }
        }
    }

    bool perGroupSaved = false;

    if (asinCol > 0) {
        // Listing template: write child ASINs, skip the required/optional markers row
        const int firstDataRow = asinHdrRow + 2;
        for (int i = 0; i < childAsins.size(); ++i)
            doc.write(firstDataRow + i, asinCol, childAsins.at(i));

    } else if (m_sizeTableModel) {
        // Size chart template (Type B): fill ROW rows with transposed size table data.
        // One output file per country group; each file contains only that group's size
        // equivalence column plus all body-measurement columns.

        // Template-column → model-row label fragments mapping.
        // Maps the template's standard dimension name (e.g. "FrSize") to fragments
        // that appear in the model's row label (e.g. "FR/BE/ES/TR" contains "FR").
        static const QList<QPair<QString, QStringList>> kColMap = {
            // Country-equivalent size columns
            {QStringLiteral("FrSize"),           {QStringLiteral("FR"), QStringLiteral("BE")}},
            {QStringLiteral("UkSize"),           {QStringLiteral("UK"), QStringLiteral("GB")}},
            {QStringLiteral("ItSize"),           {QStringLiteral("IT")}},
            {QStringLiteral("EsSize"),           {QStringLiteral("ES"), QStringLiteral("FR")}},
            {QStringLiteral("EuSize"),           {QStringLiteral("DE"), QStringLiteral("EU"), QStringLiteral("NL")}},
            {QStringLiteral("UsSize"),           {QStringLiteral("US"), QStringLiteral("COM"), QStringLiteral("CA")}},
            {QStringLiteral("AuSize"),           {QStringLiteral("AU"), QStringLiteral("UK")}},
            {QStringLiteral("JpSize"),           {QStringLiteral("JP")}},
            {QStringLiteral("CaSize"),           {QStringLiteral("CA"), QStringLiteral("US")}},
            {QStringLiteral("MxSize"),           {QStringLiteral("MX"), QStringLiteral("US")}},
            {QStringLiteral("BrSize"),           {QStringLiteral("BR")}},
            {QStringLiteral("KrSize"),           {QStringLiteral("KR")}},
            {QStringLiteral("CnSize"),           {QStringLiteral("CN")}},
            // Body measurement columns
            {QStringLiteral("BustSize"),         {QStringLiteral("Bust")}},
            {QStringLiteral("WaistSize"),        {QStringLiteral("Waist")}},
            {QStringLiteral("HipSize"),          {QStringLiteral("Hip")}},
            {QStringLiteral("NeckSize"),         {QStringLiteral("Neck")}},
            {QStringLiteral("SleeveLengthSize"), {QStringLiteral("Sleeve")}},
            {QStringLiteral("CuffSize"),         {QStringLiteral("Cuff")}},
            {QStringLiteral("BicepSize"),        {QStringLiteral("Bicep")}},
            {QStringLiteral("ShoulderWidthSize"),{QStringLiteral("Shoulder")}},
            {QStringLiteral("StrapLengthSize"),  {QStringLiteral("Strap")}},
        };

        // Strip " cm / X in" suffix from measurement values (e.g. "86 cm / 33¾ in" → "86").
        auto extractCmValue = [](const QString &val) -> QString {
            const int idx = val.indexOf(QLatin1String(" cm"));
            return (idx > 0) ? val.left(idx) : val;
        };

        // Read ALL row labels from model column 0 (country groups + measurements)
        const auto *sizeCat = _currentCategory();
        const int nGroupRows = sizeCat
            ? static_cast<int>(sizeCat->countryGroups().size())
            : m_sizeTableModel->rowCount();
        QStringList modelRowLabels;
        for (int r = 0; r < m_sizeTableModel->rowCount(); ++r) {
            auto *it = m_sizeTableModel->item(r, 0);
            modelRowLabels << (it ? it->text() : QString{});
        }

        // For a given template column header, return the model row index whose label
        // matches one of the key fragments (first match wins).
        auto findModelRow = [&](const QString &colHeader) -> int {
            for (const auto &[hdrFrag, labelFrags] : kColMap) {
                if (colHeader.compare(hdrFrag, Qt::CaseInsensitive) != 0)
                    continue;
                for (const QString &lFrag : labelFrags)
                    for (int r = 0; r < modelRowLabels.size(); ++r)
                        if (modelRowLabels[r].contains(lFrag, Qt::CaseInsensitive))
                            return r;
            }
            return -1;
        };

        // Parse the COLUMN_HEADERS_STANDARD_DIMENSION_NAME row to get ordered column headers.
        // Data always starts at column C (index 3) in this Amazon template format;
        // the first column there is BrandSize (the size label), subsequent columns are
        // country-standard sizes.
        constexpr int kDataStartCol = 3; // column C
        QList<int> colModelRows;         // colModelRows[i] → model row for template col kDataStartCol+1+i
        for (int row = 1; row <= 50; ++row) {
            const QVariant va = doc.read(row, 1);
            if (!va.isValid()) continue;
            if (va.toString().trimmed().compare(
                    QStringLiteral("COLUMN_HEADERS_STANDARD_DIMENSION_NAME"),
                    Qt::CaseInsensitive) != 0)
                continue;
            for (int col = kDataStartCol + 1; col <= kDataStartCol + 30; ++col) {
                const QVariant hv = doc.read(row, col);
                if (!hv.isValid() || hv.toString().trimmed().isEmpty()) break;
                colModelRows << findModelRow(hv.toString().trimmed());
            }
            break;
        }
        // Fallback: if no header row found, write country-group rows in model order
        if (colModelRows.isEmpty())
            for (int r = 0; r < nGroupRows; ++r)
                colModelRows << r;

        // Collect ROW rows (column A = "ROW")
        const int nSizes = m_sizeTableModel->columnCount() - 1;
        QList<int> rowRows;
        for (int row = 1; row <= 500 && rowRows.size() < nSizes; ++row) {
            const QVariant va = doc.read(row, 1);
            if (!va.isValid()) continue;
            if (va.toString().trimmed().compare(QStringLiteral("ROW"), Qt::CaseInsensitive) == 0)
                rowRows << row;
        }

        // Pre-compute brand labels (same for every group)
        QStringList brandLabels;
        if (sizeCat) {
            const QString bMode = ui->sizeRangeBrand->mode();
            const QString bFrom = ui->sizeRangeBrand->from();
            const QString bTo   = ui->sizeRangeBrand->to();
            if (bMode == QLatin1String("letters")) {
                const QStringList allLetters = sizeCat->letterSizes();
                int bfi = allLetters.indexOf(bFrom);
                int bti = allLetters.indexOf(bTo);
                if (bfi < 0) bfi = 0;
                if (bti < 0) bti = allLetters.size() - 1;
                if (bfi > bti) std::swap(bfi, bti);
                brandLabels = allLetters.mid(bfi, bti - bfi + 1);
            } else {
                const QStringList allKeys = sizeCat->referenceKeys();
                int bfi = allKeys.indexOf(bFrom);
                int bti = allKeys.indexOf(bTo);
                if (bfi < 0) bfi = 0;
                if (bti < 0) bti = allKeys.size() - 1;
                if (bfi > bti) std::swap(bfi, bti);
                brandLabels = allKeys.mid(bfi, bti - bfi + 1);
            }
        }

        // --- One output file per country group ---
        const QList<CountryGroup> allGroups = sizeCat ? sizeCat->countryGroups()
                                                       : QList<CountryGroup>{};
        const int nAllGroups = allGroups.size();

        // When no group info is available, fall back to a single file with all rows.
        const int loopCount = (nAllGroups > 0) ? nAllGroups : 1;

        for (int g = 0; g < loopCount; ++g) {
            const QString groupKey = (nAllGroups > 0)
                ? allGroups[g].key.toLower()
                : QStringLiteral("all");

            QXlsx::Document gDoc(templatePath);
            if (!gDoc.load()) continue;

            for (int si = 0; si < nSizes && si < rowRows.size(); ++si) {
                const int tRow = rowRows[si];

                // Column C: brand label
                const QString bl = brandLabels.value(si);
                if (!bl.isEmpty())
                    gDoc.write(tRow, kDataStartCol, bl);

                // Data columns: fill only the current group row + measurement rows
                for (int ci = 0; ci < colModelRows.size(); ++ci) {
                    const int mr = colModelRows[ci];
                    if (mr < 0 || mr >= m_sizeTableModel->rowCount()) continue;
                    const bool isThisGroup  = (nAllGroups == 0) || (mr == g);
                    const bool isMeasurement = (mr >= nGroupRows);
                    if (!isThisGroup && !isMeasurement) continue;
                    auto *item = m_sizeTableModel->item(mr, si + 1);
                    if (item && !item->text().isEmpty())
                        gDoc.write(tRow, kDataStartCol + 1 + ci, extractCmValue(item->text()));
                }
            }

            const QString destPath = sizeTablePath
                + QStringLiteral("/filled_template_%1.xlsx").arg(groupKey);
            if (!gDoc.saveAs(destPath)) {
                QMessageBox::warning(this, tr("Template error"),
                    tr("Could not save filled template to:\n%1").arg(destPath));
            }
        }
        perGroupSaved = true;
    }

    if (!perGroupSaved) {
        const QString destPath = sizeTablePath + QStringLiteral("/filled_template.xlsx");
        if (!doc.saveAs(destPath)) {
            QMessageBox::warning(this, tr("Template error"),
                                 tr("Could not save filled template to:\n%1").arg(destPath));
        }
    }

    co_return;
}

QCoro::Task<void> PaneSizing::_addSkusFromTemplate()
{
    if (!m_productWorkingDir.exists()) {
        QMessageBox::warning(this, tr("Add SKUs"), tr("Load a product first."));
        co_return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open listing template"), {}, tr("Excel (*.xlsx)"));
    if (path.isEmpty())
        co_return;

    const QStringList skus = readSkusFromXlsx(path);
    if (skus.isEmpty()) {
        QMessageBox::information(this, tr("Add SKUs"),
            tr("No SKUs found in the template.\n"
               "Looked for columns: seller_sku, item_sku, sku."));
        co_return;
    }

    const QString mpId = firstMarketplaceIdFromCountryList(ui->listWidgetCountries);

    // Progress dialog
    QDialog progressDlg(this);
    progressDlg.setWindowTitle(tr("Fetching ASINs from SKUs"));
    progressDlg.resize(520, 340);
    auto *dlgLayout  = new QVBoxLayout(&progressDlg);
    auto *statusLbl  = new QLabel(tr("Looking up ASINs…"), &progressDlg);
    auto *progressBar = new QProgressBar(&progressDlg);
    progressBar->setRange(0, skus.size());
    progressBar->setValue(0);
    auto *logEdit = new QPlainTextEdit(&progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto *closeBtn = new QPushButton(tr("Close"), &progressDlg);
    closeBtn->setEnabled(false);
    connect(closeBtn, &QPushButton::clicked, &progressDlg, &QDialog::accept);
    dlgLayout->addWidget(statusLbl);
    dlgLayout->addWidget(progressBar);
    dlgLayout->addWidget(logEdit);
    dlgLayout->addWidget(closeBtn);
    progressDlg.show();
    QCoreApplication::processEvents();

    QStringList foundAsins;
    int idx = 0;
    for (const QString &sku : skus) {
        ++idx;
        statusLbl->setText(tr("SKU %1 / %2: %3").arg(idx).arg(skus.size()).arg(sku));
        progressBar->setValue(idx - 1);
        QCoreApplication::processEvents();

        QString asin;
        co_await m_api->fetchAsinBySku(mpId, sku, &asin);

        if (!asin.isEmpty()) {
            logEdit->appendPlainText(QStringLiteral("  %1 → %2").arg(sku, asin));
            if (!foundAsins.contains(asin))
                foundAsins << asin;
        } else {
            logEdit->appendPlainText(QStringLiteral("  %1 → not found").arg(sku));
        }
        progressBar->setValue(idx);
        logEdit->moveCursor(QTextCursor::End);
        QCoreApplication::processEvents();
    }

    // Merge with existing asins-extra.txt
    m_productWorkingDir.mkpath(QStringLiteral("size-table"));
    const QString outPath =
        m_productWorkingDir.filePath(QStringLiteral("size-table/asins-extra.txt"));

    QStringList merged;
    {
        QFile f(outPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QString &a : QString::fromUtf8(f.readAll()).split(QLatin1Char(','))) {
                const QString t = a.trimmed();
                if (!t.isEmpty() && !merged.contains(t))
                    merged << t;
            }
        }
    }
    for (const QString &a : foundAsins)
        if (!merged.contains(a))
            merged << a;

    {
        QFile f(outPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(merged.join(QLatin1Char(',')).toUtf8());
    }

    statusLbl->setText(tr("Done — found %1 ASIN(s), saved to asins-extra.txt").arg(foundAsins.size()));
    logEdit->appendPlainText(
        tr("\nTotal in asins-extra.txt: %1 ASIN(s)").arg(merged.size()));
    closeBtn->setEnabled(true);
    progressDlg.exec();
    co_return;
}

QCoro::Task<void> PaneSizing::_resolveSkus(QList<AsinSku> &items,
                                            const QString &marketplaceId,
                                            bool *cancelled)
{
    *cancelled = false;

    // Step 1: load SKUs saved from a previous upload (settings.ini)
    if (m_productWorkingDir.exists()) {
        QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                    QSettings::IniFormat);
        for (auto &item : items)
            if (item.sku.isEmpty())
                item.sku = s.value(QStringLiteral("sizing/skus/") + item.asin).toString();
    }

    // Step 2: fetch ALL listings (FBA + MFN) via Reports API for any still-missing
    {
        bool anyMissing = false;
        for (const auto &item : items)
            if (item.sku.isEmpty()) { anyMissing = true; break; }

        if (anyMissing) {
            QMessageBox infoBox(QMessageBox::Information, tr("Fetching SKUs"),
                                tr("Requesting all-listings report from Amazon...\n"
                                   "This can take up to 3 minutes. Please wait."),
                                QMessageBox::NoButton, this);
            infoBox.setStandardButtons(QMessageBox::NoButton);
            infoBox.show();
            QCoreApplication::processEvents();

            QHash<QString, QString> reportMap;
            co_await m_api->fetchAllSkusViaReport(marketplaceId, &reportMap);
            infoBox.hide();

            if (reportMap.isEmpty() && !m_api->lastError().isEmpty()) {
                QMessageBox::warning(this, tr("Report error"),
                                     tr("Could not fetch listings report:\n%1\n\n"
                                        "Falling back to manual SKU entry.")
                                     .arg(m_api->lastError()));
            }
            for (auto &item : items)
                if (item.sku.isEmpty())
                    item.sku = reportMap.value(item.asin);

            if (m_productWorkingDir.exists()) {
                QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                            QSettings::IniFormat);
                for (const auto &item : items)
                    if (!item.sku.isEmpty())
                        s.setValue(QStringLiteral("sizing/skus/") + item.asin, item.sku);
            }
        }
    }

    // Step 3: manual dialog for any still-missing SKUs
    {
        bool anyMissing = false;
        for (const auto &item : items)
            if (item.sku.isEmpty()) { anyMissing = true; break; }

        if (anyMissing) {
            QDialog skuDlg(this);
            skuDlg.setWindowTitle(tr("Enter SKUs"));
            auto *skuLayout = new QVBoxLayout(&skuDlg);
            skuLayout->addWidget(new QLabel(
                tr("Enter the seller SKU for each ASIN.\n"
                   "These will be saved for future uploads."), &skuDlg));

            auto *table = new QTableWidget(items.size(), 2, &skuDlg);
            table->setHorizontalHeaderLabels({tr("ASIN"), tr("Seller SKU")});
            table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
            table->verticalHeader()->setVisible(false);
            for (int i = 0; i < items.size(); ++i) {
                auto *asinItem = new QTableWidgetItem(items.at(i).asin);
                asinItem->setFlags(asinItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(i, 0, asinItem);
                table->setItem(i, 1, new QTableWidgetItem(items.at(i).sku));
            }
            skuLayout->addWidget(table);
            auto *skuButtons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &skuDlg);
            connect(skuButtons, &QDialogButtonBox::accepted, &skuDlg, &QDialog::accept);
            connect(skuButtons, &QDialogButtonBox::rejected, &skuDlg, &QDialog::reject);
            skuLayout->addWidget(skuButtons);
            skuDlg.resize(500, 350);

            if (skuDlg.exec() != QDialog::Accepted) {
                *cancelled = true;
                co_return;
            }

            QSettings *s = m_productWorkingDir.exists()
                ? new QSettings(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                                QSettings::IniFormat)
                : nullptr;
            for (int i = 0; i < items.size(); ++i) {
                const QString entered = table->item(i, 1)
                                            ? table->item(i, 1)->text().trimmed()
                                            : QString{};
                items[i].sku = entered;
                if (s && !entered.isEmpty())
                    s->setValue(QStringLiteral("sizing/skus/") + items.at(i).asin, entered);
            }
            delete s;
        }
    }
    co_return;
}

QCoro::Task<void> PaneSizing::_uploadSizeImage(int imageIndex)
{
    const int row = ui->listWidgetSizeGroups->currentRow();
    if (row < 0 || row >= m_groupImages.size()) {
        QMessageBox::warning(this, tr("Upload"), tr("No size image selected."));
        co_return;
    }
    const QImage img = m_groupImages.at(row);

    QByteArray jpegData;
    {
        QBuffer buf(&jpegData);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "JPEG", 90);
    }
    if (jpegData.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"), tr("Failed to convert size image to JPEG."));
        co_return;
    }

    // Collect child ASINs + whatever SKUs are already known from the tree
    QList<AsinSku> treeItems;
    if (m_treeModel) {
        for (int i = 0; i < m_treeModel->rowCount(); ++i) {
            const QModelIndex parentIdx = m_treeModel->index(i, 0);
            for (int j = 0; j < m_treeModel->rowCount(parentIdx); ++j) {
                const QString asin = m_treeModel->data(
                    m_treeModel->index(j, TreeSizingAsins::ASIN, parentIdx),
                    Qt::DisplayRole).toString().trimmed();
                const QString sku = m_treeModel->data(
                    m_treeModel->index(j, TreeSizingAsins::SKU, parentIdx),
                    Qt::DisplayRole).toString().trimmed();
                if (!asin.isEmpty())
                    treeItems << AsinSku{asin, sku};
            }
        }
    }
    if (treeItems.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"), tr("No items found in the tree."));
        co_return;
    }

    // Upload only to amazon.fr for initial testing.
    // TODO: uncomment the block below to upload to all checked marketplaces.
    const QStringList marketplaceIds = {QStringLiteral("A13V1IB3VIYZZH")}; // FR only

    bool cancelled = false;
    co_await _resolveSkus(treeItems, marketplaceIds.first(), &cancelled);
    if (cancelled)
        co_return;

    QList<AsinSku> uploadItems;
    QStringList missingSkuAsins;
    for (const auto &item : treeItems) {
        if (item.sku.isEmpty()) missingSkuAsins << item.asin;
        else                    uploadItems << item;
    }
    if (!missingSkuAsins.isEmpty())
        qWarning() << "PaneSizing: still no SKU for ASINs:" << missingSkuAsins;

    if (uploadItems.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"),
            tr("No SKUs resolved. Upload cancelled."));
        co_return;
    }

    // Auto-detect product type from the first listing
    QString productType;
    co_await m_api->fetchListingProductType(
        marketplaceIds.first(), uploadItems.first().sku, &productType);

    if (productType.isEmpty()) {
        bool ok = false;
        const QString entered = QInputDialog::getText(
            this, tr("Product Type"),
            tr("Could not auto-detect the product type.\n"
               "Enter the Amazon product type (e.g. DRESS, SHIRT, SHOES):"),
            QLineEdit::Normal, {}, &ok);
        if (!ok || entered.trimmed().isEmpty())
            co_return;
        productType = entered.trimmed().toUpper();
    }

    int successCount = 0;
    const int totalAttempts = marketplaceIds.size() * uploadItems.size();
    QStringList errors;

    for (const QString &mpId : marketplaceIds) {
        for (const auto &item : uploadItems) {
            bool ok = false;
            co_await m_api->patchListingImage(mpId, item.sku, productType,
                                              jpegData, imageIndex, &ok);
            if (ok)
                ++successCount;
            else
                errors << QStringLiteral("%1 / %2 (%3): %4")
                              .arg(mpId, item.sku, item.asin, m_api->lastError());
            m_api->clearLastError();
        }
    }

    if (errors.isEmpty()) {
        QMessageBox::information(this, tr("Upload"),
            tr("Image uploaded to %1 listing(s).").arg(successCount));
    } else {
        QMessageBox::warning(this, tr("Upload"),
            tr("Uploaded image to %1 of %2 listing(s).\n\nErrors:\n%3")
                .arg(successCount).arg(totalAttempts).arg(errors.join('\n')));
    }
    co_return;
}

void PaneSizing::_refreshTemplateCombo()
{
    if (!m_templateModel) return;

    const QString previousId = ui->comboBoxSizeSaved->currentData().toString();

    QSignalBlocker blocker(ui->comboBoxSizeSaved);
    ui->comboBoxSizeSaved->clear();
    ui->comboBoxSizeSaved->addItem(tr("— select —"), QString{});
    for (int row = 0; row < m_templateModel->rowCount(); ++row) {
        const SizingTemplate &t = m_templateModel->templateAt(row);
        ui->comboBoxSizeSaved->addItem(t.name, t.id);
    }

    if (!previousId.isEmpty()) {
        const int idx = ui->comboBoxSizeSaved->findData(previousId);
        if (idx >= 0) ui->comboBoxSizeSaved->setCurrentIndex(idx);
    }
}

void PaneSizing::onSavedSizeAddClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Template"),
                                               tr("Template name:"),
                                               QLineEdit::Normal, QString{}, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const int row = m_templateModel->addTemplate(name.trimmed());

    // Auto-save current sizing data into the new template if a category is set.
    const auto *cat = _currentCategory();
    if (cat) {
        SizingTemplate data;
        data.id       = m_templateModel->idForRow(row);
        data.name     = name.trimmed();
        data.category     = cat->displayName();
        data.mode         = ui->sizeRangeMain->mode();
        data.fromVal      = ui->sizeRangeMain->from();
        data.toVal        = ui->sizeRangeMain->to();
        data.brandMode    = ui->sizeRangeBrand->mode();
        data.brandFromVal = ui->sizeRangeBrand->from();
        data.brandToVal   = ui->sizeRangeBrand->to();
        for (const auto &w : m_measurementWidgets)
            data.measurements[w.fieldId] = {
                w.refSpinBox->value(),
                w.stepSpinBox->value(),
                w.rangeSpinBox->value()
            };
        m_templateModel->updateTemplate(row, data);
    }

    _refreshTemplateCombo();
    const QString newId = m_templateModel->idForRow(row);
    const int idx = ui->comboBoxSizeSaved->findData(newId);
    if (idx >= 0) ui->comboBoxSizeSaved->setCurrentIndex(idx);
}

void PaneSizing::onSavedSizeSaveClicked()
{
    const QString id = ui->comboBoxSizeSaved->currentData().toString();
    if (id.isEmpty()) {
        QMessageBox::information(this, tr("No template"),
                                 tr("Select or create a template first."));
        return;
    }
    const int row = m_templateModel->rowForId(id);
    if (row < 0) return;

    const SizingTemplate &existing = m_templateModel->templateAt(row);

    const auto *cat = _currentCategory();
    if (!cat) {
        QMessageBox::warning(this, tr("No category"),
                             tr("Select a size category first."));
        return;
    }

    QString message;
    if (!existing.category.isEmpty() && existing.category != cat->displayName()) {
        message = tr("<p>Overwrite existing data for template \"%1\"?</p>"
                     "<p><b><font color='red' size='+2'>"
                     "WARNING: Previous category was \"%2\", current is \"%3\"!"
                     "</font></b></p>")
                  .arg(existing.name.toHtmlEscaped(),
                       existing.category.toHtmlEscaped(),
                       QString(cat->displayName()).toHtmlEscaped());
    } else {
        message = tr("Overwrite existing data for template \"%1\"?")
                  .arg(existing.name.toHtmlEscaped());
    }

    const auto answer = QMessageBox::question(this, tr("Save template"), message,
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    SizingTemplate newData;
    newData.id           = existing.id;
    newData.name         = existing.name;
    newData.category     = cat->displayName();
    newData.mode         = ui->sizeRangeMain->mode();
    newData.fromVal      = ui->sizeRangeMain->from();
    newData.toVal        = ui->sizeRangeMain->to();
    newData.brandMode    = ui->sizeRangeBrand->mode();
    newData.brandFromVal = ui->sizeRangeBrand->from();
    newData.brandToVal   = ui->sizeRangeBrand->to();
    for (const auto &w : m_measurementWidgets) {
        newData.measurements[w.fieldId] = {
            w.refSpinBox->value(),
            w.stepSpinBox->value(),
            w.rangeSpinBox->value()
        };
    }

    m_templateModel->updateTemplate(row, newData);
    _refreshTemplateCombo();
    const int idx = ui->comboBoxSizeSaved->findData(newData.id);
    if (idx >= 0) ui->comboBoxSizeSaved->setCurrentIndex(idx);

    QMessageBox::information(this, tr("Saved"),
                             tr("Template \"%1\" saved.").arg(existing.name));
}

void PaneSizing::onSavedSizeLoadClicked()
{
    const QString id = ui->comboBoxSizeSaved->currentData().toString();
    if (id.isEmpty()) {
        QMessageBox::information(this, tr("No template"),
                                 tr("Select a template first."));
        return;
    }
    const int row = m_templateModel->rowForId(id);
    if (row < 0) return;

    const SizingTemplate tmpl = m_templateModel->templateAt(row);

    if (tmpl.category.isEmpty()) {
        QMessageBox::information(this, tr("No data"),
            tr("Template \"%1\" has no saved sizing data yet.\n"
               "Configure the sizing settings and click Save first.")
            .arg(tmpl.name));
        return;
    }

    if (m_productWorkingDir.exists()) {
        const QString iniPath = m_productWorkingDir.filePath(QStringLiteral("settings.ini"));
        if (QFile::exists(iniPath)) {
            QSettings s(iniPath, QSettings::IniFormat);
            const QString currentType = s.value(QStringLiteral("sizing/type")).toString();
            if (!currentType.isEmpty() && currentType != tmpl.category) {
                QMessageBox::critical(this, tr("Type mismatch"),
                    tr("Cannot load: product uses category \"%1\" but template is for \"%2\".")
                        .arg(currentType, tmpl.category));
                return;
            }
        }
    }

    int catIdx = -1;
    for (int i = 0; i < ui->comboBoxSizeType->count(); ++i) {
        if (ui->comboBoxSizeType->itemText(i) == tmpl.category) {
            catIdx = i;
            break;
        }
    }
    if (catIdx < 0) {
        QMessageBox::warning(this, tr("Category not found"),
                             tr("Category \"%1\" not found.").arg(tmpl.category));
        return;
    }
    ui->comboBoxSizeType->setCurrentIndex(catIdx);

    ui->sizeRangeMain->setMode(tmpl.mode);
    _populateSizeRangeCombos();
    if (!tmpl.fromVal.isEmpty())      ui->sizeRangeMain->setFrom(tmpl.fromVal);
    if (!tmpl.toVal.isEmpty())        ui->sizeRangeMain->setTo(tmpl.toVal);
    if (!tmpl.brandMode.isEmpty())    ui->sizeRangeBrand->setMode(tmpl.brandMode);
    if (!tmpl.brandFromVal.isEmpty()) ui->sizeRangeBrand->setFrom(tmpl.brandFromVal);
    if (!tmpl.brandToVal.isEmpty())   ui->sizeRangeBrand->setTo(tmpl.brandToVal);

    for (auto it = tmpl.measurements.constBegin(); it != tmpl.measurements.constEnd(); ++it) {
        for (const auto &w : m_measurementWidgets) {
            if (w.fieldId == it.key()) {
                w.refSpinBox->setValue(it.value().refValue);
                w.stepSpinBox->setValue(it.value().step);
                w.rangeSpinBox->setValue(it.value().rangeVal);
                break;
            }
        }
    }

    _saveProductSettings();

    QMessageBox::information(this, tr("Loaded"),
                             tr("Template \"%1\" loaded and applied.").arg(tmpl.name));
}

void PaneSizing::onSavedSizeEditClicked()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Edit Sizing Templates"));
    dlg.resize(420, 360);

    auto *layout = new QVBoxLayout(&dlg);

    auto *view = new QTableView(&dlg);
    view->setModel(m_templateModel);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    view->horizontalHeader()->hideSection(SizingTableTemplateModel::ColCategory);
    view->verticalHeader()->setVisible(false);
    view->setEditTriggers(QAbstractItemView::DoubleClicked
                          | QAbstractItemView::EditKeyPressed
                          | QAbstractItemView::SelectedClicked);
    layout->addWidget(view);

    auto *btnRow = new QHBoxLayout();
    auto *deleteBtn = new QPushButton(tr("Delete"), &dlg);
    btnRow->addWidget(deleteBtn);
    btnRow->addStretch();
    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    btnRow->addWidget(bbox);
    layout->addLayout(btnRow);

    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
    connect(deleteBtn, &QPushButton::clicked, &dlg, [this, view]() {
        const QModelIndex cur = view->currentIndex();
        if (!cur.isValid()) return;
        const int row = cur.row();
        const QString name = m_templateModel->templateAt(row).name;
        const auto answer = QMessageBox::question(
            view, tr("Delete template"),
            tr("Delete template \"%1\"?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        m_templateModel->removeTemplateAt(row);
        _refreshTemplateCombo();
    });

    dlg.exec();
    _refreshTemplateCombo();
}
