#include "machine.hh"
extern "C" {
#include "m68k.h"
#include "z80.h"
}
#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace asm_test {
    std::string const& dependency_versions() {
        static std::string const versions =
#include "dependency_versions.inc"
                ;
        return versions;
    }

    namespace {
        enum class kind {
            code,
            payload,
            output,
            stack,
            scratch,
            module_buffer
        };

        struct region {
            uint32_t             start;
            kind                 type;
            std::vector<uint8_t> data;
            std::vector<bool>    written;
        };

        struct access {
            uint32_t pc = 0, address = 0;
            unsigned width = 0;
            bool     write = false;
        };

        struct memory {
            std::vector<region>      regions;
            settings                 config;
            result                   state;
            cpu                      architecture;
            uint32_t                 pc = 0;
            std::array<access, 16>   recent{};
            size_t                   accesses = 0;
            std::vector<uint32_t>    patch_words;
            bool                     measuring       = false;
            image const*             program         = nullptr;
            uint32_t                 dma_source      = 0;
            uint16_t                 dma_destination = 0, dma_words = 0;
            std::deque<dma_transfer> pending_dma;
            std::vector<bool>        allowed_vram;
            size_t                   buffer_high_water = 0, flushes = 0;

            void add(uint32_t start, size_t size, kind type) {
                auto const end = uint64_t(start) + size;
                if (end > (architecture == cpu::m68000 ? 0x1000000ULL : 0x10000ULL)) {
                    throw std::runtime_error("Machine region exceeds its address bus");
                }
                for (auto const& r : regions) {
                    if (size && !r.data.empty()
                        && start < uint64_t(r.start) + r.data.size() && r.start < end) {
                        throw std::runtime_error("Overlapping machine regions");
                    }
                }
                regions.push_back(
                        {start, type, std::vector<uint8_t>(size, 0xa5),
                         std::vector<bool>(size)});
            }

            void fail(std::string const& message) {
                if (state.diagnostic.empty()) {
                    state.diagnostic = message;
                }
            }

            region* locate(uint32_t address, unsigned width, bool write) {
                if (!state.diagnostic.empty()) {
                    return nullptr;
                }
                recent[accesses++ % recent.size()] = {pc, address, width, write};
                if (architecture == cpu::m68000 && width > 1 && (address & 1)) {
                    fail("unaligned 68000 access");
                    return nullptr;
                }
                for (auto& r : regions) {
                    if (address >= r.start
                        && uint64_t(address) + width
                                   <= uint64_t(r.start) + r.data.size()) {
                        bool code_write = config.self_modifying;
                        if (write && r.type == kind::code) {
                            for (unsigned i = 0; i < width; ++i) {
                                auto const byte = address + i;
                                code_write
                                        = code_write
                                          && std::any_of(
                                                  patch_words.begin(), patch_words.end(),
                                                  [byte](uint32_t word) {
                                                      return byte == word
                                                             || byte == word + 1;
                                                  });
                            }
                        }
                        if (write
                            && (r.type == kind::payload
                                || (r.type == kind::code && !code_write))) {
                            fail("write to read-only memory");
                            return nullptr;
                        }
                        if (!write
                            && (r.type == kind::output || r.type == kind::stack
                                || r.type == kind::module_buffer)) {
                            for (unsigned i = 0; i < width; ++i) {
                                if (!r.written[address - r.start + i]) {
                                    fail("read from unwritten output/stack");
                                    return nullptr;
                                }
                            }
                        }
                        return &r;
                    }
                }
                fail(write ? "write outside mapped region/output guard"
                           : "read outside mapped regions/payload guard");
                return nullptr;
            }

            uint32_t read(uint32_t address, unsigned width) {
                if (architecture == cpu::m68000) {
                    address &= 0xffffff;
                }
                auto* r = locate(address, width, false);
                if (!r) {
                    return 0;
                }
                if (measuring && architecture == cpu::m68000) {
                    state.bus_reads += (width + 1U) / 2U;
                }
                if (r->type == kind::payload) {
                    state.input_high_water = std::max(
                            state.input_high_water, size_t(address - r->start + width));
                }
                uint32_t value = 0;
                for (unsigned i = 0; i < width; ++i) {
                    value = (value << 8) | r->data[address - r->start + i];
                }
                return value;
            }

            void write(uint32_t address, unsigned width, uint32_t value) {
                if (architecture == cpu::m68000) {
                    address &= 0xffffff;
                }
                if (config.modules && address >= 0xe00000 && address < 0xe00010) {
                    if (measuring) {
                        state.bus_writes += (width + 1U) / 2U;
                    }
                    device_write(address, width, value);
                    return;
                }
                auto* r = locate(address, width, true);
                if (!r) {
                    return;
                }
                if (measuring && architecture == cpu::m68000) {
                    state.bus_writes += (width + 1U) / 2U;
                }
                if (r->type == kind::module_buffer && !pending_dma.empty()) {
                    fail("DMA buffer overwritten before drain");
                    return;
                }
                if (config.modules && address == 0xff8054 && width == 4) {
                    state.bookmarks.push_back(value);
                }
                for (unsigned i = 0; i < width; ++i) {
                    auto const offset = address - r->start + i;
                    if (r->type == kind::output && !r->written[offset]) {
                        ++state.bytes_written;
                    }
                    if (r->type == kind::output) {
                        state.output_high_water
                                = std::max(state.output_high_water, size_t(offset + 1));
                    }
                    if (r->type == kind::module_buffer) {
                        buffer_high_water
                                = std::max(buffer_high_water, size_t(offset + 1));
                    }
                    r->data[offset]
                            = static_cast<uint8_t>(value >> (8 * (width - i - 1)));
                    r->written[offset] = true;
                }
            }

            void device_write(uint32_t address, unsigned width, uint32_t value) {
                if (!state.diagnostic.empty()) {
                    return;
                }
                if (address == 0xe00000 && width == 4) {
                    dma_source = value;
                    return;
                }
                if (address == 0xe00004 && width == 2) {
                    dma_destination = static_cast<uint16_t>(value);
                    return;
                }
                if (address == 0xe00006 && width == 2) {
                    dma_words = static_cast<uint16_t>(value);
                    return;
                }
                if (address == 0xe00008 && width == 2 && value == 1) {
                    auto const source
                            = (program->dma_source_words ? dma_source << 1 : dma_source)
                              & 0xffffffU;
                    if ((m68k_get_reg(nullptr, M68K_REG_SR) & 0x700) != 0x700) {
                        fail("DMA insertion without masked interrupts");
                        return;
                    }
                    if (source != 0xff9000 || !dma_words || dma_words > 0x800
                        || (dma_destination & 1)) {
                        fail("Invalid DMA descriptor");
                        return;
                    }
                    if (buffer_high_water != size_t(dma_words) * 2) {
                        fail("DMA size differs from decompressed module extent");
                        return;
                    }
                    if (!pending_dma.empty()) {
                        fail("Shared DMA buffer queued twice before drain");
                        return;
                    }
                    pending_dma.push_back({source, dma_destination, dma_words});
                    return;
                }
                if (address == 0xe0000a && width == 2 && value == 1) {
                    if (++flushes > config.modules->frame_limit) {
                        fail("Module frame budget exhausted");
                        return;
                    }
                    if (flushes <= config.modules->defer_dma_flushes) {
                        return;
                    }
                    auto const was_measuring = measuring;
                    auto const drained       = !pending_dma.empty();
                    measuring = false;    // DMA memory reads are not CPU bus cycles.
                    for (auto const& dma : pending_dma) {
                        for (unsigned i = 0; i < unsigned(dma.words) * 2; ++i) {
                            auto const destination
                                    = (unsigned(dma.destination) + i) & 0xffffU;
                            if (!allowed_vram[destination]) {
                                fail("DMA outside declared VRAM range");
                                break;
                            }
                            state.vram[destination]
                                    = static_cast<uint8_t>(read(dma.source + i, 1));
                        }
                        state.dma.push_back(dma);
                    }
                    measuring = was_measuring;
                    pending_dma.clear();
                    if (drained && buffer_high_water) {
                        for (auto& r : regions) {
                            if (r.type == kind::module_buffer) {
                                std::fill(r.written.begin(), r.written.end(), false);
                                std::fill(r.data.begin(), r.data.end(), uint8_t{0xa5});
                            }
                        }
                        buffer_high_water = 0;
                    }
                    return;
                }
                fail("Unexpected test DMA device access");
            }

            uint16_t module_queue_count() const {
                for (auto const& r : regions) {
                    if (r.start == 0xff8000) {
                        return static_cast<uint16_t>(
                                (unsigned(r.data[0x50]) << 8) | r.data[0x51]);
                    }
                }
                return 0;
            }

            void dump(std::string const& registers) {
                std::ostringstream out;
                out << dependency_versions() << '\n'
                    << state.diagnostic << " PC=0x" << std::hex << pc << ' ' << registers
                    << "\nrecent accesses:";
                auto const first
                        = accesses > recent.size() ? accesses - recent.size() : 0;
                for (size_t i = first; i < accesses; ++i) {
                    auto const& a = recent[i % recent.size()];
                    out << "\n  pc=" << a.pc << (a.write ? " W" : " R") << a.width << " @"
                        << a.address;
                }
                state.diagnostic = out.str();
            }
        };

        memory*    active = nullptr;
        std::mutex machine_mutex;

        uint8_t zread(void* context, uint16_t address) {
            return static_cast<uint8_t>(static_cast<memory*>(context)->read(address, 1));
        }

        void zwrite(void* context, uint16_t address, uint8_t value) {
            static_cast<memory*>(context)->write(address, 1, value);
        }

        uint8_t port_in(z80* z, uint8_t) {
            static_cast<memory*>(z->userdata)->fail("unexpected Z80 port read");
            return 0xff;
        }

        void port_out(z80* z, uint8_t, uint8_t) {
            static_cast<memory*>(z->userdata)->fail("unexpected Z80 port write");
        }
    }    // namespace

    image load_image(
            std::filesystem::path const& path, cpu architecture, bool self_modifying) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            throw std::runtime_error("Cannot read assembly image: " + path.string());
        }
        image program{};
        program.architecture = architecture;
        for (std::istreambuf_iterator<char> it(input), end; it != end; ++it) {
            program.bytes.push_back(static_cast<uint8_t>(*it));
        }
        auto const     width          = architecture == cpu::m68000 ? 4U : 2U;
        auto const     magic          = architecture == cpu::m68000 ? "MD68" : "MDZ8";
        constexpr char module_magic[] = "MDM6";
        program.moduled
                = architecture == cpu::m68000 && program.bytes.size() >= 32
                  && std::equal(module_magic, module_magic + 4, program.bytes.begin());
        if (program.bytes.size() < 4 + 3 * width
            || (!program.moduled
                && !std::equal(magic, magic + 4, program.bytes.begin()))) {
            throw std::runtime_error("Invalid assembly image header: " + path.string());
        }
        auto field = [&](unsigned index) {
            uint32_t value = 0;
            for (unsigned i = 0; i < width; ++i) {
                auto const shift = architecture == cpu::m68000 ? width - i - 1 : i;
                value |= uint32_t(program.bytes[4 + width * index + i]) << (8 * shift);
            }
            return value;
        };
        program.entry      = field(0);
        program.payload    = field(1);
        program.completion = field(2);
        if (program.moduled) {
            program.interrupt_entry  = field(3);
            program.decoder_begin    = field(4);
            program.decoder_end      = field(5);
            program.dma_source_words = field(6) != 0;
            for (auto address :
                 {program.interrupt_entry, program.decoder_begin, program.decoder_end}) {
                if (address < program.origin
                    || address >= program.origin + program.bytes.size()) {
                    throw std::runtime_error("Module export outside image");
                }
            }
            if (program.decoder_begin >= program.decoder_end) {
                throw std::runtime_error("Invalid module decoder range");
            }
        }
        if (self_modifying) {
            if (architecture != cpu::z80 || program.bytes.size() < 14) {
                throw std::runtime_error("Missing Z80 self-modification exports");
            }
            for (unsigned index : {3U, 4U}) {
                auto const address = field(index);
                if (address < program.origin
                    || address + 2 > program.origin + program.bytes.size()) {
                    throw std::runtime_error("Self-modification export outside image");
                }
                program.patch_words.push_back(address);
            }
        }
        if (program.entry < program.origin
            || program.entry >= program.origin + program.bytes.size()) {
            throw std::runtime_error("Assembly entry is outside its image");
        }
        return program;
    }

    result execute(
            image const& program, std::string const& input, settings const& config) {
        std::lock_guard lock(
                machine_mutex);    // Musashi has a process-global CPU context.
        memory mem;
        mem.architecture = program.architecture;
        mem.config       = config;
        mem.patch_words  = program.patch_words;
        mem.program      = &program;
        if (program.moduled != bool(config.modules)) {
            throw std::runtime_error(
                    "Module image requires module machine configuration");
        }
        if (config.modules) {
            mem.state.vram.assign(0x10000, 0xa5);
            mem.allowed_vram.assign(0x10000, false);
            for (auto const& range : config.modules->ranges) {
                if (range.bytes > 0x10000) {
                    throw std::runtime_error("VRAM range exceeds address space");
                }
                for (size_t i = 0; i < range.bytes; ++i) {
                    mem.allowed_vram[(range.address + i) & 0xffff] = true;
                }
            }
        }
        auto const output      = program.architecture == cpu::m68000 ? 0x40000U : 0x8000U;
        auto const stack       = program.architecture == cpu::m68000 ? 0xf0000U : 0xe000U;
        auto const input_limit = program.architecture == cpu::z80 ? 0x4000U : 0x20000U;
        if (config.stack_scratch_bytes > 0xffc || config.readable_padding > input_limit
            || input.size() > input_limit - config.readable_padding
            || config.output_size
                       > (program.architecture == cpu::z80 ? 0x6000U : 0x80000U)) {
            throw std::runtime_error("Assembly case exceeds machine layout");
        }
        mem.add(program.origin, program.bytes.size(), kind::code);
        mem.regions.back().data = program.bytes;
        mem.add(program.payload, input.size() + config.readable_padding, kind::payload);
        std::transform(
                input.begin(), input.end(), mem.regions.back().data.begin(),
                [](char byte) {
                    return static_cast<uint8_t>(byte);
                });
        std::fill(
                mem.regions.back().data.begin()
                        + static_cast<std::ptrdiff_t>(input.size()),
                mem.regions.back().data.end(), uint8_t{0});
        mem.add(output, config.output_size, kind::output);
        mem.add(stack, 0x1000, kind::stack);
        if (program.architecture == cpu::m68000) {
            mem.add(0xff8000, config.modules ? 0x100 : 0x1000, kind::scratch);
            if (config.modules) {
                mem.add(0xff9000, 0x1000, kind::module_buffer);
                mem.add(0xffa000, 0x1000, kind::scratch);
            }
            mem.add(0, config.modules ? 0x400 : 8,
                    kind::code);    // Read-only reset vectors for a fresh CPU each run.
            std::fill(
                    mem.regions.back().data.begin(), mem.regions.back().data.end(),
                    uint8_t{0});
            for (unsigned i = 0; i < 4; ++i) {
                mem.regions.back().data[i]
                        = static_cast<uint8_t>((stack + 0x1000) >> (24 - 8 * i));
                mem.regions.back().data[i + 4]
                        = static_cast<uint8_t>(program.entry >> (24 - 8 * i));
            }
            if (config.modules) {
                for (unsigned i = 0; i < 4; ++i) {
                    mem.regions.back().data[0x78 + i] = static_cast<uint8_t>(
                            program.interrupt_entry >> (24 - 8 * i));
                }
            }
        }
        active = &mem;
        z80 z{};
        if (program.architecture == cpu::m68000) {
            m68k_init();
            m68k_set_cpu_type(M68K_CPU_TYPE_68000);
            m68k_pulse_reset();
            (void)m68k_execute(
                    0);    // Consume reset cycles without counting a decoder instruction.
            for (unsigned i = 0; i < 16; ++i) {
                m68k_set_reg(static_cast<m68k_register_t>(M68K_REG_D0 + i), 0);
            }
            m68k_set_reg(M68K_REG_SR, 0x2700);
            m68k_set_reg(M68K_REG_SP, stack + 0x1000 - 4);
            mem.write(stack + 0x1000 - 4, 4, program.completion);
            m68k_set_reg(M68K_REG_A0, program.payload);
            m68k_set_reg(M68K_REG_A1, output);
            m68k_set_reg(
                    M68K_REG_A4, output);    // Original Nemesis RAM entry convention.
            m68k_set_reg(M68K_REG_PC, program.entry);
            m68k_set_reg(M68K_REG_D0, config.art_base);
            if (config.body_size) {
                m68k_set_reg(M68K_REG_D6, *config.body_size);
            }
        } else {
            z80_init(&z);
            z.read_byte  = zread;
            z.write_byte = zwrite;
            z.port_in    = port_in;
            z.port_out   = port_out;
            z.userdata   = &mem;
            z.pc         = static_cast<uint16_t>(program.entry);
            z.sp         = static_cast<uint16_t>(stack + 0x1000 - 2);
            z.h          = static_cast<uint8_t>(program.payload >> 8);
            z.l          = static_cast<uint8_t>(program.payload);
            z.d          = static_cast<uint8_t>(output >> 8);
            z.e          = static_cast<uint8_t>(output);
            mem.write(z.sp, 1, program.completion & 255);
            mem.write(z.sp + 1, 1, program.completion >> 8);
        }
        auto const initial_sp
                = stack + 0x1000 - (program.architecture == cpu::m68000 ? 4U : 2U);
        for (size_t i = 0; i < config.stack_scratch_bytes; ++i) {
            mem.write(
                    initial_sp - 1U - static_cast<uint32_t>(i), 1,
                    config.stack_scratch_fill);
        }
        // Reset vectors and the host-installed return sentinel are setup, not
        // decoder traffic. The final emulated RET/RTS is included.
        mem.measuring = true;
        while (mem.state.diagnostic.empty()) {
            mem.pc = program.architecture == cpu::m68000
                             ? m68k_get_reg(nullptr, M68K_REG_PC)
                             : z.pc;
            if (mem.pc == program.completion) {
                auto const sp = program.architecture == cpu::m68000
                                        ? m68k_get_reg(nullptr, M68K_REG_SP)
                                        : z.sp;
                if (sp != stack + 0x1000) {
                    mem.fail("returned with an unbalanced stack");
                } else {
                    mem.state.completed = true;
                    if (config.modules && !mem.pending_dma.empty()) {
                        mem.state.completed = false;
                        mem.fail("Returned with pending DMA");
                    }
                }
                break;
            }
            if (mem.pc < program.origin
                || mem.pc >= program.origin + program.bytes.size()) {
                mem.fail("execution outside decoder image (exception/bad return)");
                break;
            }
            if (mem.state.instructions >= config.instruction_limit
                || mem.state.cycles >= config.cycle_limit) {
                mem.fail("execution budget exhausted");
                break;
            }
            ++mem.state.instructions;
            if (program.architecture == cpu::m68000) {
                bool const inside_decoder
                        = mem.pc >= program.decoder_begin && mem.pc < program.decoder_end;
                bool const candidate
                        = config.modules
                          && (config.modules->interrupt_outside_decoder
                                      ? !inside_decoder
                                                && (m68k_get_reg(nullptr, M68K_REG_SR)
                                                    & 0x700)
                                                           < 0x600
                                      : inside_decoder
                                                && (mem.module_queue_count() & 0x8000));
                if (candidate) {
                    // Counter/trace probes are host observations, not bus traffic.
                    ++mem.state.decoder_visits;
                    if (config.modules->trace_decoder_pcs) {
                        mem.state.decoder_pcs.push_back(mem.pc);
                    }
                    auto const first  = config.modules->interrupt_after;
                    auto const period = config.modules->interrupt_period;
                    auto const visit  = mem.state.decoder_visits;
                    if (first && visit >= first
                        && (visit == first || (period && (visit - first) % period == 0))
                        && mem.state.interrupted_pcs.size()
                                   < config.modules->max_interrupts
                        && (m68k_get_reg(nullptr, M68K_REG_SR) & 0x700) < 0x600) {
                        mem.state.interrupted_pcs.push_back(mem.pc);
                        m68k_set_irq(
                                6);    // The pinned core auto-acknowledges/clears it.
                    }
                }
                mem.state.cycles += static_cast<unsigned>(m68k_execute(1));
            } else {
                auto const before = z.cyc;
                z80_step(&z);
                mem.state.cycles += z.cyc - before;
                if (z.halted) {
                    mem.fail("unexpected Z80 HALT");
                }
            }
        }
        for (auto const& r : mem.regions) {
            if (config.modules && r.start == 0xff8000) {
                mem.state.control_ram = r.data;
            }
            if (r.type == kind::output) {
                auto const size = config.exact_output_size ? r.data.size()
                                                           : mem.state.output_high_water;
                mem.state.bytes.assign(
                        r.data.begin(),
                        r.data.begin() + static_cast<std::ptrdiff_t>(size));
            }
        }
        auto const required_output = config.exact_output_size
                                             ? config.output_size
                                             : mem.state.output_high_water;
        if (mem.state.completed && mem.state.bytes_written != required_output) {
            mem.state.completed = false;
            mem.fail("returned with incomplete output");
        }
        if (!mem.state.diagnostic.empty()) {
            std::ostringstream registers;
            registers << std::hex;
            if (program.architecture == cpu::m68000) {
                for (unsigned i = 0; i < 16; ++i) {
                    registers << (i < 8 ? 'D' : 'A') << (i % 8) << '='
                              << m68k_get_reg(
                                         nullptr,
                                         static_cast<m68k_register_t>(M68K_REG_D0 + i))
                              << ' ';
                }
                registers << "SR=" << m68k_get_reg(nullptr, M68K_REG_SR);
            } else {
                registers << "SP=" << z.sp << " HL=" << ((z.h << 8) | z.l)
                          << " DE=" << ((z.d << 8) | z.e) << " BC=" << ((z.b << 8) | z.c)
                          << " A=" << unsigned(z.a) << " IX=" << z.ix << " IY=" << z.iy
                          << " CF=" << z.cf << " ZF=" << z.zf;
            }
            mem.dump(registers.str());
        }
        active = nullptr;
        return mem.state;
    }

    std::string timing(cpu architecture, result const& execution) {
        auto text = std::to_string(execution.cycles);
        if (architecture == cpu::z80) {
            return text + " T-states";
        }
        return text + '(' + std::to_string(execution.bus_reads) + '/'
               + std::to_string(execution.bus_writes) + ')';
    }
}    // namespace asm_test

extern "C" {
unsigned int m68k_read_memory_8(unsigned int address) {
    return asm_test::active->read(address, 1);
}

unsigned int m68k_read_memory_16(unsigned int address) {
    return asm_test::active->read(address, 2);
}

unsigned int m68k_read_memory_32(unsigned int address) {
    return asm_test::active->read(address, 4);
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
    asm_test::active->write(address, 1, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
    asm_test::active->write(address, 2, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
    asm_test::active->write(address, 4, value);
}
}
