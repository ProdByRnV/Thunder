#pragma once

#include "PresetLibrary.h"

#include <vector>

// The browser's search, filters and sort (PRD FR-PRESET-004, UI.md §9.12).
// ◄ ► step through the same list, so choosing a category and stepping
// auditions that category.

namespace thunder::presets
{

struct Filter
{
    juce::String search;   // name, author, category, bank and tags; any case
    juce::String category; // empty = all
    juce::String bankId;   // Entry::bankId(); empty = all
    juce::String tag;      // empty = all
    bool favouritesOnly = false;

    bool isEmpty() const noexcept
    {
        return search.trim().isEmpty() && category.isEmpty() && bankId.isEmpty() && tag.isEmpty() && !favouritesOnly;
    }

    bool operator== (const Filter&) const = default;
};

enum class SortBy
{
    library, // factory presets, then the user library by bank (the scan's order)
    name,
    category,
    bank,
    author
};

struct Sort
{
    SortBy by = SortBy::library;
    bool descending = false;

    bool operator== (const Sort&) const = default;
};

// Whether `entry` passes `filter`. Every search word must appear somewhere.
bool matches (const Entry& entry, const Filter& filter, const juce::StringArray& favourites);

// The indices into `listing.entries` that pass, in `sort` order (ties keep
// the library's order).
std::vector<int> filtered (const Listing& listing, const Filter& filter, const Sort& sort,
                           const juce::StringArray& favourites);

// The categories, banks (as Entry::bankId) and tags in use, each once, sorted.
// Categories include the initial ones (PRD FR-PRESET-003) even before any
// preset uses them.
juce::StringArray categoriesIn (const Listing& listing);
juce::StringArray bankIdsIn (const Listing& listing);
juce::StringArray tagsIn (const Listing& listing);

// The initial categories (PRD FR-PRESET-003).
const juce::StringArray& initialCategories();

} // namespace thunder::presets
