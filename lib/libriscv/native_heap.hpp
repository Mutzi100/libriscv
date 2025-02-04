//
// C++ Header-Only Separate Address-Space Allocator
// by fwsGonzo, originally based on allocator written in C by Snaipe
//
#pragma once
#include "common.hpp"
#include <cstddef>
#include <cassert>
#include <deque>
#include <functional>

namespace riscv
{
struct Arena;

struct ArenaChunk
{
	using PointerType = uint32_t;

	ArenaChunk() = default;
	ArenaChunk(ArenaChunk* n, ArenaChunk* p, size_t s, bool f, PointerType d)
		: next(n), prev(p), size(s), free(f), data(d) {}

	ArenaChunk* next = nullptr;
	ArenaChunk* prev = nullptr;
	size_t size = 0;
	bool   free = false;
	PointerType data = 0;

	ArenaChunk* find(PointerType ptr);
	ArenaChunk* find_free(size_t size);
	void   merge_next(Arena&);
	void   split_next(Arena&, size_t size);
};

struct Arena
{
	using PointerType = ArenaChunk::PointerType;
	Arena(PointerType base, PointerType end);

	PointerType malloc(size_t size);
	size_t      size(PointerType src, bool allow_free = false);
	signed int  free(PointerType);

	size_t bytes_free() const;
	size_t bytes_used() const;
	size_t chunks_used() const noexcept { return m_chunks.size(); }

	void transfer(Arena& dest) const;

	inline ArenaChunk& base_chunk() {
		return m_base_chunk;
	}
	template <typename... Args>
	ArenaChunk* new_chunk(Args&&... args);
	void   free_chunk(ArenaChunk*);
	ArenaChunk* find_chunk(PointerType ptr);

private:
	inline size_t word_align(size_t size) {
		return (size + (sizeof(size_t) - 1)) & ~(sizeof(size_t) - 1);
	}
	void foreach(std::function<void(const ArenaChunk&)>) const;

	std::deque<ArenaChunk> m_chunks;
	std::vector<ArenaChunk*> m_free_chunks;
	ArenaChunk  m_base_chunk;
};

// find exact free chunk that matches ptr
inline ArenaChunk* ArenaChunk::find(PointerType ptr)
{
	ArenaChunk* ch = this;
	while (ch != nullptr) {
		if (!ch->free && ch->data == ptr)
			return ch;
		ch = ch->next;
	}
	return nullptr;
}
// find free chunk that has at least given size
inline ArenaChunk* ArenaChunk::find_free(size_t size)
{
    ArenaChunk* ch = this;
	while (ch != nullptr) {
		if (ch->free && ch->size >= size)
			return ch;
		ch = ch->next;
	}
	return nullptr;
}
// merge this and next into this chunk
inline void ArenaChunk::merge_next(Arena& arena)
{
	ArenaChunk* freech = this->next;
	this->size += freech->size;
	this->next = freech->next;
	if (this->next) {
		this->next->prev = this;
	}
	arena.free_chunk(freech);
}

inline void ArenaChunk::split_next(Arena& arena, size_t size)
{
	ArenaChunk* newch = arena.new_chunk(
		this->next,
		this,
		this->size - size,
		true,
		this->data + (PointerType) size
	);
	if (this->next) {
		this->next->prev = newch;
	}
	this->next = newch;
	this->size = size;
}

template <typename... Args>
inline ArenaChunk* Arena::new_chunk(Args&&... args)
{
	if (UNLIKELY(m_free_chunks.empty())) {
		m_chunks.emplace_back(std::forward<Args>(args)...);
		return &m_chunks.back();
	}
	auto* chunk = m_free_chunks.back();
	m_free_chunks.pop_back();
	return new (chunk) ArenaChunk {std::forward<Args>(args)...};
}
inline void Arena::free_chunk(ArenaChunk* chunk)
{
	m_free_chunks.push_back(chunk);
}
inline ArenaChunk* Arena::find_chunk(PointerType ptr)
{
	for (auto& chunk : m_chunks) {
		if (!chunk.free && chunk.data == ptr)
			return &chunk;
	}
	return nullptr;
}

inline Arena::PointerType Arena::malloc(size_t size)
{
	size_t length = word_align(size);
	length = std::max(length, (size_t) 8);
	ArenaChunk* ch = base_chunk().find_free(length);

	if (ch != nullptr) {
		ch->split_next(*this, length);
		ch->free = false;
		return ch->data;
	}
	return 0;
}

inline size_t Arena::size(PointerType ptr, bool allow_free)
{
	ArenaChunk* ch = base_chunk().find(ptr);
	if (UNLIKELY(ch == nullptr || (ch->free && !allow_free)))
		return 0;
	return ch->size;
}

inline int Arena::free(PointerType ptr)
{
	ArenaChunk* ch = base_chunk().find(ptr);
	if (UNLIKELY(ch == nullptr))
		return -1;

	ch->free = true;
	// merge chunks ahead and behind us
	if (ch->next && ch->next->free) {
		ch->merge_next(*this);
	}
	if (ch->prev && ch->prev->free) {
		ch = ch->prev;
		ch->merge_next(*this);
	}
	return 0;
}

inline Arena::Arena(PointerType arena_base, PointerType arena_end)
{
	m_base_chunk.size = arena_end - arena_base;
	m_base_chunk.data = arena_base;
	m_base_chunk.free = true;
}

inline void Arena::foreach(std::function<void(const ArenaChunk&)> callback) const
{
	const ArenaChunk* ch = &this->m_base_chunk;
    while (ch != nullptr) {
		callback(*ch);
		ch = ch->next;
	}
}

inline size_t Arena::bytes_free() const
{
	size_t size = 0;
	foreach([&size] (const ArenaChunk& chunk) {
		if (chunk.free) size += chunk.size;
	});
	return size;
}
inline size_t Arena::bytes_used() const
{
	size_t size = 0;
	foreach([&size] (const ArenaChunk& chunk) {
		if (!chunk.free) size += chunk.size;
	});
	return size;
}

inline void Arena::transfer(Arena& dest) const
{
	dest.m_base_chunk = m_base_chunk;
	dest.m_chunks.clear();
	dest.m_free_chunks.clear();

	ArenaChunk* last = &dest.m_base_chunk;

	const ArenaChunk* chunk = m_base_chunk.next;
	while (chunk != nullptr)
	{
		dest.m_chunks.push_back(*chunk);
		auto& new_chunk = dest.m_chunks.back();
		new_chunk.prev = last;
		new_chunk.next = nullptr;
		last->next = &new_chunk;
		/* New last before next iteration */
		last = &new_chunk;

		chunk = chunk->next;
	}
}

} // namespace riscv
