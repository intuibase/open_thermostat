#pragma once

#include <functional>
#include <ostream>
#include <string_view>

namespace ib::mqtt {

class MQTTReporterInterface {
public:
	using publishDiscoverySensor_t = std::function<void(std::string_view stateTopic, std::string_view sensorUniqueId, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view valueOperation, std::string_view unit, std::string_view stateClass, std::string_view devClass)>;
	using publishStateTopic_t = std::function<void(std::string_view topic, std::string_view payload, bool retained)>;

	virtual ~MQTTReporterInterface() {}

	virtual void getStatus(std::ostream &ss) const = 0;
	virtual void publishHADiscovery(publishDiscoverySensor_t const &publish) = 0;
	virtual void publishStateTopic(publishStateTopic_t const &publish, uint16_t intervalSecs) = 0;
};

} // namespace ib::mqtt