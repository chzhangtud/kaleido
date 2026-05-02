#pragma once

#include "shader_graph_types.h"

enum class ShaderGraphEditorState
{
	Clean = 0,
	Dirty,
	CompiledNotApplied,
	Applied,
	CompileFailed,
};

class ShaderGraphEditorSession
{
public:
	ShaderGraphEditorState State() const;
	void MarkDirty();
	void OnCompileSucceeded();
	void OnCompileFailed();
	void OnApplied();
	void Revert();
	void SetCompileEligible(bool eligible);

	bool CanCompile() const;
	bool CanApply() const;
	bool CanRevert() const;
	bool AddNodeByDescriptor(const std::string& descriptorId);
	const ShaderGraphAsset& GetGraph() const;

private:
	ShaderGraphAsset graph_{};
	ShaderGraphEditorState state_ = ShaderGraphEditorState::Clean;
	bool compileEligible_ = false;
};
