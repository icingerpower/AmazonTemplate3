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
#include "gui/DialogEditPrompts.h"
#include "BrokenChildTable.h"
#include "AmazonMarketplace.h"
#include "fillers/FillerSize.h"
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
#include <QGridLayout>
#include <QFormLayout>
#include <QSpacerItem>
#include <QPainter>
#include <QPixmap>
#include <QRadioButton>
#include <QStandardItem>
#include <QListWidget>
#include <QListView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QCheckBox>
#include <QFont>
#include <QIcon>
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
#include <QScrollBar>
#include <QSplitter>
#include <QTextEdit>
#include <QProgressBar>
#include <QProgressDialog>
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
#include <QColor>
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
#include <QGroupBox>
#include <QUrlQuery>
#include <QButtonGroup>
#include <QTreeView>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QDateTime>
#include <QDir>
#include <QBuffer>
#include <QSet>
#include <QCoro/QCoroTimer>
#include <QFuture>
#include <QPromise>
#include <QCoro/QCoroFuture>
#include <QTabWidget>
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
    connect(ui->buttonLoadSubFolder, &QPushButton::clicked,
            this, &PaneSizing::onLoadSubFolderClicked);

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

    connect(ui->treeWidgetColorVariants, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
                onVariantTreeSelectionChanged();
            });
    connect(ui->buttonBrowseVariantImage, &QPushButton::clicked,
            this, &PaneSizing::onBrowseVariantImageClicked);
    connect(ui->buttonUploadVariantImage, &QPushButton::clicked,
            this, &PaneSizing::onUploadVariantImageClicked);
    connect(ui->radioButtonVariantReplaceAt, &QRadioButton::toggled,
            ui->spinBoxVariantImagePos, &QSpinBox::setEnabled);

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
    connect(ui->buttonEditPrompts, &QPushButton::clicked,
            this, &PaneSizing::onEditPromptsClicked);

    m_imageNam = new QNetworkAccessManager(this);
    _initWorkflowCombo();

    // --- A+ content wiring ---
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
    connect(ui->buttonFactorize, &QPushButton::clicked,
            this, &PaneSizing::onFactorizeSizeTables);
    connect(ui->buttonPickSizeTableTemplate, &QPushButton::clicked,
            this, &PaneSizing::onPickSizeTableTemplateClicked);
    connect(ui->buttonGenerateSizeTableXlsx, &QPushButton::clicked,
            this, &PaneSizing::onGenerateSizeTableXlsxClicked);

    ui->treeViewAsins->setItemDelegateForColumn(
        TreeSizingAsins::Title, new MiddleTruncateDelegate(this));

    ui->textEditAttributes->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->textEditAttributes, &QTextEdit::customContextMenuRequested,
            this, [this](const QPoint &pos) {
                QMenu *menu = ui->textEditAttributes->createStandardContextMenu();
                menu->addSeparator();
                QAction *copyAll = menu->addAction(tr("Copy all"));
                connect(copyAll, &QAction::triggered, this, [this]() {
                    QGuiApplication::clipboard()->setText(
                        ui->textEditAttributes->toPlainText());
                });
                menu->exec(ui->textEditAttributes->mapToGlobal(pos));
                menu->deleteLater();
            });

    m_templateModel = new SizingTableTemplateModel(this);
    connect(ui->buttonSavedSizeAdd,  &QPushButton::clicked, this, &PaneSizing::onSavedSizeAddClicked);
    connect(ui->buttonSavedSizeSave, &QPushButton::clicked, this, &PaneSizing::onSavedSizeSaveClicked);
    connect(ui->buttonSavedSizeLoad, &QPushButton::clicked, this, &PaneSizing::onSavedSizeLoadClicked);
    connect(ui->buttonSavedSizeEdit, &QPushButton::clicked, this, &PaneSizing::onSavedSizeEditClicked);

    connect(ui->buttonBrokenCheck, &QPushButton::clicked,
            this, [this]() { _loadBrokenChildData(true); });

    // Broken child fix buttons
    connect(ui->buttonFixAll,    &QPushButton::clicked,
            this, &PaneSizing::onFixAllClicked);
    connect(ui->buttonFixParent, &QPushButton::clicked,
            this, &PaneSizing::onFixParentsClicked);
    connect(ui->buttonFixImages, &QPushButton::clicked,
            this, &PaneSizing::onFixImagesClicked);
    connect(ui->buttonFixLog, &QPushButton::clicked,
            this, &PaneSizing::onFixLogClicked);
    connect(ui->buttonCheckStatus, &QPushButton::clicked,
            this, &PaneSizing::onCheckStatusClicked);
    connect(ui->buttonBrowseBrokenTemplate, &QPushButton::clicked,
            this, &PaneSizing::onBrowseBrokenTemplateClicked);
    connect(ui->comboBoxBrokenAttrMarket,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PaneSizing::onBrokenAttrMarketChanged);

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
    ui->buttonFactorize->setEnabled(m_workingDir.exists());
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

// For per-color A+ slots (elementId starts with "image_color_"), scan the product
// directory for matching reference photos and inject their absolute paths into the
// prompt.  This prevents the AI from defaulting to the wrong-color main image.
static void injectColorImageHints(QList<ImageSlotSpec> &imageSpecs,
                                  const QDir &productDir,
                                  const QStringList &excludedColorIds = {})
{
    // Pre-build the set of file prefixes that belong to excluded colors so we
    // can reject files whose name is a longer match for an excluded colorId.
    // Example: active "blush-pink" must not pull in "blush-pink-dusty-image-01.jpg".
    QStringList excludedPrefixes;
    for (const QString &ecId : excludedColorIds) {
        if (!ecId.isEmpty()) {
            excludedPrefixes << ecId + QLatin1Char('-');
            excludedPrefixes << ecId + QLatin1Char('_');
        }
    }

    static const QString kColorPrefix  = QStringLiteral("image_color_");
    static const QString kDetailPrefix = QStringLiteral("image_detail_");
    for (ImageSlotSpec &spec : imageSpecs) {
        const bool isColor  = spec.elementId.startsWith(kColorPrefix);
        const bool isDetail = spec.elementId.startsWith(kDetailPrefix);
        if (!isColor && !isDetail) continue;
        const QString colorId = isColor
            ? spec.elementId.mid(kColorPrefix.size())
            : spec.elementId.mid(kDetailPrefix.size()); // e.g. "blue-floral"

        QStringList refPaths;
        for (const QString &f : productDir.entryList(QDir::Files)) {
            if (!f.startsWith(colorId + QLatin1Char('-'))
                && !f.startsWith(colorId + QLatin1Char('_')))
                continue;
            bool belongsToExcluded = false;
            for (const QString &ep : excludedPrefixes) {
                if (f.startsWith(ep)) { belongsToExcluded = true; break; }
            }
            if (!belongsToExcluded)
                refPaths.append(productDir.absoluteFilePath(f));
        }
        refPaths.sort();
        if (refPaths.isEmpty()) continue;

        const QString hint =
            QCoreApplication::translate("PaneSizing",
                "Reference photos for this exact color variant (%1) — use ONLY these:\n%2\n"
                "Reproduce the exact color, pattern and design faithfully. "
                "Do NOT substitute another color variant.")
            .arg(colorId, refPaths.join(QStringLiteral("\n")));

        spec.desktopPrompt += QStringLiteral("\n\n") + hint;
        spec.mobilePrompt  += QStringLiteral("\n\n") + hint;
    }
}

// Resolve the first marketplace ID matching a country code from the
// listWidgetCountries widget. Falls back to UK (A1F83G8C2ARO7P) if no entry
// matches a known country code.
static const QHash<QString, QString> &countryCodeToMarketplaceId()
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
    return kCodeToMp;
}

static QString firstMarketplaceIdFromCountryList(QListWidget *listWidget)
{
    const auto &m = countryCodeToMarketplaceId();
    for (int i = 0; i < listWidget->count(); ++i) {
        const QString code = listWidget->item(i)->text()
                                 .trimmed().toLower().split(QLatin1Char(' ')).first();
        const QString mp = m.value(code);
        if (!mp.isEmpty()) return mp;
    }
    return QStringLiteral("A1F83G8C2ARO7P"); // fallback: UK
}

// Returns all marketplace IDs from the country list (in order), with CA and US
// appended as final fallbacks — so products that only exist in NA are still found
// when the country list is EU-only.
static QStringList allMarketplaceIdsFromCountryList(QListWidget *listWidget)
{
    const auto &m = countryCodeToMarketplaceId();
    QStringList result;
    for (int i = 0; i < listWidget->count(); ++i) {
        const QString code = listWidget->item(i)->text()
                                 .trimmed().toLower().split(QLatin1Char(' ')).first();
        const QString mp = m.value(code);
        if (!mp.isEmpty() && !result.contains(mp))
            result << mp;
    }
    // Always try CA and US as fallbacks for NA-only products.
    for (const QString &fb : {QStringLiteral("A2EUQ1WTGCTBG2"),
                               QStringLiteral("ATVPDKIKX0DER")})
        if (!result.contains(fb)) result << fb;
    return result;
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
        if (w.rangeSpinBox) s.setValue(base + QStringLiteral("/range"), w.rangeSpinBox->value());
    }

    // Save SKUs from the tree model
    if (m_treeModel) {
        for (int fi = 0; fi < m_treeModel->rowCount(); ++fi) {
            const QModelIndex pi = m_treeModel->index(fi, 0);
            const QString pAsin  = m_treeModel->data(m_treeModel->index(fi, TreeSizingAsins::ASIN), Qt::DisplayRole).toString();
            const QString pSku   = m_treeModel->data(m_treeModel->index(fi, TreeSizingAsins::SKU), Qt::DisplayRole).toString().trimmed();
            if (!pSku.isEmpty()) s.setValue(QStringLiteral("sizing/skus/") + pAsin, pSku);

            for (int ci = 0; ci < m_treeModel->rowCount(pi); ++ci) {
                const QString cAsin = m_treeModel->data(m_treeModel->index(ci, TreeSizingAsins::ASIN, pi), Qt::DisplayRole).toString();
                const QString cSku  = m_treeModel->data(m_treeModel->index(ci, TreeSizingAsins::SKU, pi), Qt::DisplayRole).toString().trimmed();
                if (!cSku.isEmpty()) s.setValue(QStringLiteral("sizing/skus/") + cAsin, cSku);
            }
        }
    }
}

void PaneSizing::_loadProductSettings()
{
    if (!m_productWorkingDir.exists())
        return;

    // Always initialise A+ content from disk so that m_groupImages is populated
    // even when sizing settings have never been saved for this product.
    _initAplusContent();
    _refreshSizeImageUploadStatus();

    QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                 QSettings::IniFormat);

    m_aplusExcludedColors = s.value(QStringLiteral("aplus/excluded_colors"))
        .toString().split(QLatin1Char(','), Qt::SkipEmptyParts);

    const QString savedTpl = s.value(QStringLiteral("brokenChild/templatePath")).toString();
    if (!savedTpl.isEmpty())
        ui->lineEditBrokenTemplate->setText(savedTpl);

    // Restore attribute-source marketplace: product settings.ini takes priority, then global.
    const QString savedMpId = s.contains(QStringLiteral("brokenChild/attrMarketplace"))
        ? s.value(QStringLiteral("brokenChild/attrMarketplace")).toString()
        : QSettings().value(QStringLiteral("brokenChild/attrMarketplace")).toString();
    if (!savedMpId.isEmpty()) {
        for (int i = 0; i < ui->comboBoxBrokenAttrMarket->count(); ++i) {
            if (ui->comboBoxBrokenAttrMarket->itemData(i).toString() == savedMpId) {
                ui->comboBoxBrokenAttrMarket->setCurrentIndex(i);
                break;
            }
        }
    }

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
            if (w.rangeSpinBox) w.rangeSpinBox->setValue(s.value(base + QStringLiteral("/range")).toDouble());
        }
    }

    if (_currentCategory() && ui->sizeRangeMain->isRangeSelected()) {
        _rebuildSizeTable();
        updateButtonStates();
    }

    // Restore SKUs from settings.ini
    if (m_treeModel) {
        for (int fi = 0; fi < m_treeModel->rowCount(); ++fi) {
            const QModelIndex pi = m_treeModel->index(fi, 0);
            const QString pAsin  = m_treeModel->data(m_treeModel->index(fi, TreeSizingAsins::ASIN), Qt::DisplayRole).toString();
            const QString pSku   = s.value(QStringLiteral("sizing/skus/") + pAsin).toString();
            if (!pSku.isEmpty()) m_treeModel->setSku(pAsin, pSku);

            for (int ci = 0; ci < m_treeModel->rowCount(pi); ++ci) {
                const QString cAsin = m_treeModel->data(m_treeModel->index(ci, TreeSizingAsins::ASIN, pi), Qt::DisplayRole).toString();
                const QString cSku  = s.value(QStringLiteral("sizing/skus/") + cAsin).toString();
                if (!cSku.isEmpty()) m_treeModel->setSku(cAsin, cSku);
            }
        }
    }
}

void PaneSizing::_ensureModel(const QDir &dir)
{
    if (m_treeModel)
        return;
    m_treeModel = std::make_unique<TreeSizingAsins>(dir);
    m_treeModel->setApiClient(m_api.get());
    ui->treeViewAsins->setModel(m_treeModel.get());
    ui->treeViewAsins->expandAll();

    if (!m_brokenChildTable) {
        m_brokenChildTable = new BrokenChildTable(this);
        QList<BrokenChildTable::MarketplaceSpec> specs;
        for (const AmazonMarketplace &mp : AmazonMarketplace::all())
            specs.append({mp.marketplaceId(), mp.countryCode()});
        m_brokenChildTable->setMarketplaces(specs);
        ui->tableViewBrokenChild->setModel(m_brokenChildTable);
        ui->tableViewBrokenChild->horizontalHeader()->setStretchLastSection(true);
        ui->tableViewBrokenChild->verticalHeader()->hide();
        _refreshBrokenAttrCombo();
    }

    connect(m_treeModel.get(), &TreeSizingAsins::marketplacesChecked,
            this, [this](const QStringList &codes) {
                ui->listWidgetCountries->clear();
                for (const QString &c : codes)
                    ui->listWidgetCountries->addItem(c);
                _refreshSizeGroupList();

                // Sync active/inactive state into the broken child table so that
                // marketplaces marked "(missing)" are excluded from fix targets.
                if (m_brokenChildTable) {
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
                    for (const QString &c : codes) {
                        const bool missing = c.contains(QStringLiteral("(missing)"),
                                                        Qt::CaseInsensitive);
                        const QString code = c.trimmed().toLower().split(QLatin1Char(' ')).first();
                        const QString mpId = kCodeToMp.value(code);
                        if (!mpId.isEmpty())
                            m_brokenChildTable->setMarketplaceActive(mpId, !missing);
                    }
                }

                // Kick off per-child, per-marketplace health checks + SKU resolution
                // now that we know which marketplaces are available.
                _loadBrokenChildData();
            });

    connect(m_treeModel.get(), &QAbstractItemModel::modelReset,
            this, [this]() {
                m_productType.clear();
                // m_productTitle is intentionally NOT cleared here: attributesFetched
                // fires before modelReset (before endResetModel), so m_productTitle
                // already holds the current product's title when this lambda runs.
                // We need it for _tryGuessBrandRangeFromTitle() below, which must
                // run after the model is populated (i.e. after endResetModel).

                // Reset product-specific state so the previous product's A+ content
                // is not shown while the new product loads. The working dir is either
                // restored below (if the folder already exists) or set by attributesFetched.
                m_productWorkingDir = QDir{};
                m_shoeWidths.clear();
                ui->lineEditSubWorkingDir->clear();
                _refreshSizeImageUploadStatus();
                m_aplusContent.reset();
                if (m_aplusModel) {
                    ui->aplusTreeView->setModel(nullptr);
                    delete m_aplusModel;
                    m_aplusModel = nullptr;
                }
                m_groupImages.clear();
                ui->listWidgetSizeGroups->clear();
                ui->buttonAplusDeleteVersion->setEnabled(false);
                _refreshAplusPreview();

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

    connect(m_treeModel.get(), &TreeSizingAsins::colorLog,
            this, [this](const QString& log) {
                const QString cur = ui->textEditAttributes->toPlainText();
                ui->textEditAttributes->setPlainText(
                    cur.isEmpty() ? log : cur + QStringLiteral("\n\n") + log);
            });

    connect(m_treeModel.get(), &TreeSizingAsins::attributesFetched,
            this, [this](const QStringList& bullets, const QStringList& materials,
                         const QString& mainImageUrl, const QString& asin,
                         const QString& title, const QStringList& shoeWidths) {
                m_productTitle = title;
                m_currentAsin = asin;
                m_shoeWidths = shoeWidths;
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
                // Shoe width: collect across all variants, show the widest available.
                // If both narrow and a regular/wider option exist, show the regular one
                // (the wider fit that most customers should order as baseline).
                if (!shoeWidths.isEmpty()) {
                    auto isNarrow = [](const QString &w) {
                        const QString l = w.toLower();
                        return l == QLatin1String("n")
                            || l.contains(QLatin1String("narrow"))
                            || l.contains(QLatin1String("slim"))
                            || l.contains(QStringLiteral("étroit"))
                            || l.contains(QLatin1String("schmal"));
                    };
                    QString display;
                    for (const QString &w : shoeWidths)
                        if (!isNarrow(w)) { display = w; break; }
                    if (display.isEmpty()) display = shoeWidths.first();
                    if (!text.isEmpty()) text += QLatin1Char('\n');
                    text += tr("Shoe width: ") + display + QLatin1Char('\n');
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
    ui->buttonFactorize->setEnabled(m_workingDir.exists());
    ui->buttonGenerateSizeTableXlsx->setEnabled(
        m_generatedSuccessfully && !m_sizeTableTemplatePath.isEmpty());
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

        QDoubleSpinBox *rangeSpin = nullptr;
        if (!field.noRange) {
            layout->addWidget(new QLabel(tr("range")));
            rangeSpin = new QDoubleSpinBox;
            rangeSpin->setRange(0, 50);
            rangeSpin->setDecimals(1);
            rangeSpin->setSingleStep(0.5);
            rangeSpin->setValue(0.0);
            layout->addWidget(rangeSpin);
        }

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
            if (w.rangeSpinBox) w.rangeSpinBox->setValue(s->value(base + QStringLiteral("/range")).toDouble());
        }
    }
}

bool PaneSizing::_rebuildSizeTable()
{
    const auto *cat = _currentCategory();
    if (!cat)
        return false;

    const QString sizeMode = ui->sizeRangeMain->mode();
    const bool useLetters  = (sizeMode == QLatin1String("letters"));
    const bool useHeight   = (sizeMode == QLatin1String("height"));
    const bool useOneSize  = (sizeMode == QLatin1String("one_size"));
    QString keyFrom, keyTo;
    QStringList letterHeaders;

    if (useOneSize) {
        QMap<QString, MeasurementInput> measurements;
        for (const auto &w : m_measurementWidgets)
            measurements[w.fieldId] = {w.refSpinBox->value(), w.stepSpinBox->value(),
                                       w.rangeSpinBox ? w.rangeSpinBox->value() : 0.0};
        try {
            ui->tableViewSizing->setModel(nullptr);
            delete m_sizeTableModel;
            m_sizeTableModel = cat->buildOneSizeTable(measurements, this);
            ui->tableViewSizing->setModel(m_sizeTableModel);
            ui->tableViewSizing->resizeColumnsToContents();
            ui->tableViewSizing->setEditTriggers(QAbstractItemView::NoEditTriggers);
            ui->buttonMakeEditable->setChecked(false);
            const QImage img = cat->renderImage(m_sizeTableModel);
            ui->labelGeneratedImage->setPixmap(QPixmap::fromImage(img));
            ui->labelGeneratedImage->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            m_generatedSuccessfully = true;
            return true;
        } catch (const std::exception &e) {
            QMessageBox::warning(this, tr("Generation failed"), QString::fromUtf8(e.what()));
            return false;
        }
    }

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
        measurements[w.fieldId] = {w.refSpinBox->value(), w.stepSpinBox->value(),
                                   w.rangeSpinBox ? w.rangeSpinBox->value() : 0.0};

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
        return true;
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Generation failed"), QString::fromUtf8(e.what()));
        return false;
    }
}

void PaneSizing::onGenSizeTablesClicked()
{
    m_generatedSuccessfully = false;

    if (!_rebuildSizeTable()) {
        updateButtonStates();
        return;
    }

    const auto *cat = _currentCategory();

    try {
        _saveProductSettings();

        // Persist spinbox values for this category (generic fallback)
        auto s = WorkingDirectoryManager::instance()->settings();
        const QString prefix = QStringLiteral("sizeCat/") + cat->displayName() + QLatin1Char('/');
        for (const auto &w : m_measurementWidgets) {
            const QString base = prefix + w.fieldId;
            s->setValue(base + QStringLiteral("/ref"),   w.refSpinBox->value());
            s->setValue(base + QStringLiteral("/step"),  w.stepSpinBox->value());
            if (w.rangeSpinBox) s->setValue(base + QStringLiteral("/range"), w.rangeSpinBox->value());
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

void PaneSizing::onLoadSubFolderClicked()
{
    const QDir sizingDir(m_workingDir.filePath(QStringLiteral("sizing")));
    if (!sizingDir.exists()) {
        QMessageBox::information(this, tr("Load Sub Folder"),
            tr("No sizing folder found in the working directory."));
        return;
    }

    // Collect all subdirectories with their metadata
    struct SubFolderEntry {
        QDateTime  date;
        QString    category;
        QString    folderName;
        QString    asin;
        bool       euParentFailed     = false;  // any EU mp has exists+!hasParent
        bool       amParentFailed     = false;  // any AM mp has exists+!hasParent
        bool       sizeImageGenerated = false;  // aplus/size_chart/size_chart.png exists
        bool       sizeImageUploaded  = false;  // sizing/sizeImageUploaded set in settings.ini
        QDateTime  aplusUploadedAt;             // aplus/uploadedAt set in settings.ini
    };
    QList<SubFolderEntry> entries;

    const QFileInfoList dirs = sizingDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                        QDir::NoSort);
    for (const QFileInfo &fi : dirs) {
        SubFolderEntry e;
        e.folderName = fi.fileName();
        e.date       = fi.lastModified();

        // Extract ASIN: everything before the first '-'
        const int dash = e.folderName.indexOf(QLatin1Char('-'));
        e.asin = (dash > 0) ? e.folderName.left(dash) : e.folderName;

        // Read category from settings.ini if present
        const QSettings ini(fi.filePath() + QStringLiteral("/settings.ini"),
                            QSettings::IniFormat);
        e.category = ini.value(QStringLiteral("sizing/type")).toString();

        // Parse broken_child_health.json for EU/America broken-parent status
        {
            static const QSet<QString> euSet = {
                QStringLiteral("A1F83G8C2ARO7P"),  // UK
                QStringLiteral("A1PA6795UKMFR9"),  // DE
                QStringLiteral("A13V1IB3VIYZZH"),  // FR
                QStringLiteral("A1RKKUPIHCS9HS"),  // ES
                QStringLiteral("APJ6JRA9NG5V4"),   // IT
                QStringLiteral("A1805IZSGTT6HS"),  // NL
            };
            static const QSet<QString> amSet = {
                QStringLiteral("ATVPDKIKX0DER"),   // US/COM
                QStringLiteral("A2EUQ1WTGCTBG2"),  // CA
            };

            QFile healthFile(fi.filePath() + QStringLiteral("/broken_child_health.json"));
            if (healthFile.open(QIODevice::ReadOnly)) {
                const QJsonDocument doc = QJsonDocument::fromJson(healthFile.readAll());
                healthFile.close();
                const QJsonObject root = doc.object();

                const QJsonArray mps  = root.value(QStringLiteral("marketplaces")).toArray();
                const QJsonArray rows = root.value(QStringLiteral("rows")).toArray();

                for (const QJsonValue &rowVal : rows) {
                    const QJsonArray health = rowVal.toObject()
                        .value(QStringLiteral("health")).toArray();
                    for (int idx = 0; idx < health.size() && idx < mps.size(); ++idx) {
                        const QJsonObject h = health[idx].toObject();
                        const bool exists    = h.value(QStringLiteral("exists")).toBool();
                        const bool hasParent = h.value(QStringLiteral("hasParent")).toBool();
                        if (exists && !hasParent) {
                            const QString mp = mps[idx].toString();
                            if (euSet.contains(mp))
                                e.euParentFailed = true;
                            if (amSet.contains(mp))
                                e.amParentFailed = true;
                        }
                    }
                }
            }
        }

        // Size image upload status
        e.sizeImageGenerated = QFileInfo::exists(
            fi.filePath() + QStringLiteral("/aplus/size_chart/size_chart.png"));
        e.sizeImageUploaded = ini.contains(QStringLiteral("sizing/sizeImageUploaded"));

        // A+ content upload date
        const QVariant aplusUploadedVar = ini.value(QStringLiteral("aplus/uploadedAt"));
        if (aplusUploadedVar.isValid() && !aplusUploadedVar.toString().isEmpty())
            e.aplusUploadedAt = QDateTime::fromString(
                aplusUploadedVar.toString(), Qt::ISODate);

        entries.append(e);
    }

    if (entries.isEmpty()) {
        QMessageBox::information(this, tr("Load Sub Folder"),
            tr("No product sub-folders found."));
        return;
    }

    // Sort by date descending (most recent first)
    std::sort(entries.begin(), entries.end(),
              [](const SubFolderEntry &a, const SubFolderEntry &b) {
                  return a.date > b.date;
              });

    // Build dialog
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Load Sub Folder"));
    dlg.resize(900, 480);
    auto *layout = new QVBoxLayout(&dlg);

    auto *table = new QTableWidget(entries.size(), 7, &dlg);
    table->setHorizontalHeaderLabels(
        {tr("Date"), tr("Category"), tr("EU Parent"), tr("America"), tr("Size Image"), tr("A+ Upload"), tr("Folder")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->hide();

    for (int i = 0; i < entries.size(); ++i) {
        const SubFolderEntry &e = entries[i];
        table->setItem(i, 0, new QTableWidgetItem(
            e.date.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        table->setItem(i, 1, new QTableWidgetItem(e.category));

        // EU Parent column
        {
            auto *it = new QTableWidgetItem(
                e.euParentFailed ? QStringLiteral("✗") : QStringLiteral("✓"));
            it->setTextAlignment(Qt::AlignCenter);
            if (e.euParentFailed)
                it->setBackground(QColor(220, 60, 60));
            table->setItem(i, 2, it);
        }
        // America column
        {
            auto *it = new QTableWidgetItem(
                e.amParentFailed ? QStringLiteral("✗") : QStringLiteral("✓"));
            it->setTextAlignment(Qt::AlignCenter);
            if (e.amParentFailed)
                it->setBackground(QColor(220, 60, 60));
            table->setItem(i, 3, it);
        }
        // Size Image upload status column
        {
            auto *it = new QTableWidgetItem();
            it->setTextAlignment(Qt::AlignCenter);
            if (!e.sizeImageGenerated) {
                it->setText(QStringLiteral("—"));
            } else if (e.sizeImageUploaded) {
                it->setText(QStringLiteral("✓"));
                it->setForeground(QColor(46, 125, 50));   // dark green
            } else {
                it->setText(QStringLiteral("✗"));
                it->setBackground(QColor(220, 60, 60));
            }
            table->setItem(i, 4, it);
        }
        // A+ Upload column
        {
            auto *it = new QTableWidgetItem();
            it->setTextAlignment(Qt::AlignCenter);
            if (e.aplusUploadedAt.isValid()) {
                it->setText(e.aplusUploadedAt.toString(QStringLiteral("yyyy-MM-dd")));
                it->setForeground(QColor(46, 125, 50));   // dark green
            } else {
                it->setText(QStringLiteral("—"));
            }
            table->setItem(i, 5, it);
        }
        table->setItem(i, 6, new QTableWidgetItem(e.folderName));
    }
    table->resizeColumnToContents(0);
    table->resizeColumnToContents(1);
    table->resizeColumnToContents(2);
    table->resizeColumnToContents(3);
    table->resizeColumnToContents(4);
    table->resizeColumnToContents(5);
    table->selectRow(0);
    layout->addWidget(table);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    // Double-click on a row also accepts
    connect(table, &QTableWidget::cellDoubleClicked, &dlg, &QDialog::accept);
    layout->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const int row = table->currentRow();
    if (row < 0 || row >= entries.size())
        return;

    const QString asin = entries[row].asin;
    if (asin.isEmpty())
        return;

    const QDir defaultDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    _ensureModel(defaultDir);
    _refreshApi();
    m_treeModel->load(asin);
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

void PaneSizing::onPickSizeTableTemplateClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select size table template"), {}, tr("Excel (*.xlsx)"));
    if (path.isEmpty())
        return;
    m_sizeTableTemplatePath = path;
    ui->lineEditSizeTableTemplate->setText(path);
    updateButtonStates();
}

void PaneSizing::onGenerateSizeTableXlsxClicked()
{
    _saveToSizeTableFolder();
}

void PaneSizing::onAddSkusFromTemplateClicked()
{
    _addSkusFromTemplate();
}

void PaneSizing::onFactorizeSizeTables()
{
    if (!m_workingDir.exists()) return;

    const QDir sizingRoot(m_workingDir.filePath(QStringLiteral("sizing")));
    if (!sizingRoot.exists()) {
        QMessageBox::information(this, tr("Factorize"),
            tr("No sizing directory found."));
        return;
    }

    // --- Scan all product subdirs for size table txt files ---
    struct ProductInfo {
        QString dirPath;
        QString parentAsin;
        QString title;
        QStringList childAsins;
        QMap<QString, QString> groupTxtContents; // group key → file content
    };

    QList<ProductInfo> products;

    const QStringList entries = sizingRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry == QStringLiteral("factorized-size-tables")) continue;

        const QString productDirPath = sizingRoot.filePath(entry);
        const QDir sizeTableDir(productDirPath + QStringLiteral("/size-table"));
        if (!sizeTableDir.exists()) continue;

        // Parse dir name: "{parentAsin}-{title}" or just "{parentAsin}"
        const int dashIdx = entry.indexOf(QLatin1Char('-'));
        const QString parentAsin = (dashIdx > 0) ? entry.left(dashIdx) : entry;
        const QString title      = (dashIdx > 0) ? entry.mid(dashIdx + 1) : QString{};

        // Read size_table_*.txt files
        const QStringList txtFiles = sizeTableDir.entryList(
            QStringList{QStringLiteral("size_table_*.txt")}, QDir::Files);
        if (txtFiles.isEmpty()) continue;

        ProductInfo info;
        info.dirPath    = productDirPath;
        info.parentAsin = parentAsin;
        info.title      = title;

        for (const QString &txtFile : txtFiles) {
            // "size_table_" = 11 chars, ".txt" = 4 chars
            const QString group = txtFile.mid(11, txtFile.length() - 15);
            QFile f(sizeTableDir.filePath(txtFile));
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                info.groupTxtContents.insert(group, QString::fromUtf8(f.readAll()));
        }
        if (info.groupTxtContents.isEmpty()) continue;

        // Read asin.txt
        QFile asinFile(sizeTableDir.filePath(QStringLiteral("asin.txt")));
        if (asinFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(asinFile.readAll());
            for (const QString &line : content.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                info.childAsins << line.trimmed();
        }

        products << info;
    }

    if (products.isEmpty()) {
        QMessageBox::information(this, tr("Factorize"),
            tr("No size tables found. Generate size tables first."));
        return;
    }

    // --- Group by fingerprint ---
    // Fingerprint = sorted concatenation of "group|||content===" for all groups.
    QMap<QString, QList<int>> fingerprints;
    for (int i = 0; i < products.size(); ++i) {
        QString fp;
        for (auto it = products[i].groupTxtContents.constBegin();
             it != products[i].groupTxtContents.constEnd(); ++it) {
            fp += it.key() + QStringLiteral("|||") + it.value() + QStringLiteral("===");
        }
        fingerprints[fp].append(i);
    }

    // --- Create factorized-size-tables output ---
    m_workingDir.mkpath(QStringLiteral("sizing/factorized-size-tables"));
    const QDir factDir(m_workingDir.filePath(QStringLiteral("sizing/factorized-size-tables")));

    int groupCount = 0;
    for (auto it = fingerprints.constBegin(); it != fingerprints.constEnd(); ++it) {
        const QList<int> &indices = it.value();
        const ProductInfo &rep = products[indices.first()];

        // Merge and sort child ASINs from all products in this group
        QStringList allAsins;
        for (int idx : indices)
            for (const QString &a : products[idx].childAsins)
                if (!a.isEmpty() && !allAsins.contains(a))
                    allAsins << a;
        allAsins.sort();

        // Dir name: first child ASIN (or parent ASIN) + title from representative
        const QString firstAsin = allAsins.isEmpty() ? rep.parentAsin : allAsins.first();
        QString dirName = firstAsin;
        if (!rep.title.isEmpty())
            dirName += QLatin1Char('-') + rep.title;

        const QString factSubdirPath = factDir.filePath(dirName);
        QDir().mkpath(factSubdirPath);

        // Write merged asin.txt
        if (!allAsins.isEmpty()) {
            QFile f(factSubdirPath + QStringLiteral("/asin.txt"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                f.write(allAsins.join(QLatin1Char('\n')).toUtf8());
        }

        // Copy xlsx + txt from representative product
        const QDir repSizeTableDir(rep.dirPath + QStringLiteral("/size-table"));
        const QStringList xlsxFiles = repSizeTableDir.entryList(
            QStringList{QStringLiteral("filled_template_*.xlsx")}, QDir::Files);
        for (const QString &xlsx : xlsxFiles) {
            const QString dst = factSubdirPath + QLatin1Char('/') + xlsx;
            QFile::remove(dst);
            QFile::copy(repSizeTableDir.filePath(xlsx), dst);
        }
        for (auto git = rep.groupTxtContents.constBegin();
             git != rep.groupTxtContents.constEnd(); ++git) {
            const QString txtName = QStringLiteral("size_table_%1.txt").arg(git.key());
            QFile f(factSubdirPath + QLatin1Char('/') + txtName);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                f.write(git.value().toUtf8());
        }

        ++groupCount;
    }

    QMessageBox::information(this, tr("Factorize"),
        tr("Scanned %1 product(s) → %2 unique size table group(s).\n"
           "Output: sizing/factorized-size-tables/")
        .arg(products.size()).arg(groupCount));
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

// Injects explicit per-color reference images into the image_group slot.
// This prevents the AI from exploring the product directory and picking up
// excluded color images that may still be on disk from a previous session.
static void injectGroupShotColorHints(QList<ImageSlotSpec> &specs,
                                      const QStringList &activeColors,
                                      const QStringList &excludedColorIds,
                                      const QDir &productDir)
{
    for (ImageSlotSpec &spec : specs) {
        if (spec.elementId != QStringLiteral("image_group")) continue;

        QStringList lines;
        for (const QString &color : activeColors) {
            const QString cid = colorToFileSegment(color);
            for (const QString &f : productDir.entryList(QDir::Files)) {
                if (!f.startsWith(cid + QLatin1Char('-'))
                    && !f.startsWith(cid + QLatin1Char('_')))
                    continue;
                // Reject files that belong to an excluded color
                bool excluded = false;
                for (const QString &ecId : excludedColorIds) {
                    if (f.startsWith(ecId + QLatin1Char('-'))
                        || f.startsWith(ecId + QLatin1Char('_')))
                    { excluded = true; break; }
                }
                if (!excluded) {
                    lines << QStringLiteral("%1: %2")
                             .arg(color, productDir.absoluteFilePath(f));
                    break;
                }
            }
        }
        if (lines.isEmpty()) continue;

        const QString hint = QCoreApplication::translate("PaneSizing",
            "Reference photos for each color variant you must depict "
            "(use ONLY these %1 colors — do NOT include any other color variant):\n%2")
            .arg(lines.size()).arg(lines.join(QLatin1Char('\n')));
        spec.desktopPrompt += QStringLiteral("\n\n") + hint;
        spec.mobilePrompt  += QStringLiteral("\n\n") + hint;
    }
}

void PaneSizing::_downloadVariantImages(const QList<QPair<QString, QStringList>> &colorImages)
{
    if (!m_productWorkingDir.exists() || colorImages.isEmpty())
        return;

    m_colorVariants = colorImages;

    ui->treeWidgetColorVariants->clear();
    m_variantImagePaths.clear();
    m_variantBrowsedImagePath.clear();
    ui->labelBrowsedImage->setText(tr("(no browsed image)"));
    ui->labelBrowsedImage->setPixmap({});

    const bool multiColor = colorImages.size() > 1;
    const QString dir = m_productWorkingDir.absolutePath();

    for (const auto &[color, urls] : colorImages) {
        const bool excluded = !color.isEmpty() && m_aplusExcludedColors.contains(color);

        auto *colorItem = new QTreeWidgetItem(ui->treeWidgetColorVariants);
        colorItem->setText(0, color.isEmpty() ? tr("(default)") : color);
        if (excluded) {
            colorItem->setText(0, tr("%1 (excluded)").arg(color));
            colorItem->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
        }

        const QString prefix = multiColor
            ? colorToFileSegment(color) + QLatin1Char('-')
            : QString{};
        int index = 1;
        bool firstForColor = true;

        for (const QString &url : urls) {
            const QString filename = QStringLiteral("%1image-%2.jpg")
                .arg(prefix).arg(index, 2, 10, QLatin1Char('0'));
            const QString localPath = dir + QLatin1Char('/') + filename;
            m_variantImagePaths.append(localPath);

            auto *imgItem = new QTreeWidgetItem(colorItem);
            imgItem->setText(0, filename);
            imgItem->setData(0, Qt::UserRole, localPath);

            if (firstForColor) {
                colorItem->setData(0, Qt::UserRole, localPath);
                firstForColor = false;
            }

            if (!excluded && !QFileInfo::exists(localPath)) {
                QNetworkRequest req{QUrl(url)};
                QNetworkReply *reply = m_imageNam->get(req);
                const QString savedPath = localPath;
                connect(reply, &QNetworkReply::finished, this,
                        [this, reply, savedPath]() {
                    reply->deleteLater();
                    if (reply->error() != QNetworkReply::NoError)
                        return;
                    QFile f(savedPath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(reply->readAll());
                        f.close();
                    }
                    // Refresh display if this image is currently selected
                    auto *cur = ui->treeWidgetColorVariants->currentItem();
                    if (cur && cur->data(0, Qt::UserRole).toString() == savedPath)
                        onVariantTreeSelectionChanged();
                });
            }
            ++index;
        }
        colorItem->setExpanded(true);
    }

    if (ui->treeWidgetColorVariants->topLevelItemCount() > 0) {
        ui->treeWidgetColorVariants->setCurrentItem(
            ui->treeWidgetColorVariants->topLevelItem(0));
        onVariantTreeSelectionChanged();
    }
}

void PaneSizing::onVariantTreeSelectionChanged()
{
    auto *item = ui->treeWidgetColorVariants->currentItem();
    if (!item) { ui->labelVariantImage->clear(); return; }

    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) { ui->labelVariantImage->clear(); return; }

    const QPixmap pm(path);
    if (pm.isNull()) {
        ui->labelVariantImage->setText(tr("(image not yet downloaded)"));
        return;
    }
    const QSize sz = ui->labelVariantImage->size().isEmpty()
                   ? QSize(400, 300) : ui->labelVariantImage->size();
    ui->labelVariantImage->setPixmap(
        pm.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PaneSizing::onBrowseVariantImageClicked()
{
    const QString startDir = m_productWorkingDir.exists()
                           ? m_productWorkingDir.absolutePath() : QString{};
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select image to upload"),
        startDir,
        tr("Images (*.jpg *.jpeg *.png)"));
    if (path.isEmpty()) return;

    m_variantBrowsedImagePath = path;
    const QPixmap pm(path);
    if (!pm.isNull()) {
        const QSize sz = ui->labelBrowsedImage->size().isEmpty()
                       ? QSize(400, 300) : ui->labelBrowsedImage->size();
        ui->labelBrowsedImage->setPixmap(
            pm.scaled(sz, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        ui->labelBrowsedImage->setText(tr("(failed to load image)"));
    }
}

void PaneSizing::onUploadVariantImageClicked()
{
    int imageIndex;
    if (ui->radioButtonVariantAppend->isChecked())
        imageIndex = -1;
    else if (ui->radioButtonVariantReplaceLast->isChecked())
        imageIndex = -2;
    else
        imageIndex = ui->spinBoxVariantImagePos->value();

    m_variantUploadTask = _uploadVariantImage(imageIndex);
}

QCoro::Task<void> PaneSizing::_fetchAllSkusCached(const QString &marketplaceId,
                                                  QHash<QString, QString> *asinToSku,
                                                  bool forceRefresh,
                                                  QHash<QString, QPair<QString,QString>> *asinToGtin)
{
    asinToSku->clear();

    if (!m_workingDir.exists()) {
        co_await m_api->fetchAllSkusViaReport(marketplaceId, asinToSku,
                                               nullptr, asinToGtin);
        co_return;
    }

    const QString cachePath = m_workingDir.filePath(
        QStringLiteral("sizing/sku_cache_%1.json").arg(marketplaceId));
    QFile cacheFile(cachePath);

    // 1. Try to load from cache (SKUs only — GTINs always fetched live from report)
    if (!forceRefresh && cacheFile.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(cacheFile.readAll()).object();
        for (auto it = root.begin(); it != root.end(); ++it)
            asinToSku->insert(it.key(), it.value().toString());

        if (!asinToSku->isEmpty()) {
            qDebug() << "PaneSizing: loaded" << asinToSku->size()
                     << "SKUs from global cache for" << marketplaceId;
            co_return;
        }
    }

    // 2. Not in cache (or cache empty or forced) -> fetch from Amazon
    co_await m_api->fetchAllSkusViaReport(marketplaceId, asinToSku,
                                           nullptr, asinToGtin);

    // 3. Save to cache if successful
    if (!asinToSku->isEmpty()) {
        m_workingDir.mkpath(QStringLiteral("sizing"));
        if (cacheFile.open(QIODevice::WriteOnly)) {
            QJsonObject root;
            for (auto it = asinToSku->constBegin(); it != asinToSku->constEnd(); ++it)
                root.insert(it.key(), it.value());
            cacheFile.write(QJsonDocument(root).toJson());
            qDebug() << "PaneSizing: saved" << asinToSku->size()
                     << "SKUs to global cache for" << marketplaceId;
        }
    }
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

    // Hide/show this dialog when the user switches tabs.
    // currentChanged fires after Qt has already updated page visibility, so
    // panePtr->isVisible() reliably reflects whether the pane is the active tab.
    // This avoids currentWidget() comparison (which can fail when the page is
    // wrapped by an internal QStackedWidget entry) and avoids QEvent::Hide (which
    // some window managers emit spuriously during exec() calls).
    {
        QTabWidget *tabWgt = nullptr;
        for (QWidget *p = parent; p && !tabWgt; p = p->parentWidget())
            tabWgt = qobject_cast<QTabWidget *>(p);
        if (tabWgt) {
            QPointer<QDialog>  dlgPtr(progressDlg);
            QPointer<QWidget>  panePtr(parent);
            auto *conn = new QObject(progressDlg);
            QObject::connect(tabWgt, &QTabWidget::currentChanged, conn,
                [dlgPtr, panePtr](int) {
                    if (!dlgPtr || !panePtr) return;
                    if (panePtr->isVisible()) dlgPtr->show();
                    else                      dlgPtr->hide();
                });
        }
    }

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
    m_uploadTask = _uploadAplusContent();
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

// Forward declaration — defined later in this file.
static QString extractFaqContent(const QString &raw);

QCoro::Task<void> PaneSizing::_uploadAplusContent()
{
    if (!m_aplusContent || !m_aplusApi) co_return;

    // CLI and working directory for FAQ regeneration on keyword-violation retries.
    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    const QDir &faqEffDir = m_productWorkingDir.exists() ? m_productWorkingDir : m_workingDir;
    const QString faqWorkDir = faqEffDir.isAbsolute() ? faqEffDir.path() : QString{};

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

            // Build the base module list (size chart + images). The FAQ is kept
            // separately so it can be regenerated and retried on keyword violations.
            QJsonArray moduleListBase;

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
                        moduleListBase.append(buildImageModule(uploadId, sc->displayName, imgW, imgH,
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
                moduleListBase.append(buildImageModule(uploadId, info.displayName, imgW, imgH, imgHeadline));
            }

            // --- FAQ text (kept separate for retry on keyword violations) ---
            // faqKey is hoisted so the retry loop can use it for pushVersion.
            const QString faqKey = APlusUploadDialog::faqLangKeyForMarketplace(mpId);
            QString faqText;
            if (addFaq) {
                const auto *faq = findAplusElement(infos, APlusElementType::Faq,
                    QStringLiteral("faq_") + faqKey, QStringLiteral("faq"));
                if (!faq) {
                    appendAplusLog(progressUi.logPtr,
                        tr("  ⚠ FAQ '%1' not found — skipped").arg(faqKey));
                } else {
                    faqText = faq->textContent.isEmpty()
                        ? QStringLiteral("(no FAQ content)") : faq->textContent;
                    appendAplusLog(progressUi.logPtr,
                        tr("  ✓ FAQ '%1' included (%2 chars)").arg(faq->displayName).arg(faqText.size()));
                }
            }

            if (moduleListBase.isEmpty() && faqText.isEmpty()) {
                appendAplusLog(progressUi.logPtr, tr("  ⚠ No modules — skipping this upload."));
                step += fixedPerUpload;
                continue;
            }

            // --- Resolve child ASINs and document name (once, before retry loop) ---
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

            // Persist a rewritten FAQ as a new version so it survives restart.
            // Called after every successful in-upload rewrite.
            QTextEdit *const faqLogPtr = progressUi.logPtr;
            auto saveFaqRewrite = [this, &faqKey, faqLogPtr](const QString &text) {
                if (!m_aplusContent || text.isEmpty()) {
                    appendAplusLog(faqLogPtr, tr("  ⚠ saveFaqRewrite: skipped (no content or empty text)"));
                    return;
                }
                QDir aplusDir = m_aplusContent->dir();
                appendAplusLog(faqLogPtr, tr("  ℹ saveFaqRewrite: dir=%1").arg(aplusDir.absolutePath()));
                if (!aplusDir.mkpath(QStringLiteral("faq"))) {
                    appendAplusLog(faqLogPtr, tr("  ⚠ saveFaqRewrite: mkpath faq/ failed"));
                    return;
                }
                const QString sfx = (faqKey == QLatin1String("en"))
                    ? QString{} : QLatin1Char('_') + faqKey;
                const QString relPath = QStringLiteral("faq/v_") + _aplusTimestamp()
                                      + sfx + QStringLiteral(".txt");
                const QString absPath = aplusDir.filePath(relPath);
                {
                    QFile f(absPath);
                    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        appendAplusLog(faqLogPtr, tr("  ⚠ saveFaqRewrite: cannot write %1").arg(absPath));
                        return;
                    }
                    f.write(text.toUtf8());
                }
                APlusVersion ver;
                ver.generated   = QDateTime::currentDateTime();
                ver.desktopFile = ver.mobileFile = relPath;
                const QString elemId  = QStringLiteral("faq_") + faqKey;
                const APlusElement *e = m_aplusContent->findElement(elemId);
                const QString dispName = e ? e->displayName
                                           : tr("FAQ (%1)").arg(faqKey);
                m_aplusContent->pushVersion(elemId, APlusElementType::Faq, dispName, ver);
                if (m_aplusModel) _rebuildAplusModel();
                appendAplusLog(faqLogPtr, tr("  ✓ saveFaqRewrite: saved %1").arg(relPath));
            };

            // === Create / validate / submit — retried on FAQ keyword violations ===
            const int kMaxFaqRetries = 5;
            int faqAttempt = 0;
            QStringList allBlockedKeywords; // accumulated across retries for cumulative feedback
            while (true) {
                // Assemble full module list from base + current FAQ text.
                QJsonArray moduleList = moduleListBase;
                if (!faqText.isEmpty())
                    moduleList.append(buildFaqModule(faqText));

                // --- Create content document ---
                // Step counter advances only on first attempt (retries aren't pre-budgeted).
                if (faqAttempt == 0) ++step;
                setAplusStatus(progressUi,
                    faqAttempt == 0 ? tr("Creating A+ content document…")
                                    : tr("Retry %1 — Creating A+ content document…").arg(faqAttempt),
                    step - 1);
                appendAplusLog(progressUi.logPtr,
                    faqAttempt == 0
                        ? tr("▶ Creating document (locale: %1)").arg(locale)
                        : tr("▶ Retry %1 — Creating document (locale: %2)").arg(faqAttempt).arg(locale));

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
                if (faqAttempt == 0) ++step;
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
                if (faqAttempt == 0) ++step;
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
                    if (faqAttempt == 0) ++step;
                    setAplusStatus(progressUi, tr("Submitting for approval…"), step - 1);
                    appendAplusLog(progressUi.logPtr, tr("▶ Submitting for approval…"));
                    bool approvalOk = false;
                    QStringList blocked;
                    co_await m_aplusApi->submitForApproval(contentReferenceKey, mpId, &approvalOk, &blocked);
                    if (!approvalOk) {
                        // Accumulate all blocked keywords across retries for cumulative feedback.
                        for (const QString &kw : std::as_const(blocked))
                            if (!allBlockedKeywords.contains(kw))
                                allBlockedKeywords.append(kw);

                        if (!blocked.isEmpty() && cli && !faqText.isEmpty()
                                && faqAttempt < kMaxFaqRetries) {
                            ++faqAttempt;
                            appendAplusLog(progressUi.logPtr,
                                tr("  ↩ FAQ rejected (attempt %1/%2) — forbidden: %3 — regenerating…")
                                    .arg(faqAttempt).arg(kMaxFaqRetries)
                                    .arg(blocked.join(QStringLiteral(", "))));
                            appendAplusLog(progressUi.logPtr,
                                tr("  ℹ FAQ sent for rewrite:\n%1")
                                    .arg(faqText.left(1000)));
                            // Pass ALL accumulated forbidden words so the model doesn't
                            // reintroduce words that were banned in earlier rounds.
                            const QString rewritePrompt =
                                QStringLiteral("Rewrite the following Amazon A+ Content FAQ.\n"
                                               "Keep the SAME language as the input (language code: ")
                                + faqKey
                                + QStringLiteral(").\n"
                                               "The words/phrases below are STRICTLY forbidden by Amazon's "
                                               "community guidelines and MUST NOT appear anywhere in the output "
                                               "(this list grows with each rejection round — honour all of them): ")
                                + allBlockedKeywords.join(QStringLiteral(", "))
                                + QStringLiteral(".\n\n"
                                               "Rules:\n"
                                               "- Replace or rephrase every sentence containing a forbidden word\n"
                                               "- Keep all Q&A pairs\n"
                                               "- Keep the Q:/A: format exactly\n"
                                               "- Return ONLY the FAQ, no extra text\n\n")
                                + faqText;
                            // Bridge runPromptAsync → QFuture to avoid co_await on a QProcess
                            // signal: the QCoro::Task<T> temporary from runPrompt() has its
                            // lifetime managed by GCC's frame analysis, which may prematurely
                            // call ~Task() → handle.destroy() while QProcess::finished is
                            // still queued, crashing the QCoroSignal resume lambda.
                            QPromise<CliRunResult> cliPromise;
                            cliPromise.start();
                            QFuture<CliRunResult> cliFuture = cliPromise.future();
                            {
                                auto sp = QSharedPointer<QPromise<CliRunResult>>::create(
                                    std::move(cliPromise));
                                cli->runPromptAsync(rewritePrompt, faqWorkDir, this,
                                    [sp](CliRunResult r) mutable {
                                        sp->addResult(std::move(r));
                                        sp->finish();
                                    });
                            }
                            const CliRunResult r = co_await qCoro(cliFuture).result();
                            const QString newFaq = extractFaqContent(r.output.trimmed());
                            appendAplusLog(progressUi.logPtr,
                                tr("  ℹ CLI rewrite output:\n%1")
                                    .arg(r.output.trimmed().left(1000)));
                            if (!newFaq.isEmpty()) {
                                faqText = newFaq;
                                saveFaqRewrite(newFaq);
                                // Pre-flight: verify no blocked keyword survived (saves an Amazon slot)
                                bool preflightFailed = false;
                                QString preflightPrompt;
                                {
                                    const QString lowerFaq = faqText.toLower();
                                    QStringList still;
                                    for (const QString &kw : std::as_const(allBlockedKeywords))
                                        if (lowerFaq.contains(kw.toLower()))
                                            still.append(kw);
                                    if (!still.isEmpty()) {
                                        preflightFailed = true;
                                        appendAplusLog(progressUi.logPtr,
                                            tr("  ⚠ Rewrite still contains %1 — targeted re-rewrite…")
                                                .arg(still.join(QStringLiteral(", "))));
                                        preflightPrompt =
                                            QStringLiteral("CRITICAL: The FAQ below STILL contains the "
                                                           "forbidden word(s): ")
                                            + still.join(QStringLiteral(", "))
                                            + QStringLiteral(".\nSearch EVERY line for these words and any "
                                                             "compound forms that include them as a substring. "
                                                             "Replace EACH occurrence with a different synonym. "
                                                             "Keep the Q:/A: format and all pairs unchanged.\n"
                                                             "Return ONLY the corrected FAQ.\n\n")
                                            + faqText;
                                    }
                                } // lowerFaq, still destroyed
                                if (preflightFailed) {
                                    QPromise<CliRunResult> fixProm;
                                    fixProm.start();
                                    QFuture<CliRunResult> fixFut = fixProm.future();
                                    {
                                        auto sp2 = QSharedPointer<QPromise<CliRunResult>>::create(
                                            std::move(fixProm));
                                        cli->runPromptAsync(preflightPrompt, faqWorkDir, this,
                                            [sp2](CliRunResult r2) mutable {
                                                sp2->addResult(std::move(r2));
                                                sp2->finish();
                                            });
                                    }
                                    const CliRunResult fixR = co_await qCoro(fixFut).result();
                                    const QString fixedFaq = extractFaqContent(fixR.output.trimmed());
                                    appendAplusLog(progressUi.logPtr,
                                        tr("  ℹ Targeted re-rewrite:\n%1")
                                            .arg(fixR.output.trimmed().left(800)));
                                    if (!fixedFaq.isEmpty()) {
                                        faqText = fixedFaq;
                                        saveFaqRewrite(fixedFaq);
                                    }
                                }
                                continue; // retry with (possibly re-rewritten) FAQ
                            }
                            appendAplusLog(progressUi.logPtr,
                                tr("  ⚠ CLI rewrite returned empty output — keeping original FAQ"));
                        }

                        // All retries exhausted (or no CLI / non-keyword failure).
                        // If a FAQ was involved, let the user skip, regenerate, or abort.
                        if (!allBlockedKeywords.isEmpty() && !faqText.isEmpty()) {
                            bool userSkippedFaq = false;
                            while (true) {
                                const QString detail = tr(
                                    "The FAQ was rejected by Amazon %1 time(s).\n\n"
                                    "All forbidden words found: %2\n\n"
                                    "Skip FAQ and upload without it, regenerate and retry, "
                                    "or interrupt the entire upload?")
                                        .arg(faqAttempt)
                                        .arg(allBlockedKeywords.join(QStringLiteral(", ")));
                                bool doAbort = false, doSkip = false;
                                {
                                    // Scoped so QMessageBox is destroyed before co_await.
                                    QMessageBox msgBox(this);
                                    msgBox.setWindowTitle(tr("FAQ blocked by Amazon"));
                                    msgBox.setText(detail);
                                    QPushButton *skipBtn  = msgBox.addButton(tr("Skip FAQ"),   QMessageBox::AcceptRole);
                                    QPushButton *regenBtn = msgBox.addButton(tr("Regenerate"), QMessageBox::ActionRole);
                                    QPushButton *abortBtn = msgBox.addButton(tr("Interrupt"),  QMessageBox::RejectRole);
                                    msgBox.setDefaultButton(cli ? regenBtn : skipBtn);
                                    if (!cli) regenBtn->setEnabled(false);
                                    msgBox.exec();
                                    const QAbstractButton *clicked = msgBox.clickedButton();
                                    doAbort = (clicked == abortBtn);
                                    doSkip  = (clicked == skipBtn);
                                }
                                if (doAbort) {
                                    appendAplusLog(progressUi.logPtr,
                                        tr("  ✗ Upload interrupted by user."));
                                    co_return;
                                }
                                if (doSkip) {
                                    userSkippedFaq = true;
                                    break;
                                }
                                // Regenerate: same rewrite logic as the auto-retry above.
                                appendAplusLog(progressUi.logPtr,
                                    tr("  ↩ Manual FAQ regeneration — forbidden: %1…")
                                        .arg(allBlockedKeywords.join(QStringLiteral(", "))));
                                appendAplusLog(progressUi.logPtr,
                                    tr("  ℹ FAQ sent for rewrite:\n%1")
                                        .arg(faqText.left(1000)));
                                const QString rewritePrompt =
                                    QStringLiteral("Rewrite the following Amazon A+ Content FAQ.\n"
                                                   "Keep the SAME language as the input (language code: ")
                                    + faqKey
                                    + QStringLiteral(").\n"
                                                   "The words/phrases below are STRICTLY forbidden by Amazon's "
                                                   "community guidelines and MUST NOT appear anywhere in the output "
                                                   "(this list grows with each rejection round — honour all of them): ")
                                    + allBlockedKeywords.join(QStringLiteral(", "))
                                    + QStringLiteral(".\n\n"
                                                   "Rules:\n"
                                                   "- Replace or rephrase every sentence containing a forbidden word\n"
                                                   "- Keep all Q&A pairs\n"
                                                   "- Keep the Q:/A: format exactly\n"
                                                   "- Return ONLY the FAQ, no extra text\n\n")
                                    + faqText;
                                QPromise<CliRunResult> cliPromise2;
                                cliPromise2.start();
                                QFuture<CliRunResult> cliFuture2 = cliPromise2.future();
                                {
                                    auto sp = QSharedPointer<QPromise<CliRunResult>>::create(
                                        std::move(cliPromise2));
                                    cli->runPromptAsync(rewritePrompt, faqWorkDir, this,
                                        [sp](CliRunResult r) mutable {
                                            sp->addResult(std::move(r));
                                            sp->finish();
                                        });
                                }
                                const CliRunResult r2 = co_await qCoro(cliFuture2).result();
                                const QString newFaq = extractFaqContent(r2.output.trimmed());
                                appendAplusLog(progressUi.logPtr,
                                    tr("  ℹ CLI rewrite output:\n%1")
                                        .arg(r2.output.trimmed().left(1000)));
                                if (!newFaq.isEmpty()) {
                                    faqText = newFaq;
                                    saveFaqRewrite(newFaq);
                                    // Pre-flight: verify no blocked keyword survived
                                    bool preflightFailed2 = false;
                                    QString preflightPrompt2;
                                    {
                                        const QString lowerFaq2 = faqText.toLower();
                                        QStringList still2;
                                        for (const QString &kw : std::as_const(allBlockedKeywords))
                                            if (lowerFaq2.contains(kw.toLower()))
                                                still2.append(kw);
                                        if (!still2.isEmpty()) {
                                            preflightFailed2 = true;
                                            appendAplusLog(progressUi.logPtr,
                                                tr("  ⚠ Rewrite still contains %1 — targeted re-rewrite…")
                                                    .arg(still2.join(QStringLiteral(", "))));
                                            preflightPrompt2 =
                                                QStringLiteral("CRITICAL: The FAQ below STILL contains the "
                                                               "forbidden word(s): ")
                                                + still2.join(QStringLiteral(", "))
                                                + QStringLiteral(".\nSearch EVERY line for these words and any "
                                                                 "compound forms that include them as a substring. "
                                                                 "Replace EACH occurrence with a different synonym. "
                                                                 "Keep the Q:/A: format and all pairs unchanged.\n"
                                                                 "Return ONLY the corrected FAQ.\n\n")
                                                + faqText;
                                        }
                                    } // lowerFaq2, still2 destroyed
                                    if (preflightFailed2) {
                                        QPromise<CliRunResult> fixProm2;
                                        fixProm2.start();
                                        QFuture<CliRunResult> fixFut2 = fixProm2.future();
                                        {
                                            auto sp3 = QSharedPointer<QPromise<CliRunResult>>::create(
                                                std::move(fixProm2));
                                            cli->runPromptAsync(preflightPrompt2, faqWorkDir, this,
                                                [sp3](CliRunResult r3) mutable {
                                                    sp3->addResult(std::move(r3));
                                                    sp3->finish();
                                                });
                                        }
                                        const CliRunResult fixR2 = co_await qCoro(fixFut2).result();
                                        const QString fixedFaq2 = extractFaqContent(fixR2.output.trimmed());
                                        appendAplusLog(progressUi.logPtr,
                                            tr("  ℹ Targeted re-rewrite:\n%1")
                                                .arg(fixR2.output.trimmed().left(800)));
                                        if (!fixedFaq2.isEmpty()) {
                                            faqText = fixedFaq2;
                                            saveFaqRewrite(fixedFaq2);
                                        }
                                    }
                                    ++faqAttempt;
                                    break; // retry upload with regenerated FAQ
                                }
                                appendAplusLog(progressUi.logPtr,
                                    tr("  ⚠ CLI rewrite returned empty output — try again or skip."));
                                // loop back to show dialog again
                            }
                            if (userSkippedFaq) {
                                faqText.clear();
                                appendAplusLog(progressUi.logPtr,
                                    tr("  ↩ Retrying without FAQ…"));
                            }
                            continue;
                        }

                        appendAplusLog(progressUi.logPtr,
                            tr("  ⚠ Approval submission failed: %1").arg(m_aplusApi->lastError()));
                    } else {
                        appendAplusLog(progressUi.logPtr,
                            tr("  ✓ Submitted. Amazon reviews within 24–48 hours."));
                    }
                }

                appendAplusLog(progressUi.logPtr,
                    tr("  ✓ Upload complete — key: %1").arg(contentReferenceKey));
                break; // done (success or non-retryable failure)
            } // end FAQ retry loop
        }
    }

    setAplusStatus(progressUi, tr("Done!"), totalSteps);
    appendAplusLog(progressUi.logPtr, tr("✓ All uploads complete."));

    if (m_productWorkingDir.exists()) {
        QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                    QSettings::IniFormat);
        s.setValue(QStringLiteral("aplus/uploadedAt"),
                   QDateTime::currentDateTime().toString(Qt::ISODate));
    }

    // Warn if size images exist for this product but have never been uploaded.
    if (!m_groupImages.isEmpty()) {
        bool uploaded = false;
        if (m_productWorkingDir.exists()) {
            QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                        QSettings::IniFormat);
            uploaded = s.contains(QStringLiteral("sizing/sizeImageUploaded"));
        }
        if (!uploaded)
            QMessageBox::warning(this, tr("A+ Content Uploaded"),
                tr("A+ content was uploaded successfully.\n\n"
                   "Don't forget to also upload the size images using the "
                   "\"Upload\" button on the Size Image tab."));
    }
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

    QAction *genSelectedAct = m_aplusMenu->addAction(tr("Select images to regenerate…"));
    connect(genSelectedAct, &QAction::triggered, this, &PaneSizing::onAplusGenerateSelected);

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

    m_aplusMenu->addSeparator();
    QAction *excludeAct = m_aplusMenu->addAction(tr("Excluded colors…"));
    connect(excludeAct, &QAction::triggered, this, &PaneSizing::onAplusExcludedColors);
}

void PaneSizing::onAplusExcludedColors()
{
    if (m_colorVariants.isEmpty()) {
        QMessageBox::information(this, tr("Excluded Colors"),
            tr("No color variants loaded. Load a product first."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Excluded colors from A+ generation"));
    dlg.resize(380, 280);
    auto *lay = new QVBoxLayout(&dlg);

    auto *infoLabel = new QLabel(
        tr("Uncheck colors to exclude them from A+ content generation:"), &dlg);
    infoLabel->setWordWrap(true);
    lay->addWidget(infoLabel);

    auto *list = new QListWidget(&dlg);
    for (const auto &[color, urls] : std::as_const(m_colorVariants)) {
        if (color.isEmpty()) continue;
        auto *item = new QListWidgetItem(color, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_aplusExcludedColors.contains(color)
                            ? Qt::Unchecked : Qt::Checked);
    }
    lay->addWidget(list);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted) return;

    m_aplusExcludedColors.clear();
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->checkState() == Qt::Unchecked)
            m_aplusExcludedColors << list->item(i)->text();
    }

    if (m_productWorkingDir.exists()) {
        QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                    QSettings::IniFormat);
        s.setValue(QStringLiteral("aplus/excluded_colors"),
                   m_aplusExcludedColors.join(QLatin1Char(',')));
    }
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
    APlusWorkflow *wf = APlusWorkflow::findById(
        ui->comboBoxWorkflow->currentData().toString());
    if (wf) {
        const AbstractSizeCategory *cat = _currentCategory();
        wf->setCategoryKey(cat ? cat->displayName() : QString{});
    }
    return wf;
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
//
// For categories where allGroupsAlwaysVisible() is true (e.g. shoes), one target is
// generated per defined group regardless of the country list, so that missing countries
// never prevent a group's chart from being generated.
static QList<PaneSizing::SizeChartTarget> buildSizeChartTargets(
    const AbstractSizeCategory *cat, QListWidget *countriesList)
{
    QList<PaneSizing::SizeChartTarget> result;
    if (!cat) return result;
    const QList<CountryGroup> groups = cat->countryGroups();

    if (cat->allGroupsAlwaysVisible()) {
        for (int g = 0; g < groups.size(); ++g) {
            const CountryGroup &grp = groups[g];
            const QString lang = grp.isEnglish ? QStringLiteral("English")
                                               : (!grp.codes.isEmpty()
                                                  ? countryCodeToLanguage(grp.codes.first())
                                                  : QString{});
            if (lang.isEmpty()) continue;
            PaneSizing::SizeChartTarget t;
            t.groupKey   = grp.key.toLower();
            t.groupLabel = grp.label;
            t.groupRow   = g;
            t.language   = lang;
            t.isEnglish  = grp.isEnglish;
            result.append(t);
        }
        return result;
    }

    using Key = QPair<int, QString>; // (groupRow, language)
    QSet<Key> seen;

    for (int i = 0; i < countriesList->count(); ++i) {
        const QString code = countriesList->item(i)->text().trimmed().toLower();
        if (code.contains(QLatin1String("(missing)"))) continue;

        int groupRow = -1;
        for (int g = 0; g < groups.size() && groupRow < 0; ++g) {
            if (!groups[g].codes.isEmpty()) {
                if (groups[g].codes.contains(code))
                    groupRow = g;
            } else {
                const QStringList parts = groups[g].label.split(QLatin1Char('/'));
                for (const QString &part : parts)
                    if (part.compare(code, Qt::CaseInsensitive) == 0)
                        { groupRow = g; break; }
            }
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
static QString sanitizeFaqText(const QString &text)
{
    QString out = text;
    // Replace typographic dashes with plain hyphen-dash
    out.replace(QChar(0x2014), QStringLiteral(" - ")); // em-dash —
    out.replace(QChar(0x2013), QStringLiteral(" - ")); // en-dash –
    // Strip markdown bold (**text** → text) and italic (*text* → text)
    out.replace(QRegularExpression(QStringLiteral("\\*\\*(.+?)\\*\\*")), QStringLiteral("\\1"));
    out.replace(QRegularExpression(QStringLiteral("\\*([^*\n]+)\\*")), QStringLiteral("\\1"));
    // Strip markdown headers (## Heading → Heading)
    out.replace(QRegularExpression(QStringLiteral("(?m)^#{1,6}\\s*")), QString{});
    // Remove markdown horizontal-rule lines (--- / ***)
    out.replace(QRegularExpression(QStringLiteral("(?m)^[-*]{3,}\\s*$")), QString{});
    // Collapse 3+ blank lines down to one
    out.replace(QRegularExpression(QStringLiteral("\n{3,}")), QStringLiteral("\n\n"));
    return out.trimmed();
}

static QString extractFaqContent(const QString &raw)
{
    const QStringList lines = raw.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.length() >= 3 && trimmed[0] == QLatin1Char('Q')
                && (trimmed[1] == QLatin1Char(':') || trimmed[1] == QLatin1Char(' '))) {
            return sanitizeFaqText(lines.mid(i).join(QLatin1Char('\n')).trimmed());
        }
    }
    return sanitizeFaqText(raw); // no Q: pattern found — sanitize and return as-is
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
        if (!color.isEmpty() && !m_aplusExcludedColors.contains(color))
            colors << color;
    const QString focusColor = colors.isEmpty() ? QString{} : colors.first();

    // Build main image hint (used by the workflow's preamble + by FAQ prompt below).
    // Use the absolute path so the AI can find the file regardless of its workDir.
    const QString mainImageHint = m_mainImageLocalPath.isEmpty() ? QString{}
        : tr("A product photo is available at \"%1\". "
             "You may use it as reference.")
          .arg(m_mainImageLocalPath);

    const QStringList stepInstrs = _stepInstructions();

    // Build all image slot specs from the workflow, then inject per-color references.
    QList<ImageSlotSpec> slotSpecs = workflow->buildSlots(
        m_aplusContent.get(), colors, focusColor,
        description, mainImageHint, stepInstrs);
    {
        QStringList exIds;
        for (const QString &c : m_aplusExcludedColors) exIds << colorToFileSegment(c);
        injectColorImageHints(slotSpecs, m_productWorkingDir, exIds);
        injectGroupShotColorHints(slotSpecs, colors, exIds, m_productWorkingDir);
    }

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
    {
        const AbstractSizeCategory *cat = _currentCategory();
        if (cat && cat->allGroupsAlwaysVisible()) {
            faqPrompt += tr("This is a shoe product. "
                            "If the shoe width listed in the product description is narrow, "
                            "or if the bullet points suggest the fit runs small or recommend sizing up, "
                            "include as the very first Q&A: "
                            "\"Should I order a size up?\" — answer: yes, order 1–2 sizes larger than usual.\n\n");
        }
    }
    faqPrompt += tr("Generate a concise, engaging Amazon A+ Content FAQ section for "
                    "this product in English. Output as a list of question/answer pairs in plain text. "
                    "For any measurements (height, chest, weight, foot length, etc.), always include "
                    "both metric and imperial equivalents, e.g. \"165–175 cm (65–69 in)\" or \"86 cm (34 in)\". "
                    "IMPORTANT: do NOT use em-dashes (—) anywhere in the output; use a comma or rewrite the sentence instead.");

    // --- Prompt review dialog ---
    // For clothing: show one representative prompt per workflow step (desktop only).
    // For generic: show desktop + mobile of the first slot, same as before.
    QString groupShotPreview, perColorPreview, detailPreview, aspirationalPreview;
    for (const ImageSlotSpec &spec : slotSpecs) {
        if (spec.elementId == QStringLiteral("image_group") && groupShotPreview.isEmpty())
            groupShotPreview = spec.desktopPrompt;
        else if (spec.elementId.startsWith(QStringLiteral("image_color_")) && perColorPreview.isEmpty())
            perColorPreview = spec.desktopPrompt;
        else if (spec.elementId.startsWith(QStringLiteral("image_detail_")) && detailPreview.isEmpty())
            detailPreview = spec.desktopPrompt;
        else if (spec.elementId == QStringLiteral("image_aspirational") && aspirationalPreview.isEmpty())
            aspirationalPreview = spec.desktopPrompt;
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
        if (!aspirationalPreview.isEmpty()) {
            auto *ed = new QTextEdit(); ed->setPlainText(aspirationalPreview);
            tabs->addTab(ed, tr("Aspirational Scene"));
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

    // Tracks image tasks that produced no output file, for the post-run recovery dialog.
    struct FailedEntry {
        CliTask              task;       // original task — can be re-run as-is
        std::function<void()> fillBlack; // alternative: write a black PNG and push it
        QString              cliOutput;  // what the CLI printed (for diagnosis)
    };
    auto failedEntries = QSharedPointer<QList<FailedEntry>>::create();

    // One desktop + mobile task pair per workflow slot.
    // If the current version has exactly one side (the other was explicitly deleted),
    // only regenerate the missing side and inherit the surviving file into the new version.
    for (const ImageSlotSpec &spec : slotSpecs) {
        const int vCount = spec.versionCount;
        for (int v = 0; v < vCount; ++v) {
            const QString elemId = spec.elementId;
            const QString displayName = spec.displayName;
            const QDir elemDir(m_aplusContent->dir().filePath(elemId));
            elemDir.mkpath(QStringLiteral("."));
            const QString elemWorkDir = elemDir.absolutePath();

            const APlusElement *elemPtr    = m_aplusContent->findElement(elemId);
            const APlusVersion *curVer     = elemPtr ? elemPtr->current() : nullptr;
            const bool curHasDesktop = curVer && !curVer->desktopFile.isEmpty();
            const bool curHasMobile  = curVer && !curVer->mobileFile.isEmpty();
            // Only skip one side when the current version has that side but lacks the other.
            const bool doDesktop = !curHasDesktop || curHasMobile;
            const bool doMobile  = !curHasMobile  || curHasDesktop;
            // Relative paths to carry over from the surviving side (when regenerating only one).
            const QString inheritedDesktopRel = !doDesktop ? curVer->desktopFile : QString{};
            const QString inheritedMobileRel  = !doMobile  ? curVer->mobileFile  : QString{};

            auto filePair   = QSharedPointer<QPair<QString,QString>>::create();
            auto beforeSnap = QSharedPointer<QStringList>::create();

            if (doDesktop) {
                CliTask desktopTask;
                desktopTask.label   = (vCount > 1)
                    ? tr("Desktop image — %1 (v%2)").arg(displayName).arg(v + 1)
                    : tr("Desktop image — %1").arg(displayName);
                desktopTask.prompt  = spec.desktopPrompt
                    + QStringLiteral("\n\nOUTPUT PATH: Save the final image to this exact absolute path: ")
                    + elemDir.absoluteFilePath(QStringLiteral("desktop.png"));
                desktopTask.workDir = elemWorkDir;
                desktopTask.onBefore = [beforeSnap, elemDir]() {
                    *beforeSnap = elemDir.entryList({QStringLiteral("*.png"),
                        QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
                };
                desktopTask.onDone = [this, elemDir, beforeSnap, filePair, elemId, displayName,
                                       generatedImages, doMobile, inheritedMobileRel](CliRunResult r) {
                    const QString preferred = elemDir.filePath(QStringLiteral("desktop.png"));
                    if (QFileInfo::exists(preferred)) {
                        filePair->first = preferred;
                    } else {
                        const QStringList after = elemDir.entryList({QStringLiteral("*.png"),
                            QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
                        for (const QString &f : after) {
                            if (!beforeSnap->contains(f)) {
                                filePair->first = elemDir.filePath(f);
                                break;
                            }
                        }
                    }
                    if (!filePair->first.isEmpty())
                        generatedImages->append(filePair->first);

                    // If we're not doing a mobile task, push the result immediately using the inherited side.
                    if (!doMobile) {
                        const QImage img(filePair->first);
                        if (!img.isNull()) {
                            _aplusPushImage(img, elemId, displayName, APlusElementType::Image);
                            // Patch the new version to include the inherited mobile file.
                            if (APlusElement *e = m_aplusContent->findElement(elemId)) {
                                if (APlusVersion *v = e->current())
                                    v->mobileFile = inheritedMobileRel;
                            }
                        }
                    }
                };
                // Wrap to detect failure (file still empty after onDone ran).
                {
                    auto origDone     = desktopTask.onDone;
                    CliTask origTask  = desktopTask;  // snapshot with original onDone
                    QDir capturedDir  = elemDir;
                    auto fillBlackFn  = [origDone, capturedDir]() {
                        QImage img(1920, 1080, QImage::Format_RGB888);
                        img.fill(Qt::black);
                        img.save(capturedDir.filePath(QStringLiteral("desktop.png")), "PNG");
                        origDone({});
                    };
                    desktopTask.onDone = [origDone, filePair, origTask, fillBlackFn, failedEntries]
                                         (CliRunResult r) {
                        origDone(r);
                        if (filePair->first.isEmpty()) {
                            const QString out = (r.output + r.errorOutput).trimmed();
                            failedEntries->append({origTask, fillBlackFn, out});
                        }
                    };
                }
                tasks.append(desktopTask);
            }

            if (doMobile) {
                CliTask mobileTask;
                mobileTask.label   = (vCount > 1)
                    ? tr("Mobile image — %1 (v%2)").arg(displayName).arg(v + 1)
                    : tr("Mobile image — %1").arg(displayName);
                mobileTask.prompt  = spec.mobilePrompt
                    + QStringLiteral("\n\nOUTPUT PATH: Save the final image to this exact absolute path: ")
                    + elemDir.absoluteFilePath(QStringLiteral("mobile.png"));
                mobileTask.workDir = elemWorkDir;
                mobileTask.onBefore = [beforeSnap, elemDir]() {
                    *beforeSnap = elemDir.entryList({QStringLiteral("*.png"),
                        QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
                };
                mobileTask.onDone = [this, elemDir, beforeSnap, filePair, elemId, displayName,
                                      generatedImages, doDesktop, inheritedDesktopRel](CliRunResult r) {
                    const QString preferred = elemDir.filePath(QStringLiteral("mobile.png"));
                    if (QFileInfo::exists(preferred)) {
                        filePair->second = preferred;
                    } else {
                        const QStringList after = elemDir.entryList({QStringLiteral("*.png"),
                            QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
                        for (const QString &f : after) {
                            if (!beforeSnap->contains(f) && elemDir.filePath(f) != filePair->first) {
                                filePair->second = elemDir.filePath(f);
                                break;
                            }
                        }
                    }
                    if (!filePair->second.isEmpty())
                        generatedImages->append(filePair->second);

                    // Push the combined result.
                    const QImage dImg(filePair->first.isEmpty() ? elemDir.filePath(inheritedDesktopRel) : filePair->first);
                    const QImage mImg(filePair->second);
                    if (!dImg.isNull() || !mImg.isNull()) {
                        // _aplusPushImage expects a single image and scales it for both.
                        // Here we have potentially two different images.
                        // We'll use the desktop one as primary if available.
                        _aplusPushImage(dImg.isNull() ? mImg : dImg, elemId, displayName, APlusElementType::Image);
                        if (APlusElement *e = m_aplusContent->findElement(elemId)) {
                            if (APlusVersion *v = e->current()) {
                                // Re-save mobile if it was generated separately.
                                if (!mImg.isNull()) {
                                    const QString ts = _aplusTimestamp();
                                    const QString relMobile = elemId + QStringLiteral("/v_") + ts + QStringLiteral("_mobile.png");
                                    mImg.scaledToWidth(600, Qt::SmoothTransformation).save(m_aplusContent->dir().filePath(relMobile), "PNG");
                                    v->mobileFile = relMobile;
                                }
                                if (!inheritedDesktopRel.isEmpty() && dImg.isNull())
                                    v->desktopFile = inheritedDesktopRel;
                            }
                        }
                    }
                };
                // Wrap to detect failure (file still empty after onDone ran).
                {
                    auto origDone     = mobileTask.onDone;
                    CliTask origTask  = mobileTask;
                    QDir capturedDir  = elemDir;
                    auto fillBlackFn  = [origDone, capturedDir]() {
                        QImage img(600, 600, QImage::Format_RGB888);
                        img.fill(Qt::black);
                        img.save(capturedDir.filePath(QStringLiteral("mobile.png")), "PNG");
                        origDone({});
                    };
                    mobileTask.onDone = [origDone, filePair, origTask, fillBlackFn, failedEntries]
                                        (CliRunResult r) {
                        origDone(r);
                        if (filePair->second.isEmpty()) {
                            const QString out = (r.output + r.errorOutput).trimmed();
                            failedEntries->append({origTask, fillBlackFn, out});
                        }
                    };
                }
                tasks.append(mobileTask);
            }
        }
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
                                    "Use metric units only — remove any imperial equivalents "
                                    "(e.g. write \"165–175 cm\" not \"165–175 cm (65–69 in)\"). "
                                    "Do NOT use em-dashes (—) anywhere; use a comma or rewrite the sentence instead. "
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
                   generatedImages, workDir, startOverPtr, failedEntries]
                  (int step, int total, const QString &label, CliRunResult result) mutable {
        if (step == total + 1) {
            // All content tasks done.

            // --- Build the assessment runner (called after any recovery action) ---
            auto runAssessment = [selfPtr, statusLabelPtr, progressBarPtr, logEditPtr, appendLog,
                                  generatedImages, workDir, startOverPtr]() mutable {
                if (statusLabelPtr) statusLabelPtr->setText(
                    QObject::tr("Step %1 of %2: %3")
                        .arg(progressBarPtr ? progressBarPtr->maximum() : 0)
                        .arg(progressBarPtr ? progressBarPtr->maximum() : 0)
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
                    "\nFinish with a one-line verdict using exactly one of these three labels:\n"
                    "  PASS               — everything looks correct, no issues\n"
                    "  PASS_WITH_WARNINGS — minor issues only (slightly awkward phrasing, "
                                           "small stylistic imperfections) that do not affect usability\n"
                    "  FAIL               — structural problems: missing files, broken images, "
                                           "wrong language, content unrelated to the product, "
                                           "or anything that would prevent publication\n"
                    "Put the verdict label alone on its own line at the very end.");

                assessCli->runPromptAsync(p, workDir, selfPtr,
                    [statusLabelPtr, progressBarPtr, logEditPtr, appendLog, startOverPtr]
                    (CliRunResult assessResult) {
                        const QString out = assessResult.output.trimmed();
                        const QString display = out.isEmpty()
                                             ? assessResult.errorOutput.trimmed() : out;
                        if (!display.isEmpty()) {
                            const QStringList lines = display.split(QLatin1Char('\n'));
                            QString verdict;
                            for (int i = lines.size() - 1; i >= 0; --i) {
                                const QString t = lines.at(i).trimmed();
                                if (!t.isEmpty()) { verdict = t; break; }
                            }
                            QString icon;
                            if (verdict == QLatin1String("PASS"))
                                icon = QStringLiteral("✓");
                            else if (verdict == QLatin1String("PASS_WITH_WARNINGS"))
                                icon = QStringLiteral("⚠");
                            else
                                icon = QStringLiteral("✗");
                            appendLog(QStringLiteral("Assessment:\n") + display
                                      + QStringLiteral("\n") + icon + QStringLiteral(" ") + verdict);
                        } else {
                            appendLog(QObject::tr("(assessment produced no output)"));
                        }

                        if (statusLabelPtr) statusLabelPtr->setText(QObject::tr("All done!"));
                        if (progressBarPtr) progressBarPtr->setValue(progressBarPtr->maximum());
                        if (startOverPtr) startOverPtr->setEnabled(true);
                    });
            }; // end runAssessment

            // --- Recovery dialog if any image tasks produced no output ---
            if (!failedEntries->isEmpty() && selfPtr) {
                QStringList failedLabels;
                for (const auto &e : std::as_const(*failedEntries)) {
                    failedLabels << e.task.label;
                    // Log what the CLI actually said for each failed task
                    if (!e.cliOutput.isEmpty())
                        appendLog(QObject::tr("  ⚠ %1 — CLI said: %2")
                                      .arg(e.task.label, e.cliOutput.left(300)));
                    else
                        appendLog(QObject::tr("  ⚠ %1 — CLI produced no output").arg(e.task.label));
                }

                QMessageBox box(selfPtr);
                box.setWindowTitle(QObject::tr("Some images failed to generate"));
                box.setText(QObject::tr("%n image generation(s) produced no output file.", "",
                                        failedEntries->size()));
                box.setInformativeText(failedLabels.join(QStringLiteral("\n")));
                auto *retryBtn = box.addButton(QObject::tr("Run again the failed ones"),
                                               QMessageBox::AcceptRole);
                auto *blackBtn = box.addButton(QObject::tr("Fill with black images"),
                                               QMessageBox::ActionRole);
                box.addButton(QObject::tr("Leave it like this"), QMessageBox::RejectRole);
                box.exec();

                if (box.clickedButton() == retryBtn) {
                    QList<CliTask> retryTasks;
                    for (const auto &e : std::as_const(*failedEntries))
                        retryTasks.append(e.task);
                    failedEntries->clear();

                    const int n = retryTasks.size();
                    if (progressBarPtr) { progressBarPtr->setRange(0, n + 1); progressBarPtr->setValue(0); }
                    appendLog(QObject::tr("↩ Retrying %1 failed image(s)…").arg(n));

                    selfPtr->_runSequentially(std::move(retryTasks),
                        [statusLabelPtr, progressBarPtr, appendLog]
                        (int s, int tot, const QString &lbl) {
                            if (statusLabelPtr) statusLabelPtr->setText(
                                QObject::tr("Step %1 of %2: %3").arg(s).arg(tot).arg(lbl));
                            if (progressBarPtr) progressBarPtr->setValue(s - 1);
                            appendLog(QObject::tr("▶ %1").arg(lbl));
                        },
                        [runAssessment, appendLog](int s, int tot, const QString &lbl, CliRunResult r) mutable {
                            if (s == tot + 1) { runAssessment(); return; }
                            const qint64 secs = r.durationMs / 1000;
                            if (!r.processStarted)
                                appendLog(QObject::tr("  ✗ Failed to start CLI for: %1").arg(lbl));
                            else
                                appendLog(QObject::tr("  ✓ Done (%1s): %2").arg(secs).arg(lbl));
                            if (!r.output.trimmed().isEmpty())
                                appendLog(QObject::tr("    output: %1").arg(r.output.trimmed().left(300)));
                            if (!r.errorOutput.trimmed().isEmpty())
                                appendLog(QObject::tr("    stderr: %1").arg(r.errorOutput.trimmed().left(200)));
                        });
                    return;
                } else if (box.clickedButton() == blackBtn) {
                    appendLog(QObject::tr("↩ Filling %1 failed image(s) with black placeholders…")
                                  .arg(failedEntries->size()));
                    for (const auto &e : std::as_const(*failedEntries))
                        e.fillBlack();
                    failedEntries->clear();
                } else {
                    appendLog(QObject::tr("↩ Leaving %1 failed image(s) as-is.")
                                  .arg(failedEntries->size()));
                    failedEntries->clear();
                }
            }

            runAssessment();
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

void PaneSizing::_refreshSizeImageUploadStatus()
{
    bool uploaded = false;
    if (m_productWorkingDir.exists()) {
        QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                    QSettings::IniFormat);
        uploaded = s.contains(QStringLiteral("sizing/sizeImageUploaded"));
    }
    QLabel *lbl = ui->labelSizeImageUploadStatus;
    if (uploaded) {
        lbl->setText(tr("Images uploaded"));
        lbl->setStyleSheet(QStringLiteral("color: #2e7d32;")); // dark green on light
    } else {
        lbl->setText(tr("Images not uploaded"));
        lbl->setStyleSheet(QStringLiteral("color: #c62828;")); // dark red on light
    }
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

bool PaneSizing::_isNarrowOnlyShoe() const
{
    if (m_shoeWidths.isEmpty()) return false;
    for (const QString &w : m_shoeWidths) {
        const QString l = w.toLower();
        const bool narrow = (l == QLatin1String("n"))
                         || l.contains(QLatin1String("narrow"))
                         || l.contains(QLatin1String("slim"))
                         || l.contains(QStringLiteral("étroit"))
                         || l.contains(QLatin1String("schmal"));
        if (!narrow) return false; // at least one non-narrow width — no note needed
    }
    return true;
}

QImage PaneSizing::_appendNarrowSizingNote(const QImage &img) const
{
    const QString note = tr("Narrow sizing — order 1–2 sizes up for regular-width feet");
    const int stripH = 36;
    const QColor bg(QStringLiteral("#e8f6f3"));
    const QColor textColor(QStringLiteral("#2a6b64"));

    QImage out(img.width(), img.height() + stripH, QImage::Format_ARGB32);
    out.fill(bg);

    QPainter p(&out);
    p.drawImage(0, 0, img);

    QFont f(QStringLiteral("Arial"), 11, QFont::Normal, /*italic=*/true);
    p.setFont(f);
    p.setPen(textColor);
    p.drawText(QRect(0, img.height(), img.width(), stripH),
               Qt::AlignCenter, note);
    p.end();
    return out;
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

    // Temporarily remove non-target group rows (high→low to keep indices stable).
    // In letter/one_size mode the country-group rows were already removed from the model
    // by _rebuildSizeTable (only "Size" + measurement rows remain), so groupRow no longer
    // maps to real model rows — skip the filtering entirely.
    // Also skip when the category requires all groups to be visible together (e.g. shoes).
    const bool lettersMode  = ui->sizeRangeMain->mode() == QLatin1String("letters");
    const bool oneSizeMode  = ui->sizeRangeMain->mode() == QLatin1String("one_size");
    using RowData = QList<QStandardItem *>;
    QList<QPair<int, RowData>> removedGroupRows;
    if (!lettersMode && !oneSizeMode && groupRow >= 0 && groupRow < groupCount) {
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

    // For one_size mode: temporarily replace the "One size" cell with its translation.
    // "One size" is in item(0,1) — the savedLabels mechanism only covers column 0.
    // For English charts translatedLabels is empty, so the cell keeps its English text.
    static const QHash<QString, QString> kOneSizeTr = {
        {"French",   "Taille unique"}, {"German",   "Einheitsgröße"}, {"Spanish", "Talla única"},
        {"Italian",  "Taglia unica"},  {"Dutch",    "Eén maat"},      {"Japanese", "フリーサイズ"},
        {"Polish",   "Jeden rozmiar"}, {"Swedish",  "En storlek"},    {"Turkish",  "Tek beden"},
    };
    QString savedOneSizeCell;
    if (oneSizeMode && !translatedLabels.isEmpty()) {
        auto *it01 = m_sizeTableModel->item(0, 1);
        if (it01) {
            savedOneSizeCell = it01->text();
            it01->setText(kOneSizeTr.value(displayLang, QStringLiteral("One size")));
        }
    }

    const QImage img = cat->renderImage(m_sizeTableModel);

    // Restore "One size" cell
    if (!savedOneSizeCell.isEmpty()) {
        auto *it01 = m_sizeTableModel->item(0, 1);
        if (it01) it01->setText(savedOneSizeCell);
    }

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
    if (_isNarrowOnlyShoe()) {
        desktop = _appendNarrowSizingNote(desktop);
        mobile  = _appendNarrowSizingNote(mobile);
    }
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
    {
        const AbstractSizeCategory *cat = _currentCategory();
        if (cat && cat->allGroupsAlwaysVisible()) {
            prompt += tr("This is a shoe product. "
                         "If the shoe width listed in the product description is narrow, "
                         "or if the bullet points suggest the fit runs small or recommend sizing up, "
                         "include as the very first Q&A: "
                         "\"Should I order a size up?\" — answer: yes, order 1–2 sizes larger than usual.\n\n");
        }
    }
    prompt += QStringLiteral("Generate a concise, engaging Amazon A+ Content FAQ section for this product in English. "
                             "Output as a list of question/answer pairs in plain text. "
                             "For any measurements (height, chest, weight, foot length, etc.), always include "
                             "both metric and imperial equivalents, e.g. \"165-175 cm (65-69 in)\" or \"86 cm (34 in)\".");

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
    resultDlg->resize(700, 540);
    auto *resultLayout = new QVBoxLayout(resultDlg);

    auto *faqStatusLabel = new QLabel(tr("Generating FAQ with %1…").arg(cli->getName()), resultDlg);
    { QFont f = faqStatusLabel->font(); f.setBold(true); faqStatusLabel->setFont(f); }
    resultLayout->addWidget(faqStatusLabel);

    auto *faqProgressBar = new QProgressBar(resultDlg);
    faqProgressBar->setRange(0, 0);  // indeterminate while English FAQ is being generated
    resultLayout->addWidget(faqProgressBar);

    auto *output = new QTextEdit(resultDlg);
    output->setReadOnly(true);
    resultLayout->addWidget(output);

    auto *faqBtnRow = new QHBoxLayout();
    auto *faqCopyBtn = new QPushButton(tr("Copy"), resultDlg);
    faqBtnRow->addWidget(faqCopyBtn);
    faqBtnRow->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, resultDlg);
    QPushButton *faqCloseBtn = closeBtns->button(QDialogButtonBox::Close);
    if (faqCloseBtn) faqCloseBtn->setEnabled(false);
    faqBtnRow->addWidget(closeBtns);
    resultLayout->addLayout(faqBtnRow);

    connect(faqCopyBtn, &QPushButton::clicked, resultDlg, [output]() {
        QGuiApplication::clipboard()->setText(output->toPlainText());
    });
    connect(closeBtns, &QDialogButtonBox::rejected, resultDlg, &QDialog::reject);

    QPointer<QLabel>       faqStatusPtr(faqStatusLabel);
    QPointer<QProgressBar> faqBarPtr(faqProgressBar);
    QPointer<QPushButton>  faqCloseBtnPtr(faqCloseBtn);

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
        [output, saveFaqToAplus, workDir, faqDirSnap, guard,
         faqStatusPtr, faqBarPtr, faqCloseBtnPtr](CliRunResult result) {

        auto markFaqDone = [faqStatusPtr, faqBarPtr, faqCloseBtnPtr]() {
            if (faqStatusPtr) faqStatusPtr->setText(QObject::tr("Done."));
            if (faqBarPtr) { faqBarPtr->setRange(0, 1); faqBarPtr->setValue(1); }
            if (faqCloseBtnPtr) faqCloseBtnPtr->setEnabled(true);
        };

        if (!result.processStarted) {
            if (faqStatusPtr)
                faqStatusPtr->setText(QObject::tr("Failed to start CLI process."));
            markFaqDone();
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

        if (text.isEmpty() || !guard) { markFaqDone(); return; }

        // Pre-count unique non-English target languages to size the progress bar
        int uniqueLangCount = 0;
        {
            QSet<QString> seen;
            for (int i = 0; i < guard->ui->listWidgetCountries->count(); ++i) {
                const QString code = guard->ui->listWidgetCountries->item(i)->text().trimmed();
                if (code.contains(QStringLiteral("(missing)"))) continue;
                const QString lang = countryCodeToLanguage(code);
                if (lang.isEmpty() || seen.contains(lang)) continue;
                seen.insert(lang);
                ++uniqueLangCount;
            }
        }
        // format + validate for English, then translate + format + validate per language
        const int totalTasks = 2 + uniqueLangCount * 3;
        if (faqBarPtr) faqBarPtr->setRange(0, totalTasks);

        auto globalStep = QSharedPointer<int>::create(0);
        using StartFn = std::function<void(int, int, const QString &)>;
        using DoneFn  = std::function<void(int, int, const QString &, CliRunResult)>;

        StartFn onTaskStart{[faqStatusPtr, faqBarPtr, globalStep, totalTasks]
            (int, int, const QString &label) {
                ++(*globalStep);
                if (faqStatusPtr)
                    faqStatusPtr->setText(QStringLiteral("(%1/%2) %3")
                        .arg(*globalStep).arg(totalTasks).arg(label));
                if (faqBarPtr) faqBarPtr->setValue(*globalStep - 1);
            }};

        auto textHolder = QSharedPointer<QString>::create(text);

        QList<PaneSizing::CliTask> fvTasks;
        guard->_appendFaqFormatValidateTasks(fvTasks, textHolder, workDir,
            [saveFaqToAplus, guard, workDir, textHolder,
             onTaskStart, faqStatusPtr, faqBarPtr, faqCloseBtnPtr, totalTasks]
            (const QString &finalText) {

                saveFaqToAplus(finalText);
                if (finalText.isEmpty() || !guard) {
                    if (faqStatusPtr) faqStatusPtr->setText(QObject::tr("Done."));
                    if (faqBarPtr) faqBarPtr->setValue(totalTasks);
                    if (faqCloseBtnPtr) faqCloseBtnPtr->setEnabled(true);
                    return;
                }

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
                if (targetLangs.isEmpty()) {
                    if (faqStatusPtr) faqStatusPtr->setText(QObject::tr("Done."));
                    if (faqBarPtr) faqBarPtr->setValue(totalTasks);
                    if (faqCloseBtnPtr) faqCloseBtnPtr->setEnabled(true);
                    return;
                }

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
                                         "Use metric units only — remove any imperial equivalents "
                                         "(e.g. write \"165–175 cm\" not \"165–175 cm (65–69 in)\"). "
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
                DoneFn onTransDone{[faqStatusPtr, faqBarPtr, faqCloseBtnPtr, totalTasks]
                    (int step, int total, const QString &, CliRunResult) {
                        if (step == total + 1) {
                            if (faqStatusPtr) faqStatusPtr->setText(QObject::tr("Done."));
                            if (faqBarPtr) faqBarPtr->setValue(totalTasks);
                            if (faqCloseBtnPtr) faqCloseBtnPtr->setEnabled(true);
                        }
                    }};
                guard->_runSequentially(std::move(transTasks), onTaskStart, std::move(onTransDone));
            });
        guard->_runSequentially(std::move(fvTasks), std::move(onTaskStart), {});
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
    const QString mainImageHint = m_mainImageLocalPath.isEmpty() ? QString{}
        : tr("A product photo is available at \"%1\". "
             "You may use it as reference.")
          .arg(m_mainImageLocalPath);

    APlusWorkflow *workflow = _currentWorkflow();
    const QStringList stepInstrs = _stepInstructions();

    QString desktopPrompt, mobilePrompt, displayName;
    if (workflow) {
        // Collect colors from m_colorVariants (focus = first entry)
        QStringList colors;
        for (const auto &[color, urls] : std::as_const(m_colorVariants))
            if (!color.isEmpty() && !m_aplusExcludedColors.contains(color))
                colors << color;
        const QString focusColor = colors.isEmpty() ? QString{} : colors.first();

        QList<ImageSlotSpec> slotSpecs = workflow->buildSlots(
            m_aplusContent.get(), colors, focusColor,
            description, mainImageHint, stepInstrs);
        {
            QStringList exIds;
            for (const QString &c : m_aplusExcludedColors) exIds << colorToFileSegment(c);
            injectColorImageHints(slotSpecs, m_productWorkingDir, exIds);
            injectGroupShotColorHints(slotSpecs, colors, exIds, m_productWorkingDir);
        }

        for (const auto &spec : slotSpecs) {
            if (spec.elementId == elementId) {
                desktopPrompt = spec.desktopPrompt;
                mobilePrompt  = spec.mobilePrompt;
                displayName   = spec.displayName;
                break;
            }
        }
    }

    if (desktopPrompt.isEmpty()) {
        const QTextEdit *promptEditor = (ui->tabWidgetPrompt_01->currentIndex() == 0)
                                      ? ui->textEditPrompt_01
                                      : ui->textEditPrompt_02;
        const QString userPrompt = promptEditor->toPlainText().trimmed();

        if (!description.isEmpty())
            desktopPrompt += QStringLiteral("Product description:\n") + description + QStringLiteral("\n\n");
        if (!userPrompt.isEmpty())
            desktopPrompt += QStringLiteral("Instructions:\n") + userPrompt + QStringLiteral("\n\n");
        desktopPrompt += QStringLiteral(
            "Generate a professional Amazon A+ content marketing image for this product. "
            "Output as desktop.png (970x600 landscape, white background) and "
            "mobile.png (600x600 square) in the working directory.");
        mobilePrompt = desktopPrompt;
    }

    if (displayName.isEmpty()) {
        if (const APlusElement *e = m_aplusContent->findElement(elementId))
            displayName = e->displayName;
        else
            displayName = elementId;
    }

    // Snapshot existing image files so we can detect new ones after CLI runs.
    QDir aplusDir = m_aplusContent->dir();
    aplusDir.mkpath(elementId);
    const QDir elementDir(aplusDir.filePath(elementId));
    const QString workDir = elementDir.absolutePath();

    // --- Prompt review dialog ---
    QDialog reviewDlg(this);
    reviewDlg.setWindowTitle(tr("Review prompt — %1").arg(cli->getName()));
    reviewDlg.resize(700, 450);
    auto *reviewLayout = new QVBoxLayout(&reviewDlg);
    auto *tabs = new QTabWidget(&reviewDlg);
    auto *desktopEdit = new QTextEdit(&reviewDlg); desktopEdit->setPlainText(desktopPrompt);
    auto *mobileEdit = new QTextEdit(&reviewDlg); mobileEdit->setPlainText(mobilePrompt);
    tabs->addTab(desktopEdit, tr("Desktop"));
    tabs->addTab(mobileEdit, tr("Mobile"));
    reviewLayout->addWidget(tabs);

    auto *reviewBtns = new QDialogButtonBox(&reviewDlg);
    reviewBtns->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    reviewBtns->addButton(QDialogButtonBox::Cancel);
    connect(reviewBtns, &QDialogButtonBox::accepted, &reviewDlg, &QDialog::accept);
    connect(reviewBtns, &QDialogButtonBox::rejected, &reviewDlg, &QDialog::reject);
    reviewLayout->addWidget(reviewBtns);

    if (reviewDlg.exec() != QDialog::Accepted)
        return;

    const QString finalDesktop = desktopEdit->toPlainText();
    const QString finalMobile = mobileEdit->toPlainText();

    int vCount = 1;
    if (workflow) {
        // Collect colors from m_colorVariants (focus = first entry)
        QStringList colors;
        for (const auto &[color, urls] : std::as_const(m_colorVariants))
            if (!color.isEmpty() && !m_aplusExcludedColors.contains(color))
                colors << color;
        const QString focusColor = colors.isEmpty() ? QString{} : colors.first();

        const QList<ImageSlotSpec> slotSpecsLocal = workflow->buildSlots(
            m_aplusContent.get(), colors, focusColor,
            description, mainImageHint, stepInstrs);

        for (const auto &spec : slotSpecsLocal) {
            if (spec.elementId == elementId) {
                vCount = spec.versionCount;
                break;
            }
        }
    }

    QList<CliTask> tasks;
    for (int v = 0; v < vCount; ++v) {
        auto filePair = QSharedPointer<QPair<QString,QString>>::create();
        auto beforeSnap = QSharedPointer<QStringList>::create();

        CliTask desktopTask;
        desktopTask.label = (vCount > 1) ? tr("Desktop %1 (v%2)").arg(displayName).arg(v+1) : tr("Desktop %1").arg(displayName);
        desktopTask.prompt = finalDesktop
            + QStringLiteral("\n\nOUTPUT PATH: Save the final image to this exact absolute path: ")
            + elementDir.absoluteFilePath(QStringLiteral("desktop.png"));
        desktopTask.workDir = workDir;
        desktopTask.onBefore = [beforeSnap, elementDir]() {
            *beforeSnap = elementDir.entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
        };
        desktopTask.onDone = [this, elementDir, beforeSnap, filePair, elementId, displayName](CliRunResult r) {
            const QString preferred = elementDir.filePath(QStringLiteral("desktop.png"));
            if (QFileInfo::exists(preferred)) filePair->first = preferred;
            else {
                for (const QString &f : elementDir.entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files))
                    if (!beforeSnap->contains(f)) { filePair->first = elementDir.filePath(f); break; }
            }
        };
        tasks.append(desktopTask);

        CliTask mobileTask;
        mobileTask.label = (vCount > 1) ? tr("Mobile %1 (v%2)").arg(displayName).arg(v+1) : tr("Mobile %1").arg(displayName);
        mobileTask.prompt = finalMobile
            + QStringLiteral("\n\nOUTPUT PATH: Save the final image to this exact absolute path: ")
            + elementDir.absoluteFilePath(QStringLiteral("mobile.png"));
        mobileTask.workDir = workDir;
        mobileTask.onBefore = [beforeSnap, elementDir]() {
            *beforeSnap = elementDir.entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
        };
        mobileTask.onDone = [this, elementDir, beforeSnap, filePair, elementId, displayName](CliRunResult r) {
            const QString preferred = elementDir.filePath(QStringLiteral("mobile.png"));
            if (QFileInfo::exists(preferred)) filePair->second = preferred;
            else {
                for (const QString &f : elementDir.entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files))
                    if (!beforeSnap->contains(f) && elementDir.filePath(f) != filePair->first) { filePair->second = elementDir.filePath(f); break; }
            }

            const QImage dImg(filePair->first);
            const QImage mImg(filePair->second);
            if (!dImg.isNull() || !mImg.isNull()) {
                _aplusPushImage(dImg.isNull() ? mImg : dImg, elementId, displayName, APlusElementType::Image);
                if (APlusElement *e = m_aplusContent->findElement(elementId)) {
                    if (APlusVersion *vRec = e->current()) {
                        if (!mImg.isNull()) {
                            const QString ts = _aplusTimestamp();
                            const QString relMobile = elementId + QStringLiteral("/v_") + ts + QStringLiteral("_mobile.png");
                            mImg.scaledToWidth(600, Qt::SmoothTransformation).save(m_aplusContent->dir().filePath(relMobile), "PNG");
                            vRec->mobileFile = relMobile;
                        }
                    }
                }
            }
        };
        tasks.append(mobileTask);
    }

    // Reuse the progress UI logic from _runSequentially if possible, 
    // but onAplusGenerateImage usually just runs the tasks.
    _runSequentially(std::move(tasks));
}

void PaneSizing::onAplusGenerateSelected()
{
    if (!m_aplusContent) {
        QMessageBox::information(this, tr("Generate Selected"), tr("Load a product first."));
        return;
    }
    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) {
        QMessageBox::warning(this, tr("Generate Selected"), tr("No CLI tool selected."));
        return;
    }
    if (!cli->canGenImages()) {
        QMessageBox::warning(this, tr("Generate Selected"),
            tr("The selected CLI (%1) cannot generate images. "
               "Please switch to an image-capable CLI (e.g., Codex).").arg(cli->getName()));
        return;
    }
    APlusWorkflow *workflow = _currentWorkflow();
    if (!workflow) {
        QMessageBox::warning(this, tr("Generate Selected"), tr("No workflow selected."));
        return;
    }

    // --- Selection dialog (table with per-slot Desktop / Mobile checkboxes) ---
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Select images to regenerate"));
    dlg.resize(1100, 560);
    auto *lay = new QVBoxLayout(&dlg);

    // Toolbar row
    auto *toolBar = new QHBoxLayout();
    auto *selectAllBtn    = new QPushButton(tr("Select all"),        &dlg);
    auto *skipExistingBtn = new QPushButton(tr("Deselect existing"), &dlg);
    toolBar->addWidget(selectAllBtn);
    toolBar->addWidget(skipExistingBtn);
    toolBar->addStretch();
    lay->addLayout(toolBar);

    // Splitter: table left, preview right
    auto *splitter = new QSplitter(Qt::Horizontal, &dlg);
    lay->addWidget(splitter, 1);

    auto *table = new QTableWidget(splitter);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({tr("Image"), tr("Desktop"), tr("Mobile"), tr("Extra instructions"), tr("Count")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    table->setColumnWidth(0, 180);
    table->setColumnWidth(1, 80);
    table->setColumnWidth(2, 80);
    table->setColumnWidth(4, 65);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setIconSize(QSize(60, 40));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    splitter->addWidget(table);

    auto *previewLabel = new QLabel(splitter);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setText(tr("(select an image to preview)"));
    previewLabel->setMinimumWidth(300);
    splitter->addWidget(previewLabel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({480, 600});

    const QDir aplusDir = m_aplusContent->dir();
    QStringList elementIds;
    QStringList imagePaths;
    QVector<bool> hasCurrentVersion;

    for (const APlusElement &e : m_aplusContent->elements()) {
        if (e.type != APlusElementType::Image) continue;
        const APlusVersion *ver = e.current();
        const int row = table->rowCount();
        table->insertRow(row);
        table->setRowHeight(row, 50);

        auto *nameItem = new QTableWidgetItem(e.displayName);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        QString imgPath;
        if (ver) {
            imgPath = aplusDir.filePath(ver->desktopFile);
            nameItem->setToolTip(tr("Generated: %1")
                .arg(ver->generated.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
            const QImage thumb(imgPath);
            if (!thumb.isNull())
                nameItem->setIcon(QIcon(QPixmap::fromImage(
                    thumb.scaled(60, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
        }
        table->setItem(row, 0, nameItem);

        auto *dItem = new QTableWidgetItem();
        dItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        dItem->setCheckState(Qt::Checked);
        dItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, 1, dItem);

        auto *mItem = new QTableWidgetItem();
        mItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        mItem->setCheckState(Qt::Checked);
        mItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, 2, mItem);

        auto *instrEdit = new QLineEdit(table);
        instrEdit->setPlaceholderText(tr("Optional extra prompt…"));
        instrEdit->setFrame(false);
        table->setCellWidget(row, 3, instrEdit);

        auto *countSpin = new QSpinBox(table);
        countSpin->setRange(1, 10);
        countSpin->setValue(1);
        countSpin->setFrame(false);
        countSpin->setAlignment(Qt::AlignCenter);
        table->setCellWidget(row, 4, countSpin);

        elementIds << e.id;
        imagePaths << imgPath;
        hasCurrentVersion.push_back(ver != nullptr);
    }

    if (table->rowCount() == 0) {
        QMessageBox::information(this, tr("Generate Selected"),
            tr("No image slots found. Run Generate All first or add image slots."));
        return;
    }

    connect(selectAllBtn, &QPushButton::clicked, &dlg, [table, selectAllBtn]() {
        // Toggle: if everything is already checked, uncheck all; otherwise check all.
        bool anyUnchecked = false;
        for (int r = 0; r < table->rowCount(); ++r) {
            if (table->item(r, 1)->checkState() != Qt::Checked
                    || table->item(r, 2)->checkState() != Qt::Checked) {
                anyUnchecked = true;
                break;
            }
        }
        const Qt::CheckState target = anyUnchecked ? Qt::Checked : Qt::Unchecked;
        for (int r = 0; r < table->rowCount(); ++r) {
            table->item(r, 1)->setCheckState(target);
            table->item(r, 2)->setCheckState(target);
        }
        selectAllBtn->setText(target == Qt::Checked ? QObject::tr("Unselect all")
                                                    : QObject::tr("Select all"));
    });
    connect(skipExistingBtn, &QPushButton::clicked, &dlg,
        [table, hasCurrentVersion]() {
            for (int r = 0; r < table->rowCount() && r < (int)hasCurrentVersion.size(); ++r) {
                if (hasCurrentVersion[r]) {
                    table->item(r, 1)->setCheckState(Qt::Unchecked);
                    table->item(r, 2)->setCheckState(Qt::Unchecked);
                }
            }
        });

    connect(table, &QTableWidget::currentCellChanged, &dlg,
        [previewLabel, imagePaths](int row, int, int, int) {
            if (row < 0 || row >= imagePaths.size() || imagePaths.at(row).isEmpty()) {
                previewLabel->setPixmap({});
                previewLabel->setText(QObject::tr("(no image)"));
                return;
            }
            const QPixmap pm(imagePaths.at(row));
            if (pm.isNull()) {
                previewLabel->setPixmap({});
                previewLabel->setText(QObject::tr("(image not available)"));
                return;
            }
            const QSize available = previewLabel->contentsRect().size() - QSize(8, 8);
            const QSize cap = available.isEmpty() ? QSize(560, 400) : available;
            previewLabel->setPixmap(pm.scaled(cap, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        });

    if (table->rowCount() > 0)
        table->selectRow(0);

    auto *btns = new QDialogButtonBox(&dlg);
    btns->addButton(tr("Generate Selected"), QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);

    if (dlg.exec() != QDialog::Accepted)
        return;

    // Collect per-slot desktop/mobile selections
    struct SlotSel { QString elemId; bool desktop; bool mobile; QString extraInstr; int count; };
    QList<SlotSel> selections;
    for (int r = 0; r < table->rowCount(); ++r) {
        const bool d = table->item(r, 1)->checkState() == Qt::Checked;
        const bool m = table->item(r, 2)->checkState() == Qt::Checked;
        QString extra;
        if (auto *le = qobject_cast<QLineEdit *>(table->cellWidget(r, 3)))
            extra = le->text().trimmed();
        int count = 1;
        if (auto *sp = qobject_cast<QSpinBox *>(table->cellWidget(r, 4)))
            count = sp->value();
        if (d || m)
            selections.append({elementIds.at(r), d, m, extra, count});
    }

    if (selections.isEmpty()) {
        QMessageBox::information(this, tr("Generate Selected"), tr("No images selected."));
        return;
    }

    // --- Build slot specs ---
    const QString description = ui->textEditAttributes->toPlainText().trimmed();
    QStringList colors;
    for (const auto &[color, urls] : std::as_const(m_colorVariants))
        if (!color.isEmpty() && !m_aplusExcludedColors.contains(color)) colors << color;
    const QString focusColor = colors.isEmpty() ? QString{} : colors.first();
    const QString mainImageHint = m_mainImageLocalPath.isEmpty() ? QString{}
        : tr("A product photo is available at \"%1\". "
             "You may use it as reference.")
          .arg(m_mainImageLocalPath);

    QMap<QString, ImageSlotSpec> slotByElemId;
    {
        QList<ImageSlotSpec> slotList = workflow->buildSlots(
            m_aplusContent.get(), colors, focusColor,
            description, mainImageHint, _stepInstructions());
        {
            QStringList exIds;
            for (const QString &c : m_aplusExcludedColors) exIds << colorToFileSegment(c);
            injectColorImageHints(slotList, m_productWorkingDir, exIds);
            injectGroupShotColorHints(slotList, colors, exIds, m_productWorkingDir);
        }
        for (const ImageSlotSpec &spec : slotList)
            slotByElemId.insert(spec.elementId, spec);
    }

    QList<SlotSel> finalSels;
    for (const SlotSel &sel : std::as_const(selections))
        if (slotByElemId.contains(sel.elemId))
            finalSels.append(sel);

    if (finalSels.isEmpty()) {
        QStringList selectedIds, workflowIds;
        for (const SlotSel &s : selections) selectedIds << s.elemId;
        for (const QString &k : slotByElemId.keys()) workflowIds << k;
        workflowIds.sort();
        QMessageBox::information(this, tr("Generate Selected"),
            tr("The selected image slots are not produced by the current workflow.\n\n"
               "Selected: %1\n\nWorkflow produces (colors: %2): %3")
            .arg(selectedIds.join(QStringLiteral(", ")))
            .arg(colors.join(QStringLiteral(", ")))
            .arg(workflowIds.join(QStringLiteral(", "))));
        return;
    }

    for (const SlotSel &sel : std::as_const(finalSels))
        m_aplusContent->dir().mkpath(sel.elemId);
    if (m_aplusModel) { _rebuildAplusModel(); _rebuildAplusMenu(); }

    // --- Build sequential tasks (respecting per-slot desktop/mobile selection) ---
    QList<CliTask> tasks;

    for (const SlotSel &sel : std::as_const(finalSels)) {
        const ImageSlotSpec &spec = slotByElemId[sel.elemId];
        const QString elemId      = sel.elemId;
        const QString displayName = spec.displayName;
        const bool    doDesktop   = sel.desktop;
        const bool    doMobile    = sel.mobile;
        const int     genCount    = sel.count;

    for (int attempt = 0; attempt < genCount; ++attempt) {
        const QString attemptSuffix = genCount > 1
            ? tr(" (%1/%2)").arg(attempt + 1).arg(genCount) : QString{};

        const QDir elemDir(m_aplusContent->dir().filePath(elemId));
        elemDir.mkpath(QStringLiteral("."));
        const QString elemWorkDir = elemDir.absolutePath();

        // Capture existing version paths at task-build time for partial-regen reuse
        const APlusElement *elem = m_aplusContent->findElement(elemId);
        const QString existingDesktopRel = (elem && elem->current())
            ? elem->current()->desktopFile : QString{};
        const QString existingMobileRel  = (elem && elem->current())
            ? elem->current()->mobileFile  : QString{};

        auto filePair   = QSharedPointer<QPair<QString,QString>>::create();
        auto beforeSnap = QSharedPointer<QStringList>::create();
        // Shared timestamp so both tasks for the same slot produce matching versioned names
        auto tsPtr = QSharedPointer<QString>::create();

        if (doDesktop) {
            CliTask dt;
            dt.label   = tr("Desktop image — %1%2").arg(displayName, attemptSuffix);
            dt.prompt  = spec.desktopPrompt;
            if (!sel.extraInstr.isEmpty())
                dt.prompt += QStringLiteral("\n\nAdditional instruction: ") + sel.extraInstr;
            dt.prompt += QStringLiteral("\n\nOUTPUT PATH: Save the final image to this exact absolute path: ")
                + elemDir.absoluteFilePath(QStringLiteral("desktop.png"));
            dt.workDir = elemWorkDir;
            dt.onBefore = [beforeSnap, elemDir]() {
                *beforeSnap = elemDir.entryList(
                    {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")},
                    QDir::Files);
            };
            dt.onDone = [this, elemDir, beforeSnap, filePair, tsPtr,
                         elemId, displayName, doMobile, existingMobileRel](CliRunResult r) {
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
                if (tsPtr->isEmpty()) *tsPtr = _aplusTimestamp();
                {
                    const QFileInfo fi(filePair->first);
                    if (!fi.fileName().isEmpty() && fi.suffix() != QLatin1String("txt")
                            && !fi.fileName().startsWith(QStringLiteral("v_"))) {
                        const QString vp = elemDir.filePath(QStringLiteral("v_") + *tsPtr
                            + QStringLiteral("_desktop.") + fi.suffix());
                        if (QFile::rename(filePair->first, vp)) filePair->first = vp;
                    }
                }
                if (!doMobile) {
                    // Desktop only — create version now, reusing existing mobile
                    if (!m_aplusContent) return;
                    const QDir aDir = m_aplusContent->dir();
                    APlusVersion ver;
                    ver.generated   = QDateTime::currentDateTime();
                    ver.desktopFile = aDir.relativeFilePath(filePair->first);
                    ver.mobileFile  = existingMobileRel;
                    m_aplusContent->pushVersion(elemId, APlusElementType::Image, displayName, ver);
                    if (m_aplusModel) _rebuildAplusModel();
                }
            };
            tasks.append(dt);
        }

        if (doMobile) {
            CliTask mt;
            mt.label   = tr("Mobile image — %1%2").arg(displayName, attemptSuffix);
            mt.prompt  = spec.mobilePrompt;
            if (!sel.extraInstr.isEmpty())
                mt.prompt += QStringLiteral("\n\nAdditional instruction: ") + sel.extraInstr;
            mt.prompt += QStringLiteral("\n\nOUTPUT PATH: Save the final image to this exact absolute path: ")
                + elemDir.absoluteFilePath(QStringLiteral("mobile.png"));
            mt.workDir = elemWorkDir;
            mt.onBefore = [beforeSnap, elemDir]() {
                *beforeSnap = elemDir.entryList(
                    {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")},
                    QDir::Files);
            };
            mt.onDone = [this, elemDir, beforeSnap, filePair, tsPtr,
                         elemId, displayName, doDesktop, existingDesktopRel](CliRunResult r) {
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
                if (tsPtr->isEmpty()) *tsPtr = _aplusTimestamp();
                {
                    const QFileInfo fi(filePair->second);
                    if (!fi.fileName().isEmpty() && fi.suffix() != QLatin1String("txt")
                            && !fi.fileName().startsWith(QStringLiteral("v_"))) {
                        const QString vp = elemDir.filePath(QStringLiteral("v_") + *tsPtr
                            + QStringLiteral("_mobile.") + fi.suffix());
                        if (QFile::rename(filePair->second, vp)) filePair->second = vp;
                    }
                }
                if (!m_aplusContent) return;
                // Only create a version entry if at least one real image was produced.
                // Text fallback files (.txt) are skipped — they mean the CLI produced
                // prose instead of an image, which creates junk version entries.
                const bool hasDesktopImg = !filePair->first.isEmpty()
                    && QFileInfo(filePair->first).suffix().compare(
                           QLatin1String("txt"), Qt::CaseInsensitive) != 0;
                const bool hasMobileImg = !filePair->second.isEmpty()
                    && QFileInfo(filePair->second).suffix().compare(
                           QLatin1String("txt"), Qt::CaseInsensitive) != 0;
                if (!hasDesktopImg && !hasMobileImg) return;
                const QDir aDir = m_aplusContent->dir();
                APlusVersion ver;
                ver.generated   = QDateTime::currentDateTime();
                ver.desktopFile = doDesktop ? aDir.relativeFilePath(filePair->first)
                                            : existingDesktopRel;
                ver.mobileFile  = aDir.relativeFilePath(filePair->second);
                m_aplusContent->pushVersion(elemId, APlusElementType::Image, displayName, ver);
                if (m_aplusModel) _rebuildAplusModel();
            };
            tasks.append(mt);
        }
    } // end attempt loop
    } // end finalSels loop

    // --- Progress dialog ---
    auto *progressDlg = new QDialog(this);
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowTitle(
        tr("Generating selected images — %1").arg(cli->getName()));
    progressDlg->resize(560, 400);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font(); boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, tasks.size());
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
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout->addWidget(closeBtns);
    pLayout->addLayout(btnLayout);

    QPointer<QLabel>       statusPtr(statusLabel);
    QPointer<QProgressBar> barPtr(progressBar);
    QPointer<QTextEdit>    logPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);

    connect(copyBtn, &QPushButton::clicked, progressDlg,
        [logPtr]() { if (logPtr) QGuiApplication::clipboard()->setText(logPtr->toPlainText()); });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);

    progressDlg->show();

    // Hide/show this dialog when the user switches tabs.
    {
        QTabWidget *tabWgt = nullptr;
        for (QWidget *p = this; p && !tabWgt; p = p->parentWidget())
            tabWgt = qobject_cast<QTabWidget *>(p);
        if (tabWgt) {
            QPointer<QDialog>  dlgPtr(progressDlg);
            QPointer<QWidget>  selfPtr(this);
            auto *conn = new QObject(progressDlg);
            connect(tabWgt, &QTabWidget::currentChanged, conn,
                [dlgPtr, selfPtr](int) {
                    if (!dlgPtr || !selfPtr) return;
                    if (selfPtr->isVisible()) dlgPtr->show();
                    else                      dlgPtr->hide();
                });
        }
    }

    auto appendLog = [logPtr](const QString &line) {
        if (!logPtr) return;
        logPtr->append(QStringLiteral("[%1] %2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), line));
    };

    _runSequentially(
        std::move(tasks),
        [statusPtr, barPtr, appendLog](int step, int total, const QString &label) {
            if (statusPtr) statusPtr->setText(
                QObject::tr("Step %1 of %2: %3").arg(step).arg(total).arg(label));
            if (barPtr) barPtr->setValue(step - 1);
            appendLog(QObject::tr("▶ %1").arg(label));
        },
        [statusPtr, barPtr, closeBtnPtr, appendLog]
        (int step, int total, const QString &label, CliRunResult r) {
            if (step == total + 1) {
                if (statusPtr) statusPtr->setText(QObject::tr("Done."));
                if (barPtr)    barPtr->setValue(barPtr->maximum());
                if (closeBtnPtr) closeBtnPtr->setEnabled(true);
                return;
            }
            const qint64 secs = r.durationMs / 1000;
            appendLog(QObject::tr("[%1/%2] %3 — %4 (%5s)")
                .arg(step).arg(total).arg(label)
                .arg(r.processStarted ? QObject::tr("ok") : QObject::tr("failed"))
                .arg(secs));
            const QString errOut = r.errorOutput.trimmed();
            if (!errOut.isEmpty())
                appendLog(QObject::tr("  stderr: %1").arg(errOut.right(400)));
            const QString stdOut = r.output.trimmed();
            if (!stdOut.isEmpty())
                appendLog(QObject::tr("  output: %1").arg(stdOut.right(600)));
            if (barPtr) barPtr->setValue(step);
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
    const APlusElement &el = els.at(elemIdx);
    const QString id = el.id;

    // For image versions that have both sides present, offer selective deletion.
    if (el.type == APlusElementType::Image
            && loc.version >= 0 && loc.version < el.versions.size()) {
        const APlusVersion &ver = el.versions.at(loc.version);
        if (!ver.desktopFile.isEmpty() && !ver.mobileFile.isEmpty()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle(tr("Delete image"));
            msgBox.setText(tr("Which part of this version do you want to delete?"));
            QPushButton *btnDesktop = msgBox.addButton(tr("Desktop only"),        QMessageBox::DestructiveRole);
            QPushButton *btnMobile  = msgBox.addButton(tr("Mobile only"),         QMessageBox::DestructiveRole);
            QPushButton *btnBoth    = msgBox.addButton(tr("Delete entire version"), QMessageBox::DestructiveRole);
            msgBox.addButton(QMessageBox::Cancel);
            msgBox.exec();

            QAbstractButton *clicked = msgBox.clickedButton();
            if (!clicked || clicked == msgBox.button(QMessageBox::Cancel))
                return;
            if (clicked == btnDesktop || clicked == btnMobile) {
                m_aplusContent->clearVersionFile(id, loc.version, clicked == btnDesktop);
                _rebuildAplusModel();
                _refreshAplusPreview(ui->aplusTreeView->currentIndex());
                return;
            }
            Q_UNUSED(btnBoth) // falls through to full delete below
        }
    }

    m_aplusContent->deleteVersion(id, loc.version);
    _rebuildAplusModel();
    _refreshAplusPreview(ui->aplusTreeView->currentIndex());
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

    // Try API if still empty: resolve a SKU, then call fetchListingProductType.
    if (m_productType.isEmpty() && !childAsins.isEmpty()) {
        QString sku;

        // 1. Tree model (populated when loaded from xlsx with SKU column).
        if (m_treeModel) {
            for (int fi = 0; fi < m_treeModel->rowCount() && sku.isEmpty(); ++fi) {
                const QModelIndex pi = m_treeModel->index(fi, 0);
                for (int ci = 0; ci < m_treeModel->rowCount(pi) && sku.isEmpty(); ++ci)
                    sku = m_treeModel->data(
                              m_treeModel->index(ci, TreeSizingAsins::SKU, pi)).toString().trimmed();
            }
        }

        // 2. settings.ini cache from a previous upload.
        if (sku.isEmpty() && m_productWorkingDir.exists()) {
            QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                         QSettings::IniFormat);
            for (const QString &asin : childAsins) {
                sku = ps.value(QStringLiteral("sizing/skus/") + asin).toString();
                if (!sku.isEmpty()) break;
            }
        }

        // 3. Reports API — fetches all FBA+MFN listings (may take up to ~3 min).
        if (sku.isEmpty()) {
            const QString mpId = firstMarketplaceIdFromCountryList(ui->listWidgetCountries);
            if (!mpId.isEmpty()) {
                QMessageBox infoBox(QMessageBox::Information, tr("Fetching SKUs"),
                                    tr("Requesting all-listings report from Amazon to determine"
                                       " product type…\nThis can take up to 3 minutes."),
                                    QMessageBox::NoButton, this);
                infoBox.setStandardButtons(QMessageBox::NoButton);
                infoBox.show();
                QCoreApplication::processEvents();

                QHash<QString, QString> reportMap;
                co_await _fetchAllSkusCached(mpId, &reportMap);
                infoBox.hide();

                for (const QString &asin : childAsins) {
                    sku = reportMap.value(asin);
                    if (!sku.isEmpty()) break;
                }

                // Cache resolved SKUs for subsequent runs.
                if (!sku.isEmpty() && m_productWorkingDir.exists()) {
                    QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                                 QSettings::IniFormat);
                    for (auto it = reportMap.constBegin(); it != reportMap.constEnd(); ++it) {
                        if (childAsins.contains(it.key()))
                            ps.setValue(QStringLiteral("sizing/skus/") + it.key(), it.value());
                    }
                }
            }
        }

        if (!sku.isEmpty()) {
            // Try every marketplace in the country list, then CA and US as fallbacks,
            // so products that only exist in Canada (or any single NA market) are found
            // without asking the user.
            for (const QString &mpId : allMarketplaceIdsFromCountryList(ui->listWidgetCountries)) {
                co_await m_api->fetchListingProductType(mpId, sku, &m_productType);
                if (!m_productType.isEmpty()) break;
            }
            if (!m_productType.isEmpty() && m_productWorkingDir.exists()) {
                QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                             QSettings::IniFormat);
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

    // Template is set explicitly via the Pick button in the Size table page.
    const QString templatePath = m_sizeTableTemplatePath;
    if (templatePath.isEmpty() || !QFileInfo::exists(templatePath)) {
        QMessageBox::warning(this, tr("Template error"),
                             tr("No template selected. Use the Pick button to choose an xlsx template."));
        co_return;
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

            // --- Write size_table_{group}.txt alongside the xlsx ---
            {
                QString tsv;
                QTextStream ts(&tsv);

                // Header: empty corner + one column per brand size
                ts << QStringLiteral("BrandSize");
                for (const QString &bl : brandLabels)
                    ts << QLatin1Char('\t') << bl;
                ts << QLatin1Char('\n');

                // Row for this country group
                ts << modelRowLabels.value(g);
                for (int si = 0; si < nSizes; ++si) {
                    auto *it = m_sizeTableModel->item(g, si + 1);
                    ts << QLatin1Char('\t') << (it ? it->text() : QString{});
                }
                ts << QLatin1Char('\n');

                // Measurement rows (shared across all groups)
                for (int r = nGroupRows; r < m_sizeTableModel->rowCount(); ++r) {
                    ts << modelRowLabels.value(r);
                    for (int si = 0; si < nSizes; ++si) {
                        auto *it = m_sizeTableModel->item(r, si + 1);
                        ts << QLatin1Char('\t') << extractCmValue(it ? it->text() : QString{});
                    }
                    ts << QLatin1Char('\n');
                }

                QFile f(sizeTablePath + QStringLiteral("/size_table_%1.txt").arg(groupKey));
                if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                    f.write(tsv.toUtf8());
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

            // Update the tree model with the resolved SKU
            if (m_treeModel)
                m_treeModel->setSku(asin, sku);

            // Cache resolved SKU in settings.ini
            if (m_productWorkingDir.exists()) {
                QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                            QSettings::IniFormat);
                s.setValue(QStringLiteral("sizing/skus/") + asin, sku);
            }
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
    qDebug() << "PaneSizing: resolving SKUs for" << items.size() << "items in" << marketplaceId;

    // Step 1: load SKUs saved from a previous upload (settings.ini)
    if (m_productWorkingDir.exists()) {
        QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                    QSettings::IniFormat);
        for (auto &item : items) {
            if (item.sku.isEmpty()) {
                item.sku = s.value(QStringLiteral("sizing/skus/") + item.asin).toString();
                if (!item.sku.isEmpty())
                    qDebug() << "PaneSizing: ASIN" << item.asin << "resolved from local settings.ini:" << item.sku;
            }
        }
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
            co_await _fetchAllSkusCached(marketplaceId, &reportMap);
            infoBox.hide();

            if (reportMap.isEmpty() && !m_api->lastError().isEmpty()) {
                QMessageBox::warning(this, tr("Report error"),
                                     tr("Could not fetch listings report:\n%1\n\n"
                                        "Falling back to manual SKU entry.")
                                     .arg(m_api->lastError()));
            }

            for (auto &item : items) {
                if (item.sku.isEmpty()) {
                    item.sku = reportMap.value(item.asin);
                    if (!item.sku.isEmpty())
                        qDebug() << "PaneSizing: ASIN" << item.asin << "resolved from global report cache:" << item.sku;
                }
            }

            if (m_productWorkingDir.exists()) {
                QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                            QSettings::IniFormat);
                for (const auto &item : items)
                    if (!item.sku.isEmpty())
                        s.setValue(QStringLiteral("sizing/skus/") + item.asin, item.sku);
            }
        }
    }

    // Step 2b: Fallback to FBA Inventory API if still missing
    {
        bool anyMissing = false;
        for (const auto &item : items)
            if (item.sku.isEmpty()) { anyMissing = true; break; }

        if (anyMissing) {
            qDebug() << "PaneSizing: some SKUs still missing, trying FBA Inventory API fallback...";
            QHash<QString, QString> fbaMap;
            co_await m_api->fetchAllFbaSkus(marketplaceId, &fbaMap);

            for (auto &item : items) {
                if (item.sku.isEmpty()) {
                    item.sku = fbaMap.value(item.asin);
                    if (!item.sku.isEmpty())
                        qDebug() << "PaneSizing: ASIN" << item.asin << "resolved from FBA Inventory API:" << item.sku;
                }
            }

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
            qDebug() << "PaneSizing: some SKUs still missing after report. ASINs:" << [&]() {
                QStringList missing;
                for (const auto &it : items) if (it.sku.isEmpty()) missing << it.asin;
                return missing;
            }();
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
    if (m_groupImages.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"), tr("No size image available."));
        co_return;
    }
    // Always upload the default chart (index 0 — all size groups combined),
    // regardless of which per-group chart is currently previewed in the list.
    const QImage img = m_groupImages.at(0);

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

    // Progress dialog — shown throughout the entire upload flow.
    auto *prog = new QProgressDialog(tr("Resolving SKUs…"), QString{}, 0, 0, this);
    prog->setAttribute(Qt::WA_DeleteOnClose);
    prog->setWindowTitle(tr("Upload"));
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(0);
    prog->show();
    QPointer<QProgressDialog> progPtr(prog);

    bool cancelled = false;
    co_await _resolveSkus(treeItems, marketplaceIds.first(), &cancelled);
    if (cancelled) {
        if (progPtr) progPtr->close();
        co_return;
    }

    QList<AsinSku> uploadItems;
    QStringList missingSkuAsins;
    for (const auto &item : treeItems) {
        if (item.sku.isEmpty()) missingSkuAsins << item.asin;
        else                    uploadItems << item;
    }
    if (!missingSkuAsins.isEmpty())
        qWarning() << "PaneSizing: still no SKU for ASINs:" << missingSkuAsins;

    if (uploadItems.isEmpty()) {
        if (progPtr) progPtr->close();
        QMessageBox::warning(this, tr("Upload"),
            tr("No SKUs resolved. Upload cancelled."));
        co_return;
    }

    // Auto-detect product type from the first listing
    if (progPtr) progPtr->setLabelText(tr("Detecting product type…"));
    QString productType;
    co_await m_api->fetchListingProductType(
        marketplaceIds.first(), uploadItems.first().sku, &productType);

    if (productType.isEmpty()) {
        if (progPtr) progPtr->close();
        bool ok = false;
        const QString entered = QInputDialog::getText(
            this, tr("Product Type"),
            tr("Could not auto-detect the product type.\n"
               "Enter the Amazon product type (e.g. DRESS, SHIRT, SHOES):"),
            QLineEdit::Normal, {}, &ok);
        if (!ok || entered.trimmed().isEmpty())
            co_return;
        productType = entered.trimmed().toUpper();
        // Re-show progress for the upload phase.
        auto *prog2 = new QProgressDialog(QString{}, QString{}, 0, uploadItems.size(), this);
        prog2->setAttribute(Qt::WA_DeleteOnClose);
        prog2->setWindowTitle(tr("Upload"));
        prog2->setWindowModality(Qt::WindowModal);
        prog2->setMinimumDuration(0);
        prog2->show();
        progPtr = prog2;
    } else {
        if (progPtr) {
            progPtr->setRange(0, uploadItems.size());
            progPtr->setValue(0);
        }
    }

    int successCount = 0;
    const int totalAttempts = marketplaceIds.size() * uploadItems.size();
    QStringList errors;
    int step = 0;

    for (const QString &mpId : marketplaceIds) {
        for (const auto &item : uploadItems) {
            if (progPtr) {
                progPtr->setLabelText(
                    tr("Uploading listing %1 / %2…").arg(step + 1).arg(uploadItems.size()));
                progPtr->setValue(step);
            }
            bool ok = false;
            co_await m_api->patchListingImage(mpId, item.sku, productType,
                                              jpegData, imageIndex, &ok);
            if (ok)
                ++successCount;
            else
                errors << QStringLiteral("%1 / %2 (%3): %4")
                              .arg(mpId, item.sku, item.asin, m_api->lastError());
            m_api->clearLastError();
            ++step;
        }
    }

    if (progPtr) progPtr->close();

    if (errors.isEmpty()) {
        if (m_productWorkingDir.exists()) {
            QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                        QSettings::IniFormat);
            s.setValue(QStringLiteral("sizing/sizeImageUploaded"),
                       QDateTime::currentDateTime().toString(Qt::ISODate));
        }
        _refreshSizeImageUploadStatus();
        QMessageBox::information(this, tr("Upload"),
            tr("Image uploaded to %1 listing(s).").arg(successCount));
    } else {
        QMessageBox::warning(this, tr("Upload"),
            tr("Uploaded image to %1 of %2 listing(s).\n\nErrors:\n%3")
                .arg(successCount).arg(totalAttempts).arg(errors.join('\n')));
    }
    co_return;
}

QCoro::Task<void> PaneSizing::_uploadVariantImage(int imageIndex)
{
    // Determine which color is selected
    auto *selItem = ui->treeWidgetColorVariants->currentItem();
    if (!selItem) {
        QMessageBox::warning(this, tr("Upload"), tr("No color selected."));
        co_return;
    }
    auto *colorItem = selItem->parent() ? selItem->parent() : selItem;
    const QString color = colorItem->text(0);

    // Image to upload: prefer locally browsed image, otherwise selected Amazon variant
    const QString imgPath = m_variantBrowsedImagePath.isEmpty()
        ? selItem->data(0, Qt::UserRole).toString()
        : m_variantBrowsedImagePath;
    if (imgPath.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"), tr("No image selected."));
        co_return;
    }

    QImage img(imgPath);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Upload"), tr("Could not load image from:\n%1").arg(imgPath));
        co_return;
    }

    QByteArray jpegData;
    {
        QBuffer buf(&jpegData);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "JPEG", 90);
    }
    if (jpegData.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"), tr("Failed to encode image as JPEG."));
        co_return;
    }

    // Collect child ASINs belonging to this color (empty = upload to all children)
    const QStringList colorAsins = m_colorAsins.value(color.toLower());

    QList<AsinSku> treeItems;
    if (m_treeModel) {
        for (int i = 0; i < m_treeModel->rowCount(); ++i) {
            const QModelIndex pi = m_treeModel->index(i, 0);
            for (int j = 0; j < m_treeModel->rowCount(pi); ++j) {
                const QString asin = m_treeModel->data(
                    m_treeModel->index(j, TreeSizingAsins::ASIN, pi),
                    Qt::DisplayRole).toString().trimmed();
                const QString sku = m_treeModel->data(
                    m_treeModel->index(j, TreeSizingAsins::SKU, pi),
                    Qt::DisplayRole).toString().trimmed();
                if (!asin.isEmpty() && (colorAsins.isEmpty() || colorAsins.contains(asin)))
                    treeItems << AsinSku{asin, sku};
            }
        }
    }
    if (treeItems.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"),
            tr("No child ASINs found for color \"%1\".").arg(color));
        co_return;
    }

    const QStringList marketplaceIds = allMarketplaceIdsFromCountryList(ui->listWidgetCountries);

    auto *prog = new QProgressDialog(tr("Resolving SKUs…"), QString{}, 0, 0, this);
    prog->setAttribute(Qt::WA_DeleteOnClose);
    prog->setWindowTitle(tr("Upload variant image"));
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(0);
    prog->show();
    QPointer<QProgressDialog> progPtr(prog);

    // Group checked marketplaces by region so we resolve SKUs independently per region.
    // Using EU SKUs against the JP seller ID (or vice-versa) always fails with HTTP 400
    // "Invalid sellerId" — resolving per-region avoids cross-region SKU mismatches.
    QMap<QString, QStringList> regionMarketplaces;
    for (const QString &mpId : marketplaceIds)
        regionMarketplaces[AmazonCatalogApi::lwaRegionForMarketplace(mpId)] << mpId;

    const QString primaryRegion =
        AmazonCatalogApi::lwaRegionForMarketplace(marketplaceIds.first());

    QString productType;
    int successCount = 0;
    int skippedRegions = 0;
    QStringList errors;

    for (auto it = regionMarketplaces.cbegin(); it != regionMarketplaces.cend(); ++it) {
        const QString &region    = it.key();
        const QStringList &rMps  = it.value();
        const QString refMp      = rMps.first();

        // Build a fresh AsinSku list for this region (start with ASINs only, no SKUs).
        QList<AsinSku> regionItems;
        for (const AsinSku &item : treeItems)
            regionItems << AsinSku{item.asin, QString{}};

        if (region == primaryRegion) {
            // Primary region: full resolution including settings.ini + Reports API + manual dialog.
            bool cancelled = false;
            if (progPtr) progPtr->setLabelText(tr("Resolving SKUs…"));
            co_await _resolveSkus(regionItems, refMp, &cancelled);
            if (cancelled) { if (progPtr) progPtr->close(); co_return; }
        } else {
            // Secondary region: Reports API only — no settings.ini cross-contamination,
            // no manual dialog. If the product doesn't exist in this region, all SKUs
            // stay empty and we skip silently.
            if (progPtr) progPtr->setLabelText(
                tr("Checking %1 listings…").arg(region));
            QHash<QString, QString> reportMap;
            co_await _fetchAllSkusCached(refMp, &reportMap);
            for (auto &item : regionItems)
                item.sku = reportMap.value(item.asin);
        }

        QList<AsinSku> uploadItems;
        for (const auto &item : regionItems)
            if (!item.sku.isEmpty()) uploadItems << item;

        if (uploadItems.isEmpty()) {
            ++skippedRegions;
            continue;
        }

        // Detect product type once (from first region that has SKUs).
        if (productType.isEmpty()) {
            if (progPtr) progPtr->setLabelText(tr("Detecting product type…"));
            co_await m_api->fetchListingProductType(refMp, uploadItems.first().sku, &productType);
            if (productType.isEmpty()) {
                if (progPtr) progPtr->close();
                bool ok = false;
                const QString entered = QInputDialog::getText(
                    this, tr("Product Type"),
                    tr("Could not auto-detect product type.\nEnter the Amazon product type:"),
                    QLineEdit::Normal, {}, &ok);
                if (!ok || entered.trimmed().isEmpty()) co_return;
                productType = entered.trimmed().toUpper();
                auto *prog2 = new QProgressDialog(
                    QString{}, QString{}, 0, uploadItems.size(), this);
                prog2->setAttribute(Qt::WA_DeleteOnClose);
                prog2->setWindowTitle(tr("Upload variant image"));
                prog2->setWindowModality(Qt::WindowModal);
                prog2->setMinimumDuration(0);
                prog2->show();
                progPtr = prog2;
            }
        }

        if (progPtr) progPtr->setRange(0, uploadItems.size());

        int step = 0;
        for (const QString &mpId : rMps) {
            for (const auto &item : uploadItems) {
                if (progPtr) {
                    progPtr->setLabelText(
                        tr("Uploading %1 / %2 (%3)…")
                            .arg(step + 1).arg(uploadItems.size()).arg(mpId));
                    progPtr->setValue(step);
                }
                bool ok = false;
                co_await m_api->patchListingImage(
                    mpId, item.sku, productType, jpegData, imageIndex, &ok);
                if (ok) ++successCount;
                else errors << QStringLiteral("%1 / %2 (%3): %4")
                                   .arg(mpId, item.sku, item.asin, m_api->lastError());
                m_api->clearLastError();
                ++step;
            }
        }
    }

    if (progPtr) progPtr->close();

    if (errors.isEmpty()) {
        ui->labelVariantUploadStatus->setText(
            tr("✓ Uploaded to %1 listing(s)").arg(successCount));
        QMessageBox::information(this, tr("Upload"),
            tr("Image uploaded to %1 listing(s).").arg(successCount));
    } else {
        QMessageBox::warning(this, tr("Upload"),
            tr("Uploaded to %1 listing(s).\n\nErrors:\n%2")
                .arg(successCount).arg(errors.join('\n')));
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
                w.rangeSpinBox ? w.rangeSpinBox->value() : 0.0
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
            w.rangeSpinBox ? w.rangeSpinBox->value() : 0.0
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
                if (w.rangeSpinBox) w.rangeSpinBox->setValue(it.value().rangeVal);
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

// ---------------------------------------------------------------------------
// BrokenChildTable population
// ---------------------------------------------------------------------------

QCoro::Task<void> PaneSizing::_loadBrokenChildData(bool forceRefresh)
{
    if (!m_treeModel || !m_brokenChildTable) co_return;

    // Try the on-disk cache first (avoids all API calls on repeat loads).
    bool usedCache = false;
    if (!forceRefresh && m_productWorkingDir.exists())
        usedCache = m_brokenChildTable->loadFromDir(m_productWorkingDir);

    // Invalidate the cache if it has a different number of children than the
    // current tree model — e.g. the variation family gained or lost a member
    // since the last run (including when a new parent ASIN was resolved that
    // brought in extra children).
    if (usedCache) {
        int treeChildCount = 0;
        for (int fi = 0; fi < m_treeModel->rowCount(); ++fi)
            treeChildCount += m_treeModel->rowCount(m_treeModel->index(fi, 0));
        if (m_brokenChildTable->rowCount() != treeChildCount)
            usedCache = false;
    }

    if (!usedCache) {
        // Fresh load: collect child entries from the tree model.
        QList<BrokenChildTable::ChildEntry> entries;
        for (int fi = 0; fi < m_treeModel->rowCount(); ++fi) {
            const QModelIndex parentIdx = m_treeModel->index(fi, 0);
            const QString parentAsin = m_treeModel->data(
                m_treeModel->index(fi, TreeSizingAsins::ASIN)).toString();
            const QString parentSku  = m_treeModel->data(
                m_treeModel->index(fi, TreeSizingAsins::SKU)).toString();

            for (int ci = 0; ci < m_treeModel->rowCount(parentIdx); ++ci) {
                BrokenChildTable::ChildEntry e;
                e.asin       = m_treeModel->data(
                    m_treeModel->index(ci, TreeSizingAsins::ASIN,  parentIdx)).toString();
                e.sku        = m_treeModel->data(
                    m_treeModel->index(ci, TreeSizingAsins::SKU,   parentIdx)).toString();
                e.color      = m_treeModel->data(
                    m_treeModel->index(ci, TreeSizingAsins::Color, parentIdx)).toString();
                e.size       = m_treeModel->data(
                    m_treeModel->index(ci, TreeSizingAsins::Size,  parentIdx)).toString();
                e.parentAsin = parentAsin;
                e.parentSku  = parentSku;
                if (!e.asin.isEmpty())
                    entries.append(e);
            }
        }
        if (entries.isEmpty()) co_return;

        co_await m_brokenChildTable->populate(m_api.get(), std::move(entries));
    }

    // ── SKU resolution (runs regardless of cache/fresh) ────────────────────
    // The catalog API never returns seller SKUs, so SKU/parentSku are empty
    // until resolved. Strategy: settings.ini (free) → listing report (once).

    // Build current asinToSku map from what the table already knows.
    QHash<QString, QString> asinToSku;
    for (const auto &row : m_brokenChildTable->rows()) {
        if (!row.sku.isEmpty())       asinToSku.insert(row.asin,       row.sku);
        if (!row.parentSku.isEmpty()) asinToSku.insert(row.parentAsin, row.parentSku);
    }

    // 1. Check settings.ini for previously resolved SKUs.
    if (m_productWorkingDir.exists()) {
        QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
        for (const auto &row : m_brokenChildTable->rows()) {
            if (asinToSku.value(row.asin).isEmpty()) {
                const QString s = ps.value(QStringLiteral("sizing/skus/") + row.asin).toString();
                if (!s.isEmpty()) {
                    asinToSku.insert(row.asin, s);
                    qDebug() << "PaneSizing: ASIN" << row.asin << "resolved from settings.ini:" << s;
                }
            }
            if (!row.parentAsin.isEmpty() && asinToSku.value(row.parentAsin).isEmpty()) {
                const QString s = ps.value(QStringLiteral("sizing/skus/") + row.parentAsin).toString();
                if (!s.isEmpty()) {
                    asinToSku.insert(row.parentAsin, s);
                    qDebug() << "PaneSizing: parent ASIN" << row.parentAsin << "resolved from settings.ini:" << s;
                }
            }
        }
    }

    // 2. If any child SKU is still missing, run the listing reports to fill the gaps.
    // We check ALL marketplaces currently in the country list.
    {
        QStringList missingAsins;
        for (const auto &row : m_brokenChildTable->rows())
            if (asinToSku.value(row.asin).isEmpty()) missingAsins << row.asin;

        if (!missingAsins.isEmpty() && m_api) {
            // Collect all available marketplace IDs
            QStringList mpIds;
            for (int i = 0; i < ui->listWidgetCountries->count(); ++i) {
                const QString text = ui->listWidgetCountries->item(i)->text().toLower();
                if (text.contains(QStringLiteral("(missing)"))) continue;
                const QString code = text.split(QLatin1Char(' ')).first().trimmed();
                const auto *m = AmazonMarketplace::forCountryCode(code);
                if (m) mpIds << m->marketplaceId();
            }

            for (const QString &mpId : mpIds) {
                QHash<QString, QString> reportMap;
                co_await _fetchAllSkusCached(mpId, &reportMap, forceRefresh);

                bool foundSomething = false;
                for (const QString &asin : std::as_const(missingAsins)) {
                    if (asinToSku.value(asin).isEmpty()) {
                        const QString s = reportMap.value(asin);
                        if (!s.isEmpty()) {
                            asinToSku.insert(asin, s);
                            qDebug() << "PaneSizing: ASIN" << asin << "resolved from global report cache (" << mpId << "):" << s;
                            foundSomething = true;
                        }
                    }
                }

                // Update missingAsins for next marketplace loop
                if (foundSomething) {
                    missingAsins.clear();
                    for (const auto &row : m_brokenChildTable->rows())
                        if (asinToSku.value(row.asin).isEmpty()) missingAsins << row.asin;
                    if (missingAsins.isEmpty()) break;
                }
            }
        }
    }

    // 3. For parent ASINs still missing a SKU, derive it from a child listing's
    //    attributes (child_parent_sku_relationships). Virtual parents never appear
    //    in listing reports; the child always carries the parent SKU in its attributes.
    //    Try every child until one succeeds — don't give up after the first failure
    //    since a broken child may not have the attribute set.
    if (m_api) {
        QSet<QString> resolvedParents;
        for (const auto &row : m_brokenChildTable->rows()) {
            if (row.parentAsin.isEmpty()) continue;
            if (!asinToSku.value(row.parentAsin).isEmpty()) continue;
            if (resolvedParents.contains(row.parentAsin)) continue;
            const QString childSku = asinToSku.value(row.asin);
            if (childSku.isEmpty()) continue;

            // Use the seller's primary marketplace (first in country list = FR).
            // GB and other auto-mirrored marketplaces have no child_parent_sku_relationships.
            const QString mpId = firstMarketplaceIdFromCountryList(ui->listWidgetCountries);
            if (mpId.isEmpty()) continue;

            QString parentSku;
            co_await m_api->fetchParentSku(mpId, childSku, &parentSku);
            if (!parentSku.isEmpty()) {
                asinToSku.insert(row.parentAsin, parentSku);
                resolvedParents.insert(row.parentAsin); // success → don't retry
            }
            // On failure: do NOT insert into resolvedParents so the next child is tried.
        }
    }

    // 4. Apply resolved SKUs: update table, tree model, and settings.ini cache.
    if (!asinToSku.isEmpty()) {
        m_brokenChildTable->updateSkus(asinToSku);
        if (m_treeModel) {
            for (auto it = asinToSku.constBegin(); it != asinToSku.constEnd(); ++it)
                if (!it.value().isEmpty()) m_treeModel->setSku(it.key(), it.value());
        }
        if (m_productWorkingDir.exists()) {
            QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                         QSettings::IniFormat);
            for (auto it = asinToSku.constBegin(); it != asinToSku.constEnd(); ++it)
                if (!it.value().isEmpty())
                    ps.setValue(QStringLiteral("sizing/skus/") + it.key(), it.value());
        }
    }

    // Persist health + SKUs so the next non-forced load uses the cache.
    if (m_productWorkingDir.exists())
        m_brokenChildTable->saveToDir(m_productWorkingDir);

    co_return;
}

// ---------------------------------------------------------------------------
// Broken-child fix workflow
// ---------------------------------------------------------------------------

void PaneSizing::_refreshBrokenAttrCombo()
{
    if (!m_brokenChildTable) return;
    const QString currentMpId = _brokenAttrMarketplaceId();

    QSignalBlocker blocker(ui->comboBoxBrokenAttrMarket);
    ui->comboBoxBrokenAttrMarket->clear();
    for (int i = 0; i < m_brokenChildTable->marketplaceCount(); ++i) {
        const auto &spec = m_brokenChildTable->marketplaceAt(i);
        ui->comboBoxBrokenAttrMarket->addItem(spec.code.toUpper(), spec.id);
    }

    // Restore previously selected marketplace if still in list.
    if (!currentMpId.isEmpty()) {
        for (int i = 0; i < ui->comboBoxBrokenAttrMarket->count(); ++i) {
            if (ui->comboBoxBrokenAttrMarket->itemData(i).toString() == currentMpId) {
                ui->comboBoxBrokenAttrMarket->setCurrentIndex(i);
                return;
            }
        }
    }
    // Fallback: pick global then product setting.
    const QString saved = m_productWorkingDir.exists()
        ? QSettings(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                    QSettings::IniFormat)
              .value(QStringLiteral("brokenChild/attrMarketplace")).toString()
        : QSettings().value(QStringLiteral("brokenChild/attrMarketplace")).toString();
    if (!saved.isEmpty()) {
        for (int i = 0; i < ui->comboBoxBrokenAttrMarket->count(); ++i) {
            if (ui->comboBoxBrokenAttrMarket->itemData(i).toString() == saved) {
                ui->comboBoxBrokenAttrMarket->setCurrentIndex(i);
                return;
            }
        }
    }
}

QString PaneSizing::_brokenAttrMarketplaceId() const
{
    if (ui->comboBoxBrokenAttrMarket->count() == 0) return {};
    return ui->comboBoxBrokenAttrMarket->currentData().toString();
}

void PaneSizing::onBrokenAttrMarketChanged(int /*index*/)
{
    const QString mpId = _brokenAttrMarketplaceId();
    if (mpId.isEmpty()) return;
    // Save to both global QSettings and product settings.ini.
    QSettings().setValue(QStringLiteral("brokenChild/attrMarketplace"), mpId);
    if (m_productWorkingDir.exists()) {
        QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                    QSettings::IniFormat);
        s.setValue(QStringLiteral("brokenChild/attrMarketplace"), mpId);
    }
}

void PaneSizing::onBrowseBrokenTemplateClicked()
{
    const QString cur = ui->lineEditBrokenTemplate->text().trimmed();
    const QString startDir = cur.isEmpty()
        ? m_productWorkingDir.absolutePath()
        : QFileInfo(cur).dir().absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select variation template"), startDir,
        tr("Excel templates (*.xlsm *.xlsx);;All files (*)"));
    if (path.isEmpty()) return;
    ui->lineEditBrokenTemplate->setText(path);
    if (m_productWorkingDir.exists()) {
        QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                    QSettings::IniFormat);
        s.setValue(QStringLiteral("brokenChild/templatePath"), path);
    }
}

bool PaneSizing::_confirmFixSettings(bool fixParents)
{
    auto *dlg    = new QDialog(this);
    auto *layout = new QVBoxLayout(dlg);
    dlg->setWindowTitle(tr("Fix settings"));

    if (fixParents) {
        // --- Attribute source country ---
        auto *grpAttr = new QGroupBox(tr("Attribute source country"), dlg);
        auto *grpLay  = new QHBoxLayout(grpAttr);
        auto *combo   = new QComboBox(dlg);
        for (int i = 0; i < ui->comboBoxBrokenAttrMarket->count(); ++i) {
            combo->addItem(ui->comboBoxBrokenAttrMarket->itemText(i),
                           ui->comboBoxBrokenAttrMarket->itemData(i));
        }
        combo->setCurrentIndex(ui->comboBoxBrokenAttrMarket->currentIndex());
        grpLay->addWidget(combo);
        layout->addWidget(grpAttr);

        // --- Template file ---
        auto *grpTpl  = new QGroupBox(tr("Variation template (xlsm/xlsx)"), dlg);
        auto *tplLay  = new QHBoxLayout(grpTpl);
        auto *tplEdit = new QLineEdit(ui->lineEditBrokenTemplate->text().trimmed(), dlg);
        tplEdit->setMinimumWidth(320);
        auto *browseBtn = new QPushButton(tr("Browse…"), dlg);
        tplLay->addWidget(tplEdit);
        tplLay->addWidget(browseBtn);
        layout->addWidget(grpTpl);

        connect(browseBtn, &QPushButton::clicked, dlg, [this, tplEdit]() {
            const QString cur = tplEdit->text().trimmed();
            const QString startDir = cur.isEmpty()
                ? m_productWorkingDir.absolutePath()
                : QFileInfo(cur).dir().absolutePath();
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Select variation template"), startDir,
                tr("Excel templates (*.xlsm *.xlsx);;All files (*)"));
            if (!path.isEmpty()) tplEdit->setText(path);
        });

        auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
        layout->addWidget(btns);
        connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

        if (dlg->exec() != QDialog::Accepted) { dlg->deleteLater(); return false; }

        // Apply selections back to the main UI.
        const int sel = combo->currentIndex();
        if (sel >= 0) ui->comboBoxBrokenAttrMarket->setCurrentIndex(sel);
        const QString tplPath = tplEdit->text().trimmed();
        if (!tplPath.isEmpty()) {
            ui->lineEditBrokenTemplate->setText(tplPath);
            if (m_productWorkingDir.exists()) {
                QSettings s(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                            QSettings::IniFormat);
                s.setValue(QStringLiteral("brokenChild/templatePath"), tplPath);
            }
        }
    } else {
        // Images-only fix: just confirm, no template needed.
        auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
        layout->addWidget(new QLabel(tr("Fix images only — no template required."), dlg));
        layout->addWidget(btns);
        connect(btns, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
        if (dlg->exec() != QDialog::Accepted) { dlg->deleteLater(); return false; }
    }

    dlg->deleteLater();
    return true;
}

// Pre-flight confirmation dialog temporarily disabled to speed up fix iterations.
void PaneSizing::onFixAllClicked()
{
    _runBrokenChildFix(true, true);
}

void PaneSizing::onFixParentsClicked()
{
    _runBrokenChildFix(true, false);
}

void PaneSizing::onFixImagesClicked()
{
    _runBrokenChildFix(false, true);
}

void PaneSizing::onCheckStatusClicked()
{
    _runBrokenChildFix(true, false, /*checkOnly*/ true);
}

void PaneSizing::_appendFixLog(const QString &asin, const QString &marketplace,
                               const QString &details)
{
    const QString logPath = m_workingDir.filePath(QStringLiteral("sizing/fix_log.txt"));
    QFile f(logPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream s(&f);
    s << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
      << "  " << asin << "  " << marketplace << "  " << details << '\n';
}

void PaneSizing::onFixLogClicked()
{
    const QString logPath = m_workingDir.filePath(QStringLiteral("sizing/fix_log.txt"));
    QFile f(logPath);

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Fix log — sizing/fix_log.txt"));
    dlg->resize(900, 500);
    auto *layout = new QVBoxLayout(dlg);

    auto *edit = new QTextEdit(dlg);
    edit->setReadOnly(true);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        edit->setPlainText(QString::fromUtf8(f.readAll()));
        // Scroll to bottom (most recent entries)
        auto *bar = edit->verticalScrollBar();
        if (bar) bar->setValue(bar->maximum());
    } else {
        edit->setPlainText(tr("No fix log found at:\n%1\n\n"
                              "Run \"Fix parents\" or \"Fix images\" first.").arg(logPath));
    }

    layout->addWidget(edit);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    auto *copyBtn = new QPushButton(tr("Copy"), dlg);
    btnBox->addButton(copyBtn, QDialogButtonBox::ActionRole);
    connect(copyBtn, &QPushButton::clicked, dlg, [edit]() {
        QGuiApplication::clipboard()->setText(edit->toPlainText());
    });
    connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    layout->addWidget(btnBox);

    dlg->show();
}

// Default listing language tag per marketplace — required by simple localized
// attributes such as APPAREL's `size` ({value, language_tag, marketplace_id}).
static QString langTagForMarketplace(const QString &mpId)
{
    static const QHash<QString, QString> kTags{
        {QStringLiteral("A1PA6795UKMFR9"), QStringLiteral("de_DE")},
        {QStringLiteral("A13V1IB3VIYZZH"), QStringLiteral("fr_FR")},
        {QStringLiteral("A1RKKUPIHCS9HS"), QStringLiteral("es_ES")},
        {QStringLiteral("APJ6JRA9NG5V4"),  QStringLiteral("it_IT")},
        {QStringLiteral("AMEN7PMS3EDWL"),  QStringLiteral("nl_BE")},
        {QStringLiteral("A1805IZSGTT6HS"), QStringLiteral("nl_NL")},
        {QStringLiteral("A2NODRKZP88ZB9"), QStringLiteral("sv_SE")},
        {QStringLiteral("A1C3SOZRARQ6R3"), QStringLiteral("pl_PL")},
        {QStringLiteral("A28R8C7NBKEWEA"), QStringLiteral("en_IE")},
        {QStringLiteral("A1F83G8C2ARO7P"), QStringLiteral("en_GB")},
        {QStringLiteral("ATVPDKIKX0DER"),  QStringLiteral("en_US")},
        {QStringLiteral("A2EUQ1WTGCTBG2"), QStringLiteral("en_CA")},
        {QStringLiteral("A1AM78C64UM0Y8"), QStringLiteral("es_MX")},
        {QStringLiteral("A1VC38T7YXB528"), QStringLiteral("ja_JP")},
    };
    return kTags.value(mpId, QStringLiteral("en_US"));
}

// Recursively replaces every "marketplace_id" field in a copied attribute value
// so that raw attribute structures fetched from one marketplace can be
// submitted to another.
static QJsonValue swapMarketplaceIdRec(const QJsonValue &v, const QString &mpId)
{
    if (v.isObject()) {
        QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key() == QLatin1String("marketplace_id"))
                it.value() = mpId;
            else
                it.value() = swapMarketplaceIdRec(it.value(), mpId);
        }
        return o;
    }
    if (v.isArray()) {
        QJsonArray a;
        for (const QJsonValue &e : v.toArray())
            a.append(swapMarketplaceIdRec(e, mpId));
        return a;
    }
    return v;
}

QCoro::Task<void> PaneSizing::_buildFullVariationMessages(
    QString mpId, QString mpCode,
    QString parentSku, QString productType, QString variationTheme,
    QJsonObject parentAttrsFallback,
    QList<VariationTemplateEntry> tplEntries,
    QHash<QString,QString> familyAttrFallback,
    QJsonArray* messagesOut, QStringList* logOut)
{
    *messagesOut = QJsonArray{};

    // ── Pre-scan THIS marketplace's own listings ────────────────────────────
    // Size systems are REGIONAL (IT ≠ FR/ES ≠ DE/NL/SE/PL ≠ UK ≠ US/CA), so
    // size_system / size_class must come from the TARGET marketplace itself —
    // never borrowed from UK or another region's parent. We derive a reference
    // from any CHILD that already has a composite on this marketplace, and cache
    // each SKU's local attributes so the build loop below doesn't re-fetch.
    QHash<QString, QJsonObject> localBySku;
    QString refSizeSystem, refSizeClass;
    QJsonObject parentLocalSizeObj; // parent's composite on THIS marketplace
    for (const VariationTemplateEntry &e : tplEntries) {
        if (e.sku.isEmpty()) continue;
        QJsonObject la;
        co_await m_api->fetchListingAttributes(mpId, e.sku, &la);
        localBySku.insert(e.sku, la);
        const QJsonArray sizeArr = la.value(QStringLiteral("apparel_size")).toArray();
        if (sizeArr.isEmpty()) continue;
        const QJsonObject o = sizeArr.first().toObject();
        if (e.isParent) { parentLocalSizeObj = o; continue; } // children preferred
        if (refSizeSystem.isEmpty() && o.contains(QStringLiteral("size_system")))
            refSizeSystem = o.value(QStringLiteral("size_system")).toString();
        if (refSizeClass.isEmpty() && o.contains(QStringLiteral("size_class")))
            refSizeClass = o.value(QStringLiteral("size_class")).toString();
    }
    // Fallback: the parent's composite on this SAME marketplace. Even when it is
    // partially corrupted (missing size/size_class), its size_system sub-field is
    // still regional data stored on this marketplace — not borrowed from elsewhere.
    if (refSizeSystem.isEmpty() && parentLocalSizeObj.contains(QStringLiteral("size_system")))
        refSizeSystem = parentLocalSizeObj.value(QStringLiteral("size_system")).toString();
    if (refSizeClass.isEmpty() && parentLocalSizeObj.contains(QStringLiteral("size_class")))
        refSizeClass = parentLocalSizeObj.value(QStringLiteral("size_class")).toString();
    logOut->append(tr("%1 region reference: size_system=%2 size_class=%3").arg(
        mpCode,
        refSizeSystem.isEmpty() ? QStringLiteral("(none)") : refSizeSystem,
        refSizeClass.isEmpty()  ? QStringLiteral("(none)") : refSizeClass));

    // ── Schema-driven attribute selection ───────────────────────────────────
    // Attribute names DIFFER by product type: APPAREL uses `size` (simple
    // {value, language_tag}) + `color`, while DRESS-like types use the
    // `apparel_size` composite + `color_name`/`color_map`. Sending the wrong
    // set is silently IGNORED by Amazon (feed warning 90000900) — the root
    // cause of "accepted but nothing stored". Use the listings' ACTUAL product
    // type on THIS marketplace, and let its schema pick the attribute names.
    QString actualPt;
    for (const VariationTemplateEntry &e : tplEntries) {
        if (e.isParent || e.sku.isEmpty()) continue;
        AmazonCatalogApi::ListingCheck cc;
        co_await m_api->checkListing(mpId, e.sku, &cc);
        if (!cc.productType.isEmpty()) { actualPt = cc.productType; break; }
    }
    if (actualPt.isEmpty()) actualPt = productType;
    QSet<QString> ptProps;
    co_await m_api->fetchProductTypeSchemaProps(mpId, actualPt, &ptProps);
    const bool hasApparelSize = ptProps.contains(QStringLiteral("apparel_size"));
    const bool hasSimpleSize  = ptProps.contains(QStringLiteral("size"));
    const bool hasColorName   = ptProps.contains(QStringLiteral("color_name"));
    const bool hasColor       = ptProps.contains(QStringLiteral("color"));
    const QString langTag     = langTagForMarketplace(mpId);
    logOut->append(tr("%1 productType=%2 → size attr: %3, color attr: %4").arg(
        mpCode, actualPt,
        hasApparelSize ? QStringLiteral("apparel_size") :
        hasSimpleSize  ? QStringLiteral("size") : QStringLiteral("(NONE IN SCHEMA!)"),
        hasColorName   ? QStringLiteral("color_name") :
        hasColor       ? QStringLiteral("color") : QStringLiteral("(NONE IN SCHEMA!)")));

    int messageId = 1;
    for (const VariationTemplateEntry &e : tplEntries) {
        if (e.sku.isEmpty()) continue;

        // The SKU's own listing on the TARGET marketplace is the best source:
        // values are already localized and schema-shaped for this marketplace.
        const QJsonObject localAttrs = localBySku.value(e.sku);

        QJsonArray patches;
        auto addPatch = [&](const QString &key, const QJsonValue &val) {
            patches.append(QJsonObject{
                {QStringLiteral("op"),    QStringLiteral("replace")},
                {QStringLiteral("path"),  QStringLiteral("/attributes/") + key},
                {QStringLiteral("value"), val},
            });
        };
        auto simpleVal = [&](const QString &v) {
            return QJsonArray{QJsonObject{{QStringLiteral("value"), v},
                                          {QStringLiteral("marketplace_id"), mpId}}};
        };
        // Raw nested copy: local listing first (already this marketplace), then
        // the fallback attributes object (marketplace_id swapped recursively).
        auto rawOr = [&](const QString &key,
                         const QJsonObject &fbAttrs) -> QJsonValue {
            if (localAttrs.contains(key))
                return localAttrs.value(key);
            if (fbAttrs.contains(key))
                return swapMarketplaceIdRec(fbAttrs.value(key), mpId);
            return QJsonValue();  // null → "not available"
        };
        auto usable = [](const QJsonValue &v) {
            return !v.isUndefined() && !v.isNull();
        };
        // Scalar value of an attribute, preferring an English language_tag entry —
        // avoids pushing localized values (e.g. "Erwachsener") to other marketplaces,
        // which Amazon rejects as "invalid language data" (error 100720).
        auto englishScalar = [](const QJsonObject &attrs, const QString &key) -> QString {
            const QJsonArray arr = attrs.value(key).toArray();
            QString noLang, any;
            for (const QJsonValue &jv : arr) {
                const QJsonObject o = jv.toObject();
                const QString val = o.value(QStringLiteral("value")).toString();
                if (val.isEmpty()) continue;
                const QString lang = o.value(QStringLiteral("language_tag")).toString();
                if (lang.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)) return val;
                if (lang.isEmpty() && noLang.isEmpty()) noLang = val;
                if (any.isEmpty()) any = val;
            }
            return noLang.isEmpty() ? any : noLang;
        };
        // Canonicalize age_range_description to Amazon's English enum value.
        // Listings often store only the localized display name ("Erwachsener",
        // "Adulte"…) — pushing that back verbatim causes 100720 "invalid language"
        // and can flip apparel_size conditional validation (90248 keys on
        // age_range_description.value).
        auto canonicalAge = [](const QString &v) -> QString {
            static const QHash<QString, QString> kMap{
                {QStringLiteral("erwachsener"), QStringLiteral("Adult")},
                {QStringLiteral("erwachsene"),  QStringLiteral("Adult")},
                {QStringLiteral("adulte"),      QStringLiteral("Adult")},
                {QStringLiteral("adulto"),      QStringLiteral("Adult")},
                {QStringLiteral("adulta"),      QStringLiteral("Adult")},
                {QStringLiteral("volwassene"),  QStringLiteral("Adult")},
                {QStringLiteral("volwassenen"), QStringLiteral("Adult")},
                {QStringLiteral("adult"),       QStringLiteral("Adult")},
            };
            return kMap.value(v.trimmed().toLower(), v);
        };

        // Product identifier — mirrors external_product_id(+type) flat file columns.
        if (!e.gtin.isEmpty() && !e.gtinType.isEmpty()) {
            addPatch(QStringLiteral("externally_assigned_product_identifier"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("type_of_product_id"), e.gtinType},
                         {QStringLiteral("product_id"),         e.gtin},
                     }});
        } else if (!e.asin.isEmpty()) {
            addPatch(QStringLiteral("merchant_suggested_asin"), simpleVal(e.asin));
        }

        addPatch(QStringLiteral("parentage_level"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("value"), e.isParent ? QStringLiteral("parent")
                                                          : QStringLiteral("child")},
                     {QStringLiteral("marketplace_id"), mpId}}});
        if (!variationTheme.isEmpty())
            addPatch(QStringLiteral("variation_theme"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("name"),           variationTheme},
                         {QStringLiteral("marketplace_id"), mpId}}});

        if (e.isParent) {
            // Descriptive attributes so a missing/invalid parent listing can be
            // (re)created — this is what the flat file's parent row provides and
            // what the previous skinny feed lacked.
            static const QStringList kParentCopyKeys{
                QStringLiteral("brand"),
                QStringLiteral("item_name"),
                QStringLiteral("product_description"),
                QStringLiteral("bullet_point"),
                QStringLiteral("recommended_browse_nodes"),
                QStringLiteral("department"),
                QStringLiteral("country_of_origin"),
                QStringLiteral("condition_type"),
            };
            for (const QString &k : kParentCopyKeys) {
                if (!ptProps.contains(k)) continue; // not defined by this schema
                const QJsonValue v = rawOr(k, parentAttrsFallback);
                if (usable(v)) addPatch(k, v);
            }
            // NOTE: a delete op ({"op":"delete"}) is NOT supported by JSON_LISTINGS_FEED
            // (Amazon returns "Invalid empty value provided in patch"), so a corrupted
            // apparel_size already stored on the parent (e.g. DE) cannot be removed via
            // feed. The only way to overwrite it is a `replace` with a COMPLETE valid
            // composite — done below when the parent already carries an apparel_size.
            if (hasApparelSize && localAttrs.contains(QStringLiteral("apparel_size"))) {
                const QJsonArray pSize = localAttrs.value(QStringLiteral("apparel_size")).toArray();
                QJsonObject pObj = pSize.isEmpty() ? QJsonObject{} : pSize.first().toObject();
                // Ensure the composite is complete & valid so it stops failing
                // validation: use THIS marketplace's regional size_system/size_class
                // (from the children's reference), force body_type/height_type =
                // regular, and supply a size (Amazon demands ≥1).
                const QString sysFb = refSizeSystem.isEmpty() ? QStringLiteral("as8") : refSizeSystem;
                const QString clsFb = refSizeClass.isEmpty()  ? QStringLiteral("numeric") : refSizeClass;
                if (!pObj.contains(QStringLiteral("size_system")))
                    pObj.insert(QStringLiteral("size_system"), sysFb);
                if (!pObj.contains(QStringLiteral("size_class")))
                    pObj.insert(QStringLiteral("size_class"), clsFb);
                pObj.insert(QStringLiteral("body_type"),   QStringLiteral("regular"));
                pObj.insert(QStringLiteral("height_type"), QStringLiteral("regular"));
                QString pSizeVal = pObj.value(QStringLiteral("size")).toString();
                if (pSizeVal.isEmpty() && !tplEntries.isEmpty()) {
                    // Borrow the first child's size so the composite is non-empty.
                    for (const auto &ce : tplEntries) {
                        if (ce.isParent || ce.size.isEmpty()) continue;
                        QString s = ce.size;
                        if (!s.startsWith(QStringLiteral("numeric_"))) {
                            bool num=false; s.toDouble(&num);
                            if (num) s = QStringLiteral("numeric_") + s;
                        }
                        pSizeVal = s; break;
                    }
                }
                if (!pSizeVal.isEmpty())
                    pObj.insert(QStringLiteral("size"), pSizeVal);
                pObj.insert(QStringLiteral("marketplace_id"), mpId);
                addPatch(QStringLiteral("apparel_size"), QJsonArray{pObj});
                logOut->append(tr("parent %1: repaired apparel_size=%2").arg(
                    e.sku, QString::fromUtf8(QJsonDocument(pObj).toJson(QJsonDocument::Compact))));
            }

            // Only gender / age_range are safe top-level descriptors on the parent
            // (when the schema defines them), and they must be English canonical
            // values (invalid-language otherwise).
            for (const QString &k : {QStringLiteral("target_gender"),
                                     QStringLiteral("age_range_description")}) {
                if (!ptProps.contains(k)) continue;
                QString v = englishScalar(localAttrs, k);
                if (v.isEmpty()) v = englishScalar(parentAttrsFallback, k);
                if (v.isEmpty()) v = familyAttrFallback.value(k);
                if (k == QLatin1String("age_range_description"))
                    v = canonicalAge(v);
                if (!v.isEmpty()) addPatch(k, simpleVal(v));
            }
        } else {
            addPatch(QStringLiteral("child_parent_sku_relationship"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("child_relationship_type"), QStringLiteral("variation")},
                         {QStringLiteral("parent_sku"),              parentSku},
                         {QStringLiteral("marketplace_id"),          mpId}}});

            // department — only when the schema defines it (APPAREL does not).
            if (ptProps.contains(QStringLiteral("department"))) {
                const QJsonValue dept = rawOr(QStringLiteral("department"), parentAttrsFallback);
                if (usable(dept)) addPatch(QStringLiteral("department"), dept);
            }

            // Color — theme attribute; the schema decides the attribute name.
            if (hasColorName) {
                QJsonValue colorV = rawOr(QStringLiteral("color_name"), {});
                if (!usable(colorV) && !e.color.isEmpty())
                    colorV = simpleVal(e.color);
                if (usable(colorV)) {
                    addPatch(QStringLiteral("color_name"), colorV);
                    const QJsonValue mapV = rawOr(QStringLiteral("color_map"), {});
                    addPatch(QStringLiteral("color_map"), usable(mapV) ? mapV : colorV);
                } else {
                    logOut->append(tr("⚠ %1: no color available — theme attribute missing!").arg(e.sku));
                }
            } else if (hasColor) {
                // Simple `color` ({value, language_tag}). Prefer the child's own
                // stored value on this marketplace (correct language); only fall
                // back to the collected color when nothing is stored locally.
                QJsonValue colorV = rawOr(QStringLiteral("color"), {});
                if (!usable(colorV) && !e.color.isEmpty())
                    colorV = QJsonArray{QJsonObject{
                        {QStringLiteral("value"),          e.color},
                        {QStringLiteral("language_tag"),   langTag},
                        {QStringLiteral("marketplace_id"), mpId}}};
                if (usable(colorV))
                    addPatch(QStringLiteral("color"), colorV);
                else
                    logOut->append(tr("⚠ %1: no color available — theme attribute missing!").arg(e.sku));
            }

            // ukAttrs: English age/gender source (not used for sizing values).
            static const QString kUkMpId = QStringLiteral("A1F83G8C2ARO7P");
            QJsonObject ukAttrs;
            if (mpId != kUkMpId)
                co_await m_api->fetchListingAttributes(kUkMpId, e.sku, &ukAttrs);

            // The child's size in THIS country's numbering (bare, e.g. "38").
            QString bareSize = e.size;
            if (!bareSize.isEmpty() && !e.sizeSource.isEmpty() && e.sizeSource != mpCode)
                bareSize = FillerSize::convertSize(bareSize, e.sizeSource, mpCode);
            if (bareSize.startsWith(QStringLiteral("numeric_")))
                bareSize = bareSize.mid(8);

            if (hasApparelSize) {
                // apparel_size is a COMPOSITE: [{size, size_system, size_class, …}].
                // size_system / size_class are REGIONAL — child's own local composite
                // first, then the region reference from sibling children (pre-scan).
                // Canonical numeric size value is "numeric_<n>" (proven on IT).
                // body_type/height_type = "regular" for women's clothing.
                const QJsonArray localSizeArr = localAttrs.value(QStringLiteral("apparel_size")).toArray();

                QJsonObject sizeObj;
                if (!localSizeArr.isEmpty()) {
                    const QJsonObject o = localSizeArr.first().toObject();
                    for (const QString &k : {QStringLiteral("size_system"),
                                             QStringLiteral("size_class")}) {
                        if (o.contains(k)) sizeObj.insert(k, o.value(k));
                    }
                }
                if (!sizeObj.contains(QStringLiteral("size_system")) && !refSizeSystem.isEmpty())
                    sizeObj.insert(QStringLiteral("size_system"), refSizeSystem);
                if (!sizeObj.contains(QStringLiteral("size_class")) && !refSizeClass.isEmpty())
                    sizeObj.insert(QStringLiteral("size_class"), refSizeClass);
                sizeObj.insert(QStringLiteral("body_type"),   QStringLiteral("regular"));
                sizeObj.insert(QStringLiteral("height_type"), QStringLiteral("regular"));

                QString sizeVal;
                if (!localSizeArr.isEmpty())
                    sizeVal = localSizeArr.first().toObject().value(QStringLiteral("size")).toString();
                if (sizeVal.isEmpty())
                    sizeVal = bareSize;

                if (!sizeVal.isEmpty()) {
                    const QString sc = sizeObj.value(QStringLiteral("size_class")).toString();
                    QString bare = sizeVal;
                    if (bare.startsWith(QStringLiteral("numeric_")))
                        bare = bare.mid(8);
                    bool isNumeric = false;
                    bare.toDouble(&isNumeric);
                    if (isNumeric && (sc.compare(QStringLiteral("numeric"), Qt::CaseInsensitive) == 0
                                      || sc.isEmpty())) {
                        sizeVal = QStringLiteral("numeric_") + bare;
                        if (sc.isEmpty())
                            sizeObj.insert(QStringLiteral("size_class"), QStringLiteral("numeric"));
                    }
                    sizeObj.insert(QStringLiteral("size"), sizeVal);
                }
                if (!sizeObj.isEmpty()) {
                    sizeObj.insert(QStringLiteral("marketplace_id"), mpId);
                    addPatch(QStringLiteral("apparel_size"), QJsonArray{sizeObj});
                }
                QStringList miss;
                if (!sizeObj.contains(QStringLiteral("size")))        miss << QStringLiteral("size");
                if (!sizeObj.contains(QStringLiteral("size_system"))) miss << QStringLiteral("size_system");
                if (!sizeObj.contains(QStringLiteral("size_class")))  miss << QStringLiteral("size_class");
                logOut->append(tr("%1 apparel_size=%2%3")
                    .arg(e.sku,
                         QString::fromUtf8(QJsonDocument(sizeObj).toJson(QJsonDocument::Compact)),
                         miss.isEmpty() ? QString()
                                        : QStringLiteral("  ⚠ MISSING: ") + miss.join(QStringLiteral(","))));
            } else if (hasSimpleSize) {
                // Simple `size`: [{value, language_tag, marketplace_id}] — free text,
                // language_tag REQUIRED. Prefer the child's own stored value (already
                // the right language); else the converted bare size for this country.
                QJsonValue sizeV = rawOr(QStringLiteral("size"), {});
                if (!usable(sizeV) && !bareSize.isEmpty())
                    sizeV = QJsonArray{QJsonObject{
                        {QStringLiteral("value"),          bareSize},
                        {QStringLiteral("language_tag"),   langTag},
                        {QStringLiteral("marketplace_id"), mpId}}};
                if (usable(sizeV)) {
                    addPatch(QStringLiteral("size"), sizeV);
                    logOut->append(tr("%1 size=%2").arg(
                        e.sku,
                        QString::fromUtf8(QJsonDocument(sizeV.toArray()).toJson(QJsonDocument::Compact))));
                } else {
                    logOut->append(tr("⚠ %1: no size available — theme attribute missing!").arg(e.sku));
                }
            } else {
                logOut->append(tr("⚠ %1: schema has neither apparel_size nor size!").arg(e.sku));
            }

            // target_gender / age_range_description — only when the schema defines
            // them. Must be English canonical values (localized values rejected).
            auto topLevelEnglish = [&](const QString &key, const QString &own) {
                if (!ptProps.contains(key)) return;
                QString v = englishScalar(localAttrs, key);
                if (v.isEmpty()) v = englishScalar(ukAttrs, key);
                if (v.isEmpty()) v = englishScalar(parentAttrsFallback, key);
                if (v.isEmpty()) v = own;
                if (v.isEmpty()) v = familyAttrFallback.value(key);
                if (key == QLatin1String("age_range_description"))
                    v = canonicalAge(v);
                if (!v.isEmpty()) addPatch(key, simpleVal(v));
            };
            topLevelEnglish(QStringLiteral("target_gender"),         e.gender);
            topLevelEnglish(QStringLiteral("age_range_description"), e.ageRange);
        }

        logOut->append(tr("%1 %2: %3 patch(es), local listing %4")
                           .arg(e.isParent ? QStringLiteral("parent") : QStringLiteral("child "),
                                e.sku)
                           .arg(patches.size())
                           .arg(localAttrs.isEmpty() ? tr("ABSENT") : tr("found")));

        messagesOut->append(QJsonObject{
            {QStringLiteral("messageId"),     messageId++},
            {QStringLiteral("sku"),           e.sku},
            {QStringLiteral("operationType"), QStringLiteral("PATCH")},
            {QStringLiteral("productType"),   actualPt},
            {QStringLiteral("patches"),       patches},
        });
    }
    co_return;
}

QString PaneSizing::_fillVariationTemplate(
    const QString &templatePath,
    const QString &parentSku,
    const QJsonObject &parentAttrs,
    const QString &attrMarketplaceId,
    const QString &productType,
    const QString &variationTheme,
    const QList<VariationTemplateEntry> &feedEntries,
    const QHash<QString,QString> &attrOverrides)
{
    QXlsx::Document doc(templatePath);
    if (!doc.load()) {
        qWarning() << "_fillVariationTemplate: cannot load" << templatePath;
        return {};
    }

    // Find the Vorlage (data entry) sheet: try by name, then scan for feed_product_type in row 3.
    QString sheetName;
    for (const QString &s : doc.sheetNames()) {
        if (s.compare(QStringLiteral("Vorlage"), Qt::CaseInsensitive) == 0) {
            sheetName = s; break;
        }
    }
    if (sheetName.isEmpty()) {
        for (const QString &s : doc.sheetNames()) {
            doc.selectSheet(s);
            for (int c = 1; c <= 10; ++c) {
                if (doc.read(3, c).toString() == QStringLiteral("feed_product_type")) {
                    sheetName = s; break;
                }
            }
            if (!sheetName.isEmpty()) break;
        }
    }
    if (sheetName.isEmpty()) {
        qWarning() << "_fillVariationTemplate: no Vorlage sheet found in" << templatePath;
        return {};
    }
    doc.selectSheet(sheetName);

    // Map marketplace ID to the country code used in FillerSize size tables.
    static const QHash<QString, QString> kMpToCountry{
        {QStringLiteral("A1PA6795UKMFR9"), QStringLiteral("DE")},
        {QStringLiteral("A13V1IB3VIYZZH"), QStringLiteral("FR")},
        {QStringLiteral("APJ6JRA9NG5V4"),  QStringLiteral("IT")},
        {QStringLiteral("A1RKKUPIHCS9HS"), QStringLiteral("ES")},
        {QStringLiteral("AMEN7PMS3EDWL"),  QStringLiteral("NL")},
        {QStringLiteral("A1F83G8C2ARO7P"), QStringLiteral("UK")},
        {QStringLiteral("ATVPDKIKX0DER"),  QStringLiteral("COM")},
        {QStringLiteral("A2EUQ1WTGCTBG2"), QStringLiteral("COM")},
    };
    const QString templateCountry = kMpToCountry.value(attrMarketplaceId);
    qDebug() << "_fillVariationTemplate: attr marketplace" << attrMarketplaceId
             << "→ template country" << (templateCountry.isEmpty() ? "(unknown)" : templateCountry);

    // Build column name → 1-based column index from row 3.
    QHash<QString, int> colMap;
    for (int c = 1; c <= 300; ++c) {
        const QString key = doc.read(3, c).toString().trimmed();
        if (!key.isEmpty()) colMap[key] = c;
    }

    // Write a value into the named column of a row (no-op if column not in template).
    auto cell = [&](int row, const QString &colName, const QString &val) {
        if (val.isEmpty()) return;
        const auto it = colMap.constFind(colName);
        if (it != colMap.constEnd()) doc.write(row, it.value(), val);
    };

    // Extract first scalar value from a SP-API attribute array.
    auto firstVal = [&](const QString &key) -> QString {
        const QJsonArray arr = parentAttrs.value(key).toArray();
        if (arr.isEmpty()) return {};
        const QJsonObject obj = arr.first().toObject();
        const QString v = obj.value(QStringLiteral("value")).toString();
        return v.isEmpty() ? obj.value(QStringLiteral("name")).toString() : v;
    };
    auto allVals = [&](const QString &key) -> QStringList {
        QStringList out;
        for (const QJsonValue &jv : parentAttrs.value(key).toArray())
            out << jv.toObject().value(QStringLiteral("value")).toString();
        out.removeAll(QString{});
        return out;
    };

    // Map SP-API gtinType ("ean","gtin","upc","gtin14") to Amazon flat file type string.
    auto idType = [](const QString &t) -> QString {
        if (t == QLatin1String("ean"))    return QStringLiteral("EAN");
        if (t == QLatin1String("gtin"))   return QStringLiteral("GTIN");
        if (t == QLatin1String("upc"))    return QStringLiteral("UPC");
        if (t == QLatin1String("gtin14")) return QStringLiteral("GTIN14");
        return t.toUpper();
    };

    // Find parent feed entry (for GTIN).
    VariationTemplateEntry parentEntry;
    for (const auto &e : feedEntries)
        if (e.isParent) { parentEntry = e; break; }

    // Build ASIN → {color, size} from broken child table.
    QHash<QString, QPair<QString,QString>> asinColorSize;
    if (m_brokenChildTable) {
        for (const auto &r : m_brokenChildTable->rows())
            asinColorSize.insert(r.asin, {r.color, r.size});
    }

    // Collect apparel attrs from children: "first found" wins across the whole set.
    // Parent listing often lacks these; use any child that has a value.
    QString fbSizeSystem, fbSizeClass, fbGender, fbAgeRange, fbBodyType, fbHeightType;
    for (const auto &e : feedEntries) {
        if (e.isParent) continue;
        if (fbSizeSystem.isEmpty()  && !e.sizeSystem.isEmpty())  fbSizeSystem  = e.sizeSystem;
        if (fbSizeClass.isEmpty()   && !e.sizeClass.isEmpty())   fbSizeClass   = e.sizeClass;
        if (fbGender.isEmpty()      && !e.gender.isEmpty())      fbGender      = e.gender;
        if (fbAgeRange.isEmpty()    && !e.ageRange.isEmpty())    fbAgeRange    = e.ageRange;
        if (fbBodyType.isEmpty()    && !e.bodyType.isEmpty())    fbBodyType    = e.bodyType;
        if (fbHeightType.isEmpty()  && !e.heightType.isEmpty())  fbHeightType  = e.heightType;
        if (!fbSizeSystem.isEmpty() && !fbSizeClass.isEmpty() && !fbGender.isEmpty()
                && !fbAgeRange.isEmpty() && !fbBodyType.isEmpty() && !fbHeightType.isEmpty())
            break;
    }

    const QString brand = firstVal(QStringLiteral("brand"));
    const QStringList bullets = allVals(QStringLiteral("bullet_point"));
    const QStringList nodes   = allVals(QStringLiteral("recommended_browse_nodes"));

    // dataRow = 4 (from template settings URL parameter).
    int row = 4;

    // --- Parent row ---
    cell(row, QStringLiteral("feed_product_type"),      productType);
    cell(row, QStringLiteral("item_sku"),               parentSku);
    cell(row, QStringLiteral("brand_name"),             brand);
    cell(row, QStringLiteral("update_delete"),          QStringLiteral("PartialUpdate"));
    if (!parentEntry.gtin.isEmpty()) {
        cell(row, QStringLiteral("external_product_id"),      parentEntry.gtin);
        cell(row, QStringLiteral("external_product_id_type"), idType(parentEntry.gtinType));
    }
    cell(row, QStringLiteral("item_name"),              firstVal(QStringLiteral("item_name")));
    cell(row, QStringLiteral("product_description"),    firstVal(QStringLiteral("product_description")));
    if (!nodes.isEmpty())
        cell(row, QStringLiteral("recommended_browse_nodes"), nodes.first());
    cell(row, QStringLiteral("outer_material_type"),    firstVal(QStringLiteral("outer")));
    cell(row, QStringLiteral("inner_material_type"),    firstVal(QStringLiteral("inner")));
    cell(row, QStringLiteral("parent_child"),           QStringLiteral("Parent"));
    cell(row, QStringLiteral("relationship_type"),      QStringLiteral("Variation"));
    cell(row, QStringLiteral("variation_theme"),        variationTheme);
    cell(row, QStringLiteral("generic_keywords"),
         allVals(QStringLiteral("generic_keyword")).join(QLatin1Char(' ')));
    cell(row, QStringLiteral("pattern_type"),           firstVal(QStringLiteral("pattern_type")));
    cell(row, QStringLiteral("lifestyle"),              firstVal(QStringLiteral("lifestyle")));
    cell(row, QStringLiteral("style_name"),             firstVal(QStringLiteral("style")));
    cell(row, QStringLiteral("neck_style"),             firstVal(QStringLiteral("neck")));
    cell(row, QStringLiteral("department_name"),        firstVal(QStringLiteral("department")));
    for (int i = 0; i < bullets.size() && i < 5; ++i)
        cell(row, QStringLiteral("bullet_point%1").arg(i + 1), bullets.at(i));
    cell(row, QStringLiteral("item_length_description"), firstVal(QStringLiteral("item_length_description")));
    cell(row, QStringLiteral("country_of_origin"),      firstVal(QStringLiteral("country_of_origin")));
    cell(row, QStringLiteral("condition_type"),         firstVal(QStringLiteral("condition_type")));
    // For each field: use parent attrs if available, then first-found child value, then override.
    auto parentOrChild = [&](const QString &key, const QString &childFb) {
        const QString v = firstVal(key);
        if (!v.isEmpty()) return v;
        if (!childFb.isEmpty()) return childFb;
        return attrOverrides.value(key);
    };
    cell(row, QStringLiteral("target_gender"),         parentOrChild(QStringLiteral("target_gender"),         fbGender));
    cell(row, QStringLiteral("age_range_description"),  parentOrChild(QStringLiteral("age_range_description"), fbAgeRange));
    cell(row, QStringLiteral("apparel_body_type"),      parentOrChild(QStringLiteral("apparel_body_type"),     fbBodyType));
    cell(row, QStringLiteral("apparel_height_type"),    parentOrChild(QStringLiteral("apparel_height_type"),   fbHeightType));
    cell(row, QStringLiteral("apparel_size_system"),    parentOrChild(QStringLiteral("apparel_size_system"),   fbSizeSystem));
    cell(row, QStringLiteral("apparel_size_class"),     parentOrChild(QStringLiteral("apparel_size_class"),    fbSizeClass));

    // --- Child rows ---
    for (const auto &e : feedEntries) {
        if (e.isParent) continue;
        ++row;
        cell(row, QStringLiteral("feed_product_type"),  productType);
        cell(row, QStringLiteral("item_sku"),           e.sku);
        cell(row, QStringLiteral("brand_name"),         brand);
        cell(row, QStringLiteral("update_delete"),      QStringLiteral("PartialUpdate"));
        if (!e.gtin.isEmpty()) {
            cell(row, QStringLiteral("external_product_id"),      e.gtin);
            cell(row, QStringLiteral("external_product_id_type"), idType(e.gtinType));
        }
        cell(row, QStringLiteral("parent_child"),       QStringLiteral("Child"));
        cell(row, QStringLiteral("parent_sku"),         parentSku);
        cell(row, QStringLiteral("relationship_type"),  QStringLiteral("Variation"));
        cell(row, QStringLiteral("variation_theme"),    variationTheme);
        cell(row, QStringLiteral("color_name"),         e.color);
        cell(row, QStringLiteral("color_map"),          e.color);
        {
            QString sz = e.size;
            if (!sz.isEmpty() && !templateCountry.isEmpty()
                    && !e.sizeSource.isEmpty() && e.sizeSource != templateCountry) {
                sz = FillerSize::convertSize(sz, e.sizeSource, templateCountry);
            }
            cell(row, QStringLiteral("size_name"),    sz);
            cell(row, QStringLiteral("apparel_size"), sz);
        }
        // Each field: own value → first-found across children → parent attrs → override.
        auto best = [&](const QString &own, const QString &fb, const QString &key) {
            if (!own.isEmpty()) return own;
            if (!fb.isEmpty()) return fb;
            const QString pv = firstVal(key);
            if (!pv.isEmpty()) return pv;
            return attrOverrides.value(key);
        };
        cell(row, QStringLiteral("apparel_size_system"),   best(e.sizeSystem,  fbSizeSystem,  QStringLiteral("apparel_size_system")));
        cell(row, QStringLiteral("apparel_size_class"),    best(e.sizeClass,   fbSizeClass,   QStringLiteral("apparel_size_class")));
        cell(row, QStringLiteral("target_gender"),         best(e.gender,      fbGender,      QStringLiteral("target_gender")));
        cell(row, QStringLiteral("age_range_description"), best(e.ageRange,    fbAgeRange,    QStringLiteral("age_range_description")));
        cell(row, QStringLiteral("apparel_body_type"),     best(e.bodyType,    fbBodyType,    QStringLiteral("apparel_body_type")));
        cell(row, QStringLiteral("apparel_height_type"),   best(e.heightType,  fbHeightType,  QStringLiteral("apparel_height_type")));
    }

    // Save filled copy next to the working dir, preserving the original's extension.
    const QString ext     = QFileInfo(templatePath).suffix();
    const QString outPath = m_productWorkingDir.filePath(
        QStringLiteral("filled_variation_%1.%2")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")), ext));
    if (!doc.saveAs(outPath)) {
        qWarning() << "_fillVariationTemplate: saveAs failed:" << outPath;
        return {};
    }
    return outPath;
}

void PaneSizing::_generateParentFlatFile(const QString &marketplaceCode,
                                          const QString &parentSku,
                                          const QJsonObject &parentAttrs,
                                          const QString &productType,
                                          const QString &variationTheme,
                                          const QList<FlatFileChildEntry> &children)
{
    if (!m_productWorkingDir.exists()) return;

    auto firstVal = [&](const QJsonObject &attrs, const QString &key) -> QString {
        const QJsonArray arr = attrs.value(key).toArray();
        if (arr.isEmpty()) return {};
        const QJsonObject obj = arr.first().toObject();
        const QString v = obj.value(QStringLiteral("value")).toString();
        return v.isEmpty() ? obj.value(QStringLiteral("name")).toString() : v;
    };
    auto allVals = [&](const QJsonObject &attrs, const QString &key) -> QStringList {
        QStringList out;
        for (const QJsonValue &jv : attrs.value(key).toArray())
            out << jv.toObject().value(QStringLiteral("value")).toString();
        out.removeAll(QString{});
        return out;
    };

    // --- Column headers (fixed order) ---
    QStringList headers;
    headers << QStringLiteral("item_sku")
            << QStringLiteral("update_delete")
            << QStringLiteral("feed_product_type")
            << QStringLiteral("item_name")
            << QStringLiteral("brand_name")
            << QStringLiteral("parent_child")
            << QStringLiteral("parent_sku")
            << QStringLiteral("relationship_type")
            << QStringLiteral("variation_theme")
            << QStringLiteral("color_name")
            << QStringLiteral("size_name")
            << QStringLiteral("bullet_point1")
            << QStringLiteral("bullet_point2")
            << QStringLiteral("bullet_point3")
            << QStringLiteral("bullet_point4")
            << QStringLiteral("bullet_point5")
            << QStringLiteral("generic_keywords")
            << QStringLiteral("product_description")
            << QStringLiteral("country_of_origin")
            << QStringLiteral("department_name")
            << QStringLiteral("condition_type")
            << QStringLiteral("lifestyle")
            << QStringLiteral("pattern_type")
            << QStringLiteral("style_name")
            << QStringLiteral("item_length_description")
            << QStringLiteral("neck_style")
            << QStringLiteral("outer_material_type");

    const QStringList nodes = allVals(parentAttrs, QStringLiteral("recommended_browse_nodes"));
    for (int i = 0; i < nodes.size(); ++i)
        headers << QStringLiteral("recommended_browse_nodes%1").arg(i + 1);

    const int nCols = headers.size();

    // --- Parent row values (same column order) ---
    const QStringList bullets = allVals(parentAttrs, QStringLiteral("bullet_point"));
    QStringList parentRow;
    parentRow << parentSku
              << QStringLiteral("PartialUpdate")
              << productType
              << firstVal(parentAttrs, QStringLiteral("item_name"))
              << firstVal(parentAttrs, QStringLiteral("brand"))
              << QStringLiteral("Parent")
              << QString{}                          // parent_sku (empty for parent)
              << QStringLiteral("Variation")
              << variationTheme
              << QString{}                          // color_name (empty for parent)
              << QString{}                          // size_name  (empty for parent)
              << (bullets.size() > 0 ? bullets.at(0) : QString{})
              << (bullets.size() > 1 ? bullets.at(1) : QString{})
              << (bullets.size() > 2 ? bullets.at(2) : QString{})
              << (bullets.size() > 3 ? bullets.at(3) : QString{})
              << (bullets.size() > 4 ? bullets.at(4) : QString{})
              << allVals(parentAttrs, QStringLiteral("generic_keyword")).join(QLatin1Char(' '))
              << firstVal(parentAttrs, QStringLiteral("product_description"))
              << firstVal(parentAttrs, QStringLiteral("country_of_origin"))
              << firstVal(parentAttrs, QStringLiteral("department"))
              << firstVal(parentAttrs, QStringLiteral("condition_type"))
              << firstVal(parentAttrs, QStringLiteral("lifestyle"))
              << firstVal(parentAttrs, QStringLiteral("pattern_type"))
              << firstVal(parentAttrs, QStringLiteral("style"))
              << firstVal(parentAttrs, QStringLiteral("item_length_description"))
              << firstVal(parentAttrs, QStringLiteral("neck"))
              << firstVal(parentAttrs, QStringLiteral("outer"));
    for (const QString &node : nodes)
        parentRow << node;

    // --- Write xlsx ---
    const QString fileName = QStringLiteral("flatfile_parent_%1_%2.txt")
                                 .arg(marketplaceCode.toLower(),
                                      QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
    const QString filePath = m_productWorkingDir.filePath(fileName);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "_generateParentFlatFile: cannot open" << filePath;
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    auto writeTsvRow = [&](const QStringList &row) {
        out << row.join(QLatin1Char('\t')) << QLatin1Char('\n');
    };

    writeTsvRow(headers);
    writeTsvRow(parentRow);

    for (const auto &ch : children) {
        QStringList childRow(nCols);
        auto set = [&](const QString &hdr, const QString &val) {
            const int idx = headers.indexOf(hdr);
            if (idx >= 0) childRow[idx] = val;
        };
        set(QStringLiteral("item_sku"),          ch.sku);
        set(QStringLiteral("update_delete"),     QStringLiteral("PartialUpdate"));
        set(QStringLiteral("feed_product_type"), productType);
        set(QStringLiteral("parent_child"),      QStringLiteral("Child"));
        set(QStringLiteral("parent_sku"),        parentSku);
        set(QStringLiteral("relationship_type"), QStringLiteral("Variation"));
        set(QStringLiteral("variation_theme"),   variationTheme);
        set(QStringLiteral("color_name"),        ch.color);
        set(QStringLiteral("size_name"),         ch.size);
        writeTsvRow(childRow);
    }

    file.close();
    qDebug() << "_generateParentFlatFile: saved" << filePath;
}

QCoro::Task<void> PaneSizing::_runBrokenChildFix(bool fixParents, bool fixImages,
                                                 bool checkOnly)
{
    if (!m_brokenChildTable) co_return;

    // ── 1. Collect fix targets ──────────────────────────────────────────────
    QList<BrokenChildTable::FixTarget> targets =
        m_brokenChildTable->getFixTargets(fixParents, fixImages);

    // Remove targets for marketplaces the seller has no active listing on.
    // Read the country list widget at fix-time — it reflects marketplacesChecked
    // output and marks absent marketplaces as "(missing)". This is more reliable
    // than setMarketplaceActive() which depends on signal timing.
    {
        QSet<QString> missingCodes;
        for (int i = 0; i < ui->listWidgetCountries->count(); ++i) {
            const QString item = ui->listWidgetCountries->item(i)->text().toLower().trimmed();
            if (item.contains(QLatin1String("(missing)")))
                missingCodes.insert(item.split(QLatin1Char(' ')).first());
        }
        if (!missingCodes.isEmpty()) {
            targets.erase(
                std::remove_if(targets.begin(), targets.end(),
                    [&](const BrokenChildTable::FixTarget &t) {
                        return missingCodes.contains(
                            m_brokenChildTable->marketplaceAt(t.mktIdx).code.toLower());
                    }),
                targets.end());
        }
    }

    if (targets.isEmpty()) {
        QMessageBox::information(this, tr("Fix"), tr("Nothing to fix."));
        co_return;
    }

    // ── 2. Progress dialog (same pattern as PaneWarnings) ───────────────────
    auto *progressDlg = new QDialog(this);
    progressDlg->setAttribute(Qt::WA_DeleteOnClose);
    progressDlg->setWindowModality(Qt::ApplicationModal);
    progressDlg->setWindowTitle(tr("Fixing broken children…"));
    progressDlg->resize(640, 460);
    auto *pLayout = new QVBoxLayout(progressDlg);

    auto *statusLabel = new QLabel(tr("Starting…"), progressDlg);
    QFont boldFont = statusLabel->font();
    boldFont.setBold(true);
    statusLabel->setFont(boldFont);
    pLayout->addWidget(statusLabel);

    auto *progressBar = new QProgressBar(progressDlg);
    progressBar->setRange(0, qMax(1, targets.size()));
    progressBar->setValue(0);
    pLayout->addWidget(progressBar);

    auto *logEdit = new QTextEdit(progressDlg);
    logEdit->setReadOnly(true);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pLayout->addWidget(logEdit);

    auto *btnLayout = new QHBoxLayout();
    auto *copyBtn = new QPushButton(tr("Copy log"), progressDlg);
    btnLayout->addWidget(copyBtn);
    auto *openDirBtn = new QPushButton(tr("Open dir"), progressDlg);
    openDirBtn->setEnabled(m_productWorkingDir.exists());
    btnLayout->addWidget(openDirBtn);
    auto *copyPathBtn = new QPushButton(tr("Copy path"), progressDlg);
    copyPathBtn->setEnabled(false);
    copyPathBtn->setToolTip(tr("Copy path of the filled template to clipboard"));
    btnLayout->addWidget(copyPathBtn);
    btnLayout->addStretch();
    auto *closeBtns = new QDialogButtonBox(QDialogButtonBox::Close, progressDlg);
    QPushButton *closeBtn = closeBtns->button(QDialogButtonBox::Close);
    if (closeBtn) closeBtn->setEnabled(false);
    btnLayout->addWidget(closeBtns);
    pLayout->addLayout(btnLayout);

    QPointer<QLabel>       statusLabelPtr(statusLabel);
    QPointer<QProgressBar> progressBarPtr(progressBar);
    QPointer<QTextEdit>    logEditPtr(logEdit);
    QPointer<QPushButton>  closeBtnPtr(closeBtn);
    QPointer<QPushButton>  copyPathBtnPtr(copyPathBtn);
    QPointer<QDialog>      dlgPtr(progressDlg);

    auto appendLog = [logEditPtr](const QString &line) {
        if (!logEditPtr) return;
        const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
        logEditPtr->append(QStringLiteral("[%1] %2").arg(ts, line));
    };

    connect(copyBtn, &QPushButton::clicked, progressDlg, [logEditPtr]() {
        if (logEditPtr)
            QGuiApplication::clipboard()->setText(logEditPtr->toPlainText());
    });
    const QString prodWorkingPath = m_productWorkingDir.absolutePath();
    connect(openDirBtn, &QPushButton::clicked, progressDlg, [prodWorkingPath]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(prodWorkingPath));
    });
    connect(closeBtns, &QDialogButtonBox::rejected, progressDlg, &QDialog::close);

    progressDlg->show();

    appendLog(tr("Found %1 cell(s) to fix.").arg(targets.size()));

    // ── 3. Determine product type ───────────────────────────────────────────
    if (m_productType.isEmpty() && m_productWorkingDir.exists()) {
        QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
        m_productType = ps.value(QStringLiteral("sizing/productType")).toString();
    }

    // ── 4. Collect all unique child + parent ASINs that appear in targets ──
    QSet<QString> uniqueAsins;
    for (const auto &t : targets) {
        const auto &row = m_brokenChildTable->rows().at(t.rowIdx);
        if (!row.asin.isEmpty()) uniqueAsins.insert(row.asin);
        if (!row.parentAsin.isEmpty()) uniqueAsins.insert(row.parentAsin);
    }

    // Map asin → sku (resolved from tree, settings.ini, or report)
    QMap<QString, QString> asinToSku;
    // Map asin → {gtin, gtinType} — populated from the listings report as a side-effect
    QHash<QString, QPair<QString,QString>> asinToGtin;

    // 4a. Tree model first
    if (m_treeModel) {
        for (int fi = 0; fi < m_treeModel->rowCount(); ++fi) {
            const QModelIndex pi = m_treeModel->index(fi, 0);
            const QString pAsin = m_treeModel->data(
                m_treeModel->index(fi, TreeSizingAsins::ASIN)).toString().trimmed();
            const QString pSku = m_treeModel->data(
                m_treeModel->index(fi, TreeSizingAsins::SKU)).toString().trimmed();
            if (!pAsin.isEmpty() && !pSku.isEmpty())
                asinToSku.insert(pAsin, pSku);
            for (int ci = 0; ci < m_treeModel->rowCount(pi); ++ci) {
                const QString cAsin = m_treeModel->data(
                    m_treeModel->index(ci, TreeSizingAsins::ASIN, pi)).toString().trimmed();
                const QString cSku = m_treeModel->data(
                    m_treeModel->index(ci, TreeSizingAsins::SKU, pi)).toString().trimmed();
                if (!cAsin.isEmpty() && !cSku.isEmpty())
                    asinToSku.insert(cAsin, cSku);
            }
        }
    }

    // 4b. settings.ini cache
    if (m_productWorkingDir.exists()) {
        QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                     QSettings::IniFormat);
        for (const QString &asin : std::as_const(uniqueAsins)) {
            if (asinToSku.value(asin).isEmpty()) {
                const QString sku = ps.value(QStringLiteral("sizing/skus/") + asin).toString();
                if (!sku.isEmpty()) asinToSku.insert(asin, sku);
            }
        }
    }

    // 4c. Check if anything is still missing → Reports API fallback
    bool anyMissing = false;
    for (const QString &asin : std::as_const(uniqueAsins)) {
        if (asinToSku.value(asin).isEmpty()) { anyMissing = true; break; }
    }

    if (anyMissing) {
        if (statusLabelPtr) statusLabelPtr->setText(tr("Fetching all listings…"));
        appendLog(tr("Some SKUs unknown — fetching all-listings report (up to ~3 min)…"));

        // Use the seller's primary marketplace (first in country list = FR for EU
        // sellers). Broken target marketplaces (ES, IT, GB mirror…) often don't
        // carry the parent virtual listing, making their reports return 0.
        const QString reportMpId = firstMarketplaceIdFromCountryList(ui->listWidgetCountries);

        QHash<QString, QString> reportMap;
        co_await _fetchAllSkusCached(reportMpId, &reportMap, false, &asinToGtin);

        if (reportMap.isEmpty() && !m_api->lastError().isEmpty()) {
            appendLog(tr("Reports API error: %1").arg(m_api->lastError()));
        } else {
            int filled = 0;
            for (const QString &asin : std::as_const(uniqueAsins)) {
                if (asinToSku.value(asin).isEmpty()) {
                    const QString sku = reportMap.value(asin);
                    if (!sku.isEmpty()) {
                        asinToSku.insert(asin, sku);
                        ++filled;
                    }
                }
            }
            appendLog(tr("Report resolved %1 new SKU(s).").arg(filled));

            // Cache resolved SKUs to settings.ini
            if (m_productWorkingDir.exists()) {
                QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                             QSettings::IniFormat);
                for (auto it = asinToSku.constBegin(); it != asinToSku.constEnd(); ++it) {
                    if (uniqueAsins.contains(it.key()) && !it.value().isEmpty())
                        ps.setValue(QStringLiteral("sizing/skus/") + it.key(), it.value());
                }
            }
        }
    }

    // ── 4d. Derive missing parent SKUs from child listing attributes ───────────
    // Virtual parents never appear in listing reports.
    // Try every candidate child until one returns a parentSku — a broken child
    // may not have child_parent_sku_relationships set, but a sibling will.
    for (const QString &missingAsin : std::as_const(uniqueAsins)) {
        if (!asinToSku.value(missingAsin).isEmpty()) continue;

        bool resolved = false;
        QSet<QString> triedChildSkus;
        for (const auto &t2 : targets) {
            if (resolved) break;
            const auto &row2 = m_brokenChildTable->rows().at(t2.rowIdx);
            if (row2.parentAsin != missingAsin) continue;
            const QString knownChildSku = asinToSku.value(row2.asin);
            if (knownChildSku.isEmpty()) continue;
            if (triedChildSkus.contains(knownChildSku)) continue; // already tried this child
            triedChildSkus.insert(knownChildSku);

            // Use the seller's primary marketplace — GB and other auto-mirrored
            // marketplaces often have no child_parent_sku_relationships attribute.
            const QString mpId2 = firstMarketplaceIdFromCountryList(ui->listWidgetCountries);
            if (mpId2.isEmpty()) continue;

            appendLog(tr("Looking up parent SKU for %1 via child %2 (%3) on %4…")
                          .arg(missingAsin, row2.asin, knownChildSku, mpId2));

            QString parentSku;
            QString rawResponse;
            co_await m_api->fetchParentSku(mpId2, knownChildSku, &parentSku, &rawResponse);

            if (!parentSku.isEmpty()) {
                asinToSku.insert(missingAsin, parentSku);
                appendLog(tr("  → parent SKU: %1").arg(parentSku));
                if (m_productWorkingDir.exists()) {
                    QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                                 QSettings::IniFormat);
                    ps.setValue(QStringLiteral("sizing/skus/") + missingAsin, parentSku);
                }
                resolved = true;
            } else {
                appendLog(tr("  → no parentSku via %1 — trying next child…").arg(row2.asin));
                // Log raw response once (first failure) so we can see what the API actually returns
                if (!resolved && !rawResponse.isEmpty())
                    appendLog(tr("  [raw] %1").arg(rawResponse.left(800)));
            }
        }
        if (!resolved)
            appendLog(tr("  ✗ Could not resolve parent SKU for %1").arg(missingAsin));
    }

    // ── 4e. Push all resolved SKUs back into the tree model ─────────────────
    // The tree is populated from the Catalog API which never returns seller SKUs.
    // Updating it here means the SKU column is visible after the first fix run,
    // and step 4a will find them on subsequent runs without needing the report.
    if (m_treeModel) {
        for (auto it = asinToSku.constBegin(); it != asinToSku.constEnd(); ++it) {
            if (!it.value().isEmpty())
                m_treeModel->setSku(it.key(), it.value());
        }
    }

    // ── 5. If still no product type, try to derive from a known SKU ─────────
    if (m_productType.isEmpty() && !asinToSku.isEmpty()) {
        QString anySku;
        QString anyMpId;
        for (const auto &t : targets) {
            const auto &row = m_brokenChildTable->rows().at(t.rowIdx);
            const QString sku = asinToSku.value(row.asin);
            if (!sku.isEmpty()) {
                anySku  = sku;
                anyMpId = m_brokenChildTable->marketplaceAt(t.mktIdx).id;
                break;
            }
        }
        if (!anySku.isEmpty() && !anyMpId.isEmpty()) {
            appendLog(tr("Detecting product type from SKU %1…").arg(anySku));
            co_await m_api->fetchListingProductType(anyMpId, anySku, &m_productType);
            if (!m_productType.isEmpty()) {
                appendLog(tr("Product type: %1").arg(m_productType));
                if (m_productWorkingDir.exists()) {
                    QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                                 QSettings::IniFormat);
                    ps.setValue(QStringLiteral("sizing/productType"), m_productType);
                }
            }
        }
    }

    if (m_productType.isEmpty()) {
        // Ask the user once
        bool ok = false;
        const QString entered = QInputDialog::getText(
            this, tr("Product Type"),
            tr("Could not auto-detect the product type.\n"
               "Enter the Amazon product type (e.g. DRESS, SHIRT, SHOES):"),
            QLineEdit::Normal, {}, &ok);
        if (ok && !entered.trimmed().isEmpty()) {
            m_productType = entered.trimmed().toUpper();
            if (m_productWorkingDir.exists()) {
                QSettings ps(m_productWorkingDir.filePath(QStringLiteral("settings.ini")),
                             QSettings::IniFormat);
                ps.setValue(QStringLiteral("sizing/productType"), m_productType);
            }
        }
    }

    if (m_productType.isEmpty()) {
        appendLog(tr("No product type — aborting."));
        if (statusLabelPtr) statusLabelPtr->setText(tr("Aborted."));
        if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
        co_return;
    }

    // ── 5e1. Check-only mode: read-only settled-state diagnostic ────────────
    // Reports, per broken marketplace: the apparel_size schema rules (allowed
    // size_system/size_class enum values), the parent's and each child's stored
    // apparel_size composite, listing status, relationships and issues.
    // Performs NO submission, so it never resets Amazon's propagation clock.
    if (checkOnly) {
        QString parentSku;
        for (const auto &t : targets) {
            const auto &row = m_brokenChildTable->rows().at(t.rowIdx);
            if (!row.parentAsin.isEmpty()) {
                parentSku = asinToSku.value(row.parentAsin);
                if (!parentSku.isEmpty()) break;
            }
        }
        QStringList mpIds;
        for (const auto &t : targets) {
            const QString mpId = m_brokenChildTable->marketplaceAt(t.mktIdx).id;
            if (!mpIds.contains(mpId)) mpIds << mpId;
        }
        QList<QPair<QString, QString>> childSkuAsin; // (sku, asin)
        {
            QSet<QString> seen;
            for (const auto &t : targets) {
                const auto &row = m_brokenChildTable->rows().at(t.rowIdx);
                const QString sku = asinToSku.value(row.asin);
                if (sku.isEmpty() || seen.contains(sku)) continue;
                seen.insert(sku);
                childSkuAsin.append(qMakePair(sku, row.asin));
            }
        }

        int step = 0;
        if (progressBarPtr) progressBarPtr->setRange(0, mpIds.size());
        for (const QString &mpId : mpIds) {
            QString mpCode = mpId;
            for (int i = 0; i < m_brokenChildTable->marketplaceCount(); ++i) {
                const auto &s = m_brokenChildTable->marketplaceAt(i);
                if (s.id == mpId) { mpCode = s.code; break; }
            }
            appendLog(tr("═══ %1 — status check (read-only) ═══").arg(mpCode));
            if (statusLabelPtr) statusLabelPtr->setText(tr("Checking %1…").arg(mpCode));

            QString actualPt; // the listings' REAL product type on this marketplace

            if (!parentSku.isEmpty()) {
                AmazonCatalogApi::ListingCheck pc;
                co_await m_api->checkListing(mpId, parentSku, &pc);
                actualPt = pc.productType;
                QJsonObject pAttrs;
                co_await m_api->fetchListingAttributes(mpId, parentSku, &pAttrs);
                const QJsonArray ps = pAttrs.value(QStringLiteral("apparel_size")).toArray();
                appendLog(tr("  parent %1: %2 productType=%3 status=[%4] linked children=%5 theme=%6")
                              .arg(parentSku,
                                   pc.exists ? tr("exists") : tr("NOT LISTED"),
                                   pc.productType.isEmpty() ? tr("(none)") : pc.productType,
                                   pc.status)
                              .arg(pc.childSkus.size())
                              .arg(pc.variationTheme.isEmpty() ? tr("(none)") : pc.variationTheme));
                appendLog(tr("    stored apparel_size: %1").arg(
                    ps.isEmpty() ? tr("(none)")
                                 : QString::fromUtf8(QJsonDocument(ps).toJson(QJsonDocument::Compact))));
                for (const QString &iss : pc.issues)
                    appendLog(QStringLiteral("    ") + iss);
            }

            for (const auto &cs : childSkuAsin) {
                AmazonCatalogApi::ListingCheck cc;
                co_await m_api->checkListing(mpId, cs.first, &cc);
                if (actualPt.isEmpty()) actualPt = cc.productType;
                QJsonObject cAttrs;
                co_await m_api->fetchListingAttributes(mpId, cs.first, &cAttrs);
                const QJsonArray csz = cAttrs.value(QStringLiteral("apparel_size")).toArray();
                const QJsonArray simpleSz = cAttrs.value(QStringLiteral("size")).toArray();
                const bool linked = !parentSku.isEmpty() && cc.parentSku == parentSku;
                appendLog(tr("  %1 (%2): parent=%3%4 productType=%5 status=[%6]")
                              .arg(cs.first, cs.second,
                                   cc.parentSku.isEmpty() ? tr("(none)") : cc.parentSku,
                                   linked ? QStringLiteral(" ✓") : QString(),
                                   cc.productType.isEmpty() ? tr("(none)") : cc.productType,
                                   cc.status));
                appendLog(tr("    stored apparel_size: %1 | size: %2").arg(
                    csz.isEmpty() ? tr("(none)")
                                  : QString::fromUtf8(QJsonDocument(csz).toJson(QJsonDocument::Compact)),
                    simpleSz.isEmpty() ? tr("(none)")
                                       : QString::fromUtf8(QJsonDocument(simpleSz).toJson(QJsonDocument::Compact))));
                for (const QString &iss : cc.issues)
                    appendLog(QStringLiteral("    ") + iss);
            }

            // Schema of the listings' ACTUAL product type: which attribute names
            // exist here (size vs apparel_size), and the apparel_size enums if any.
            {
                const QString ptForSchema = actualPt.isEmpty() ? m_productType : actualPt;
                QSet<QString> ptProps;
                co_await m_api->fetchProductTypeSchemaProps(mpId, ptForSchema, &ptProps);
                QStringList sizingProps;
                for (const QString &p : {QStringLiteral("size"), QStringLiteral("apparel_size"),
                                         QStringLiteral("color"), QStringLiteral("color_name"),
                                         QStringLiteral("color_map"),
                                         QStringLiteral("special_size_type")}) {
                    if (ptProps.contains(p)) sizingProps << p;
                }
                appendLog(tr("  schema (%1): %2 top-level attrs; sizing-related: %3")
                              .arg(ptForSchema)
                              .arg(ptProps.size())
                              .arg(sizingProps.isEmpty() ? tr("(none)")
                                                         : sizingProps.join(QStringLiteral(", "))));
                if (ptProps.contains(QStringLiteral("apparel_size"))) {
                    QStringList sysEnums, clsEnums;
                    QString schemaDump;
                    co_await m_api->fetchApparelSizeSchemaInfo(mpId, ptForSchema,
                                                               &sysEnums, &clsEnums, &schemaDump);
                    appendLog(tr("  apparel_size.size_system allowed: %1").arg(
                        sysEnums.isEmpty() ? tr("(none found)") : sysEnums.join(QStringLiteral(", "))));
                    appendLog(tr("  apparel_size.size_class allowed: %1").arg(
                        clsEnums.isEmpty() ? tr("(none found)") : clsEnums.join(QStringLiteral(", "))));
                    if (!schemaDump.isEmpty())
                        appendLog(tr("  schema dump: %1").arg(schemaDump));
                }
            }
            ++step;
            if (progressBarPtr) progressBarPtr->setValue(step);
        }
        appendLog(tr("Check complete — nothing was submitted."));
        if (statusLabelPtr) statusLabelPtr->setText(tr("Done (read-only)."));
        if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
        co_return;
    }

    // ── 5e2. Fetch variation_theme for the parent PATCH (step 5f) ────────────
    QString variationTheme;
    if (fixParents) {
        const QString primaryMpId = firstMarketplaceIdFromCountryList(ui->listWidgetCountries);
        QString parentSkuForFetch;
        for (const auto &t : targets) {
            parentSkuForFetch = asinToSku.value(m_brokenChildTable->rows().at(t.rowIdx).parentAsin);
            if (!parentSkuForFetch.isEmpty()) break;
        }
        if (!primaryMpId.isEmpty() && !parentSkuForFetch.isEmpty()) {
            QString unused;
            co_await m_api->fetchListingBrandAndTheme(primaryMpId, parentSkuForFetch,
                                                      &unused, &variationTheme);
            appendLog(tr("variation_theme: %1").arg(
                variationTheme.isEmpty() ? tr("(not found)") : variationTheme));
        }
    }

    // ── 5f. Individual parent patches skipped (useless for refresh) ────────
    /*
    if (fixParents) {
        ...
    }
    */

    // ── 5g. Upload full-fidelity JSON_LISTINGS_FEED per marketplace ─────────
    // When the feed path ran, the per-child PATCH in 6a is skipped: the feed
    // carries strictly more data, and concurrent submissions for the same SKU
    // can interleave badly in Amazon's async processing.
    bool fullFeedSubmitted = false;
    if (fixParents) {
        const QString primaryMpId = firstMarketplaceIdFromCountryList(ui->listWidgetCountries);
        if (!primaryMpId.isEmpty()) {
            QString feedParentSku;
            for (const auto &t : targets) {
                const auto &row = m_brokenChildTable->rows().at(t.rowIdx);
                if (!row.parentAsin.isEmpty()) {
                    feedParentSku = asinToSku.value(row.parentAsin);
                    if (!feedParentSku.isEmpty()) break;
                }
            }

            if (!feedParentSku.isEmpty()) {
                QList<AmazonCatalogApi::VariationFeedEntry> feedEntries;

                {
                    QString parentAsin;
                    for (const auto &t : targets) {
                        parentAsin = m_brokenChildTable->rows().at(t.rowIdx).parentAsin;
                        if (!parentAsin.isEmpty()) break;
                    }
                    feedEntries.append({feedParentSku, parentAsin, true, {}, {}, {}});
                }

                QSet<QString> addedChildSkus;
                for (const auto &t : targets) {
                    if (!t.needsParent) continue;
                    const auto &row = m_brokenChildTable->rows().at(t.rowIdx);
                    const QString childSku = asinToSku.value(row.asin);
                    if (childSku.isEmpty() || addedChildSkus.contains(childSku)) continue;
                    addedChildSkus.insert(childSku);
                    feedEntries.append({childSku, row.asin, false, feedParentSku, {}, {}});
                }

                // Fetch GTIN (EAN/UPC) for each entry — try three sources in order:
                //   1. Report cache (asinToGtin built during SKU resolution above)
                //   2. Catalog Items API identifiers (all EU + all NA marketplaces)
                //   3. Listings Items API attributes (seller-submitted, most reliable for EANs)
                // Falls back to ASIN if all sources fail (logged as warning).
                appendLog(tr("Fetching GTINs for feed entries…"));
                for (auto &entry : feedEntries) {
                    if (entry.asin.isEmpty()) continue;

                    appendLog(tr("  [%1 / %2]").arg(entry.asin, entry.sku));

                    // Source 1: report cache (built during SKU resolution above)
                    if (asinToGtin.contains(entry.asin)) {
                        const auto &p = asinToGtin.value(entry.asin);
                        entry.gtin     = p.first;
                        entry.gtinType = p.second;
                        appendLog(tr("    → %1 %2 (report cache)")
                                      .arg(entry.gtinType, entry.gtin));
                        continue;
                    }
                    appendLog(tr("    source 1 (report cache): not in cache"));

                    // Source 2: Catalog Items API identifiers (all EU + all NA in two calls)
                    co_await m_api->fetchAsinGtin(entry.asin, primaryMpId,
                                                  &entry.gtin, &entry.gtinType);
                    if (!entry.gtin.isEmpty()) {
                        appendLog(tr("    → %1 %2 (catalog identifiers)")
                                      .arg(entry.gtinType, entry.gtin));
                        continue;
                    }
                    appendLog(tr("    source 2 (catalog identifiers): not found"));

                    // Source 3: Listings Items API attributes by SKU — reads seller-submitted EAN
                    if (!entry.sku.isEmpty()) {
                        QString diagLog;
                        co_await m_api->fetchListingGtin(primaryMpId, entry.sku,
                                                         &entry.gtin, &entry.gtinType,
                                                         &diagLog);
                        appendLog(tr("    source 3 (listing attributes): %1").arg(
                            entry.gtin.isEmpty() ? diagLog : tr("%1 %2").arg(entry.gtinType, entry.gtin)));
                        if (!entry.gtin.isEmpty()) continue;
                    }

                    appendLog(tr("    ⚠ all sources exhausted — using ASIN as fallback"));
                }

                QStringList brokenMpIds;
                for (const auto &t : targets) {
                    if (!t.needsParent) continue;
                    const QString mpId = m_brokenChildTable->marketplaceAt(t.mktIdx).id;
                    if (!brokenMpIds.contains(mpId)) brokenMpIds << mpId;
                }

                // Collect the complete per-child data set — the same data the
                // manual flat file carries. Used for BOTH the template file and
                // the full-fidelity per-marketplace feeds below.
                const QString tplPath = ui->lineEditBrokenTemplate->text().trimmed();
                {
                    const QString attrMpId = _brokenAttrMarketplaceId().isEmpty()
                        ? (brokenMpIds.isEmpty() ? primaryMpId : brokenMpIds.first())
                        : _brokenAttrMarketplaceId();
                    appendLog(tr("Fetching parent attributes (%1) for template…")
                                  .arg(ui->comboBoxBrokenAttrMarket->currentText()));
                    QJsonObject parentAttrs;
                    co_await m_api->fetchListingAttributes(attrMpId, feedParentSku, &parentAttrs);

                    // Fallback color/size/sizeSource from BrokenChildTable (may be from any marketplace).
                    struct ColorSizeSrc { QString color; QString size; QString sizeSource; };
                    QHash<QString, ColorSizeSrc> fallbackByAsin;
                    for (const auto &r : m_brokenChildTable->rows())
                        fallbackByAsin.insert(r.asin, {r.color, r.size, r.sizeSource});

                    auto childFirstVal = [](const QJsonObject &attrs, const QString &key) -> QString {
                        const QJsonArray arr = attrs.value(key).toArray();
                        if (arr.isEmpty()) return {};
                        const QJsonObject obj = arr.first().toObject();
                        const QString v = obj.value(QStringLiteral("value")).toString();
                        return v.isEmpty() ? obj.value(QStringLiteral("name")).toString() : v;
                    };

                    QList<VariationTemplateEntry> tplEntries;
                    for (const auto &e : feedEntries) {
                        const auto fb = fallbackByAsin.value(e.asin);
                        // Size fallback from BrokenChildTable is acceptable (with conversion).
                        // Color fallback is NOT: BrokenChildTable always stores English (Step 3c).
                        QString color;
                        QString size    = fb.size;
                        QString sizeSrc = fb.sizeSource;
                        QString sizeSystem, sizeClass, gender, ageRange, bodyType, heightType;
                        if (!e.asin.isEmpty() && !e.isParent) {
                            // 1. Listings Items API (by SKU) — seller-submitted, localized.
                            QJsonObject childListingAttrs;
                            if (!e.sku.isEmpty())
                                co_await m_api->fetchListingAttributes(attrMpId, e.sku, &childListingAttrs);
                            const QString lc   = childFirstVal(childListingAttrs, QStringLiteral("color_name"));
                            const QString ls   = childFirstVal(childListingAttrs, QStringLiteral("apparel_size"));
                            const QString lss  = childFirstVal(childListingAttrs, QStringLiteral("apparel_size_system"));
                            const QString lsc  = childFirstVal(childListingAttrs, QStringLiteral("apparel_size_class"));
                            const QString lg   = childFirstVal(childListingAttrs, QStringLiteral("target_gender"));
                            const QString la   = childFirstVal(childListingAttrs, QStringLiteral("age_range_description"));
                            const QString lbt  = childFirstVal(childListingAttrs, QStringLiteral("apparel_body_type"));
                            const QString lht  = childFirstVal(childListingAttrs, QStringLiteral("apparel_height_type"));

                            // 2. Catalog Items API (by ASIN) — fallback for any field missing from Listings API.
                            AmazonCatalogApi::CatalogApparelAttrs cat;
                            if (lc.isEmpty() || lss.isEmpty() || lsc.isEmpty()
                                    || lg.isEmpty() || la.isEmpty() || lbt.isEmpty() || lht.isEmpty())
                                co_await m_api->fetchCatalogApparelAttrs(attrMpId, e.asin, &cat);

                            color      = lc.isEmpty()  ? cat.color      : lc;
                            sizeSystem = lss.isEmpty() ? cat.sizeSystem  : lss;
                            sizeClass  = lsc.isEmpty() ? cat.sizeClass   : lsc;
                            gender     = lg.isEmpty()  ? cat.gender      : lg;
                            ageRange   = la.isEmpty()  ? cat.ageRange    : la;
                            bodyType   = lbt.isEmpty() ? cat.bodyType    : lbt;
                            heightType = lht.isEmpty() ? cat.heightType  : lht;
                            if (!ls.isEmpty()) { size = ls; sizeSrc = {}; }

                            appendLog(tr("  [tpl] %1 (%2): color=%3 size=%4 sizeSystem=%5 gender=%6 age=%7")
                                          .arg(e.asin, e.sku,
                                               color.isEmpty()      ? QStringLiteral("(empty)") : color,
                                               size.isEmpty()       ? QStringLiteral("(empty)") : size,
                                               sizeSystem.isEmpty() ? QStringLiteral("(empty)") : sizeSystem,
                                               gender.isEmpty()     ? QStringLiteral("(empty)") : gender,
                                               ageRange.isEmpty()   ? QStringLiteral("(empty)") : ageRange));
                        }
                        tplEntries.append({e.sku, e.asin, e.isParent,
                                           e.gtin, e.gtinType,
                                           color, size, sizeSrc,
                                           sizeSystem, sizeClass, gender, ageRange, bodyType, heightType});
                    }

                    // Detect which apparel attrs are still empty across ALL children + parent.
                    QHash<QString,QString> attrOverrides;
                    QHash<QString,QString> familyAttrFallback;
                    {
                        QString fbSys, fbCls, fbGen, fbAge, fbBody, fbHt;
                        for (const auto &e : tplEntries) {
                            if (e.isParent) continue;
                            if (fbSys.isEmpty()  && !e.sizeSystem.isEmpty()) fbSys  = e.sizeSystem;
                            if (fbCls.isEmpty()  && !e.sizeClass.isEmpty())  fbCls  = e.sizeClass;
                            if (fbGen.isEmpty()  && !e.gender.isEmpty())     fbGen  = e.gender;
                            if (fbAge.isEmpty()  && !e.ageRange.isEmpty())   fbAge  = e.ageRange;
                            if (fbBody.isEmpty() && !e.bodyType.isEmpty())   fbBody = e.bodyType;
                            if (fbHt.isEmpty()   && !e.heightType.isEmpty()) fbHt   = e.heightType;
                        }
                        // Also fall back to parent listing attrs (same JSON format).
                        auto tryParent = [&](const QString &key, QString &fb) {
                            if (!fb.isEmpty()) return;
                            fb = childFirstVal(parentAttrs, key);
                        };
                        tryParent(QStringLiteral("apparel_size_system"),   fbSys);
                        tryParent(QStringLiteral("apparel_size_class"),    fbCls);
                        tryParent(QStringLiteral("target_gender"),         fbGen);
                        tryParent(QStringLiteral("age_range_description"), fbAge);
                        tryParent(QStringLiteral("apparel_body_type"),     fbBody);
                        tryParent(QStringLiteral("apparel_height_type"),   fbHt);

                        QStringList missing;
                        if (fbSys.isEmpty())  missing << QStringLiteral("apparel_size_system");
                        if (fbCls.isEmpty())  missing << QStringLiteral("apparel_size_class");
                        if (fbGen.isEmpty())  missing << QStringLiteral("target_gender");
                        if (fbAge.isEmpty())  missing << QStringLiteral("age_range_description");
                        if (fbBody.isEmpty()) missing << QStringLiteral("apparel_body_type");
                        if (fbHt.isEmpty())   missing << QStringLiteral("apparel_height_type");

                        if (!missing.isEmpty()) {
                            appendLog(tr("⚠ Apparel attrs still empty after all API sources: %1").arg(missing.join(QStringLiteral(", "))));

                            // Step 1: catalog keyword search on the selected marketplace.
                            const QString itemName = childFirstVal(parentAttrs, QStringLiteral("item_name"));
                            const QStringList nameWords = itemName.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                            const QString searchKw = QStringList(nameWords.mid(0, 3)).join(QLatin1Char(' '));

                            QHash<QString,QString> candidate;
                            QString candidateAsin;
                            if (!searchKw.isEmpty()) {
                                appendLog(tr("  Searching catalog for similar product (\"%1\")…").arg(searchKw));
                                AmazonCatalogApi::CatalogApparelAttrs searchCat;
                                co_await m_api->searchCatalogForApparelAttrs(
                                    attrMpId, searchKw, missing, &searchCat, &candidateAsin);
                                if (!candidateAsin.isEmpty()) {
                                    if (missing.contains(QStringLiteral("apparel_size_system"))   && !searchCat.sizeSystem.isEmpty()) candidate[QStringLiteral("apparel_size_system")]   = searchCat.sizeSystem;
                                    if (missing.contains(QStringLiteral("apparel_size_class"))    && !searchCat.sizeClass.isEmpty())  candidate[QStringLiteral("apparel_size_class")]    = searchCat.sizeClass;
                                    if (missing.contains(QStringLiteral("target_gender"))         && !searchCat.gender.isEmpty())     candidate[QStringLiteral("target_gender")]         = searchCat.gender;
                                    if (missing.contains(QStringLiteral("age_range_description")) && !searchCat.ageRange.isEmpty())   candidate[QStringLiteral("age_range_description")] = searchCat.ageRange;
                                    if (missing.contains(QStringLiteral("apparel_body_type"))     && !searchCat.bodyType.isEmpty())   candidate[QStringLiteral("apparel_body_type")]     = searchCat.bodyType;
                                    if (missing.contains(QStringLiteral("apparel_height_type"))   && !searchCat.heightType.isEmpty()) candidate[QStringLiteral("apparel_height_type")]   = searchCat.heightType;
                                    appendLog(tr("  Found candidate ASIN %1").arg(candidateAsin));
                                }
                            }

                            // Step 2: UK LISTINGS API fallback (by child SKU).
                            // apparel_size_class / apparel_body_type / apparel_height_type are
                            // Listings-API-only fields — the Catalog API never exposes them, so
                            // reading the working UK listing directly is the only automated source.
                            // The children already exist on UK (the working marketplace), so their
                            // seller-submitted values are authoritative.
                            static const QString kUkMpId = QStringLiteral("A1F83G8C2ARO7P");
                            const bool stillMissing = std::any_of(
                                missing.cbegin(), missing.cend(),
                                [&](const QString &f){ return candidate.value(f).isEmpty(); });
                            if (stillMissing && kUkMpId != attrMpId) {
                                for (const auto &e : tplEntries) {
                                    if (e.isParent || e.sku.isEmpty()) continue;
                                    QJsonObject ukAttrs;
                                    co_await m_api->fetchListingAttributes(kUkMpId, e.sku, &ukAttrs);
                                    if (ukAttrs.isEmpty()) continue;
                                    for (const QString &f : missing) {
                                        if (!candidate.value(f).isEmpty()) continue;
                                        const QString v = childFirstVal(ukAttrs, f);
                                        if (!v.isEmpty()) {
                                            candidate[f] = v;
                                            candidateAsin.clear(); // internal — no ASIN to display
                                        }
                                    }
                                    const bool allFound = std::none_of(
                                        missing.cbegin(), missing.cend(),
                                        [&](const QString &f){ return candidate.value(f).isEmpty(); });
                                    if (allFound) break;
                                }
                                appendLog(tr("  UK listing fallback filled: %1")
                                              .arg(QStringList(candidate.keys()).join(QStringLiteral(", "))));
                            }

                            // Load cached values from the last run for this product type + marketplace.
                            // Cache wins over "not found" but loses to actual API values.
                            const QString attrCacheFile = m_workingDir.filePath(
                                QStringLiteral("apparel_attrs_cache.ini"));
                            QSettings attrCache(attrCacheFile, QSettings::IniFormat);
                            const QString productTypeKey = m_productType.isEmpty()
                                ? QStringLiteral("UNKNOWN") : m_productType;
                            attrCache.beginGroup(productTypeKey);
                            attrCache.beginGroup(attrMpId);
                            for (const QString &f : missing) {
                                if (candidate.value(f).isEmpty()) {
                                    const QString cached = attrCache.value(f).toString();
                                    if (!cached.isEmpty())
                                        candidate[f] = cached;
                                }
                            }
                            attrCache.endGroup();
                            attrCache.endGroup();

                            // No dialog. Apply whatever the automated sources found and
                            // move on. Descriptive apparel attributes are OPTIONAL for a
                            // relationship fix — the children already carry them on the
                            // broken marketplaces (they exist, just unlinked). Anything
                            // still empty is logged; if Amazon actually requires it, the
                            // checkListing post-check reports the exact field per SKU.
                            attrCache.beginGroup(productTypeKey);
                            attrCache.beginGroup(attrMpId);
                            for (const QString &f : missing) {
                                const QString v = candidate.value(f);
                                if (!v.isEmpty()) {
                                    attrOverrides.insert(f, v);
                                    attrCache.setValue(f, v); // persist for next run
                                }
                            }
                            attrCache.endGroup();
                            attrCache.endGroup();
                            attrCache.sync();

                            if (!attrOverrides.isEmpty())
                                appendLog(tr("  Apparel attrs auto-resolved: %1").arg(
                                    QStringList(attrOverrides.keys()).join(QStringLiteral(", "))));
                            QStringList unresolved;
                            for (const QString &f : missing)
                                if (candidate.value(f).isEmpty()) unresolved << f;
                            if (!unresolved.isEmpty())
                                appendLog(tr("  Left empty (optional; check post-fix issues if rejected): %1")
                                              .arg(unresolved.join(QStringLiteral(", "))));
                        }

                        // Family-level fallback chain shared by the template and
                        // the feeds: first-found among children → parent listing
                        // → user dialog value.
                        auto famFb = [&](const QString &key, const QString &fb) {
                            if (!fb.isEmpty()) return fb;
                            const QString pv = childFirstVal(parentAttrs, key);
                            if (!pv.isEmpty()) return pv;
                            return attrOverrides.value(key);
                        };
                        familyAttrFallback.insert(QStringLiteral("apparel_size_system"),   famFb(QStringLiteral("apparel_size_system"),   fbSys));
                        familyAttrFallback.insert(QStringLiteral("apparel_size_class"),    famFb(QStringLiteral("apparel_size_class"),    fbCls));
                        familyAttrFallback.insert(QStringLiteral("target_gender"),         famFb(QStringLiteral("target_gender"),         fbGen));
                        familyAttrFallback.insert(QStringLiteral("age_range_description"), famFb(QStringLiteral("age_range_description"), fbAge));
                        familyAttrFallback.insert(QStringLiteral("apparel_body_type"),     famFb(QStringLiteral("apparel_body_type"),     fbBody));
                        familyAttrFallback.insert(QStringLiteral("apparel_height_type"),   famFb(QStringLiteral("apparel_height_type"),   fbHt));
                    }

                    if (!tplPath.isEmpty() && m_productWorkingDir.exists()) {
                        const QString filledPath = _fillVariationTemplate(
                            tplPath, feedParentSku, parentAttrs, attrMpId,
                            m_productType, variationTheme, tplEntries, attrOverrides);
                        if (!filledPath.isEmpty()) {
                            appendLog(tr("Template filled: %1").arg(filledPath));
                            if (copyPathBtnPtr) {
                                copyPathBtnPtr->setEnabled(true);
                                connect(copyPathBtnPtr, &QPushButton::clicked,
                                        progressDlg, [filledPath]() {
                                    QGuiApplication::clipboard()->setText(filledPath);
                                });
                            }
                        } else {
                            appendLog(tr("⚠ Template filling failed — check file path and sheet structure."));
                        }
                    }

                    // ── Full-fidelity JSON_LISTINGS_FEED, one per marketplace ──
                    // Mirrors the manual flat file: complete parent row + complete
                    // child rows, localized per country. checkListing before/after
                    // exposes Amazon's async validation errors (the reason earlier
                    // ACCEPTED submissions silently never materialized).
                    for (const QString &feedMpId : brokenMpIds) {
                        QString feedMpCode = feedMpId;
                        for (int i = 0; i < m_brokenChildTable->marketplaceCount(); ++i) {
                            const auto &s = m_brokenChildTable->marketplaceAt(i);
                            if (s.id == feedMpId) { feedMpCode = s.code; break; }
                        }
                        appendLog(tr("═══ %1 — full variation feed ═══").arg(feedMpCode));

                        // Pre-check: does the parent listing even exist here?
                        {
                            AmazonCatalogApi::ListingCheck pc;
                            co_await m_api->checkListing(feedMpId, feedParentSku, &pc);
                            if (!pc.exists) {
                                appendLog(tr("  parent %1: NOT LISTED on %2 — feed will create it")
                                              .arg(feedParentSku, feedMpCode));
                            } else {
                                appendLog(tr("  parent %1: status=[%2] linked children=%3 theme=%4")
                                              .arg(feedParentSku,
                                                   pc.status.isEmpty() ? tr("none") : pc.status)
                                              .arg(pc.childSkus.size())
                                              .arg(pc.variationTheme.isEmpty() ? tr("(none)") : pc.variationTheme));
                                for (const QString &iss : pc.issues)
                                    appendLog(QStringLiteral("    ") + iss);
                            }
                        }

                        QJsonArray feedMessages;
                        QStringList buildLog;
                        co_await _buildFullVariationMessages(
                            feedMpId, feedMpCode, feedParentSku, m_productType,
                            variationTheme, parentAttrs, tplEntries,
                            familyAttrFallback, &feedMessages, &buildLog);
                        for (const QString &l : buildLog)
                            appendLog(QStringLiteral("  ") + l);

                        QString feedResult;
                        const QStringList feedMpList{feedMpId};
                        co_await m_api->submitJsonListingsFeed(
                            feedMpList, feedMessages, &feedResult);
                        appendLog(tr("  Feed result: %1").arg(feedResult));
                        _appendFixLog(feedParentSku, feedMpCode,
                                      QStringLiteral("full feed: ") + feedResult);
                        fullFeedSubmitted = true;

                        // Post-check: per-child validation issues + relationship
                        // state. Async validation may lag — issues shown here are
                        // authoritative, missing relationships may still appear.
                        for (const auto &e : tplEntries) {
                            if (e.isParent || e.sku.isEmpty()) continue;
                            AmazonCatalogApi::ListingCheck cc;
                            co_await m_api->checkListing(feedMpId, e.sku, &cc);
                            if (!cc.exists) {
                                appendLog(tr("  %1: NOT LISTED").arg(e.sku));
                                continue;
                            }
                            const bool linked = (cc.parentSku == feedParentSku);
                            appendLog(tr("  %1: parent=%2 status=[%3]%4")
                                          .arg(e.sku,
                                               cc.parentSku.isEmpty() ? tr("(none)") : cc.parentSku,
                                               cc.status,
                                               linked ? QStringLiteral(" ✓") : QString()));
                            for (const QString &iss : cc.issues)
                                appendLog(QStringLiteral("    ") + iss);
                        }
                    }
                }

                /* Flat file generation — disabled pending Option B (official template).
                if (m_productWorkingDir.exists()) {
                    QHash<QString, QPair<QString,QString>> asinColorSize;
                    for (const auto &row : m_brokenChildTable->rows())
                        asinColorSize.insert(row.asin, {row.color, row.size});
                    QList<FlatFileChildEntry> children;
                    for (const auto &e : feedEntries) {
                        if (e.isParent) continue;
                        const auto cs = asinColorSize.value(e.asin);
                        children.append({e.sku, cs.first, cs.second});
                    }
                    appendLog(tr("Generating flat file(s) (%1 child(ren))…").arg(children.size()));
                    for (const QString &mpId : brokenMpIds) {
                        QString mpCode = mpId;
                        for (int i = 0; i < m_brokenChildTable->marketplaceCount(); ++i) {
                            const auto &s = m_brokenChildTable->marketplaceAt(i);
                            if (s.id == mpId) { mpCode = s.code; break; }
                        }
                        QJsonObject parentAttrs;
                        co_await m_api->fetchListingAttributes(mpId, feedParentSku, &parentAttrs);
                        _generateParentFlatFile(mpCode, feedParentSku, parentAttrs,
                                                m_productType, variationTheme, children);
                        const QString fileName = QStringLiteral("flatfile_parent_%1_%2.txt")
                            .arg(mpCode.toLower(),
                                 QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
                        appendLog(tr("  %1: %2").arg(mpCode, m_productWorkingDir.filePath(fileName)));
                    }
                }
                */
            } else {
                appendLog(tr("Feed upload skipped: parent SKU not resolved."));
            }
        }
    }

    // ── 6. Process each target sequentially ─────────────────────────────────
    int done = 0;
    for (const auto &target : targets) {
        if (!dlgPtr) co_return; // user closed dialog (shouldn't happen — disabled)

        const auto &row = m_brokenChildTable->rows().at(target.rowIdx);
        const auto &spec = m_brokenChildTable->marketplaceAt(target.mktIdx);
        const QString mpId   = spec.id;
        const QString mpCode = spec.code;
        const QString childSku = asinToSku.value(row.asin);

        if (statusLabelPtr) {
            statusLabelPtr->setText(tr("[%1/%2] %3 in %4")
                                        .arg(done + 1)
                                        .arg(targets.size())
                                        .arg(row.asin, mpCode));
        }

        if (childSku.isEmpty()) {
            appendLog(tr("[%1] %2: no SKU — skipping").arg(mpCode, row.asin));
            ++done;
            if (progressBarPtr) progressBarPtr->setValue(done);
            continue;
        }

        // 6a. Parent fix via direct Listings Items API PATCH (per child × marketplace).
        // Skipped when the full feed already covered this — see 5g.
        if (target.needsParent && fixParents && fullFeedSubmitted) {
            appendLog(tr("[%1] %2: parent fix covered by full feed — skipping direct patch")
                          .arg(mpCode, row.asin));
        } else if (target.needsParent && fixParents) {
            const QString parentSku = asinToSku.value(row.parentAsin);
            if (parentSku.isEmpty()) {
                appendLog(tr("[%1] %2: no parent SKU — skipping direct patch").arg(mpCode, row.asin));
            } else {
                QString parentDetails;
                co_await m_api->patchListingAsParent(mpId, parentSku, m_productType,
                                                     variationTheme, &parentDetails);
                appendLog(tr("[%1] parent %2: %3").arg(mpCode, parentSku, parentDetails));

                // Fetch this child's own attributes on this marketplace to get
                // localized color/size/sizeSystem values (may differ per country).
                QString localColor;
                QString localSize       = row.size;
                QString localSizeSystem;
                QString localSizeClass, localGender, localAgeRange, localBodyType, localHeightType;
                QJsonObject childAttrs;
                co_await m_api->fetchListingAttributes(mpId, childSku, &childAttrs);
                if (!childAttrs.isEmpty()) {
                    auto firstVal = [&](const QString &key) {
                        const QJsonArray arr = childAttrs.value(key).toArray();
                        if (arr.isEmpty()) return QString{};
                        const QJsonObject obj = arr.first().toObject();
                        const QString v = obj.value(QStringLiteral("value")).toString();
                        return v.isEmpty() ? obj.value(QStringLiteral("name")).toString() : v;
                    };
                    localColor      = firstVal(QStringLiteral("color_name"));
                    localSizeSystem = firstVal(QStringLiteral("apparel_size_system"));
                    localSizeClass  = firstVal(QStringLiteral("apparel_size_class"));
                    localGender     = firstVal(QStringLiteral("target_gender"));
                    localAgeRange   = firstVal(QStringLiteral("age_range_description"));
                    localBodyType   = firstVal(QStringLiteral("apparel_body_type"));
                    localHeightType = firstVal(QStringLiteral("apparel_height_type"));
                    const QString s  = firstVal(QStringLiteral("apparel_size"));
                    if (!s.isEmpty()) {
                        localSize = s;
                    } else if (!localSize.isEmpty() && !row.sizeSource.isEmpty()
                               && row.sizeSource != mpCode) {
                        localSize = FillerSize::convertSize(localSize, row.sizeSource, mpCode);
                    }
                }
                // Catalog Items API fallback for any field still empty.
                if (localColor.isEmpty() || localSizeSystem.isEmpty()
                        || localSizeClass.isEmpty() || localGender.isEmpty()
                        || localAgeRange.isEmpty() || localBodyType.isEmpty() || localHeightType.isEmpty()) {
                    AmazonCatalogApi::CatalogApparelAttrs cat;
                    co_await m_api->fetchCatalogApparelAttrs(mpId, row.asin, &cat);
                    if (localColor.isEmpty())      localColor      = cat.color;
                    if (localSizeSystem.isEmpty()) localSizeSystem = cat.sizeSystem;
                    if (localSizeClass.isEmpty())  localSizeClass  = cat.sizeClass;
                    if (localGender.isEmpty())     localGender     = cat.gender;
                    if (localAgeRange.isEmpty())   localAgeRange   = cat.ageRange;
                    if (localBodyType.isEmpty())   localBodyType   = cat.bodyType;
                    if (localHeightType.isEmpty()) localHeightType = cat.heightType;
                }
                // UK catalog fallback for attrs still empty (same ASIN, different marketplace).
                static const QString kUkMpId6a = QStringLiteral("A1F83G8C2ARO7P");
                if (kUkMpId6a != mpId && (localSizeClass.isEmpty() || localGender.isEmpty()
                        || localAgeRange.isEmpty() || localBodyType.isEmpty() || localHeightType.isEmpty())) {
                    AmazonCatalogApi::CatalogApparelAttrs ukCat;
                    co_await m_api->fetchCatalogApparelAttrs(kUkMpId6a, row.asin, &ukCat);
                    if (localSizeClass.isEmpty())  localSizeClass  = ukCat.sizeClass;
                    if (localGender.isEmpty())     localGender     = ukCat.gender;
                    if (localAgeRange.isEmpty())   localAgeRange   = ukCat.ageRange;
                    if (localBodyType.isEmpty())   localBodyType   = ukCat.bodyType;
                    if (localHeightType.isEmpty()) localHeightType = ukCat.heightType;
                }
                QHash<QString,QString> extraAttrs;
                if (!localSizeClass.isEmpty())  extraAttrs[QStringLiteral("apparel_size_class")]    = localSizeClass;
                if (!localGender.isEmpty())     extraAttrs[QStringLiteral("target_gender")]         = localGender;
                if (!localAgeRange.isEmpty())   extraAttrs[QStringLiteral("age_range_description")] = localAgeRange;
                if (!localBodyType.isEmpty())   extraAttrs[QStringLiteral("apparel_body_type")]     = localBodyType;
                if (!localHeightType.isEmpty()) extraAttrs[QStringLiteral("apparel_height_type")]   = localHeightType;

                appendLog(tr("[%1] %2 color=%3 size=%4 sizeSystem=%5 gender=%6 sizeClass=%7")
                              .arg(mpCode, row.asin, localColor, localSize, localSizeSystem,
                                   localGender, localSizeClass));

                bool ok = false;
                QString childDetails;
                co_await m_api->patchListingParent(mpId, childSku, m_productType,
                                                   parentSku, variationTheme,
                                                   localColor, localSize, localSizeSystem,
                                                   extraAttrs, &ok, &childDetails);
                appendLog(tr("[%1] %2 → parent: %3").arg(mpCode, row.asin, childDetails));
            }
        }

        // 6b. Image fix
        if (target.needsImages && fixImages) {
            const QString sourceAsin =
                m_brokenChildTable->bestImageSourceAsin(row.color.toLower(), target.mktIdx);
            if (sourceAsin.isEmpty() || sourceAsin == row.asin) {
                appendLog(tr("[%1] %2: no better image source — skipping image fix")
                              .arg(mpCode, row.asin));
            } else {
                QStringList imageUrls;
                co_await m_api->fetchItemImages(sourceAsin, mpId, &imageUrls);
                if (imageUrls.isEmpty()) {
                    appendLog(tr("[%1] %2: could not fetch images from %3 (%4)")
                                  .arg(mpCode, row.asin, sourceAsin, m_api->lastError()));
                } else {
                    bool ok = false;
                    co_await m_api->patchListingImageUrls(mpId, childSku, m_productType,
                                                          imageUrls, &ok);
                    if (ok) {
                        appendLog(tr("[%1] %2: images fixed (%3 slot(s) from %4)")
                                      .arg(mpCode, row.asin).arg(imageUrls.size())
                                      .arg(sourceAsin));
                    } else {
                        appendLog(tr("[%1] %2: image fix failed: %3")
                                      .arg(mpCode, row.asin, m_api->lastError()));
                    }
                }
            }
        }

        ++done;
        if (progressBarPtr) progressBarPtr->setValue(done);
    }

    // Refresh is omitted — Amazon propagates relationship changes over hours,
    // so an immediate re-check would show the old state. Use "Re-run" manually
    // after waiting (typically 1–24 h).
    appendLog(tr("Done. Press Re-run after Amazon has had time to propagate the changes."));
    if (statusLabelPtr) statusLabelPtr->setText(tr("Done."));
    if (closeBtnPtr)    closeBtnPtr->setEnabled(true);
    co_return;
}

void PaneSizing::onEditPromptsClicked()
{
    APlusWorkflow *wf = _currentWorkflow();
    if (!wf) return;

    DialogEditPrompts dlg(wf, this);
    dlg.exec();
}
