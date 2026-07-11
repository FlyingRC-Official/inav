/*
 * This file is part of INAV.
 *
 * INAV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <stdint.h>

#include "platform.h"

#include "drivers/bus.h"
#include "drivers/io.h"
#include "drivers/pwm_mapping.h"
#include "drivers/timer.h"
#include "drivers/pinio.h"
#include "drivers/sensor.h"

BUSDEV_REGISTER_SPI_TAG(busdev_bmi270_1, DEVHW_BMI270, BMI270_1_SPI_BUS, BMI270_1_CS_PIN, NONE, 0, DEVFLAGS_NONE, IMU_BMI270_1_ALIGN);
BUSDEV_REGISTER_SPI_TAG(busdev_bmi270_2, DEVHW_BMI270, BMI270_2_SPI_BUS, BMI270_2_CS_PIN, NONE, 1, DEVFLAGS_NONE, IMU_BMI270_2_ALIGN);
BUSDEV_REGISTER_SPI_TAG(busdev_icm42688, DEVHW_ICM42605, BMI270_1_SPI_BUS, BMI270_1_CS_PIN, NONE, 0, DEVFLAGS_NONE, IMU_ICM42688_ALIGN);
BUSDEV_REGISTER_SPI_TAG(busdev_icm42605, DEVHW_ICM42605, BMI270_2_SPI_BUS, BMI270_2_CS_PIN, NONE, 1, DEVFLAGS_NONE, IMU_ICM42605_ALIGN);

timerHardware_t timerHardware[] = {
    DEF_TIM(TIM3, CH3, PB0, TIM_USE_OUTPUT_AUTO, 0, 0),
    DEF_TIM(TIM3, CH4, PB1, TIM_USE_OUTPUT_AUTO, 0, 1),
    DEF_TIM(TIM5, CH1, PA0, TIM_USE_OUTPUT_AUTO, 0, 2),
    DEF_TIM(TIM5, CH2, PA1, TIM_USE_OUTPUT_AUTO, 0, 3),
    DEF_TIM(TIM5, CH3, PA2, TIM_USE_OUTPUT_AUTO, 0, 4),
    DEF_TIM(TIM5, CH4, PA3, TIM_USE_OUTPUT_AUTO, 0, 5),
    DEF_TIM(TIM4, CH1, PD12, TIM_USE_OUTPUT_AUTO, 0, 6),
    DEF_TIM(TIM4, CH2, PD13, TIM_USE_OUTPUT_AUTO, 0, 7),
    DEF_TIM(TIM4, CH3, PD14, TIM_USE_OUTPUT_AUTO, 0, 0),
    DEF_TIM(TIM4, CH4, PD15, TIM_USE_OUTPUT_AUTO, 0, 0),
    DEF_TIM(TIM15, CH1, PE5, TIM_USE_OUTPUT_AUTO, 0, 0),
    DEF_TIM(TIM15, CH2, PE6, TIM_USE_OUTPUT_AUTO, 0, 0),
    DEF_TIM(TIM1, CH1, PA8, TIM_USE_LED, 0, 9),
    DEF_TIM(TIM2, CH1, PA15, TIM_USE_BEEPER, 0, 0),
    DEF_TIM(TIM8, CH1, PC6, TIM_USE_ANY, 0, 0),
};

const int timerHardwareCount = sizeof(timerHardware) / sizeof(timerHardware[0]);
