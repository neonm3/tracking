/* Shared Use License: This file is owned by Derivative Inc. (Derivative)
* and can only be used, and/or modified for use, in conjunction with
* Derivative's TouchDesigner software, and only if you are a licensee who has
* accepted Derivative's TouchDesigner license or assignment agreement.
*/

#ifndef __BasicFilterTOP__
#define __BasicFilterTOP__

#include "TOP_CPlusPlusBase.h"
#include "Parameters.h"

#include <vector>
#include <string>

class BasicFilterTOP : public TD::TOP_CPlusPlusBase
{
public:
	BasicFilterTOP(const TD::OP_NodeInfo* info, TD::TOP_Context* context);
	~BasicFilterTOP() override;

	void getGeneralInfo(TD::TOP_GeneralInfo* ginfo, const TD::OP_Inputs* inputs, void* reserved) override;
	void execute(TD::TOP_Output* output, const TD::OP_Inputs* inputs, void* reserved) override;

	void getWarningString(TD::OP_String* warning, void* reserved) override;
	void getErrorString(TD::OP_String* error, void* reserved) override;
	void getInfoPopupString(TD::OP_String* info, void* reserved) override;
	void pulsePressed(const char* name, void* reserved) override;

	int32_t getNumInfoCHOPChans(void* reserved) override { return 0; }
	void getInfoCHOPChan(int32_t index, TD::OP_InfoCHOPChan* chan, void* reserved) override {}
	bool getInfoDATSize(TD::OP_InfoDATSize* infoSize, void* reserved) override { return false; }
	void getInfoDATEntries(int32_t index, int32_t nEntries, TD::OP_InfoDATEntries* entries, void* reserved) override {}

	void setupParameters(TD::OP_ParameterManager* manager, void* reserved) override;

private:
	TD::TOP_Context* myContext = nullptr;
	GevIQ24Params myParams;
	std::vector<uint8_t> myRGBA;
	int myW = 1280;
	int myH = 720;
	std::string myStatus;
	std::string myWarning;
	std::string myError;
	std::string myInfo;
};

#endif
