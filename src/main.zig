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
pub const Serial = @import("Serial.zig");

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

serial: Serial,
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
    const guest = try Fundude.init(self.allocator);
    self.guest = guest;

    try guest.load(self.mmu.cart);
    self.serialConnect(guest);
}

pub fn serialConnect(self: *Fundude, other: *Fundude) void {
    std.debug.assert(self.serial.guest_sb == null);
    std.debug.assert(self.serial.guest_if == null);
    std.debug.assert(other.serial.guest_sb == null);
    std.debug.assert(other.serial.guest_if == null);

    self.serial.connect(&other.mmu.dyn.io);
    other.serial.connect(&self.mmu.dyn.io);
}

pub fn serialDisconnect(self: *Fundude, other: *Fundude) void {
    std.debug.assert(self.serial.guest_sb != null);
    std.debug.assert(self.serial.guest_if != null);
    std.debug.assert(other.serial.guest_sb != null);
    std.debug.assert(other.serial.guest_if != null);

    self.serial.disconnect();
    other.serial.disconnect();
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
    const clock = @call(Fundude.profiling_call, self.timer.tick, .{&self.mmu});

    @call(Fundude.profiling_call, self.cpu.tick, .{&self.mmu});
    @call(Fundude.profiling_call, self.video.tick, .{ &self.mmu, catchup });
    @call(Fundude.profiling_call, self.serial.tick, .{ &self.mmu, clock });

    @call(Fundude.profiling_call, self.temportal.tick, .{ self, clock });

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
    _ = Serial;
}
