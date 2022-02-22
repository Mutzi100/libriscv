#include "threads.hpp"
#include "native_heap.hpp"
#include <cassert>
#include <cstdio>

namespace riscv {
	static const uint32_t STACK_SIZE = 256 * 1024;

template <int W>
void Machine<W>::setup_native_threads(const size_t syscall_base)
{
	this->m_mt.reset(new MultiThreading<W>(*this));

	// 500: microclone
	this->install_syscall_handler(syscall_base+0,
	[] (Machine<W>& machine) {
		const auto stack = (machine.template sysarg<address_type<W>> (0) & ~0xF);
		const auto  func = machine.template sysarg<address_type<W>> (1);
		const auto   tls = machine.template sysarg<address_type<W>> (2);
		const auto flags = machine.template sysarg<uint32_t> (3);
		THPRINT(">>> clone(func=0x%X, stack=0x%X, tls=0x%X)\n",
				func, stack, tls);
		auto* thread = machine.threads().create(
			CHILD_SETTID | flags, tls, 0x0, stack, tls);
		// suspend and store return value for parent: child TID
		auto* parent = machine.threads().get_thread();
		parent->suspend(thread->tid);
		// activate and setup a function call
		thread->activate();
		// the cast is a work-around for a compiler bug
		// NOTE: have to start at DST-4 here!!!
		machine.setup_call(func-4, (const address_type<W>) tls);
	});
	// exit
	this->install_syscall_handler(syscall_base+1,
	[] (Machine<W>& machine) {
		const int status = machine.template sysarg<int> (0);
		THPRINT(">>> Exit on tid=%d, exit status = %d\n",
				machine.threads().get_tid(), (int) status);
		// Exit returns true if the program ended
		if (!machine.threads().get_thread()->exit()) {
			// Should be a new thread now
			return;
		}
		machine.stop();
		machine.set_result(status);
	});
	// sched_yield
	this->install_syscall_handler(syscall_base+2,
	[] (Machine<W>& machine) {
		// begone!
		machine.threads().suspend_and_yield();
	});
	// yield_to
	this->install_syscall_handler(syscall_base+3,
	[] (Machine<W>& machine) {
		machine.threads().yield_to(machine.template sysarg<uint32_t> (0));
	});
	// block (w/reason)
	this->install_syscall_handler(syscall_base+4,
	[] (Machine<W>& machine) {
		// begone!
		if (machine.threads().block(machine.template sysarg<int> (0)))
			return;
		// error, we didn't block
		machine.set_result(-1);
	});
	// unblock (w/reason)
	this->install_syscall_handler(syscall_base+5,
	[] (Machine<W>& machine) {
		if (!machine.threads().wakeup_blocked(machine.template sysarg<int> (0)))
			machine.set_result(-1);
	});
	// unblock thread
	this->install_syscall_handler(syscall_base+6,
	[] (Machine<W>& machine) {
		machine.threads().unblock(machine.template sysarg<int> (0));
	});

	// super fast "direct" threads
	// N+8: clone threadcall
	this->install_syscall_handler(syscall_base+8,
	[] (Machine<W>& machine) {
		// invoke clone threadcall
		const auto tls = machine.arena().malloc(STACK_SIZE);
		if (UNLIKELY(tls == 0)) {
			fprintf(stderr,
				"Error: Thread stack allocation failed: %#x\n", tls);
			machine.set_result(-1);
			return;
		}
		const auto stack = ((tls + STACK_SIZE) & ~0xF);
		const auto  func = machine.template sysarg<address_type<W>> (0);
		const auto  fini = machine.template sysarg<address_type<W>> (1);
		auto* thread = machine.threads().create(
			CHILD_SETTID, tls, 0x0, stack, tls);
		// set PC back to clone point - 4
		machine.cpu.registers().pc =
			machine.cpu.reg(riscv::REG_RA) - 4;
		// suspend and store return value for parent: child TID
		auto* parent = machine.threads().get_thread();
		parent->suspend(thread->tid);
		// activate and setup a function call
		thread->activate();
		// exit into the exit function which frees the thread
		machine.cpu.reg(riscv::REG_RA) = fini;
		// move 6 arguments back
		std::memmove(&machine.cpu.reg(10), &machine.cpu.reg(12),
			6 * sizeof(address_type<W>));
		// geronimo!
		machine.cpu.jump(func - 4);
	});
	// N+9: exit threadcall
	this->install_syscall_handler(syscall_base+9,
	[] (Machine<W>& machine) {
		auto retval = machine.cpu.reg(riscv::REG_RETVAL);
		auto self = machine.cpu.reg(riscv::REG_TP);
		// TODO: check this return value
		machine.arena().free(self);
		// exit thread instead
		machine.threads().get_thread()->exit();
		// return value from exited thread
		machine.set_result(retval);
	});
}

template void Machine<4>::setup_native_threads(const size_t);
template void Machine<8>::setup_native_threads(const size_t);
} // riscv
