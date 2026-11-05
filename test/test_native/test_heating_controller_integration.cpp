#include <Arduino.h>
#include <gtest/gtest.h>
#include "OpenWeatherTestPeer.h"
#include "HeatingControllerCore.h"
#include "RoomTestPeer.h"

namespace native_controller {
inline config::BoilerConfig boiler;
inline std::vector<heating::RoomConfig> rooms;
inline std::vector<config::PinConfig> pins;
inline uint64_t nowMillis=1793808000000ULL;
inline std::time_t wall=1793808000;
struct Weather : heating::OpenWeather {
 Weather(std::shared_ptr<ib::logger::LoggerInterface> log,config::OpenWeatherConfig c)
  : OpenWeather(log,c,[]{return nowMillis;},[]{return wall;}) {}
};
struct Reader {
 using BleDevices_t=std::set<heating::BleAddress_t>;
 template<class... T> Reader(T&&...) {}
 void triggerScan() {} bool isScanPending() { return false; } void shutDownBLE() {}
};
struct State {
 mutable std::mutex mutex;
 uint64_t aosSupplyUpdated=nowMillis, aosHeatingUpdated=nowMillis, aosOutdoorUpdated=nowMillis;
 std::optional<int16_t> outdoorTemperature=500;
 std::optional<uint16_t> currentFlowTemperature=450;
 std::optional<bool> heatingActive=true;
};
struct Ems {
 State state;
 bool enabled=false;
 uint8_t flow=0, setting=0;
 unsigned commands=0, writes=0;
 template<class... T> Ems(T&&...) {}
 State &getBoilerState() { return state; }
 void changeBoilerState(bool on,uint8_t t) { enabled=on; flow=t; ++commands; }
 void setHeatingTemperature(uint8_t t) { setting=t; ++writes; }
 template<class... T> void registerTelegramProcessor(T&&...) {}
 template<class... T> void getStatus(T&&...) const {}
 template<class... T> void getBoilerParams(T&&...) const {}
 template<class... T> void getDiagnostics(T&&...) const {}
 void loop() {}
};
struct Noop {
 template<class... T> Noop(T&&...) {}
 template<class... T> void getMetrics(T&&...) const {}
 void loop() {}
};
struct Services {
 using BeaconTemperatureReader=Reader;
 using OpenWeather=Weather;
 using EmsController=Ems;
 using EmsMetrics=Noop;
 using MQTT=Noop;
 template<class C> static auto &roomList(C &c) { return c.rooms_; }
 template<class C> static auto &ems(C &c) { return c.ems_; }
 template<class C> static auto &weather(C &c) { return c.openWeather_; }
 template<class C> static auto &boilerOutput(C &c) { return c.boiler_; }
};
using Controller=heating::HeatingControllerCore<Services>;
}
namespace config {
BoilerConfig getBoilerConfig() { return native_controller::boiler; }
OpenWeatherConfig getOpenWeatherConfig() { return {}; }
std::vector<heating::RoomConfig> getRoomsConfig(const std::string &) { return native_controller::rooms; }
std::string getCurrentProgram() { return "Test"; }
std::vector<GpioExtenderConfig> getGpioExtenders() { return {}; }
std::optional<PinConfig> getBoilerPin() { return {}; }
std::vector<PinConfig> getValvePins() { return native_controller::pins; }
}
namespace {
using Peer=heating::RoomTestPeer;
using Services=native_controller::Services;
class HeatingOperation : public testing::Test {
protected:
 heating::BoilerController::clock_t::time_point now{};
 std::unique_ptr<native_controller::Controller> controller;
 void SetUp() override {
  native_controller::boiler={};
  auto &b=native_controller::boiler;
  b.boiler.controlMode=config::BoilerConfig::controlMode_t::ems;
  b.boiler.supplyMode=config::BoilerConfig::supplyMode_t::fixed;
  b.boiler.fixedSupplyTemperature=45;
  b.boiler.outdoorSensor=config::BoilerConfig::outdoorSensor_t::ems;
  native_controller::rooms.clear();
  native_controller::pins.clear();
  for (int i=0;i<2;++i) {
   heating::RoomConfig r{}; r.id_=i?"bbbbbbbb":"aaaaaaaa"; r.name_=i?"Bedroom":"Office";
   r.enabled_=true; r.baseTemperature_=2100; r.adaptiveStart_.thermalLearningEnabled=true;
   heating::RoomConfig::TemperatureSetting s{}; s.timeFrom_=0;s.timeTo_=2359;s.temperature_=2200;
   s.heatingTemperatureOverride_=i?60:70; r.temperatures_.push_back(s);
   native_controller::rooms.push_back(r);
  }
 }
 void boot(bool reverse=false) {
  if(reverse) std::reverse(native_controller::rooms.begin(),native_controller::rooms.end());
  controller=std::make_unique<native_controller::Controller>(nullptr,[this]{return now;});
  controller->stopBluetoothScan();
  for(auto &r:Services::roomList(*controller)) r->storeTemperature(2000);
 }
 void clearOverrides() {
  for(auto &r:native_controller::rooms) r.temperatures_.clear();
  controller->reloadConfiguration();
  for(auto &r:Services::roomList(*controller)) r->storeTemperature(2000);
 }
};
TEST_F(HeatingOperation, ActualOperateAttributesBoostToPreviousIntervalInBothRoomOrders) {
 for(bool reverse:{false,true}) {
  SetUp();boot(reverse);
  controller->operate();
  auto &ems=Services::ems(*controller);
  ASSERT_TRUE(ems.enabled); EXPECT_EQ(ems.flow,70);
  for(auto &r:Services::roomList(*controller)) {
   EXPECT_FALSE(Peer::context(*r).supplyBoostApplied); // New request is not previous output.
   Peer::seedWarmup(*r);
  }
  controller->operate();
  for(auto &r:Services::roomList(*controller)) {
   EXPECT_TRUE(Peer::context(*r).supplyBoostApplied);
   EXPECT_EQ(Peer::context(*r).appliedSupply,70);
   EXPECT_TRUE(Peer::pendingReset(*r)); EXPECT_EQ(Peer::finishWarmup(*r),0u);
  }
  clearOverrides();controller->operate();
  EXPECT_EQ(ems.flow,45);
  for(auto &r:Services::roomList(*controller)) EXPECT_TRUE(Peer::context(*r).supplyBoostApplied);
  controller->operate();
  for(auto &r:Services::roomList(*controller)) {
   EXPECT_FALSE(Peer::context(*r).supplyBoostApplied);
   EXPECT_EQ(Peer::context(*r).appliedSupply,45);
   EXPECT_TRUE(Peer::eligible(*r));
   Peer::seedWarmup(*r);EXPECT_EQ(Peer::finishWarmup(*r),1u);
  }
 }
}
TEST_F(HeatingOperation, DelayManualTestAndStopNeverCountPendingRequestAsApplied) {
 native_controller::boiler.boiler.valvePreheatingDelay=30;boot();
 controller->operate(); EXPECT_EQ(Services::ems(*controller).commands,0u);
 now+=std::chrono::seconds(29);controller->operate();
 for(auto &r:Services::roomList(*controller)) EXPECT_FALSE(Peer::context(*r).supplyBoostApplied);
 now+=std::chrono::seconds(1);controller->operate();EXPECT_EQ(Services::ems(*controller).flow,70);
 for(auto &r:Services::roomList(*controller)) EXPECT_FALSE(Peer::context(*r).supplyBoostApplied);
 controller->operate();
 for(auto &r:Services::roomList(*controller)) EXPECT_TRUE(Peer::context(*r).supplyBoostApplied);
 controller->startManualGpioTest(true,{},10);const auto commands=Services::ems(*controller).commands;
 controller->operate();EXPECT_EQ(Services::ems(*controller).commands,commands);
 for(auto &r:Services::roomList(*controller)) {
  EXPECT_TRUE(Peer::context(*r).manualTest);EXPECT_FALSE(Peer::context(*r).supplyBoostApplied);
 }
 controller->stopManualGpioTest();controller->operate();
 for(auto &r:Services::roomList(*controller)) EXPECT_FALSE(Peer::context(*r).supplyBoostApplied);
}
TEST_F(HeatingOperation, ForecastUsesSameLimitedRoundedCurveAndFallbackAsActualOutput) {
 auto &b=native_controller::boiler;
 b.boiler.supplyMode=config::BoilerConfig::supplyMode_t::curve;
 b.boiler.fixedSupplyTemperature=48;b.boiler.weatherFallback=false;b.boiler.maxHeatingTemp=60;
 b.heatingCurve.heatingCurve={20,25,30,35,45,55,65,75,80};
 for(auto &r:native_controller::rooms) r.temperatures_.clear();
 boot();auto &state=Services::ems(*controller).state;
 for(auto [outdoor,expected]:{std::pair{475,35},{-2000,60},{7000,48},{500,35}}) {
  state.outdoorTemperature=outdoor;
  controller->operate(); EXPECT_EQ(Services::ems(*controller).flow,expected);
  for(auto &r:Services::roomList(*controller)) {
   EXPECT_EQ(Peer::predictedSupply(*r),expected);
   EXPECT_FALSE(Peer::context(*r).supplyBoostApplied);
  }
 }
}
TEST_F(HeatingOperation, ActualWeatherSourcesAndStaleEmsMatchForecastAndCommands) {
 using Source=config::BoilerConfig::outdoorSensor_t;
 struct Case { Source source; bool fallback, emsFresh; int expected; };
 for(auto test : {Case{Source::ems,false,true,35}, {Source::openweather,false,true,28},
     {Source::automatic,false,true,35}, {Source::ems,true,false,28},
     {Source::ems,false,false,48}, {Source::automatic,false,false,28}}) {
  SetUp();
  auto &b=native_controller::boiler;
  b.boiler.supplyMode=config::BoilerConfig::supplyMode_t::curve;
  b.boiler.fixedSupplyTemperature=48;b.boiler.outdoorSensor=test.source;b.boiler.weatherFallback=test.fallback;
  b.heatingCurve.heatingCurve={20,25,30,35,45,55,65,75,80};
  for(auto &r:native_controller::rooms) r.temperatures_.clear();
  boot();
  heating::OpenWeatherTestPeer::receive(Services::weather(*controller),
   "{\"main\":{\"temp\":12},\"dt\":1793808000}");
  if(!test.emsFresh) Services::ems(*controller).state.aosOutdoorUpdated-=1800001;
  controller->operate();EXPECT_EQ(Services::ems(*controller).flow,test.expected);
  for(auto &r:Services::roomList(*controller)) EXPECT_EQ(Peer::predictedSupply(*r),test.expected);
 }
}
TEST_F(HeatingOperation, WeatherLossPreservesRelayBoostAndBlocksBothRoomsUntilRecovery) {
 auto &b=native_controller::boiler;
 b.boiler.controlMode=config::BoilerConfig::controlMode_t::onoff_outdoor;
 b.boiler.supplyMode=config::BoilerConfig::supplyMode_t::legacy;
 b.boiler.fixedSupplyTemperature.reset();b.boiler.weatherFallback=false;
 b.heatingCurve.heatingCurve={20,25,30,35,45,55,65,75,80};
 boot();controller->operate();
 ASSERT_EQ(Services::ems(*controller).setting,70);
 Services::ems(*controller).state.outdoorTemperature.reset();
 clearOverrides();controller->operate();
 EXPECT_EQ(Services::ems(*controller).setting,70);
 controller->operate();
 for(auto &r:Services::roomList(*controller)) {
  EXPECT_TRUE(Peer::context(*r).supplyBaselineUnknown);
  EXPECT_TRUE(Peer::context(*r).supplyBoostApplied);
  EXPECT_EQ(Peer::context(*r).appliedSupply,70);
  EXPECT_FALSE(Peer::eligible(*r));
 }
 Services::ems(*controller).state.outdoorTemperature=500;
 controller->operate();EXPECT_EQ(Services::ems(*controller).setting,35);
 controller->operate();
 for(auto &r:Services::roomList(*controller)) {
  EXPECT_FALSE(Peer::context(*r).supplyBaselineUnknown);
  EXPECT_FALSE(Peer::context(*r).supplyBoostApplied);
  EXPECT_TRUE(Peer::eligible(*r));
 }
}

TEST_F(HeatingOperation, ActualOperateClosesOnlySatisfiedPhysicalRoomValves) {
 for(int i=0;i<2;++i) {
  config::PinConfig pin;pin.source="builtin";pin.pin=5+i;pin.label=i?"Bedroom":"Office";
  native_controller::pins.push_back(pin);
  native_controller::rooms[i].valves_={pin.label};
 }
 native_controller::rooms[0].temperatures_.clear();native_controller::rooms[0].baseTemperature_=1800;
 boot();controller->operate();controller->operate();
 auto &boiler=Services::boilerOutput(*controller);
 EXPECT_TRUE(boiler.isBoilerStarted());EXPECT_FALSE(boiler.isValveOpen("Office"));EXPECT_TRUE(boiler.isValveOpen("Bedroom"));
 controller->operate();
 EXPECT_FALSE(Peer::context(*Services::roomList(*controller)[0]).valveOpen.value());
 EXPECT_TRUE(Peer::context(*Services::roomList(*controller)[1]).valveOpen.value());
}
TEST_F(HeatingOperation, StaleEmsActivityAndStopCancelCalibrationInTheCorrectInterval) {
 boot();controller->operate();
 auto &ems=Services::ems(*controller);
 ems.state.aosHeatingUpdated-=180001;
 controller->operate();
 for(auto &r:Services::roomList(*controller)) {
  EXPECT_TRUE(Peer::context(*r).supplyBoostApplied);EXPECT_FALSE(Peer::eligible(*r));
  EXPECT_TRUE(Peer::pendingReset(*r));
 }
 for(auto &r:Services::roomList(*controller))
  controller->setRoomTemporaryTemperature(r->getName(),1800,600);
 controller->operate();EXPECT_FALSE(ems.enabled);
 ems.state.heatingActive=false;ems.state.aosHeatingUpdated=native_controller::nowMillis;
 controller->operate();
 for(auto &r:Services::roomList(*controller)) {
  EXPECT_FALSE(Peer::context(*r).boilerCommanded.value());
  EXPECT_FALSE(Peer::context(*r).supplyBoostApplied);
 }
}

TEST_F(HeatingOperation, WeatherRecoveryResumesRoomsInsideHysteresisAfterValveDelay) {
 auto &b=native_controller::boiler;
 b.boiler.supplyMode=config::BoilerConfig::supplyMode_t::legacy;
 b.boiler.fixedSupplyTemperature.reset();b.boiler.weatherFallback=false;b.boiler.valvePreheatingDelay=30;
 b.heatingCurve.heatingCurve={20,25,30,35,45,55,65,75,80};
 boot();controller->operate();now+=std::chrono::seconds(30);controller->operate();
 auto &ems=Services::ems(*controller);ASSERT_TRUE(ems.enabled);
 for(auto &r:Services::roomList(*controller)) { Peer::dropReading(*r);r->storeTemperature(2200); }
 ems.state.outdoorTemperature.reset();controller->operate();ASSERT_FALSE(ems.enabled);
 ems.state.outdoorTemperature=500;controller->operate();EXPECT_FALSE(ems.enabled);
 now+=std::chrono::seconds(29);controller->operate();EXPECT_FALSE(ems.enabled);
 now+=std::chrono::seconds(1);controller->operate();EXPECT_TRUE(ems.enabled);EXPECT_EQ(ems.flow,70);
}
TEST_F(HeatingOperation, DemandSatisfiedDuringWeatherOutageDoesNotResumeInHysteresis) {
 auto &b=native_controller::boiler;
 b.boiler.supplyMode=config::BoilerConfig::supplyMode_t::legacy;
 b.boiler.fixedSupplyTemperature.reset();b.boiler.weatherFallback=false;
 b.heatingCurve.heatingCurve={20,25,30,35,45,55,65,75,80};
 boot();controller->operate();auto &ems=Services::ems(*controller);ASSERT_TRUE(ems.enabled);
 ems.state.outdoorTemperature.reset();controller->operate();ASSERT_FALSE(ems.enabled);
 for(auto &r:Services::roomList(*controller)) { Peer::dropReading(*r);r->storeTemperature(2300); }
 controller->operate();
 for(auto &r:Services::roomList(*controller)) { Peer::dropReading(*r);r->storeTemperature(2200); }
 ems.state.outdoorTemperature=500;controller->operate();EXPECT_FALSE(ems.enabled);
}

TEST_F(HeatingOperation, OutdoorInputRemainsAvailableWithFixedOrBoilerSettingSupply) {
 for(bool boilerSetting:{false,true}) {
  SetUp();auto &b=native_controller::boiler;
  b.boiler.outdoorSensor=config::BoilerConfig::outdoorSensor_t::openweather;
  b.boiler.weatherFallback=false;
  b.boiler.controlMode=boilerSetting?config::BoilerConfig::controlMode_t::onoff:config::BoilerConfig::controlMode_t::ems;
  b.boiler.supplyMode=boilerSetting?config::BoilerConfig::supplyMode_t::boiler:config::BoilerConfig::supplyMode_t::fixed;
  for(auto &r:native_controller::rooms) r.temperatures_.clear();
  boot();Services::ems(*controller).state.outdoorTemperature.reset();
  heating::OpenWeatherTestPeer::receive(Services::weather(*controller),
   "{\"main\":{\"temp\":12},\"dt\":1793808000}");
  controller->operate();
  EXPECT_TRUE(Services::boilerOutput(*controller).isBoilerStarted());
  if(boilerSetting) EXPECT_EQ(Services::ems(*controller).commands,0u);
  else EXPECT_EQ(Services::ems(*controller).flow,45);
  for(auto &r:Services::roomList(*controller)) {
   EXPECT_STREQ(Peer::context(*r).outdoorSource,"openweather");
   auto status=cJSON_Parse(r->getStatus().c_str());ASSERT_NE(status,nullptr);
   auto outdoor=cJSON_GetObjectItem(cJSON_GetObjectItem(status,"adaptiveStart"),"outdoorTemp");
   ASSERT_TRUE(cJSON_IsNumber(outdoor));EXPECT_EQ(outdoor->valueint,12);
   cJSON_Delete(status);
   if(boilerSetting) EXPECT_TRUE(std::isnan(Peer::predictedSupply(*r)));
   else EXPECT_EQ(Peer::predictedSupply(*r),45);
  }
 }
}

}
