const m = require('zigbee-herdsman-converters/lib/modernExtend');

module.exports = {
    zigbeeModel: ['caelum_pro'],
    model: 'caelum_pro',
    vendor: 'ESPRESSIF',
    description: 'Caelum Pro - Battery-powered Zigbee weather station (SHT4x + LPS22HB + DS18B20 + rain + wind + light)',
    extend: [
        // Firmware endpoint map:
        //   EP1 = environmental (SHT4x temp/humidity, LPS22HB pressure, battery)
        //   EP2 = rain gauge       (genAnalogInput presentValue = total rainfall, mm)
        //   EP3 = DS18B20          (external temperature probe)
        //   EP4 = wind speed       (genAnalogInput presentValue = m/s)
        //   EP5 = wind direction   (genAnalogInput presentValue = degrees)
        //   EP6 = illuminance      (standard Illuminance Measurement cluster 0x0400)
        m.deviceEndpoints({endpoints: {"1": 1, "2": 2, "3": 3, "4": 4, "5": 5, "6": 6}}),

        // EP1 - on-board environmental sensors
        m.temperature({
            endpointNames: ["1"],
            access: "STATE_GET",
            reporting: {min: 10, max: 3600, change: 10},
        }),
        m.humidity({
            endpointNames: ["1"],
            access: "STATE_GET",
            reporting: {min: 10, max: 3600, change: 100},
        }),
        m.pressure({
            endpointNames: ["1"],
            access: "STATE_GET",
            reporting: {min: 10, max: 3600, change: 10},
        }),
        m.battery(),

        // NOTE: the firmware also exposes battery ADC diagnostics on genPowerCfg
        // attributes 0x4000-0x4004 (raw ADC, ADC-pin mV, cal flag, uptime, auto-
        // reboot count). They are intentionally NOT surfaced here to keep the
        // device page clean - read them on demand via the Z2M dev console by
        // numeric attribute ID if the battery ADC ever needs debugging again.

        // EP2 - rain gauge total (mm)
        m.numeric({
            endpointNames: ["2"],
            name: "rain_amount",
            cluster: "genAnalogInput",
            attribute: "presentValue",
            reporting: {min: 0, max: 3600, change: 0.3},
            description: "Total rainfall",
            unit: "mm",
            precision: 1,
            access: "ALL",
            icon: "mdi:weather-rainy",
        }),

        // EP3 - DS18B20 external temperature probe
        m.temperature({
            endpointNames: ["3"],
            access: "STATE_GET",
            reporting: {min: 10, max: 3600, change: 10},
        }),

        // EP4 - wind speed (m/s)
        m.numeric({
            endpointNames: ["4"],
            name: "wind_speed",
            cluster: "genAnalogInput",
            attribute: "presentValue",
            reporting: {min: 0, max: 3600, change: 0.5},
            description: "Wind speed",
            unit: "m/s",
            precision: 1,
            access: "STATE_GET",
            icon: "mdi:weather-windy",
        }),

        // EP5 - wind direction (degrees, 0 = North)
        m.numeric({
            endpointNames: ["5"],
            name: "wind_direction",
            cluster: "genAnalogInput",
            attribute: "presentValue",
            reporting: {min: 0, max: 3600, change: 5},
            description: "Wind direction",
            unit: "°",
            precision: 0,
            access: "STATE_GET",
            icon: "mdi:compass",
        }),

        // EP6 - illuminance (standard cluster)
        m.illuminance({
            endpointNames: ["6"],
            access: "STATE_GET",
            reporting: {min: 10, max: 3600, change: 100},
        }),
    ],
    meta: {multiEndpoint: true},
    ota: true,
};
