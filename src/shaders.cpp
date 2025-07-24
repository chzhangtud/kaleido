#include "shaders.h"
#include "config.h"
#include <stdio.h>

#if VK_HEADER_VERSION >= 135
#include <spirv-headers/spirv.h>
#else
#include <vulkan/spirv.h>
#endif

// https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.pdf

struct Id
{
	uint32_t opcode;
	uint32_t typeId;
	uint32_t storageClass;
	uint32_t binding;
	uint32_t set;
	uint32_t constant;
};

static VkShaderStageFlagBits getShaderStage(SpvExecutionModel model)
{
	switch (model)
	{
	case SpvExecutionModelVertex:
		return VK_SHADER_STAGE_VERTEX_BIT;
	case SpvExecutionModelGeometry:
		return VK_SHADER_STAGE_GEOMETRY_BIT;
	case SpvExecutionModelFragment:
		return VK_SHADER_STAGE_FRAGMENT_BIT;
	case SpvExecutionModelGLCompute:
		return VK_SHADER_STAGE_COMPUTE_BIT;
	case SpvExecutionModelMeshEXT:
		return VK_SHADER_STAGE_MESH_BIT_EXT;
	case SpvExecutionModelTaskEXT:
		return VK_SHADER_STAGE_TASK_BIT_EXT;
	default:
		assert(!"Unsupported execution model.");
		return VkShaderStageFlagBits(0);;
	}
}

static VkDescriptorType getDescriptorType(SpvOp op)
{
	switch (op)
	{
	case SpvOpTypeStruct:
		return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	case SpvOpTypeImage:
		return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	case SpvOpTypeSampler:
		return VK_DESCRIPTOR_TYPE_SAMPLER;
	case SpvOpTypeSampledImage:
		return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	case SpvOpTypeAccelerationStructureKHR:
		return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	default:
		assert(!"Unknown resource type");
		return VkDescriptorType(0);
	}
}

static void parseShader(Shader& shader, const uint32_t* code, uint32_t codeSize)
{
	assert(code[0] == SpvMagicNumber);

	uint32_t idBound = code[3];

	int localSizeIdX = -1;
	int localSizeIdY = -1;
	int localSizeIdZ = -1;

	std::vector<Id> ids(idBound);

	const uint32_t* insn = code + 5;

	while (insn != code + codeSize)
	{
		uint16_t opcode = uint16_t(insn[0]);
		uint16_t wordCount = uint16_t(insn[0] >> 16);

		switch (opcode)
		{
		case SpvOpVariable:
		{
			assert(wordCount >= 4);

			uint32_t id = insn[2];
			assert(id < idBound);

			assert(ids[id].opcode == 0);
			ids[id].opcode = opcode;
			ids[id].typeId = insn[1];
			ids[id].storageClass = insn[3];
			break;
		}
		case SpvOpEntryPoint:
		{
			assert(wordCount >= 2);
			shader.stage = getShaderStage(SpvExecutionModel(insn[1]));
			break;
		}
		case SpvOpExecutionMode:
		{
			assert(wordCount >= 3);
			uint32_t mode = insn[2];
			switch (mode)
			{
			case SpvExecutionModeLocalSize:
				assert(wordCount == 6);
				shader.localSizeX = insn[3];
				shader.localSizeY = insn[4];
				shader.localSizeZ = insn[5];
				break;
			}
			break;
		}
		case SpvOpExecutionModeId:
		{
			assert(wordCount >= 3);
			uint32_t mode = insn[2];

			switch (mode)
			{
			case SpvExecutionModeLocalSizeId:
				assert(wordCount == 6);
				localSizeIdX = int(insn[3]);
				localSizeIdY = int(insn[4]);
				localSizeIdZ = int(insn[5]);
				break;
			}
			break;
		}
		case SpvOpDecorate:
		{
			assert(wordCount >= 3);
			uint32_t id = insn[1];

			switch (insn[2])
			{
			case SpvDecorationDescriptorSet:
				assert(wordCount == 4);
				ids[id].set = insn[3];
				break;
			case SpvDecorationBinding:
				assert(wordCount == 4);
				ids[id].binding = insn[3];
				break;
			}
			break;
		}
		case SpvOpTypeStruct:
		case SpvOpTypeImage:
		case SpvOpTypeSampler:
		case SpvOpTypeSampledImage:
		case SpvOpTypeAccelerationStructureKHR:
		{
			assert(wordCount >= 2);

			uint32_t id = insn[1];
			assert(id < idBound);

			assert(ids[id].opcode == 0);
			ids[id].opcode = opcode;
			break;
		}
		case SpvOpTypePointer:
		{
			assert(wordCount == 4);

			uint32_t id = insn[1];
			assert(id < idBound);

			assert(ids[id].opcode == 0);
			ids[id].opcode = opcode;
			ids[id].typeId = insn[3];
			ids[id].storageClass = insn[2];
			break;
		}
		case SpvOpConstant:
		{
			assert(wordCount >= 4); // we currently only correctly handle 32-bit integer constants

			uint32_t id = insn[2];
			assert(id < idBound);

			assert(ids[id].opcode == 0);
			ids[id].opcode = opcode;
			ids[id].typeId = insn[1];
			ids[id].constant = insn[3]; // note: this is the value, not the id of the constant
			break;
		}
		}
		assert(insn + wordCount <= code + codeSize);
		insn += wordCount;
	}

	for (const auto& id : ids)
	{
		// set 0 is reserved for push descriptors
		if (id.opcode == SpvOpVariable && (id.storageClass == SpvStorageClassUniform || id.storageClass == SpvStorageClassUniformConstant || id.storageClass == SpvStorageClassStorageBuffer) && id.set == 0)
		{
			assert(id.set == 0);
			assert(id.binding < 32);
			assert(ids[id.typeId].opcode == SpvOpTypePointer);

			uint32_t typeKind = ids[ids[id.typeId].typeId].opcode;
			VkDescriptorType resourceType = getDescriptorType(SpvOp(typeKind));
			
			assert((shader.resourceMask& (1 << id.binding)) == 0 || shader.resourceTypes[id.binding] == resourceType);

			shader.resourceTypes[id.binding] = resourceType;
			shader.resourceMask |= 1 << id.binding;
		}

		if (id.opcode == SpvOpVariable && id.storageClass == SpvStorageClassUniformConstant && id.set == 1)
		{
			shader.useDescriptorArray = true;
		}

		if (id.opcode == SpvOpVariable && id.storageClass == SpvStorageClassPushConstant)
		{
			shader.usePushConstants = true;
		}
	}

	if (shader.stage == VK_SHADER_STAGE_COMPUTE_BIT)
	{
		if (localSizeIdX >= 0)
		{
			assert(ids[localSizeIdX].opcode == SpvOpConstant);
			shader.localSizeX = ids[localSizeIdX].constant;
		}

		if (localSizeIdY >= 0)
		{
			assert(ids[localSizeIdY].opcode == SpvOpConstant);
			shader.localSizeY = ids[localSizeIdY].constant;
		}

		if (localSizeIdZ >= 0)
		{
			assert(ids[localSizeIdZ].opcode == SpvOpConstant);
			shader.localSizeZ = ids[localSizeIdZ].constant;
		}

		assert(shader.localSizeX && shader.localSizeY && shader.localSizeZ);
	}
}

static uint32_t gatherResources(Shaders shaders, VkDescriptorType(&resourceTypes)[32])
{
	uint32_t resourceMask = 0;

	for (const Shader* shader : shaders)
	{
		for (uint32_t i = 0; i < 32; ++i)
		{
			if (shader->resourceMask & (1 << i))
			{
				if (resourceMask & (1 << i))
				{
					assert(resourceTypes[i] == shader->resourceTypes[i]);
				}
				else
				{
					resourceTypes[i] = shader->resourceTypes[i];
					resourceMask |= 1 << i;
				}
			}
		}
	}

	return resourceMask;
}

bool loadShader(Shader& shader, VkDevice device, const char* path)
{
	FILE* file = fopen(path, "rb");
	if (!file)
	{
		fprintf(stdout, LOGE("Failed to open file: %s\n"), path);
		return false;
	}

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	assert(length >= 0);
	fseek(file, 0, SEEK_SET);

	char* buffer = new char[length];
	assert(buffer);

	size_t rc = fread(buffer, 1, length, file);
	assert(rc == size_t(length));
	fclose(file);

	VkShaderModuleCreateInfo createInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	createInfo.codeSize = length; // note: this needs to be in bytes.
	createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer);

	VkShaderModule shaderModule = 0;
	VK_CHECK(vkCreateShaderModule(device, &createInfo, 0, &shaderModule));

	assert(length % 4 == 0);
	parseShader(shader, reinterpret_cast<const uint32_t*>(buffer), length / 4);
	shader.module = shaderModule;
	
	delete[] buffer;

	return true;
}

void destroyShader(Shader& shader, VkDevice device)
{
	vkDestroyShaderModule(device, shader.module, 0);
}

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device, Shaders shaders)
{
	std::vector<VkDescriptorSetLayoutBinding> setBindings;

	VkDescriptorType resourceTypes[32] = {};
	uint32_t resourceMask = gatherResources(shaders, resourceTypes);

	for (uint32_t i = 0; i < 32; ++i)
	{
		if (resourceMask & (1 << i))
		{
			VkDescriptorSetLayoutBinding binding = {};
			binding.binding = i;
			binding.descriptorType = resourceTypes[i];
			binding.descriptorCount = 1;
			binding.stageFlags = 0;

			for (const auto& shader : shaders)
				if (shader->resourceMask & (1 << i))
					binding.stageFlags |= shader->stage;

			setBindings.emplace_back(binding);
		}
	}

	VkDescriptorSetLayoutCreateInfo setCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	setCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;

	setCreateInfo.bindingCount = uint32_t(setBindings.size());
	setCreateInfo.pBindings = setBindings.data();

	VkDescriptorSetLayout setLayout = 0;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &setCreateInfo, 0, &setLayout));

	return setLayout;
}

static VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout, VkDescriptorSetLayout arrayLayout, VkShaderStageFlags pushConstantStages, size_t pushConstantSize)
{
	VkDescriptorSetLayout layouts[2] = { setLayout, arrayLayout };

	VkPipelineLayoutCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	createInfo.setLayoutCount = arrayLayout ? 2 : 1;
	createInfo.pSetLayouts = layouts;

	VkPushConstantRange pushConstantRange = {};

	if (pushConstantSize)
	{
		pushConstantRange.stageFlags = pushConstantStages;
		pushConstantRange.size = uint32_t(pushConstantSize);

		createInfo.pushConstantRangeCount = 1;
		createInfo.pPushConstantRanges = &pushConstantRange;
	}

	VkPipelineLayout layout = 0;
	VK_CHECK(vkCreatePipelineLayout(device, &createInfo, 0, &layout));

	return layout;
}

static VkDescriptorUpdateTemplate createUpdateTemplate(VkDevice device, VkPipelineBindPoint bindPoint, VkPipelineLayout layout, Shaders shaders)
{
	std::vector<VkDescriptorUpdateTemplateEntry> entries;

	VkDescriptorType resourceTypes[32] = {};
	uint32_t recourceMask = gatherResources(shaders, resourceTypes);

	for (uint32_t i = 0; i < 32; ++i)
	{
		if (recourceMask & (1 << i))
		{
			VkDescriptorUpdateTemplateEntry entry = {};
			entry.dstBinding = i;
			entry.dstArrayElement = 0;
			entry.descriptorCount = 1;
			entry.descriptorType = resourceTypes[i];
			entry.offset = sizeof(DescriptorInfo) * i;
			entry.stride = sizeof(DescriptorInfo);

			entries.emplace_back(entry);
		}
	}

	VkDescriptorUpdateTemplateCreateInfo createInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO };

	createInfo.descriptorUpdateEntryCount = uint32_t(entries.size());
	createInfo.pDescriptorUpdateEntries = entries.data();

	createInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
	createInfo.pipelineBindPoint = bindPoint;
	createInfo.pipelineLayout = layout;

	VkDescriptorUpdateTemplate updateTemplate = 0;
	VK_CHECK(vkCreateDescriptorUpdateTemplate(device, &createInfo, 0, &updateTemplate));

	return updateTemplate;
}

VkDescriptorSetLayout createDescriptorArrayLayout(VkDevice device)
{
	VkShaderStageFlags stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	VkDescriptorSetLayoutBinding setBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DESCRIPTOR_LIMIT, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };

	VkDescriptorBindingFlags bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |  VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
	VkDescriptorSetLayoutBindingFlagsCreateInfo setBindingFlags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
	setBindingFlags.bindingCount = 1;
	setBindingFlags.pBindingFlags = &bindingFlags;

	VkDescriptorSetLayoutCreateInfo setCreateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	setCreateInfo.pNext = &setBindingFlags;
	setCreateInfo.bindingCount = 1;
	setCreateInfo.pBindings = &setBinding;

	VkDescriptorSetLayout setLayout = 0;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &setCreateInfo, 0, &setLayout));

	return setLayout;
}

std::pair<VkDescriptorPool, VkDescriptorSet> createDescriptorArray(VkDevice device, VkDescriptorSetLayout layout, uint32_t descriptorCount)
{
	VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorCount };

	VkDescriptorPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.maxSets = 1;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;

	VkDescriptorPool pool = nullptr;
	VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, 0, &pool));

	VkDescriptorSetVariableDescriptorCountAllocateInfo setAllocateCountInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
	setAllocateCountInfo.descriptorSetCount = 1;
	setAllocateCountInfo.pDescriptorCounts = &descriptorCount;

	VkDescriptorSetAllocateInfo setAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	setAllocateInfo.pNext = &setAllocateCountInfo;
	setAllocateInfo.descriptorPool = pool;
	setAllocateInfo.descriptorSetCount = 1;
	setAllocateInfo.pSetLayouts = &layout;

	VkDescriptorSet set = 0;
	VK_CHECK(vkAllocateDescriptorSets(device, &setAllocateInfo, &set));

	return std::make_pair(pool, set);
}

VkPipeline createGraphicsPipeline(VkDevice device, VkPipelineCache pipelineCache, const VkPipelineRenderingCreateInfo& renderingInfo, Shaders shaders, VkPipelineLayout layout, bool useSpecializationConstants, VkBool32 LATE, VkBool32 TASK, VkBool32 POST)
{
	VkGraphicsPipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

	// TODO: create a specialization constants module to replace code below
	std::vector<VkSpecializationMapEntry> specializationEntries;
	VkSpecializationInfo specializationInfo = {};
	std::vector<VkBool32> data = { LATE, TASK, POST };
	if (useSpecializationConstants)
	{
		specializationEntries.push_back({ 0, 0, sizeof(VkBool32) });
		specializationEntries.push_back({ 1, sizeof(VkBool32), sizeof(VkBool32) });
		specializationEntries.push_back({ 2, 2 * sizeof(VkBool32), sizeof(VkBool32) });


		specializationInfo.mapEntryCount = uint32_t(specializationEntries.size());
		specializationInfo.pMapEntries = specializationEntries.data();
		specializationInfo.dataSize = sizeof(LATE) +sizeof(TASK) + sizeof(POST);
		specializationInfo.pData = data.data();
	}

	std::vector<VkPipelineShaderStageCreateInfo> stages = {};
	for (const auto& shader : shaders)
	{
		VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		stage.stage = shader->stage;
		stage.module = shader->module;
		stage.pSpecializationInfo = &specializationInfo;
		stage.pName = "main";

		stages.emplace_back(stage);
	}

	createInfo.stageCount = uint32_t(stages.size());
	createInfo.pStages = stages.data();

	VkPipelineVertexInputStateCreateInfo vertexInput = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	createInfo.pVertexInputState = &vertexInput;

	// TODO: temporary, legacy FFP IA
	VkVertexInputBindingDescription stream = { 0, 8 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX };
	VkVertexInputAttributeDescription attrs[3] = {};

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	createInfo.pInputAssemblyState = &inputAssembly;

	VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
	createInfo.pViewportState = &viewportState;

	VkPipelineRasterizationStateCreateInfo rasterizationState = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterizationState.lineWidth = 1.f;
	rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	//rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
	rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
	createInfo.pRasterizationState = &rasterizationState;

	VkPipelineMultisampleStateCreateInfo multisampleState = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	createInfo.pMultisampleState = &multisampleState;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencilState.depthTestEnable = VK_TRUE;
	depthStencilState.depthWriteEnable = VK_TRUE;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER; // we are currently use inverse z.
	createInfo.pDepthStencilState = &depthStencilState;

	VkPipelineColorBlendAttachmentState colorAttachmentState = {};
	colorAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
		| VK_COLOR_COMPONENT_G_BIT
		| VK_COLOR_COMPONENT_B_BIT
		| VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlendState = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = &colorAttachmentState;
	createInfo.pColorBlendState = &colorBlendState;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]);
	dynamicState.pDynamicStates = dynamicStates;
	createInfo.pDynamicState = &dynamicState;

	createInfo.layout = layout;
	createInfo.pNext = &renderingInfo;

	VkPipeline pipeline = 0;
	VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &createInfo, 0, &pipeline));

	return pipeline;
}

VkPipeline createComputePipeline(VkDevice device, VkPipelineCache pipelineCache, const Shader& shader, VkPipelineLayout layout, bool useSpecializationConstants, VkBool32 LATE, VkBool32 TASK)
{
	assert(shader.stage == VK_SHADER_STAGE_COMPUTE_BIT);
	VkComputePipelineCreateInfo createInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };

	// TODO: create a specialization constants module to replace code below
	std::vector<VkSpecializationMapEntry> specializationEntries;
	VkSpecializationInfo specializationInfo = {};
	std::vector<VkBool32> data = { LATE, TASK };
	if (useSpecializationConstants)
	{
		specializationEntries.push_back({ 0, 0, sizeof(VkBool32) });
		specializationEntries.push_back({ 1, sizeof(VkBool32), sizeof(VkBool32)});

		specializationInfo.mapEntryCount = uint32_t(specializationEntries.size());
		specializationInfo.pMapEntries = specializationEntries.data();
		specializationInfo.dataSize = sizeof(LATE) + sizeof(TASK);
		specializationInfo.pData = data.data();
	}

	VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	stage.stage = shader.stage;
	stage.module = shader.module;
	stage.pName = "main";
	stage.pSpecializationInfo = &specializationInfo;

	createInfo.stage = stage;
	createInfo.layout = layout;

	VkPipeline pipeline = 0;
	vkCreateComputePipelines(device, pipelineCache, 1, &createInfo, 0, &pipeline);

	return pipeline;
}

Program createProgram(VkDevice device, VkPipelineBindPoint bindPoint, Shaders shaders, size_t pushConstantSize, VkDescriptorSetLayout arrayLayout)
{
	Program program = {};
	for (const auto& shader : shaders)
		if (shader->usePushConstants)
			program.pushConstantStages |= shader->stage;

	bool usesDescriptorArray = false;
	for (const Shader* shader : shaders)
	{
		if (shader->useDescriptorArray)
		{
			usesDescriptorArray = true;
			break;
		}
	}

	assert(!usesDescriptorArray || arrayLayout);

	program.bindPoint = bindPoint;
	program.descriptorSetLayout = createDescriptorSetLayout(device, shaders);
	assert(program.descriptorSetLayout);
	
	program.layout = createPipelineLayout(device, program.descriptorSetLayout, arrayLayout, program.pushConstantStages, pushConstantSize);
	assert(program.layout);

	program.updateTemplate = createUpdateTemplate(device, bindPoint, program.layout, shaders);
	assert(program.updateTemplate);


	return program; 
}

void destroyProgram(VkDevice device, Program& program)
{
	vkDestroyDescriptorUpdateTemplate(device, program.updateTemplate, 0);
	vkDestroyPipelineLayout(device, program.layout, 0);
	vkDestroyDescriptorSetLayout(device, program.descriptorSetLayout, 0);
}
