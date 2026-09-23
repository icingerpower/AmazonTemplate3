#include "StorePlacements.h"
#include <QJsonObject>
#include <algorithm>

QStringList StorePlacements::path(const Item &item)
{
    return {item.brand, item.category, item.gender, item.age};
}

bool StorePlacements::within(const QStringList &path, const QStringList &prefix)
{
    return !prefix.isEmpty() && path.size() >= prefix.size()
        && path.mid(0, prefix.size()) == prefix;
}

void StorePlacements::setPath(Item &item, const QStringList &path)
{
    item.brand = path.value(0);
    item.category = path.value(1);
    item.gender = path.value(2);
    item.age = path.value(3);
    item.manuallyMoved = true;
}

QList<StorePlacements::Placement> StorePlacements::unique(const QList<Placement> &placements)
{
    QList<Placement> result;
    for (const auto &placement : placements) {
        auto existing = std::find_if(result.begin(), result.end(), [&](const Placement &p) {
            return p.path == placement.path;
        });
        if (existing == result.end()) result.append(placement);
        else if (!placement.duplicate) existing->duplicate = false;
    }
    return result;
}

QList<StorePlacements::Placement> StorePlacements::placements(const Item &item) const
{
    if (!m_overrides.contains(item.asin)) return {{path(item), false}};
    auto result = m_overrides.value(item.asin);
    for (auto &placement : result)
        if (!placement.duplicate) placement.path = path(item);
    return unique(result);
}

QList<StorePlacements::Item> StorePlacements::treeItems(const QList<Item> &items) const
{
    QList<Item> result;
    for (const auto &item : items) {
        for (const auto &placement : placements(item)) {
            Item displayed = item;
            setPath(displayed, placement.path);
            result.append(displayed);
        }
    }
    return result;
}

bool StorePlacements::isDuplicate(const Item &item, const QStringList &scope) const
{
    bool found = false;
    for (const auto &placement : placements(item)) {
        if (!within(placement.path, scope)) continue;
        if (!placement.duplicate) return false;
        found = true;
    }
    return found;
}

void StorePlacements::transfer(QList<Item> &items, const QSet<QString> &asins,
                               const QStringList &source, const QStringList &destination,
                               bool duplicate)
{
    if (source.isEmpty() || destination.isEmpty() || destination.size() > 4) return;
    for (auto &item : items) {
        if (!asins.contains(item.asin)) continue;
        const auto before = placements(item);
        if (std::none_of(before.cbegin(), before.cend(), [&](const Placement &p) {
                return within(p.path, source);
            })) continue;
        QList<Placement> after;
        for (const auto &placement : before) {
            if (!within(placement.path, source)) {
                after.append(placement);
                continue;
            }
            auto moved = placement;
            for (int i = 0; i < destination.size(); ++i) moved.path[i] = destination[i];
            if (duplicate) {
                after.append(placement);
                moved.duplicate = true;
            } else if (!moved.duplicate) {
                setPath(item, moved.path);
            }
            after.append(moved);
        }
        m_overrides[item.asin] = unique(after);
    }
}

void StorePlacements::remove(QList<Item> &items, const QSet<QString> &asins,
                             const QStringList &scope)
{
    for (const auto &item : items) {
        if (!asins.contains(item.asin)) continue;
        auto remaining = placements(item);
        remaining.removeIf([&](const Placement &p) { return within(p.path, scope); });
        m_overrides[item.asin] = remaining;
    }
    items.removeIf([&](const Item &item) {
        if (!m_overrides.contains(item.asin) || !m_overrides.value(item.asin).isEmpty()) return false;
        m_overrides.remove(item.asin);
        return true;
    });
}

void StorePlacements::removeCategory(QList<Item> &items, const QStringList &scope)
{
    if (scope.isEmpty() || scope.size() > 4) return;
    for (auto &item : items) {
        const auto before = placements(item);
        if (std::none_of(before.cbegin(), before.cend(), [&](const Placement &p) {
                return within(p.path, scope);
            })) continue;
        QList<Placement> after;
        for (auto placement : before) {
            if (within(placement.path, scope)) {
                if (placement.duplicate) continue;
                placement.path[scope.size() - 1].clear();
                setPath(item, placement.path);
            }
            after.append(placement);
        }
        m_overrides[item.asin] = unique(after);
    }
    items.removeIf([&](const Item &item) {
        if (!m_overrides.contains(item.asin) || !m_overrides.value(item.asin).isEmpty()) return false;
        m_overrides.remove(item.asin);
        return true;
    });
}

QJsonArray StorePlacements::toJson(const Item &item) const
{
    QJsonArray result;
    for (const auto &placement : placements(item)) {
        result.append(QJsonObject{{QStringLiteral("path"), QJsonArray::fromStringList(placement.path)},
                                  {QStringLiteral("duplicate"), placement.duplicate}});
    }
    return result;
}

void StorePlacements::load(const QString &asin, const QJsonArray &array)
{
    QList<Placement> result;
    bool originalFound = false;
    for (const auto &value : array) {
        const auto object = value.toObject();
        const auto parts = object.value(QStringLiteral("path")).toArray();
        if (parts.size() != 4) continue;
        Placement placement;
        bool valid = true;
        for (const auto &part : parts) {
            valid &= part.isString();
            placement.path.append(part.toString());
        }
        if (!valid) continue;
        placement.duplicate = object.value(QStringLiteral("duplicate")).toBool(false);
        if (!placement.duplicate && originalFound) continue;
        originalFound |= !placement.duplicate;
        result.append(placement);
    }
    // Malformed metadata must not make a legacy catalog entry disappear.
    if (!result.isEmpty()) m_overrides[asin] = unique(result);
}
