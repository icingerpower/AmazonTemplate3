#ifndef SETTINGSTABLE_H
#define SETTINGSTABLE_H

#include <QAbstractTableModel>

class SettingsTable : public QAbstractTableModel
{
    Q_OBJECT

public:
    // --- Keys (used by any class to read/write settings) ---
    static const QString KEY_OPENAI_API_KEY;

    static const QString KEY_LWA_CLIENT_ID;
    static const QString KEY_LWA_CLIENT_SECRET;
    static const QString KEY_EU_LWA_REFRESH_TOKEN;
    static const QString KEY_NA_LWA_REFRESH_TOKEN;
    static const QString KEY_JP_LWA_REFRESH_TOKEN;

    static const QString KEY_EU_SELLER_ID;
    static const QString KEY_NA_SELLER_ID;
    static const QString KEY_JP_SELLER_ID;

    static const QString KEY_IMGBB_API_KEY;

    static SettingsTable *instance();

    QString value(const QString &key, const QString &defaultValue = {}) const;
    void setValue(const QString &key, const QString &value);

    // QAbstractTableModel — 2 columns: Label | Value
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    explicit SettingsTable(QObject *parent = nullptr);

    struct Entry {
        QString label;
        QString key;
        bool sensitive;
    };
    static const QList<Entry> ENTRIES;
    static SettingsTable *s_instance;
};

#endif // SETTINGSTABLE_H
