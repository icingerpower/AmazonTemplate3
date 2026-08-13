#include "DialogGenStorefrontImage.h"
#include "SettingsTable.h"
#include "AbstractCli.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QGuiApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSize>
#include <QSplitter>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>


namespace {
constexpr int kIconSizePx = 72;

// Whether this productType is normally shown worn/used by a model (clothing,
// footwear) as opposed to a flat product shot (rugs, carpets, bags, wallets…).
// Same keyword-matching style as PaneStore's isShoes/isWomen category checks,
// but matched per underscore-separated TOKEN, not substring — Amazon productType
// values are already whole tokens (e.g. "WOUND_DRESSING"), and a substring check
// would wrongly match "DRESS" inside "DRESSING".
bool categoryHasModel(const QString &category)
{
    const QString catUp = category.toUpper();

    // Accessories that share a token with real apparel/footwear but are never
    // modeled themselves (an insert INSIDE a shoe, not worn on a body).
    static const QStringList kExcluded = {
        QStringLiteral("SHOE_TREE"), QStringLiteral("SHOE_INSERT"),
    };
    if (kExcluded.contains(catUp)) return false;

    static const QStringList kModeledTokens = {
        QStringLiteral("DRESS"),      QStringLiteral("ROBE"),       QStringLiteral("SWIMWEAR"),
        QStringLiteral("KAFTAN"),     QStringLiteral("APPAREL"),    QStringLiteral("SHIRT"),
        QStringLiteral("SWEATER"),    QStringLiteral("COAT"),       QStringLiteral("SUIT"),
        QStringLiteral("UNDERWEAR"),  QStringLiteral("UNDERPANTS"), QStringLiteral("SHOE"),
        QStringLiteral("SHOES"),      QStringLiteral("BOOT"),       QStringLiteral("SANDAL"),
        QStringLiteral("FOOTWEAR"),   QStringLiteral("SKIRT"),      QStringLiteral("PANTS"),
        QStringLiteral("JACKET"),     QStringLiteral("COVERING"),   QStringLiteral("LINGERIE"),
        QStringLiteral("JUMPSUIT"),   QStringLiteral("LEGGING"),    QStringLiteral("NIGHTGOWN"),
        QStringLiteral("NIGHTSHIRT"), QStringLiteral("CORSET"),     QStringLiteral("OUTFIT"),
        QStringLiteral("HAT"),        QStringLiteral("LEOTARD"),    QStringLiteral("OVERALLS"),
        QStringLiteral("SCARF"),      QStringLiteral("SHORTS"),     QStringLiteral("TIGHTS"),
    };
    const QStringList tokens = catUp.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    for (const QString &token : tokens)
        if (kModeledTokens.contains(token)) return true;
    return false;
}
} // namespace

DialogGenStorefrontImage::DialogGenStorefrontImage(
    const QDir &workingDir,
    const QList<AmazonCatalogApi::StoreItem> &selectedItems,
    const QHash<QString, QPixmap> &asinToPixmap,
    AbstractCli *cli,
    const QStringList &nodePath,
    QWidget *parent)
    : QDialog(parent)
    , m_workingDir(workingDir)
    , m_items(selectedItems)
    , m_asinToPixmap(asinToPixmap)
    , m_cli(cli)
    , m_nodePath(nodePath)
{
    setWindowTitle(tr("Generate Storefront Image"));
    resize(900, 680);

    auto *mainLayout = new QVBoxLayout(this);

    // ----- horizontal splitter: product list (left) | settings (right) -----
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // LEFT: product list
    m_productList = new QListWidget(splitter);
    m_productList->setIconSize(QSize(kIconSizePx, kIconSizePx));
    m_productList->setViewMode(QListView::IconMode);
    m_productList->setMovement(QListView::Static);
    m_productList->setResizeMode(QListView::Adjust);
    m_productList->setSelectionMode(QAbstractItemView::NoSelection);
    m_productList->setMinimumWidth(180);
    splitter->addWidget(m_productList);

    // RIGHT: settings panel
    auto *rightWidget = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightWidget);

    // --- Settings group ---
    auto *settingsGroup = new QGroupBox(tr("Settings"), rightWidget);
    auto *settingsLayout = new QVBoxLayout(settingsGroup);

    // Show selected CLI as read-only label
    auto *cliRow = new QHBoxLayout();
    cliRow->addWidget(new QLabel(tr("CLI:"), settingsGroup));
    auto *cliLabel = new QLabel(m_cli ? m_cli->getName() : tr("(none)"), settingsGroup);
    cliRow->addWidget(cliLabel, 1);
    settingsLayout->addLayout(cliRow);

    // Size row
    auto *sizeRow = new QHBoxLayout();
    sizeRow->addWidget(new QLabel(tr("Size:"), settingsGroup));
    m_comboBoxSize = new QComboBox(settingsGroup);
    m_comboBoxSize->addItems({QStringLiteral("Both"),
                              QStringLiteral("Desktop (1792×1024)"),
                              QStringLiteral("Mobile (1024×1024)")});
    sizeRow->addWidget(m_comboBoxSize, 1);
    settingsLayout->addLayout(sizeRow);

    rightLayout->addWidget(settingsGroup);

    // --- Prompt group (stretchable) ---
    auto *promptGroup = new QGroupBox(tr("Prompt"), rightWidget);
    auto *promptLayout = new QVBoxLayout(promptGroup);
    m_promptEdit = new QTextEdit(promptGroup);
    promptLayout->addWidget(m_promptEdit);
    rightLayout->addWidget(promptGroup, 1);

    // --- Versions group (fixed height) ---
    auto *versionsGroup = new QGroupBox(tr("Versions"), rightWidget);
    versionsGroup->setFixedHeight(160);
    auto *versionsLayout = new QVBoxLayout(versionsGroup);
    m_versionsList = new QListWidget(versionsGroup);
    m_versionsList->setViewMode(QListView::IconMode);
    m_versionsList->setFlow(QListView::LeftToRight);
    m_versionsList->setFixedHeight(110);
    m_versionsList->setIconSize(QSize(120, 70));
    m_versionsList->setUniformItemSizes(true);
    m_versionsList->setMovement(QListView::Static);
    versionsLayout->addWidget(m_versionsList);
    auto *versionsBtnRow = new QHBoxLayout();
    m_buttonViewImage = new QPushButton(tr("View image"), versionsGroup);
    m_buttonViewImage->setEnabled(false);
    versionsBtnRow->addWidget(m_buttonViewImage);
    m_buttonCopyPath = new QPushButton(tr("Copy path"), versionsGroup);
    m_buttonCopyPath->setEnabled(false);
    versionsBtnRow->addWidget(m_buttonCopyPath);
    m_buttonDeleteVersion = new QPushButton(tr("Delete selected"), versionsGroup);
    m_buttonDeleteVersion->setEnabled(false);
    versionsBtnRow->addWidget(m_buttonDeleteVersion);
    versionsLayout->addLayout(versionsBtnRow);
    rightLayout->addWidget(versionsGroup);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter, 1);

    // ----- log -----
    m_logEdit = new QTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setFixedHeight(120);
    m_logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    mainLayout->addWidget(m_logEdit);

    // ----- bottom buttons -----
    auto *btnRow = new QHBoxLayout();
    m_buttonGenerate = new QPushButton(tr("Generate"), this);
    btnRow->addWidget(m_buttonGenerate);
    btnRow->addStretch();
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    btnRow->addWidget(btnBox);
    mainLayout->addLayout(btnRow);

    // ----- connections -----
    connect(m_buttonGenerate, &QPushButton::clicked,
            this, &DialogGenStorefrontImage::_onGenerateClicked);
    connect(m_buttonViewImage, &QPushButton::clicked,
            this, &DialogGenStorefrontImage::_viewImage);
    connect(m_buttonCopyPath, &QPushButton::clicked,
            this, &DialogGenStorefrontImage::_copyImagePath);
    connect(m_buttonDeleteVersion, &QPushButton::clicked,
            this, &DialogGenStorefrontImage::_deleteSelectedVersion);
    connect(m_versionsList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_buttonViewImage->setEnabled(row >= 0);
        m_buttonCopyPath->setEnabled(row >= 0);
        m_buttonDeleteVersion->setEnabled(row >= 0);
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ----- initial population -----
    _populateProductList();
    _autoFillPrompt();
    _loadVersions();
}

DialogGenStorefrontImage::~DialogGenStorefrontImage() = default;

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

QString DialogGenStorefrontImage::_storefrontDir() const
{
    return m_workingDir.filePath(QStringLiteral("stores/storefront"));
}

QString DialogGenStorefrontImage::_versionsJsonPath() const
{
    return QDir(_storefrontDir()).filePath(QStringLiteral("versions.json"));
}

// ---------------------------------------------------------------------------
// Population helpers
// ---------------------------------------------------------------------------

void DialogGenStorefrontImage::_populateProductList()
{
    m_productList->clear();
    for (const AmazonCatalogApi::StoreItem &item : std::as_const(m_items)) {
        auto *li = new QListWidgetItem(m_productList);
        li->setText(item.asin);
        li->setToolTip(item.title);
        const QPixmap px = m_asinToPixmap.value(item.asin);
        if (!px.isNull())
            li->setIcon(QIcon(px));
        li->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }
}

QString DialogGenStorefrontImage::_referenceImagePath(const QString &asin) const
{
    // Same resolution order PaneStore uses for its own thumbnails — see
    // PaneStore::_applyItems: sizing/{ASIN}-*/{ASIN}_main.jpg first (full-res
    // source photo), then stores/thumbs/{ASIN}.jpg (cached thumbnail).
    const QDir sizingDir(m_workingDir.filePath(QStringLiteral("sizing")));
    const QStringList dirs = sizingDir.entryList({asin + QStringLiteral("-*")}, QDir::Dirs);
    for (const QString &d : dirs) {
        const QString path = sizingDir.filePath(
            d + QLatin1Char('/') + asin + QStringLiteral("_main.jpg"));
        if (QFile::exists(path)) return path;
    }
    const QString thumb = m_workingDir.filePath(
        QStringLiteral("stores/thumbs/%1.jpg").arg(asin));
    return QFile::exists(thumb) ? thumb : QString();
}

void DialogGenStorefrontImage::_autoFillPrompt()
{
    // Deliberately does NOT describe color/pattern/material/size in text — an
    // image model asked to "generate a black burkini" from a caption invents
    // its own idea of black and ignores what the real product looks like.
    // Instead, point it at the actual product photos already on disk and tell
    // it to reproduce them faithfully. This is category-agnostic by design:
    // the same instruction works for clothing, shoes, or carpets, since none
    // of it depends on textual attributes specific to any one product type.
    // The title is deliberately left out here too — it's exactly the same
    // color/size-laden text the instruction above tells the model to ignore;
    // the ASIN is enough to keep each reference line traceable in the log.
    QStringList refLines;
    int missing = 0;
    bool anyModeled = false;
    for (const AmazonCatalogApi::StoreItem &item : std::as_const(m_items)) {
        if (categoryHasModel(item.category)) anyModeled = true;
        const QString path = _referenceImagePath(item.asin);
        if (path.isEmpty()) { ++missing; continue; }
        refLines << tr("- %1: %2").arg(item.asin, path);
    }

    QString prompt = tr(
        "Professional horizontal banner for an Amazon storefront, composed from "
        "the exact products shown in the reference photos listed below — one "
        "photo per product. Reproduce each product exactly as it appears in its "
        "own reference photo: same color, pattern, material, and shape. Do not "
        "invent, guess, or reinterpret any product's appearance from its name — "
        "the photos are the only source of truth for that. You may design the "
        "arrangement, background, lighting, and composition freely, with a "
        "background that matches the product's occasion.\n\n"
        "Reference photos:\n%1\n\n"
        "Clean modern photography, high quality, no text, no watermarks, "
        "horizontal composition.")
        .arg(refLines.join(QLatin1Char('\n')));

    if (anyModeled)
        prompt += QLatin1Char('\n') + tr("Improve model positions.");

    if (missing > 0) {
        prompt += QLatin1Char('\n') + tr(
            "Note: %1 selected product(s) have no photo on disk and are omitted above.")
            .arg(missing);
    }

    m_promptEdit->setPlainText(prompt);
}

void DialogGenStorefrontImage::_loadVersions()
{
    m_versionsList->clear();

    QFile f(_versionsJsonPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    QList<QJsonObject> versions;
    versions.reserve(arr.size());
    for (const QJsonValue &v : arr)
        versions.append(v.toObject());
    std::sort(versions.begin(), versions.end(),
              [](const QJsonObject &a, const QJsonObject &b) {
                  return a.value(QStringLiteral("ts")).toVariant().toLongLong()
                       > b.value(QStringLiteral("ts")).toVariant().toLongLong();
              });

    const QDir sfDir(_storefrontDir());

    // One strip item per image file — "Both" versions produce two items.
    auto addItem = [&](const QJsonObject &obj, const QString &fname,
                       const QString &typeLabel) {
        const qint64 ts = obj.value(QStringLiteral("ts")).toVariant().toLongLong();
        const QString cli = obj.value(QStringLiteral("cli")).toString();
        const QString dateStr =
            QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("MM-dd HH:mm"));
        auto *li = new QListWidgetItem(m_versionsList);
        li->setText(QStringLiteral("%1 %2\n%3").arg(dateStr, typeLabel, cli));
        li->setData(Qt::UserRole,     sfDir.filePath(fname));       // abs path
        li->setData(Qt::UserRole + 1, static_cast<qlonglong>(ts)); // ts for delete
        QPixmap px(sfDir.filePath(fname));
        if (!px.isNull())
            li->setIcon(QIcon(px.scaled(120, 70, Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation)));
    };

    // Show versions whose nodePath is a prefix of (or equal to) this dialog's nodePath.
    // Versions with no nodePath (old entries) are shown everywhere.
    auto nodePathVisible = [](const QStringList &stored, const QStringList &current) {
        if (stored.isEmpty()) return true;
        if (current.size() < stored.size()) return false;
        for (int i = 0; i < stored.size(); ++i)
            if (current[i] != stored[i]) return false;
        return true;
    };

    for (const QJsonObject &obj : std::as_const(versions)) {
        QStringList nodePath;
        for (const QJsonValue &v : obj.value(QStringLiteral("nodePath")).toArray())
            nodePath.append(v.toString());
        if (!nodePathVisible(nodePath, m_nodePath)) continue;
        const QString desktop = obj.value(QStringLiteral("desktop")).toString();
        const QString mobile  = obj.value(QStringLiteral("mobile")).toString();
        if (!desktop.isEmpty()) addItem(obj, desktop, tr("Desktop"));
        if (!mobile.isEmpty())  addItem(obj, mobile,  tr("Mobile"));
    }
}

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

void DialogGenStorefrontImage::_log(const QString &line)
{
    const QString ts =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_logEdit->append(QStringLiteral("[%1] %2").arg(ts, line));
}

// ---------------------------------------------------------------------------
// Generate trigger
// ---------------------------------------------------------------------------

void DialogGenStorefrontImage::_onGenerateClicked()
{
    if (!m_cli) {
        QMessageBox::warning(this, tr("No CLI"), tr("No CLI selected."));
        return;
    }
    _generate();
}

// ---------------------------------------------------------------------------
// _versionAt — load versions.json sorted newest-first, return entry at row
// ---------------------------------------------------------------------------

QJsonObject DialogGenStorefrontImage::_versionByTs(qint64 ts) const
{
    QFile f(_versionsJsonPath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("ts")).toVariant().toLongLong() == ts)
            return o;
    }
    return {};
}

// ---------------------------------------------------------------------------
// View image
// ---------------------------------------------------------------------------

void DialogGenStorefrontImage::_viewImage()
{
    const int row = m_versionsList->currentRow();
    if (row < 0) return;

    const qint64 ts = m_versionsList->item(row)->data(Qt::UserRole + 1).toLongLong();
    const QJsonObject ver = _versionByTs(ts);
    if (ver.isEmpty()) return;

    const QDir sfDir(_storefrontDir());

    // Build list of available images for this version.
    struct ImageEntry { QString label; QString absPath; };
    QList<ImageEntry> images;
    const QString desktopFile = ver.value(QStringLiteral("desktop")).toString();
    const QString mobileFile  = ver.value(QStringLiteral("mobile")).toString();
    if (!desktopFile.isEmpty()) images.append({tr("Desktop"), sfDir.filePath(desktopFile)});
    if (!mobileFile.isEmpty())  images.append({tr("Mobile"),  sfDir.filePath(mobileFile)});
    if (images.isEmpty()) return;

    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("View image — %1")
        .arg(QDateTime::fromSecsSinceEpoch(
                 ver.value(QStringLiteral("ts")).toVariant().toLongLong())
             .toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    dlg->resize(1000, 700);

    auto *layout = new QVBoxLayout(dlg);

    // Top bar: combo + copy path button
    auto *topRow = new QHBoxLayout();
    auto *combo  = new QComboBox(dlg);
    for (const ImageEntry &e : std::as_const(images))
        combo->addItem(e.label);
    topRow->addWidget(new QLabel(tr("Image:"), dlg));
    topRow->addWidget(combo, 1);
    auto *copyBtn = new QPushButton(tr("Copy path"), dlg);
    topRow->addWidget(copyBtn);
    layout->addLayout(topRow);

    // Scrollable image label
    auto *scroll = new QScrollArea(dlg);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setWidgetResizable(false);
    auto *imgLabel = new QLabel(scroll);
    imgLabel->setAlignment(Qt::AlignCenter);
    scroll->setWidget(imgLabel);
    layout->addWidget(scroll, 1);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    layout->addWidget(btnBox);

    // Load and display the selected image, scaled to fit the scroll area.
    auto loadImage = [=](int idx) {
        if (idx < 0 || idx >= images.size()) return;
        const QPixmap px(images.at(idx).absPath);
        if (px.isNull()) {
            imgLabel->setText(tr("(image not found: %1)").arg(images.at(idx).absPath));
            imgLabel->resize(400, 100);
            return;
        }
        const QSize available = scroll->viewport()->size() - QSize(4, 4);
        const QPixmap scaled  = px.scaled(available, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
        imgLabel->setPixmap(scaled);
        imgLabel->resize(scaled.size());
    };

    connect(combo, &QComboBox::currentIndexChanged, dlg, loadImage);
    connect(copyBtn, &QPushButton::clicked, dlg, [=]() {
        const int idx = combo->currentIndex();
        if (idx >= 0 && idx < images.size())
            QGuiApplication::clipboard()->setText(images.at(idx).absPath);
    });
    connect(btnBox, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    dlg->show();
    // Load after show so viewport size is known.
    QTimer::singleShot(0, dlg, [=]() { loadImage(combo->currentIndex()); });
}

// ---------------------------------------------------------------------------
// Copy image path
// ---------------------------------------------------------------------------

void DialogGenStorefrontImage::_copyImagePath()
{
    const int row = m_versionsList->currentRow();
    if (row < 0) return;
    const QString path = m_versionsList->item(row)->data(Qt::UserRole).toString();
    if (!path.isEmpty())
        QGuiApplication::clipboard()->setText(path);
}

// ---------------------------------------------------------------------------
// Delete version
// ---------------------------------------------------------------------------

void DialogGenStorefrontImage::_deleteSelectedVersion()
{
    const int row = m_versionsList->currentRow();
    if (row < 0) return;

    const qint64 victimTs =
        m_versionsList->item(row)->data(Qt::UserRole + 1).toLongLong();
    if (victimTs == 0) return;

    QFile f(_versionsJsonPath());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    // Find victim entry by ts.
    QJsonObject victim;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("ts")).toVariant().toLongLong() == victimTs) {
            victim = o;
            break;
        }
    }
    if (victim.isEmpty()) return;

    // Remove image files.
    const QDir sfDir(_storefrontDir());
    for (const char *key : {"desktop", "mobile"}) {
        const QString fname = victim.value(QLatin1String(key)).toString();
        if (!fname.isEmpty())
            QFile::remove(sfDir.filePath(fname));
    }

    // Remove the entry (matched by ts) from the original array.
    QJsonArray newArr;
    for (const QJsonValue &v : std::as_const(arr)) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("ts")).toVariant().toLongLong() == victimTs)
            continue;
        newArr.append(o);
    }

    QSaveFile sf(_versionsJsonPath());
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(newArr).toJson(QJsonDocument::Compact));
        sf.commit();
    }

    _loadVersions();
    _log(tr("Deleted version."));
}

// ---------------------------------------------------------------------------
// _generate — uses runPromptAsync (safe: no coroutine frame, QPointer guard)
// ---------------------------------------------------------------------------

void DialogGenStorefrontImage::_generate()
{
    if (!m_cli) return;

    const QString prompt   = m_promptEdit->toPlainText().trimmed();
    const QString sizeSel  = m_comboBoxSize->currentText();

    const bool wantDesktop = sizeSel.startsWith(QStringLiteral("Desktop"))
                          || sizeSel == QStringLiteral("Both");
    const bool wantMobile  = sizeSel.startsWith(QStringLiteral("Mobile"))
                          || sizeSel == QStringLiteral("Both");

    m_workingDir.mkpath(QStringLiteral("stores/storefront"));
    const QDir sfDir(_storefrontDir());
    const qint64 ts = QDateTime::currentSecsSinceEpoch();

    const QString desktopFile = QStringLiteral("%1_desktop.jpg").arg(ts);
    const QString mobileFile  = QStringLiteral("%1_mobile.jpg").arg(ts);
    const QString desktopAbs  = sfDir.filePath(desktopFile);
    const QString mobileAbs   = sfDir.filePath(mobileFile);

    QString fullPrompt = prompt;
    fullPrompt += QStringLiteral("\n\n");
    if (wantDesktop)
        fullPrompt += tr("Save the desktop image as JPEG to \"%1\" (1792×1024 pixels).\n")
                      .arg(desktopAbs);
    if (wantMobile)
        fullPrompt += tr("Save the mobile image as JPEG to \"%1\" (1024×1024 pixels).\n")
                      .arg(mobileAbs);

    m_buttonGenerate->setEnabled(false);
    _log(tr("Running %1…").arg(m_cli->getName()));

    // Use runPromptAsync: heap-allocated QProcess + QPointer guard, no coroutine
    // frame involved — avoids the QCoroSignal dangling-pointer crash.
    m_cli->runPromptAsync(fullPrompt, sfDir.absolutePath(), this,
        [this, ts, prompt, sfDir,
         wantDesktop, wantMobile,
         desktopFile, mobileFile, desktopAbs, mobileAbs,
         nodePath = m_nodePath]
        (CliRunResult result)
    {
        m_buttonGenerate->setEnabled(true);

        if (!result.processStarted) {
            _log(tr("⚠ CLI not found: %1").arg(m_cli ? m_cli->getExecutable() : QString{}));
            return;
        }
        if (!result.output.isEmpty())
            _log(result.output.trimmed());
        if (result.exitCode != 0) {
            _log(tr("⚠ CLI failed (exit %1): %2")
                 .arg(result.exitCode).arg(result.errorOutput.trimmed()));
            return;
        }

        QString savedDesktop, savedMobile, savedDesktopAbs, savedMobileAbs;
        if (wantDesktop) {
            if (QFile::exists(desktopAbs)) {
                savedDesktop    = desktopFile;
                savedDesktopAbs = desktopAbs;
                _log(tr("Saved %1").arg(desktopFile));
            } else {
                _log(tr("⚠ Desktop image not produced: %1").arg(desktopAbs));
            }
        }
        if (wantMobile) {
            if (QFile::exists(mobileAbs)) {
                savedMobile    = mobileFile;
                savedMobileAbs = mobileAbs;
                _log(tr("Saved %1").arg(mobileFile));
            } else {
                _log(tr("⚠ Mobile image not produced: %1").arg(mobileAbs));
            }
        }

        if (savedDesktop.isEmpty() && savedMobile.isEmpty()) {
            _log(tr("⚠ No output images were produced."));
            return;
        }

        QJsonArray arr;
        {
            QFile f(_versionsJsonPath());
            if (f.open(QIODevice::ReadOnly)) {
                arr = QJsonDocument::fromJson(f.readAll()).array();
                f.close();
            }
        }
        QJsonObject entry;
        entry[QStringLiteral("ts")]       = static_cast<double>(ts);
        entry[QStringLiteral("cli")]      = m_cli ? m_cli->getName() : QString{};
        entry[QStringLiteral("prompt")]   = prompt;
        entry[QStringLiteral("desktop")]  = savedDesktop;
        entry[QStringLiteral("mobile")]   = savedMobile;
        QJsonArray pathArr;
        for (const QString &s : nodePath) pathArr.append(s);
        entry[QStringLiteral("nodePath")] = pathArr;
        arr.append(entry);

        QSaveFile sf(_versionsJsonPath());
        if (sf.open(QIODevice::WriteOnly)) {
            sf.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
            sf.commit();
        }

        _loadVersions();
        _log(tr("Done."));
        emit imageGenerated(savedDesktopAbs, savedMobileAbs);
    });
}
