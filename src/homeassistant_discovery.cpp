#include "homeassistant_discovery.h"
#include "vendor/json.hpp"
#include "vendor/flashmq_plugin.h"
#include "utils.h"
#include <algorithm>
#include <sstream>
#include <regex>

using namespace dbus_flashmq;

HADevice::HADevice(const std::string &name, const std::string &model, const std::string &identifier)
    : name(name), model(model), identifiers(identifier)
{
}

std::string HADevice::toJson() const
{
    nlohmann::json j;
    j["name"] = name;
    j["manufacturer"] = manufacturer;
    j["model"] = model;
    j["identifiers"] = nlohmann::json::array({identifiers});

    if (!sw_version.empty()) j["sw_version"] = sw_version;
    if (!configuration_url.empty()) j["configuration_url"] = configuration_url;
	if (!via_device.empty()) {
        j["via_device"] = via_device;
    }

    return j.dump();
}

std::vector<HAServiceRegistry::DiagnosticSensorDef> HAServiceRegistry::getCommonDiagnostics() const
{
    return {
        // Device Instance - Universal identifier
        {
            .dbus_path = "/DeviceInstance",
            .icon = "mdi:numeric",
            .friendly_name_suffix = "Device Instance"
        },

        // Error Code - Common error reporting
        {
            .dbus_path = "/ErrorCode",
            .icon = "mdi:alert-circle",
            .friendly_name_suffix = "Error Code"
        },

        // Status - Device operational status
        {
            .dbus_path = "/Status",
            .icon = "mdi:information",
            .friendly_name_suffix = "Status"
        },

        // State - Device state (different from Status)
        {
            .dbus_path = "/State",
            .icon = "mdi:power-settings",
            .friendly_name_suffix = "Device State"
        },

        // Firmware version
        {
            .dbus_path = "/FirmwareVersion",
            .icon = "mdi:chip",
            .friendly_name_suffix = "Firmware Version"
        },

        // Hardware version
        {
            .dbus_path = "/HardwareVersion",
            .icon = "mdi:memory",
            .friendly_name_suffix = "Hardware Version"
        },

        // Serial number
        {
            .dbus_path = "/Serial",
            .icon = "mdi:barcode",
            .friendly_name_suffix = "Serial Number"
        }
    };
}

void HAServiceRegistry::addDiagnosticSensor(HAServiceDefinition& service_def, const DiagnosticSensorDef& diag_def) const
{
    HASensorConfig sensor;
    sensor.icon = diag_def.icon;
    sensor.entity_category = "diagnostic";
    sensor.friendly_name_suffix = diag_def.friendly_name_suffix;

    // Set optional properties if specified
    if (!diag_def.device_class.empty()) {
        sensor.device_class = diag_def.device_class;
    }

    if (!diag_def.unit_of_measurement.empty()) {
        sensor.unit_of_measurement = diag_def.unit_of_measurement;
        sensor.state_class = "measurement"; // Assume measurement if has unit
    }

    if (!diag_def.value_template.empty()) {
        sensor.value_template = diag_def.value_template;
    }

    if (diag_def.suggested_display_precision >= 0) {
        sensor.suggested_display_precision = diag_def.suggested_display_precision;
    }

    service_def.sensors[diag_def.dbus_path] = sensor;
}

void HAServiceRegistry::addCommonDiagnosticSensors(HAServiceDefinition& service_def) const
{
    // Add all common diagnostic sensors
    for (const auto& diag_def : getCommonDiagnostics()) {
        addDiagnosticSensor(service_def, diag_def);
    }
}

HAEntityConfig::HAEntityConfig(const std::string &name, const std::string &unique_id, const std::string &state_topic)
    : name(name), unique_id(unique_id), state_topic(state_topic)
{
}

std::string HAEntityConfig::toJson(const HADevice &device) const
{
	    nlohmann::json config_json;

    config_json["name"] = name;
    config_json["unique_id"] = unique_id;
    config_json["state_topic"] = state_topic;
    config_json["device"] = nlohmann::json::parse(device.toJson());

    if (!value_template.empty()) {
        config_json["value_template"] = value_template;
    }

    if (!unit_of_measurement.empty()) {
        config_json["unit_of_measurement"] = unit_of_measurement;
    }

    if (!device_class.empty()) {
        config_json["device_class"] = device_class;
    }

    if (!state_class.empty() && state_class != "None") {
        config_json["state_class"] = state_class;
    }

    if (!icon.empty()) {
        config_json["icon"] = icon;
    }

    if (!entity_category.empty()) {
        config_json["entity_category"] = entity_category;
    }

    config_json["enabled_by_default"] = enabled_by_default;

    if (suggested_display_precision >= 0) {
        config_json["suggested_display_precision"] = suggested_display_precision;
    }

    // NEW: Add command support for controllable entities
    if (!command_topic.empty()) {
        config_json["command_topic"] = command_topic;

        // For switch entities
        if (!payload_on.empty()) {
            config_json["payload_on"] = payload_on;
        }
        if (!payload_off.empty()) {
            config_json["payload_off"] = payload_off;
        }

        // For number entities (dimmers)
        if (min_value != 0 || max_value != 100) {
            config_json["min"] = min_value;
            config_json["max"] = max_value;
        }
        if (!mode.empty()) {
            config_json["mode"] = mode;
        }

        config_json["optimistic"] = optimistic;
    }

    return config_json.dump();

}

HAServiceRegistry::HAServiceRegistry()
{
    registerTemperatureService();
    registerBatteryService();
    registerSolarChargerService();
    registerVeBusService();
    registerSystemService();
    registerTankService();
    registerGridMeterService();
	registerSwitchService();
	registerMeteoService();
}

void HAServiceRegistry::registerMeteoService()
{
    HAServiceDefinition meteo_def;
    meteo_def.friendly_name = "Weather Station";
    meteo_def.model_name = "Meteorological Sensor";

    // Cell Temperature (main measurement)
    HASensorConfig cell_temp_sensor;
    cell_temp_sensor.device_class = "temperature";
    cell_temp_sensor.state_class = "measurement";
    cell_temp_sensor.unit_of_measurement = "°C";
    cell_temp_sensor.icon = "mdi:thermometer";
    cell_temp_sensor.suggested_display_precision = 1;
    cell_temp_sensor.friendly_name_suffix = "Cell Temperature";
    meteo_def.sensors["/CellTemperature"] = cell_temp_sensor;

    // Irradiance (solar irradiance measurement)
    HASensorConfig irradiance_sensor;
    irradiance_sensor.device_class = "irradiance";
    irradiance_sensor.state_class = "measurement";
    irradiance_sensor.unit_of_measurement = "W/m²";
    irradiance_sensor.icon = "mdi:solar-power";
    irradiance_sensor.suggested_display_precision = 1;
    irradiance_sensor.friendly_name_suffix = "Solar Irradiance";
    meteo_def.sensors["/Irradiance"] = irradiance_sensor;

    addDiagnosticSensor(meteo_def, {
        .dbus_path = "/BatteryVoltage",
        .icon = "mdi:battery",
        .friendly_name_suffix = "Battery Voltage",
        .device_class = "voltage",
        .unit_of_measurement = "V",
        .suggested_display_precision = 3
    });

    addCommonDiagnosticSensors(meteo_def);

    // Today's Yield (energy generation for today)
    HASensorConfig todays_yield_sensor;
    todays_yield_sensor.device_class = "energy";
    todays_yield_sensor.state_class = "total_increasing";
    todays_yield_sensor.unit_of_measurement = "kWh";
    todays_yield_sensor.icon = "mdi:solar-panel";
    todays_yield_sensor.suggested_display_precision = 2;
    todays_yield_sensor.friendly_name_suffix = "Today's Yield";
    meteo_def.sensors["/TodaysYield"] = todays_yield_sensor;

    // Time Since Last Sun (time tracking)
    HASensorConfig time_since_sun_sensor;
    time_since_sun_sensor.device_class = "duration";
    time_since_sun_sensor.state_class = "measurement";
    time_since_sun_sensor.unit_of_measurement = "s";
    time_since_sun_sensor.icon = "mdi:clock";
    time_since_sun_sensor.entity_category = "diagnostic";
    time_since_sun_sensor.friendly_name_suffix = "Time Since Last Sun";
    meteo_def.sensors["/TimeSinceLastSun"] = time_since_sun_sensor;

    // Device Instance (identification)
    HASensorConfig device_instance_sensor;
    device_instance_sensor.icon = "mdi:numeric";
    device_instance_sensor.entity_category = "diagnostic";
    device_instance_sensor.friendly_name_suffix = "Device Instance";
    meteo_def.sensors["/DeviceInstance"] = device_instance_sensor;

    // Custom device name extraction for meteo sensors
    meteo_def.get_device_name = [](const std::unordered_map<std::string, Item>& items) -> std::string {
        auto custom_name = items.find("/CustomName");
        if (custom_name != items.end()) {
            std::string name = custom_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        auto device_name = items.find("/DeviceName");
        if (device_name != items.end()) {
            std::string name = device_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        auto product_name = items.find("/ProductName");
        if (product_name != items.end()) {
            std::string name = product_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        // Determine sensor type based on available measurements
        bool has_irradiance = items.find("/Irradiance") != items.end();
        bool has_temp = items.find("/CellTemperature") != items.end();
        bool has_yield = items.find("/TodaysYield") != items.end();

        if (has_irradiance && has_temp && has_yield) {
            return "Solar Weather Station";
        } else if (has_irradiance && has_temp) {
            return "Solar Irradiance Sensor";
        } else if (has_irradiance) {
            return "Irradiance Sensor";
        } else if (has_temp) {
            return "Meteorological Sensor";
        } else {
            return "Weather Station";
        }
    };

    service_definitions["meteo"] = std::move(meteo_def);
}

void HAServiceRegistry::registerTemperatureService()
{
    HAServiceDefinition temp_def;
    temp_def.friendly_name = "Environmental Sensor";
    temp_def.model_name = "Environmental Sensor";

    // Define the temperature sensor
    HASensorConfig temp_sensor;
    temp_sensor.device_class = "temperature";
    temp_sensor.state_class = "measurement";
    temp_sensor.unit_of_measurement = "°C";
    temp_sensor.icon = "mdi:thermometer";
    temp_sensor.suggested_display_precision = 1;
    temp_sensor.friendly_name_suffix = "Temperature";
    temp_def.sensors["/Temperature"] = temp_sensor;

    // Define the humidity sensor
    HASensorConfig humidity_sensor;
    humidity_sensor.device_class = "humidity";
    humidity_sensor.state_class = "measurement";
    humidity_sensor.unit_of_measurement = "%";
    humidity_sensor.icon = "mdi:water-percent";
    humidity_sensor.suggested_display_precision = 1;
    humidity_sensor.friendly_name_suffix = "Humidity";
    temp_def.sensors["/Humidity"] = humidity_sensor;

    // Define the pressure sensor
    HASensorConfig pressure_sensor;
    pressure_sensor.device_class = "atmospheric_pressure";
    pressure_sensor.state_class = "measurement";
    pressure_sensor.unit_of_measurement = "hPa";
    pressure_sensor.icon = "mdi:gauge";
    pressure_sensor.suggested_display_precision = 1;
    pressure_sensor.friendly_name_suffix = "Pressure";
    temp_def.sensors["/Pressure"] = pressure_sensor;

    addDiagnosticSensor(temp_def, {
        .dbus_path = "/BatteryVoltage",
        .icon = "mdi:battery",
        .friendly_name_suffix = "Battery Voltage",
        .device_class = "voltage",
        .unit_of_measurement = "V",
        .suggested_display_precision = 3
    });

    addCommonDiagnosticSensors(temp_def);

    temp_def.get_device_name = [](const std::unordered_map<std::string, Item>& items) -> std::string {
        auto custom_name = items.find("/CustomName");
        if (custom_name != items.end()) {
            std::string name = custom_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        auto product_name = items.find("/ProductName");
        if (product_name != items.end()) {
            std::string name = product_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        // Determine sensor type based on available measurements
        bool has_temp = items.find("/Temperature") != items.end();
        bool has_humidity = items.find("/Humidity") != items.end();
        bool has_pressure = items.find("/Pressure") != items.end();

        if (has_temp && has_humidity && has_pressure) {
            return "Environmental Sensor";
        } else if (has_temp && has_humidity) {
            return "Temperature & Humidity Sensor";
        } else if (has_temp && has_pressure) {
            return "Temperature & Pressure Sensor";
        } else if (has_humidity && has_pressure) {
            return "Humidity & Pressure Sensor";
        } else if (has_humidity) {
            return "Humidity Sensor";
        } else if (has_pressure) {
            return "Pressure Sensor";
        } else {
            return "Temperature Sensor";
        }
    };

    service_definitions["temperature"] = std::move(temp_def);
}

void HAServiceRegistry::registerBatteryService()
{
    HAServiceDefinition battery_def;
    battery_def.friendly_name = "Battery Monitor";
    battery_def.model_name = "Battery Monitor";

    // State of Charge
    HASensorConfig soc_sensor;
    soc_sensor.device_class = "battery";
    soc_sensor.state_class = "measurement";
    soc_sensor.unit_of_measurement = "%";
    soc_sensor.icon = "mdi:battery";
    soc_sensor.suggested_display_precision = 1;
    soc_sensor.friendly_name_suffix = "State of Charge";
    battery_def.sensors["/Soc"] = soc_sensor;

    // Main battery voltage
    HASensorConfig voltage_sensor;
    voltage_sensor.device_class = "voltage";
    voltage_sensor.state_class = "measurement";
    voltage_sensor.unit_of_measurement = "V";
    voltage_sensor.icon = "mdi:flash";
    voltage_sensor.suggested_display_precision = 2;
    voltage_sensor.friendly_name_suffix = "Voltage";
    battery_def.sensors["/Dc/0/Voltage"] = voltage_sensor;

    // Battery current
    HASensorConfig current_sensor;
    current_sensor.device_class = "current";
    current_sensor.state_class = "measurement";
    current_sensor.unit_of_measurement = "A";
    current_sensor.icon = "mdi:current-dc";
    current_sensor.suggested_display_precision = 2;
    current_sensor.friendly_name_suffix = "Current";
    battery_def.sensors["/Dc/0/Current"] = current_sensor;

    // Total energy consumed from battery (discharge)
    HASensorConfig energy_consumed;
    energy_consumed.device_class = "energy";
    energy_consumed.state_class = "total_increasing";
    energy_consumed.unit_of_measurement = "kWh";
    energy_consumed.icon = "mdi:battery-minus";
    energy_consumed.suggested_display_precision = 2;
    energy_consumed.friendly_name_suffix = "Energy Consumed";
    battery_def.sensors["/History/DischargedEnergy"] = energy_consumed;

    // Total energy charged to battery
    HASensorConfig energy_charged;
    energy_charged.device_class = "energy";
    energy_charged.state_class = "total_increasing";
    energy_charged.unit_of_measurement = "kWh";
    energy_charged.icon = "mdi:battery-plus";
    energy_charged.suggested_display_precision = 2;
    energy_charged.friendly_name_suffix = "Energy Charged";
    battery_def.sensors["/History/ChargedEnergy"] = energy_charged;

    // Battery power
    HASensorConfig power_sensor;
    power_sensor.device_class = "power";
    power_sensor.state_class = "measurement";
    power_sensor.unit_of_measurement = "W";
    power_sensor.icon = "mdi:flash";
    power_sensor.suggested_display_precision = 1;
    power_sensor.friendly_name_suffix = "Power";
    battery_def.sensors["/Dc/0/Power"] = power_sensor;

    // Battery temperature
    HASensorConfig temp_sensor;
    temp_sensor.device_class = "temperature";
    temp_sensor.state_class = "measurement";
    temp_sensor.unit_of_measurement = "°C";
    temp_sensor.icon = "mdi:thermometer";
    temp_sensor.suggested_display_precision = 1;
    temp_sensor.friendly_name_suffix = "Temperature";
    battery_def.sensors["/Dc/0/Temperature"] = temp_sensor;

    // Mid voltage (BMV-702)
    HASensorConfig mid_voltage_sensor;
    mid_voltage_sensor.device_class = "voltage";
    mid_voltage_sensor.state_class = "measurement";
    mid_voltage_sensor.unit_of_measurement = "V";
    mid_voltage_sensor.icon = "mdi:flash";
    mid_voltage_sensor.suggested_display_precision = 2;
    mid_voltage_sensor.friendly_name_suffix = "Mid Voltage";
    battery_def.sensors["/Dc/0/MidVoltage"] = mid_voltage_sensor;

    // Mid voltage deviation
    HASensorConfig mid_deviation_sensor;
    mid_deviation_sensor.state_class = "measurement";
    mid_deviation_sensor.unit_of_measurement = "%";
    mid_deviation_sensor.icon = "mdi:battery-alert";
    mid_deviation_sensor.suggested_display_precision = 1;
    mid_deviation_sensor.friendly_name_suffix = "Mid Voltage Deviation";
    battery_def.sensors["/Dc/0/MidVoltageDeviation"] = mid_deviation_sensor;

    // Starter voltage (BMV-702)
    HASensorConfig starter_voltage_sensor;
    starter_voltage_sensor.device_class = "voltage";
    starter_voltage_sensor.state_class = "measurement";
    starter_voltage_sensor.unit_of_measurement = "V";
    starter_voltage_sensor.icon = "mdi:car-battery";
    starter_voltage_sensor.suggested_display_precision = 2;
    starter_voltage_sensor.friendly_name_suffix = "Starter Voltage";
    battery_def.sensors["/Dc/1/Voltage"] = starter_voltage_sensor;

    // Consumed amp hours
    HASensorConfig consumed_ah_sensor;
    consumed_ah_sensor.state_class = "total_increasing";
    consumed_ah_sensor.unit_of_measurement = "Ah";
    consumed_ah_sensor.icon = "mdi:battery-minus";
    consumed_ah_sensor.suggested_display_precision = 2;
    consumed_ah_sensor.friendly_name_suffix = "Consumed Amp Hours";
    battery_def.sensors["/ConsumedAmphours"] = consumed_ah_sensor;

    // Time to go
    HASensorConfig ttg_sensor;
    ttg_sensor.device_class = "duration";
    ttg_sensor.state_class = "measurement";
    ttg_sensor.unit_of_measurement = "s";
    ttg_sensor.icon = "mdi:timer";
    ttg_sensor.suggested_display_precision = 0;
    ttg_sensor.friendly_name_suffix = "Time to Go";
    battery_def.sensors["/TimeToGo"] = ttg_sensor;

    addCommonDiagnosticSensors(battery_def);

    // Custom device name for batteries
    battery_def.get_device_name = [](const std::unordered_map<std::string, Item>& items) -> std::string {
        auto custom_name = items.find("/CustomName");
        if (custom_name != items.end()) {
            std::string name = custom_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        auto product_name = items.find("/ProductName");
        if (product_name != items.end()) {
            std::string name = product_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        return "Battery Monitor";
    };

    service_definitions["battery"] = std::move(battery_def);
}

void HAServiceRegistry::registerSolarChargerService()
{
    HAServiceDefinition solar_def;
    solar_def.friendly_name = "Solar Charger";
    solar_def.model_name = "MPPT Solar Charger";

    // PV Voltage
    HASensorConfig pv_voltage;
    pv_voltage.device_class = "voltage";
    pv_voltage.state_class = "measurement";
    pv_voltage.unit_of_measurement = "V";
    pv_voltage.icon = "mdi:solar-panel";
    pv_voltage.suggested_display_precision = 2;
    pv_voltage.friendly_name_suffix = "PV Voltage";
    solar_def.sensors["/Pv/V"] = pv_voltage;

    // PV Current
    HASensorConfig pv_current;
    pv_current.device_class = "current";
    pv_current.state_class = "measurement";
    pv_current.unit_of_measurement = "A";
    pv_current.icon = "mdi:solar-panel";
    pv_current.suggested_display_precision = 2;
    pv_current.friendly_name_suffix = "PV Current";
    solar_def.sensors["/Pv/I"] = pv_current;

    // PV Power
    HASensorConfig pv_power;
    pv_power.device_class = "power";
    pv_power.state_class = "measurement";
    pv_power.unit_of_measurement = "W";
    pv_power.icon = "mdi:solar-panel";
    pv_power.suggested_display_precision = 1;
    pv_power.friendly_name_suffix = "PV Power";
    solar_def.sensors["/Yield/Power"] = pv_power;

    // Battery voltage (output)
    HASensorConfig bat_voltage;
    bat_voltage.device_class = "voltage";
    bat_voltage.state_class = "measurement";
    bat_voltage.unit_of_measurement = "V";
    bat_voltage.icon = "mdi:battery-charging";
    bat_voltage.suggested_display_precision = 2;
    bat_voltage.friendly_name_suffix = "Battery Voltage";
    solar_def.sensors["/Dc/0/Voltage"] = bat_voltage;

    // Battery current (output)
    HASensorConfig bat_current;
    bat_current.device_class = "current";
    bat_current.state_class = "measurement";
    bat_current.unit_of_measurement = "A";
    bat_current.icon = "mdi:battery-charging";
    bat_current.suggested_display_precision = 2;
    bat_current.friendly_name_suffix = "Battery Current";
    solar_def.sensors["/Dc/0/Current"] = bat_current;

    // Charge state
    HASensorConfig state_sensor;
    state_sensor.state_class = "";  // This is an enum, not a measurement
    state_sensor.icon = "mdi:battery-charging";
    state_sensor.friendly_name_suffix = "State";
    // Custom value template for state enum
    state_sensor.value_template = "{% set states = {0: 'Off', 2: 'Fault', 3: 'Bulk', 4: 'Absorption', 5: 'Float'} %}{{ states[value_json.value] | default('Unknown') }}";
    solar_def.sensors["/State"] = state_sensor;

    // Daily yield
    HASensorConfig daily_yield;
    daily_yield.device_class = "energy";
    daily_yield.state_class = "total_increasing";
    daily_yield.unit_of_measurement = "kWh";
    daily_yield.icon = "mdi:solar-panel";
    daily_yield.suggested_display_precision = 2;
    daily_yield.friendly_name_suffix = "Daily Yield";
    solar_def.sensors["/History/Daily/0/Yield"] = daily_yield;

    addDiagnosticSensor(solar_def, {
        .dbus_path = "/MppOperationMode",
        .icon = "mdi:solar-panel",
        .friendly_name_suffix = "MPP Operation Mode",
        .value_template = "{% set modes = {0: 'Off', 1: 'Voltage/current limited', 2: 'MPPT active', 255: 'Not available'} %}{{ modes[value_json.value] | default('Unknown') }}"
    });

    addDiagnosticSensor(solar_def, {
        .dbus_path = "/Load/State",
        .icon = "mdi:power-plug",
        .friendly_name_suffix = "Load Output",
        .value_template = "{% if value_json.value == 1 %}On{% else %}Off{% endif %}"
    });

	addCommonDiagnosticSensors(solar_def);

    service_definitions["solarcharger"] = std::move(solar_def);
}

void HAServiceRegistry::registerVeBusService()
{
    HAServiceDefinition vebus_def;
    vebus_def.friendly_name = "MultiPlus";
    vebus_def.model_name = "MultiPlus/Quattro";

    // AC Input Power (per phase)
    for (int phase = 1; phase <= 3; ++phase) {
        std::string path = "/Ac/ActiveIn/L" + std::to_string(phase) + "/P";
        HASensorConfig ac_in_power;
        ac_in_power.device_class = "power";
        ac_in_power.state_class = "measurement";
        ac_in_power.unit_of_measurement = "W";
        ac_in_power.icon = "mdi:transmission-tower";
        ac_in_power.suggested_display_precision = 1;
        ac_in_power.friendly_name_suffix = "AC In L" + std::to_string(phase) + " Power";
        vebus_def.sensors[path] = ac_in_power;

        // AC Output Power
        path = "/Ac/Out/L" + std::to_string(phase) + "/P";
        HASensorConfig ac_out_power;
        ac_out_power.device_class = "power";
        ac_out_power.state_class = "measurement";
        ac_out_power.unit_of_measurement = "W";
        ac_out_power.icon = "mdi:home-lightning-bolt";
        ac_out_power.suggested_display_precision = 1;
        ac_out_power.friendly_name_suffix = "AC Out L" + std::to_string(phase) + " Power";
        vebus_def.sensors[path] = ac_out_power;

        // AC Input Voltage
        path = "/Ac/ActiveIn/L" + std::to_string(phase) + "/V";
        HASensorConfig ac_in_voltage;
        ac_in_voltage.device_class = "voltage";
        ac_in_voltage.state_class = "measurement";
        ac_in_voltage.unit_of_measurement = "V";
        ac_in_voltage.icon = "mdi:transmission-tower";
        ac_in_voltage.suggested_display_precision = 1;
        ac_in_voltage.friendly_name_suffix = "AC In L" + std::to_string(phase) + " Voltage";
        vebus_def.sensors[path] = ac_in_voltage;

        // AC Output Voltage
        path = "/Ac/Out/L" + std::to_string(phase) + "/V";
        HASensorConfig ac_out_voltage;
        ac_out_voltage.device_class = "voltage";
        ac_out_voltage.state_class = "measurement";
        ac_out_voltage.unit_of_measurement = "V";
        ac_out_voltage.icon = "mdi:home-lightning-bolt";
        ac_out_voltage.suggested_display_precision = 1;
        ac_out_voltage.friendly_name_suffix = "AC Out L" + std::to_string(phase) + " Voltage";
        vebus_def.sensors[path] = ac_out_voltage;
    }

    // DC Power
    HASensorConfig dc_power;
    dc_power.device_class = "power";
    dc_power.state_class = "measurement";
    dc_power.unit_of_measurement = "W";
    dc_power.icon = "mdi:battery-charging";
    dc_power.suggested_display_precision = 1;
    dc_power.friendly_name_suffix = "DC Power";
    vebus_def.sensors["/Dc/0/Power"] = dc_power;

    // DC Voltage
    HASensorConfig dc_voltage;
    dc_voltage.device_class = "voltage";
    dc_voltage.state_class = "measurement";
    dc_voltage.unit_of_measurement = "V";
    dc_voltage.icon = "mdi:battery-charging";
    dc_voltage.suggested_display_precision = 2;
    dc_voltage.friendly_name_suffix = "DC Voltage";
    vebus_def.sensors["/Dc/0/Voltage"] = dc_voltage;

    // State
    HASensorConfig state_sensor;
    state_sensor.icon = "mdi:power-settings";
    state_sensor.friendly_name_suffix = "State";
    state_sensor.value_template = "{% set states = {0: 'Off', 1: 'Low Power', 2: 'Fault', 3: 'Bulk', 4: 'Absorption', 5: 'Float', 6: 'Storage', 7: 'Equalize', 8: 'Passthru', 9: 'Inverting', 10: 'Power assist', 11: 'Power supply', 252: 'Bulk protect'} %}{{ states[value_json.value] | default('Unknown') }}";
    vebus_def.sensors["/State"] = state_sensor;

	addDiagnosticSensor(vebus_def, {
        .dbus_path = "/Mode",
        .icon = "mdi:cog",
        .friendly_name_suffix = "Mode",
        .value_template = "{% set modes = {1: 'Charger Only', 2: 'Inverter Only', 3: 'On', 4: 'Off'} %}{{ modes[value_json.value] | default('Unknown') }}"
    });

    addDiagnosticSensor(vebus_def, {
        .dbus_path = "/VebusError",
        .icon = "mdi:alert-circle",
        .friendly_name_suffix = "VE.Bus Error"
    });

    addCommonDiagnosticSensors(vebus_def);

    service_definitions["vebus"] = std::move(vebus_def);
}

void HAServiceRegistry::registerSystemService()
{
    HAServiceDefinition system_def;
    system_def.friendly_name = "GX System";
    system_def.model_name = "Venus GX";

    // AC Loads per phase
    for (int phase = 1; phase <= 3; ++phase) {
        std::string path = "/Ac/Consumption/L" + std::to_string(phase) + "/Power";
        HASensorConfig ac_loads;
        ac_loads.device_class = "power";
        ac_loads.state_class = "measurement";
        ac_loads.unit_of_measurement = "W";
        ac_loads.icon = "mdi:home-lightning-bolt";
        ac_loads.suggested_display_precision = 1;
        ac_loads.friendly_name_suffix = "AC Load L" + std::to_string(phase);
        system_def.sensors[path] = ac_loads;
    }

    // Total AC consumption
    HASensorConfig total_consumption;
    total_consumption.device_class = "power";
    total_consumption.state_class = "measurement";
    total_consumption.unit_of_measurement = "W";
    total_consumption.icon = "mdi:home-lightning-bolt";
    total_consumption.suggested_display_precision = 1;
    total_consumption.friendly_name_suffix = "Total AC Consumption";
    system_def.sensors["/Ac/Consumption/Total/Power"] = total_consumption;

    // Battery Power (system level)
    HASensorConfig bat_power;
    bat_power.device_class = "power";
    bat_power.state_class = "measurement";
    bat_power.unit_of_measurement = "W";
    bat_power.icon = "mdi:battery";
    bat_power.suggested_display_precision = 1;
    bat_power.friendly_name_suffix = "Battery Power";
    system_def.sensors["/Dc/Battery/Power"] = bat_power;

    // PV Power (system level)
    HASensorConfig pv_power;
    pv_power.device_class = "power";
    pv_power.state_class = "measurement";
    pv_power.unit_of_measurement = "W";
    pv_power.icon = "mdi:solar-panel";
    pv_power.suggested_display_precision = 1;
    pv_power.friendly_name_suffix = "PV Power";
    system_def.sensors["/Dc/Pv/Power"] = pv_power;

	addDiagnosticSensor(system_def, {
        .dbus_path = "/SystemState/State",
        .icon = "mdi:state-machine",
        .friendly_name_suffix = "System State"
    });

    addCommonDiagnosticSensors(system_def);

    service_definitions["system"] = std::move(system_def);
}

void HAServiceRegistry::registerTankService()
{
    HAServiceDefinition tank_def;
    tank_def.friendly_name = "Tank Sensor";
    tank_def.model_name = "Tank Level Sensor";

    // Tank Level
    HASensorConfig level_sensor;
    level_sensor.state_class = "measurement";
    level_sensor.unit_of_measurement = "%";
    level_sensor.icon = "mdi:gauge";
    level_sensor.suggested_display_precision = 1;
    level_sensor.friendly_name_suffix = "Level";
    tank_def.sensors["/Level"] = level_sensor;

    // Tank Capacity
    HASensorConfig capacity_sensor;
    capacity_sensor.state_class = "measurement";
    capacity_sensor.unit_of_measurement = "L";
    capacity_sensor.icon = "mdi:storage-tank";
    capacity_sensor.suggested_display_precision = 0;
    capacity_sensor.entity_category = "diagnostic";
    capacity_sensor.friendly_name_suffix = "Capacity";
    tank_def.sensors["/Capacity"] = capacity_sensor;

    // Remaining volume
    HASensorConfig remaining_sensor;
    remaining_sensor.state_class = "measurement";
    remaining_sensor.unit_of_measurement = "L";
    remaining_sensor.icon = "mdi:gauge";
    remaining_sensor.suggested_display_precision = 1;
    remaining_sensor.friendly_name_suffix = "Remaining";
    tank_def.sensors["/Remaining"] = remaining_sensor;

    // Custom device name for tanks (often have meaningful names)
    tank_def.get_device_name = [](const std::unordered_map<std::string, Item>& items) -> std::string {
        auto custom_name = items.find("/CustomName");
        if (custom_name != items.end()) {
            std::string name = custom_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        auto product_name = items.find("/ProductName");
        if (product_name != items.end()) {
            std::string name = product_name->second.get_value().value.as_text();
            if (!name.empty()) return name;
        }

        // Try to determine tank type from FluidType
        auto fluid_type = items.find("/FluidType");
        if (fluid_type != items.end()) {
            int type = fluid_type->second.get_value().value.as_int();
            switch (type) {
                case 0: return "Fuel Tank";
                case 1: return "Fresh Water Tank";
                case 2: return "Waste Water Tank";
                case 3: return "Live Well";
                case 4: return "Oil Tank";
                case 5: return "Black Water Tank";
                case 6: return "Gasoline Tank";
                case 7: return "Diesel Tank";
                case 8: return "LPG Tank";
                case 9: return "LNG Tank";
                case 10: return "Hydraulic Oil Tank";
                case 11: return "Raw Water Tank";
                default: return "Tank Sensor";
            }
        }

        return "Tank Sensor";
    };

	addDiagnosticSensor(tank_def, {
        .dbus_path = "/FluidType",
        .icon = "mdi:waves",
        .friendly_name_suffix = "Fluid Type",
        .value_template = "{% set types = {0: 'Fuel', 1: 'Fresh water', 2: 'Waste water', 3: 'Live well', 4: 'Oil', 5: 'Black water'} %}{{ types[value_json.value] | default('Unknown') }}"
    });

    addCommonDiagnosticSensors(tank_def);

    service_definitions["tank"] = std::move(tank_def);
}

void HAServiceRegistry::registerGridMeterService()
{
    HAServiceDefinition grid_def;
    grid_def.friendly_name = "Grid Meter";
    grid_def.model_name = "Energy Meter";

    // AC Power per phase
    for (int phase = 1; phase <= 3; ++phase) {
        std::string power_path = "/Ac/L" + std::to_string(phase) + "/Power";
        HASensorConfig power_sensor;
        power_sensor.device_class = "power";
        power_sensor.state_class = "measurement";
        power_sensor.unit_of_measurement = "W";
        power_sensor.icon = "mdi:transmission-tower";
        power_sensor.suggested_display_precision = 1;
        power_sensor.friendly_name_suffix = "L" + std::to_string(phase) + " Power";
        grid_def.sensors[power_path] = power_sensor;

        // AC Voltage per phase
        std::string voltage_path = "/Ac/L" + std::to_string(phase) + "/Voltage";
        HASensorConfig voltage_sensor;
        voltage_sensor.device_class = "voltage";
        voltage_sensor.state_class = "measurement";
        voltage_sensor.unit_of_measurement = "V";
        voltage_sensor.icon = "mdi:flash";
        voltage_sensor.suggested_display_precision = 1;
        voltage_sensor.friendly_name_suffix = "L" + std::to_string(phase) + " Voltage";
        grid_def.sensors[voltage_path] = voltage_sensor;

        // AC Current per phase
        std::string current_path = "/Ac/L" + std::to_string(phase) + "/Current";
        HASensorConfig current_sensor;
        current_sensor.device_class = "current";
        current_sensor.state_class = "measurement";
        current_sensor.unit_of_measurement = "A";
        current_sensor.icon = "mdi:current-ac";
        current_sensor.suggested_display_precision = 2;
        current_sensor.friendly_name_suffix = "L" + std::to_string(phase) + " Current";
        grid_def.sensors[current_path] = current_sensor;

        // Energy consumed/produced per phase
        std::string energy_forward_path = "/Ac/L" + std::to_string(phase) + "/Energy/Forward";
        HASensorConfig energy_forward_sensor;
        energy_forward_sensor.device_class = "energy";
        energy_forward_sensor.state_class = "total_increasing";
        energy_forward_sensor.unit_of_measurement = "kWh";
        energy_forward_sensor.icon = "mdi:counter";
        energy_forward_sensor.suggested_display_precision = 2;
        energy_forward_sensor.friendly_name_suffix = "L" + std::to_string(phase) + " Energy Import";
        grid_def.sensors[energy_forward_path] = energy_forward_sensor;

        std::string energy_reverse_path = "/Ac/L" + std::to_string(phase) + "/Energy/Reverse";
        HASensorConfig energy_reverse_sensor;
        energy_reverse_sensor.device_class = "energy";
        energy_reverse_sensor.state_class = "total_increasing";
        energy_reverse_sensor.unit_of_measurement = "kWh";
        energy_reverse_sensor.icon = "mdi:counter";
        energy_reverse_sensor.suggested_display_precision = 2;
        energy_reverse_sensor.friendly_name_suffix = "L" + std::to_string(phase) + " Energy Export";
        grid_def.sensors[energy_reverse_path] = energy_reverse_sensor;
    }

    // Total power
    HASensorConfig total_power;
    total_power.device_class = "power";
    total_power.state_class = "measurement";
    total_power.unit_of_measurement = "W";
    total_power.icon = "mdi:transmission-tower";
    total_power.suggested_display_precision = 1;
    total_power.friendly_name_suffix = "Total Power";
    grid_def.sensors["/Ac/Power"] = total_power;

    addDiagnosticSensor(grid_def, {
        .dbus_path = "/Position",
        .icon = "mdi:map-marker",
        .friendly_name_suffix = "Position",
        .value_template = "{% set positions = {0: 'AC input 1', 1: 'AC output', 2: 'AC input 2'} %}{{ positions[value_json.value] | default('Unknown') }}"
    });

    addCommonDiagnosticSensors(grid_def);

    service_definitions["grid"] = std::move(grid_def);
}

void HAServiceRegistry::registerSwitchService()
{
    HAServiceDefinition switch_def;
    switch_def.friendly_name = "Switch Device";
    switch_def.model_name = "Victron Switch";

    HASensorConfig device_state_sensor;
    device_state_sensor.icon = "mdi:power-settings";
    device_state_sensor.entity_category = "diagnostic";
    device_state_sensor.friendly_name_suffix = "Device State";
	device_state_sensor.value_template = "{% set states = {256: 'Connected', 257: 'Over temperature', 258: 'Temperature warning', 259: 'Channel fault', 260: 'Channel Tripped', 261: 'Under Voltage'} %}{{ states[value_json.value] | default('Unknown (' + value_json.value|string + ')') }}";
    switch_def.sensors["/State"] = device_state_sensor;

    // Custom device name extraction
    switch_def.get_device_name = [](const std::unordered_map<std::string, Item>& items) -> std::string {
        auto custom_name = items.find("/CustomName");
        if (custom_name != items.end()) {
            std::string name = custom_name->second.get_value().value.as_text();
            if (!name.empty() && name != "---") return name;
        }

        auto product_name = items.find("/ProductName");
        if (product_name != items.end()) {
            std::string name = product_name->second.get_value().value.as_text();
            if (!name.empty() && name != "---") return name;
        }

        return "Switch Device";
    };

    addCommonDiagnosticSensors(switch_def);

    service_definitions["switch"] = std::move(switch_def);
}

const HAServiceDefinition* HAServiceRegistry::getServiceDefinition(const std::string& service_type) const
{
    auto it = service_definitions.find(service_type);
    return (it != service_definitions.end()) ? &it->second : nullptr;
}

std::vector<std::string> HAServiceRegistry::getSupportedServiceTypes() const
{
    std::vector<std::string> types;
    for (const auto& pair : service_definitions) {
        types.push_back(pair.first);
    }
    return types;
}

bool HAServiceRegistry::isSupported(const std::string& service_type) const
{
    return service_definitions.find(service_type) != service_definitions.end();
}

bool HAServiceRegistry::hasSensorPath(const std::string& service_type, const std::string& dbus_path) const
{
    auto service_def = getServiceDefinition(service_type);
    if (!service_def) return false;

    return service_def->sensors.find(dbus_path) != service_def->sensors.end();
}

const HASensorConfig* HAServiceRegistry::getSensorConfig(const std::string& service_type, const std::string& dbus_path) const
{
    auto service_def = getServiceDefinition(service_type);
    if (!service_def) return nullptr;

    auto sensor_it = service_def->sensors.find(dbus_path);
    return (sensor_it != service_def->sensors.end()) ? &sensor_it->second : nullptr;
}

// HomeAssistantDiscovery Implementation
HomeAssistantDiscovery::HomeAssistantDiscovery()
{
}

void HomeAssistantDiscovery::setEnabled(bool enable)
{
    enabled = enable;
    flashmq_logf(LOG_INFO, "Home Assistant Discovery %s", enabled ? "enabled" : "disabled");
}

bool HomeAssistantDiscovery::isEnabled() const
{
    return enabled;
}

void HomeAssistantDiscovery::setDiscoveryPrefix(const std::string &prefix)
{
    discovery_prefix = prefix;
    flashmq_logf(LOG_DEBUG, "Home Assistant Discovery prefix set to: %s", prefix.c_str());
}

const std::string &HomeAssistantDiscovery::getDiscoveryPrefix() const
{
    return discovery_prefix;
}

std::string HomeAssistantDiscovery::extractDeviceNameFromService(const std::string &full_service_name) const
{
    if (full_service_name.find("com.victronenergy.") == 0) {
        std::vector<std::string> parts = splitToVector(full_service_name, '.');
        if (parts.size() >= 4) {
            // parts[0] = "com"
            // parts[1] = "victronenergy"
            // parts[2] = "temperature" (service type)
            // parts[3] = "ruuvi_e6d73b9b0950" (device name)
            return parts[3];
        }
    }

    return "";
}

void HomeAssistantDiscovery::ensureGXSystemDevice()
{
    if (!enabled) {
        return;
    }

    std::string gx_device_id = vrm_id;  // GX system uses VRM ID as device ID

    // Check if GX system device already exists
    if (published_devices.find(gx_device_id) != published_devices.end()) {
        return;
    }

    const HAServiceDefinition* system_def = service_registry.getServiceDefinition("system");
    if (system_def) {
        std::string device_name = "GX System";
        std::string model = "Venus GX (" + vrm_id + ")";

        HADevice gx_device(device_name, model, vrm_id);
        // No via_device for GX system - it's the root

        published_devices[gx_device_id] = gx_device;

        flashmq_logf(LOG_INFO, "Created GX System device with VRM ID: %s", vrm_id.c_str());
    }
}

void HomeAssistantDiscovery::setVrmId(const std::string &vrm_id)
{
    this->vrm_id = vrm_id;
    flashmq_logf(LOG_DEBUG, "Home Assistant Discovery VRM ID set to: %s", vrm_id.c_str());
}

void HomeAssistantDiscovery::setEnabledServices(const std::vector<std::string>& services)
{
    enabled_services.clear();
    for (const std::string& service : services) {
        std::string trimmed_service = service;
        trim(trimmed_service);
        if (!trimmed_service.empty()) {
            enabled_services.insert(trimmed_service);
        }
    }
    service_filter_enabled = !enabled_services.empty();

    if (service_filter_enabled) {
        flashmq_logf(LOG_DEBUG, "Home Assistant Discovery enabled for %zu specific services", enabled_services.size());
    } else {
        flashmq_logf(LOG_DEBUG, "Home Assistant Discovery enabled for all supported services");
    }
}

bool HomeAssistantDiscovery::isServiceEnabled(const std::string& service_type) const
{
    if (!service_filter_enabled) {
        return true; // All services enabled
    }
    return enabled_services.find(service_type) != enabled_services.end();
}

std::string HomeAssistantDiscovery::sanitizeForHA(const std::string &input) const
{
    std::string result = input;

    // Replace invalid characters with underscores
    std::regex invalid_chars("[^a-zA-Z0-9_]");
    result = std::regex_replace(result, invalid_chars, "_");

    // Remove leading/trailing underscores and convert to lowercase
    result = std::regex_replace(result, std::regex("^_+|_+$"), "");
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);

    // Avoid empty strings
    if (result.empty()) {
        result = "unknown";
    }

    return result;
}

std::string HomeAssistantDiscovery::createDeviceIdentifier(const ShortServiceName &short_service_name,
                                                          const std::string &full_service_name) const
{
    if (!full_service_name.empty()) {
        std::string device_name = extractDeviceNameFromService(full_service_name);
        if (!device_name.empty()) {
            return sanitizeForHA(short_service_name.service_type) + "_" + vrm_id + "_" + sanitizeForHA(device_name);
        }
    }

    return sanitizeForHA(short_service_name.service_type) + "_" + vrm_id + "_" + sanitizeForHA(short_service_name);
}

std::string HomeAssistantDiscovery::createEntityId(const ShortServiceName &short_service_name,
                                                  const std::string &dbus_path,
                                                  const std::string &full_service_name) const
{
    std::string path_sanitized = sanitizeForHA(dbus_path);

    if (!full_service_name.empty()) {
        std::string device_name = extractDeviceNameFromService(full_service_name);
        if (!device_name.empty()) {
            return sanitizeForHA(short_service_name.service_type) + "_" + vrm_id + "_" + sanitizeForHA(device_name) + "_" + path_sanitized;
        }
    }

    return sanitizeForHA(short_service_name.service_type) + "_" + vrm_id + "_" + sanitizeForHA(short_service_name) + "_" + path_sanitized;
}

std::string HomeAssistantDiscovery::createDiscoveryTopic(const std::string &component,
                                                        const std::string &device_id,
                                                        const std::string &object_id) const
{
    return discovery_prefix + "/" + component + "/" + device_id + "/" + object_id + "/config";
}

std::string HomeAssistantDiscovery::createFriendlyEntityName(const std::string& base_device_name, const HASensorConfig& sensor_config) const
{
    if (!sensor_config.friendly_name_suffix.empty()) {
        return sensor_config.friendly_name_suffix;
    }
    return base_device_name;
}

HADevice HomeAssistantDiscovery::createDevice(const ShortServiceName &short_service_name,
                                             const HAServiceDefinition* service_def,
                                             const std::unordered_map<std::string, Item> *all_items) const
{
    std::string device_name;
    std::string model;

	if (service_def && !service_def->model_name.empty()) {
        model = service_def->model_name;
    } else {
        model = "Victron Device";
    }

    bool has_custom_name = false;

    // Try to get custom device name if we have items and a custom function
    if (all_items && service_def && service_def->get_device_name) {
        // Check if a custom name was actually set before calling the function
        auto custom_name = all_items->find("/CustomName");
        if (custom_name != all_items->end()) {
            std::string custom_name_value = custom_name->second.get_value().value.as_text();
            if (!custom_name_value.empty() && custom_name_value != "---") {
                has_custom_name = true;
            }
        }

        device_name = service_def->get_device_name(*all_items);
    } else if (service_def) {
        device_name = service_def->friendly_name;
    } else {
        device_name = "Victron Device";
    }

    // Use ProductName for model if available
    if (all_items) {
        auto product_name = all_items->find("/ProductName");
        if (product_name != all_items->end()) {
            std::string product_name_value = product_name->second.get_value().value.as_text();
            if (!product_name_value.empty() && product_name_value != "---") {
                model = product_name_value;
            }
        }
    }

	if (model.empty()) {
        model = "Victron Device";
    }

    if (!has_custom_name) {
        std::string instance_part = short_service_name;
        size_t slash_pos = instance_part.find('/');
        if (slash_pos != std::string::npos) {
            std::string instance_str = instance_part.substr(slash_pos + 1);
            if (instance_str != "0") {
                device_name += " " + instance_str;
            }
        }
    }

    std::string identifier;
    HADevice device;

    if (short_service_name.service_type == "system") {
        // The GX system device uses VRM ID as identifier (no service type prefix)
        identifier = vrm_id;
		model = "Venus GX (" + vrm_id + ")";
        device = HADevice(device_name, model, identifier);
    } else {
        identifier = createDeviceIdentifier(short_service_name);
        device = HADevice(device_name, model, identifier);
        device.via_device = vrm_id;  // Connect to GX system
    }

    return device;
}

HAEntityConfig HomeAssistantDiscovery::createEntityConfig(const Item &item,
                                                        const ShortServiceName &short_service_name,
                                                        const HASensorConfig &sensor_config,
                                                        const std::string &device_name) const
{
    std::string entity_name = createFriendlyEntityName(device_name, sensor_config);
	std::string unique_id = createEntityId(short_service_name, item.get_path(), item.get_service_name());
    std::string state_topic = "N/" + vrm_id + "/" + short_service_name + item.get_path();

    HAEntityConfig config(entity_name, unique_id, state_topic);

    // Copy sensor configuration
    config.value_template = sensor_config.value_template;
    config.unit_of_measurement = sensor_config.unit_of_measurement;
    config.device_class = sensor_config.device_class;
    config.state_class = sensor_config.state_class;
    config.icon = sensor_config.icon;
    config.entity_category = sensor_config.entity_category;
    config.enabled_by_default = sensor_config.enabled_by_default;
    config.suggested_display_precision = sensor_config.suggested_display_precision;

    config.command_topic = sensor_config.command_topic;
    config.payload_on = sensor_config.payload_on;
    config.payload_off = sensor_config.payload_off;
    config.optimistic = sensor_config.optimistic;
    config.min_value = sensor_config.min_value;
    config.max_value = sensor_config.max_value;
    config.mode = sensor_config.mode;

    return config;
}

bool HomeAssistantDiscovery::isSupportedSensor(const std::string &service_type, const std::string &dbus_path) const
{
    if (!isServiceEnabled(service_type)) {
        return false;
    }

	// Check if we have an exact match first
    if (service_registry.hasSensorPath(service_type, dbus_path)) {
        return true;
    }

    // For switch service, handle dynamic output discovery
    if (service_type == "switch") {
        return isSwitchOutputPath(dbus_path);
    }

    return false;
}

void HomeAssistantDiscovery::publishSensorEntity(const Item &item, const ShortServiceName &short_service_name)
{
    if (!enabled) {
        return;
    }

    if (!isSupportedSensor(short_service_name.service_type, item.get_path())) {
        return;
    }

	if (short_service_name.service_type != "system") {
        ensureGXSystemDevice();
    }

    flashmq_logf(LOG_DEBUG, "Publishing Home Assistant discovery for sensor: %s%s",
                 short_service_name.c_str(), item.get_path().c_str());

    try {
        const HAServiceDefinition* service_def = service_registry.getServiceDefinition(short_service_name.service_type);
        const HASensorConfig* sensor_config = service_registry.getSensorConfig(short_service_name.service_type, item.get_path());

        // Handle dynamic switch configurations
        HASensorConfig dynamic_config;
        if (!sensor_config && short_service_name.service_type == "switch") {
            dynamic_config = createDynamicSwitchSensorConfig(item.get_path(), short_service_name);
            sensor_config = &dynamic_config;
        }

        if (!service_def || !sensor_config) {
            flashmq_logf(LOG_ERR, "No service definition or sensor config found for %s%s",
                         short_service_name.service_type.c_str(), item.get_path().c_str());
            return;
        }

        // Create device (only if not already published)
        std::string device_id = createDeviceIdentifier(short_service_name, item.get_service_name());
        if (published_devices.find(device_id) == published_devices.end()) {
            HADevice device = createDevice(short_service_name, service_def, nullptr);
            published_devices[device_id] = device;
            flashmq_logf(LOG_DEBUG, "Created HA device: %s", device.name.c_str());
        }

        // Create entity configuration
        HADevice &device = published_devices[device_id];
        HAEntityConfig config = createEntityConfig(item, short_service_name, *sensor_config, device.name);
        std::string entity_id = config.unique_id;

        // Create discovery topic and payload - NOW WITH DEVICE ID
        std::string discovery_topic = createDiscoveryTopic(sensor_config->component, device_id, entity_id);
        std::string payload = config.toJson(device);

        // Publish to MQTT (always publish to update with latest info)
        flashmq_publish_message(discovery_topic, 0, true, payload); // retained = true for discovery

        // Cache the published entity
        published_entities[entity_id] = config;

        flashmq_logf(LOG_INFO, "Published Home Assistant discovery for sensor: %s", config.name.c_str());
        flashmq_logf(LOG_DEBUG, "Discovery topic: %s", discovery_topic.c_str());

    } catch (const std::exception &ex) {
        flashmq_logf(LOG_ERR, "Error publishing Home Assistant discovery for sensor: %s", ex.what());
    }
}

void HomeAssistantDiscovery::publishSensorEntityWithItems(const Item &item,
                                                         const ShortServiceName &short_service_name,
                                                         const std::unordered_map<std::string, Item> &all_items)
{
    if (!enabled) {
        return;
    }

    if (!isSupportedSensor(short_service_name.service_type, item.get_path())) {
        return;
    }

    flashmq_logf(LOG_DEBUG, "Processing Home Assistant discovery for: %s%s",
                 short_service_name.c_str(), item.get_path().c_str());

    try {
        const HAServiceDefinition* service_def = service_registry.getServiceDefinition(short_service_name.service_type);
        const HASensorConfig* sensor_config = service_registry.getSensorConfig(short_service_name.service_type, item.get_path());

        // Handle dynamic switch configurations
        HASensorConfig dynamic_config;
        if (!sensor_config && short_service_name.service_type == "switch") {
            dynamic_config = createDynamicSwitchSensorConfig(item.get_path(), short_service_name);
            sensor_config = &dynamic_config;
        }

        if (!service_def || !sensor_config) {
            flashmq_logf(LOG_ERR, "No service definition or sensor config found for %s%s",
                         short_service_name.service_type.c_str(), item.get_path().c_str());
            return;
        }

        // Create/update device with proper name using all available items
        std::string device_id = createDeviceIdentifier(short_service_name, item.get_service_name());
        HADevice device = createDevice(short_service_name, service_def, &all_items);

        // Always update device in case name changed
        published_devices[device_id] = device;

        // Create entity configuration
        HAEntityConfig config = createEntityConfig(item, short_service_name, *sensor_config, device.name);
        std::string entity_id = config.unique_id;

        // Create discovery topic and payload
        std::string discovery_topic = createDiscoveryTopic(sensor_config->component, device_id, entity_id);
        std::string payload = config.toJson(device);

        // Only publish if payload actually changed (this prevents flooding!)
        if (needsDiscoveryUpdate(entity_id, payload)) {
            flashmq_publish_message(discovery_topic, 0, true, payload); // retained = true for discovery

            // Cache the published entity and payload
            published_entities[entity_id] = config;
            cached_discovery_payloads[entity_id] = payload;

            flashmq_logf(LOG_INFO, "Published Home Assistant discovery for %s: %s (device: %s, component: %s)",
                         sensor_config->component.c_str(), config.name.c_str(), device.name.c_str(), sensor_config->component.c_str());
            flashmq_logf(LOG_DEBUG, "Discovery topic: %s", discovery_topic.c_str());
        } else {
            flashmq_logf(LOG_DEBUG, "Skipping HA discovery - no changes for: %s", entity_id.c_str());
        }

    } catch (const std::exception &ex) {
        flashmq_logf(LOG_ERR, "Error publishing Home Assistant discovery for sensor: %s", ex.what());
    }
}

// New method implementation
bool HomeAssistantDiscovery::needsDiscoveryUpdate(const std::string& entity_id, const std::string& new_payload) {
    auto it = cached_discovery_payloads.find(entity_id);
    if (it == cached_discovery_payloads.end()) {
        return true; // First time publishing
    }
    return it->second != new_payload; // Only publish if payload changed
}

void HomeAssistantDiscovery::removeSensorEntity(const Item &item, const ShortServiceName &short_service_name)
{
    if (!enabled) {
        return;
    }

    if (!isSupportedSensor(short_service_name.service_type, item.get_path())) {
        return;
    }

    try {
        std::string entity_id = createEntityId(short_service_name, item.get_path(), item.get_service_name());
        std::string device_id = createDeviceIdentifier(short_service_name, item.get_service_name());

        auto it = published_entities.find(entity_id);
        if (it != published_entities.end()) {
            const HASensorConfig* sensor_config = service_registry.getSensorConfig(short_service_name.service_type, item.get_path());
            std::string component = "sensor"; // default

            if (sensor_config) {
                component = sensor_config->component;
            } else if (short_service_name.service_type == "switch") {
                // Handle dynamic switch configuration
                HASensorConfig dynamic_config = createDynamicSwitchSensorConfig(item.get_path(), short_service_name);
                component = dynamic_config.component;
            }

            // Send empty payload to remove entity - NOW WITH DEVICE ID
            std::string discovery_topic = createDiscoveryTopic(component, device_id, entity_id);
            flashmq_publish_message(discovery_topic, 0, true, ""); // empty payload removes the entity

            published_entities.erase(it);
            flashmq_logf(LOG_INFO, "Removed Home Assistant discovery for sensor: %s", entity_id.c_str());
        }
    } catch (const std::exception &ex) {
        flashmq_logf(LOG_ERR, "Error removing Home Assistant discovery for sensor: %s", ex.what());
    }
}

void HomeAssistantDiscovery::publishAllSensorsForService(const std::string &service,
                                                       const ShortServiceName &short_service_name,
                                                       const std::unordered_map<std::string, Item> &all_items)
{
    if (!enabled) {
        return;
    }

    if (!isServiceEnabled(short_service_name.service_type)) {
        return;
    }

    flashmq_logf(LOG_DEBUG, "Publishing all Home Assistant sensors for service: %s", service.c_str());

    try {
        const HAServiceDefinition* service_def = service_registry.getServiceDefinition(short_service_name.service_type);
        if (!service_def) {
            return; // Service not supported for HA discovery
        }

        // Publish each supported sensor
        for (const auto &item_pair : all_items) {
            const std::string &dbus_path = item_pair.first;
            const Item &item = item_pair.second;

            if (isSupportedSensor(short_service_name.service_type, dbus_path)) {
                publishSensorEntityWithItems(item, short_service_name, all_items);
            }
        }

    } catch (const std::exception &ex) {
        flashmq_logf(LOG_ERR, "Error publishing all Home Assistant sensors for service %s: %s",
                     service.c_str(), ex.what());
    }
}

bool HomeAssistantDiscovery::isSwitchOutputPath(const std::string &dbus_path) const
{
    if (dbus_path == "/State") {
        return true;
    }

    if (dbus_path.find("/SwitchableOutput/") == 0) {
        return (dbus_path.find("/State") != std::string::npos ||
                dbus_path.find("/Dimming") != std::string::npos);
    }

    return false;
}

HASensorConfig HomeAssistantDiscovery::createDynamicSwitchSensorConfig(const std::string &dbus_path,
                                                                       const ShortServiceName &short_service_name) const
{
    HASensorConfig config;

    if (dbus_path == "/State") {
        config.icon = "mdi:power-settings";
        config.entity_category = "diagnostic";
        config.friendly_name_suffix = "Device State";
		config.value_template = "{% set states = {256: 'Connected', 257: 'Over temperature', 258: 'Temperature warning', 259: 'Channel fault', 260: 'Channel Tripped', 261: 'Under Voltage'} %}{{ states[value_json.value] | default('Unknown (' + value_json.value|string + ')') }}";
    }
    else if (dbus_path.find("/SwitchableOutput/") == 0) {
        // Parse the output type and number
        std::string remainder = dbus_path.substr(18); // Remove "/SwitchableOutput/"
        size_t first_slash = remainder.find('/');
        std::string output_part = remainder.substr(0, first_slash);
        std::string property = remainder.substr(first_slash + 1);

        std::string output_type;
        std::string output_number;

        // Parse output_N, pwm_N, or relay_N
        size_t underscore_pos = output_part.find('_');
        if (underscore_pos != std::string::npos) {
            output_type = output_part.substr(0, underscore_pos);
            output_number = output_part.substr(underscore_pos + 1);
        }

        if (property == "State") {
            config.component = "switch";
            config.device_class = "switch";

			config.command_topic = "W/" + vrm_id + "/" + short_service_name + dbus_path;
            config.payload_on = "{\"value\": 1}";
            config.payload_off = "{\"value\": 0}";
            config.optimistic = false; // Wait for state feedback

            config.value_template = "{% if value_json.value == 1 %}ON{% else %}OFF{% endif %}";

            if (output_type == "output") {
                config.icon = "mdi:electric-switch";
                config.friendly_name_suffix = "Output " + output_number;
            } else if (output_type == "pwm") {
                config.icon = "mdi:sine-wave";
                config.friendly_name_suffix = "PWM " + output_number;
            } else if (output_type == "relay") {
                config.icon = "mdi:relay";
                config.friendly_name_suffix = "Relay " + output_number;
            }
        }
        else if (property == "Dimming" && output_type == "pwm") {
            config.component = "number";
            config.state_class = "measurement";
            config.unit_of_measurement = "%";
            config.icon = "mdi:brightness-percent";
            config.suggested_display_precision = 0;
            config.friendly_name_suffix = "PWM " + output_number + " Dimming";

            config.command_topic = "W/" + vrm_id + "/" + short_service_name + dbus_path;
            config.min_value = 0;
            config.max_value = 100;
            config.mode = "slider";
            config.optimistic = false; // Wait for state feedback
        }
    }

    return config;
}

void HomeAssistantDiscovery::removeAllSensorsForService(const ShortServiceName &short_service_name,
                                                      const std::unordered_map<std::string, Item> &all_items)
{
    if (!enabled) {
        return;
    }

    flashmq_logf(LOG_DEBUG, "Removing all Home Assistant sensors for service: %s", short_service_name.c_str());

    try {
        // Remove each published sensor for this service
        for (const auto &item_pair : all_items) {
            const std::string &dbus_path = item_pair.first;
            const Item &item = item_pair.second;

            if (service_registry.hasSensorPath(short_service_name.service_type, dbus_path)) {
                removeSensorEntity(item, short_service_name);
            }
        }

        // Clean up the device if no more entities exist
        std::string device_id = createDeviceIdentifier(short_service_name);
        auto device_it = published_devices.find(device_id);
        if (device_it != published_devices.end()) {
            // Check if any entities still exist for this device
            bool has_entities = false;
            std::string device_prefix = device_id + "_";
            for (const auto &entity_pair : published_entities) {
                if (entity_pair.first.find(device_prefix) == 0) {
                    has_entities = true;
                    break;
                }
            }

            if (!has_entities) {
                published_devices.erase(device_it);
                flashmq_logf(LOG_DEBUG, "Removed HA device: %s", device_id.c_str());
            }
        }

    } catch (const std::exception &ex) {
        flashmq_logf(LOG_ERR, "Error removing all Home Assistant sensors for service: %s", ex.what());
    }
}

void HomeAssistantDiscovery::clearAll()
{
    if (!enabled) {
        return;
    }

    flashmq_logf(LOG_INFO, "Clearing all Home Assistant discovery entities");

    // Remove all published entities
	for (const auto &pair : published_entities) {
        try {
            // We need to parse the entity_id to get device_id and component
            // Entity ID format: vrm_id_service_type_instance_path
            // Device ID format: vrm_id_service_type_instance
            std::string entity_id = pair.first;

            // Find the device_id by looking at published devices
            std::string device_id;
            for (const auto &device_pair : published_devices) {
                if (entity_id.find(device_pair.first + "_") == 0) {
                    device_id = device_pair.first;
                    break;
                }
            }

            if (!device_id.empty()) {
                // Default to sensor component, could be made smarter
                std::string discovery_topic = createDiscoveryTopic("sensor", device_id, entity_id);
                flashmq_publish_message(discovery_topic, 0, true, ""); // empty payload removes the entity
            }
        } catch (const std::exception &ex) {
            flashmq_logf(LOG_ERR, "Error clearing Home Assistant discovery entity: %s", ex.what());
        }
    }

    published_entities.clear();
    published_devices.clear();
	cached_discovery_payloads.clear();

    flashmq_logf(LOG_INFO, "Cleared all Home Assistant discovery entities");
}
