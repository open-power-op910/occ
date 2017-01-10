/* IBM_PROLOG_BEGIN_TAG                                                   */
/* This is an automatically generated prolog.                             */
/*                                                                        */
/* $Source: src/occ_405/amec/amec_sensors_power.c $                       */
/*                                                                        */
/* OpenPOWER OnChipController Project                                     */
/*                                                                        */
/* Contributors Listed Below - COPYRIGHT 2011,2016                        */
/* [+] International Business Machines Corp.                              */
/*                                                                        */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/*                                                                        */
/* IBM_PROLOG_END_TAG                                                     */

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <occ_common.h>
#include <ssx.h>
#include <errl.h>               // Error logging
#include "sensor.h"
#include "rtls.h"
#include "occ_sys_config.h"
#include "occ_service_codes.h"  // for SSX_GENERIC_FAILURE
#include "dcom.h"
#include "proc_data.h"
#include "amec_smh.h"
#include "amec_slave_smh.h"
#include <trac.h>
#include "amec_sys.h"
#include "sensor_enum.h"
#include "amec_service_codes.h"
#include <amec_sensors_power.h>
#include <cmdh_snapshot.h>
#include "amec_oversub.h"
#include "avsbus.h"

/******************************************************************************/
/* Globals                                                                    */
/******************************************************************************/
// This holds the converted ADC Reads
uint32_t G_lastValidAdcValue[MAX_APSS_ADC_CHANNELS] = {0};

// There are only MAX_APSS_ADC_CHANNELS channels.  Therefore if the channel value
// is greater then the MAX, then there was no channel associated with the function id.
#define ADC_CONVERTED_VALUE(i_chan) \
    ((i_chan < MAX_APSS_ADC_CHANNELS) ? G_lastValidAdcValue[i_chan] : 0)

extern uint8_t G_occ_interrupt_type;

//*************************************************************************
// Code
//*************************************************************************

// Function Specification
//
// Name: amec_sensor_from_apss_adc
//
// Description: Calculates sensor from raw ADC value
//
// Thread: RealTime Loop
//
// End Function Specification
uint32_t amec_value_from_apss_adc(uint8_t i_chan)
{
    uint16_t l_raw = 0;
    uint32_t l_temp = 0;
    uint32_t l_gain = 0;
    uint32_t l_offset = 0;

    if(i_chan != SYSCFG_INVALID_ADC_CHAN)
    {
        /*
         * The APSS value is in mV or mA depending on the channel and the raw
         * reading from the APSS must be decoded using the following info:
         *
         * APSS LSB: Vref/4096
         * Full scale: (Vref - LSB) = 0xFFF
         * Midscale: Vref/2 = 0x800
         * Midscale - 1 LSB: Vref/2 - 1 LSB, 0x7FF
         * Zero: 0V, 0x000
         * Any voltage at or above Vref will will result in an ADC channel value
         * of 0xFFF
         *
         * Our APSS has Vref pinned to 2.048 V
         * LSB: 0.5 mV (minimum ADC resolution)
         * Full scale: 2047.5 mV (0xFFF)
         * Midscale: 1024 mV (0x800)
         * Zero: 0V, 0x000
         *
         * To get the right mV reading from the raw APSS data all we need to do
         * is divide the raw 12 bit ADC code by 2.  The same logic applies if
         * the channel is measuring power in mA.
         *
         * If there is an offset it will bein mV or mA depending upon the sensor
         * and it needs to be added or subtracted from the raw value depending
         * on it's signedness.  Negative offsets are stored in 2's complement
         * form.
         *
         * The gain values will be multiplied by 1000 in TMGT before being sent
         * in the sysconfig packet.  Raw gain from the MRW values are in A/V and
         * TMGT will multiply this by 1000 before sending in the sysconfig
         * packet to preserve precision.  That makes the gain units mA/V.
         *
         * To apply the gain multiply the sysconfig value against the converted
         * APSS voltage.
         *
         * Applying the gain to the converted APSS data gives (mV . mA)/V so we
         * divide by 1000 to reduce the result to mA.  This is the unit that
         * is returned to the caller. For example:
         * raw APSS value: 0x800
         * converted value: raw/2 = 1024 mV
         * gain from MRW: 10.00 A/V
         * Converted gain: 10,000 mA/V
         * gain adjusted output: 1024mV * 10,000 mA/V = 10,240,000 (mV . mA)/V
         * Reduced value: adjusted/1000 = 10,240 mA
         *
         * Note that in the case of the remote ground and 12V sense the gain
         * values are in V/V so the returned value is actually in mVs.
         *
         * Max returnable value is 4,294,967,295 mA or approx. 4.3 MA
         */

        // Get ADC Mapping calibration info for this entity.
        l_gain = G_sysConfigData.apss_cal[i_chan].gain;
        l_offset = G_sysConfigData.apss_cal[i_chan].offset;

        // Read Raw Value in mA (divide masked channel data by 2)
        l_raw = (G_dcom_slv_inbox_rx.adc[i_chan] & APSS_12BIT_ADC_MASK)/2;
        // Apply offset and gain
        if (l_offset & 0x80000000)
        {
            // Negative offset
            l_raw -= (~l_offset + 1);
        }
        else
        {
            l_raw += l_offset;
        }
        //Check to see if l_raw is negative.  If so, set raw to 0
        if (l_raw & 0x8000)
        {
            l_raw = 0;
        }

        l_temp = ((uint32_t)l_raw * l_gain);
        // Reduce value back to mA or mV
        l_temp /= 1000;
    }

    AMEC_DBG("APSS ADC info: chan=%d, raw=0x%04x, offset=%d, gain=%d calibrated output=%d\n",
             i_chan, l_raw, l_offset, l_gain, l_temp);

    return l_temp;
}

#define ADCMULT_TO_UNITS 1000000
#define ADCMULT_ROUND ADCMULT_TO_UNITS/2
// Function Specification
//
// Name: amec_update_apss_sensors
//
// Description: Calculates sensor from raw ADC values obtained from APSS
//
// Thread: RealTime Loop
//
// End Function Specification
void amec_update_apss_sensors(void)
{
    // Need to check to make sure APSS data has been received
    // via slave inbox first
    if (G_slv_inbox_received)
    {
        uint8_t l_proc   = G_pbax_id.module_id;
        uint32_t temp32  = 0;
        uint8_t  l_idx   = 0;
        uint32_t l_bulk_current_sum = 0;

        // ----------------------------------------------------
        // Convert all ADC Channels immediately
        // ----------------------------------------------------
        for (l_idx = 0; l_idx < MAX_APSS_ADC_CHANNELS; l_idx++)
        {
            // These values returned are gain adjusted. The APSS readings for
            // the remote ground and 12V sense are returned in mVs, all other
            // readings are treated as mAs.
            G_lastValidAdcValue[l_idx] = amec_value_from_apss_adc(l_idx);

            // Add up all channels now, we will subtract ones later that don't
            // count towards the system power
            l_bulk_current_sum += G_lastValidAdcValue[l_idx];
        }

        // --------------------------------------------------------------
        // Convert 12Vsense into interim value - this has to happen first
        // --------------------------------------------------------------
        // Calculations involving bulk_voltage must be 64bit so final result
        // does not get truncated (before dividing by ADCMULT_TO_UNITS)
        uint64_t l_bulk_voltage = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.sense_12v);

        if (OCC_MASTER == G_occ_role)
        {
            // Update channel sensors for all channels (except 12v sense and gnd)
            for (l_idx = 0; l_idx < MAX_APSS_ADC_CHANNELS; l_idx++)
            {
                if(l_idx == G_sysConfigData.apss_adc_map.current_12v_stby)
                {
                    // Save value of 12V Standby Current (.01A) in a sensor for lab use only
                    temp32 = ADC_CONVERTED_VALUE(l_idx)/100;  // convert mA to .01A
                    sensor_update(AMECSENSOR_PTR(CUR12VSTBY), (uint16_t) temp32);
                }
                else if((l_idx != G_sysConfigData.apss_adc_map.sense_12v) &&
                        (l_idx != G_sysConfigData.apss_adc_map.remote_gnd))
                {
                    temp32 = ((ADC_CONVERTED_VALUE(l_idx) * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
                    sensor_update(AMECSENSOR_PTR(PWRAPSSCH0 + l_idx), (uint16_t) temp32);
                }
            }
        }

        // ----------------------------------------------------------
        // Convert Raw Vdd/Vcs/Vio/Vpcie Power from APSS into sensors
        // ----------------------------------------------------------
        // Some sensor values are in Watts so after getting the mA readings we
        // multiply by the bulk voltage (mVs) which requires us to then divide
        // by 1000000 to get W (A.V), ie.
        //  divide by    1000 to get it back to milliUnits (0.001)
        //  divide by   10000 to get it to centiUnits      (0.01)
        //  divide by  100000 to get it to deciUnits       (0.1)
        //  divide by 1000000 to get it to Units           (1)

        //Update channel specific sensors based on saved pairing between function Ids and Channels.

        uint32_t l_vdd = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.vdd[l_proc]);
        uint32_t l_vcs_vio_vpcie = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.vcs_vio_vpcie[l_proc]);
        temp32 = ((l_vcs_vio_vpcie + l_vdd) * l_bulk_voltage)/ADCMULT_TO_UNITS;
        sensor_update(AMECSENSOR_PTR(PWR250USP0), (uint16_t) temp32);

        // Save off the combined power from all modules
        for (l_idx=0; l_idx < MAX_NUM_CHIP_MODULES; l_idx++)
        {
            uint32_t l_vd = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.vdd[l_idx]);
            uint32_t l_vpcie = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.vcs_vio_vpcie[l_idx]);
            g_amec->proc_snr_pwr[l_idx] =  ((l_vpcie + l_vd) * l_bulk_voltage)/ADCMULT_TO_UNITS;
        }

        // All readings from APSS come back as milliUnits, so if we want
        // to convert one, we need to
        //  divide by    1 to get it back to milliUnits (0.001)
        //  divide by   10 to get it to centiUnits      (0.01)
        //  divide by  100 to get it to deciUnits       (0.1)
        //  divide by 1000 to get it to Units           (1)
        // Vdd has both a power and a current sensor, we convert the Vdd power
        // to Watts and the current to centiAmps
        temp32 = ((l_vdd * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
        sensor_update( AMECSENSOR_PTR(PWR250USVDD0), (uint16_t)temp32);
        temp32 = ((l_vcs_vio_vpcie * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
        sensor_update( AMECSENSOR_PTR(PWRVCSVIOVDN), (uint16_t)temp32);

        // ----------------------------------------------------
        // Convert Other Raw Misc Power from APSS into sensors
        // ----------------------------------------------------

        // Fans: Add up all Fan channels
        temp32  = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.fans[0]);
        temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.fans[1]);
        temp32  = ((temp32  * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
        sensor_update( AMECSENSOR_PTR(PWR250USFAN), (uint16_t)temp32);

        // I/O: Add up all I/O channels
        temp32  = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.io[0]);
        temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.io[1]);
        temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.io[2]);
        temp32 = ((temp32  * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
        sensor_update( AMECSENSOR_PTR(PWR250USIO), (uint16_t)temp32);

        // Memory: Add up all channels for the same processor.
        temp32 = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.memory[l_proc][0]);
        temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.memory[l_proc][1]);
        temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.memory[l_proc][2]);
        temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.memory[l_proc][3]);
        //Only for FSP-LESS systems do we add in Centaur power because it is measured on its own A/D channel, but is part of memory power
        if (FSP_SUPPORTED_OCC != G_occ_interrupt_type)
        {
            temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.mem_cache);
        }
        temp32 = ((temp32  * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
        sensor_update( AMECSENSOR_PTR(PWR250USMEM0), (uint16_t)temp32);

        // Save off the combined power from all memory
        for (l_idx=0; l_idx < MAX_NUM_CHIP_MODULES; l_idx++)
        {
            uint32_t l_temp = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.memory[l_idx][0]);
            l_temp += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.memory[l_idx][1]);
            l_temp += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.memory[l_idx][2]);
            l_temp += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.memory[l_idx][3]);
            g_amec->mem_snr_pwr[l_idx] = ((l_temp  * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
        }

        // Storage/Media
        temp32  = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.storage_media[0]);
        temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.storage_media[1]);
        temp32  = ((temp32  * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
        sensor_update( AMECSENSOR_PTR(PWR250USSTORE), (uint16_t)temp32);

        // Save total GPU adapter for this proc
        if (l_proc < MAX_GPU_DOMAINS)
        {
            temp32 = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.gpu[l_proc][0]);
            temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.gpu[l_proc][1]);
            temp32 += ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.gpu[l_proc][2]);
            temp32 = ((temp32 * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
            sensor_update( AMECSENSOR_PTR(PWRGPU), (uint16_t)temp32);
        }

        // ----------------------------------------------------
        // Convert Raw Bulk Power from APSS into sensors
        // ----------------------------------------------------
        // We don't get this adc channel in some systems, we have to add it manually.
        // With valid sysconfig data the code here should automatically use what
        // is provided by the APSS if it is available, or manually sum it up if not.
        temp32 = ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.total_current_12v);
        temp32 = ((temp32 * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;

        // To calculated the total 12V current based on a sum of all ADC channels,
        // Subract adc channels that don't measure power
        l_bulk_current_sum -= ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.sense_12v);
        l_bulk_current_sum -= ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.remote_gnd);
        l_bulk_current_sum -= ADC_CONVERTED_VALUE(G_sysConfigData.apss_adc_map.current_12v_stby);

        // If we don't have a ADC channel that measures the bulk 12v power, use
        // the ADC sum instead
        if(0 == temp32)
        {
            temp32 = ((l_bulk_current_sum * l_bulk_voltage)+ADCMULT_ROUND)/ADCMULT_TO_UNITS;
        }
        sensor_update(AMECSENSOR_PTR(PWR250US), (uint16_t)temp32);

        // Calculate average frequency of all OCCs.
        uint32_t    l_allOccAvgFreqOver250us = 0;
        uint8_t     l_presentOCCs = 0;
        uint8_t     l_occCount = 0;

        // Add up the average freq from all OCCs.
        for (l_occCount = 0; l_occCount < MAX_OCCS; l_occCount++)
        {
            if (G_sysConfigData.is_occ_present & (1<< l_occCount))
            {
                l_allOccAvgFreqOver250us += G_dcom_slv_outbox_rx[l_occCount].freqa4msp0;
                l_presentOCCs++;
            }
        }
        //Calculate average of all the OCCs.
        l_allOccAvgFreqOver250us /= l_presentOCCs;

        // Save the max and min pwr250us sensors and keep an accumulator of the
        // average frequency over 30 seconds.
        if (g_pwr250us_over30sec.count == 0)
        {
            //The counter has been reset, therefore initialize the stored values.
            g_pwr250us_over30sec.max = (uint16_t) temp32;
            g_pwr250us_over30sec.min = (uint16_t) temp32;
            g_pwr250us_over30sec.freqaAccum = l_allOccAvgFreqOver250us;
        }
        else
        {
            //Check for max.
            if (temp32 > g_pwr250us_over30sec.max)
            {
                g_pwr250us_over30sec.max = (uint16_t) temp32;
            }
            //Check for min.
            if (temp32 < g_pwr250us_over30sec.min)
            {
                g_pwr250us_over30sec.min = (uint16_t) temp32;
            }
            //Average frequency accumulator.
            g_pwr250us_over30sec.freqaAccum += l_allOccAvgFreqOver250us;
        }

        //Count of number of updates.
        g_pwr250us_over30sec.count++;

        // ----------------------------------------------------
        // Clear Flag to indicate that AMEC has received the data.
        // ----------------------------------------------------
        G_slv_inbox_received = FALSE;
    }
    else
    {
        // Skip it...AMEC Health Monitor will figure out we didn't
        // update this sensor.
    }
}


// Read the current from AVS Bus and update sensors
void process_avsbus_current()
{
    if (G_avsbus_vdd_monitoring)
    {
        // Read Vdd current (returns 10mA)
        uint32_t current = avsbus_read(AVSBUS_VDD, AVSBUS_CURRENT);
        if (current != 0)
        {
            // Current value stored in the sensor should be in 10mA (scale -2)
            sensor_update(AMECSENSOR_PTR(CURVDD), (uint16_t)current);
        }
    }
    if (G_avsbus_vdn_monitoring)
    {
        // Read Vdn current (returns 10mA)
        uint32_t current = avsbus_read(AVSBUS_VDN, AVSBUS_CURRENT);
        if (current != 0)
        {
            // Current value stored in the sensor should be in 10mA (scale -2)
            sensor_update(AMECSENSOR_PTR(CURVDN), (uint16_t)current);
        }
    }
}


// Read the voltage from AVS Bus and update sensors
void process_avsbus_voltage()
{
    if (G_avsbus_vdd_monitoring)
    {
        // Read Vdd voltage (returns mV)
        uint32_t voltage = avsbus_read(AVSBUS_VDD, AVSBUS_VOLTAGE);
        if (voltage != 0)
        {
            // Voltage value stored in the sensor should be in 100mV (scale -1)
            voltage /= 100;
            sensor_update(AMECSENSOR_PTR(VOLTVDD), (uint16_t)voltage);
        }
    }
    if (G_avsbus_vdn_monitoring)
    {
        // Read Vdn voltage (returns mV)
        uint32_t voltage = avsbus_read(AVSBUS_VDN, AVSBUS_VOLTAGE);
        if (voltage != 0)
        {
            // Voltage value stored in the sensor should be in 100mV (scale -1)
            voltage /= 100;
            sensor_update(AMECSENSOR_PTR(VOLTVDN), (uint16_t)voltage);
        }
    }
}


// Calculate chip voltage and power and update sensors
void update_avsbus_power_sensors(const avsbus_type_e i_type)
{
    static bool L_throttle_vdd = FALSE;
    static bool L_throttle_vdn = FALSE;
    bool * L_throttle = &L_throttle_vdd;
    // TODO: RTC 130216 : read loadline and distloss from Pstate Super Structure
    uint16_t l_loadline = 0x61AB; // OCCPstateParmBlock.vdd_sysparm.loadline_uohm
    uint16_t l_distloss = 0x0000; // OCCPstateParmBlock.vdd_sysparm.distloss_uohm
    uint32_t l_currentSensor = CURVDD;
    uint32_t l_voltageSensor = VOLTVDD;
    uint32_t l_voltageChip = VOLTVDDSENSE;
    uint32_t l_powerSensor = PWRVDD;
    if (AVSBUS_VDN == i_type)
    {
        L_throttle = &L_throttle_vdn;
        l_loadline = 0x6BA8; // OCCPstateParmBlock.vdn_sysparm.loadline_uohm
        l_distloss = 0x0000; // OCCPstateParmBlock.vdn_sysparm.distloss_uohm
        l_currentSensor = CURVDN;
        l_voltageSensor = VOLTVDN;
        l_voltageChip = VOLTVDNSENSE;
        l_powerSensor = PWRVDN;
    }

    // Read latest voltage/current sensors
    const sensor_t *volt = getSensorByGsid(l_voltageSensor);
    const uint32_t l_voltage_100mv = volt->sample;
    const sensor_t *curr = getSensorByGsid(l_currentSensor);
    const uint32_t l_current_10ma = curr->sample;

#ifdef AVSDEBUG
    // TODO: RTC 130216 : REMOVE AFTER VERIFYING loadline/distlost from Pstate Super Structure
    static uint8_t L_traceCount = 0;
    if (L_traceCount < 4)
    {
        TRAC_INFO("update_avsbus_power_sensors: #%d Vd%c=%dx100mV, I=%dx10mA", L_traceCount, (i_type==AVSBUS_VDD)?'d':'n',
                  l_voltage_100mv, l_current_10ma);
        TRAC_INFO("update_avsbus_power_sensors: #%d Vd%c Rloadline=%d, Rdistloss=%d", L_traceCount, (i_type==AVSBUS_VDD)?'d':'n',
                  l_loadline, l_distloss);
    }
#endif

    if ((l_voltage_100mv != 0) && (l_current_10ma != 0))
    {
        // Calculate voltage on just processor package (need to take load-line into account)
        // Voltage value stored in the sensor should be in 100mV (scale -1)
        // (current is in 10mA units, and load-line is in tenth of microOhms)
        // v(V)     = i(10mA)*(1 A/1000 mA) * r(1 uOhm)*(1 Ohm/1,000,000 uOhm)
        //          = i * (1 A/100) * r * (1 Ohm/1,000,000)
        //          = i * r / 100,000,000
        // v(mV)    = v(V) * 1,000
        // v(100mV) = v(mV) / 100
        //          = (v(V) * 1,000) / 100 = v(V) * 10
        //          = (i * r / 100,000,000) * 10 = i * r / 10,000,000
        // NOTE: distloss is the same as Rpath in the WOF algorithm
        const uint64_t l_volt_drop_100mv = (l_current_10ma * (l_loadline+l_distloss)) / 10000000;
        // Calculate chip voltage
        int32_t l_chip_voltage_100mv = l_voltage_100mv - l_volt_drop_100mv;
        if ((l_chip_voltage_100mv <= 0) || (l_chip_voltage_100mv > 0xFFFF))
        {
            // Voltage out of range, do not write sensors
            if (!*L_throttle)
            {
                TRAC_ERR("update_avsbus_power_sensors: chip voltage out of range! %dmV - %d(100mV) = %d(100mV)",
                         l_voltage_100mv, WORD_LOW(l_volt_drop_100mv), l_chip_voltage_100mv);
                *L_throttle = TRUE;
            }
        }
        else
        {
            *L_throttle = FALSE;

            // Update chip voltage (remote sense adjusted for loadline) (100mV units)
            sensor_update(AMECSENSOR_PTR(l_voltageChip), (uint16_t)l_chip_voltage_100mv);

            // Power value stored in the sensor should be in W (scale 0)
            // p(W) = v(V) * i(A) = v(100mV)*100/1000 * i(10mA)*10/1000
            //                    = v(100mV)/10       * i(10mA)/100
            //                    = v(100mv) * i(10mA) / 1000
            const uint32_t l_power = l_chip_voltage_100mv * l_current_10ma / 1000;
            sensor_update(AMECSENSOR_PTR(l_powerSensor), (uint16_t)l_power);
        }

#ifdef AVSDEBUG
        // TODO: RTC 130216 : REMOVE AFTER VERIFYING loadline/distlost from Pstate Super Structure
        if (L_traceCount < 4)
        {
            const sensor_t *power = getSensorByGsid(l_powerSensor);
            TRAC_INFO("update_avsbus_power_sensors: #%d Vd%cs=%dx100mV, P=%dW", L_traceCount, (i_type==AVSBUS_VDD)?'d':'n',
                      l_chip_voltage_100mv, power->sample);
        }
#endif
    }

#ifdef AVSDEBUG
    // TODO: RTC 130216 : REMOVE AFTER VERIFYING loadline/distlost from Pstate Super Structure
    ++L_traceCount;
#endif

} // end update_avsbus_power_sensors()


// Function Specification
//
// Name: amec_update_avsbus_sensors
//
// Description: Read AVS Bus data and update sensors (called every tick)
//   The very first tick kicks off a read of the currents for both buses (Vdd/Vdn).
//   The next tick will process the currents and then start a read of the voltages.
//   The next tick will process the voltages and then start a read of the currents.
//   (readings for each bus will essentially be updated every 2 ticks)
//
// Thread: RealTime Loop
//
// End Function Specification
void amec_update_avsbus_sensors(void)
{

    static enum {
        AVSBUS_STATE_DISABLED           = 0,
        AVSBUS_STATE_INITIATE_READ      = 1,
        AVSBUS_STATE_PROCESS_CURRENT    = 2,
        AVSBUS_STATE_PROCESS_VOLTAGE    = 3
    } L_avsbus_state = AVSBUS_STATE_INITIATE_READ;

    if (isSafeStateRequested())
    {
        L_avsbus_state = AVSBUS_STATE_DISABLED;
        G_avsbus_vdd_monitoring = FALSE;
        G_avsbus_vdn_monitoring = FALSE;
    }

    switch (L_avsbus_state)
    {
        case AVSBUS_STATE_INITIATE_READ:
            // Start first AVS Bus read of current
            initiate_avsbus_reads(AVSBUS_CURRENT);
            L_avsbus_state = AVSBUS_STATE_PROCESS_CURRENT;
            break;

        case AVSBUS_STATE_PROCESS_CURRENT:
            // Process the current readings
            process_avsbus_current();
            // Initiate read of voltages
            initiate_avsbus_reads(AVSBUS_VOLTAGE);
            L_avsbus_state = AVSBUS_STATE_PROCESS_VOLTAGE;
            break;

        case AVSBUS_STATE_PROCESS_VOLTAGE:
            // Process the voltage readings
            process_avsbus_voltage();
            // Initiate read of currents
            initiate_avsbus_reads(AVSBUS_CURRENT);
            L_avsbus_state = AVSBUS_STATE_PROCESS_CURRENT;
            break;

        case AVSBUS_STATE_DISABLED:
            break;

        default:
            TRAC_ERR("amec_update_avsbus_sensors: INVALID AVSBUS STATE 0x%02X", L_avsbus_state);
            L_avsbus_state = AVSBUS_STATE_INITIATE_READ;
            break;
    }

    // Update the chip voltage and power sensors after every reading
    if (G_avsbus_vdd_monitoring)
    {
        update_avsbus_power_sensors(AVSBUS_VDD);
    }
    if (G_avsbus_vdn_monitoring)
    {
        update_avsbus_power_sensors(AVSBUS_VDN);
    }

} // end amec_update_avsbus_sensors()


// Function Specification
//
// Name: amec_update_vrm_sensors
//
// Description: Updates sensors that use data from the VRMs
// (e.g., VR_FAN, FANS_FULL_SPEED, VR_HOT).
//
// Thread: RealTime Loop
//
// End Function Specification
void amec_update_vrm_sensors(void)
{
// TODO: RTC 132561 - VR_FAN support
#if 0
    /*------------------------------------------------------------------------*/
    /*  Local Variables                                                       */
    /*------------------------------------------------------------------------*/
    int                         l_rc = 0;
    int                         l_vrfan = 0;
    int                         l_softoc = 0;
    int                         l_minus_np1_regmode = 0;
    int                         l_minus_n_regmode = 0;
    static uint8_t              L_error_count = 0;
    uint8_t                     l_pin_value = 1; // active low, so set default to high
    uint8_t                     l_vrhot_count = 0;
    errlHndl_t                  l_err = NULL;

    /*------------------------------------------------------------------------*/
    /*  Code                                                                  */
    /*------------------------------------------------------------------------*/

    // VR_FAN and SOFT_OC come from SPIVID
    l_rc = vrm_read_state(SPIVRM_PORT(0),
                          &l_minus_np1_regmode,
                          &l_minus_n_regmode,
                          &l_vrfan,
                          &l_softoc);
    if (l_rc == 0)
    {
        // Update the VR_FAN sensor
        sensor_update( AMECSENSOR_PTR(VRFAN250USPROC), (uint16_t)l_vrfan );

        // Clear our error count
        L_error_count = 0;

        // No longer reading gpio from APSS in GA1 due to instability in
        // APSS composite mode
        //apss_gpio_get(l_pin, &l_pin_value);

        // VR_HOT sensor is a counter of number of times the VRHOT signal
        // has been asserted
        l_vrhot_count = AMECSENSOR_PTR(VRHOT250USPROC)->sample;

        // Check if VR_FAN is asserted AND if 'fans_full_speed' GPIO is ON.
        // Note that this GPIO is active low.
        if (AMECSENSOR_PTR(VRFAN250USPROC)->sample && !(l_pin_value))
        {
            // VR_FAN is asserted and 'fans_full_speed' GPIO is ON,
            // then increment our VR_HOT counter
            if (l_vrhot_count < g_amec->vrhotproc.setpoint)
            {
                l_vrhot_count++;
            }
        }
        else
        {
            // Reset our VR_HOT counter
            l_vrhot_count = 0;
        }
        sensor_update(AMECSENSOR_PTR(VRHOT250USPROC), l_vrhot_count);
    }
    else
    {
        // Increment our error count
        L_error_count++;

        // Don't allow the error count to wrap
        if (L_error_count == 0)
        {
            L_error_count = 0xFF;
        }
    }

    sensor_update( AMECSENSOR_PTR(VRFAN250USMEM), 0 );
    sensor_update( AMECSENSOR_PTR(VRHOT250USMEM), 0 );
#endif
} // end amec_update_vrm_sensors()


/*----------------------------------------------------------------------------*/
/* End                                                                        */
/*----------------------------------------------------------------------------*/
