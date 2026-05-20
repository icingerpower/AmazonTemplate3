#include "PaneSettings.h"
#include "ui_PaneSettings.h"
#include "SettingsTable.h"
#include "OpenAi2.h"

PaneSettings::PaneSettings(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PaneSettings)
{
    ui->setupUi(this);
    _loadSettings();
    _connectSlots();
}

PaneSettings::~PaneSettings()
{
    delete ui;
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
}
