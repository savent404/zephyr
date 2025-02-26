#include "ad7124-8.hpp"

using namespace adc7124_8;


template <>
uint32_t adc::spi_r_<1>(cmd c)
{
    return spi_->read8(c());
}
template <>
uint32_t adc::spi_r_<2>(cmd c)
{
    return spi_->read16(c());
}
template <>
uint32_t adc::spi_r_<3>(cmd c)
{
    return spi_->read24(c());
}
template <>
uint32_t adc::spi_r_<4>(cmd c)
{
    return spi_->read32(c());
}
template <>
void adc::spi_w_<1>(cmd c, uint32_t data)
{
    spi_->write8(c(), data);
}
template <>
void adc::spi_w_<2>(cmd c, uint32_t data)
{
    spi_->write16(c(), data);
}
template <>
void adc::spi_w_<3>(cmd c, uint32_t data)
{
    spi_->write24(c(), data);
}
template <>
void adc::spi_w_<4>(cmd c, uint32_t data)
{
    spi_->write32(c(), data);
}

bool adc::check_id(void)
{
    uint8_t res;
    r_<1>(cmd{true, true, reg::REG_ID}, &res);
    return res == 0x17;
}

uint8_t adc::read_status(void)
{
    return spi_r_<1>(cmd{true, true, reg::REG_STATUS});
}

bool adc::status_is_data_ready(uint8_t status)
{
    return status & static_cast<uint8_t>(status::STATUS_RDY_N) ? false : true;
}

uint8_t adc::status_has_error(uint8_t status)
{
    const uint8_t err_mask = static_cast<uint8_t>(status::STATUS_ERR) | static_cast<uint8_t>(status::STATUS_POR);
    return status & err_mask;
}

uint8_t adc::status_get_curr_cha(uint8_t status)
{
    return status & 0x0F;
}

bool adc::read_data(uint32_t *data, uint8_t *status)
{
    bool res;
    uint32_t t;

    if (data_with_status_) {
        if (!data || !status) {
            return false;
        }
        t = spi_r_<4>(cmd{true, true, reg::REG_DATA});
        *data = t & 0x00'FF'FF'FF;
        *status = (t & 0xFF'00'00'00) >> 24;
        res = t & 0x80'00'00'00 ? false : true;
    } else {
        if (!data) {
            return false;
        }
        *data = spi_r_<3>(cmd{true, true, reg::REG_DATA});
        res = true;
    }
    return res;
}

uint32_t adc::read_diag(void)
{
    return spi_r_<3>(cmd{true, true, reg::REG_ERR});
}

void adc::adc_config(adc_clock_ref clk_ref, adc_mode mode, bool internal_vol_ref, bool data_with_status, adc_pwr_mode pwr_mode)
{
    uint8_t p = 0;
    p |= (static_cast<uint8_t>(clk_ref) << 6);
    p |= (static_cast<uint8_t>(mode) << 3);
    p |= (internal_vol_ref ? 0 : 1);
    p |= (data_with_status ? 0 : 1);
    p |= (static_cast<uint8_t>(pwr_mode) << 6);
    spi_w_<1>(cmd{true, false, reg::REG_ADC_CTRL}, p);
}

void adc::cha_config(uint8_t ch, bool enable, const cha_filter_param &param)
{

    cmd c{true, false, (reg)((uint8_t)reg::REG_CHA_0 + ch)};
    uint16_t p = 0;
    if (ch > 8) {
        return;
    }
    if (enable) {
        p |= 0x8000;
        p |= (ch << 12); /* use different setup as default */
        p |= (ch*2 << 5);
        p |= (ch*2 + 1);
    }
    spi_w_<2>(c, p);

    cmd f{true, false, (reg)((uint8_t)reg::REG_FILTER_0 + ch)};
    uint32_t f_val = 0;
    f_val |= param.fs & 0x3FF; /* output data rate */
    f_val |= (param.single_cycle ? BIT(16) : 0);
    f_val |= (static_cast<uint8_t>(param.post) & 0x7) << 17;
    f_val |= (param.reject_50_60Hz ? BIT(20) : 0);
    f_val |= (static_cast<uint8_t>(param.type) & 0x7) << 21;
    spi_w_<3>(f, f_val);
}

void adc::diag_config(uint32_t diag_mask)
{
    spi_w_<3>(cmd{true, false, reg::REG_ERR_EN}, diag_mask & 0x7F'FF'FF);
}

