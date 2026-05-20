#ifndef MIDDLETRUNCATEDELEGATE_H
#define MIDDLETRUNCATEDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QFontMetrics>
#include <QStyleOptionViewItem>
#include <QApplication>
#include <QStyle>
#include <QModelIndex>

// Small QStyledItemDelegate that elides displayed text using Qt::ElideMiddle
// when the content doesn't fit the cell. The full text is still available
// via the model's Qt::ToolTipRole.
class MiddleTruncateDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit MiddleTruncateDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        const QString fullText = index.data(Qt::DisplayRole).toString();

        // Let the style draw background, selection highlight, decoration, etc.
        opt.text.clear();
        const QWidget* widget = opt.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        // Compute the available text rect (respecting decoration / margins)
        QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget);
        if (textRect.isEmpty())
            textRect = option.rect;

        const QFontMetrics fm(opt.font);
        const QString elided = fm.elidedText(fullText, Qt::ElideMiddle, textRect.width());

        painter->save();
        painter->setFont(opt.font);
        painter->setPen(opt.palette.color(
            (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText
                                                 : QPalette::Text));
        painter->drawText(textRect, opt.displayAlignment, elided);
        painter->restore();
    }
};

#endif // MIDDLETRUNCATEDELEGATE_H
