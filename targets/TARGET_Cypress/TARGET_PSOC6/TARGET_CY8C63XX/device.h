/*
 * mbed Microcontroller Library
 * Copyright (c) 2017-2018 Future Electronics
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MBED_DEVICE_H
#define MBED_DEVICE_H

/*----------------------------------------------------------------------------*/
/** Config options.                                                           */
/*----------------------------------------------------------------------------*/
/** ALTHF (BLE ECO) frequency in Hz */
#define CYDEV_CLK_ALTHF__HZ                 (  8000000UL)

/*----------------------------------------------------------------------------*/

#include "cmsis.h"
#include "objects.h"

/*
 * Board clocks.
 */
/** IMO frequency in Hz */
#define CY_CLK_IMO_FREQ_HZ                  (  8000000UL)
/** PILO frequency in Hz */
#define CY_CLK_PILO_FREQ_HZ                 (    32768UL)

/** WCO frequency in Hz */
#define CY_CLK_WCO_FREQ_HZ                  (    32768UL)

/** HVILO frequency in Hz */
#define CY_CLK_HVILO_FREQ_HZ                (    32000UL)

/** ALTLF frequency in Hz */
#define CY_CLK_ALTLF_FREQ_HZ                (    32768UL)

/** Default HFClk frequency in Hz */
#ifndef CY_CLK_HFCLK0_FREQ_HZ
#define CY_CLK_HFCLK0_FREQ_HZ               (100000000UL)
#endif

/** Default PeriClk frequency in Hz */
#ifndef CY_CLK_PERICLK_FREQ_HZ
#define CY_CLK_PERICLK_FREQ_HZ              (CY_CLK_HFCLK0_FREQ_HZ / 2)
#endif

/** Default SlowClk system core frequency in Hz */
#ifndef CY_CLK_SYSTEM_FREQ_HZ
#define CY_CLK_SYSTEM_FREQ_HZ               (CY_CLK_PERICLK_FREQ_HZ)
#endif


/** Interrupt assignment for CM0+ core.
 * On PSoC6 CM0+ core physical interrupt are routed into NVIC through a programmable
 * multiplexer. This requires that we define which of the 32 NVIC channels is used
 * by which interrupt. This is done here.
 */
#define CY_M0_CORE_IRQ_CHANNEL_US_TICKER    ((IRQn_Type)0)
#define CY_M0_CORE_IRQ_CHANNEL_SERIAL       ((IRQn_Type)4)
#define CY_M0_CORE_IRQ_CHANNEL_BLE          ((IRQn_Type)3)

/** Identifiers used in allocation of NVIC channels.
 */
#define CY_US_TICKER_IRQN_ID                (0x100)
#define CY_SERIAL_IRQN_ID                   (0x200)
#define CY_BLE_IRQN_ID                      (0x300)
#define CY_GPIO_IRQN_ID                     (0x400)
#define CY_LP_TICKER_IRQN_ID                (0x500)
#endif
