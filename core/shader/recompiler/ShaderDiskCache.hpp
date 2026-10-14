#ifndef CORE_SHADER_RECOMPILER_SHADERDISKCACHE_HPP
#define CORE_SHADER_RECOMPILER_SHADERDISKCACHE_HPP

#include "CompiledVariant.hpp"
#include "IntermediateRepresentation/IrMetadata/ResourcePlan.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ShaderRecompiler::ShaderDiskCache {

inline constexpr std::uint32_t FormatVersion = 31;
inline constexpr std::size_t EntryHeaderBytes = 48;
