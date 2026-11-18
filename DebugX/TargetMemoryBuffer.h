#pragma once

#include <functional>
#include <vector>
#include <BufferManager.h>

//
// What CHexControl edits in the Memory pane: a copy of a chunk of target memory, whose every change is also handed
// to a writer (that writes it to the target). The control changes bytes only through SetData as long as it is not in
// insert mode; the size is fixed -- target memory cannot be inserted into or deleted from -- so Insert and Delete
// refuse, and a write past the end is cut short.
//
class TargetMemoryBuffer final : public IBufferManager {
public:
	// the address the first byte is at, and what is written where
	using Writer = std::function<void(ULONG64 address, std::vector<BYTE> data)>;

	TargetMemoryBuffer(ULONG64 address, std::vector<BYTE> data, Writer writer)
		: m_Address(address), m_Data(std::move(data)), m_Writer(std::move(writer)) {
	}

	ULONG64 GetAddress() const {
		return m_Address;
	}

	uint32_t GetData(int64_t offset, uint8_t* buffer, uint32_t count) override {
		if (offset < 0 || offset >= GetSize())
			return 0;
		count = static_cast<uint32_t>(std::min<int64_t>(count, GetSize() - offset));
		::memcpy(buffer, m_Data.data() + offset, count);
		return count;
	}

	bool SetData(int64_t offset, const uint8_t* data, uint32_t count) override {
		if (offset < 0 || offset >= GetSize())
			return false;
		count = static_cast<uint32_t>(std::min<int64_t>(count, GetSize() - offset));
		if (count == 0 || ::memcmp(m_Data.data() + offset, data, count) == 0)
			return true;
		::memcpy(m_Data.data() + offset, data, count);
		if (m_Writer)
			m_Writer(m_Address + offset, std::vector<BYTE>(data, data + count));
		return true;
	}

	bool SetData(int64_t offset, uint8_t value, uint32_t count) override {
		if (offset < 0 || offset >= GetSize())
			return false;
		count = static_cast<uint32_t>(std::min<int64_t>(count, GetSize() - offset));
		std::vector<uint8_t> fill(count, value);
		return SetData(offset, fill.data(), count);
	}

	bool Insert(int64_t, const uint8_t*, uint32_t) override {
		return false;
	}
	bool Delete(int64_t, size_t) override {
		return false;
	}

	int64_t GetSize() const override {
		return static_cast<int64_t>(m_Data.size());
	}
	uint8_t* GetRawData(int64_t offset) override {
		return m_Data.data() + offset;
	}
	bool IsReadOnly() const override {
		return false;
	}

private:
	ULONG64 m_Address;
	std::vector<BYTE> m_Data;
	Writer m_Writer;
};
