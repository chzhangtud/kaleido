#include "shader_graph_editor_session.h"

ShaderGraphEditorState ShaderGraphEditorSession::State() const
{
	return state_;
}

void ShaderGraphEditorSession::MarkDirty()
{
	state_ = ShaderGraphEditorState::Dirty;
	compileEligible_ = true;
}

void ShaderGraphEditorSession::OnCompileSucceeded()
{
	state_ = ShaderGraphEditorState::CompiledNotApplied;
	compileEligible_ = true;
}

void ShaderGraphEditorSession::OnCompileFailed()
{
	state_ = ShaderGraphEditorState::CompileFailed;
	compileEligible_ = true;
}

void ShaderGraphEditorSession::OnApplied()
{
	state_ = ShaderGraphEditorState::Applied;
	compileEligible_ = true;
}

void ShaderGraphEditorSession::Revert()
{
	state_ = ShaderGraphEditorState::Clean;
}

void ShaderGraphEditorSession::SetCompileEligible(bool eligible)
{
	compileEligible_ = eligible;
}

bool ShaderGraphEditorSession::CanCompile() const
{
	return compileEligible_;
}

bool ShaderGraphEditorSession::CanApply() const
{
	return state_ == ShaderGraphEditorState::CompiledNotApplied;
}

bool ShaderGraphEditorSession::CanRevert() const
{
	return state_ == ShaderGraphEditorState::Dirty || state_ == ShaderGraphEditorState::CompileFailed ||
	       state_ == ShaderGraphEditorState::CompiledNotApplied;
}

bool ShaderGraphEditorSession::AddNodeByDescriptor(const std::string& descriptorId)
{
	if (descriptorId.empty())
		return false;
	ShaderGraphNodeInstance instance{};
	instance.id = static_cast<int>(graph_.nodeInstances.size()) + 1;
	instance.descriptorId = descriptorId;
	instance.descriptorVersion = 1;
	graph_.version = 3;
	graph_.nodeInstances.push_back(std::move(instance));
	MarkDirty();
	return true;
}

const ShaderGraphAsset& ShaderGraphEditorSession::GetGraph() const
{
	return graph_;
}
