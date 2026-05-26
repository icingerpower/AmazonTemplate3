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
#include <QPainter>
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
#include <QProgressBar>
#include <QFontDatabase>
#include <QGuiApplication>
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
#include <QSettings>
#include <QUrl>
#include <QButtonGroup>
#include <QTreeView>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QDateTime>
#include <QDir>
#include <QSet>

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

    connect(ui->listWidgetImages, &QListWidget::currentRowChanged,
            this, &PaneSizing::onVariantImageSelected);

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
    makePromptSaver(ui->textEditPrompt_01, QStringLiteral("aplusPromptDesktop"));
    makePromptSaver(ui->textEditPrompt_02, QStringLiteral("aplusPromptMobile"));
    makePromptSaver(ui->textEditFaqPrompt, QStringLiteral("aplusPromptFaq"));

    {
        auto s = WorkingDirectoryManager::instance()->settings();
        auto loadPrompt = [&](QTextEdit *ed, const QString &key, const QString &legacyKey = {}) {
            ed->blockSignals(true);
            QString val = s->value(key).toString();
            if (val.isEmpty() && !legacyKey.isEmpty())
                val = s->value(legacyKey).toString();
            ed->setPlainText(val);
            ed->blockSignals(false);
        };
        loadPrompt(ui->textEditPrompt_01, QStringLiteral("aplusPromptDesktop"),
                                          QStringLiteral("aplusPromptOneColor"));
        loadPrompt(ui->textEditPrompt_02, QStringLiteral("aplusPromptMobile"),
                                          QStringLiteral("aplusPromptMultipleColors"));
        loadPrompt(ui->textEditFaqPrompt, QStringLiteral("aplusPromptFaq"));
    }

    connect(ui->buttonCopyPrompt, &QPushButton::clicked, this, [this]() {
        QTextEdit *editor = (ui->tabWidgetPrompt_01->currentIndex() == 0)
                          ? ui->textEditPrompt_01
                          : ui->textEditPrompt_02;
        QGuiApplication::clipboard()->setText(editor->toPlainText());
    });

    m_imageNam = new QNetworkAccessManager(this);

    // --- A+ content wiring ---
    connect(ui->buttonAplusAddImageSlot, &QPushButton::clicked,
            this, &PaneSizing::onAplusAddImageSlot);
    connect(ui->buttonAplusDeleteVersion, &QPushButton::clicked,
            this, &PaneSizing::onAplusDeleteVersion);

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

static QString countryCodeToLanguage(const QString &code);
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

    QString mode, from, to;
    if (ui->radioButton_2->isChecked()) {
        mode = QStringLiteral("letters");
        from = ui->comboBoxLetterSizeFrom->currentText();
        to   = ui->comboBoxLetterSizeTo->currentText();
    } else if (ui->radioButton_3->isChecked()) {
        mode = QStringLiteral("height");
        from = ui->comboBoxHeightFrom->currentText();
        to   = ui->comboBoxHeightTo->currentText();
    } else {
        mode = QStringLiteral("numbers");
        from = ui->comboBoxSizeFrom->currentText();
        to   = ui->comboBoxSizeTo->currentText();
    }
    s.setValue(QStringLiteral("sizing/mode"), mode);
    s.setValue(QStringLiteral("sizing/from"), from);
    s.setValue(QStringLiteral("sizing/to"),   to);

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

    // Restore mode radio button.
    const QString mode = s.value(QStringLiteral("sizing/mode")).toString();
    if (mode == QStringLiteral("letters"))
        ui->radioButton_2->setChecked(true);
    else if (mode == QStringLiteral("height"))
        ui->radioButton_3->setChecked(true);
    else
        ui->radioButton->setChecked(true);

    // Restore from/to values.
    const QString from = s.value(QStringLiteral("sizing/from")).toString();
    const QString to   = s.value(QStringLiteral("sizing/to")).toString();
    if (mode == QStringLiteral("letters")) {
        ui->comboBoxLetterSizeFrom->setCurrentText(from);
        ui->comboBoxLetterSizeTo->setCurrentText(to);
    } else if (mode == QStringLiteral("height")) {
        ui->comboBoxHeightFrom->setCurrentText(from);
        ui->comboBoxHeightTo->setCurrentText(to);
    } else {
        ui->comboBoxSizeFrom->setCurrentText(from);
        ui->comboBoxSizeTo->setCurrentText(to);
    }

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
            });

    connect(m_treeModel.get(), &TreeSizingAsins::variantImagesFetched,
            this, &PaneSizing::_downloadVariantImages);


    connect(m_treeModel.get(), &TreeSizingAsins::loadError,
            this, [this](const QString& message) {
                QMessageBox::warning(this, tr("Amazon API error"), message);
            });

    connect(m_treeModel.get(), &TreeSizingAsins::attributesFetched,
            this, [this](const QStringList& bullets, const QStringList& materials,
                         const QString& mainImageUrl, const QString& asin,
                         const QString& title) {
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
                }

                if (!mainImageUrl.isEmpty() && !asin.isEmpty())
                    _downloadMainImage(mainImageUrl, asin);
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

    // A+ generate: enabled when a product dir is loaded
    const bool hasProduct = m_productWorkingDir.exists();
    ui->buttonAplusGenerate->setEnabled(hasProduct);
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

        m_generatedSuccessfully = true;

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

        // Silently translate size chart headers for each target language in background.
        // Each onDone (inside _buildSizeChartTranslationTasks) calls _refreshSizeGroupList()
        // so the list updates progressively as translations complete.
        if (m_sizeTableModel && m_aplusContent) {
            AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
            if (cli) {
                QList<QPair<QString,QString>> targetLangs;
                QSet<QString> seenLangs;
                for (int i = 0; i < ui->listWidgetCountries->count(); ++i) {
                    const QString code = ui->listWidgetCountries->item(i)->text().trimmed();
                    if (code.contains(QLatin1String("(missing)"))) continue;
                    const QString lang = countryCodeToLanguage(code);
                    if (lang.isEmpty() || seenLangs.contains(lang)) continue;
                    seenLangs.insert(lang);
                    targetLangs.append({code, lang});
                }
                if (!targetLangs.isEmpty()) {
                    QStringList origRowLabels;
                    for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
                        auto *it = m_sizeTableModel->item(row, 0);
                        origRowLabels << (it ? it->text() : QString{});
                    }
                    _runSequentially(_buildSizeChartTranslationTasks(targetLangs, origRowLabels));
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

void PaneSizing::onUploadSizeTableClicked()
{
    if (!m_generatedSuccessfully || !m_sizeTableModel)
        return;

    static const QHash<QString, QString> kCodeToMarketplace = {
        {"fr", "A13V1IB3VIYZZH"}, {"de", "A1PA6795UKMFR9"},
        {"it", "APJ6JRA9NG5V4"},  {"es", "A1RKKUPIHCS9HS"},
        {"uk", "A1F83G8C2ARO7P"}, {"nl", "A1805IZSGTT6HS"},
        {"se", "A2NODRKZP88ZB9"}, {"pl", "A1C3SOZRARQ6R3"},
        {"be", "AMEN7PMS3EDWL"},  {"ie", "A28R8C7NBKEWEA"},
        {"tr", "A33AVAJ2PDY3EV"}, {"us", "ATVPDKIKX0DER"},
        {"ca", "A2EUQ1WTGCTBG2"}, {"mx", "A1AM78C64UM0Y8"},
        {"jp", "A1VC38T7YXB528"},
    };

    QStringList availableCodes, marketplaceIds;
    for (int i = 0; i < ui->listWidgetCountries->count(); ++i) {
        const QString text = ui->listWidgetCountries->item(i)->text().trimmed();
        if (text.contains(QLatin1String("(missing)")))
            continue;
        const QString mpId = kCodeToMarketplace.value(text.toLower());
        if (!mpId.isEmpty()) {
            availableCodes << text.toUpper();
            marketplaceIds << mpId;
        }
    }

    if (marketplaceIds.isEmpty()) {
        QMessageBox::warning(this, tr("Upload"), tr("No available marketplaces found."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Upload Size Chart"));
    auto *layout = new QVBoxLayout(&dlg);

    layout->addWidget(new QLabel(
        tr("Marketplaces: %1").arg(availableCodes.join(QStringLiteral(", "))), &dlg));

    auto *ptLabel = new QLabel(tr("Product type (e.g. SHIRT, SHOES, PANTS):"), &dlg);
    auto *ptEdit  = new QLineEdit(&dlg);
    ptEdit->setPlaceholderText(QStringLiteral("SHIRT"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    layout->addWidget(ptLabel);
    layout->addWidget(ptEdit);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString productType = ptEdit->text().trimmed().toUpper();
    if (productType.isEmpty())
        return;

    _uploadSizeChart(marketplaceIds, productType);
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
            this, [this](const QString &) { m_aplusModel->rebuild(); });
    connect(m_aplusContent.get(), &APlusContent::layoutChanged,
            this, [this]() { m_aplusModel->rebuild(); });

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
    m_aplusModel->rebuild();

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
        m_aplusModel->rebuild();
    _refreshSizeGroupList();
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
    };
    return map.value(code.toLower().trimmed());
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

    const QString imgHint = m_mainImageLocalPath.isEmpty() ? QString{}
        : tr("A product photo is available in the working directory as \"%1\". "
             "You may use it as reference.\n\n")
          .arg(QFileInfo(m_mainImageLocalPath).fileName());

    const QString base = tr("Product:\n") + description + QStringLiteral("\n\n") + imgHint;
    const QString workDir = m_productWorkingDir.exists()
                          ? m_productWorkingDir.absolutePath() : QString{};

    const QString desktopInstructions = ui->textEditPrompt_01->toPlainText().trimmed();
    const QString mobileInstructions  = ui->textEditPrompt_02->toPlainText().trimmed();
    const QString faqInstructions     = ui->textEditFaqPrompt->toPlainText().trimmed();

    auto buildImagePrompt = [&](const QString &instructions, const QString &spec) -> QString {
        QString p = base;
        if (!instructions.isEmpty())
            p += tr("Instructions:\n") + instructions + QStringLiteral("\n\n");
        p += spec;
        return p;
    };

    QString desktopPrompt = buildImagePrompt(desktopInstructions,
        tr("Generate a professional Amazon A+ desktop marketing image "
           "(970x600 px, landscape). Save as desktop.png in the current directory."));
    QString mobilePrompt = buildImagePrompt(mobileInstructions,
        tr("Generate a professional Amazon A+ mobile marketing image "
           "(600x600 px, square). Save as mobile.png in the current directory."));
    QString faqPrompt = base;
    if (!faqInstructions.isEmpty())
        faqPrompt += tr("Instructions:\n") + faqInstructions + QStringLiteral("\n\n");
    faqPrompt += tr("Generate a concise, engaging Amazon A+ Content FAQ section for "
                    "this product in English. Output as a list of question/answer pairs in plain text.");

    // --- 3-tab prompt review dialog ---
    QDialog reviewDlg(this);
    reviewDlg.setWindowTitle(tr("Review prompts — %1").arg(cli->getName()));
    reviewDlg.resize(750, 520);
    auto *dlgLayout = new QVBoxLayout(&reviewDlg);
    auto *tabs = new QTabWidget(&reviewDlg);
    auto *desktopEdit = new QTextEdit(); desktopEdit->setPlainText(desktopPrompt);
    auto *mobileEdit  = new QTextEdit(); mobileEdit->setPlainText(mobilePrompt);
    auto *faqEdit     = new QTextEdit(); faqEdit->setPlainText(faqPrompt);
    tabs->addTab(desktopEdit, tr("Desktop image"));
    tabs->addTab(mobileEdit,  tr("Mobile image"));
    tabs->addTab(faqEdit,     tr("FAQ"));
    dlgLayout->addWidget(tabs);
    auto *btns = new QDialogButtonBox(&reviewDlg);
    btns->addButton(tr("Generate All"), QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &reviewDlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &reviewDlg, &QDialog::reject);
    dlgLayout->addWidget(btns);

    if (reviewDlg.exec() != QDialog::Accepted)
        return;

    const QString finalDesktop = desktopEdit->toPlainText();
    const QString finalMobile  = mobileEdit->toPlainText();
    const QString finalFaq     = faqEdit->toPlainText();

    // Collect unique non-English target languages from available countries
    QList<QPair<QString,QString>> targetLangs; // (countryCode, "French" / "German" / ...)
    {
        QSet<QString> seen;
        for (int i = 0; i < ui->listWidgetCountries->count(); ++i) {
            const QString code = ui->listWidgetCountries->item(i)->text().trimmed();
            if (code.contains(QStringLiteral("(missing)"))) continue;
            const QString lang = countryCodeToLanguage(code);
            if (lang.isEmpty() || seen.contains(lang)) continue;
            seen.insert(lang);
            targetLangs.append({code, lang});
        }
    }

    // --- Build sequential task list ---
    QList<CliTask> tasks;

    // Auto-create default image slots if none exist yet.
    {
        int imgCount = 0;
        for (const APlusElement &el : m_aplusContent->elements())
            if (el.type == APlusElementType::Image) ++imgCount;
        if (imgCount == 0) {
            for (int i = 0; i < 2; ++i)
                m_aplusContent->ensureImageElement(i);
            if (m_aplusModel) { m_aplusModel->rebuild(); _rebuildAplusMenu(); }
        }
    }

    // Accumulates absolute paths of every image file produced, for the assessment step.
    auto generatedImages = QSharedPointer<QStringList>::create();

    // One desktop + mobile task pair per image slot
    for (const APlusElement &el : m_aplusContent->elements()) {
        if (el.type != APlusElementType::Image) continue;

        const QString elemId = el.id;
        const QDir elemDir(m_aplusContent->dir().filePath(elemId));
        elemDir.mkpath(QStringLiteral("."));
        const QString elemWorkDir = elemDir.absolutePath();

        auto filePair  = QSharedPointer<QPair<QString,QString>>::create();
        auto beforeSnap = QSharedPointer<QStringList>::create();

        CliTask desktopTask;
        desktopTask.label   = tr("Desktop image — %1").arg(el.displayName);
        desktopTask.prompt  = finalDesktop;
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
        mobileTask.label   = tr("Mobile image — %1").arg(el.displayName);
        mobileTask.prompt  = finalMobile;
        mobileTask.workDir = elemWorkDir;
        mobileTask.onBefore = [beforeSnap, elemDir]() {
            *beforeSnap = elemDir.entryList({QStringLiteral("*.png"),
                QStringLiteral("*.jpg"), QStringLiteral("*.jpeg")}, QDir::Files);
        };
        mobileTask.onDone = [this, elemDir, beforeSnap, filePair, elemId, el,
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
            m_aplusContent->pushVersion(elemId, APlusElementType::Image, el.displayName, ver);
            if (m_aplusModel) m_aplusModel->rebuild();
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
            if (m_aplusModel) m_aplusModel->rebuild();
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
                if (m_aplusModel) m_aplusModel->rebuild();
            });
    }

    // Size chart translation tasks — one per target language
    if (m_sizeTableModel) {
        QStringList origRowLabels;
        for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
            auto *it = m_sizeTableModel->item(row, 0);
            origRowLabels << (it ? it->text() : QString{});
        }
        const auto chartTasks = _buildSizeChartTranslationTasks(targetLangs, origRowLabels);
        tasks.append(chartTasks);
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

    QSet<QString> seenLangs;
    for (int i = 0; i < ui->listWidgetCountries->count(); ++i) {
        const QString code = ui->listWidgetCountries->item(i)->text().trimmed();
        if (code.contains(QLatin1String("(missing)"))) continue;
        const QString lang = countryCodeToLanguage(code);
        if (lang.isEmpty() || seenLangs.contains(lang)) continue;
        seenLangs.insert(lang);
        const QString elemId = QStringLiteral("size_chart_") + code;
        addEntry(tr("Size Chart (%1)").arg(lang),
                 generatedImages.contains(elemId) ? generatedImages[elemId] : defaultImg);
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

QList<PaneSizing::CliTask> PaneSizing::_buildSizeChartTranslationTasks(
    const QList<QPair<QString, QString>> &targetLangs,
    const QStringList &origRowLabels)
{
    // origRowLabels = model->item(r, 0)->text() for r=0..rowCount-1
    // These are the visible row labels ("Size", "Chest (cm)", ...) that renderImage renders.
    auto origLabelsPtr = QSharedPointer<QStringList>::create(origRowLabels);
    const QString workDir = m_productWorkingDir.exists()
                          ? m_productWorkingDir.absolutePath() : QString{};

    QList<CliTask> tasks;
    for (const auto &[langCode, langName] : std::as_const(targetLangs)) {
        CliTask chartTask;
        chartTask.label   = tr("Size chart — %1").arg(langName);
        chartTask.workDir = workDir;
        const QString capturedLangCode = langCode;
        const QString capturedLangName = langName;
        chartTask.promptFn = [origLabelsPtr, capturedLangName]() -> QString {
            QString p = QStringLiteral("Translate the following Amazon size chart row labels to ")
                      + capturedLangName
                      + QStringLiteral(".\nReturn ONLY the translated labels, one per line, "
                                       "in the same order. No extra text.\n\n");
            for (const QString &h : std::as_const(*origLabelsPtr))
                p += h + QLatin1Char('\n');
            return p;
        };
        chartTask.onDone = [this, capturedLangCode, capturedLangName](CliRunResult r) {
            if (!m_aplusContent || !m_sizeTableModel) return;
            const auto *cat = _currentCategory();
            if (!cat) return;

            const QStringList lines = r.output.trimmed().split(
                QLatin1Char('\n'), Qt::SkipEmptyParts);
            if (lines.isEmpty()) return;

            // Temporarily swap row labels (column 0), strip inches cells, render, restore.
            // renderImage() reads model->item(r,0)->text() — not horizontalHeaderItem.
            // Non-English markets are metric: strip the " cm / xx in" portion so renderImage
            // does not emit an inches row.
            static const QString kCmSep = QStringLiteral(" cm / ");

            QStringList savedLabels;
            for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
                auto *it = m_sizeTableModel->item(row, 0);
                savedLabels << (it ? it->text() : QString{});
            }
            for (int row = 0; row < m_sizeTableModel->rowCount() && row < lines.size(); ++row) {
                if (auto *it = m_sizeTableModel->item(row, 0))
                    it->setText(lines[row].trimmed());
            }

            // Strip inches part from data cells so renderImage skips the inches row.
            QList<QPair<int,int>> inchCells;
            QStringList savedCellTexts;
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

            const QImage img = cat->renderImage(m_sizeTableModel);

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

            auto padToMinHeight = [](const QImage &src, int minH) -> QImage {
                if (src.height() >= minH) return src;
                QImage padded(src.width(), minH, QImage::Format_ARGB32);
                padded.fill(Qt::white);
                QPainter p(&padded);
                p.drawImage(0, (minH - src.height()) / 2, src);
                p.end();
                return padded;
            };

            const QString elemId = QStringLiteral("size_chart_") + capturedLangCode;
            const QDir aplusDir = m_aplusContent->dir();
            aplusDir.mkpath(elemId);
            QImage desktop = img.scaledToWidth(970, Qt::SmoothTransformation);
            QImage mobile  = img.scaledToWidth(600, Qt::SmoothTransformation);
            desktop = padToMinHeight(desktop, 400);
            mobile  = padToMinHeight(mobile,  400);
            const QString relD = elemId + QStringLiteral("/size_chart.png");
            const QString relM = elemId + QStringLiteral("/size_chart_mobile.png");
            desktop.save(aplusDir.filePath(relD));
            mobile.save(aplusDir.filePath(relM));

            APlusVersion ver;
            ver.generated   = QDateTime::currentDateTime();
            ver.desktopFile = relD;
            ver.mobileFile  = relM;
            m_aplusContent->setSingleVersion(elemId, APlusElementType::SizeChart,
                                             tr("Size Chart (%1)").arg(capturedLangName), ver);
            if (m_aplusModel) m_aplusModel->rebuild();
            _refreshSizeGroupList();
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

    // Collect unique target languages from the country list.
    QList<QPair<QString,QString>> targetLangs;
    {
        QSet<QString> seen;
        for (int i = 0; i < ui->listWidgetCountries->count(); ++i) {
            const QString code = ui->listWidgetCountries->item(i)->text().trimmed();
            if (code.contains(QLatin1String("(missing)"))) continue;
            const QString lang = countryCodeToLanguage(code);
            if (lang.isEmpty() || seen.contains(lang)) continue;
            seen.insert(lang);
            targetLangs.append({code, lang});
        }
    }
    if (targetLangs.isEmpty()) return;

    AbstractCli *cli = ui->comboBoxCli->currentData().value<AbstractCli *>();
    if (!cli) return;

    QStringList origRowLabels;
    for (int row = 0; row < m_sizeTableModel->rowCount(); ++row) {
        auto *it = m_sizeTableModel->item(row, 0);
        origRowLabels << (it ? it->text() : QString{});
    }

    QList<CliTask> tasks = _buildSizeChartTranslationTasks(targetLangs, origRowLabels);
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
        guard->m_aplusModel->rebuild();

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
                            if (guard->m_aplusModel) guard->m_aplusModel->rebuild();
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
                guard->m_aplusModel->rebuild();
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
    m_aplusModel->rebuild();
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
    m_aplusModel->rebuild();
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

QCoro::Task<void> PaneSizing::_uploadSizeChart(QStringList marketplaceIds, QString productType)
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
    const int totalAttempts = marketplaceIds.size() * skus.size();
    QStringList errors;

    for (const QString &marketplaceId : marketplaceIds) {
        for (const QString &sku : skus) {
            bool ok = false;
            co_await m_api->patchListingSizeChart(
                marketplaceId, sku, productType, headerCells, dataRows, &ok);
            if (ok)
                ++successCount;
            else
                errors << QStringLiteral("%1 / %2: %3").arg(marketplaceId, sku, m_api->lastError());
            m_api->clearLastError();
        }
    }

    if (errors.isEmpty()) {
        QMessageBox::information(this, tr("Upload"),
            tr("Size chart uploaded to %1 listing(s) across %2 marketplace(s).")
                .arg(successCount).arg(marketplaceIds.size()));
    } else {
        QMessageBox::warning(this, tr("Upload"),
            tr("Uploaded %1 of %2 listing(s).\n\nErrors:\n%3")
                .arg(successCount).arg(totalAttempts).arg(errors.join('\n')));
    }
    co_return;
}
