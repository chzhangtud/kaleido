#pragma once

#include <string>
#include <vector>

enum class SGCompileMessageSeverity
{
	Info = 0,
	Warning,
	Error,
};

enum class SGCompileMessagePhase
{
	Validate = 0,
	Codegen,
	Compile,
};

struct SGCompileMessage
{
	SGCompileMessageSeverity severity = SGCompileMessageSeverity::Info;
	int nodeId = -1;
	SGCompileMessagePhase phase = SGCompileMessagePhase::Validate;
	std::string text;
};

class ShaderGraphCompileReport
{
public:
	void Clear();
	void Add(SGCompileMessageSeverity severity, int nodeId, SGCompileMessagePhase phase, const std::string& text);
	std::vector<SGCompileMessage> ForNode(int nodeId) const;
	const SGCompileMessage* FirstError() const;
	bool HasErrors() const;
	const std::vector<SGCompileMessage>& Messages() const;

private:
	std::vector<SGCompileMessage> messages_;
};
