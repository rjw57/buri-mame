// license:BSD-3-Clause
// copyright-holders:Rich Wareham
/*
 * Odin driver.
 *
 * The Odin homebrew computer is a simple 6502-based machine with the following
 * memory map:
 *
 * $0000 - $7FFF: RAM
 * $8000 - $DEFF: unused
 * $DF00 - $DFFF: I/O page
 * $E000 - $FFFF: ROM
 *
 * Within the I/O page the following devices are exposed:
 *
 * $DF00 - $DF0F: W65C22 VIA/GPIO
 * $DF10 - $DF11: TMS9929 VDP
 * $DF12 - $DF13: YM3812 sound
 * $DF18 - $DF1B: MOS6551 ACIA/UART
 *
 */

#include "emu.h"
#include "speaker.h"

// CPU
#include "cpu/m6502/m65c02.h"

// MOS6522 VIA
#include "machine/6522via.h"

// TMS9929 VDP
#include "video/tms9928a.h"

// YM3812 sound
#include "sound/3812intf.h"

// MOS6551 UART
#include "machine/mos6551.h"
#include "bus/rs232/rs232.h"

const char* MOS6522_TAG = "mos6522";
const char* TMS9929_TAG = "tms9929";
const char* YM3812_TAG = "ym3812";
const char* MOS6551_TAG = "mos6551";

const char* UART1_TAG = "uart1";

const int RAM_START = 0x0000, RAM_SIZE = 0x8000;
const int ROM_START = 0xE000, ROM_SIZE = 0x2000;

// I/O
const int MOS6522_START = 0xDF00;
const int TMS9929_START = 0xDF10;
const int YM3812_START  = 0xDF12;
const int MOS6551_START = 0xDF18;

class odin_state : public driver_device
{
public:
	odin_state(const machine_config &mconfig,
	           device_type type, const char *tag) :
	           driver_device(mconfig, type, tag),
	           m_maincpu(*this, "maincpu"),
	           m_mos6522(*this, MOS6522_TAG),
	           m_tms9929(*this, TMS9929_TAG),
	           m_ym3812(*this, YM3812_TAG),
	           m_mos6551(*this, MOS6551_TAG)
	{
		m_irqs.val = 0;
	}

	DECLARE_WRITE_LINE_MEMBER(mos6522_irq_w);
	DECLARE_WRITE_LINE_MEMBER(tms9929_irq_w);
	DECLARE_WRITE_LINE_MEMBER(mos6551_irq_w);

	required_device<cpu_device> m_maincpu;
	required_device<via6522_device> m_mos6522;
	required_device<tms9929a_device> m_tms9929;
	required_device<ym3812_device> m_ym3812;
	required_device<mos6551_device> m_mos6551;

	// Member holding state of individual IRQ lines from H/W.
	union {
		uint32_t val;
		struct {
			int mos6522 : 1;
			int tms9929 : 1;
			int mos6551 : 1;
		} flags;
	} m_irqs;

private:
	// Called after each IRQ line changes. Sets the main processor IRQ to be
	// wired OR of peripherals' lines.
	void irqs_updated_() {
		m_maincpu->set_input_line(
			M65C02_IRQ_LINE,
			m_irqs.val ? ASSERT_LINE : CLEAR_LINE);
	}
};

static ADDRESS_MAP_START(odin_mem, AS_PROGRAM, 8, odin_state)
	AM_RANGE(RAM_START, RAM_START + RAM_SIZE - 1) AM_RAM
	AM_RANGE(ROM_START, ROM_START + ROM_SIZE - 1) AM_ROM AM_REGION("maincpu", 0)

	AM_RANGE(MOS6522_START, MOS6522_START + 15)
		AM_DEVREADWRITE(MOS6522_TAG, via6522_device, read, write)
	AM_RANGE(TMS9929_START, TMS9929_START + 1)
		AM_DEVREADWRITE(TMS9929_TAG, tms9929a_device, read, write)
	AM_RANGE(YM3812_START, YM3812_START + 1)
		AM_DEVREADWRITE(YM3812_TAG, ym3812_device, read, write)
	AM_RANGE(MOS6551_START, MOS6551_START + 3)
		AM_DEVREADWRITE(MOS6551_TAG, mos6551_device, read, write)
ADDRESS_MAP_END

static INPUT_PORTS_START(odin)
INPUT_PORTS_END

static DEVICE_INPUT_DEFAULTS_START( serial_port )
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_19200 )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_19200 )
	DEVICE_INPUT_DEFAULTS( "RS232_STARTBITS", 0xff, RS232_STARTBITS_1 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
DEVICE_INPUT_DEFAULTS_END

static MACHINE_CONFIG_START(odin)
	MCFG_CPU_ADD("maincpu", M65C02, XTAL_2MHz)
	MCFG_CPU_PROGRAM_MAP(odin_mem)

	// VIA/GPIO
	MCFG_DEVICE_ADD(MOS6522_TAG, VIA6522, XTAL_2MHz)
	MCFG_VIA6522_IRQ_HANDLER(WRITELINE(odin_state, mos6522_irq_w))

	// Sound
	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD(YM3812_TAG, YM3812, XTAL_3_579545MHz)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	// Video
	MCFG_DEVICE_ADD(TMS9929_TAG, TMS9929A, XTAL_10_738635MHz)
	MCFG_TMS9928A_VRAM_SIZE(0x4000)
	MCFG_TMS9928A_OUT_INT_LINE_CB(WRITELINE(odin_state, tms9929_irq_w))
	MCFG_TMS9928A_SCREEN_ADD_PAL( "screen" )
	MCFG_SCREEN_UPDATE_DEVICE( TMS9929_TAG, tms9929a_device, screen_update )

	// UART support
	MCFG_DEVICE_ADD(MOS6551_TAG, MOS6551, 0)
	MCFG_MOS6551_XTAL(XTAL_1_8432MHz)
	MCFG_MOS6551_TXD_HANDLER(DEVWRITELINE(UART1_TAG, rs232_port_device, write_txd))
	MCFG_MOS6551_IRQ_HANDLER(WRITELINE(odin_state, mos6551_irq_w))
	MCFG_MOS6551_DTR_HANDLER(DEVWRITELINE(UART1_TAG, rs232_port_device, write_dtr))

	MCFG_RS232_PORT_ADD(UART1_TAG, default_rs232_devices, nullptr)
	MCFG_RS232_RXD_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_rxd))
	MCFG_RS232_DCD_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_dcd))
	MCFG_RS232_DSR_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_dsr))
	MCFG_RS232_CTS_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_cts))
	MCFG_DEVICE_CARD_DEVICE_INPUT_DEFAULTS("terminal", serial_port)
	MCFG_DEVICE_CARD_DEVICE_INPUT_DEFAULTS("pty", serial_port)
MACHINE_CONFIG_END

ROM_START(odin)
	ROM_REGION(0x2000, "maincpu", 0)
	ROM_LOAD("odinos.bin", 0x0000, ROM_SIZE, "")
ROM_END

WRITE_LINE_MEMBER(odin_state::mos6522_irq_w)
{
	m_irqs.flags.mos6522 = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(odin_state::tms9929_irq_w)
{
	m_irqs.flags.tms9929 = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(odin_state::mos6551_irq_w)
{
	m_irqs.flags.mos6551 = (state != 0);
	irqs_updated_();
}

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    STATE         INIT    COMPANY                FULLNAME               FLAGS */
COMP(2017,  odin,   0,      0,       odin,      odin,    odin_state,   0,      "Rich Wareham", "Odin homebrew computer", MACHINE_TYPE_COMPUTER | MACHINE_NO_SOUND)
