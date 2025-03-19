//===---- RingOS.cpp - RingOS ToolChain Implementation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RingOS.h"
#include "CommonArgs.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <string>

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

void tools::ringos::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                         const InputInfo &Output,
                                         const InputInfoList &Inputs,
                                         const ArgList &Args,
                                         const char *LinkingOutput) const {
  const auto &TC = getToolChain();
  const auto &D = TC.getDriver();
  ArgStringList CmdArgs;

  if (!D.SysRoot.empty())
    CmdArgs.push_back(Args.MakeArgString("--sysroot=" + D.SysRoot));

  // Only static linking is supported.
  CmdArgs.push_back("-static");

  // ??
  CmdArgs.push_back("--eh-frame-hdr");

  assert((Output.isFilename() || Output.isNothing()) && "Invalid output.");
  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  }

  // ??
  CmdArgs.push_back("-z");
  CmdArgs.push_back("pack-relative-relocs");

  bool HasNoStdLib = Args.hasArg(options::OPT_nostdlib, options::OPT_r);
  bool HasNoStdLibXX = Args.hasArg(options::OPT_nostdlibxx);
  bool HasNoLibC = Args.hasArg(options::OPT_nolibc);
  bool HasNoDefaultLibs = Args.hasArg(options::OPT_nodefaultlibs);

  bool ShouldLinkCompilerRuntime = !HasNoDefaultLibs && !HasNoStdLib;
  bool ShouldLinkLibC = !HasNoLibC && !HasNoStdLib && !HasNoDefaultLibs;
  bool ShouldLinkLibCXX =
      D.CCCIsCXX() && !HasNoStdLibXX && !HasNoStdLib && !HasNoDefaultLibs;

  Args.addAllArgs(CmdArgs, {options::OPT_L, options::OPT_u});

  TC.AddFilePathLibArgs(Args, CmdArgs);

  SmallString<128> LibPath(D.Dir);
  llvm::sys::path::append(LibPath, llvm::sys::path::Style::native, "..", "lib");

  CmdArgs.push_back(Args.MakeArgString(Twine("-L") + LibPath));

  if (D.isUsingLTO()) {
    assert(!Inputs.empty() && "Must have at least one input.");
    // Find the first filename InputInfo object.
    auto const *Input = llvm::find_if(
        Inputs, [](const InputInfo &II) -> bool { return II.isFilename(); });
    if (Input == Inputs.end())
      // For a very rare case, all of the inputs to the linker are
      // InputArg. If that happens, just use the first InputInfo.
      Input = Inputs.begin();

    addLTOOptions(TC, Args, CmdArgs, Output, *Input,
                  D.getLTOMode() == LTOK_Thin);
  }

  Args.addAllArgs(CmdArgs, {options::OPT_T_Group, options::OPT_s,
                            options::OPT_t, options::OPT_r});

  addLinkerCompressDebugSectionsOption(TC, Args, CmdArgs);

  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (ShouldLinkCompilerRuntime) {
    AddRunTimeLibs(TC, D, CmdArgs, Args);

    // We supply our own sanitizer runtimes that output errors to the
    // Kernel debug log as well as stderr.
    // FIXME: Properly port clang/gcc sanitizers and use those instead.
    const SanitizerArgs &Sanitize = TC.getSanitizerArgs(Args);
    if (Sanitize.needsUbsanRt())
      CmdArgs.push_back("-lubsan");
  }

  // Silence warnings when linking C code with a C++ '-stdlib' argument.
  Args.ClaimAllArgs(options::OPT_stdlib_EQ);

  if (ShouldLinkLibCXX) {
    CmdArgs.push_back(Args.MakeArgString(LibPath + Twine("/libc++.a")));
    CmdArgs.push_back(Args.MakeArgString(LibPath + Twine("/libc++abi.a")));
  }

  if (ShouldLinkLibC) {
    CmdArgs.push_back(Args.MakeArgString(LibPath + Twine("/libc.a")));
  }

  const char *Exec = Args.MakeArgString(TC.GetLinkerPath());
  C.addCommand(std::make_unique<Command>(JA, *this,
                                         ResponseFileSupport::AtFileCurCP(),
                                         Exec, CmdArgs, Inputs, Output));
}

RingOS::RingOS(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {}

Tool *RingOS::buildLinker() const { return new tools::ringos::Linker(*this); }

void RingOS::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                       ArgStringList &CC1Args) const {
  const Driver &D = getDriver();

  if (DriverArgs.hasArg(options::OPT_nostdinc)) {
    return;
  }
  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    addSystemInclude(DriverArgs, CC1Args, concat(D.ResourceDir, "include"));
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc)) {
    return;
  }

  addSystemInclude(DriverArgs, CC1Args, concat(D.Dir, "..", "include"));
}
