/* Shared Use License: This file is owned by Derivative Inc. (Derivative)
* and can only be used, and/or modified for use, in conjunction with
* Derivative's TouchDesigner software, and only if you are a licensee who has
* accepted Derivative's TouchDesigner license or assignment agreement.
*/

#include "Parameters.h"
#include "CPlusPlus_Common.h"

using namespace TD;

void SetupParameters(OP_ParameterManager* manager)
{
	{
		OP_NumericParameter np;
		np.name = EnableName;
		np.label = EnableLabel;
		np.defaultValues[0] = 1.0;
		manager->appendToggle(np);
	}
	{
		OP_NumericParameter np;
		np.name = CameraIndexName;
		np.label = CameraIndexLabel;
		np.minSliders[0] = 0;
		np.maxSliders[0] = 23;
		np.minValues[0] = 0;
		np.maxValues[0] = 23;
		np.defaultValues[0] = 0;
		manager->appendInt(np);
	}
	{
		OP_StringParameter sp;
		sp.name = OutputModeName;
		sp.label = OutputModeLabel;
		sp.defaultValue = "Selected";
		const char* names[] = { "Selected", "Grid" };
		const char* labels[] = { "Selected", "Grid (24-up)" };
		manager->appendMenu(sp, 2, names, labels);
	}
	{
		OP_NumericParameter np;
		np.name = GridColsName;
		np.label = GridColsLabel;
		np.minSliders[0] = 1;
		np.maxSliders[0] = 12;
		np.minValues[0] = 1;
		np.maxValues[0] = 24;
		np.defaultValues[0] = 6;
		manager->appendInt(np);
	}
	{
		OP_StringParameter sp;
		sp.name = DcfPathName;
		sp.label = DcfPathLabel;
		sp.defaultValue = "";
		manager->appendString(sp);
	}
	{
		OP_NumericParameter np;
		np.name = DeviceOffsetName;
		np.label = DeviceOffsetLabel;
		np.minSliders[0] = 0;
		np.maxSliders[0] = 128;
		np.minValues[0] = 0;
		np.maxValues[0] = 1024;
		np.defaultValues[0] = 0;
		manager->appendInt(np);
	}
	{
		OP_StringParameter sp;
		sp.name = DebugLevelName;
		sp.label = DebugLevelLabel;
		sp.defaultValue = "Off";
		const char* names[] = { "Off", "Basic", "Verbose" };
		const char* labels[] = { "Off", "Basic", "Verbose" };
		manager->appendMenu(sp, 3, names, labels);
	}
	{
		OP_NumericParameter np;
		np.name = DumpDevicesName;
		np.label = DumpDevicesLabel;
		manager->appendPulse(np);
	}
}

void GevIQ24Params::load(const OP_Inputs* inputs)
{
	enable = inputs->getParInt(EnableName) != 0;
	cameraIndex = inputs->getParInt(CameraIndexName);
	outputMode = inputs->getParInt(OutputModeName);
	gridCols = std::max(1, inputs->getParInt(GridColsName));
	dcfPath = inputs->getParString(DcfPathName) ? inputs->getParString(DcfPathName) : "";
	deviceOffset = inputs->getParInt(DeviceOffsetName);
	debugLevel = inputs->getParInt(DebugLevelName);
}
