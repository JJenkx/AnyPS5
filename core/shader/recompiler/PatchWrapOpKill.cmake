get_target_property(spirvWrapSourceDir SPIRV-Tools-opt SOURCE_DIR)
set(spirvWrapSource "${spirvWrapSourceDir}/wrap_opkill.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${spirvWrapSource}")
file(READ "${spirvWrapSource}" spirvWrapContent)
set(spirvWrapOriginal [=[Pass::Status WrapOpKill::Process() {
  bool modified = false;]=])
set(spirvWrapReplacement [=[Pass::Status WrapOpKill::Process() {
  bool needs_wrapping = false;
  for (Function& function : *context()->module()) {
    if (!function.WhileEachInst([](Instruction* instruction) {
          return instruction->opcode() != spv::Op::OpKill &&
                 instruction->opcode() != spv::Op::OpTerminateInvocation;
        })) {
      needs_wrapping = true;
      break;
    }
  }
  if (!needs_wrapping) return Status::SuccessWithoutChange;
  bool modified = false;]=])
string(FIND "${spirvWrapContent}" "${spirvWrapOriginal}" spirvWrapPosition)
if(spirvWrapPosition EQUAL -1)
    message(FATAL_ERROR "SPIRV-Tools WrapOpKill changed; review its no-op scan")
endif()
string(REPLACE "${spirvWrapOriginal}" "${spirvWrapReplacement}" spirvWrapContent "${spirvWrapContent}")
string(REPLACE "#include \"ir_builder.h\"" "#include \"source/opt/ir_builder.h\"" spirvWrapContent "${spirvWrapContent}")
set(spirvWrapPatched "${CMAKE_BINARY_DIR}/3rdparty/SPIRV-Tools/patched/wrap_opkill.cpp")
file(CONFIGURE OUTPUT "${spirvWrapPatched}" CONTENT "${spirvWrapContent}" @ONLY)
get_target_property(spirvWrapSources SPIRV-Tools-opt SOURCES)
set(spirvWrapReplacedSources)
set(spirvWrapReplacements 0)
foreach(source IN LISTS spirvWrapSources)
    get_filename_component(sourcePath "${source}" ABSOLUTE BASE_DIR "${spirvWrapSourceDir}")
    if(sourcePath STREQUAL spirvWrapSource)
        list(APPEND spirvWrapReplacedSources "${spirvWrapPatched}")
        math(EXPR spirvWrapReplacements "${spirvWrapReplacements} + 1")
    else()
        list(APPEND spirvWrapReplacedSources "${source}")
    endif()
endforeach()
if(NOT spirvWrapReplacements EQUAL 1)
    message(FATAL_ERROR "SPIRV-Tools WrapOpKill source list changed; review its no-op scan")
endif()
set_property(TARGET SPIRV-Tools-opt PROPERTY SOURCES "${spirvWrapReplacedSources}")
set_property(TARGET SPIRV-Tools-opt PROPERTY ANYPS5_WRAP_KILL_SCAN_PATCHED TRUE)
