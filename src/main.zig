const std = @import("std");
const root = @import("root");

pub const Cpu = @import("Cpu.zig");
const video = @import("video.zig");
pub const Video = video.Video;
const joypad = @import("joypad.zig");
pub const Mmu = @import("Mmu.zig");
const timer = @import("timer.zig");
pub const Timer = timer.Timer;
pub const Savestate = @import("Savestate.zig");
pub const Temportal = @import("Temportal.zig");
pub const serial = @import("serial.zig");

pub const MHz = 4194304;
pub const profiling_call = std.builtin.CallOptions{
    .modifier = if (@hasDecl(root, "is_profiling") and root.is_profiling) .never_inline else .auto,
};

const Fundude = @This();

allocator: *std.mem.Allocator,
guest: ?*Fundude,

video: video.Video,
cpu: Cpu,
mmu: Mmu,

serial: serial.Serial,
inputs: joypad.Inputs,
timer: timer.Timer,
temportal: Temportal,

breakpoint: u16,

pub fn init(allocator: *std.mem.Allocator) !*Fundude {
    var fd = try allocator.create(Fundude);
    fd.allocator = allocator;
    fd.guest = null;
    return fd;
}

pub fn deinit(self: *Fundude) void {
    if (self.guest) |guest| {
        guest.deinit();
        self.guest = null;
    }

    self.allocator.destroy(self);
    self.* = undefined;
}

pub fn clone(self: *Fundude) !void {
    self.guest = try Fundude.init(self.allocator);
}

pub fn load(self: *Fundude, cart: []const u8) !void {
    try self.mmu.load(cart);
    self.reset();

    if (self.guest) |guest| {
        // TODO: recursion doesn't work -- investigate why
        // guest.load(cart) catch unreachable;

        guest.mmu.load(cart) catch unreachable;
        guest.reset();
    }
}

pub fn reset(self: *Fundude) void {
    self.mmu.reset();
    self.video.reset();
    self.cpu.reset();
    self.inputs.reset();
    self.timer.reset();
    self.serial.reset();

    self.temportal.reset();
    self.temportal.save(self);

    self.breakpoint = 0xFFFF;

    if (self.guest) |guest| {
        guest.reset();
    }
}

// TODO: convert "catchup" to an enum
pub fn tick(self: *Fundude, catchup: bool) void {
    @call(Fundude.profiling_call, self.cpu.tick, .{&self.mmu});
    @call(Fundude.profiling_call, self.video.tick, .{ &self.mmu, catchup });
    @call(Fundude.profiling_call, self.timer.tick, .{&self.mmu});
    @call(Fundude.profiling_call, self.serial.tick, .{&self.mmu});

    @call(Fundude.profiling_call, self.temportal.tick, .{self});

    if (self.guest) |guest| {
        guest.tick(catchup);
    }
}

pub const dump = Savestate.dump;
pub const restore = Savestate.restore;
pub const validateSavestate = Savestate.validate;
pub const savestate_size = Savestate.size;

test "" {
    _ = Fundude;
    _ = Savestate;
}
