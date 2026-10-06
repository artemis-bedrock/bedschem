#include "bedschem/nbt/tag.h"

#include <algorithm>
#include <ranges>

namespace bedschem::nbt {
	template <typename Entries>
	static auto lowerBound(Entries& entries, std::string_view name) {
		return std::ranges::lower_bound(entries, name, std::less<>{}, [](const Compound::Entry& entry) -> std::string_view {
			return entry.first;
		});
	}

	template <typename Entries>
	static auto* findValue(Entries& entries, std::string_view name) {
		const auto entry = lowerBound(entries, name);
		using Result = decltype(&entry->second);
		if (entry == entries.end() || entry->first != name) {
			return Result{ nullptr };
		}

		return &entry->second;
	}

	bool List::operator==(const List& other) const {
		return type == other.type && items == other.items;
	}

	Compound::Compound(std::vector<Entry> entries) : mEntries(std::move(entries)) {
		std::ranges::stable_sort(mEntries, std::less<>{}, &Entry::first);

		auto keep = mEntries.begin();
		for (auto current = mEntries.begin(); current != mEntries.end(); ++current) {
			const auto next = std::next(current);
			if (next != mEntries.end() && next->first == current->first) {
				continue;
			}

			if (keep != current) {
				*keep = std::move(*current);
			}

			++keep;
		}

		mEntries.erase(keep, mEntries.end());
	}

	const Tag* Compound::find(std::string_view name) const {
		return findValue(mEntries, name);
	}

	Tag* Compound::find(std::string_view name) {
		return findValue(mEntries, name);
	}

	bool Compound::contains(std::string_view name) const {
		return find(name) != nullptr;
	}

	Tag& Compound::set(std::string name, Tag value) {
		const auto entry = lowerBound(mEntries, name);
		if (entry != mEntries.end() && entry->first == name) {
			entry->second = std::move(value);
			return entry->second;
		}

		return mEntries.emplace(entry, std::move(name), std::move(value))->second;
	}

	bool Compound::erase(std::string_view name) {
		const auto entry = lowerBound(mEntries, name);
		if (entry == mEntries.end() || entry->first != name) {
			return false;
		}

		mEntries.erase(entry);
		return true;
	}

	std::size_t Compound::size() const noexcept {
		return mEntries.size();
	}

	bool Compound::empty() const noexcept {
		return mEntries.empty();
	}

	Compound::const_iterator Compound::begin() const noexcept {
		return mEntries.begin();
	}

	Compound::const_iterator Compound::end() const noexcept {
		return mEntries.end();
	}

	bool Compound::operator==(const Compound& other) const {
		return mEntries == other.mEntries;
	}

	TagType Tag::type() const noexcept {
		return static_cast<TagType>(value.index() + 1);
	}
}
