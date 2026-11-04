/* SPDX-License-Identifier: MIT */
#ifndef J313_BOARDINFO_H
#define J313_BOARDINFO_H
#include <stdint.h>

enum bi_board { BI_BOARD_UNKNOWN, BI_J313 };
enum bi_soc { BI_SOC_UNKNOWN, BI_T8103 };
enum bi_gpio_controller { BI_GPIO_UNKNOWN, BI_GPIO_AP, BI_GPIO_NUB };
enum bi_level { BI_LEVEL_UNKNOWN, BI_LEVEL_HIGH, BI_LEVEL_LOW };
enum bi_divisor_rule { BI_DIV_UNKNOWN, BI_DIV_RAW, BI_DIV_RAW_PLUS_ONE };
/* Exactly CONFIRMED + nonzero caller audit reference is required. */
enum bi_state { BI_UNKNOWN, BI_EXPECTED, BI_CONFIRMED, BI_REJECTED };
struct bi_evidence { enum bi_state state; uint64_t reference; };
enum bi_fact {
 BI_IDENTITY, BI_SPI_MAPPED, BI_SPI_DEVICE_MEMORY,
 BI_AP_MAPPED, BI_AP_DEVICE_MEMORY, BI_NUB_MAPPED, BI_NUB_DEVICE_MEMORY,
 BI_SPI_POWER_ACTUAL, BI_AP_POWER_ACTUAL, BI_NUB_POWER_ACTUAL,
 BI_CLOCK_ACTUAL, BI_PINMUX_ACTUAL, BI_CS_ROUTE, BI_ENABLE_ROUTE,
 BI_HID_IRQ_ROUTE, BI_CONTROLLER_IRQ_ROUTE, BI_DIVISOR_ACTUAL,
 BI_BUS_MODE_ACTUAL, BI_EXCLUSIVE_STABLE_LEASE, BI_FACT_COUNT
};
struct bi_mmio { uint64_t physical_base, mapped_base, size; };
struct bi_gpio {
 enum bi_gpio_controller controller;
 uint32_t pin;
 enum bi_level active;
};
/* Only public-DTS expectations, never a runtime initializer. */
struct bi_profile {
 enum bi_board board;
 enum bi_soc soc;
 uint64_t spi_base, spi_size, ap_base, nub_base, clock_hz;
 uint32_t max_sck_hz, cs, controller_irq;
 enum bi_level controller_irq_level;
 struct bi_gpio enable, hid_irq;
 uint32_t mux_pins[4], mux_function;
 uint32_t setup_us, hold_us, inactive_us;
};
struct bi_request {
 enum bi_board board;
 enum bi_soc soc;
 struct bi_mmio spi, ap, nub;
 uint64_t clock_hz, sck_hz;
 uint32_t divider;
 enum bi_divisor_rule divisor_rule;
 uint32_t cs, controller_irq;
 enum bi_level controller_irq_level;
 struct bi_gpio enable, hid_irq;
 uint32_t mux_pins[4], mux_function;
 uint32_t cpol, cpha, bits_per_word, msb_first;
 uint32_t setup_us, hold_us, inactive_us;
 struct bi_evidence evidence[BI_FACT_COUNT];
};
enum bi_error {
 BI_OK, BI_NULL_REQUEST, BI_WRONG_IDENTITY, BI_BAD_MMIO,
 BI_ADDRESS_OVERFLOW, BI_BAD_ROUTE, BI_BAD_PINMUX, BI_BAD_CLOCK,
 BI_BAD_DIVIDER, BI_BAD_BUS, BI_UNCONFIRMED
};
/* A value snapshot, not a hardware capability or firmware authorization. */
struct bi_validated_resource { struct bi_request snapshot; uint64_t divisor; };
struct bi_result {
 enum bi_error error;
 enum bi_fact missing_fact; /* meaningful only for BI_UNCONFIRMED */
 struct bi_validated_resource resource; /* all-zero on every failure */
};
struct bi_profile bi_expected_profile(void);
struct bi_result bi_validate(const struct bi_request *request);
#endif
