import { on_off } from "zigbee-herdsman-converters/converters/fromZigbee";
import { on_off as tzOnOff } from "zigbee-herdsman-converters/converters/toZigbee";
import { presets, access } from "zigbee-herdsman-converters/lib/exposes";
import * as reporting from "zigbee-herdsman-converters/lib/reporting";
import { repInterval } from "zigbee-herdsman-converters/lib/constants";
import * as me from "zigbee-herdsman-converters/lib/modernExtend";
import * as utils from "zigbee-herdsman-converters/lib/utils";
import { zigbeeOTA } from "zigbee-herdsman-converters/lib/ota";

const powerMap = {
  Bassa: 0,
  Media: 1,
  Alta: 2,
  Massima: 3,
  Nucleare: 4,
};

const tzPower = {
  key: ['power'],
  convertSet: async (entity, key, value, meta) => {
    await entity.write('manuSpecificSuxsem', { power: powerMap[value] }, utils.getOptions(meta.mapped, entity));
    return {
      state: {
        power: powerMap[value]
      }
    };
  },
};

const tzLevelConfig = {
  key: ['level_config_1', 'level_config_2', 'level_config_3', 'level_config_4', 'level_config_5'],
  convertSet: async (entity, key, value, meta) => {
    await entity.write('manuSpecificSuxsem', { [key]: value }, utils.getOptions(meta.mapped, entity));
    return {
      state: {
        [key]: value
      }
    };
  },
};

const fzSuxsem = {
  cluster: 'manuSpecificSuxsem',
  type: ['attributeReport', 'readResponse'],
  convert: (model, msg) => {
    const payload = {};
    if (msg.data.hasOwnProperty('power')) {
      const property = 'power';
      const state = Object.keys(powerMap).find(key => powerMap[key] === msg.data.power);
      payload[property] = state;
    }
    ["1", "2", "3", "4", "5"].forEach(i => {
      const levelConfigKey = `level_config_${i}`;
      if (msg.data.hasOwnProperty(levelConfigKey)) {
        const property = levelConfigKey;
        const state = msg.data[levelConfigKey];
        payload[property] = state;
      }
    });
    return payload;
  },
};

/** @type{import('zigbee-herdsman-converters/lib/types').DefinitionWithExtend | import('zigbee-herdsman-converters/lib/types').DefinitionWithExtend[]} */
export default {
  zigbeeModel: ['ScaldaScrivania'],
  model: 'ScaldaScrivania',
  vendor: 'Suxsem',
  description: 'ScaldaScrivania',
  ota: zigbeeOTA,
  fromZigbee: [on_off, fzSuxsem],
  toZigbee: [tzOnOff, tzPower, tzLevelConfig],
  exposes: [
    presets.switch(),
    presets.enum('power', access.STATE_SET, ['Bassa', 'Media', 'Alta', 'Massima', 'Nucleare']).withDescription('Potenza'),
    ...["1", "2", "3", "4", "5"].map(i =>
      presets.numeric(`level_config_${i}`, access.STATE_SET)
        .withDescription(`Configurazione livello ${i}, espresso in decimi di secondo da 0 a 100 (max 10s)`)
        .withValueMin(0)
        .withValueMax(100)
    ),
  ],
  extend: [
    me.deviceAddCustomCluster("manuSpecificSuxsem", {
      ID: 0xfeb2,
      //    manufacturerCode: 0xFFF5,
      attributes: {
        power: { ID: 0x0000, type: 32 }, // Uint8
        level_config_1: { ID: 0x0001, type: 32 }, // Uint8
        level_config_2: { ID: 0x0002, type: 32 }, // Uint8
        level_config_3: { ID: 0x0003, type: 32 }, // Uint8
        level_config_4: { ID: 0x0004, type: 32 }, // Uint8
        level_config_5: { ID: 0x0005, type: 32 }, // Uint8
      },
    }),
  ],
  // The configure method below is needed to make the device reports on/off state changes
  // when the device is controlled manually through the button on it.
  configure: async (device, coordinatorEndpoint, definition) => {
    const endpoint = device.getEndpoint(10);

    if (endpoint) {
      await reporting.bind(endpoint, coordinatorEndpoint, ["genOnOff", "manuSpecificSuxsem"]);
      await reporting.onOff(endpoint);
      const pPower = reporting.payload("power", 1, repInterval.HOUR, 1);
      await endpoint.configureReporting("manuSpecificSuxsem", pPower);
      for (let i = 1; i <= 5; i += 1) {
        const pLevelConfig = reporting.payload(`level_config_${i}`, 1, repInterval.HOUR, 1);
        await endpoint.configureReporting("manuSpecificSuxsem", pLevelConfig);
      }
    }
  },
};
