#ifndef HOMEASSISTANT_DISCOVERY_H
#define HOMEASSISTANT_DISCOVERY_H

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <functional>
#include "types.h"
#include "shortservicename.h"
#include "serviceidentifier.h"
#include "vendor/json.hpp"

namespace dbus_flashmq
{

/**
 * @brief Information about a Home Assistant device
 */
struct HADevice
{
    std::string name;
    std::string manufacturer = "Victron Energy";
    std::string model;
    std::string identifiers;
    std::string sw_version;
    std::string configuration_url;
    std::string via_device;

    HADevice() = default;
    HADevice(const std::string &name, const std::string &model, const std::string &identifier, const std::string &via_device)
        : name(name), model(model), identifiers(identifier), via_device(via_device) { }

    nlohmann::json toJson() const;
};

/**
 * @brief Configuration for a Home Assistant entity sensor
 */
struct HASensorConfig
{
    std::string component = "sensor";           // sensor, binary_sensor, switch, etc.
    std::string friendly_name_suffix;          // e.g., "Voltage", "Power"
    std::string device_class;                   // temperature, voltage, current, etc.
    std::string state_class = "measurement";    // measurement, total, total_increasing
    std::string unit_of_measurement;
    std::string icon;
    std::string entity_category;               // config, diagnostic
    bool enabled_by_default = true;
    int suggested_display_precision = -1;
    std::string value_template = "{{ value_json.value }}";

    std::string command_topic;
    std::string payload_on = "1";
    std::string payload_off = "0";
    bool optimistic = false;
    int min_value = 0;
    int max_value = 100;
    std::string mode = "slider";
};

/**
 * @brief Service definition for Home Assistant discovery
 */
struct HAServiceDefinition
{
    std::string friendly_name;
    std::string model_name;
    std::string manufacturer = "Victron Energy";

    // Map of dbus_path -> sensor config
    std::unordered_map<std::string, HASensorConfig> sensors;

    // Optional custom device name extraction function
    std::function<std::string(const std::unordered_map<std::string, Item>&)> get_device_name;

    bool hasSensorPath(const std::string& dbus_path) const { return sensors.find(dbus_path) != sensors.end(); };
    const HASensorConfig* getSensorConfig(const std::string& dbus_path) const {
        auto sensor_it = sensors.find(dbus_path);
        return (sensor_it != sensors.end()) ? &sensor_it->second : nullptr;
    }
};

/**
 * @brief Home Assistant entity configuration
 */
struct HAEntityConfig
{
    std::string name;
    std::string unique_id;
    std::string state_topic;

    const HASensorConfig &sensor_config;

    HAEntityConfig(const std::string &name, const std::string &unique_id,
                   const std::string &state_topic, const HASensorConfig &sensor_config)
        : name(name), unique_id(unique_id), state_topic(state_topic), sensor_config(sensor_config) { }

    nlohmann::json toJson(const HADevice &device) const;
    nlohmann::json toJson() const;
};

/**
 * @brief Registry of all supported Victron services for HA discovery
 */
class HAServiceRegistry
{
private:
    struct DiagnosticSensorDef {
        std::string dbus_path;
        std::string icon;
        std::string friendly_name_suffix;
        std::string device_class = "";     // Optional
        std::string unit_of_measurement = ""; // Optional
        std::string value_template = "";   // Optional for state mapping
        int suggested_display_precision = -1; // -1 means not set
    };

    std::unordered_map<std::string, HAServiceDefinition> service_definitions;

    void registerTemperatureService();
    void registerBatteryService();
    void registerSolarChargerService();
    void registerVeBusService();
    void registerSystemService();
    void registerTankService();
    void registerGridMeterService();
    void registerSwitchService();
    void registerMeteoService();

    void addCommonDiagnosticSensors(HAServiceDefinition& service_def) const;
    void addDiagnosticSensor(HAServiceDefinition& service_def, const DiagnosticSensorDef& diag_def) const;
    std::vector<DiagnosticSensorDef> getCommonDiagnostics() const;

public:
    HAServiceRegistry();

    const HAServiceDefinition* getServiceDefinition(const std::string &service_type) const;
    const HASensorConfig* getSensorConfig(const std::string& service_type, const std::string& dbus_path) const;
};

/**
 * @brief Home Assistant Discovery with generic sensor support
 */
class HomeAssistantDiscovery
{
private:
    std::string discovery_prefix = "homeassistant";
    std::string vrm_id;
    bool enabled = false;

    // Optional: limit to specific service types
    std::unordered_set<std::string> enabled_services;
    bool service_filter_enabled = false;

    HAServiceRegistry service_registry;

    // Cache of published devices/entities/payloads
    std::unordered_set<std::string> published_device_ids;
    std::unordered_set<std::string> published_entity_ids;

    // Key is discovery_topic
    std::unordered_map<std::string, std::string> cached_discovery_payloads;

    // Helper methods
    static std::string createEntityId(const std::string &device_id,
                                      std::string_view dbus_path);
    static std::string toIdentifier(std::string_view input);
    static std::string createFriendlyEntityName(const std::string& base_device_name, const HASensorConfig& sensor_config);

    std::string createSystemIdentifier() const;
    std::string createDeviceIdentifier(const ShortServiceName &short_service_name) const;
    std::string createDiscoveryTopic(std::string_view component,
                                     std::string_view device_id,
                                     std::string_view entity_id) const;

    std::string createDeviceDiscoveryTopic(std::string_view device_id) const;

    HADevice createDevice(const ShortServiceName &short_service_name,
                          const HAServiceDefinition &service_def,
                          const std::unordered_map<std::string, Item> &all_items) const;

    HAEntityConfig createEntityConfig(const std::string &dbus_path,
                                      const ShortServiceName &short_service_name,
                                      const HASensorConfig &sensor_config,
                                      const std::string &device_name,
                                      const std::string &device_id) const;

    bool isServiceEnabled(const std::string& service_type) const;

    void publishSensorEntityWithItems(const std::string &dbus_path,
                                      const ShortServiceName &short_service_name,
                                      const HAServiceDefinition &service_def,
                                      const HADevice &device);

    bool isSupportedSensor(std::string_view service_type, const std::string &dbus_path, const HAServiceDefinition &service_def) const;
    HASensorConfig createDynamicSwitchSensorConfig(const std::string &dbus_path,
                                                   const ShortServiceName &short_service_name) const;
    void removeSensorEntity(const Item &item, const ShortServiceName &short_service_name);
    bool needsDiscoveryUpdate(const std::string& discovery_topic, const std::string& new_payload);

public:
    HomeAssistantDiscovery();
    ~HomeAssistantDiscovery() = default;

    // Configuration
    void setEnabled(bool enable);
    bool isEnabled() const;
    void setDiscoveryPrefix(const std::string &prefix);
    const std::string &getDiscoveryPrefix() const;
    void setVrmId(const std::string &vrm_id);
    void setEnabledServices(const std::vector<std::string>& services);

    // Core sensor support
    void publishSensorEntitiesWithItems(const std::string &service,
                                        const ShortServiceName &short_service_name,
                                        const std::unordered_map<std::string, std::unordered_map<std::string, Item>> &all_items,
                                        const std::unordered_map<std::string, Item> &changed_items);

    // Bulk operations for service lifecycle
    void publishAllConfigs() const;
    void removeAllSensorsForService(const ShortServiceName &short_service_name,
                                    const std::unordered_map<std::string, Item> &all_items);
    void clearAll();

};

}

#endif // HOMEASSISTANT_DISCOVERY_H
