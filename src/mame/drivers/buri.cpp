// license:BSD-3-Clause
// copyright-holders:Rich Wareham
/*
 * Buri driver.
 */

#include "emu.h"

#include "bus/rs232/rs232.h"
#include "cpu/g65816/g65816.h"
#include "machine/6522via.h"
#include "machine/ins8250.h"
#include "machine/mos6551.h"
#include "machine/pckeybrd.h"
#include "machine/spi.h"
#include "sound/3812intf.h"
#include "video/tms9928a.h"

const char* VIA6522_TAG = "via6522";
const char* MOS6551_TAG = "mos6551";
const char* YM3812_TAG = "ym3812";
const char* TMS9929_TAG = "tms9929";
const char* KEYBOARD_TAG = "at_keyboard";
const char* SPI_KEYBOARD_TAG = "spi_keyboard";

const char* UART1_TAG = "uart1";

const int VIA6522_START = 0xDEF0;
const int MOS6551_START = 0xDFFC;
const int TMS9929_START = 0xDE00;
const int YM3812_START = 0xDE02;

// SPI keyboard controller
// -----------------------
//
// The SPI keyboard controller talks to a hardware keyboard and presents an
// AT-scancode set 1 keyboard over SPI. There are two modes: read scancode and
// write control.
//
// After selecting the device, the master exchanges two bytes with the device.
// Subsequent SPI exchanges are ignored with MISO set to 0.
//
// Read scancode
// ~~~~~~~~~~~~~
//
// | MOSI | MISO     |
// |======|==========|
// | $00  | <X>      |
// | <X>  | scancode |
//
// After the scancode is read, the internal scancode register is reset to $00.
// Subsequent reads will therefore return $00.
//
// Write control
// ~~~~~~~~~~~~~
// Writing is indicated by sending a byte with the high bit set. The low 7 bits
// are the control code. The control response is sent in the next byte.
//
// | MOSI       | MISO     |
// |============|==========|
// | $80 |<ctrl | <X>      |
// | <X>        | response |
//
// Unknown control codes may have unintended effects and have undefined
// responses.
//
// Control codes
// ~~~~~~~~~~~~~
//
// $00 - responds $FF if scancode register is full or $00 if empty

#define MCFG_SPI_KBD_ADD( _tag ) \
	MCFG_DEVICE_ADD( _tag, SPI_KEYBOARD, 0 ) \
	MCFG_SPI_MODE(SPI_MODE0) \
	MCFG_SPI_DATA_DIRECTION(SPI_MSB_FIRST)

#define MCFG_SPI_KBD_IRQ_CALLBACK(_irq) \
	downcast<spi_kbd_device *>(device)->set_irq_callback(DEVCB_##_irq);

enum spi_kbd_state {
	SPI_KBD_NOT_SELECTED,
	SPI_KBD_NEWLY_SELECTED,
	SPI_KBD_READY_TO_READ,
	SPI_KBD_READY_TO_RESPOND,
	SPI_KBD_DONE,
};

class spi_kbd_device : public spi_slave_device
{
public:
	spi_kbd_device(const machine_config &mconfig, const char *tag,
	               device_t *owner, uint32_t clock);

	template<class _irq> void set_irq_callback(_irq irq) {
		m_write_irq.set_callback(irq);
	}

	virtual machine_config_constructor device_mconfig_additions() const override;

	DECLARE_WRITE_LINE_MEMBER( keyboard_w );

protected:
	virtual void device_start() override;
	virtual void spi_slave_selected() override;
	virtual void spi_slave_deselected() override;
	virtual uint8_t spi_slave_exchange_byte(uint8_t) override;

	uint8_t control(uint8_t ctrl_byte);

	required_device<at_keyboard_device> m_keyboard_dev;

	devcb_write_line m_write_irq;

	spi_kbd_state m_state;
	uint8_t m_last_scancode;
	bool m_scancode_reg_full;
};

extern const device_type SPI_KEYBOARD;

const device_type SPI_KEYBOARD = &device_creator<spi_kbd_device>;

spi_kbd_device::spi_kbd_device(const machine_config &mconfig, const char *tag,
                               device_t *owner, uint32_t clock)
	: spi_slave_device(mconfig, tag, owner, clock),
	m_keyboard_dev(*this, "at_keyboard"),
	m_write_irq(*this), m_state(SPI_KBD_NOT_SELECTED),
	m_last_scancode(0), m_scancode_reg_full(false)
{ }

static MACHINE_CONFIG_FRAGMENT( keyboard )
	MCFG_AT_KEYB_ADD("at_keyboard", 1, WRITELINE(spi_kbd_device, keyboard_w))
MACHINE_CONFIG_END

machine_config_constructor spi_kbd_device::device_mconfig_additions() const
{
	return MACHINE_CONFIG_NAME( keyboard );
}

WRITE_LINE_MEMBER(spi_kbd_device::keyboard_w)
{
	// NOP if keyboard data ready line is 0
	if(!state) { return; }

	// Read keyboard scancode
	m_last_scancode = m_keyboard_dev->read(machine().dummy_space(), 0);

	// Indicate that we have data to read
	m_scancode_reg_full = true;
	m_write_irq(1);
}

void spi_kbd_device::device_start()
{
	spi_slave_device::device_start();
	m_write_irq.resolve_safe();
}

void spi_kbd_device::spi_slave_selected()
{
	m_state = SPI_KBD_NEWLY_SELECTED;
}

void spi_kbd_device::spi_slave_deselected()
{
	m_state = SPI_KBD_NOT_SELECTED;
}

uint8_t spi_kbd_device::spi_slave_exchange_byte(uint8_t recv_byte)
{
	printf("SPI recv 0x%02x\n", recv_byte);

	switch(m_state) {
	case SPI_KBD_NEWLY_SELECTED:
		if(recv_byte & 0x80) {
			// control
			m_state = SPI_KBD_READY_TO_RESPOND;
			return control(recv_byte & 0x7F);
		} else {
			// read
			m_state = SPI_KBD_READY_TO_READ;
			return m_last_scancode;
		}
	case SPI_KBD_READY_TO_READ:
		// clear scancode full flag
		m_scancode_reg_full = false;
		m_write_irq(0);
		m_last_scancode = 0x00;
		m_state = SPI_KBD_DONE;
		return 0x00;
	case SPI_KBD_READY_TO_RESPOND:
		m_state = SPI_KBD_DONE;
		return 0x00;
	default:
		return 0x00;
	}
}

// Called when there is a new control byte. Returns the response from the
// control byte.
uint8_t spi_kbd_device::control(uint8_t ctrl_byte)
{
	switch(ctrl_byte) {
	case 0x00:
		return m_scancode_reg_full ? 0x00 : 0xFF;
	default:
		return 0x00;
	}
}

// Interesting wrinkles of Buri hardware
// =====================================
//
// SPI
// ---
//
// Buri exposes a SPI interface to peripherals via the 6522 VIA:
//
//           |     |
//       PA0 |-->--| CLK
// VIA   PA1 |-->--| MOSI   SPI peripheral
//       PA7 |--<--| MISO
//           |     |
//
// Lines PA2, PA3 and PA4 are connected to a 74138 3-to-8 decoder to provide the
// chip select lines for the peripherals. Consequently there can be up to 7 SPI
// peripherals attached with device number 7 being reserved for "no device".
//
// Well known peripherals:
//
//     0 - Keyboard

class buri_state : public driver_device
{
public:
	buri_state(const machine_config &mconfig,
	           device_type type, const char *tag) :
	           driver_device(mconfig, type, tag),
	           m_maincpu(*this, "maincpu"),
	           m_mos6551(*this, MOS6551_TAG),
	           m_tms2998a(*this, TMS9929_TAG),
	           m_keyboard(*this, KEYBOARD_TAG),
	           m_via6522(*this, VIA6522_TAG),
	           m_spi_keyboard(*this, SPI_KEYBOARD_TAG)
	{
		m_selected_spi_device = 7; // no device
		m_irqs.val = 0;
	}

	DECLARE_WRITE_LINE_MEMBER(mos6551_irq_w);
	DECLARE_WRITE_LINE_MEMBER(tms9929a_irq_w);
	DECLARE_WRITE_LINE_MEMBER(via6522_irq_w);
	DECLARE_WRITE_LINE_MEMBER(spi_kbd_irq_w);
	DECLARE_WRITE_LINE_MEMBER(keyboard_data_ready);
	DECLARE_WRITE8_MEMBER(via_pa_w);

	required_device<cpu_device> m_maincpu;
	required_device<mos6551_device> m_mos6551;
	required_device<tms9929a_device> m_tms2998a;
	required_device<at_keyboard_device> m_keyboard;
	required_device<via6522_device> m_via6522;
	required_device<spi_kbd_device> m_spi_keyboard;

	union {
		uint32_t val;
		struct {
			int mos6551 : 1;
			int tms9929a : 1;
			int via6522 : 1;
		} flags;
	} m_irqs;

private:
	int m_selected_spi_device;

	void irqs_updated_() {
		m_maincpu->set_input_line(
			G65816_LINE_IRQ,
			m_irqs.val ? ASSERT_LINE : CLEAR_LINE);
	}
};

static ADDRESS_MAP_START(buri_mem, AS_PROGRAM, 8, buri_state)
	AM_RANGE(0x0000, 0xC000) AM_RAM
	AM_RANGE(0xE000, 0xFFFF) AM_ROM AM_REGION("maincpu", 0)
	AM_RANGE(0x010000, 0x07FFFF) AM_RAM
	AM_RANGE(0xE0C000, 0xE0FFFF) AM_RAM

	// VDP
	AM_RANGE(TMS9929_START, TMS9929_START)
		AM_DEVREADWRITE(TMS9929_TAG, tms9929a_device,
				vram_read, vram_write)
	AM_RANGE(TMS9929_START + 1, TMS9929_START + 1)
		AM_DEVREADWRITE(TMS9929_TAG, tms9929a_device,
				register_read, register_write)

	// ACIA
	AM_RANGE(MOS6551_START, MOS6551_START + 3)
		AM_DEVREADWRITE(MOS6551_TAG, mos6551_device, read, write)

	// SOUND
	AM_RANGE(YM3812_START, YM3812_START + 1)
		AM_DEVREADWRITE(YM3812_TAG, ym3812_device, read, write)

	// VIA
	AM_RANGE(VIA6522_START, VIA6522_START + 15)
		AM_DEVREADWRITE(VIA6522_TAG, via6522_device, read, write)
ADDRESS_MAP_END

static INPUT_PORTS_START(buri)
	PORT_INCLUDE(at_keyboard)
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

	MCFG_DEVICE_ADD(MOS6551_TAG, MOS6551, 0)
	MCFG_MOS6551_XTAL(XTAL_1_8432MHz)
	MCFG_MOS6551_TXD_HANDLER(DEVWRITELINE(UART1_TAG, rs232_port_device, write_txd))
	MCFG_MOS6551_IRQ_HANDLER(WRITELINE(buri_state, mos6551_irq_w))

	MCFG_RS232_PORT_ADD(UART1_TAG, default_rs232_devices, "pty")
	MCFG_RS232_RXD_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_rxd))
	MCFG_RS232_DCD_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_dcd))
	MCFG_RS232_DSR_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_dsr))
	MCFG_RS232_CTS_HANDLER(
		DEVWRITELINE(MOS6551_TAG, mos6551_device, write_cts))

	MCFG_DEVICE_CARD_DEVICE_INPUT_DEFAULTS("pty", terminal)
	MCFG_DEVICE_CARD_DEVICE_INPUT_DEFAULTS("terminal", terminal)

	MCFG_DEVICE_ADD(TMS9929_TAG, TMS9929A, XTAL_10_738635MHz)
	MCFG_TMS9928A_VRAM_SIZE(0x4000)
	MCFG_TMS9928A_OUT_INT_LINE_CB(WRITELINE(buri_state, tms9929a_irq_w))
	MCFG_TMS9928A_SCREEN_ADD_PAL( "screen" )
	MCFG_SCREEN_UPDATE_DEVICE( TMS9929_TAG, tms9929a_device, screen_update )

	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_ADD(YM3812_TAG, YM3812, XTAL_3_579545MHz)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	MCFG_DEVICE_ADD(VIA6522_TAG, VIA6522, XTAL_2MHz)
	MCFG_VIA6522_WRITEPA_HANDLER(WRITE8(buri_state, via_pa_w))
	MCFG_VIA6522_IRQ_HANDLER(WRITELINE(buri_state, via6522_irq_w))

	MCFG_AT_KEYB_ADD(KEYBOARD_TAG, 1, WRITELINE(buri_state, keyboard_data_ready))

	MCFG_SPI_KBD_ADD(SPI_KEYBOARD_TAG)
	MCFG_SPI_MISO_CALLBACK(DEVWRITELINE(VIA6522_TAG, via6522_device, write_pa7))
	MCFG_SPI_KBD_IRQ_CALLBACK(WRITELINE(buri_state, spi_kbd_irq_w));
MACHINE_CONFIG_END

ROM_START(buri)
	ROM_REGION(0x2000, "maincpu", 0)
	ROM_LOAD("burios.bin", 0x0000, 0x2000, NO_DUMP)
ROM_END

WRITE_LINE_MEMBER(buri_state::mos6551_irq_w)
{
	m_irqs.flags.mos6551 = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(buri_state::tms9929a_irq_w)
{
	m_irqs.flags.tms9929a = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(buri_state::via6522_irq_w)
{
	m_irqs.flags.via6522 = (state != 0);
	irqs_updated_();
}

WRITE_LINE_MEMBER(buri_state::keyboard_data_ready)
{
	// Called when there is data to be read from the keyboard.
	if(state) {
		uint8_t v = m_keyboard->read(machine().dummy_space(), 0);
		m_via6522->write_pb(machine().dummy_space(), 0, v);

		// Pulse CA1
		m_via6522->write_cb1(1);
		m_via6522->write_cb1(0);
	}
}

WRITE8_MEMBER(buri_state::via_pa_w)
{
	int clk = data & 0x1;
	int mosi = (data & 0x2) ? 1 : 0;
	m_selected_spi_device = (data & 0x1C) >> 2;

	m_spi_keyboard->write_select(m_selected_spi_device == 0);
	m_spi_keyboard->write_clock(clk);
	m_spi_keyboard->write_mosi(mosi);
}

WRITE_LINE_MEMBER(buri_state::spi_kbd_irq_w)
{
	// NOP
	// printf("SPI keyboard IRQ: %i\n", state);
}

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    CLASS         INIT    COMPANY                FULLNAME               FLAGS */
COMP(2016,  buri,   0,      0,       buri,      buri,    driver_device,0,      "Rich Wareham", "Búri homebrew computer", MACHINE_TYPE_COMPUTER)
