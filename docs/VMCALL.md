# Function calls into the guest VM

## Requisites

It's possible to make function calls into the virtual machine, but there are a handful of things that need to be taken care of in order to do that. First off, you should not return from `int main()`, as that will call global destructors which will make the run-time environment unreliable for several programming languages. Instead, one should call `_exit(status)` (or equivalent) from `int main()`, which immediately exits the machine, but does not call global destructors and free resources.

It is not unsafe to call `_exit()` or any other equivalent function directly, as all it does is invoke the EXIT system call, which immediately stops execution of the program. In a normal operating system this also makes the execution environment (usually a process) disappear, and releases all the resources back. In this case we just want to preserve the machine state (which is in a good known state) while also stopping execution, so that we can call into the programs functions directly.

Once the machine is no longer running, but still left in a state in which we can call into it, we have to make sure that our callable function in the Machine is present in the symbol table of the ELF, so that we can find the address of this function.

A third, and final, stumbling block is sometimes having functions, even those marked with `__attribute__((used))`, not appearing in the ELF symbol table, which is a linker issue. This can happen when using `-gc-sections` and friends. You can test if your symbol is visible to the emulator by using `machine.address_of("myFunction")` which returns a memory address. If the address is 0, then the name was not found in the symbol table, which makes `vmcall(...)` impossible to perform. Calling will most likely cause a CPU exception on address 0, which by default has no execute privileges.

Start by running the machine normally and complete `int main()` to make sure global constructors are called and the C run-time environment is fully initialized. So, if you are calling `_exit(0)` from main instead of returning, and not stripping ELF symbols from your binary you are ready to make function calls into the virtual machine.

### Option B: Not exiting main

Another common method is to not exit main at all. Use a function to register handlers by their address, followed by a custom system call that stops the machine directly. At that point you even have access to the original main stack and arguments. Example:

```
void handle_http_get(const char *url) {
	// ...
}
void handle_http_post(const char *url, const uint8_t *data, size_t len) {
	// ...
}


int main()
{
	// do stuff ...

	register_get_handler(handle_http_get);
	register_post_handler(handle_http_post);
	wait_for_requests();
}
```

The register functions can be very simple assembly:

```
inline long register_get_handler(void (*handler) (const char *))
{
	register long a0 asm("a0") = (uintptr_t)handler;
	register long syscall_id asm("a7") = SYSNO_REGISTER_GET;

	asm volatile ("scall" : "+r"(a0) : "r"(syscall_id) : "memory");

	return a0;
}
inline void wait_for_requests()
{
	register long a0 asm("a0");
	register long syscall_id asm("a7") = SYSNO_WAIT_REQUESTS;

	asm volatile ("scall" : "=r"(a0) : "r"(syscall_id));
}
```

The idea is to get the address of the `register_get_handler` function to reference it, which will make sure it is not pruned from the executable. Additionally, we get the address, and we can then remember it outside of the Machine, so that we can call this function when we are receiving a HTTP GET request.

The `wait_for_requests()` function is intended to simply stop the Machine. It is a system call that calls `machine.stop()`, and it is also recommended to set some boolean `initialized` to true, so that you can verify that the guest program actually called `wait_for_requests()`.

For this to work you will need custom system call handlers, one for each function above, which does the above work. The system calls also need to not collide with any Linux system call numbers if you are using that ABI.

## Calling a function in the guest virtual machine

Example which calls the function `test` with the arguments `555` and `666`:
```C++
	int ret = machine.vmcall("test", 555, 666);
	printf("test returned %d\n", ret);
```

Arguments are passed as normal. The function will move integral values and pointers into the integer registers, and floating-point values into the FP registers. Structs and strings will be pushed on stack and then an integer register will hold the pointer, following the RISC-V calling convention.

```C++
	struct PodTest {
		int32_t a = 4;
		int64_t b = 6;
	} test;
	int ret = machine.vmcall("test", "This is a string", test);
	printf("test returned %d\n", ret);
```

You can specify a maximum of 8 integer and 8 floating-point arguments, for a total of 16. Instruction counters and registers are not reset on calling functions, so make sure to take that into consideration when measuring.

It is not recommended to copy data into guest memory and then pass pointers to this data as arguments, as it's a very complex task to determine which memory is unused by the guest before and even during the call. Instead, the guest can allocate room for the struct on its own, and then simply perform a system call where it passes a pointer to the struct as an argument.


## Doing work in-between machine execution

Without using threads the machine program will simply run until it's completed, an exception occurs, or the machine is stopped from the outside during a trap or system call. It would be nice to have the ability to run the host-side program in-between without preemption. We can do this by making vmcall not execute machine instructions, and instead do it ourselves manually:

```C++
// Reset the stack pointer from any previous call to its initial value
machine.cpu.reset_stack_pointer();
// Function call setup for the guest VM, but don't start execution
machine.setup_call("test", 555, 666);
// Run the program for X amount of instructions, then print something, then
// resume execution again. Do this until stopped.
for (;;) {
	try {
		// Execute 1000 instructions at a time
		machine.simulate(1000);
	} catch (riscv::MachineTimeoutException& mte) {
		// Do some work
		printf("Instruction count: %zu\n", (size_t) machine.instruction_counter());
		// Continue running
		continue;
	}
	// Do some work after completion
	printf("Done\n");
	break;
}
```

Note that for the sake of this example we have not wrapped the call to `simulate()` in a try..catch, but if a CPU exception happens, it will throw a `riscv::MachineException`, and possibly exceptions from your own system call handlers.

It is also possible to simulate without timeout exceptions. It is a template parameter that defaults to true. You can compare the max instructions counter to non-zero to see if the machine didn't stop from a call to `machine.stop()`.

## Maximizing success and optimizing calls

Check that the symbol exists using the `Machine::address_of()` function. It will cache the address anyway, and be re-used by the vmcall function. If you are going to use `-gc-sections` then you should know about the linker argument `--undefined=symbolname` to make sure the symbol will never get removed. Also make sure to mark the function as `extern "C"` if you are using C++. You can pass arguments to the linker from the compiler frontend using `-Wl,<linker arg>`. For example `-Wl,--undefined=test` will retain test even through linker GC. To minimize the size of the binaries use `--retain-symbols-file` instead of -s or -S to the linker (see: man ld).

Build your executable with `-O2 -march=rv32g -mabi=ilp32d` or `-O2 -march=rv32imfd -mabi=ilp32` based on measurements. Soft-float is always slower. Accelerate the heap by managing the chunks from the outside using system calls. There is no need for any mmap functionality. Use custom linear arenas for the page hashmap if you require instant machine deletion.

Use `Memory::memview()` or `Memory::memstring()` to handle arguments passed from guest to host. They have fast-paths for strings and structs that don't cross page-boundaries. Use `std::deque` instead of `std::vector` inside the guest where possible. The fastest way to append data to a list is using a `std::array` with a pointer: `*ptr++ = value;`.

You can provide arguments to main with `Machine::setup_argv()`. They are all regular C++ strings, and will be passed into the VM in a way that is understood by normal C runtimes.

It is also important to cache the address of the function, to avoid having to translate the string function name on every call. There are many ways to do this and for example a map<string, address> would work. You can find the address by calling `machine.address_of("myFunction")`.

## Interrupting a running machine

It is possible to interrupt a running machine to perform another task. This can be done using the `Machine::preempt()` function. A machine can also interrupt itself without any issues.
