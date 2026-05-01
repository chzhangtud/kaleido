#include "shader_graph_codegen_glsl.h"

#include "shader_graph_validate.h"

#include <sstream>
#include <unordered_map>

namespace
{
using PortExprMap = std::unordered_map<int, std::unordered_map<int, std::string>>;
using PortTypeMap = std::unordered_map<int, std::unordered_map<int, SGPortType>>;
using InputEdgeMap = std::unordered_map<int, std::unordered_map<int, const SGEdge*>>;

std::string NodeVarName(int nodeId, int port)
{
	return "sg_n" + std::to_string(nodeId) + "_p" + std::to_string(port);
}

const char* GlslTypeName(SGPortType t)
{
	switch (t)
	{
	case SGPortType::PortBool: return "bool";
	case SGPortType::PortInt: return "int";
	case SGPortType::PortFloat: return "float";
	case SGPortType::PortVec2: return "vec2";
	case SGPortType::PortVec3: return "vec3";
	case SGPortType::PortVec4: return "vec4";
	default: return "float";
	}
}

std::string ConvertExpr(const std::string& expr, SGPortType from, SGPortType to)
{
	if (from == to)
		return expr;
	if (!SGPortTypeCanImplicitConvert(from, to))
		return expr;
	switch (to)
	{
	case SGPortType::PortInt:
		if (from == SGPortType::PortFloat)
			return "int(" + expr + ")";
		if (from == SGPortType::PortVec2 || from == SGPortType::PortVec3 || from == SGPortType::PortVec4)
			return "int((" + expr + ").x)";
		return expr;
	case SGPortType::PortFloat:
		if (from == SGPortType::PortInt)
			return "float(" + expr + ")";
		if (from == SGPortType::PortVec2 || from == SGPortType::PortVec3 || from == SGPortType::PortVec4)
			return "float((" + expr + ").x)";
		return expr;
	case SGPortType::PortVec2:
		if (from == SGPortType::PortInt || from == SGPortType::PortFloat)
			return "vec2(" + expr + ")";
		if (from == SGPortType::PortVec3 || from == SGPortType::PortVec4)
			return "(" + expr + ").xy";
		return expr;
	case SGPortType::PortVec3:
		if (from == SGPortType::PortInt || from == SGPortType::PortFloat)
			return "vec3(" + expr + ")";
		if (from == SGPortType::PortVec2)
			return "vec3((" + expr + "), 0.0)";
		if (from == SGPortType::PortVec4)
			return "(" + expr + ").xyz";
		return expr;
	case SGPortType::PortVec4:
		if (from == SGPortType::PortInt || from == SGPortType::PortFloat)
			return "vec4(" + expr + ")";
		if (from == SGPortType::PortVec2)
			return "vec4((" + expr + "), 0.0, 1.0)";
		if (from == SGPortType::PortVec3)
			return "vec4((" + expr + "), 1.0)";
		return expr;
	default:
		return expr;
	}
}

std::string MakeLiteral(float value)
{
	std::ostringstream oss;
	oss.setf(std::ios::fixed);
	oss.precision(6);
	oss << value;
	return oss.str();
}

float NodeValueOr(const SGNode& node, size_t index, float fallback)
{
	if (index < node.values.size())
		return node.values[index];
	return fallback;
}
} // namespace

SGCodegenResult GenerateShaderGraphGlsl(const ShaderGraphAsset& graph)
{
	SGCodegenResult result{};
	const SGValidateResult v = ValidateShaderGraph(graph);
	if (!v.ok)
	{
		result.error = v.error;
		return result;
	}

	std::unordered_map<int, const SGNode*> nodeById;
	nodeById.reserve(graph.nodes.size());
	for (const SGNode& n : graph.nodes)
		nodeById[n.id] = &n;

	InputEdgeMap inputs;
	for (const SGEdge& e : graph.edges)
		inputs[e.toNode][e.toPort] = &e;

	auto getInputExpr = [&](int nodeId, int port, const PortExprMap& exprs, const PortTypeMap& types, SGPortType requiredType) -> std::string
	{
		auto nodeIt = inputs.find(nodeId);
		if (nodeIt == inputs.end())
			return "";
		auto portIt = nodeIt->second.find(port);
		if (portIt == nodeIt->second.end())
			return "";
		const SGEdge* e = portIt->second;
		auto fromNodeIt = exprs.find(e->fromNode);
		if (fromNodeIt == exprs.end())
			return "";
		auto fromPortIt = fromNodeIt->second.find(e->fromPort);
		if (fromPortIt == fromNodeIt->second.end())
			return "";
		SGPortType fromType = SGPortType::PortFloat;
		auto typeNodeIt = types.find(e->fromNode);
		if (typeNodeIt != types.end())
		{
			auto typePortIt = typeNodeIt->second.find(e->fromPort);
			if (typePortIt != typeNodeIt->second.end())
				fromType = typePortIt->second;
		}
		return ConvertExpr(fromPortIt->second, fromType, requiredType);
	};

	auto getInputType = [&](int nodeId, int port, const PortTypeMap& types, SGPortType& out) -> bool
	{
		auto nodeIt = inputs.find(nodeId);
		if (nodeIt == inputs.end())
			return false;
		auto portIt = nodeIt->second.find(port);
		if (portIt == nodeIt->second.end())
			return false;
		const SGEdge* e = portIt->second;
		auto fromNodeIt = types.find(e->fromNode);
		if (fromNodeIt == types.end())
			return false;
		auto fromPortIt = fromNodeIt->second.find(e->fromPort);
		if (fromPortIt == fromNodeIt->second.end())
			return false;
		out = fromPortIt->second;
		return true;
	};

	std::ostringstream code;
	code << "struct SGParams { float p0; float p1; float p2; float p3; };\n";
	code << "float sg_noise_perlin3d(vec3 p) {\n";
	code << "    return fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453) * 2.0 - 1.0;\n";
	code << "}\n\n";
	code << "vec3 sg_eval_base_color(vec2 uv, vec3 wpos, vec3 nrm, float timeSec, SGParams params)\n";
	code << "{\n";

	PortExprMap exprs;
	PortTypeMap types;
	std::string outputExpr = "vec3(1.0)";

	for (int nodeId : v.topoOrder)
	{
		const SGNode* node = nodeById[nodeId];
		if (!node)
			continue;

		switch (node->op)
		{
		case SGNodeOp::InputUV:
			exprs[nodeId][0] = "uv.x";
			exprs[nodeId][1] = "uv.y";
			exprs[nodeId][2] = "uv";
			types[nodeId][0] = SGPortType::PortFloat;
			types[nodeId][1] = SGPortType::PortFloat;
			types[nodeId][2] = SGPortType::PortVec2;
			break;
		case SGNodeOp::InputTime:
		{
			const float cycle = NodeValueOr(*node, 0, 0.0f);
			const std::string timeExpr = "params.p2";
			if (cycle > 0.0001f)
			{
				const std::string out = NodeVarName(nodeId, 0);
				code << "    float " << out << " = fract(" << timeExpr << " / " << MakeLiteral(cycle) << ");\n";
				exprs[nodeId][0] = out;
			}
			else
			{
				exprs[nodeId][0] = timeExpr;
			}
			types[nodeId][0] = SGPortType::PortFloat;
			break;
		}
		case SGNodeOp::InputWorldPos:
			exprs[nodeId][0] = "wpos";
			types[nodeId][0] = SGPortType::PortVec3;
			break;
		case SGNodeOp::InputNormal:
			exprs[nodeId][0] = "nrm";
			types[nodeId][0] = SGPortType::PortVec3;
			break;
		case SGNodeOp::ConstFloat:
			exprs[nodeId][0] = MakeLiteral(NodeValueOr(*node, 0, 0.0f));
			types[nodeId][0] = SGPortType::PortFloat;
			break;
		case SGNodeOp::ConstVec2:
		{
			const float x = NodeValueOr(*node, 0, 0.0f);
			const float y = NodeValueOr(*node, 1, 0.0f);
			exprs[nodeId][0] = "vec2(" + MakeLiteral(x) + ", " + MakeLiteral(y) + ")";
			types[nodeId][0] = SGPortType::PortVec2;
			break;
		}
		case SGNodeOp::ConstVec3:
		{
			const float x = NodeValueOr(*node, 0, 0.0f);
			const float y = NodeValueOr(*node, 1, 0.0f);
			const float z = NodeValueOr(*node, 2, 0.0f);
			exprs[nodeId][0] = "vec3(" + MakeLiteral(x) + ", " + MakeLiteral(y) + ", " + MakeLiteral(z) + ")";
			types[nodeId][0] = SGPortType::PortVec3;
			break;
		}
		case SGNodeOp::ParamFloat:
		{
			int idx = int(NodeValueOr(*node, 0, 0.0f));
			if (idx < 0)
				idx = 0;
			if (idx > 3)
				idx = 3;
			exprs[nodeId][0] = "params.p" + std::to_string(idx);
			types[nodeId][0] = SGPortType::PortFloat;
			break;
		}
		case SGNodeOp::Add:
		case SGNodeOp::Sub:
		case SGNodeOp::Mul:
		case SGNodeOp::Div:
		{
			SGPortType ta = SGPortType::PortFloat;
			SGPortType tb = SGPortType::PortFloat;
			if (!getInputType(nodeId, 0, types, ta) || !getInputType(nodeId, 1, types, tb))
				continue;
			SGPortType outType = ta;
			if (SGPortTypeCanImplicitConvert(tb, ta))
				outType = ta;
			else if (SGPortTypeCanImplicitConvert(ta, tb))
				outType = tb;
			else
				continue;
			const std::string a = getInputExpr(nodeId, 0, exprs, types, outType);
			const std::string b = getInputExpr(nodeId, 1, exprs, types, outType);
			const std::string out = NodeVarName(nodeId, 0);
			const char* op = (node->op == SGNodeOp::Add) ? "+" : (node->op == SGNodeOp::Sub) ? "-" : (node->op == SGNodeOp::Mul) ? "*" : "/";
			code << "    " << GlslTypeName(outType) << " " << out << " = (" << a << ") " << op << " (" << b << ");\n";
			exprs[nodeId][0] = out;
			types[nodeId][0] = outType;
			break;
		}
		case SGNodeOp::Sin:
		case SGNodeOp::Cos:
		case SGNodeOp::Frac:
		case SGNodeOp::Saturate:
		{
			SGPortType inType = SGPortType::PortFloat;
			if (!getInputType(nodeId, 0, types, inType))
				continue;
			const std::string in = getInputExpr(nodeId, 0, exprs, types, inType);
			const std::string out = NodeVarName(nodeId, 0);
			const char* fn = (node->op == SGNodeOp::Sin) ? "sin" : (node->op == SGNodeOp::Cos) ? "cos" : (node->op == SGNodeOp::Frac) ? "fract" : "clamp";
			if (node->op == SGNodeOp::Saturate)
				code << "    " << GlslTypeName(inType) << " " << out << " = clamp(" << in << ", 0.0, 1.0);\n";
			else
				code << "    " << GlslTypeName(inType) << " " << out << " = " << fn << "(" << in << ");\n";
			exprs[nodeId][0] = out;
			types[nodeId][0] = inType;
			break;
		}
		case SGNodeOp::Lerp:
		{
			SGPortType ta = SGPortType::PortFloat;
			SGPortType tb = SGPortType::PortFloat;
			if (!getInputType(nodeId, 0, types, ta) || !getInputType(nodeId, 1, types, tb))
				continue;
			SGPortType outType = SGPortTypeCanImplicitConvert(tb, ta) ? ta : tb;
			if (!SGPortTypeCanImplicitConvert(ta, outType) || !SGPortTypeCanImplicitConvert(tb, outType))
				continue;
			const std::string a = getInputExpr(nodeId, 0, exprs, types, outType);
			const std::string b = getInputExpr(nodeId, 1, exprs, types, outType);
			const std::string t = getInputExpr(nodeId, 2, exprs, types, SGPortType::PortFloat);
			const std::string out = NodeVarName(nodeId, 0);
			code << "    " << GlslTypeName(outType) << " " << out << " = mix(" << a << ", " << b << ", " << t << ");\n";
			exprs[nodeId][0] = out;
			types[nodeId][0] = outType;
			break;
		}
		case SGNodeOp::ComposeVec3:
		{
			const std::string x = getInputExpr(nodeId, 0, exprs, types, SGPortType::PortFloat);
			const std::string y = getInputExpr(nodeId, 1, exprs, types, SGPortType::PortFloat);
			const std::string z = getInputExpr(nodeId, 2, exprs, types, SGPortType::PortFloat);
			const std::string out = NodeVarName(nodeId, 0);
			code << "    vec3 " << out << " = vec3(" << x << ", " << y << ", " << z << ");\n";
			exprs[nodeId][0] = out;
			types[nodeId][0] = SGPortType::PortVec3;
			break;
		}
		case SGNodeOp::SplitVec3X:
		case SGNodeOp::SplitVec3Y:
		case SGNodeOp::SplitVec3Z:
		{
			const std::string v3 = getInputExpr(nodeId, 0, exprs, types, SGPortType::PortVec3);
			const char comp = (node->op == SGNodeOp::SplitVec3X) ? 'x' : (node->op == SGNodeOp::SplitVec3Y) ? 'y' : 'z';
			const std::string out = NodeVarName(nodeId, 0);
			code << "    float " << out << " = (" << v3 << ")." << comp << ";\n";
			exprs[nodeId][0] = out;
			types[nodeId][0] = SGPortType::PortFloat;
			break;
		}
		case SGNodeOp::NoisePerlin3D:
		{
			const std::string p = getInputExpr(nodeId, 0, exprs, types, SGPortType::PortVec3);
			const std::string out = NodeVarName(nodeId, 0);
			code << "    float " << out << " = sg_noise_perlin3d(" << p << ");\n";
			exprs[nodeId][0] = out;
			types[nodeId][0] = SGPortType::PortFloat;
			break;
		}
		case SGNodeOp::Remap:
		{
			const std::string value = getInputExpr(nodeId, 0, exprs, types, SGPortType::PortFloat);
			const std::string inMin = getInputExpr(nodeId, 1, exprs, types, SGPortType::PortFloat).empty()
			                              ? MakeLiteral(NodeValueOr(*node, 0, -1.0f))
			                              : getInputExpr(nodeId, 1, exprs, types, SGPortType::PortFloat);
			const std::string inMax = getInputExpr(nodeId, 2, exprs, types, SGPortType::PortFloat).empty()
			                              ? MakeLiteral(NodeValueOr(*node, 1, 1.0f))
			                              : getInputExpr(nodeId, 2, exprs, types, SGPortType::PortFloat);
			const std::string outMin = getInputExpr(nodeId, 3, exprs, types, SGPortType::PortFloat).empty()
			                               ? MakeLiteral(NodeValueOr(*node, 2, 0.0f))
			                               : getInputExpr(nodeId, 3, exprs, types, SGPortType::PortFloat);
			const std::string outMax = getInputExpr(nodeId, 4, exprs, types, SGPortType::PortFloat).empty()
			                               ? MakeLiteral(NodeValueOr(*node, 3, 1.0f))
			                               : getInputExpr(nodeId, 4, exprs, types, SGPortType::PortFloat);
			const std::string out = NodeVarName(nodeId, 0);
			code << "    float " << out << " = (" << outMin << ") + ((" << value << " - (" << inMin << ")) / max((" << inMax << ") - (" << inMin << "), 1e-6)) * ((" << outMax << ") - (" << outMin << "));\n";
			exprs[nodeId][0] = out;
			types[nodeId][0] = SGPortType::PortFloat;
			break;
		}
		case SGNodeOp::OutputSurface:
		{
			const std::string input = getInputExpr(nodeId, 0, exprs, types, SGPortType::PortVec3);
			if (!input.empty())
				outputExpr = input;
			break;
		}
		default:
			break;
		}
	}

	code << "    return " << outputExpr << ";\n";
	code << "}\n";

	result.ok = true;
	result.fragmentFunction = code.str();
	return result;
}
