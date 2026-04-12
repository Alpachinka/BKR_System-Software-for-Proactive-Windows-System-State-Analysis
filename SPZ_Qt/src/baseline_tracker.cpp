#include "baseline_tracker.h"
#include <numeric>

BaselineTracker::BaselineTracker(QObject* parent) : QObject(parent) {}

void BaselineTracker::addSample(int cpu, int ram)
{
    // Add new values
    m_cpu.push_back(cpu);
    m_ram.push_back(ram);
    m_cpuSum += cpu;
    m_ramSum += ram;

    // Remove oldest values if window is full
    if (static_cast<int>(m_cpu.size()) > WINDOW) {
        m_cpuSum -= m_cpu.front();
        m_ramSum -= m_ram.front();
        m_cpu.pop_front();
        m_ram.pop_front();
    }
}

double BaselineTracker::avgCpu() const
{
    if (m_cpu.empty()) return -1.0;
    return static_cast<double>(m_cpuSum) / m_cpu.size();
}

double BaselineTracker::avgRam() const
{
    if (m_ram.empty()) return -1.0;
    return static_cast<double>(m_ramSum) / m_ram.size();
}
