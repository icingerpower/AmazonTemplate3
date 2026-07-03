#include "PaneSettings.h"
#include "ui_PaneSettings.h"
#include "SettingsTable.h"
#include "TemuStoreModel.h"
#include "OpenAi2.h"
#include "AbstractCli.h"

#include <QColor>
#include <QHeaderView>
#include <QStandardItemModel>

PaneSettings::PaneSettings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettings)
{
    ui->setupUi(this);

    // The CLI list is now built synchronously in MainWindow via PATH lookup,
    // so there is nothing to refresh. The button is kept for UI consistency
    // but is left without a handler.
    ui->buttonRefreshCliTools->setEnabled(false);

    _loadSettings();
    _connectSlots();
}

PaneSettings::~PaneSettings()
{
    delete ui;
}

void PaneSettings::setAvailableClis(const QList<AbstractCli *> &clis)
{
    m_availableClis = clis;

    const QList<AbstractCli *> &all = AbstractCli::ALL_CLIS();
    auto *model = new QStandardItemModel(all.size(), 3, this);
    model->setHorizontalHeaderLabels({tr("CLI"), tr("Executable"), tr("Status")});
    for (int i = 0; i < all.size(); ++i) {
        model->setItem(i, 0, new QStandardItem(all[i]->getName()));
        model->setItem(i, 1, new QStandardItem(all[i]->getExecutable()));
        const bool available = clis.contains(all[i]);
        auto *statusItem = new QStandardItem(available ? tr("Available") : tr("Not installed"));
        statusItem->setForeground(available ? QColor(Qt::darkGreen) : QColor(Qt::gray));
        model->setItem(i, 2, statusItem);
    }
    ui->tableViewCliTools->setModel(model);
    ui->tableViewCliTools->horizontalHeader()->setStretchLastSection(true);
    ui->tableViewCliTools->verticalHeader()->hide();
}

void PaneSettings::_loadSettings()
{
    auto *st = SettingsTable::instance();

    ui->lineEditOpenAiKey->setText(st->value(SettingsTable::KEY_OPENAI_API_KEY));

    ui->lineEditLwaClientId->setText(st->value(SettingsTable::KEY_LWA_CLIENT_ID));
    ui->lineEditLwaClientSecret->setText(st->value(SettingsTable::KEY_LWA_CLIENT_SECRET));

    ui->lineEditEuLwaRefreshToken->setText(st->value(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN));
    ui->lineEditEuSellerId->setText(st->value(SettingsTable::KEY_EU_SELLER_ID));

    ui->lineEditNaLwaRefreshToken->setText(st->value(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN));
    ui->lineEditNaSellerId->setText(st->value(SettingsTable::KEY_NA_SELLER_ID));

    ui->lineEditJpLwaRefreshToken->setText(st->value(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN));
    ui->lineEditJpSellerId->setText(st->value(SettingsTable::KEY_JP_SELLER_ID));

    ui->lineEditImgbbApiKey->setText(st->value(SettingsTable::KEY_IMGBB_API_KEY));

    ui->lineEditTemuAppKey->setText(st->value(SettingsTable::KEY_TEMU_APP_KEY));
    ui->lineEditTemuAppSecret->setText(st->value(SettingsTable::KEY_TEMU_APP_SECRET));

    m_temuStoreModel = new TemuStoreModel(this);
    ui->tableViewTemuStores->setModel(m_temuStoreModel);
    ui->tableViewTemuStores->horizontalHeader()->setStretchLastSection(true);
    ui->tableViewTemuStores->verticalHeader()->hide();

    const QString openAiKey = st->value(SettingsTable::KEY_OPENAI_API_KEY);
    if (!openAiKey.isEmpty()) {
        OpenAi2::instance()->init(openAiKey);
        OpenAi2::instance()->setMaxQueriesSameTime(10);
    }
}

void PaneSettings::_connectSlots()
{
    auto *st = SettingsTable::instance();

    connect(ui->lineEditOpenAiKey, &QLineEdit::textChanged, this, [st](const QString &v) {
        st->setValue(SettingsTable::KEY_OPENAI_API_KEY, v);
        if (!v.isEmpty()) {
            OpenAi2::instance()->init(v);
            OpenAi2::instance()->setMaxQueriesSameTime(10);
        }
    });

    connect(ui->lineEditLwaClientId, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_LWA_CLIENT_ID, v); });
    connect(ui->lineEditLwaClientSecret, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_LWA_CLIENT_SECRET, v); });

    connect(ui->lineEditEuLwaRefreshToken, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_EU_LWA_REFRESH_TOKEN, v); });
    connect(ui->lineEditEuSellerId, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_EU_SELLER_ID, v); });

    connect(ui->lineEditNaLwaRefreshToken, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_NA_LWA_REFRESH_TOKEN, v); });
    connect(ui->lineEditNaSellerId, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_NA_SELLER_ID, v); });

    connect(ui->lineEditJpLwaRefreshToken, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_JP_LWA_REFRESH_TOKEN, v); });
    connect(ui->lineEditJpSellerId, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_JP_SELLER_ID, v); });

    connect(ui->lineEditImgbbApiKey, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_IMGBB_API_KEY, v); });

    connect(ui->lineEditTemuAppKey, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_TEMU_APP_KEY, v); });
    connect(ui->lineEditTemuAppSecret, &QLineEdit::textChanged,
            this, [st](const QString &v){ st->setValue(SettingsTable::KEY_TEMU_APP_SECRET, v); });

    connect(ui->buttonAddTemuStore, &QPushButton::clicked,
            this, [this]() { m_temuStoreModel->addStore(); });
    connect(ui->buttonRemoveTemuStore, &QPushButton::clicked,
            this, [this]() {
                const QModelIndex idx = ui->tableViewTemuStores->currentIndex();
                if (idx.isValid())
                    m_temuStoreModel->removeStore(idx.row());
            });
}
