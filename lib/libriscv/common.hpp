#pragma once
#include <type_traits>
#include <functional>
#include <string>

#ifndef LIKELY
#define LIKELY(x) __builtin_expect((x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect((x), 0)
#endif

#ifndef COLD_PATH
#define COLD_PATH() __attribute__((cold))
#endif

#ifndef RISCV_SYSCALLS_MAX
#define RISCV_SYSCALLS_MAX   384
#endif

#ifndef RISCV_SYSCALL_EBREAK_NR
#define RISCV_SYSCALL_EBREAK_NR    (RISCV_SYSCALLS_MAX-1)
#endif

#ifdef RISCV_DEBUG
# ifndef RISCV_MEMORY_TRAPS_ENABLED
#   define RISCV_MEMORY_TRAPS_ENABLED
# endif
#endif

#ifndef RISCV_RODATA_SEGMENT_IS_SHARED
#define RISCV_RODATA_SEGMENT_IS_SHARED 1
#endif

#if defined(RISCV_INSTR_CACHE) && defined(RISCV_INSTR_CACHE_PREGEN)
// WARNING: this feature disables virtual execute memory
#define RISCV_INBOUND_JUMPS_ONLY
#endif

namespace riscv
{
	static constexpr int SYSCALL_EBREAK = RISCV_SYSCALL_EBREAK_NR;

	static constexpr int PageSize = 4096;

#ifdef RISCV_MEMORY_TRAPS_ENABLED
	static constexpr bool memory_traps_enabled = true;
#else
	static constexpr bool memory_traps_enabled = false;
#endif

#ifdef RISCV_DEBUG
	static constexpr bool debugging_enabled = true;
	static constexpr bool memory_alignment_check = true;
#else
	static constexpr bool debugging_enabled = false;
	static constexpr bool memory_alignment_check = false;
#endif

#ifdef RISCV_EXT_ATOMICS
	static constexpr bool atomics_enabled = true;
#else
	static constexpr bool atomics_enabled = false;
#endif
#ifdef RISCV_EXT_COMPRESSED
	static constexpr bool compressed_enabled = true;
#else
	static constexpr bool compressed_enabled = false;
#endif
#ifdef RISCV_EXT_FLOATS
	static constexpr bool floating_point_enabled = true;
#else
	static constexpr bool floating_point_enabled = false;
#endif
}

namespace riscv
{
	template <int W> struct Memory;
	template <int W> struct MultiThreading;
	template <int W> struct Multiprocessing;
	struct Arena;

	template <int W>
	struct MachineOptions
	{
		uint64_t memory_max = 16ull << 20; // 16mb
		bool load_program = true;
		bool protect_segments = true;
		bool verbose_loader = false;

		std::function<struct Page&(Memory<W>&, size_t)> page_fault_handler = nullptr;
	};

	template <int W>
	struct SerializedMachine;

	template <class...> constexpr std::false_type always_false {};

	template<typename T>
	struct is_string
		: public std::disjunction<
			std::is_same<char *, typename std::decay<T>::type>,
			std::is_same<const char *, typename std::decay<T>::type>
	> {};

	template<class T>
	struct is_stdstring : public std::is_same<T, std::basic_string<char>> {};
}
