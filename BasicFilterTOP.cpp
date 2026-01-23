/* Shared Use License: This file is owned by Derivative Inc. (Derivative)
* and can only be used, and/or modified for use, in conjunction with
* Derivative's TouchDesigner software, and only if you are a licensee who has
* accepted Derivative's TouchDesigner license or assignment agreement.
*/

#include "BasicFilterTOP.h"
#include "MilManager.h"
#include "Parameters.h"

#include <cstring>
#include <algorithm>

using namespace TD;

extern "C"
{
DLLEXPORT
void FillTOPPluginInfo(TOP_PluginInfo* info)
{
	info->apiVersion = TOPCPlusPlusAPIVersion;
	info->executeMode = TOP_ExecuteMode::CPUMem;

	OP_CustomOPInfo& customInfo = info->customOPInfo;
	customInfo.opType->setString("Geviq24");
	customInfo.opLabel->setString("GevIQ 24 In");
	customInfo.authorName->setString("Custom");
	customInfo.authorEmail->setString("n/a");

	// This TOP generates frames (no inputs).
	customInfo.minInputs = 0;
	customInfo.maxInputs = 0;
}

DLLEXPORT
TOP_CPlusPlusBase* CreateTOPInstance(const OP_NodeInfo* info, TOP_Context* context)
{
	return new BasicFilterTOP(info, context);
}

DLLEXPORT
void DestroyTOPInstance(TOP_CPlusPlusBase* instance, TOP_Context* context)
{
	delete static_cast<BasicFilterTOP*>(instance);
}
}

BasicFilterTOP::BasicFilterTOP(const OP_NodeInfo* info, TOP_Context* context)
	: myContext(context)
{
}

BasicFilterTOP::~BasicFilterTOP()
{
	// Optional: keep MIL system alive across instances for faster reloads.
}

void BasicFilterTOP::getWarningString(OP_String* warning, void* reserved)
{
	if (warning)
		warning->setString(myWarning.c_str());
}

void BasicFilterTOP::getErrorString(OP_String* error, void* reserved)
{
	if (error)
		error->setString(myError.c_str());
}

void BasicFilterTOP::getInfoPopupString(OP_String* info, void* reserved)
{
	if (info)
		info->setString(myInfo.c_str());
}

void BasicFilterTOP::pulsePressed(const char* name, void* reserved)
{
	if (!name)
		return;
	if (std::string(name) == DumpDevicesName)
	{
		MilManager& mil = MilManager::instance();
		const bool verbose = myParams.debugLevel >= 2;
		myInfo = mil.dumpDevices(64, verbose);
		myWarning = "Dumped MIL device probe to Info popup.";
		myError.clear();
	}
}

void BasicFilterTOP::setupParameters(OP_ParameterManager* manager, void* reserved)
{
	SetupParameters(manager);
}

void BasicFilterTOP::getGeneralInfo(TOP_GeneralInfo* ginfo, const OP_Inputs* inputs, void* reserved)
{
	// We generate data every frame.
	ginfo->cookEveryFrame = true;
}

void BasicFilterTOP::execute(TOP_Output* output, const OP_Inputs* inputs, void* reserved)
{
	myParams.load(inputs);
	myError.clear();
	myWarning.clear();
	myInfo.clear();

	if (!myParams.enable)
	{
		// Output black frame
		const int w = 64, h = 64;
		auto buf = myContext->createOutputBuffer((uint64_t)w*h*4, TOP_BufferFlags::None, nullptr);
		std::memset(buf->data, 0, (size_t)buf->size);

		TOP_UploadInfo info;
		info.textureDesc.width = w;
		info.textureDesc.height = h;
		info.textureDesc.pixelFormat = OP_PixelFormat::RGBA8Fixed;
		info.textureDesc.texDim = OP_TexDim::e2D;
		info.bufferOffset = 0;
		output->uploadBuffer(&buf, info, nullptr);
		return;
	}

	MilManager& mil = MilManager::instance();

	// Always keep a system summary available in the info popup.
	myInfo = mil.summaryLine();

	// If we are not actually compiled with MIL, make it unmistakable.
	if (!mil.builtWithMil())
	{
		myError = "This build is NOT using MIL (HAVE_MIL not defined). Rebuild with HAVE_MIL + MIL include/lib paths.";
		myWarning.clear();
		// Fall through to error frame.
	}

	const int camIdx = std::max(0, std::min(23, myParams.cameraIndex));
	const int devNum = myParams.deviceOffset + camIdx;

	bool ok = mil.builtWithMil();
	int w=0,h=0;

	if (ok && myParams.outputMode == 1)
	{
		ok = mil.grabGridToRGBA8(24, myParams.gridCols, myParams.deviceOffset, myParams.dcfPath, myRGBA, w, h);
	}
	else if (ok)
	{
		ok = mil.ensureDigitizer(devNum, myParams.dcfPath) && mil.grabToRGBA8(devNum, myRGBA, w, h);
	}

	// Debug status strings
	if (myParams.debugLevel >= 1)
	{
		std::string s;
		s += "camIdx=" + std::to_string(camIdx) + " devNum=" + std::to_string(devNum);
		s += " mode=" + std::string(myParams.outputMode == 1 ? "Grid" : "Selected");
		s += " dcf='" + (myParams.dcfPath.empty() ? std::string("<M_DEFAULT>") : myParams.dcfPath) + "'";
		s += " | " + mil.summaryLine();
		if (!ok)
			s += " | lastError: " + mil.lastError();
		myWarning = s;
		if (myParams.debugLevel >= 2)
		{
			myInfo += "\n";
			myInfo += "\nLastError: " + mil.lastError();
			myInfo += "\nNote: use the 'Dump MIL Devices' pulse to probe digitizer indices.";
		}
	}
	else if (!ok)
	{
		// Even if debug is off, provide an actionable error message.
		myError = mil.lastError();
	}

	if (!ok)
	{
		// Create an error frame (magenta) so it's obvious in TD.
		const int ew = 320, eh = 64;
		auto buf = myContext->createOutputBuffer((uint64_t)ew*eh*4, TOP_BufferFlags::None, nullptr);
		uint8_t* p = (uint8_t*)buf->data;
		for (int i=0;i<ew*eh;i++){ p[i*4+0]=255; p[i*4+1]=0; p[i*4+2]=255; p[i*4+3]=255; }

		TOP_UploadInfo info;
		info.textureDesc.width = ew;
		info.textureDesc.height = eh;
		info.textureDesc.pixelFormat = OP_PixelFormat::RGBA8Fixed;
		info.textureDesc.texDim = OP_TexDim::e2D;
		info.bufferOffset = 0;
		output->uploadBuffer(&buf, info, nullptr);
		return;
	}

	auto buf = myContext->createOutputBuffer((uint64_t)w*h*4, TOP_BufferFlags::None, nullptr);
	std::memcpy(buf->data, myRGBA.data(), (size_t)w*h*4);

	TOP_UploadInfo info;
	info.textureDesc.width = w;
	info.textureDesc.height = h;
	info.textureDesc.pixelFormat = OP_PixelFormat::RGBA8Fixed;
		info.textureDesc.texDim = OP_TexDim::e2D;
	info.bufferOffset = 0;
	output->uploadBuffer(&buf, info, nullptr);
}
