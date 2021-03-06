
template <int W>
inline Machine<W>::Machine(const std::vector<uint8_t>& binary, address_t maxmem)
	: cpu(*this), memory(*this, binary, maxmem)
{
	cpu.reset();
}
template <int W>
inline Machine<W>::~Machine()
{
	for (auto& callback : m_destructor_callbacks) callback();
}

template <int W>
inline void Machine<W>::stop(bool v) noexcept {
	m_stopped = v;
}
template <int W>
inline bool Machine<W>::stopped() const noexcept {
	return m_stopped;
}

template <int W>
inline void Machine<W>::simulate(uint64_t max_instr)
{
	this->m_stopped = false;
	if (max_instr != 0) {
		max_instr += cpu.registers().counter;
		while (LIKELY(!this->stopped())) {
			cpu.simulate();
			if (UNLIKELY(cpu.registers().counter >= max_instr)) break;
		}
	}
	else {
		while (LIKELY(!this->stopped())) {
			cpu.simulate();
		}
	}
}

template <int W>
inline void Machine<W>::reset()
{
	cpu.reset();
	memory.reset();
}

template <int W>
inline void Machine<W>::install_syscall_handler(int sysn, syscall_t handler)
{
	m_syscall_handlers.at(sysn) = handler;
}
template <int W> inline
typename Machine<W>::syscall_t Machine<W>::get_syscall_handler(int sysn) {
	return m_syscall_handlers.at(sysn);
}

template <int W>
inline void Machine<W>::system_call(int syscall_number)
{
	if ((size_t) syscall_number < m_syscall_handlers.size())
	{
		auto& handler = m_syscall_handlers[syscall_number];
		if (handler != nullptr)
		{
			address_t ret = handler(*this);
			// EBREAK handler should not modify registers
			if (syscall_number != SYSCALL_EBREAK) {
				cpu.reg(RISCV::REG_RETVAL) = ret;
				if (UNLIKELY(this->verbose_jumps)) {
					printf("SYSCALL %d returned %ld (0x%lX)\n",
							syscall_number, (long) ret, (long) ret);
				}
			}
			return;
		}
	}
	if (throw_on_unhandled_syscall == false)
	{
		if (UNLIKELY(verbose_machine)) {
			fprintf(stderr, ">>> Warning: Unhandled syscall %d\n", syscall_number);
		}
		// EBREAK should not modify registers
		if (syscall_number != SYSCALL_EBREAK) {
			cpu.reg(RISCV::REG_RETVAL) = -ENOSYS;
		}
	}
	else {
		throw MachineException(UNHANDLED_SYSCALL,
								"Unhandled system call", syscall_number);
	}
}

template <int W>
template <typename T>
inline T Machine<W>::sysarg(int idx) const
{
	return static_cast<T> (cpu.reg(RISCV::REG_ARG0 + idx));
}

template <int W>
address_type<W> Machine<W>::copy_to_guest(address_t dst, const void* buf, size_t len)
{
	memory.memcpy(dst, buf, len);
	return dst + len;
}

template <int W>
inline address_type<W> Machine<W>::address_of(const char* name)
{
	return memory.resolve_address(name);
}

template <int W>
address_type<W> Machine<W>::stack_push(const void* data, size_t length)
{
	auto& sp = cpu.reg(RISCV::REG_SP);
	sp = (sp - length) & ~(W-1); // maintain word alignment
	this->copy_to_guest(sp, data, length);
	return sp;
}

template <int W>
void Machine<W>::realign_stack(unsigned align)
{
	address_t align_mask = 15;
	switch (align) {
		case 4:  align_mask = 0x3; break;
		case 8:  align_mask = 0x7; break;
		case 16: align_mask = 0xF; break;
		default: throw MachineException(INVALID_ALIGNMENT, "Invalid alignment", align);
	}
	cpu.reg(RISCV::REG_SP) &= ~align_mask;
}

template <int W>
inline address_type<W> Machine<W>::free_memory() const noexcept
{
	return (memory.pages_total() - memory.pages_active()) * Page::size();
}

template <int W>
inline void Machine<W>::add_destructor_callback(delegate<void()> cb)
{
	m_destructor_callbacks.push_back(std::move(cb));
}

#include "machine_vmcall.hpp"
