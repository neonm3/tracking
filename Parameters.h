/* Shared Use License: This file is owned by Derivative Inc. (Derivative)
* and can only be used, and/or modified for use, in conjunction with
* Derivative's TouchDesigner software, and only if you are a licensee who has
* accepted Derivative's TouchDesigner license or assignment agreement.
*/

// Parameters.h generated/edited for GevIQ24 TOP.

#pragma once
#include <string>

namespace TD
{
	class OP_Inputs;
	class OP_ParameterManager;
}

// Parameter names/labels
constexpr static char EnableName[] = "Enable";
constexpr static char EnableLabel[] = "Enable";

constexpr static char CameraIndexName[] = "Cameraindex";
constexpr static char CameraIndexLabel[] = "Camera Index";

constexpr static char OutputModeName[] = "Outputmode";
constexpr static char OutputModeLabel[] = "Output Mode";

constexpr static char GridColsName[] = "Gridcols";
constexpr static char GridColsLabel[] = "Grid Columns";

constexpr static char DcfPathName[] = "Dcfpath";
constexpr static char DcfPathLabel[] = "DCF Path";

constexpr static char DeviceOffsetName[] = "Deviceoffset";
constexpr static char DeviceOffsetLabel[] = "Device Offset";

constexpr static char DebugLevelName[] = "Debuglevel";
constexpr static char DebugLevelLabel[] = "Debug Level";

constexpr static char DumpDevicesName[] = "Dumpdevices";
constexpr static char DumpDevicesLabel[] = "Dump MIL Devices";

// Small helper to read parameters
struct GevIQ24Params
{
	bool enable = true;
	int cameraIndex = 0;     // 0..23
	int outputMode = 0;      // 0=Selected, 1=Grid (composite)
	int gridCols = 6;        // for grid mode (e.g. 6 -> 6x4 = 24)
	std::string dcfPath;     // optional: path to DCF (or leave empty for M_DEFAULT)
	int deviceOffset = 0;    // add to cameraIndex to map to MIL dig dev numbers
	int debugLevel = 0;      // 0=Off, 1=Basic, 2=Verbose

	void load(const TD::OP_Inputs* inputs);
};

void SetupParameters(TD::OP_ParameterManager* manager);
