const std = @import("std");
const builtin = std.builtin;
const zee_alloc = @import("zee_alloc");

const Fundude = @import("main.zig");
const util = @import("util.zig");

pub const is_profiling = false;

const allocator = if (builtin.link_libc)
    std.heap.c_allocator
else if (builtin.cpu.arch.isWasm())
blk: {
    (zee_alloc.ExportC{
        .allocator = zee_alloc.ZeeAllocDefaults.wasm_allocator,
        .malloc = true,
        .free = true,
    }).run();
    break :blk zee_alloc.ZeeAllocDefaults.wasm_allocator;
} else {
    @compileError("No allocator found. Did you remember to link libc?");
};

/// Convert a slice into known memory representation -- enables C ABI
pub const U8Chunk = packed struct {
    // TODO: switch floats to ints
    // JS can't handle i64 yet so we're using f64 for now
    // const Int = std.meta.IntType(true, 2 * @bitSizeOf(usize));
    const Float = @Type(builtin.TypeInfo{ .Float = .{ .bits = 2 * @bitSizeOf(usize) } });
    const Abi = if (builtin.cpu.arch.isWasm()) Float else U8Chunk;

    ptr: [*]u8,
    len: usize,

    pub fn toSlice(raw: Abi) []u8 {
        const self = @bitCast(U8Chunk, raw);
        return self.ptr[0..self.len];
    }

    pub fn fromSlice(slice: []u8) Abi {
        const self = U8Chunk{ .ptr = slice.ptr, .len = slice.len };
        return @bitCast(Abi, self);
    }

    pub fn empty() Abi {
        return U8Chunk.fromSlice(&[0]u8{});
    }
};

pub fn MatrixChunk(comptime T: type) type {
    return packed struct {
        const UsizeHalf = std.meta.Int(.signed, @bitSizeOf(usize) / 2);
        const Abi = if (builtin.cpu.arch.isWasm()) U8Chunk.Abi else MatrixChunk(T);

        ptr: [*]T,
        width: UsizeHalf,
        height: UsizeHalf,

        pub fn fromMatrix(matrix: anytype) Abi {
            const M = std.meta.Child(@TypeOf(matrix.ptr));
            if (@sizeOf(M) != @sizeOf(T)) @compileError("Unsupported Matrix type: " ++ @typeName(M));

            const self = MatrixChunk(T){
                .ptr = @ptrCast([*]T, matrix.ptr),
                .width = @intCast(UsizeHalf, matrix.width),
                .height = @intCast(UsizeHalf, matrix.height),
            };
            return @bitCast(Abi, self);
        }

        pub fn toMatrix(raw: Abi) MatrixSlice(T) {
            const self = @bitCast(MatrixChunk(T), raw);
            return .{
                .data = self.ptr[0 .. self.width * self.height],
                .width = self.width,
                .height = self.height,
            };
        }
    };
}

export fn fd_create() ?*Fundude {
    const fd = allocator.create(Fundude) catch return null;
    fd.* = .{};
    return fd;
}

export fn fd_destroy(fd: *Fundude) void {
    fd.deinit(allocator);
    allocator.destroy(fd);
}

export fn fd_load(fd: *Fundude, cart: U8Chunk.Abi) i8 {
    fd.init(allocator, .{ .cart = U8Chunk.toSlice(cart) }) catch |err| return switch (err) {
        error.CartTypeError => 1,
        error.RomSizeError => 2,
        error.RamSizeError => 3,
        error.OutOfMemory => 127,
    };
    return 0;
}

export fn fd_reset(fd: *Fundude) void {
    fd.init(allocator, .{}) catch unreachable;
}

export fn fd_step(fd: *Fundude) i8 {
    return fd.step();
}

export fn fd_step_ms(fd: *Fundude, ms: f64) i32 {
    return fd.step_ms(@floatToInt(i64, ms), is_profiling);
}

export fn fd_step_cycles(fd: *Fundude, cycles: i32) i32 {
    return fd.step_cycles(cycles, is_profiling);
}

export fn fd_rewind(fd: *Fundude) void {
    fd.temportal.rewind(fd);
}

export fn fd_dump(fd: *Fundude) U8Chunk.Abi {
    const mem = allocator.alloc(u8, Fundude.savestate_size) catch return U8Chunk.empty();
    var stream = std.io.fixedBufferStream(mem);
    fd.dump(stream.writer()) catch {
        allocator.free(mem);
        return U8Chunk.empty();
    };
    return U8Chunk.fromSlice(mem);
}

export fn fd_restore(fd: *Fundude, bytes: U8Chunk.Abi) u8 {
    var stream = std.io.fixedBufferStream(U8Chunk.toSlice(bytes));
    fd.validateSavestate(stream.reader()) catch return 1;

    stream.reset();
    fd.restore(stream.reader()) catch unreachable;
    return 0;
}

export fn fd_input_press(fd: *Fundude, input: u8) u8 {
    const changed = fd.inputs.press(&fd.mmu, .{ .raw = input });
    if (changed) {
        fd.cpu.mode = .norm;
        fd.mmu.dyn.io.IF.joypad = true;
    }
    return fd.inputs.raw;
}

export fn fd_input_release(fd: *Fundude, input: u8) u8 {
    _ = fd.inputs.release(&fd.mmu, .{ .raw = input });
    return fd.inputs.raw;
}

export fn fd_disassemble(buffer: *[16]u8, arg0: u8, arg1: u8, arg2: u8) U8Chunk.Abi {
    const op = Fundude.Cpu.Op.decode(.{ arg0, arg1, arg2 });
    return U8Chunk.fromSlice(op.disassemble(buffer) catch unreachable);
}

export fn fd_instr_len(arg0: u8) usize {
    const op = Fundude.Cpu.Op.decode(.{ arg0, 0, 0 });
    return op.length;
}

// Video
export fn fd_screen(fd: *Fundude) MatrixChunk(u16).Abi {
    return MatrixChunk(u16).fromMatrix(fd.video.screen().toSlice());
}

export fn fd_background(fd: *Fundude) MatrixChunk(u16).Abi {
    return MatrixChunk(u16).fromMatrix(fd.video.cache.background.data.toSlice());
}

export fn fd_window(fd: *Fundude) MatrixChunk(u16).Abi {
    return MatrixChunk(u16).fromMatrix(fd.video.cache.window.data.toSlice());
}

export fn fd_sprites(fd: *Fundude) MatrixChunk(u16).Abi {
    return MatrixChunk(u16).fromMatrix(fd.video.cache.sprites.data.toSlice());
}

export fn fd_patterns(fd: *Fundude) MatrixChunk(u8).Abi {
    return MatrixChunk(u8).fromMatrix(fd.video.cache.patterns.toMatrixSlice());
}

export fn fd_cpu_reg(fd: *Fundude) U8Chunk.Abi {
    return U8Chunk.fromSlice(std.mem.asBytes(&fd.cpu.reg));
}

/// Only expose the dynamic memory -- e.g. 0x8000 - 0xFFFF
export fn fd_mmu(fd: *Fundude) U8Chunk.Abi {
    return U8Chunk.fromSlice(std.mem.asBytes(&fd.mmu.dyn)[0x8000..]);
}

export fn fd_set_breakpoint(fd: *Fundude, breakpoint: u16) void {
    fd.breakpoint = breakpoint;
}
