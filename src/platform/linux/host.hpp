#pragma once

#include "app/host.hpp"

#include <string>
#include <vector>

std::vector<VolumeEntry> linuxListVolumes();
bool linuxPickPath(std::string& path, bool save);
void linuxLaunch(const std::string& program, const std::string& arg);
bool linuxLaunchRootScan();
void linuxFillHost(Host& host);
