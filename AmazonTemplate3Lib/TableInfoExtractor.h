#ifndef TABLEINFOEXTRACTOR_H
#define TABLEINFOEXTRACTOR_H

#include <QAbstractTableModel>

class TableInfoExtractor : public QAbstractTableModel
{
    Q_OBJECT

public:
    static const int IND_SKU;
    static const int IND_TITLE;
    static const int IND_COLOR_MAP;
    static const int IND_COLOR_NAME;
    static const int IND_SIZE_1;
    static const int IND_SIZE_2;
    static const int IND_SIZE_NUM;
    static const int IND_MODEL_NAME;
    static const int IND_IMAGE_PATHS;
    explicit TableInfoExtractor(QObject *parent = nullptr);

    void fillGtinTemplate(const QString &filePathFrom
                          , const QString &filePathTo
                          , const QString &brand
                          , const QString &categoryCode
                          ) const;
    void copyColumn(const QModelIndex &index);
    // Header:
    QVariant headerData(int section,
                        Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    // Editable:
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList readGtin(const QString &gtinFilePath) const;
    int getColIndexImage(int indImage) const;
    QString readAvailableImage(QString baseUrl, const QString &dirPath);

public slots:
    void clear();
    QString pasteSKUs(); // Returns the error if any
    QString pasteTitles(); // Returns the error if any
    void generateImageNames(QString baseUrl);

private:
    static const QStringList HEADER;
    QList<QStringList> m_listOfStringList;
    QString _paste(int colIndex);
    void _clearColumn(int colIndex);
    QStringList _getImageFileNames() const;
};

#endif // TABLEINFOEXTRACTOR_H
