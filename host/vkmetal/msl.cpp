/*
 * SPIR-V to MSL through SPIRV-Cross. descriptor sets become argument buffers padded to the set layouts, so every
 * shader of a layout sees the same slots; push constants go to their own buffer. vertex shaders flip y, so
 * Vulkan viewports are Metal's.
 */
#include "spirv_msl.hpp"

#include <cstring>
#include <exception>

extern "C" {
#include <vulkan/vulkan.h>
#include "mtl.h"
#include "vkm_msl.h"
}

using namespace spirv_cross;

static SPIRType::BaseType base_type(uint8_t kind)
{
    switch (kind) {
    case VKM_MSL_IMAGE: return SPIRType::Image;
    case VKM_MSL_SAMPLER: return SPIRType::Sampler;
    case VKM_MSL_COMBINED: return SPIRType::SampledImage;
    default: return SPIRType::Void;
    }
}

static uint8_t input_kind(SPIRType::BaseType t)
{
    switch (t) {
    case SPIRType::Int:
    case SPIRType::Short:
    case SPIRType::SByte:
    case SPIRType::Int64: return 1;
    case SPIRType::UInt:
    case SPIRType::UShort:
    case SPIRType::UByte:
    case SPIRType::UInt64: return 2;
    default: return 0;
    }
}

static uint32_t constant_value(CompilerMSL &c, const SpecializationConstant &s, uint32_t fallback)
{
    if (!s.id)
        return fallback;
    return c.get_constant(s.id).scalar();
}

extern "C" bool vkm_msl(const uint32_t *spirv, size_t words, const char *entry, int model,
                        const VkmMslBinding *bindings, uint32_t binding_count, uint32_t set_mask,
                        const VkmMslSpec *spec, uint32_t spec_count, VkmMslResult *out)
{
    memset(out, 0, sizeof(*out));
    try {
        CompilerMSL c(spirv, words);
        auto execution = spv::ExecutionModel(model);
        c.set_entry_point(entry, execution);

        CompilerMSL::Options o;
        o.platform = CompilerMSL::Options::macOS;
        o.set_msl_version(3, 2);
        o.argument_buffers = true;
        o.argument_buffers_tier = CompilerMSL::Options::ArgumentBuffersTier::Tier2;
        o.pad_argument_buffer_resources = true;
        o.force_active_argument_buffer_resources = true;
        o.texture_buffer_native = true;
        o.pad_fragment_output_components = true;
        o.buffer_size_buffer_index = VKM_BUFFER_AUX;
        o.enable_decoration_binding = false;
        o.multiview = false;
        c.set_msl_options(o);

        auto common = c.get_common_options();
        common.vertex.flip_vert_y = true;
        c.set_common_options(common);

        for (uint32_t set = 0; set < VKM_SETS; set++)
            if (set_mask & (1u << set))
                c.set_argument_buffer_device_address_space(set, true);
        MSLResourceBinding push;
        push.stage = execution;
        push.basetype = SPIRType::Void;
        push.desc_set = kPushConstDescSet;
        push.binding = kPushConstBinding;
        push.msl_buffer = VKM_BUFFER_PUSH;
        c.add_msl_resource_binding(push);
        for (uint32_t i = 0; i < binding_count; i++) {
            const VkmMslBinding &b = bindings[i];
            MSLResourceBinding r;
            r.stage = execution;
            r.basetype = base_type(b.kind);
            r.desc_set = b.set;
            r.binding = b.binding;
            r.count = b.count;
            r.msl_buffer = r.msl_texture = b.slot;
            r.msl_sampler = b.kind == VKM_MSL_COMBINED ? b.sampler_slot : b.slot;
            c.add_msl_resource_binding(r);
        }

        for (auto &s : c.get_specialization_constants())
            for (uint32_t i = 0; i < spec_count; i++)
                if (spec[i].id == s.constant_id) {
                    auto &k = c.get_constant(s.id);
                    if (spec[i].size == 8)
                        k.m.c[0].r[0].u64 = spec[i].value;
                    else if (spec[i].size == 1 && c.get_type(k.constant_type).basetype == SPIRType::Boolean)
                        k.m.c[0].r[0].u32 = spec[i].value ? 1 : 0;
                    else
                        k.m.c[0].r[0].u32 = (uint32_t)spec[i].value;
                }

        std::string source = c.compile();

        if (execution == spv::ExecutionModelGLCompute) {
            SpecializationConstant x, y, z;
            c.get_work_group_size_specialization_constants(x, y, z);
            out->local.width = constant_value(c, x, c.get_execution_mode_argument(spv::ExecutionModeLocalSize, 0));
            out->local.height = constant_value(c, y, c.get_execution_mode_argument(spv::ExecutionModeLocalSize, 1));
            out->local.depth = constant_value(c, z, c.get_execution_mode_argument(spv::ExecutionModeLocalSize, 2));
        }
        ShaderResources res = c.get_shader_resources();
        if (execution == spv::ExecutionModelVertex) {
            for (auto &in : res.stage_inputs) {
                if (!c.has_decoration(in.id, spv::DecorationLocation))
                    continue;
                uint32_t location = c.get_decoration(in.id, spv::DecorationLocation);
                const SPIRType &t = c.get_type(in.type_id);
                uint32_t slots = t.columns > 1 ? t.columns : 1;
                for (uint32_t k = 0; k < slots && location + k < VKM_VERTEX_ATTRIBUTES; k++) {
                    out->inputs |= 1u << (location + k);
                    out->input[location + k] = {(uint8_t)(location + k), (uint8_t)t.vecsize, input_kind(t.basetype)};
                }
            }
        }
        if (execution == spv::ExecutionModelFragment) {
            for (auto &o : res.stage_outputs)
                if (c.has_decoration(o.id, spv::DecorationLocation))
                    out->outputs |= 1u << c.get_decoration(o.id, spv::DecorationLocation);
            for (auto &b : res.builtin_outputs) {
                if (b.builtin == spv::BuiltInFragDepth)
                    out->writes_depth = true;
                if (b.builtin == spv::BuiltInFragStencilRefEXT)
                    out->writes_stencil = true;
            }
        }
        std::string name = c.get_cleansed_entry_point_name(entry, execution);
        snprintf(out->entry, sizeof(out->entry), "%s", name.c_str());
        out->source = strdup(source.c_str());
        return out->source != nullptr;
    } catch (const std::exception &e) {
        snprintf(out->error, sizeof(out->error), "%s", e.what());
        return false;
    }
}
