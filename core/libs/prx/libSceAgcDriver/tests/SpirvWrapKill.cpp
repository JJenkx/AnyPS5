#include "source/opt/build_module.h"
#include "source/opt/wrap_opkill.h"
#include <spirv-tools/libspirv.hpp>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string Module(const std::string& terminator, bool continueCall) {
    std::string text = "OpCapability Shader\n";
    if (terminator == "OpTerminateInvocation") text += "OpExtension \"SPV_KHR_terminate_invocation\"\n";
    text += R"(OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
%void = OpTypeVoid
%bool = OpTypeBool
%true = OpConstantTrue %bool
%fn = OpTypeFunction %void
%main = OpFunction %void None %fn
%entry = OpLabel
)";
    if (continueCall) {
        text += R"(OpBranch %loop
%loop = OpLabel
OpLoopMerge %merge %continue None
OpBranchConditional %true %continue %merge
%continue = OpLabel
%call = OpFunctionCall %void %helper
OpBranch %loop
%merge = OpLabel
OpReturn
OpFunctionEnd
%helper = OpFunction %void None %fn
%helperEntry = OpLabel
)";
    }
    return text + (terminator.empty() ? "OpReturn\n" : terminator + "\n") + "OpFunctionEnd\n";
}

void Check(const std::string& terminator, bool continueCall) {
    const auto text = Module(terminator, continueCall);
    auto context = spvtools::BuildModule(SPV_ENV_UNIVERSAL_1_5, {}, text);
    Require(context != nullptr, "test module did not assemble");
    std::vector<std::uint32_t> before;
    context->module()->ToBinary(&before, false);
    spvtools::opt::WrapOpKill pass;
    const auto status = pass.Run(context.get());
    const bool modified = continueCall && !terminator.empty();
    Require(status == (modified ? spvtools::opt::Pass::Status::SuccessWithChange : spvtools::opt::Pass::Status::SuccessWithoutChange), "unexpected wrapping status");
    if (terminator.empty()) {
        Require(!context->AreAnalysesValid(spvtools::opt::IRContext::kAnalysisStructuredCFG), "terminator-free module built structured CFG analysis");
    }
    std::vector<std::uint32_t> after;
    context->module()->ToBinary(&after, false);
    Require(modified || before == after, "no-op pass changed the module");
    Require(!modified || before != after, "terminator in a continue helper was not wrapped");
    spvtools::SpirvTools validator(SPV_ENV_UNIVERSAL_1_5);
    validator.SetMessageConsumer([](spv_message_level_t, const char*, const spv_position_t&, const char* message) {
        std::fprintf(stderr, "%s\n", message);
    });
    Require(validator.Validate(after), "wrapped module is invalid");
}

}

int main() {
    try {
        for (const auto& terminator : {std::string{}, std::string{"OpKill"}, std::string{"OpTerminateInvocation"}}) {
            Check(terminator, false);
            Check(terminator, true);
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
