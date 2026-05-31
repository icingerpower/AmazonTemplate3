#pragma once

#include <QAbstractTableModel>
#include <QDir>
#include <QList>
#include <QMap>
#include <QString>

#include "AbstractSizeCategory.h"

struct SizingTemplate {
    QString id;
    QString name;
    QString category;
    QString mode;
    QString fromVal;
    QString toVal;
    QString brandMode;
    QString brandFromVal;
    QString brandToVal;
    QMap<QString, MeasurementInput> measurements;
};

class SizingTableTemplateModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { ColName = 0, ColCategory = 1, ColumnCount = 2 };

    explicit SizingTableTemplateModel(QObject *parent = nullptr);

    void    setWorkingDir(const QDir &dir);

    int     addTemplate(const QString &name);
    void    removeTemplateAt(int row);

    QString idForRow(int row) const;
    int     rowForId(const QString &id) const;

    const SizingTemplate &templateAt(int row) const;
    void    updateTemplate(int row, const SizingTemplate &data);

    void    load();
    void    save() const;

    int           rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int           columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant      data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                             int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool          setData(const QModelIndex &index, const QVariant &value,
                          int role = Qt::EditRole) override;

private:
    QString             _filePath() const;

    QDir                m_dir;
    QList<SizingTemplate> m_templates;
};
