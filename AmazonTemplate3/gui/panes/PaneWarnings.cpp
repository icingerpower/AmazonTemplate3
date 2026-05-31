#include "PaneWarnings.h"
#include "ui_PaneWarnings.h"
#include "AmazonMarketplace.h"

#include <QListWidgetItem>
#include <QSettings>

PaneWarnings::PaneWarnings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneWarnings)
{
    ui->setupUi(this);
    _populateMarketplaces();
}

PaneWarnings::~PaneWarnings()
{
    delete ui;
}

void PaneWarnings::setWorkingDir(const QDir &workingDir)
{
    m_workingDir = workingDir;
    _loadSettings();
}

void PaneWarnings::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    ui->comboBoxCli->blockSignals(true);
    ui->comboBoxCli->clear();
    for (AbstractCli *cli : clis)
        ui->comboBoxCli->addItem(cli->getName(), QVariant::fromValue(cli));

    int defaultIndex = 0;
    for (int i = 0; i < clis.size(); ++i) {
        if (clis[i]->canGenImages()) { defaultIndex = i; break; }
    }

    const QString saved = QSettings().value(QStringLiteral("warnings/selectedCli")).toString();
    int restoredIndex = -1;
    for (int i = 0; i < clis.size(); ++i) {
        if (clis[i]->getName() == saved) { restoredIndex = i; break; }
    }
    ui->comboBoxCli->setCurrentIndex(restoredIndex >= 0 ? restoredIndex : defaultIndex);
    ui->comboBoxCli->blockSignals(false);

    connect(ui->comboBoxCli, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
        if (index < 0 || index >= m_availableClis.size()) return;
        QSettings().setValue(QStringLiteral("warnings/selectedCli"),
                             m_availableClis[index]->getName());
    });
}

void PaneWarnings::_populateMarketplaces()
{
    struct RegionInfo { AmazonMarketplace::Region region; QString label; };
    static const QList<RegionInfo> REGIONS = {
        { AmazonMarketplace::Region::Europe,       "Europe"        },
        { AmazonMarketplace::Region::NorthAmerica, "North America" },
        { AmazonMarketplace::Region::Japan,        "Japan"         },
    };

    for (const RegionInfo &ri : REGIONS) {
        auto *sep = new QListWidgetItem(ri.label);
        sep->setFlags(Qt::NoItemFlags);
        QFont f = sep->font();
        f.setBold(true);
        sep->setFont(f);
        ui->listWidgetAmazon->addItem(sep);

        for (const AmazonMarketplace *mp : AmazonMarketplace::forRegion(ri.region)) {
            auto *item = new QListWidgetItem(
                QString("%1 (%2)").arg(mp->countryName(), mp->countryCode()));
            item->setData(Qt::UserRole, mp->countryCode());
            ui->listWidgetAmazon->addItem(item);
        }
    }

    for (int i = 0; i < ui->listWidgetAmazon->count(); ++i) {
        QListWidgetItem *item = ui->listWidgetAmazon->item(i);
        if (item->flags() & Qt::ItemIsSelectable) {
            ui->listWidgetAmazon->setCurrentItem(item);
            break;
        }
    }
}

void PaneWarnings::_loadSettings()
{
    // Reserved for future per-working-directory settings.
}
