// format: stage_count global_state (stage_state_in_hex)+
#define UBER_SHADER_PS_LIST(PS) \
	PS(1_0_0) \
	PS(1_0_13) \
	PS(1_0_22) \
	PS(1_0_23) \
	PS(1_0_25) \
	PS(1_0_40000000) \
	PS(1_0_41) \
	PS(1_0_62) \
	PS(1_0_65) \
	PS(1_0_83) \
	PS(1_1_0) \
	PS(1_1_13) \
	PS(1_1_22) \
	PS(1_1_23) \
	PS(1_1_25) \
	PS(1_1_40000000) \
	PS(1_1_41) \
	PS(1_1_62) \
	PS(1_1_65) \
	PS(1_1_83) \
	PS(2_0_0_13) \
	PS(2_0_0_20000000) \
	PS(2_0_0_22) \
	PS(2_0_0_53) \
	PS(2_0_0_65) \
	PS(2_0_0_78) \
	PS(2_0_0_83) \
	PS(2_0_13_13) \
	PS(2_0_22_22) \
	PS(2_0_40000000_13) \
	PS(2_1_0_13) \
	PS(2_1_0_20000000) \
	PS(2_1_0_22) \
	PS(2_1_0_53) \
	PS(2_1_0_65) \
	PS(2_1_0_78) \
	PS(2_1_0_83) \
	PS(2_1_13_13) \
	PS(2_1_22_22) \
	PS(2_1_40000000_13) \
	PS(3_0_0_0_22) \
	PS(3_0_0_0_65) \
	PS(3_0_0_13_22) \
	PS(3_0_0_13_61) \
	PS(3_0_0_22_22) \
	PS(3_0_0_22_62) \
	PS(3_0_0_22_65) \
	PS(3_0_0_40000065_13) \
	PS(3_0_0_65_13) \
	PS(3_0_0_65_22) \
	PS(3_0_0_65_83) \
	PS(3_0_22_22_22) \
	PS(3_1_0_0_22) \
	PS(3_1_0_0_65) \
	PS(3_1_0_13_22) \
	PS(3_1_0_13_61) \
	PS(3_1_0_22_22) \
	PS(3_1_0_22_62) \
	PS(3_1_0_22_65) \
	PS(3_1_0_40000065_13) \
	PS(3_1_0_65_13) \
	PS(3_1_0_65_22) \
	PS(3_1_0_65_83) \
	PS(3_1_22_22_22) \
	PS(4_0_0_0_40000062_83) \
	PS(4_0_0_13_65_65) \
	PS(4_0_0_62_13_22) \
	PS(4_0_0_65_22_62) \
	PS(4_0_0_65_65_22) \
	PS(4_0_25_25_25_25) \
	PS(4_1_0_0_40000062_83) \
	PS(4_1_0_13_65_65) \
	PS(4_1_0_62_13_22) \
	PS(4_1_0_65_22_62) \
	PS(4_1_0_65_65_22) \
	PS(4_1_25_25_25_25) \
	PS(5_0_0_22_22_65_83) \
	PS(5_0_22_22_22_22_22) \
	PS(5_1_0_22_22_65_83) \
	PS(5_1_22_22_22_22_22)

struct UberPixelShaderState
{
	int stageStates[8] = {};
	int stageCount = 0;
	int globalState = 0;
};

static bool ParseUberPixelShaderState(UberPixelShaderState& state, const char* stateString)
{
	const char* scanStrings[8] =
	{
		"%X_%X",
		"%X_%X_%X",
		"%X_%X_%X_%X",
		"%X_%X_%X_%X_%X",
		"%X_%X_%X_%X_%X_%X",
		"%X_%X_%X_%X_%X_%X_%X",
		"%X_%X_%X_%X_%X_%X_%X_%X",
		"%X_%X_%X_%X_%X_%X_%X_%X_%X"
	};

	if(sscanf(stateString, "%d", &state.stageCount) != 1 ||
		state.stageCount < 1 ||
		state.stageCount > 8)
	{
		return false;
	}

	stateString += 2;
	const char* scanString = scanStrings[state.stageCount - 1];

	if(sscanf(stateString, scanString,
		&state.globalState,
		&state.stageStates[0], &state.stageStates[1],
		&state.stageStates[2], &state.stageStates[3],
		&state.stageStates[4], &state.stageStates[5],
		&state.stageStates[6], &state.stageStates[7]) != state.stageCount + 1)
	{
		return false;
	}

	return true;
}
