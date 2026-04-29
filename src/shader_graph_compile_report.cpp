#include "shader_graph_compile_report.h"

void ShaderGraphCompileReport::Clear()
{
	messages_.clear();
}

void ShaderGraphCompileReport::Add(
    SGCompileMessageSeverity severity, int nodeId, SGCompileMessagePhase phase, const std::string& text)
{
	messages_.push_back(SGCompileMessage{ severity, nodeId, phase, text });
}

std::vector<SGCompileMessage> ShaderGraphCompileReport::ForNode(int nodeId) const
{
	std::vector<SGCompileMessage> result;
	for (const SGCompileMessage& message : messages_)
	{
		if (message.nodeId == nodeId)
			result.push_back(message);
	}
	return result;
}

const SGCompileMessage* ShaderGraphCompileReport::FirstError() const
{
	for (const SGCompileMessage& message : messages_)
	{
		if (message.severity == SGCompileMessageSeverity::Error)
			return &message;
	}
	return nullptr;
}

bool ShaderGraphCompileReport::HasErrors() const
{
	return FirstError() != nullptr;
}

const std::vector<SGCompileMessage>& ShaderGraphCompileReport::Messages() const
{
	return messages_;
}
