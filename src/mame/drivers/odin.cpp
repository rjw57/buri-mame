// license:BSD-3-Clause
// copyright-holders:Rich Wareham
/*
 * Odin driver.
 */

#include "emu.h"

#include "bus/rs232/rs232.h"
#include "cpu/m6502/m65c02.h"
#include "machine/6522via.h"
#include "machine/mos6551.h"

const char* VIA6522_TAG = "via6522";
const char* MOS6551_TAG = "mos6551";

const char* UART1_TAG = "uart1";

const int VIA6522_START = 0xDEF0;
const int MOS6551_START = 0xDFFC;

class odin_state : public driver_device
{
public:
	odin_state(const machine_config &mconfig,
	           device_type type, const char *tag) :
	           driver_device(mconfig, type, tag),
	           m_maincpu(*this, "maincpu"),
	           m_mos6551(*this, MOS6551_TAG),
	           m_via6522(*this, VIA6522_TAG)
	{
		m_irqs.val = 0;
	}

	DECLARE_WRITE_LINE_MEMBER(mos6551_irq_w);
	DECLARE_WRITE_LINE_MEMBER(via6522_irq_w);

	required_device<cpu_device> m_maincpu;
	required_device<mos6551_device> m_mos6551;
	required_device<via6522_device> m_via6522;

	union {
		uint32_t val;
		struct {
			int mos6551 : 1;
			int via6522 : 1;
		} flags;
	} m_irqs;

private:
	void irqs_updated_() {
		m_maincpu->set_input_line(
			M65C02_IRQ_LINE,
			m_irqs.val ? ASSERT_LINE : CLEAR_LINE);
	}
};

static ADDRESS_MAP_START(odin_mem, AS_PROGRAM, 8, odin_state)
	AM_RANGE(0x0000, 0xC000) AM_RAM
	AM_RANGE(0xE000, 0xFFFF) AM_ROM AM_REGION("maincpu", 0)

	// ACIA
	AM_RANGE(MOS6551_START, MOS6551_START + 3)
		AM_DEVREADWRITE(MOS6551_TAG, mos6551_device, read, write)

	// VIA
	AM_RANGE(VIA6522_START, VIA6522_START + 15)
		AM_DEVREADWRITE(VIA6522_TAG, via6522_device, read, write)
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

	MCFG_DEVICE_ADD(VIA6522_TAG, VIA6522, XTAL_2MHz)
	MCFG_VIA6522_IRQ_HANDLER(WRITELINE(odin_state, via6522_irq_w))
MACHINE_CONFIG_END

ROM_START(odin)
	ROM_REGION(0x2000, "maincpu", 0)
	ROM_LOAD("odinos.bin", 0x0000, 0x2000, "")
ROM_END

WRITE_LINE_MEMBER(odin_state::mos6551_irq_w)
{
	m_irqs.flags.mos6551 = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(odin_state::via6522_irq_w)
{
	m_irqs.flags.via6522 = (state != 0);
	irqs_updated_();
}

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    STATE         INIT    COMPANY                FULLNAME               FLAGS */
COMP(2017,  odin,   0,      0,       odin,      odin,    odin_state,   0,      "Rich Wareham", "Odin homebrew computer", MACHINE_TYPE_COMPUTER | MACHINE_NO_SOUND)
