// license:BSD-3-Clause
// copyright-holders:Rich Wareham
#ifndef SPI_SLAVE_H
#define SPI_SLAVE_H

#include "emu.h"

// SPI slave device
// ----------------
//
// A SPI slave device exposes three serial lines, MOSI, MISO and CLK which
// understand the SPI protocol. There is also a select line which is used to
// enable the device. Setting MOSI or CLK will have no effect when the select
// line is 0. Two parameters are used to define the exact flavour of SPI:
//
// 1. SPI "mode": 0-3 defines clock idle and active states along with when data
//    is read and output sent.
// 2. Data direction: whether bytes are sent most significant bit (MSB) first or
//    least significant bit first (LSB).
//
// SPI is full duplex; each communication exchanges a byte from the master to
// the slave and a byte from the slave to the master.

enum spi_mode_t { SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3 };
enum spi_data_direction_t { SPI_MSB_FIRST, SPI_LSB_FIRST };

#define MCFG_SPI_ADD( _tag ) MCFG_DEVICE_ADD( _tag, SPI, 0 )
#define MCFG_SPI_MODE( mode ) spi_slave_device::set_mode(*device, mode);
#define MCFG_SPI_DATA_DIRECTION( dir ) spi_slave_device::set_data_direction(*device, dir);
#define MCFG_SPI_MISO_CALLBACK(_miso) \
	downcast<spi_slave_device *>(device)->set_miso_callback(DEVCB_##_miso);

class spi_slave_device_interface
{
protected:
	// device was selected & previously wasn't
	virtual void spi_slave_select() = 0;

	// device was deselected & previously was
	virtual void spi_slave_deselect() = 0;

	// a byte has been received from the master
	virtual void spi_slave_mosi_byte(uint8_t) = 0;
};

class spi_slave_device : public device_t, public spi_slave_device_interface
{
public:
	// construction/destruction
	spi_slave_device(const machine_config &mconfig, const char *tag,
	           device_t *owner, uint32_t clock );

	template<class _miso> void set_miso_callback(_miso miso) {
		m_write_miso.set_callback(miso);
	}

	// Set the byte which is sent to the master on the next communication.
	// After the byte is sent, the next miso byte is reset to zero before
	// spi_slave_mosi_byte() is called.
	void set_miso_byte(uint8_t);

	DECLARE_WRITE_LINE_MEMBER(write_select);
	DECLARE_WRITE_LINE_MEMBER(write_clock);
	DECLARE_WRITE_LINE_MEMBER(write_mosi);
	DECLARE_READ_LINE_MEMBER(read_miso);

	static void set_mode(device_t &device, spi_mode_t mode) {
		downcast<spi_slave_device &>(device).m_mode = mode;
	}

	static void set_data_direction(device_t &device, spi_data_direction_t data_dir) {
		downcast<spi_slave_device &>(device).m_data_dir = data_dir;
	}

protected:
	// device-level overrides
	virtual void device_start() override;
	virtual void device_reset() override;

	// SPI slave overrides
	virtual void spi_slave_select() override;
	virtual void spi_slave_deselect() override;
	virtual void spi_slave_mosi_byte(uint8_t) override;

private:
	spi_mode_t m_mode;
	spi_data_direction_t m_data_dir;
	bool m_selected;
	int m_clk, m_mosi, m_miso;

	uint8_t m_recv_byte, m_send_byte;
	int m_recv_count, m_send_count;

	devcb_write_line m_write_miso;

	void clk_edge_(int is_idle_to_active);

	inline void set_miso(int state) {
		m_miso = state; m_write_miso(m_miso);
	}

	inline int cpol() {
		return ((m_mode == SPI_MODE2) || (m_mode == SPI_MODE3)) ? 1 : 0;
	}

	inline int cpha() {
		return ((m_mode == SPI_MODE1) || (m_mode == SPI_MODE3)) ? 1 : 0;
	}
};

extern const device_type SPI_SLAVE;

#endif
