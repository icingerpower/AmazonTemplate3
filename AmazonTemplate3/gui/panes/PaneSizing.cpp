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

    m_imageNam = new QNetworkAccessManager(this);

    connect(ui->buttonGenerateFAQ, &QPushButton::clicked,
            this, &PaneSizing::onGenerateFaqClicked);

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

void PaneSizing::_downloadVariantImages(const QStringList &imageUrls)
{
    if (!m_productWorkingDir.exists() || imageUrls.isEmpty())
        return;

    ui->listWidgetImages->clear();
    m_variantImagePaths.clear();

    const QString dir = m_productWorkingDir.absolutePath();
    int index = 1;
    for (const QString &url : imageUrls) {
        const QString filename = QStringLiteral("image-%1.jpg")
            .arg(index, 2, 10, QLatin1Char('0'));
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

void PaneSizing::onGenerateFaqClicked()
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

    QString prompt;
    prompt += QStringLiteral("Product description:\n") + description + QStringLiteral("\n\n");
    if (!userPrompt.isEmpty())
        prompt += QStringLiteral("Instructions:\n") + userPrompt + QStringLiteral("\n\n");
    prompt += QStringLiteral("[DEBUG] Before anything else, output exactly one token on its own line: "
                             "IMGSUCCESS if you can see a product photo in this message, "
                             "IMGFAILURE if no photo is visible to you.\n\n");
    prompt += QStringLiteral("Generate a concise, engaging Amazon A+ Content FAQ section for this product. "
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
    const QDir &effectiveDir = m_productWorkingDir.exists() ? m_productWorkingDir : m_workingDir;
    const QString workDir = effectiveDir.isAbsolute() ? effectiveDir.path() : QString{};

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

    // If an image is available and the CLI is Claude, use stream-json to embed it.
    if (!m_mainImageLocalPath.isEmpty()
            && cli->getExecutable() == QStringLiteral("claude")) {
        QFile imgFile(m_mainImageLocalPath);
        if (imgFile.open(QIODevice::ReadOnly)) {
            const QByteArray b64 = imgFile.readAll().toBase64();
            imgFile.close();

            // Build a single stream-json user message with image + text content blocks.
            QJsonObject imgSource;
            imgSource[QStringLiteral("type")]       = QStringLiteral("base64");
            imgSource[QStringLiteral("media_type")] = QStringLiteral("image/jpeg");
            imgSource[QStringLiteral("data")]       = QString::fromLatin1(b64);

            QJsonObject imgBlock;
            imgBlock[QStringLiteral("type")]   = QStringLiteral("image");
            imgBlock[QStringLiteral("source")] = imgSource;

            QJsonObject textBlock;
            textBlock[QStringLiteral("type")] = QStringLiteral("text");
            textBlock[QStringLiteral("text")] = finalPrompt;

            QJsonObject message;
            message[QStringLiteral("content")] = QJsonArray{imgBlock, textBlock};

            QJsonObject userMsg;
            userMsg[QStringLiteral("type")]    = QStringLiteral("user");
            userMsg[QStringLiteral("message")] = message;

            const QByteArray stdinData =
                QJsonDocument(userMsg).toJson(QJsonDocument::Compact) + '\n';

            const QStringList args = {
                QStringLiteral("-p"),
                QStringLiteral("--input-format"), QStringLiteral("stream-json"),
                QStringLiteral("--output-format"), QStringLiteral("text"),
                QStringLiteral("--dangerously-skip-permissions"),
            };

            _runCliPrompt(cli->getExecutable(), args, stdinData, workDir,
                          resultDlg, [output](QString text) {
                output->setPlainText(text.isEmpty() ? tr("(empty response)") : text);
            });
            return;
        }
    }

    // Fallback: text-only via normal CLI path.
    cli->runPromptAsync(finalPrompt, workDir, resultDlg, [output](CliRunResult result) {
        if (!result.processStarted) {
            output->setPlainText(QObject::tr("Failed to start CLI process."));
            return;
        }
        const QString text = result.output.trimmed();
        output->setPlainText(text.isEmpty() ? result.errorOutput.trimmed() : text);
    });
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
