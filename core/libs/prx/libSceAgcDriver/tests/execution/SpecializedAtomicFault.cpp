#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/PipelineSpecialization.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "IntermediateRepresentation/IrMetadata/ShaderStage.hpp"
#include "BdaAbi.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace AgcDriver {
struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};
}

namespace {
using namespace AgcDriver::Graphics;
namespace Abi = ShaderRecompiler::BdaAbi;
constexpr std::uint32_t Lanes = 32;
constexpr std::uint32_t PayloadBytes = Lanes * 8;
constexpr std::uint64_t Guest = 0x123456780000ull;
constexpr std::uint32_t Sentinel = 0xa5a5a5a5u;

class Dispatch {
public:
    Dispatch(const Context& context, const ShaderRecompiler::RecompileResult& shader, Buffer& data, VkDeviceSize offset, Buffer& fault) : context(context), push(shader.pushConstants) {
        try {
            Require(shader.bindings.size() == 2, "specialized atomic must bind only its buffer and fault record");
            std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
            for (std::size_t i = 0; i < shader.bindings.size(); ++i) {
                const auto& binding = shader.bindings[i];
                Require(binding.descriptorSet == 0 && binding.count == 1 && binding.kind == ShaderRecompiler::DescriptorKind::StorageBuffer, "unexpected specialized atomic descriptor layout");
                Require(binding.role == ShaderRecompiler::DescriptorRole::GuestBuffers || binding.role == ShaderRecompiler::DescriptorRole::FaultBuffer, "specialized atomic unexpectedly binds an address table");
                bindings[i] = {binding.binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            }
            VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            setInfo.bindingCount = bindings.size(); setInfo.pBindings = bindings.data();
            Check(context.Function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(context.device, &setInfo, nullptr, &setLayout), "vkCreateDescriptorSetLayout");
            const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
            VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets = 1; poolInfo.poolSizeCount = 1; poolInfo.pPoolSizes = &size;
            Check(context.Function<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(context.device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
            VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, pool, 1, &setLayout};
            Check(context.Function<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(context.device, &allocation, &set), "vkAllocateDescriptorSets");
            std::array<VkDescriptorBufferInfo, 2> buffers{};
            for (std::size_t i = 0; i < shader.bindings.size(); ++i) {
                buffers[i] = shader.bindings[i].role == ShaderRecompiler::DescriptorRole::GuestBuffers
                    ? VkDescriptorBufferInfo{data.Handle(), offset, PayloadBytes}
                    : VkDescriptorBufferInfo{fault.Handle(), 0, Abi::FaultBufferBytes};
            }
            for (std::uint32_t i = 0; i < buffers.size(); ++i) {
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = set; write.dstBinding = shader.bindings[i].binding; write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo = &buffers[i];
                context.Function<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(context.device, 1, &write, 0, nullptr);
            }
            const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, 128};
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1; layoutInfo.pSetLayouts = &setLayout;
            layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &range;
            Check(context.Function<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(context.device, &layoutInfo, nullptr, &layout), "vkCreatePipelineLayout");
            VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            moduleInfo.codeSize = shader.spirv.size() * 4; moduleInfo.pCode = shader.spirv.data();
            Check(context.Function<PFN_vkCreateShaderModule>("vkCreateShaderModule")(context.device, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
            const PipelineSpecialization specialization(shader);
            VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", specialization.Info()};
            pipelineInfo.layout = layout;
            Check(context.Function<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateComputePipelines");
        } catch (...) { Release(); throw; }
    }
    ~Dispatch() { Release(); }
    void Run() {
        CommandBatch batch(context);
        const auto commands = batch.Handle();
        RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        context.Function<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        context.Function<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        if (!push.empty()) context.Function<PFN_vkCmdPushConstants>("vkCmdPushConstants")(commands, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push.size(), push.data());
        context.Function<PFN_vkCmdDispatch>("vkCmdDispatch")(commands, 1, 1, 1);
        RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
        batch.SubmitAndWait();
    }
private:
    void Release() noexcept {
        if (pipeline) context.Function<PFN_vkDestroyPipeline>("vkDestroyPipeline")(context.device, pipeline, nullptr);
        if (module) context.Function<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(context.device, module, nullptr);
        if (layout) context.Function<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(context.device, layout, nullptr);
        if (pool) context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(context.device, pool, nullptr);
        if (setLayout) context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(context.device, setLayout, nullptr);
    }
    Context context;
    std::vector<std::byte> push;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderModule module = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

void Run(AgcDriver::VulkanDevice& device, bool unaligned) {
    const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(device);
    const auto guard = std::max<VkDeviceSize>(16, context.limits.minStorageBufferOffsetAlignment);
    Buffer data(context, PayloadBytes + 2 * guard, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer fault(context, Abi::FaultBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    std::vector<std::uint32_t> expected(data.Bytes().size() / 4, Sentinel);
    std::fill_n(expected.begin() + guard / 4, Lanes, 0u);
    std::memcpy(data.Bytes().data(), expected.data(), data.Bytes().size());
    std::memset(fault.Bytes().data(), 0, fault.Bytes().size());
    const std::array<std::uint32_t, 10> code{
        0x34060082u, unaligned ? 0x4a060681u : 0x4a060680u, 0x7e500290u,
        0xe0f05000u, 0x80012803u, 0xbf8c3f70u, 0x7e5002aau,
        0xe0701080u, 0x80012803u, 0xbf810000u};
    const std::array<std::uint32_t, 8> userData{0, 0, 0, 0,
        static_cast<std::uint32_t>(Guest), static_cast<std::uint32_t>(Guest >> 32), PayloadBytes, 0x31016facu};
    ShaderRecompiler::RecompileRequest request{};
    request.shader = {ShaderRecompiler::ShaderStage::Compute, 0x58800u, code, 0, {}};
    request.context.waveSize = Lanes; request.context.userData = userData;
    request.context.compute = ShaderRecompiler::ShaderComputeStageInfo{{Lanes, 1, 1}, 0, {false, false, false}, false, 1};
    request.target = device.Target(); request.layout = {0, 0, 0, 128}; request.useCache = false;
    const auto shader = ShaderRecompiler::Recompile(request);
    Dispatch(context, shader, data, guard, fault).Run();
    data.Invalidate(); fault.Invalidate();
    Abi::Fault report{};
    std::memcpy(&report, fault.Bytes().data(), sizeof(report));
    if (unaligned) {
        Require(report.state == Abi::FaultState::Ready && report.reason == Abi::FaultReason::Unaligned, "specialized atomic did not publish its GPU alignment fault");
        Require(report.address >= Guest + 1 && report.address < Guest + Lanes * 4 && (report.address - Guest) % 4 == 1, "specialized atomic fault lost its 64-bit dynamic address");
        Require(report.bytes == 4 && report.instruction == 12 && report.stage == static_cast<std::uint32_t>(ShaderRecompiler::IrShaderStage::Compute) && report.reserved == 0, "specialized atomic fault has wrong width, pc or stage");
    } else {
        const Abi::Fault empty{};
        Require(std::memcmp(&report, &empty, sizeof(report)) == 0, "aligned specialized atomic unexpectedly faulted");
        std::fill_n(expected.begin() + guard / 4, Lanes, 1u);
        std::fill_n(expected.begin() + guard / 4 + Lanes, Lanes, 42u);
    }
    Require(std::memcmp(data.Bytes().data(), expected.data(), data.Bytes().size()) == 0, unaligned ? "faulted atomic continued to mutate its buffer or execute a later store" : "aligned atomic/store or buffer guards are incorrect");
}
}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Run(*device, false);
        Run(*device, true);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
    std::cout << "Specialized atomic GPU fault and termination passed\n";
}
