#include "../src/shader_graph_node_descriptor.h"
#include "../src/shader_graph_node_registry.h"

#include <cassert>
#include <filesystem>
#include <string>

static void TestDescriptorHasStablePortIds()
{
	ShaderGraphNodeDescriptor d{};
	d.id = "builtin/math/add";
	d.version = 1;
	d.inputs.push_back({ 1, "a", SGPortType::PortFloat });
	d.inputs.push_back({ 2, "b", SGPortType::PortFloat });
	assert(d.inputs[0].id == 1);
	assert(d.inputs[1].id == 2);
}

static void TestLoadDescriptorJson()
{
	const std::string json = R"({
      "format":"kaleido_shader_graph_node",
      "id":"builtin/math/add",
      "version":2,
      "inputs":[{"id":1,"name":"a","type":"float"},{"id":2,"name":"b","type":"float"}],
      "outputs":[{"id":1,"name":"out","type":"float"}],
      "compat":{"fallback":"builtin/expression/generic"}
    })";
	ShaderGraphNodeDescriptor d{};
	std::string err;
	assert(DeserializeShaderGraphNodeDescriptor(json, d, &err));
	assert(d.id == "builtin/math/add");
	assert(d.version == 2);
	assert(d.compatFallbackId == "builtin/expression/generic");
}

static void TestDescriptorParsesGlslTemplate()
{
	const std::string json = R"({
      "format":"kaleido_shader_graph_node",
      "id":"builtin/const/float",
      "version":1,
      "evalKind":"ConstFloat",
      "inputs":[],
      "outputs":[{"id":1,"name":"out","type":"float"}],
      "glslTemplate":"    float {{OUT}} = {{V0}};\n"
    })";
	ShaderGraphNodeDescriptor d{};
	std::string err;
	assert(DeserializeShaderGraphNodeDescriptor(json, d, &err));
	assert(d.glslTemplate.find("{{OUT}}") != std::string::npos);
}

static void TestRegistryFindsBuiltinNode()
{
	ShaderGraphNodeRegistry reg{};
	std::string err;
	bool loaded = false;
	for (const char* candidate : { "assets/shader_graph_nodes", "../assets/shader_graph_nodes", "../../assets/shader_graph_nodes",
		     "../../../assets/shader_graph_nodes" })
	{
		if (reg.LoadFromDirectory(candidate, &err))
		{
			loaded = true;
			break;
		}
	}
	assert(loaded);
	const ShaderGraphNodeDescriptor* d = reg.Find("builtin/math/add");
	assert(d != nullptr);
	assert(d->outputs.size() == 1);
}

int main()
{
	TestDescriptorHasStablePortIds();
	TestLoadDescriptorJson();
	TestDescriptorParsesGlslTemplate();
	TestRegistryFindsBuiltinNode();
	return 0;
}
