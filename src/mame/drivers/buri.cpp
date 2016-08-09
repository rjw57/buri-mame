// license:BSD-3-Clause
// copyright-holders:Rich Wareham
/*
 * Buri driver.
 */

#include "emu.h"

#include "bus/rs232/rs232.h"
#include "cpu/g65816/g65816.h"
#include "machine/mos6551.h"
#include "sound/3812intf.h"
#include "video/tms9928a.h"

class buri_state : public driver_device
{
public:
	buri_state(const machine_config &mconfig, device_type type, const char *tag) :
	           driver_device(mconfig, type, tag),
	           m_maincpu(*this, "maincpu"),
	           m_acia(*this, "acia")
	{ }

	DECLARE_DRIVER_INIT(buri);
	DECLARE_MACHINE_START(buri);
	DECLARE_MACHINE_RESET(buri);

	DECLARE_WRITE_LINE_MEMBER(acia_irq_w);

	required_device<cpu_device> m_maincpu;
	optional_device<mos6551_device> m_acia;

	// interrupts
	int m_acia_irq;
};

static ADDRESS_MAP_START(buri_mem, AS_PROGRAM, 8, buri_state)
	AM_RANGE(0x0000, 0xC000) AM_RAM
	AM_RANGE(0xE000, 0xFFFF) AM_ROM AM_REGION("maincpu", 0)
	AM_RANGE(0x010000, 0x07FFFF) AM_RAM
	AM_RANGE(0xE0C000, 0xE0FFFF) AM_RAM

	// VDP
	AM_RANGE(0xDE00, 0xDE00) AM_DEVREADWRITE("tms9918", tms9918a_device, vram_read, vram_write)
	AM_RANGE(0xDE01, 0xDE01) AM_DEVREADWRITE("tms9918", tms9918a_device, register_read, register_write)

	// ACIA
	AM_RANGE(0xDFFC, 0xDFFF) AM_DEVREADWRITE("acia", mos6551_device, read, write)

	// SOUND
	AM_RANGE(0xDE02, 0xDE03) AM_DEVREADWRITE("ym3812", ym3812_device, read, write)
ADDRESS_MAP_END

static INPUT_PORTS_START(buri)
INPUT_PORTS_END

static DEVICE_INPUT_DEFAULTS_START(terminal)
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_19200 )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_19200 )
	DEVICE_INPUT_DEFAULTS( "RS232_STARTBITS", 0xff, RS232_STARTBITS_1 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
DEVICE_INPUT_DEFAULTS_END

static MACHINE_CONFIG_START(buri, buri_state)
	MCFG_CPU_ADD("maincpu", G65816, XTAL_2MHz)
	MCFG_CPU_PROGRAM_MAP(buri_mem)

	MCFG_DEVICE_ADD("acia", MOS6551, 0)
	MCFG_MOS6551_XTAL(XTAL_1_8432MHz)
	MCFG_MOS6551_TXD_HANDLER(DEVWRITELINE("uart", rs232_port_device, write_txd))
	MCFG_MOS6551_IRQ_HANDLER(WRITELINE(buri_state, acia_irq_w))

	MCFG_RS232_PORT_ADD("uart", default_rs232_devices, "pty")
	MCFG_RS232_RXD_HANDLER(DEVWRITELINE("acia", mos6551_device, write_rxd))
	MCFG_RS232_DCD_HANDLER(DEVWRITELINE("acia", mos6551_device, write_dcd))
	MCFG_RS232_DSR_HANDLER(DEVWRITELINE("acia", mos6551_device, write_dsr))
	MCFG_RS232_CTS_HANDLER(DEVWRITELINE("acia", mos6551_device, write_cts))
	MCFG_DEVICE_CARD_DEVICE_INPUT_DEFAULTS("pty", terminal)
	MCFG_DEVICE_CARD_DEVICE_INPUT_DEFAULTS("terminal", terminal)

	MCFG_DEVICE_ADD("tms9918", TMS9918A, XTAL_10_738635MHz)
	MCFG_TMS9928A_VRAM_SIZE(0x4000)
	// MCFG_TMS9928A_OUT_INT_LINE_CB(INPUTLINE("maincpu", INPUT_LINE_IRQ0))
	MCFG_TMS9928A_SCREEN_ADD_NTSC( "screen" )
	MCFG_SCREEN_UPDATE_DEVICE( "tms9918", tms9918a_device, screen_update )

	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD("ym3812", YM3812, XTAL_3_579545MHz)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)
MACHINE_CONFIG_END

ROM_START(buri)
	ROM_REGION(0x2000, "maincpu", 0)
	ROM_LOAD("burios.bin", 0x0000, 0x2000, CRC(e527d758))
ROM_END

DRIVER_INIT_MEMBER(buri_state, buri)
{
}

MACHINE_START_MEMBER(buri_state, buri)
{
}

MACHINE_RESET_MEMBER(buri_state, buri)
{
	m_acia->reset();
}

WRITE_LINE_MEMBER(buri_state::acia_irq_w)
{
	m_maincpu->set_input_line(G65816_INT_IRQ, state ? ASSERT_LINE : CLEAR_LINE);
}

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    CLASS         INIT    COMPANY                FULLNAME               FLAGS */
COMP(2016,  buri,   0,      0,       buri,      buri,    driver_device,0,      "Rich Wareham", "Búri homebrew computer", MACHINE_TYPE_COMPUTER)
