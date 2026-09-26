#pragma once

#include <vector>
#include <algorithm>
#include <functional>
#include <assert.h>

template<typename T>
class SortedFilteredVector {
public:
	SortedFilteredVector(size_t capacity = 16) {
		m_Items.reserve(capacity);
		m_Indices.reserve(capacity);
	}

	void Add(const T& value) {
		m_Items.push_back(value);
		m_Indices.push_back(m_Items.size() - 1);
		assert(m_Items.size() == m_Indices.size());
	}

	void Clear() {
		m_Items.clear();
		m_Indices.clear();
	}

	void Reserve(size_t capacity) {
		m_Items.reserve(capacity);
		m_Indices.reserve(capacity);
	}

	void Set(std::vector<T> items) {
		m_Items = std::move(items);
		auto count = m_Items.size();
		m_Indices.clear();
		m_Indices.reserve(count);
		for (decltype(count) i = 0; i < count; i++)
			m_Indices.push_back(i);
	}

	const T& operator[](size_t index) const {
		return m_Items[m_Indices[index]];
	}

	T& operator[](size_t index) {
		return m_Items[m_Indices[index]];
	}

	void Sort(std::function<bool(const T & value1, const T & value2)> compare) {
		std::sort(m_Indices.begin(), m_Indices.end(), [=](size_t i1, size_t i2) {
			return compare(m_Items[i1], m_Items[i2]);
			});
	}

	size_t Size() const {
		return m_Items.size();
	}

	size_t FilteredSize() const {
		return m_Indices.size();
	}

	void Filter(std::function<bool(const T & value)> predicate) {
		m_Indices.clear();
		if (predicate == nullptr) {
			auto count = m_Items.size();
			for (decltype(count) i = 0; i < count; i++)
				m_Indices.push_back(i);
		}
		else {
			auto count = m_Items.size();
			for (decltype(count) i = 0; i < count; i++)
				if (predicate(m_Items[i]))
					m_Indices.push_back(i);
		}
	}

private:
	std::vector<T> m_Items;
	std::vector<size_t> m_Indices;
};

