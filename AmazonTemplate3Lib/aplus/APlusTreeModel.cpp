#include "APlusTreeModel.h"

#include <QColor>
#include <QDir>
#include <QFont>
#include <QHash>

APlusTreeModel::APlusTreeModel(APlusContent *content, QObject *parent)
    : QAbstractItemModel(parent)
    , m_content(content)
{
    _rebuildFamilies();
}

// ─── internalId encoding ───────────────────────────────────────────────────
//
//  Family nodes  (level 0): internalId == 0
//  Version nodes (level 1): internalId == familyRow + 1   (range [1, 0xFFFF])
//  Language nodes (level 2): internalId == ((familyRow + 1) << 16)
//                                          | (versionRow + 1)   (> 0xFFFF)
//
// ──────────────────────────────────────────────────────────────────────────

QModelIndex APlusTreeModel::_makeFamilyIndex(int familyRow, int col) const
{
    return createIndex(familyRow, col, kFamilyId);
}

QModelIndex APlusTreeModel::_makeVersionIndex(int familyRow, int versionRow, int col) const
{
    return createIndex(versionRow, col,
                       static_cast<quintptr>(familyRow + 1));
}

QModelIndex APlusTreeModel::_makeLanguageIndex(int familyRow, int versionRow,
                                               int langRow, int col) const
{
    const quintptr id =
        (static_cast<quintptr>(familyRow + 1) << 16) |
        static_cast<quintptr>(versionRow + 1);
    return createIndex(langRow, col, id);
}

QString APlusTreeModel::_langLabelForSuffix(const QString &suffix)
{
    if (suffix.isEmpty())
        return tr("Default");

    static const QHash<QString, QString> map = {
        { QStringLiteral("en"), QStringLiteral("English")  },
        { QStringLiteral("fr"), QStringLiteral("French")   },
        { QStringLiteral("de"), QStringLiteral("German")   },
        { QStringLiteral("it"), QStringLiteral("Italian")  },
        { QStringLiteral("es"), QStringLiteral("Spanish")  },
        { QStringLiteral("nl"), QStringLiteral("Dutch")    },
        { QStringLiteral("se"), QStringLiteral("Swedish")  },
        { QStringLiteral("pl"), QStringLiteral("Polish")   },
        { QStringLiteral("jp"), QStringLiteral("Japanese") },
        { QStringLiteral("tr"), QStringLiteral("Turkish")  },
    };
    const auto it = map.constFind(suffix.toLower());
    if (it != map.constEnd())
        return it.value();
    return suffix.toUpper();
}

void APlusTreeModel::_rebuildFamilies()
{
    m_families.clear();
    if (!m_content)
        return;

    const QList<APlusElement> &elements = m_content->elements();

    auto familyIdFor = [](const QString &elemId) -> QString {
        if (elemId == QLatin1String("size_chart")
                || elemId.startsWith(QLatin1String("size_chart_")))
            return QStringLiteral("size_chart");
        if (elemId == QLatin1String("faq")
                || elemId.startsWith(QLatin1String("faq_")))
            return QStringLiteral("faq");
        return elemId; // image / other → singleton family keyed by its own id
    };

    auto familyDisplayName = [](const QString &familyId,
                                const APlusElement &fallback) -> QString {
        if (familyId == QLatin1String("size_chart")) return tr("Size Chart");
        if (familyId == QLatin1String("faq"))        return tr("FAQ");
        return fallback.displayName;
    };

    // Preserve first-seen order of families across the elements list.
    QHash<QString, int> familyIndexByKey;

    for (int i = 0; i < elements.size(); ++i) {
        const APlusElement &el = elements.at(i);
        const QString fid = familyIdFor(el.id);

        int famIdx = familyIndexByKey.value(fid, -1);
        if (famIdx < 0) {
            FamilyNode node;
            node.familyId    = fid;
            node.displayName = familyDisplayName(fid, el);
            m_families.append(node);
            famIdx = m_families.size() - 1;
            familyIndexByKey.insert(fid, famIdx);
        }
        m_families[famIdx].variantElemIndices.append(i);
    }

    // For each family, decide on base element + sort variants.
    for (FamilyNode &fam : m_families) {
        // Find preferred base for known multi-variant families.
        int basePos = -1;          // position inside fam.variantElemIndices
        int baseElemIndex = -1;    // element index in m_content->elements()

        auto idAt = [&](int pos) -> QString {
            return elements.at(fam.variantElemIndices.at(pos)).id;
        };

        if (fam.familyId == QLatin1String("faq")) {
            // Prefer faq_en first, then faq.
            for (int p = 0; p < fam.variantElemIndices.size(); ++p) {
                if (idAt(p) == QLatin1String("faq_en")) { basePos = p; break; }
            }
            if (basePos < 0) {
                for (int p = 0; p < fam.variantElemIndices.size(); ++p) {
                    if (idAt(p) == QLatin1String("faq")) { basePos = p; break; }
                }
            }
        } else {
            // Exact familyId match (size_chart, or singleton image families).
            for (int p = 0; p < fam.variantElemIndices.size(); ++p) {
                if (idAt(p) == fam.familyId) { basePos = p; break; }
            }
        }

        // Fallback: first variant in original order.
        if (basePos < 0 && !fam.variantElemIndices.isEmpty())
            basePos = 0;

        if (basePos >= 0) {
            // Move the base variant to position 0 while preserving relative
            // order of the others.
            const int el = fam.variantElemIndices.takeAt(basePos);
            fam.variantElemIndices.prepend(el);
            baseElemIndex = el;
        }
        fam.baseElemIdx = baseElemIndex;

        // Compute language labels per variant (using suffix after the last '_').
        fam.langLabels.clear();
        fam.langLabels.reserve(fam.variantElemIndices.size());
        for (int pos = 0; pos < fam.variantElemIndices.size(); ++pos) {
            const QString eid = elements.at(fam.variantElemIndices.at(pos)).id;
            QString suffix;
            if (eid != fam.familyId
                    && eid.startsWith(fam.familyId + QLatin1Char('_'))) {
                suffix = eid.mid(fam.familyId.length() + 1);
            }
            // For singleton families (eid == familyId) suffix is empty → "Default".
            fam.langLabels.append(_langLabelForSuffix(suffix));
        }
    }
}

APlusTreeModel::Location APlusTreeModel::locate(const QModelIndex &idx) const
{
    Location loc;
    if (!idx.isValid() || !m_content)
        return loc;

    const quintptr id = idx.internalId();

    if (id == kFamilyId) {
        const int fr = idx.row();
        if (fr < 0 || fr >= m_families.size())
            return loc;
        loc.family = fr;
        return loc;
    }

    if (id <= kVersionIdMask) {
        // Version node.
        const int fr = static_cast<int>(id) - 1;
        if (fr < 0 || fr >= m_families.size())
            return loc;
        const FamilyNode &fam = m_families.at(fr);
        if (fam.baseElemIdx < 0)
            return loc;
        const auto &els = m_content->elements();
        if (fam.baseElemIdx >= els.size())
            return loc;
        const int vr = idx.row();
        if (vr < 0 || vr >= els.at(fam.baseElemIdx).versions.size())
            return loc;
        loc.family  = fr;
        loc.version = vr;
        return loc;
    }

    // Language node.
    const int fr = static_cast<int>(id >> 16) - 1;
    const int vr = static_cast<int>(id & kVersionIdMask) - 1;
    if (fr < 0 || fr >= m_families.size())
        return loc;
    const FamilyNode &fam = m_families.at(fr);
    if (vr < 0)
        return loc;
    const int lr = idx.row();
    if (lr < 0 || lr >= fam.variantElemIndices.size())
        return loc;
    // Validate variant actually has this version.
    const auto &els = m_content->elements();
    const int elemIdx = fam.variantElemIndices.at(lr);
    if (elemIdx < 0 || elemIdx >= els.size())
        return loc;
    if (vr >= els.at(elemIdx).versions.size())
        return loc;

    loc.family  = fr;
    loc.version = vr;
    loc.lang    = lr;
    return loc;
}

QString APlusTreeModel::absoluteFilePath(const Location &loc, bool desktop) const
{
    if (!m_content || !loc.isValid())
        return {};

    int elemIdx = -1;
    if (loc.isLanguage()) {
        if (loc.family < 0 || loc.family >= m_families.size())
            return {};
        const FamilyNode &fam = m_families.at(loc.family);
        if (loc.lang < 0 || loc.lang >= fam.variantElemIndices.size())
            return {};
        elemIdx = fam.variantElemIndices.at(loc.lang);
    } else if (loc.isVersion()) {
        if (loc.family < 0 || loc.family >= m_families.size())
            return {};
        elemIdx = m_families.at(loc.family).baseElemIdx;
    } else {
        return {};
    }

    const auto &els = m_content->elements();
    if (elemIdx < 0 || elemIdx >= els.size())
        return {};
    const APlusElement &el = els.at(elemIdx);
    if (loc.version < 0 || loc.version >= el.versions.size())
        return {};
    const APlusVersion &v = el.versions.at(loc.version);
    const QString rel = desktop ? v.desktopFile : v.mobileFile;
    if (rel.isEmpty())
        return {};
    return m_content->dir().absoluteFilePath(rel);
}

int APlusTreeModel::familyIndexForElement(const QString &elementId) const
{
    if (elementId.isEmpty())
        return -1;

    for (int f = 0; f < m_families.size(); ++f) {
        const FamilyNode &fam = m_families.at(f);
        if (fam.familyId == elementId)
            return f;
        // Multi-variant: check the variant element ids too.
        if (m_content) {
            const auto &els = m_content->elements();
            for (int ei : fam.variantElemIndices) {
                if (ei >= 0 && ei < els.size() && els.at(ei).id == elementId)
                    return f;
            }
        }
    }
    return -1;
}

int APlusTreeModel::elementIndexForLocation(const Location &loc) const
{
    if (!loc.isValid())
        return -1;
    if (loc.family < 0 || loc.family >= m_families.size())
        return -1;
    const FamilyNode &fam = m_families.at(loc.family);
    if (loc.isLanguage()) {
        if (loc.lang < 0 || loc.lang >= fam.variantElemIndices.size())
            return -1;
        return fam.variantElemIndices.at(loc.lang);
    }
    if (loc.isVersion())
        return fam.baseElemIdx;
    return -1;
}

QString APlusTreeModel::familyIdAt(int familyRow) const
{
    if (familyRow < 0 || familyRow >= m_families.size())
        return {};
    return m_families.at(familyRow).familyId;
}

void APlusTreeModel::rebuild()
{
    beginResetModel();
    _rebuildFamilies();
    endResetModel();
}

QModelIndex APlusTreeModel::index(int row, int col, const QModelIndex &parent) const
{
    if (!m_content)
        return {};
    if (row < 0 || col < 0 || col >= COLUMN_COUNT)
        return {};

    if (!parent.isValid()) {
        if (row >= m_families.size())
            return {};
        return _makeFamilyIndex(row, col);
    }

    const quintptr pid = parent.internalId();

    if (pid == kFamilyId) {
        // Building a version row under a family.
        const int fr = parent.row();
        if (fr < 0 || fr >= m_families.size())
            return {};
        const FamilyNode &fam = m_families.at(fr);
        if (fam.baseElemIdx < 0)
            return {};
        const auto &els = m_content->elements();
        if (fam.baseElemIdx >= els.size())
            return {};
        if (row >= els.at(fam.baseElemIdx).versions.size())
            return {};
        return _makeVersionIndex(fr, row, col);
    }

    if (pid <= kVersionIdMask) {
        // Building a language row under a version.
        const int fr = static_cast<int>(pid) - 1;
        if (fr < 0 || fr >= m_families.size())
            return {};
        const FamilyNode &fam = m_families.at(fr);
        if (fam.variantElemIndices.size() < 2)
            return {}; // single-variant families do not have a language level
        const int vr = parent.row();
        if (vr < 0)
            return {};

        // Only variants with versions.size() > vr count as visible rows.
        const auto &els = m_content->elements();
        int visibleCount = 0;
        int visibleAtRow = -1;
        for (int p = 0; p < fam.variantElemIndices.size(); ++p) {
            const int ei = fam.variantElemIndices.at(p);
            if (ei < 0 || ei >= els.size())
                continue;
            if (els.at(ei).versions.size() > vr) {
                if (visibleCount == row)
                    visibleAtRow = p;
                ++visibleCount;
            }
        }
        Q_UNUSED(visibleAtRow)
        if (row >= visibleCount)
            return {};
        return _makeLanguageIndex(fr, vr, row, col);
    }

    // Language nodes have no children.
    return {};
}

QModelIndex APlusTreeModel::parent(const QModelIndex &idx) const
{
    if (!idx.isValid() || !m_content)
        return {};

    const quintptr id = idx.internalId();

    if (id == kFamilyId)
        return {};

    if (id <= kVersionIdMask) {
        // Version node → parent is a family node.
        const int fr = static_cast<int>(id) - 1;
        if (fr < 0 || fr >= m_families.size())
            return {};
        return _makeFamilyIndex(fr, 0);
    }

    // Language node → parent is a version node.
    const int fr = static_cast<int>(id >> 16) - 1;
    const int vr = static_cast<int>(id & kVersionIdMask) - 1;
    if (fr < 0 || fr >= m_families.size())
        return {};
    return _makeVersionIndex(fr, vr, 0);
}

int APlusTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!m_content)
        return 0;

    if (!parent.isValid())
        return m_families.size();

    const quintptr pid = parent.internalId();

    if (pid == kFamilyId) {
        const int fr = parent.row();
        if (fr < 0 || fr >= m_families.size())
            return 0;
        const FamilyNode &fam = m_families.at(fr);
        if (fam.baseElemIdx < 0)
            return 0;
        const auto &els = m_content->elements();
        if (fam.baseElemIdx >= els.size())
            return 0;
        return els.at(fam.baseElemIdx).versions.size();
    }

    if (pid <= kVersionIdMask) {
        // Counting languages under a version node.
        const int fr = static_cast<int>(pid) - 1;
        if (fr < 0 || fr >= m_families.size())
            return 0;
        const FamilyNode &fam = m_families.at(fr);
        if (fam.variantElemIndices.size() < 2)
            return 0; // single-variant family → no language level
        const int vr = parent.row();
        if (vr < 0)
            return 0;
        const auto &els = m_content->elements();
        int count = 0;
        for (int ei : fam.variantElemIndices) {
            if (ei < 0 || ei >= els.size())
                continue;
            if (els.at(ei).versions.size() > vr)
                ++count;
        }
        return count;
    }

    // Language node → no children.
    return 0;
}

int APlusTreeModel::columnCount(const QModelIndex & /*parent*/) const
{
    return COLUMN_COUNT;
}

QVariant APlusTreeModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || !m_content)
        return {};
    const int col = idx.column();
    const quintptr id = idx.internalId();

    // ─── Family node ───────────────────────────────────────────────
    if (id == kFamilyId) {
        const int fr = idx.row();
        if (fr < 0 || fr >= m_families.size())
            return {};
        const FamilyNode &fam = m_families.at(fr);

        if (role == Qt::DisplayRole) {
            switch (col) {
            case Name:    return fam.displayName;
            case Desktop: return {};
            case Mobile:  return {};
            default:      return {};
            }
        }
        return {};
    }

    // ─── Version node ──────────────────────────────────────────────
    if (id <= kVersionIdMask) {
        const int fr = static_cast<int>(id) - 1;
        if (fr < 0 || fr >= m_families.size())
            return {};
        const FamilyNode &fam = m_families.at(fr);
        if (fam.baseElemIdx < 0)
            return {};
        const auto &els = m_content->elements();
        if (fam.baseElemIdx >= els.size())
            return {};
        const APlusElement &el = els.at(fam.baseElemIdx);
        const int vr = idx.row();
        if (vr < 0 || vr >= el.versions.size())
            return {};
        const APlusVersion &v = el.versions.at(vr);

        if (role == Qt::DisplayRole) {
            switch (col) {
            case Name: {
                QString label = QStringLiteral("v%1 - %2")
                                    .arg(vr + 1)
                                    .arg(v.generated.toString(
                                            QStringLiteral("yyyy-MM-dd hh:mm")));
                if (vr == 0)
                    label += QStringLiteral(" *");
                return label;
            }
            case Desktop: return v.desktopFile.isEmpty()
                                 ? QStringLiteral("-") : QStringLiteral("v");
            case Mobile:  return v.mobileFile.isEmpty()
                                 ? QStringLiteral("-") : QStringLiteral("v");
            default:      return {};
            }
        }

        if (role == Qt::FontRole && vr == 0) {
            QFont f;
            f.setBold(true);
            return f;
        }

        if (role == Qt::ForegroundRole && vr == 0)
            return QColor(QStringLiteral("#1a6b2e"));

        if (role == Qt::ToolTipRole) {
            if (col == Desktop) {
                return v.desktopFile.isEmpty()
                           ? QString()
                           : m_content->dir().absoluteFilePath(v.desktopFile);
            }
            if (col == Mobile) {
                return v.mobileFile.isEmpty()
                           ? QString()
                           : m_content->dir().absoluteFilePath(v.mobileFile);
            }
            if (col == Name) {
                QStringList lines;
                if (!v.desktopFile.isEmpty())
                    lines << tr("Desktop: %1")
                                 .arg(m_content->dir().absoluteFilePath(v.desktopFile));
                if (!v.mobileFile.isEmpty())
                    lines << tr("Mobile: %1")
                                 .arg(m_content->dir().absoluteFilePath(v.mobileFile));
                return lines.join(QLatin1Char('\n'));
            }
        }
        return {};
    }

    // ─── Language node ─────────────────────────────────────────────
    const int fr = static_cast<int>(id >> 16) - 1;
    const int vr = static_cast<int>(id & kVersionIdMask) - 1;
    if (fr < 0 || fr >= m_families.size())
        return {};
    const FamilyNode &fam = m_families.at(fr);
    const int lr = idx.row();
    if (lr < 0 || lr >= fam.variantElemIndices.size())
        return {};
    const auto &els = m_content->elements();
    const int elemIdx = fam.variantElemIndices.at(lr);
    if (elemIdx < 0 || elemIdx >= els.size())
        return {};
    const APlusElement &el = els.at(elemIdx);
    if (vr < 0 || vr >= el.versions.size())
        return {};
    const APlusVersion &v = el.versions.at(vr);

    if (role == Qt::DisplayRole) {
        switch (col) {
        case Name: {
            const QString &lbl = (lr < fam.langLabels.size())
                                     ? fam.langLabels.at(lr)
                                     : el.id;
            return lbl;
        }
        case Desktop: return v.desktopFile.isEmpty()
                             ? QStringLiteral("-") : QStringLiteral("v");
        case Mobile:  return v.mobileFile.isEmpty()
                             ? QStringLiteral("-") : QStringLiteral("v");
        default:      return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        if (col == Desktop) {
            return v.desktopFile.isEmpty()
                       ? QString()
                       : m_content->dir().absoluteFilePath(v.desktopFile);
        }
        if (col == Mobile) {
            return v.mobileFile.isEmpty()
                       ? QString()
                       : m_content->dir().absoluteFilePath(v.mobileFile);
        }
        if (col == Name) {
            QStringList lines;
            if (!v.desktopFile.isEmpty())
                lines << tr("Desktop: %1")
                             .arg(m_content->dir().absoluteFilePath(v.desktopFile));
            if (!v.mobileFile.isEmpty())
                lines << tr("Mobile: %1")
                             .arg(m_content->dir().absoluteFilePath(v.mobileFile));
            return lines.join(QLatin1Char('\n'));
        }
    }

    return {};
}

QVariant APlusTreeModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole || o != Qt::Horizontal)
        return {};
    switch (section) {
    case Name:    return tr("Content");
    case Desktop: return tr("Desktop");
    case Mobile:  return tr("Mobile");
    default:      return {};
    }
}

Qt::ItemFlags APlusTreeModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}
