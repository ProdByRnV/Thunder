#include "PresetFilter.h"

#include <algorithm>
#include <numeric>

namespace thunder::presets
{

namespace
{
int compareText (const juce::String& a, const juce::String& b)
{
    return a.compareNatural (b);
}

// Adds `text` to `list` unless it is there already in another spelling of case
// (the first spelling wins: one spelling per category, UI.md §9.16).
void addOnce (juce::StringArray& list, const juce::String& text)
{
    if (text.isNotEmpty() && !list.contains (text, true))
        list.add (text);
}

void sortNatural (juce::StringArray& list)
{
    std::sort (list.begin(), list.end(),
               [] (const juce::String& a, const juce::String& b) { return a.compareNatural (b) < 0; });
}
} // namespace

const juce::StringArray& initialCategories()
{
    static const juce::StringArray categories{"Atmos",    "Harmony", "Grit",        "Shimmer",
                                              "Rhythmic", "Lo-Fi",   "Experimental"};
    return categories;
}

bool matches (const Entry& entry, const Filter& filter, const juce::StringArray& favourites)
{
    if (filter.favouritesOnly && !favourites.contains (entry.key))
        return false;
    if (filter.category.isNotEmpty() && !entry.category.equalsIgnoreCase (filter.category))
        return false;
    if (filter.bankId.isNotEmpty() && entry.bankId() != filter.bankId)
        return false;
    if (filter.tag.isNotEmpty() && !entry.tagList().contains (filter.tag, true))
        return false;

    const auto haystack =
        juce::StringArray{entry.name, entry.author, entry.category, entry.bankLabel(), entry.tags}.joinIntoString (" ");
    for (const auto& word : juce::StringArray::fromTokens (filter.search, " ,", "\""))
        if (word.isNotEmpty() && !haystack.containsIgnoreCase (word))
            return false;

    return true;
}

std::vector<int> filtered (const Listing& listing, const Filter& filter, const Sort& sort,
                           const juce::StringArray& favourites)
{
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int> (listing.entries.size()); ++i)
        if (matches (listing.entries[static_cast<std::size_t> (i)], filter, favourites))
            indices.push_back (i);

    if (sort.by == SortBy::library)
    {
        if (sort.descending)
            std::reverse (indices.begin(), indices.end());
        return indices;
    }

    const auto field = [&sort] (const Entry& e) -> juce::String
    {
        switch (sort.by)
        {
            case SortBy::name:
                return e.name;
            case SortBy::category:
                return e.category;
            case SortBy::bank:
                return e.bankLabel();
            case SortBy::author:
                return e.author;
            case SortBy::library:
                break;
        }
        return {};
    };

    std::stable_sort (indices.begin(), indices.end(),
                      [&] (int a, int b)
                      {
                          const auto& ea = listing.entries[static_cast<std::size_t> (a)];
                          const auto& eb = listing.entries[static_cast<std::size_t> (b)];
                          auto c = compareText (field (ea), field (eb));
                          if (c == 0)
                              c = compareText (ea.name, eb.name);
                          return sort.descending ? c > 0 : c < 0;
                      });
    return indices;
}

juce::StringArray categoriesIn (const Listing& listing)
{
    juce::StringArray list;
    for (const auto& category : initialCategories())
        addOnce (list, category);
    for (const auto& e : listing.entries)
        addOnce (list, e.category);
    sortNatural (list);
    return list;
}

juce::StringArray bankIdsIn (const Listing& listing)
{
    // Factory, the top level ("User"), then the user banks by name.
    juce::StringArray banks;
    for (const auto& e : listing.entries)
        if (!banks.contains (e.bankId()))
            banks.add (e.bankId());

    std::stable_sort (
        banks.begin(), banks.end(),
        [] (const juce::String& a, const juce::String& b)
        {
            const auto rank = [] (const juce::String& id) { return id == "factory" ? 0 : id == "user:" ? 1 : 2; };
            if (rank (a) != rank (b))
                return rank (a) < rank (b);
            return a.compareNatural (b) < 0;
        });
    return banks;
}

juce::StringArray tagsIn (const Listing& listing)
{
    juce::StringArray list;
    for (const auto& e : listing.entries)
        for (const auto& tag : e.tagList())
            addOnce (list, tag);
    sortNatural (list);
    return list;
}

} // namespace thunder::presets
