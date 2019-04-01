#include "cpu.h"
#include <assert.h>

uint16_t w2(uint8_t op[]) {
  return (op[1] << 8) + op[2];
}

bool is_uint8_zero(int val) {
  return (val & 0xFF) == 0;
}

bool will_carry_from(int bit, int a, int b) {
  int mask = (1 << (bit + 1)) - 1;
  return (a & mask) + (b & mask) > mask;
}

bool will_borrow_from(int bit, int a, int b) {
  int mask = (1 << bit) - 1;
  return (a & mask) < (b & mask);
}

/* op_ functions are a lot more scannable if the names line up, hence the
   cryptic abbreviations:

     rr -- register (8bit)
     ww -- wide register (16bit)
     08 -- byte literal (8bit)
     16 -- double-byte literal (16bit)

     RR -- register address (8bit + $FF00)
     WW -- wide register address (16bit)
     0A -- byte address (8bit + $FF00)
     1F -- double-byte address (16bit)
*/

static void do_add_rr(fundude* fd, reg8* tgt, uint8_t val) {
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(tgt->_ + val),
      .N = false,
      .H = will_carry_from(3, tgt->_, val),
      .C = will_carry_from(7, tgt->_, val),
  };
  tgt->_ += val;
}

op_result op_nop() {
  return OP_RESULT(1, 4, "NOP");
}

op_result op_sys(fundude* fd, sys_mode mode, int length) {
  return OP_RESULT(length, 4, "SYS %d", mode);
}

op_result op_scf(fundude* fd) {
  fd->reg.FLAGS = (fd_flags){
      .Z = fd->reg.FLAGS.Z,
      .N = false,
      .H = false,
      .C = true,
  };
  return OP_RESULT(1, 4, "SCF");
}

op_result op_ccf(fundude* fd) {
  fd->reg.FLAGS = (fd_flags){
      .Z = fd->reg.FLAGS.Z,
      .N = false,
      .H = false,
      .C = !fd->reg.FLAGS.C,
  };
  return OP_RESULT(1, 4, "CCF");
}

op_result op_daa_rr___(fundude* fd, reg8* dst) {
  uint8_t lb = dst->_ & 0xF;
  uint8_t hb = (dst->_ >> 4) & 0xF;
  bool carry = fd->reg.FLAGS.C;

  if (fd->reg.FLAGS.N) {
    if (lb >= 10) {
      lb -= 6;
    }
    if (hb >= 10) {
      hb -= 10;
      carry = true;
    }
  } else {
    if (lb >= 10) {
      lb -= 10;
      hb++;
    }
    if (hb >= 10) {
      hb -= 10;
      carry = true;
    }
  }

  dst->_ = (hb << 4) | lb;
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(dst->_),
      .N = fd->reg.FLAGS.N,
      .H = false,
      .C = carry,
  };
  return OP_RESULT(1, 4, "DAA %s", db_reg8(fd, dst));
}

op_result op_jmp_08___(fundude* fd, uint8_t val) {
  return OP_RESULT(val, 8, "JR %d", val);
}

op_result op_jmp_if_08(fundude* fd, bool check, uint8_t val) {
  return OP_RESULT(check ? val : 2, 8, "JR %c %d", check ? 'Y' : 'N', val);
}

op_result op_rlc_rr___(fundude* fd, reg8* tgt) {
  int msb = tgt->_ >> 7 & 1;

  tgt->_ = tgt->_ << 1 | msb;
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(tgt->_),
      .N = false,
      .H = false,
      .C = msb,
  };
  return OP_RESULT(1, 4, "RLCA %s", db_reg8(fd, tgt));
}

op_result op_rla_rr___(fundude* fd, reg8* tgt) {
  int msb = tgt->_ >> 7 & 1;

  tgt->_ = tgt->_ << 1 | fd->reg.FLAGS.C;
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(tgt->_),
      .N = false,
      .H = false,
      .C = msb,
  };
  return OP_RESULT(1, 4, "RLA %s", db_reg8(fd, tgt));
}

op_result op_rrc_rr___(fundude* fd, reg8* tgt) {
  int lsb = tgt->_ & 1;

  tgt->_ = tgt->_ >> 1 | (lsb << 7);
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(tgt->_),
      .N = false,
      .H = false,
      .C = lsb,
  };
  return OP_RESULT(1, 4, "RRC %s", db_reg8(fd, tgt));
}

op_result op_rra_rr___(fundude* fd, reg8* tgt) {
  int lsb = tgt->_ & 1;

  tgt->_ = tgt->_ >> 1 | (fd->reg.FLAGS.C << 7);
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(tgt->_),
      .N = false,
      .H = false,
      .C = lsb,
  };
  return OP_RESULT(1, 4, "RRA %s", db_reg8(fd, tgt));
}

op_result op_lod_rr_08(fundude* fd, reg8* tgt, uint8_t d8) {
  tgt->_ = d8;
  return OP_RESULT(2, 8, "LD %s,d8", db_reg8(fd, tgt));
}

op_result op_lod_rr_rr(fundude* fd, reg8* tgt, reg8* src) {
  tgt->_ = src->_;
  return OP_RESULT(1, 4, "LD %s,%s", db_reg8(fd, tgt), db_reg8(fd, src));
}

op_result op_lod_rr_WW(fundude* fd, reg8* tgt, reg16* src) {
  tgt->_ = fdm_get(&fd->mem, src->_);
  return OP_RESULT(1, 8, "LD %s,(%s)", db_reg8(fd, tgt), db_reg16(fd, src));
}

op_result op_lod_ww_16(fundude* fd, reg16* tgt, uint16_t d16) {
  tgt->_ = d16;
  return OP_RESULT(3, 12, "LD %s,d16", db_reg16(fd, tgt));
}

op_result op_lod_WW_rr(fundude* fd, reg16* tgt, reg8* src) {
  fdm_set(&fd->mem, tgt->_, src->_);
  return OP_RESULT(1, 8, "LD (%s),%s", db_reg16(fd, tgt), db_reg8(fd, src));
}

op_result op_lod_1F_ww(fundude* fd, uint16_t a16, reg16* src) {
  fdm_set(&fd->mem, a16, src->_);
  return OP_RESULT(3, 20, "LD d16,%s", db_reg16(fd, src));
}

op_result op_lod_WW_08(fundude* fd, reg16* tgt, uint8_t val) {
  fdm_set(&fd->mem, tgt->_, val);
  return OP_RESULT(2, 12, "LD (%s),d8", db_reg16(fd, tgt));
}

op_result op_ldi_WW_rr(fundude* fd, reg16* tgt, reg8* src) {
  fdm_set(&fd->mem, tgt->_++, src->_);
  return OP_RESULT(1, 8, "LD (%s+),%s", db_reg16(fd, tgt), db_reg8(fd, src));
}

op_result op_ldi_rr_WW(fundude* fd, reg8* tgt, reg16* src) {
  fdm_set(&fd->mem, tgt->_, src->_++);
  return OP_RESULT(1, 8, "LD %s,(%s+)", db_reg8(fd, tgt), db_reg16(fd, src));
}

op_result op_ldd_WW_rr(fundude* fd, reg16* tgt, reg8* src) {
  fdm_set(&fd->mem, tgt->_--, src->_);
  return OP_RESULT(1, 8, "LD (%s-),%s", db_reg16(fd, tgt), db_reg8(fd, src));
}

op_result op_ldd_rr_WW(fundude* fd, reg8* tgt, reg16* src) {
  fdm_set(&fd->mem, tgt->_, src->_--);
  return OP_RESULT(1, 8, "LD %s,(%s-)", db_reg8(fd, tgt), db_reg16(fd, src));
}

op_result op_inc_ww___(fundude* fd, reg16* tgt) {
  tgt->_++;
  return OP_RESULT(1, 8, "INC %s", db_reg16(fd, tgt));
}

op_result op_inc_WW___(fundude* fd, reg16* tgt) {
  uint8_t* mem = fdm_ptr(&fd->mem, tgt->_);

  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero((*mem) + 1),
      .N = 0,
      .H = will_carry_from(3, *mem, 1),
      .C = fd->reg.FLAGS.C,
  };
  (*mem)++;
  return OP_RESULT(1, 12, "INC (%s)", db_reg16(fd, tgt));
}

op_result op_dec_ww___(fundude* fd, reg16* tgt) {
  tgt->_--;
  return OP_RESULT(1, 8, "DEC %s", db_reg16(fd, tgt));
}

op_result op_dec_WW___(fundude* fd, reg16* tgt) {
  uint8_t* mem = fdm_ptr(&fd->mem, tgt->_);

  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero((*mem) - 1),
      .N = 1,
      .H = will_borrow_from(4, *mem, 1),
      .C = fd->reg.FLAGS.C,
  };
  (*mem)--;
  return OP_RESULT(1, 12, "DEC (%s)", db_reg16(fd, tgt));
}

op_result op_add_rr_rr(fundude* fd, reg8* tgt, reg8* src) {
  do_add_rr(fd, tgt, src->_);
  return OP_RESULT(1, 4, "ADD %s,%s", db_reg8(fd, tgt), db_reg8(fd, src));
}

op_result op_add_rr_WW(fundude* fd, reg8* tgt, reg16* src) {
  do_add_rr(fd, tgt, fdm_get(&fd->mem, src->_));
  return OP_RESULT(1, 8, "ADD %s,%s", db_reg8(fd, tgt), db_reg16(fd, src));
}

op_result op_add_rr_08(fundude* fd, reg8* tgt, uint8_t val) {
  do_add_rr(fd, tgt, val);
  return OP_RESULT(2, 8, "ADD %s,d8", db_reg8(fd, tgt));
}

op_result op_add_ww_ww(fundude* fd, reg16* tgt, reg16* src) {
  fd->reg.FLAGS = (fd_flags){
      .Z = fd->reg.FLAGS.Z,
      .N = false,
      .H = will_carry_from(11, tgt->_, src->_),
      .C = will_carry_from(15, tgt->_, src->_),
  };
  tgt->_ += src->_;
  return OP_RESULT(1, 8, "ADD %s,%s", db_reg16(fd, tgt), db_reg16(fd, src));
}

op_result op_adc_rr_rr(fundude* fd, reg8* tgt, reg8* src) {
  do_add_rr(fd, tgt, fd->reg.FLAGS.C + src->_);
  return OP_RESULT(1, 4, "ADC %s,%s", db_reg8(fd, tgt), db_reg8(fd, src));
}

op_result op_adc_rr_WW(fundude* fd, reg8* tgt, reg16* src) {
  do_add_rr(fd, tgt, fd->reg.FLAGS.C + fdm_get(&fd->mem, src->_));
  return OP_RESULT(1, 8, "ADC %s,%s", db_reg8(fd, tgt), db_reg16(fd, src));
}

op_result op_sub_rr_08(fundude* fd, reg8* tgt, uint8_t val) {
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(tgt->_ - val),
      .N = true,
      .H = will_borrow_from(4, tgt->_, val),
      .C = will_borrow_from(8, tgt->_, val),
  };
  tgt->_ -= val;
  return OP_RESULT(2, 8, "SUB %s,d8", db_reg8(fd, tgt));
}

op_result op_inc_rr___(fundude* fd, reg8* tgt) {
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(tgt->_ + 1),
      .N = false,
      .H = will_carry_from(3, tgt->_, 1),
      .C = fd->reg.FLAGS.C,
  };
  tgt->_++;
  return OP_RESULT(1, 4, "INC %s", db_reg8(fd, tgt));
}

op_result op_dec_rr___(fundude* fd, reg8* tgt) {
  fd->reg.FLAGS = (fd_flags){
      .Z = is_uint8_zero(tgt->_ - 1),
      .N = true,
      .H = will_carry_from(3, tgt->_, 1),
      .C = fd->reg.FLAGS.C,
  };
  tgt->_--;
  return OP_RESULT(1, 4, "DEC %s", db_reg8(fd, tgt));
}

op_result op_cpl_rr___(fundude* fd, reg8* tgt) {
  fd->reg.FLAGS = (fd_flags){
      .Z = fd->reg.FLAGS.Z,
      .N = true,
      .H = true,
      .C = fd->reg.FLAGS.C,
  };
  return OP_RESULT(1, 4, "CPL %s", db_reg8(fd, tgt));
}

op_result fd_run(fundude* fd, uint8_t op[]) {
  switch (op[0]) {
    case 0x00: return op_nop();
    case 0x01: return op_lod_ww_16(fd, &fd->reg.BC, w2(op));
    case 0x02: return op_lod_WW_rr(fd, &fd->reg.BC, &fd->reg.A);
    case 0x03: return op_inc_ww___(fd, &fd->reg.BC);
    case 0x04: return op_inc_rr___(fd, &fd->reg.B);
    case 0x05: return op_dec_rr___(fd, &fd->reg.B);
    case 0x06: return op_lod_rr_08(fd, &fd->reg.B, op[1]);
    case 0x07: return op_rlc_rr___(fd, &fd->reg.A);
    case 0x08: return op_lod_1F_ww(fd, w2(op), &fd->reg.SP);
    case 0x09: return op_add_ww_ww(fd, &fd->reg.HL, &fd->reg.BC);
    case 0x0A: return op_lod_rr_WW(fd, &fd->reg.A, &fd->reg.BC);
    case 0x0B: return op_dec_ww___(fd, &fd->reg.BC);
    case 0x0C: return op_inc_rr___(fd, &fd->reg.C);
    case 0x0D: return op_dec_rr___(fd, &fd->reg.C);
    case 0x0E: return op_lod_rr_08(fd, &fd->reg.C, op[1]);
    case 0x0F: return op_rrc_rr___(fd, &fd->reg.A);

    case 0x10: return op_sys(fd, SYS_STOP, 2);
    case 0x11: return op_lod_ww_16(fd, &fd->reg.DE, w2(op));
    case 0x12: return op_lod_WW_rr(fd, &fd->reg.DE, &fd->reg.A);
    case 0x13: return op_inc_ww___(fd, &fd->reg.DE);
    case 0x14: return op_inc_rr___(fd, &fd->reg.D);
    case 0x15: return op_dec_rr___(fd, &fd->reg.D);
    case 0x16: return op_lod_rr_08(fd, &fd->reg.D, op[1]);
    case 0x17: return op_rla_rr___(fd, &fd->reg.A);
    case 0x18: return op_jmp_08___(fd, op[1]);
    case 0x19: return op_add_ww_ww(fd, &fd->reg.HL, &fd->reg.DE);
    case 0x1A: return op_lod_rr_WW(fd, &fd->reg.A, &fd->reg.DE);
    case 0x1B: return op_dec_ww___(fd, &fd->reg.DE);
    case 0x1C: return op_inc_rr___(fd, &fd->reg.E);
    case 0x1D: return op_dec_rr___(fd, &fd->reg.E);
    case 0x1E: return op_lod_rr_08(fd, &fd->reg.E, op[1]);
    case 0x1F: return op_rra_rr___(fd, &fd->reg.A);

    case 0x20: return op_jmp_if_08(fd, !fd->reg.FLAGS.Z, op[1]);
    case 0x21: return op_lod_ww_16(fd, &fd->reg.HL, w2(op));
    case 0x22: return op_ldi_WW_rr(fd, &fd->reg.HL, &fd->reg.A);
    case 0x23: return op_inc_ww___(fd, &fd->reg.HL);
    case 0x24: return op_inc_rr___(fd, &fd->reg.H);
    case 0x25: return op_dec_rr___(fd, &fd->reg.H);
    case 0x26: return op_lod_rr_08(fd, &fd->reg.H, op[1]);
    case 0x27: return op_daa_rr___(fd, &fd->reg.A);
    case 0x28: return op_jmp_if_08(fd, fd->reg.FLAGS.Z, op[1]);
    case 0x29: return op_add_ww_ww(fd, &fd->reg.HL, &fd->reg.HL);
    case 0x2A: return op_ldi_rr_WW(fd, &fd->reg.A, &fd->reg.HL);
    case 0x2B: return op_dec_ww___(fd, &fd->reg.HL);
    case 0x2C: return op_inc_rr___(fd, &fd->reg.L);
    case 0x2D: return op_dec_rr___(fd, &fd->reg.L);
    case 0x2E: return op_lod_rr_08(fd, &fd->reg.L, op[1]);
    case 0x2F: return op_cpl_rr___(fd, &fd->reg.A);

    case 0x30: return op_jmp_if_08(fd, !fd->reg.FLAGS.C, op[1]);
    case 0x31: return op_lod_ww_16(fd, &fd->reg.SP, w2(op));
    case 0x32: return op_ldd_WW_rr(fd, &fd->reg.HL, &fd->reg.A);
    case 0x33: return op_inc_ww___(fd, &fd->reg.SP);
    case 0x34: return op_inc_WW___(fd, &fd->reg.HL);
    case 0x35: return op_dec_WW___(fd, &fd->reg.HL);
    case 0x36: return op_lod_WW_08(fd, &fd->reg.HL, op[1]);
    case 0x37: return op_scf(fd);
    case 0x38: return op_jmp_if_08(fd, fd->reg.FLAGS.C, op[1]);
    case 0x39: return op_add_ww_ww(fd, &fd->reg.HL, &fd->reg.SP);
    case 0x3A: return op_ldd_rr_WW(fd, &fd->reg.A, &fd->reg.HL);
    case 0x3B: return op_dec_ww___(fd, &fd->reg.SP);
    case 0x3C: return op_inc_rr___(fd, &fd->reg.A);
    case 0x3D: return op_dec_rr___(fd, &fd->reg.A);
    case 0x3E: return op_lod_rr_08(fd, &fd->reg.A, op[1]);
    case 0x3F: return op_ccf(fd);

    case 0x40: return op_lod_rr_rr(fd, &fd->reg.B, &fd->reg.B);
    case 0x41: return op_lod_rr_rr(fd, &fd->reg.B, &fd->reg.C);
    case 0x42: return op_lod_rr_rr(fd, &fd->reg.B, &fd->reg.D);
    case 0x43: return op_lod_rr_rr(fd, &fd->reg.B, &fd->reg.E);
    case 0x44: return op_lod_rr_rr(fd, &fd->reg.B, &fd->reg.H);
    case 0x45: return op_lod_rr_rr(fd, &fd->reg.B, &fd->reg.L);
    case 0x46: return op_lod_rr_WW(fd, &fd->reg.B, &fd->reg.HL);
    case 0x47: return op_lod_rr_rr(fd, &fd->reg.B, &fd->reg.A);
    case 0x48: return op_lod_rr_rr(fd, &fd->reg.C, &fd->reg.B);
    case 0x49: return op_lod_rr_rr(fd, &fd->reg.C, &fd->reg.C);
    case 0x4A: return op_lod_rr_rr(fd, &fd->reg.C, &fd->reg.D);
    case 0x4B: return op_lod_rr_rr(fd, &fd->reg.C, &fd->reg.E);
    case 0x4C: return op_lod_rr_rr(fd, &fd->reg.C, &fd->reg.H);
    case 0x4D: return op_lod_rr_rr(fd, &fd->reg.C, &fd->reg.L);
    case 0x4E: return op_lod_rr_WW(fd, &fd->reg.C, &fd->reg.HL);
    case 0x4F: return op_lod_rr_rr(fd, &fd->reg.C, &fd->reg.A);

    case 0x50: return op_lod_rr_rr(fd, &fd->reg.D, &fd->reg.B);
    case 0x51: return op_lod_rr_rr(fd, &fd->reg.D, &fd->reg.C);
    case 0x52: return op_lod_rr_rr(fd, &fd->reg.D, &fd->reg.D);
    case 0x53: return op_lod_rr_rr(fd, &fd->reg.D, &fd->reg.E);
    case 0x54: return op_lod_rr_rr(fd, &fd->reg.D, &fd->reg.H);
    case 0x55: return op_lod_rr_rr(fd, &fd->reg.D, &fd->reg.L);
    case 0x56: return op_lod_rr_WW(fd, &fd->reg.D, &fd->reg.HL);
    case 0x57: return op_lod_rr_rr(fd, &fd->reg.D, &fd->reg.A);
    case 0x58: return op_lod_rr_rr(fd, &fd->reg.E, &fd->reg.B);
    case 0x59: return op_lod_rr_rr(fd, &fd->reg.E, &fd->reg.C);
    case 0x5A: return op_lod_rr_rr(fd, &fd->reg.E, &fd->reg.D);
    case 0x5B: return op_lod_rr_rr(fd, &fd->reg.E, &fd->reg.E);
    case 0x5C: return op_lod_rr_rr(fd, &fd->reg.E, &fd->reg.H);
    case 0x5D: return op_lod_rr_rr(fd, &fd->reg.E, &fd->reg.L);
    case 0x5E: return op_lod_rr_WW(fd, &fd->reg.E, &fd->reg.HL);
    case 0x5F: return op_lod_rr_rr(fd, &fd->reg.E, &fd->reg.A);

    case 0x60: return op_lod_rr_rr(fd, &fd->reg.H, &fd->reg.B);
    case 0x61: return op_lod_rr_rr(fd, &fd->reg.H, &fd->reg.C);
    case 0x62: return op_lod_rr_rr(fd, &fd->reg.H, &fd->reg.D);
    case 0x63: return op_lod_rr_rr(fd, &fd->reg.H, &fd->reg.E);
    case 0x64: return op_lod_rr_rr(fd, &fd->reg.H, &fd->reg.H);
    case 0x65: return op_lod_rr_rr(fd, &fd->reg.H, &fd->reg.L);
    case 0x66: return op_lod_rr_WW(fd, &fd->reg.H, &fd->reg.HL);
    case 0x67: return op_lod_rr_rr(fd, &fd->reg.H, &fd->reg.A);
    case 0x68: return op_lod_rr_rr(fd, &fd->reg.L, &fd->reg.B);
    case 0x69: return op_lod_rr_rr(fd, &fd->reg.L, &fd->reg.C);
    case 0x6A: return op_lod_rr_rr(fd, &fd->reg.L, &fd->reg.D);
    case 0x6B: return op_lod_rr_rr(fd, &fd->reg.L, &fd->reg.E);
    case 0x6C: return op_lod_rr_rr(fd, &fd->reg.L, &fd->reg.H);
    case 0x6D: return op_lod_rr_rr(fd, &fd->reg.L, &fd->reg.L);
    case 0x6E: return op_lod_rr_WW(fd, &fd->reg.L, &fd->reg.HL);
    case 0x6F: return op_lod_rr_rr(fd, &fd->reg.L, &fd->reg.A);

    case 0x70: return op_lod_WW_rr(fd, &fd->reg.HL, &fd->reg.B);
    case 0x71: return op_lod_WW_rr(fd, &fd->reg.HL, &fd->reg.C);
    case 0x72: return op_lod_WW_rr(fd, &fd->reg.HL, &fd->reg.D);
    case 0x73: return op_lod_WW_rr(fd, &fd->reg.HL, &fd->reg.E);
    case 0x74: return op_lod_WW_rr(fd, &fd->reg.HL, &fd->reg.H);
    case 0x75: return op_lod_WW_rr(fd, &fd->reg.HL, &fd->reg.L);
    case 0x76: return op_sys(fd, SYS_HALT, 1);
    case 0x77: return op_lod_WW_rr(fd, &fd->reg.HL, &fd->reg.A);
    case 0x78: return op_lod_rr_rr(fd, &fd->reg.A, &fd->reg.B);
    case 0x79: return op_lod_rr_rr(fd, &fd->reg.A, &fd->reg.C);
    case 0x7A: return op_lod_rr_rr(fd, &fd->reg.A, &fd->reg.D);
    case 0x7B: return op_lod_rr_rr(fd, &fd->reg.A, &fd->reg.E);
    case 0x7C: return op_lod_rr_rr(fd, &fd->reg.A, &fd->reg.H);
    case 0x7D: return op_lod_rr_rr(fd, &fd->reg.A, &fd->reg.L);
    case 0x7E: return op_lod_rr_WW(fd, &fd->reg.A, &fd->reg.HL);
    case 0x7F: return op_lod_rr_rr(fd, &fd->reg.A, &fd->reg.A);

    case 0x80: return op_add_rr_rr(fd, &fd->reg.A, &fd->reg.B);
    case 0x81: return op_add_rr_rr(fd, &fd->reg.A, &fd->reg.C);
    case 0x82: return op_add_rr_rr(fd, &fd->reg.A, &fd->reg.D);
    case 0x83: return op_add_rr_rr(fd, &fd->reg.A, &fd->reg.E);
    case 0x84: return op_add_rr_rr(fd, &fd->reg.A, &fd->reg.H);
    case 0x85: return op_add_rr_rr(fd, &fd->reg.A, &fd->reg.L);
    case 0x86: return op_add_rr_WW(fd, &fd->reg.A, &fd->reg.HL);
    case 0x87: return op_add_rr_rr(fd, &fd->reg.A, &fd->reg.A);
    case 0x88: return op_adc_rr_rr(fd, &fd->reg.A, &fd->reg.B);
    case 0x89: return op_adc_rr_rr(fd, &fd->reg.A, &fd->reg.C);
    case 0x8A: return op_adc_rr_rr(fd, &fd->reg.A, &fd->reg.D);
    case 0x8B: return op_adc_rr_rr(fd, &fd->reg.A, &fd->reg.E);
    case 0x8C: return op_adc_rr_rr(fd, &fd->reg.A, &fd->reg.H);
    case 0x8D: return op_adc_rr_rr(fd, &fd->reg.A, &fd->reg.L);
    case 0x8E: return op_adc_rr_WW(fd, &fd->reg.A, &fd->reg.HL);
    case 0x8F: return op_adc_rr_rr(fd, &fd->reg.A, &fd->reg.A);

    // --
    case 0xC6: return op_add_rr_08(fd, &fd->reg.A, op[1]);
    case 0xD6: return op_sub_rr_08(fd, &fd->reg.A, op[1]);
  }

  assert(false);  // Op not implemented
  return OP_RESULT(0, 0, "");
}

void fd_tick(fundude* fd) {
  op_result c = fd_run(fd, fdm_ptr(&fd->mem, fd->reg.PC._));
  assert(c.length > 0);
  assert(c.duration > 0);
  fd->reg.PC._ += c.length;
}
