#pragma once
#include <atomic>
namespace casa {
class AlarmAudio {
public:
    void begin();
    void setAlarm(bool value) {active.store(value);}
    void test() {testRequested.store(true);}
    bool available() const {return ready.load();}
private:
    std::atomic<bool> active{false},testRequested{false},ready{false};
    static void taskEntry(void *self);
    void task();
};
}
