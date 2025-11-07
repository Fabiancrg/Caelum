import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['caelum'],
    model: 'caelum',
    vendor: 'ESPRESSIF',
    description: 'Caelum - Battery-powered Zigbee weather station with rain gauge',
    extend: [
        m.deviceEndpoints({"endpoints":{"1":1,"2":2,"3":3,"4":4}}), 
        m.temperature(), 
        m.humidity(), 
        m.pressure(), 
        m.battery(), 
        m.numeric(
            {
                "name":"Rainfall Total",
                "cluster":"genAnalogInput",
                "attribute":"presentValue",
                "reporting":{"min":"MIN","max":"MAX","change":1},
                "description":"Total rainfall",
                "unit": "mm",
                "precision": 0,
                "access":"STATE_GET",
                icon: "mdi:weather-rainy",
                "endpointNames":["2"]
            }
        ), 
        m.numeric(
            {
                "name":"Sleep Duration",
                "cluster":"genAnalogInput",
                "attribute":"presentValue",
                "reporting":{"min":"MIN","max":900,"change":0.1},
                "description":"Sleep duration configuration",
                "unit": "s",
                "valueMin": 30,
                "valueMax": 900,
                "access":"ALL", 
                "endpointNames":["3"],
                icon: "mdi:sleep"
            }
        ),
        m.onOff(
            {
                "powerOnBehavior":false,
                "description":"LED debug indicator control",
                "endpointNames":["4"],
                "exposesName": "LED Debug", 
                "icon": "mdi:led-on"
            }
        )
    ],
};
