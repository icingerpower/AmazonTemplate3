#include "AbstractSizeCategory.h"

#include <QStandardItemModel>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QColor>
#include <QPen>
#include <QRect>
#include <QObject>
#include <cmath>
#include <stdexcept>

static QString fmtMeas(double val, double rangeVal)
{
    auto fmt = [](double v) {
        return (v == std::round(v))
            ? QString::number(static_cast<int>(qRound(v)))
            : QString::number(v, 'f', 1);
    };
    if (rangeVal <= 0.0)
        return fmt(val);
    return fmt(val) + QLatin1Char('-') + fmt(val + rangeVal);
}

// Formats a cm measurement with its inch equivalent: "90-112 cm / 35.4-44.1 in"
static QString fmtCmAndIn(double cmVal, double cmRange = 0.0)
{
    return fmtMeas(cmVal, cmRange) + QStringLiteral(" cm / ")
         + fmtMeas(cmVal / 2.54, cmRange / 2.54) + QStringLiteral(" in");
}

QString AbstractSizeCategory::_formatVal(double val, bool isCm, bool isFloat)
{
    if (isCm) {
        return QString::number(val, 'f', 1) + QStringLiteral(" cm");
    }
    if (isFloat && val != std::round(val)) {
        return QString::number(val, 'f', 1);
    }
    return QString::number(static_cast<int>(qRound(val)));
}

QStringList AbstractSizeCategory::referenceKeys() const
{
    QStringList result;
    const auto rows = sizeRows();
    const QString refKey = referenceKey();
    for (const auto &row : rows) {
        const QString k = _formatVal(row.value(refKey), false, true);
        if (!result.contains(k))
            result << k;
    }
    return result;
}

int AbstractSizeCategory::_findIndex(const QString &key) const
{
    const auto rows = sizeRows();
    const QString refKey = referenceKey();
    for (int i = 0; i < rows.size(); ++i) {
        if (_formatVal(rows[i].value(refKey), false, true) == key)
            return i;
    }
    return -1;
}

QPair<QString,QString> AbstractSizeCategory::guessRange(const QStringList &rawSizes) const
{
    const QStringList refs = referenceKeys();
    QStringList matched;

    auto tryAdd = [&](const QString &candidate) {
        if (refs.contains(candidate) && !matched.contains(candidate))
            matched << candidate;
    };

    for (const QString &raw : rawSizes) {
        const QString trimmed = raw.trimmed();
        bool ok = false;
        int iv = trimmed.toInt(&ok);
        if (ok) {
            tryAdd(_formatVal(double(iv), false, true));
            continue;
        }
        double dv = trimmed.toDouble(&ok);
        if (ok) {
            tryAdd(_formatVal(dv, false, true));
            continue;
        }
        const QString first = trimmed.split(' ').first();
        iv = first.toInt(&ok);
        if (ok) {
            tryAdd(_formatVal(double(iv), false, true));
            continue;
        }
        dv = first.toDouble(&ok);
        if (ok) {
            tryAdd(_formatVal(dv, false, true));
        }
    }

    if (matched.isEmpty())
        return {};

    QStringList sorted;
    for (const QString &ref : refs)
        if (matched.contains(ref))
            sorted << ref;

    return {sorted.first(), sorted.last()};
}

QStandardItemModel* AbstractSizeCategory::buildTable(const QString &keyFrom,
                                                    const QString &keyTo,
                                                    const QMap<QString,MeasurementInput> &measurements,
                                                    QObject *parent) const
{
    int fromIdx = _findIndex(keyFrom);
    int toIdx   = _findIndex(keyTo);
    if (fromIdx < 0 || toIdx < 0)
        throw std::invalid_argument("Invalid size range key");

    if (fromIdx > toIdx)
        std::swap(fromIdx, toIdx);

    const auto rows    = sizeRows();
    const auto groups  = countryGroups();
    const auto fields  = measurementFields();
    const QString refK = referenceKey();

    const int nSizes = toIdx - fromIdx + 1;
    const int nRows  = groups.size() + fields.size();

    auto *model = new QStandardItemModel(nRows, 1 + nSizes, parent);

    model->setHeaderData(0, Qt::Horizontal, QString());
    for (int i = 0; i < nSizes; ++i) {
        const QString header = _formatVal(rows[fromIdx + i].value(refK), false, true);
        model->setHeaderData(1 + i, Qt::Horizontal, header);
    }

    const auto noEditFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    for (int r = 0; r < groups.size(); ++r) {
        const auto &g = groups[r];
        auto *label = new QStandardItem(g.label);
        label->setFlags(noEditFlags);
        model->setItem(r, 0, label);
        for (int i = 0; i < nSizes; ++i) {
            const double v = rows[fromIdx + i].value(g.key);
            auto *cell = new QStandardItem(
                g.isCm ? fmtCmAndIn(v) : _formatVal(v, false, g.isFloat));
            cell->setFlags(noEditFlags);
            model->setItem(r, 1 + i, cell);
        }
    }

    for (int f = 0; f < fields.size(); ++f) {
        const auto &field = fields[f];
        const int r = groups.size() + f;
        auto *label = new QStandardItem(field.label);
        label->setFlags(noEditFlags);
        model->setItem(r, 0, label);

        if (!field.derivedKey.isEmpty()) {
            const bool isCm = field.label.contains(QStringLiteral("cm"));
            for (int i = 0; i < nSizes; ++i) {
                const double v = rows[fromIdx + i].value(field.derivedKey);
                auto *cell = new QStandardItem(
                    isCm ? fmtCmAndIn(v) : _formatVal(v, false, true));
                cell->setFlags(noEditFlags);
                model->setItem(r, 1 + i, cell);
            }
        } else {
            const auto m = measurements.value(field.id);
            for (int i = 0; i < nSizes; ++i) {
                const double v = m.refValue + i * m.step;
                auto *cell = new QStandardItem(fmtCmAndIn(v, m.rangeVal));
                cell->setFlags(noEditFlags);
                model->setItem(r, 1 + i, cell);
            }
        }
    }

    return model;
}

QImage AbstractSizeCategory::_renderRows(const QList<QPair<QString,QStringList>> &rows)
{
    const int nRows = rows.size();
    int nCols = 1;
    for (const auto &r : rows)
        nCols = std::max(nCols, 1 + static_cast<int>(r.second.size()));
    const int rowH  = 30;

    QFont labelFont("Arial", 9, QFont::Bold);
    QFont cellFont("Arial", 9);
    QFontMetrics fmLabel(labelFont);
    QFontMetrics fmCell(cellFont);

    QList<int> colWidths;
    colWidths.reserve(nCols);

    int maxLabel = 0;
    for (const auto &r : rows)
        maxLabel = std::max(maxLabel, fmLabel.horizontalAdvance(r.first));
    colWidths << std::max(100, maxLabel + 24);

    for (int c = 1; c < nCols; ++c) {
        int maxW = 0;
        for (const auto &r : rows) {
            if (c - 1 < r.second.size())
                maxW = std::max(maxW, fmCell.horizontalAdvance(r.second[c - 1]));
        }
        colWidths << std::max(48, maxW + 20);
    }

    int totalW = 0;
    for (int w : colWidths) totalW += w;
    const int totalH = nRows * rowH;

    QImage img(totalW, totalH, QImage::Format_ARGB32);
    img.fill(Qt::white);

    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    for (int r = 0; r < nRows; ++r) {
        const int y = r * rowH;
        const QColor bg = (r % 2 == 0) ? QColor("#d4ebe6") : QColor("#e8f6f3");
        p.fillRect(QRect(0, y, totalW, rowH), bg);
        p.fillRect(QRect(0, y, colWidths[0], rowH), QColor(0, 0, 0, 15));

        int x = 0;
        for (int c = 0; c < nCols; ++c) {
            const QRect cellRect(x, y, colWidths[c], rowH);
            if (c == 0) {
                p.setFont(labelFont);
                QRect textRect = cellRect.adjusted(10, 0, 0, 0);
                p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, rows[r].first);
            } else {
                p.setFont(cellFont);
                const QString text = (c - 1 < rows[r].second.size()) ? rows[r].second[c - 1] : QString();
                p.drawText(cellRect, Qt::AlignCenter, text);
            }
            x += colWidths[c];
        }
    }

    p.setPen(QPen(QColor("#b0d8d0"), 1));
    for (int r = 0; r <= nRows; ++r) {
        const int y = r * rowH;
        p.drawLine(0, y, totalW, y);
    }

    p.setPen(QPen(QColor("#80c0b8"), 2));
    p.drawLine(colWidths[0], 0, colWidths[0], totalH);

    p.end();
    return img;
}

QImage AbstractSizeCategory::renderImage(QStandardItemModel *model) const
{
    const int nRows = model->rowCount();
    const int nCols = model->columnCount();

    // Split cells that contain " cm / " into two rows: one for cm, one for inches.
    // The UI table keeps combined cells for editing; only the rendered image is split.
    static const QString kSep = QStringLiteral(" cm / ");

    QList<QPair<QString,QStringList>> rows;
    for (int r = 0; r < nRows; ++r) {
        const auto *labelItem = model->item(r, 0);
        const QString label = labelItem ? labelItem->text() : QString();

        QStringList cmVals, inVals;
        bool hasDual = false;
        for (int c = 1; c < nCols; ++c) {
            const auto *it = model->item(r, c);
            const QString text = it ? it->text() : QString();
            const int sepPos = text.indexOf(kSep);
            if (sepPos >= 0) {
                // "90-112 cm / 35.4-44.1 in" → cm part before " / ", in part after
                cmVals << text.left(sepPos) + QStringLiteral(" cm");
                const QString inPart = text.mid(sepPos + kSep.size());
                // strip trailing " in" if present; we'll add the unit via label
                inVals << inPart;
                hasDual = true;
            } else {
                cmVals << text;
                inVals << text;
            }
        }

        if (hasDual) {
            // cm row: replace "(cm)" with "(cm)" (unchanged), or append " (cm)"
            QString cmLabel = label;
            if (!cmLabel.contains(QStringLiteral("cm"), Qt::CaseInsensitive))
                cmLabel += QStringLiteral(" (cm)");
            // inches row: replace "cm" → "in" in the label
            QString inLabel = label;
            inLabel.replace(QStringLiteral("(cm)"), QStringLiteral("(in)"),  Qt::CaseInsensitive);
            if (inLabel == label)
                inLabel += QStringLiteral(" (in)");
            rows << qMakePair(cmLabel, cmVals);
            rows << qMakePair(inLabel, inVals);
        } else {
            rows << qMakePair(label, cmVals);
        }
    }
    return _renderRows(rows);
}

QList<QPair<QString,QImage>> AbstractSizeCategory::renderGroupImages(
    const QString &keyFrom,
    const QString &keyTo,
    const QMap<QString,MeasurementInput> &measurements,
    const QStringList &letterLabels) const
{
    int fromIdx = _findIndex(keyFrom);
    int toIdx   = _findIndex(keyTo);
    if (fromIdx < 0 || toIdx < 0)
        throw std::invalid_argument("Invalid size range key");
    if (fromIdx > toIdx)
        std::swap(fromIdx, toIdx);

    const int nSizes = toIdx - fromIdx + 1;
    const auto rows    = sizeRows();
    const auto groups  = countryGroups();
    const auto fields  = measurementFields();

    QList<QPair<QString,QImage>> result;

    for (const auto &g : groups) {
        QList<QPair<QString,QStringList>> imgRows;

        QStringList sizeVals;
        for (int i = 0; i < nSizes; ++i) {
            if (!letterLabels.isEmpty())
                sizeVals << letterLabels.value(i);
            else
                sizeVals << _formatVal(rows[fromIdx + i].value(g.key), g.isCm, g.isFloat);
        }
        const QString sizeRowLabel = letterLabels.isEmpty() ? g.label : QObject::tr("Size");
        imgRows << qMakePair(sizeRowLabel, sizeVals);

        // Build cm rows for all fields first
        QList<QPair<QString,QStringList>> cmRows;
        for (const auto &field : fields) {
            QStringList cmVals;
            for (int i = 0; i < nSizes; ++i) {
                if (!field.derivedKey.isEmpty()) {
                    const double v = rows[fromIdx + i].value(field.derivedKey);
                    cmVals << (v == qRound(v)
                        ? QString::number(static_cast<int>(qRound(v)))
                        : QString::number(v, 'f', 1));
                } else {
                    const auto m = measurements.value(field.id);
                    cmVals << fmtMeas(m.refValue + i * m.step, m.rangeVal);
                }
            }
            cmRows << qMakePair(field.label, cmVals);
        }

        // English: all inches rows first, then all cm rows
        if (g.isEnglish) {
            for (const auto &[label, cmVals] : cmRows) {
                QString inLabel = label;
                inLabel.replace(QStringLiteral("(cm)"), QStringLiteral("(in)"));
                QStringList inVals;
                for (const QString &s : cmVals) {
                    const int dash = s.indexOf(QLatin1Char('-'));
                    if (dash > 0) {
                        const double lo = s.left(dash).toDouble() * 0.393701;
                        const double hi = s.mid(dash + 1).toDouble() * 0.393701;
                        inVals << QString::number(lo, 'f', 1) + QLatin1Char('-')
                                  + QString::number(hi, 'f', 1);
                    } else {
                        inVals << QString::number(s.toDouble() * 0.393701, 'f', 1);
                    }
                }
                imgRows << qMakePair(inLabel, inVals);
            }
        }
        for (const auto &cmRow : cmRows)
            imgRows << cmRow;

        result << qMakePair(g.label, _renderRows(imgRows));
    }

    return result;
}
