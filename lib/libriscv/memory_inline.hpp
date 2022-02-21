#pragma once

template <int W>
template <typename T> inline
T Memory<W>::read(address_t address)
{
	// The read/write caches are on the CPUs
#ifdef RISCV_MULTIPROCESS
	const auto& page = machine().is_multiprocessing()
			? machine().cpu.get_readable_page(address)
			: this->get_readable_page(address);
#else
	const auto& page = this->get_readable_page(address);
#endif

#ifdef RISCV_PAGE_TRAPS_ENABLED
	if constexpr (memory_traps_enabled) {
		if (UNLIKELY(page.has_trap())) {
			return page.trap(address & (Page::size()-1), sizeof(T) | TRAP_READ, 0);
		}
	}
#endif
	return page.template aligned_read<T>(address & (Page::size()-1));
}

template <int W>
template <typename T> inline
T& Memory<W>::writable_read(address_t address)
{
	// The read/write caches are on the CPUs
#ifdef RISCV_MULTIPROCESS
	auto& page = machine().is_multiprocessing()
			? machine().cpu.get_writable_page(address)
			: this->get_writable_page(address);
#else
	auto& page = this->get_writable_page(address);
#endif

#ifdef RISCV_PAGE_TRAPS_ENABLED
	if constexpr (memory_traps_enabled) {
		if (UNLIKELY(page.has_trap())) {
			page.trap(address & (Page::size()-1), sizeof(T) | TRAP_WRITE, value);
			return;
		}
	}
#endif
	return page.template aligned_read<T>(address & (Page::size()-1));
}

template <int W>
template <typename T> inline
void Memory<W>::write(address_t address, T value)
{
#ifdef RISCV_MULTIPROCESS
	auto& page = machine().is_multiprocessing()
			? machine().cpu.get_writable_page(address)
			: this->get_writable_page(address);
#else
	auto& page = this->get_writable_page(address);
#endif

#ifdef RISCV_PAGE_TRAPS_ENABLED
	if constexpr (memory_traps_enabled) {
		if (UNLIKELY(page.has_trap())) {
			page.trap(address & (Page::size()-1), sizeof(T) | TRAP_WRITE, value);
			return;
		}
	}
#endif
	page.template aligned_write<T>(address & (Page::size()-1), value);
}

template <int W>
inline const Page& Memory<W>::get_page(const address_t address) const noexcept
{
	const auto page = page_number(address);
	return get_pageno(page);
}

template <int W>
inline const Page& Memory<W>::get_exec_pageno(const address_t pageno) const
{
#ifdef RISCV_RODATA_SEGMENT_IS_SHARED
	if (m_ropages.contains(pageno)) {
		return m_ropages.pages[pageno - m_ropages.begin];
	}
#endif
	auto it = m_pages.find(pageno);
	if (LIKELY(it != m_pages.end())) {
		return it->second;
	}
	CPU<W>::trigger_exception(EXECUTION_SPACE_PROTECTION_FAULT);
}

template <int W>
inline const Page& Memory<W>::get_pageno(const address_t pageno) const noexcept
{
#ifdef RISCV_RODATA_SEGMENT_IS_SHARED
	if (m_ropages.contains(pageno)) {
		return m_ropages.pages[pageno - m_ropages.begin];
	}
#endif
	auto it = m_pages.find(pageno);
	if (LIKELY(it != m_pages.end())) {
		return it->second;
	}
	return get_pageno_slowpath(pageno);
}

template <int W> inline void
Memory<W>::invalidate_cache(address_t pageno, Page* page)
{
	// It is possible to keep the write page as long as the
	// page tables are node-based. In that case, we only have
	// to invalidate the read page when it matches.
	if (m_rd_cache.pageno == pageno) {
		m_rd_cache.page = page;
	}
}

template <int W>
template <typename... Args> inline
Page& Memory<W>::allocate_page(const address_t page, Args&&... args)
{
	const auto it = m_pages.emplace(std::piecewise_construct,
		std::forward_as_tuple(page),
		std::forward_as_tuple(std::forward<Args> (args)...)
	);
	// invalidate all cached pages, because references are invalidated
	// prediction: we are going to use this page for reading and writing
	this->invalidate_cache(page, &it.first->second);
	// return new page
	return it.first->second;
}

template <int W>
inline size_t Memory<W>::owned_pages_active() const noexcept
{
	size_t count = 0;
	for (const auto& it : m_pages) {
		if (!it.second.attr.non_owning) count++;
	}
	return count;
}

template <int W>
inline void Memory<W>::trap(address_t page_addr, mmio_cb_t callback)
{
#ifdef RISCV_PAGE_TRAPS_ENABLED
	auto& page = create_page(page_number(page_addr));
	page.set_trap(callback);
#else
	(void) page_addr;
	(void) callback;
#endif
}

template <int W>
inline address_type<W> Memory<W>::resolve_address(const std::string& name) const
{
	auto* sym = resolve_symbol(name.c_str());
	return (sym) ? sym->st_value : 0x0;
}

template <int W>
inline address_type<W> Memory<W>::resolve_section(const char* name) const
{
	auto* shdr = this->section_by_name(name);
	if (shdr) return shdr->sh_addr;
	return 0x0;
}

template <int W>
inline address_type<W> Memory<W>::exit_address() const noexcept
{
	return this->m_exit_address;
}

template <int W>
inline void Memory<W>::set_exit_address(address_t addr)
{
	this->m_exit_address = addr;
}

template <int W>
inline bool Memory<W>::is_executable(address_t addr)
{
	return addr >= m_exec_pagedata_base && addr < m_exec_pagedata_base + m_exec_pagedata_size;
}
