#pragma once

#include <Arduino.h>

// Enum to define the roles
enum DeviceRole {
  ROLE_NOT_SET,
  AQUA_RESERV_PRO,
  WELLGUARD_PRO,
  HYDRO_CONTROL_GE
};

// Data structure for managed devices
struct Device {
    String id;
    String name;
    DeviceRole role;
    unsigned long lastSeen;
    bool isFull;
    bool pumpOn;
    bool faultActive;
};
