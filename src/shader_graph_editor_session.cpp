#include "shader_graph_editor_session.h"

ShaderGraphEditorState ShaderGraphEditorSession::State() const
{
	return state_;
}

void ShaderGraphEditorSession::MarkDirty()
{
	state_ = ShaderGraphEditorState::Dirty;
}

void ShaderGraphEditorSession::OnCompileSucceeded()
{
	state_ = ShaderGraphEditorState::CompiledNotApplied;
}

void ShaderGraphEditorSession::OnCompileFailed()
{
	state_ = ShaderGraphEditorState::CompileFailed;
}

void ShaderGraphEditorSession::OnApplied()
{
	state_ = ShaderGraphEditorState::Applied;
}

void ShaderGraphEditorSession::Revert()
{
	state_ = ShaderGraphEditorState::Clean;
}

bool ShaderGraphEditorSession::CanCompile() const
{
	return state_ == ShaderGraphEditorState::Dirty || state_ == ShaderGraphEditorState::CompileFailed
		|| state_ == ShaderGraphEditorState::Applied;
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
