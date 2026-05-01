#include "../src/shader_graph_types.h"

#include <cassert>

static void TestBoolConversionPolicy()
{
	assert(SGPortTypeCanImplicitConvert(SGPortType::PortBool, SGPortType::PortBool));
	assert(!SGPortTypeCanImplicitConvert(SGPortType::PortBool, SGPortType::PortFloat));
	assert(!SGPortTypeCanImplicitConvert(SGPortType::PortFloat, SGPortType::PortBool));
}

static void TestNumericPromotionPolicy()
{
	assert(SGPortTypeCanImplicitConvert(SGPortType::PortInt, SGPortType::PortFloat));
	assert(SGPortTypeCanImplicitConvert(SGPortType::PortFloat, SGPortType::PortVec3));
	assert(SGPortTypeCanImplicitConvert(SGPortType::PortVec3, SGPortType::PortFloat));
	assert(SGPortTypeCanImplicitConvert(SGPortType::PortVec2, SGPortType::PortVec4));
	assert(SGPortTypeCanImplicitConvert(SGPortType::PortVec3, SGPortType::PortVec4));
	assert(SGPortTypeCanImplicitConvert(SGPortType::PortVec4, SGPortType::PortVec3));
}

int main()
{
	TestBoolConversionPolicy();
	TestNumericPromotionPolicy();
	return 0;
}
