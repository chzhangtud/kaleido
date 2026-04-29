#include "../src/shader_graph_compile_report.h"
#include "../src/shader_graph_editor_session.h"

#include <cassert>

static void TestSessionTransitionsSuccessPath()
{
	ShaderGraphEditorSession session{};
	assert(session.State() == ShaderGraphEditorState::Clean);
	session.MarkDirty();
	assert(session.State() == ShaderGraphEditorState::Dirty);
	session.OnCompileSucceeded();
	assert(session.State() == ShaderGraphEditorState::CompiledNotApplied);
	session.OnApplied();
	assert(session.State() == ShaderGraphEditorState::Applied);
}

static void TestSessionTransitionsFailureAndRevert()
{
	ShaderGraphEditorSession session{};
	session.MarkDirty();
	session.OnCompileFailed();
	assert(session.State() == ShaderGraphEditorState::CompileFailed);
	session.Revert();
	assert(session.State() == ShaderGraphEditorState::Clean);
}

static void TestCompileReportMessageQueries()
{
	ShaderGraphCompileReport report{};
	report.Add(SGCompileMessageSeverity::Info, 10, SGCompileMessagePhase::Validate, "validate ok");
	report.Add(SGCompileMessageSeverity::Warning, 20, SGCompileMessagePhase::Codegen, "unused value");
	report.Add(SGCompileMessageSeverity::Error, 30, SGCompileMessagePhase::Compile, "type mismatch");

	const std::vector<SGCompileMessage> node20 = report.ForNode(20);
	assert(node20.size() == 1u);
	assert(node20[0].severity == SGCompileMessageSeverity::Warning);

	const SGCompileMessage* firstError = report.FirstError();
	assert(firstError != nullptr);
	assert(firstError->nodeId == 30);
	assert(report.HasErrors());
}

int main()
{
	TestSessionTransitionsSuccessPath();
	TestSessionTransitionsFailureAndRevert();
	TestCompileReportMessageQueries();
	return 0;
}
