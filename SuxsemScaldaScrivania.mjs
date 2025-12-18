import {on_off} from "zigbee-herdsman-converters/converters/fromZigbee";
import {on_off as tzOnOff} from "zigbee-herdsman-converters/converters/toZigbee";
import {presets, access} from "zigbee-herdsman-converters/lib/exposes";
import {bind, onOff, brightness} from "zigbee-herdsman-converters/lib/reporting";

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
    await entity.command(
      'genLevelCtrl',
      'moveToLevel',
      {
        level: powerMap[value],
        transtime: 0,
      }
    );
  },
};

const fzPower = {
  cluster: 'genLevelCtrl',
  type: ['attributeReport', 'readResponse'],
  convert: (model, msg) => {
    const level = msg.data.currentLevel;
    const power = Object.keys(powerMap).find(key => powerMap[key] === level);
    return { power };
  },
};

/** @type{import('zigbee-herdsman-converters/lib/types').DefinitionWithExtend | import('zigbee-herdsman-converters/lib/types').DefinitionWithExtend[]} */
export default {
    zigbeeModel: ['ScaldaScrivania'],
    model: 'ScaldaScrivania',
    vendor: 'Suxsem',
    description: 'ScaldaScrivania',
    fromZigbee: [on_off, fzPower],
    toZigbee: [tzOnOff, tzPower],
    exposes: [
        presets.switch(),
        presets.enum('power', access.STATE_SET, ['Bassa', 'Media', 'Alta', 'Massima', 'Nucleare']).withDescription('Potenza'),
    ],
    // The configure method below is needed to make the device reports on/off state changes
    // when the device is controlled manually through the button on it.
    configure: async (device, coordinatorEndpoint, definition) => {
        const endpoint = device.getEndpoint(10);

        if (endpoint) {
            await bind(endpoint, coordinatorEndpoint, ["genOnOff", "genLevelCtrl"]);
            await onOff(endpoint);
            await brightness(endpoint); //uses the same cluster as levelControl
        }
    },
};
