#pragma once

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

	bool CanCompile() const;
	bool CanApply() const;
	bool CanRevert() const;

private:
	ShaderGraphEditorState state_ = ShaderGraphEditorState::Clean;
};
