#pragma once

#include <Arduino.h>
#include <vector>

// Enum to define the roles
enum DeviceRole {
  ROLE_NOT_SET,
  AQUA_RESERV_PRO,
  WELLGUARD_PRO,
  HYDRO_CONTROL_GE
};

// Enum for the state of a reservoir
enum ReservoirState {
    LEVEL_EMPTY,
    LEVEL_FULL,
    LEVEL_UNKNOWN
};

// Enum for device status
enum DeviceStatus {
    STATUS_OK,
    STATUS_DISCONNECTED
};

// Data structure for managed devices
struct Device {
    String id;
    String name;
    DeviceRole role;
    unsigned long lastSeen;
    ReservoirState level;
    bool pumpOn;
    bool faultActive;
    DeviceStatus status;
    String assignedWellId;
    std::vector<String> assignedReservoirIds;
};
