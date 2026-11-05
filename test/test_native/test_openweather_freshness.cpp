#include <gtest/gtest.h>
#include "OpenWeather.h"

#include "OpenWeatherTestPeer.h"
namespace {
class WeatherFreshness : public testing::Test {
protected:
 uint64_t now=100000;
 std::time_t wall=1793808000;
 config::OpenWeatherConfig config{};
 std::unique_ptr<heating::OpenWeather> weather;
 void make(unsigned interval=120) {
  config.interval=interval;
  weather=std::make_unique<heating::OpenWeather>(nullptr,config,[this]{return now;},[this]{return wall;});
 }
 void receive(float temperature=5,uint64_t date=1793808000) {
  auto json="{\"main\":{\"temp\":"+std::to_string(temperature)+"},\"dt\":"+std::to_string(date)+"}";
  heating::OpenWeatherTestPeer::receive(*weather,json.c_str());
 }
};
TEST_F(WeatherFreshness, MissingInvalidAndRealMinusTwenty) {
 make(); EXPECT_FALSE(weather->getFreshTemperature());
 receive(-20); EXPECT_EQ(weather->getFreshTemperature(),-2000);
 for (auto payload : {"{}", "{\"main\":{\"temp\":\"5\"},\"dt\":1793808000}",
   "{\"main\":{\"temp\":5}}", "{\"main\":{\"temp\":61},\"dt\":1793808000}",
   "{\"main\":{\"temp\":1e100},\"dt\":1793808000}"}) {
  heating::OpenWeatherTestPeer::receive(*weather,payload);
  EXPECT_FALSE(weather->getFreshTemperature()) << payload;
 }
 receive(); EXPECT_EQ(weather->getFreshTemperature(),500);
}
TEST_F(WeatherFreshness, ReceiveAgeIsBoundedByThirtyToOneHundredTwentyMinutes) {
 for (auto [interval,age] : {std::pair{120,1800}, {2000,4000}, {10000,7200}}) {
  now=100000; wall=1793808000; make(interval); receive();
  now+=age*1000; EXPECT_EQ(weather->getFreshTemperature(),500);
  ++now; EXPECT_FALSE(weather->getFreshTemperature());
  receive(); EXPECT_EQ(weather->getFreshTemperature(),500);
 }
}
TEST_F(WeatherFreshness, ProviderAgeAndFutureToleranceBoundaries) {
 make();
 receive(5,wall-1800); EXPECT_EQ(weather->getFreshTemperature(),500);
 receive(5,wall-1801); EXPECT_FALSE(weather->getFreshTemperature());
 receive(5,wall+300); EXPECT_EQ(weather->getFreshTemperature(),500);
 receive(5,wall+301); EXPECT_FALSE(weather->getFreshTemperature());
 receive(5,0); EXPECT_FALSE(weather->getFreshTemperature());
 receive(); EXPECT_EQ(weather->getFreshTemperature(),500);
}
TEST_F(WeatherFreshness, UnsynchronizedClockStillEnforcesReceiveAge) {
 make(); wall=100; receive(5,0);
 EXPECT_EQ(weather->getFreshTemperature(),500);
 now+=1800001; EXPECT_FALSE(weather->getFreshTemperature());
}
}
