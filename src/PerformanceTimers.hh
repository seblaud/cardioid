#ifndef PERFORMANCE_TIMERS_HH
#define PERFORMANCE_TIMERS_HH

#include <string>
#include <iosfwd>

typedef unsigned TimerHandle;

void profileStart(const TimerHandle& handle);
void profileStop(const TimerHandle& handle);
void profileStart(const std::string& timerName);
void profileStop(const std::string& timerName);

TimerHandle profileGetHandle(const std::string& timerName);

void profileSetRefTimer(const std::string& timerName);
void profileSetPrintOrder(const std::string& timerName);
void profileDumpTimes(std::ostream&);
void profileDumpAll(const std::string& dirname);
void profileDumpStats(std::ostream& out);
#endif