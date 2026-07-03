#ifndef TEMUSTOREMODEL_H
#define TEMUSTOREMODEL_H

#include <QAbstractTableModel>
#include <QList>
#include <QString>

struct TemuStore {
    QString country;
    QString label;
    QString token;
    QString proxyHost;
    int     proxyPort = 0;
    QString proxyUser;
    QString proxyPassword;
};

class TemuStoreModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    static const int COL_COUNTRY    = 0;
    static const int COL_LABEL      = 1;
    static const int COL_TOKEN      = 2;
    static const int COL_PROXY_HOST = 3;
    static const int COL_PROXY_PORT = 4;
    static const int COL_PROXY_USER = 5;
    static const int COL_PROXY_PASS = 6;

    explicit TemuStoreModel(QObject *parent = nullptr);

    void addStore();
    void removeStore(int row);
    const QList<TemuStore> &stores() const { return m_stores; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    QList<TemuStore> m_stores;

    void _load();
    void _save() const;
};

#endif // TEMUSTOREMODEL_H
